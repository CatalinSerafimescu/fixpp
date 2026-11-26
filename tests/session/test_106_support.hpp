// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_support.hpp — test-only helpers shared by 106-logon-guards-write-bounds's
// own test files (test_106_*.cpp), in the shape of the 104 and 105 feature headers (tasks.md T005).
//
// What it holds: the instants of research D9 ("Instants") computed independently of the
// production helpers they check; the reuse of 105's direct rig, 103's resend-gap scene, the
// blocking-write transport, 105's store that holds a call and its log capture, the plain Engine rig
// PreActive105 uses, and the 104 Logon rigs' helpers; a counter of Disconnected entries; the
// record matchers for the give-up's slot-74 record and the write-timeout record; a TCP listener
// that accepts and never reads nor speaks TLS; a TCP client that never writes and observes what
// it reads and the end of its read.
//
// CONDITIONS this header keeps, checked by reading:
//   - it names no production symbol 106 adds, so it compiles on the base (tasks.md, Execution
//     rules, "[seam: Mn] cells"); the write-timeout record is matched by its format string, which
//     is a literal here, never by a production constant;
//   - an instant is computed here from its definition, never read from the production helper it
//     checks (detail::held_test_request_cap among them), so a mutant of that helper cannot move
//     both sides of a comparison;
//   - no helper reports with ASSERT_*: a miss is an ADD_FAILURE plus a false or empty return, so a
//     helper called inside an Engine window never returns early past the engine's stop;
//   - no helper pumps an io_context: the socket helpers start operations only, and a cell waits
//     through tests/support/pump_until_ready.hpp's readiness-checked helpers.
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fixpp/core/clock.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "session/test_104_support.hpp"
#include "session/test_105_direct_rig.hpp"
#include "session/test_105_support.hpp"
#include "support/blocking_write_transport.hpp"
#include "support/extract_tag.hpp"
#include "support/log_capture.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace fixpp::test_support::t106 {

namespace rgs = fixpp::test_support::resend_gap;
namespace pr = fixpp::test_support::plain_rig;
namespace bwt = fixpp::session::test;
using DirectRig = fixpp::test_support::t105::DirectRig;
using GateStoreFactory = fixpp::test_support::t105::GateStoreFactory;
using LogCapture = fixpp::test_support::LogCapture;
using steady_time_point = fixpp::core::steady_time_point;

// ── Instants (research D9, "Instants"; data-model E-4) ────────────────────────────────────────
//
// Every sum saturates at the steady clock's maximum, as the production instants do (data-model
// E-4), so a cell at a large L or S reads the saturated instant rather than an overflow.

[[nodiscard]] constexpr steady_time_point saturating_add(steady_time_point t,
                                                         std::chrono::nanoseconds d) noexcept {
    if (d.count() <= 0) return t + d;
    auto const room = steady_time_point::max() - t;
    if (room <= d) return steady_time_point::max();
    return t + d;
}

// The one-nanosecond guard of the give-up instants.
inline constexpr std::chrono::nanoseconds kInstantGuard{1};

// G for an emit not returned, or held: L + 3T + 1 ns, L the last inbound frame's instant
// (last_inbound_steady_) and T the TestRequest threshold.
[[nodiscard]] constexpr steady_time_point give_up_instant_emit_not_returned(
    steady_time_point last_inbound, std::chrono::nanoseconds threshold) noexcept {
    return saturating_add(saturating_add(last_inbound, threshold * 3), kInstantGuard);
}

// G for a written TestRequest (105): max(L + 2T, tr_written_at + T) + 1 ns.
[[nodiscard]] constexpr steady_time_point give_up_instant_written(
    steady_time_point last_inbound, std::chrono::nanoseconds threshold,
    steady_time_point tr_written_at) noexcept {
    auto const from_inbound = saturating_add(last_inbound, threshold * 2);
    auto const from_write = saturating_add(tr_written_at, threshold);
    return saturating_add(std::max(from_inbound, from_write), kInstantGuard);
}

// ── State entries (FR-017, contract C-4.6) ────────────────────────────────────────────────────

// The entries of state st in the session's state history. A cell reads it before and after the
// ending and asserts the difference.
[[nodiscard]] inline std::size_t count_state(fixpp::session::Session const& s,
                                             fixpp::session::fsm_state st) noexcept {
    auto const history = s.fsm_visit_history();
    return static_cast<std::size_t>(std::count(history.begin(), history.end(), st));
}
// The Disconnected entries.
[[nodiscard]] inline std::size_t count_disconnected(fixpp::session::Session const& s) noexcept {
    return count_state(s, fixpp::session::fsm_state::Disconnected);
}

// ── Records ───────────────────────────────────────────────────────────────────────────────────

// The give-up's slot-74 record (105, B-105-3), unchanged by 106 (data-model E-8).
inline constexpr char kGiveUpRecordFormat[] =
    "session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}";
// The write limit's record (data-model E-8). Spelled out here, not taken from the production
// call site, so a reworded record turns the matcher's cells RED.
inline constexpr char kWriteTimeoutRecordFormat[] =
    "session ended: write timed out: limit_ms={} error={}";

// ── Sockets for the handshake cells (research D9 rows FR-013 to FR-014) ───────────────────────

// A TCP listener on 127.0.0.1 that accepts every connection and never reads, writes or speaks
// TLS, so a TLS client's handshake against it can only end at the client's own bound. The
// accepted sockets are kept open until the listener is destroyed.
class SilentListener {
public:
    explicit SilentListener(asio::io_context& ioc)
        : acceptor_{ioc, asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), 0}} {
        accept_next_();
    }
    SilentListener(SilentListener const&) = delete;
    SilentListener& operator=(SilentListener const&) = delete;
    ~SilentListener() {
        std::error_code ec;
        acceptor_.close(ec);
        for (auto& s : accepted_) s->close(ec);
    }

    [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
    [[nodiscard]] std::size_t accepted() const noexcept { return accepted_.size(); }

private:
    void accept_next_() {
        auto sock = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
        acceptor_.async_accept(*sock, [this, sock](std::error_code ec) {
            if (ec) return;  // the listener closed
            accepted_.push_back(sock);
            accept_next_();
        });
    }

    asio::ip::tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<asio::ip::tcp::socket>> accepted_;
};

// What a peer read on its connection: the bytes, and how and when the read ended. The state is
// shared with the pending read, so a read that completes after the observer is destroyed writes
// into live storage (a completed handler cannot be un-queued by a cancel).
struct peer_read_state {
    std::string bytes;
    bool ended = false;
    std::error_code end_code;
    std::optional<std::chrono::steady_clock::time_point> ended_at;
    [[nodiscard]] bool eof() const noexcept { return ended && end_code == asio::error::eof; }
};

// A TCP client that connects to 127.0.0.1:port, never writes (so it never starts a TLS
// handshake), and reads until the connection ends, recording what it read and the wall-clock
// instant of the end. start() begins the connect; a cell pumps until state->ended.
class SilentClient {
public:
    explicit SilentClient(asio::io_context& ioc)
        : socket_{std::make_shared<asio::ip::tcp::socket>(ioc)} {}
    SilentClient(SilentClient const&) = delete;
    SilentClient& operator=(SilentClient const&) = delete;
    ~SilentClient() {
        std::error_code ec;
        socket_->close(ec);
    }

    void start(std::uint16_t port) {
        auto const st = state;
        auto const sock = socket_;
        sock->async_connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
                            [st, sock](std::error_code ec) {
                                if (ec) {
                                    finish(*st, ec);
                                    return;
                                }
                                read_more(st, sock);
                            });
    }

    std::shared_ptr<peer_read_state> state = std::make_shared<peer_read_state>();

private:
    static void finish(peer_read_state& st, std::error_code ec) {
        st.ended = true;
        st.end_code = ec;
        st.ended_at = std::chrono::steady_clock::now();
    }
    static void read_more(std::shared_ptr<peer_read_state> const& st,
                          std::shared_ptr<asio::ip::tcp::socket> const& sock) {
        auto buf = std::make_shared<std::array<char, 4096>>();
        sock->async_read_some(asio::buffer(*buf),
                              [st, sock, buf](std::error_code ec, std::size_t n) {
                                  st->bytes.append(buf->data(), n);
                                  if (ec) {
                                      finish(*st, ec);
                                      return;
                                  }
                                  read_more(st, sock);
                              });
    }

    std::shared_ptr<asio::ip::tcp::socket> socket_;
};

// ── The give-up cells' rig (tasks.md T071 to T075; research D9 rows FR-008, FR-009, FR-017) ──────
//
// A direct Session (t105::DirectRig: a BlockingWriteTransport whose next write can be held, the
// mock clock, both roles) with HeartBtInt H, test_request_threshold T and
// logout_disconnect_timeout_ms B, write_timeout_ms unset unless a cell's tweak sets it (the write
// limit cells of test_106_seams.cpp), and a LogCapture. Every instant is
// reached with the mock clock's step_to and a drain, never a coarser advance.
//
// CONDITIONS this rig keeps, checked by reading:
//   - the log is read through records_now(), which flushes the logger without shutting it down,
//     so a record absent at one instant and present at a later one are both readable;
//   - teardown releases every held write before the Scene closes the session.

// The records of one format the logger has emitted so far, read after a flush (not a shutdown),
// so the capture keeps recording. A flush that does not complete inside its wall budget is a
// failure and reads as no record.
[[nodiscard]] inline std::size_t records_now(LogCapture& cap, std::uint32_t format_id) {
    auto const recs = cap.flushed_records_of(format_id);
    if (!recs) {
        ADD_FAILURE() << "records_now: the logger's flush did not complete";
        return 0;
    }
    return recs->size();
}
[[nodiscard]] inline std::size_t give_up_records_now(LogCapture& cap) {
    return records_now(cap, FIXPP_FORMAT_ID(kGiveUpRecordFormat));
}
// The give-up record's test_req_id argument (its first), as 105's cells read it.
[[nodiscard]] inline std::string give_up_record_test_req_id(LogCapture& cap) {
    auto const recs = cap.flushed_records_of(FIXPP_FORMAT_ID(kGiveUpRecordFormat));
    if (!recs) return "<no flush>";
    if (recs->empty()) return "<no record>";
    auto const& a = recs->front().args[0];
    if (a.kind == fixpp::log::ArgValue::Kind::inline_str) return std::string{a.inl.data, a.inl.len};
    if (a.kind == fixpp::log::ArgValue::Kind::static_str && a.static_ptr) return a.static_ptr;
    return {};
}

struct GiveUpParams {
    std::chrono::seconds heartbeat{10};
    // Unset: the session takes its default threshold.
    std::optional<std::chrono::milliseconds> threshold{std::chrono::milliseconds{10000}};
    std::uint32_t logout_ms = 500;
};

// The session's application is DirectRig's (t105::DirectApp, `app`), whose knobs a cell sets
// before the frames that read them, so no cell rebuilds the session. A setting this header cannot
// name (a SessionConfig member 106 adds) comes through `tweak`; an EngineConfig change through
// `shape_scene`, before the session is built.
class GiveUpRig {
public:
    LogCapture log;
    GiveUpParams p;
    DirectRig d;
    std::shared_ptr<fixpp::test_support::t105::DirectApp> app = d.app;

    GiveUpRig(fixpp::session::session_role role, GiveUpParams params,
              std::function<void(fixpp::session::SessionConfig&)> const& tweak = {},
              std::function<void(rgs::Scene&)> const& shape_scene = {})
        : p{params},
          d{role,
            [this, &tweak](fixpp::session::SessionConfig& c) {
                c.heartbeat_interval = p.heartbeat;
                if (p.threshold) c.test_request_threshold = *p.threshold;
                c.logout_disconnect_timeout_ms = p.logout_ms;
                c.logger_override = log.logger;
                if (tweak) tweak(c);
            },
            true, shape_scene} {}
    GiveUpRig(GiveUpRig const&) = delete;
    GiveUpRig& operator=(GiveUpRig const&) = delete;
    ~GiveUpRig() {
        d.release();
        d.s.drain();
    }

    fixpp::session::Session& session() { return *d.s.session; }
    [[nodiscard]] steady_time_point now() const { return d.s.mock->steady_now(); }
    [[nodiscard]] steady_time_point last_inbound() const {
        return fixpp::session::session_test_access::last_inbound_steady(*d.s.session);
    }
    // The mock clock stepped to t, then every handler that became ready run.
    void at(steady_time_point t) {
        // Staging (ci/mock-clock-staging-sweep.sh): KIND D. The sleepers a cell waits on here are
        // armed to instants computed from stored anchors, never from the clock's reading when they
        // arm, and mock_clock::sleep_until fires at once for a deadline already passed, so a sleep
        // armed after this step completes in the drain() below.
        d.s.mock->step_to(t);
        d.s.drain();
    }
    // T as set (the cells that read it set it; zero when the session takes its default).
    [[nodiscard]] std::chrono::milliseconds threshold() const {
        return p.threshold.value_or(std::chrono::milliseconds::zero());
    }
    // The write of the next frame of MsgType type is held: armed at its toAdmin.
    void hold_write_of(std::string type) { d.hold_write_of(std::move(type)); }
    [[nodiscard]] std::size_t records() { return give_up_records_now(log); }
    [[nodiscard]] std::size_t write_records() {
        return records_now(log, FIXPP_FORMAT_ID(kWriteTimeoutRecordFormat));
    }
    [[nodiscard]] std::size_t disconnected() const { return count_disconnected(*d.s.session); }
    // An application send, started and not awaited.
    std::future<fixpp::core::expected_t<void>> spawn_send(std::string_view clordid) {
        payloads_.push_back(
            "35=D\x01"
            "11=" +
            std::string{clordid} + "\x01");
        auto const& pl = payloads_.back();
        return asio::co_spawn(fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
                              d.s.session->send(std::as_bytes(std::span{pl.data(), pl.size()})),
                              asio::use_future);
    }
    template <class Awaitable>
    std::future<fixpp::core::expected_t<void>> spawn(Awaitable a) {
        return asio::co_spawn(fixpp::test_support::session_executor_of(*d.s.session, d.s.ioc),
                              std::move(a), asio::use_future);
    }
    [[nodiscard]] std::size_t completed_since(std::string_view type, std::size_t from) const {
        std::size_t n = 0;
        for (std::size_t i = from; i < d.bw->recorded.size(); ++i) {
            auto const& w = d.bw->recorded[i];
            if (w.completed && fixpp::test_support::extract_tag(w.bytes, 35) == type) ++n;
        }
        return n;
    }
    // The MsgType(35) of each completed write from index from, in write-start order.
    [[nodiscard]] std::vector<std::string> completed_types(std::size_t from = 0) const {
        std::vector<std::string> out;
        for (std::size_t i = from; i < d.bw->recorded.size(); ++i) {
            auto const& w = d.bw->recorded[i];
            if (w.completed) out.emplace_back(fixpp::test_support::extract_tag(w.bytes, 35));
        }
        return out;
    }
    [[nodiscard]] std::string completed_field(std::string_view type, int tag) const {
        for (auto const& w : d.bw->recorded) {
            if (w.completed && fixpp::test_support::extract_tag(w.bytes, 35) == type) {
                return std::string{fixpp::test_support::extract_tag(w.bytes, tag)};
            }
        }
        return "<none>";
    }

private:
    std::deque<std::string> payloads_;  // the spawned sends' payloads, alive with the rig
};

}  // namespace fixpp::test_support::t106
