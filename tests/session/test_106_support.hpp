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
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/clock.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "session/test_104_support.hpp"
#include "session/test_105_direct_rig.hpp"
#include "session/test_105_support.hpp"
#include "support/blocking_write_transport.hpp"
#include "support/log_capture.hpp"

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

// A write's cut instant: S + W, S the write's start and W write_timeout_ms.
[[nodiscard]] constexpr steady_time_point write_limit_instant(
    steady_time_point write_start, std::chrono::milliseconds write_timeout) noexcept {
    return saturating_add(write_start, write_timeout);
}

// ── Disconnected entries (FR-017, contract C-4.6) ─────────────────────────────────────────────

// The Disconnected entries in the session's state history. A cell reads it before and after the
// ending and asserts the difference.
[[nodiscard]] inline std::size_t count_disconnected(fixpp::session::Session const& s) noexcept {
    auto const history = s.fsm_visit_history();
    return static_cast<std::size_t>(std::count(history.begin(), history.end(),
                                               fixpp::session::fsm_state::Disconnected));
}

// ── Records ───────────────────────────────────────────────────────────────────────────────────

// The give-up's slot-74 record (105, B-105-3), unchanged by 106 (data-model E-8).
inline constexpr char kGiveUpRecordFormat[] =
    "session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}";
// The write limit's record (data-model E-8). Spelled out here, not taken from the production
// call site, so a reworded record turns the matcher's cells RED.
inline constexpr char kWriteTimeoutRecordFormat[] =
    "session ended: write timed out: limit_ms={} error={}";

[[nodiscard]] inline std::size_t give_up_records(LogCapture& cap) {
    return cap.records_of(FIXPP_FORMAT_ID(kGiveUpRecordFormat)).size();
}
[[nodiscard]] inline std::size_t write_timeout_records(LogCapture& cap) {
    return cap.records_of(FIXPP_FORMAT_ID(kWriteTimeoutRecordFormat)).size();
}

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
    explicit SilentClient(asio::io_context& ioc) : socket_{std::make_shared<asio::ip::tcp::socket>(ioc)} {}
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
        sock->async_read_some(asio::buffer(*buf), [st, sock, buf](std::error_code ec, std::size_t n) {
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

}  // namespace fixpp::test_support::t106
