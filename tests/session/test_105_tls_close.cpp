// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_tls_close.cpp — 105-transport-liveness: the close is orderly on TLS
// (tasks.md T024; contract C-8.1; FR-002).
//
// The rig (research D10):
//   - the Engine over TLS, tests/session/engine_loopback_harness.hpp, acceptor role (the
//     registered initiator's endpoint is a placeholder): the peer is test_105_support.hpp's
//     TlsPeer::connect, an mTLS client of the acceptor's listener. The pump consumes the
//     ending, so every class runs here.
//   - a direct Session over TLS (the DirectTlsCell section below), for the classes that close at
//     a root's exit.
// The oracle: the peer reads the frames the class writes, then a close_notify (its read ends
// with transport_read_eof), within the wall cap with the mock clock still. The wall-clock
// timers: the TLS handshake timeout (the handshake completes before any cell's instant),
// tls_close_timeout (the close_notify is written at once when no SSL operation is suspended
// past close_async's quiesce), and close()'s grace timer, which no cell reaches.
//
// No ASSERT_* runs between an engine's start() and its stop(), which engine_stop_guard owns.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "session/engine_loopback_harness.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "session/test_105_support.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_executor_of.hpp"
#include "transport/asio_tls_transport.hpp"

// The friend src/transport/asio_tls_transport.hpp declares, for the write-limit cell's socket
// option; the same definition as tests/session/test_engine_session_strand.cpp's, in another binary.
namespace fixpp::transport {
class asio_tls_transport_test_access {
public:
    static asio::ip::tcp::socket& socket_of(asio_tls_transport& t) noexcept { return t.socket_; }
};
}  // namespace fixpp::transport

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::SessionConfig;
namespace pr = fixpp::test_support::plain_rig;
namespace t105 = fixpp::test_support::t105;

constexpr std::chrono::milliseconds kWallCap{3000};
constexpr std::chrono::seconds kHb{10};

std::string stamp(fixpp::core::mock_clock const& clock) {
    std::array<char, 32> buf{};
    auto const r = fixpp::core::utc_time_to_fix_string(
        clock.now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

// The Engine-over-TLS cell: the harness's acceptor with the cell's edits, the mock clock as the
// engine clock, and the peer connected once the listener is bound.
struct TlsEngineCell {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock = t105::make_mock_clock(ioc.get_executor());
    std::unique_ptr<fixpp::test_support::EngineLoopbackHarness> h;
    std::shared_ptr<t105::TlsPeer> peer;

    // False when the harness could not be built (no fixture directory): the caller skips.
    [[nodiscard]] bool build(std::function<void(SessionConfig&)> const& tweak) {
        fixpp::core::EngineConfig ec;
        ec.executor = ioc.get_executor();
        ec.clock = clock;
        h = fixpp::test_support::EngineLoopbackHarness::build(
            ioc.get_executor(), std::move(ec), true, [&](SessionConfig& c) {
                fixpp::session::CompIdAuthorizationPolicy authz;
                authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");
                c.compid_authorization_policy = authz;
                if (tweak) tweak(c);
            });
        return h != nullptr;
    }
    [[nodiscard]] bool start_and_connect() {
        if (!h->engine().start().has_value()) return false;
        if (!fixpp::test_support::pump_until(
                ioc, [&] { return h->server_endpoint().port != 0; }, 5s, 5ms,
                "TlsEngineCell::start_and_connect/bind")) {
            return false;
        }
        peer = t105::TlsPeer::connect(ioc, h->transport_fixture(), h->server_endpoint());
        return fixpp::test_support::pump_until(
                   ioc, [&] { return peer->ready || peer->setup_failed; }, 5s, 5ms,
                   "TlsEngineCell::start_and_connect/connect") &&
               peer->ready;
    }
    [[nodiscard]] std::string msg(std::string_view type, unsigned seq,
                                  std::string_view extra = {}) {
        return pr::message("FIX.4.2", type, seq, "INITIATOR", "ACCEPTOR", stamp(*clock), extra);
    }
    [[nodiscard]] fsm_state state() {
        auto s = h->engine().lookup(h->acceptor_id());
        return s ? s->state() : fsm_state::NotConnected;
    }
    [[nodiscard]] bool logon(std::string_view hb = "30") {
        peer->send(msg("A", 1,
                       "98=0\x01"
                       "108=" +
                           std::string{hb} + "\x01"));
        return fixpp::test_support::pump_until(
            ioc, [&] { return state() == fsm_state::Active; }, 5s, 5ms,
            "TlsEngineCell::logon/logon");
    }
    [[nodiscard]] bool close_notify_at_once(const char* site) {
        bool const ended =
            t105::pump_until_peer_read_ends(ioc, [&] { return peer->read_ended; }, kWallCap, site);
        return ended && peer->ending() == t105::peer_ending::close_notify;
    }
};

#define FIXPP_105_TLS_ENGINE_CELL(cell, tweak)                                 \
    TlsEngineCell cell;                                                        \
    if (!(cell).build(tweak)) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set"; \
    fixpp::test_support::engine_stop_guard stop_guard_ { *(cell).h, (cell).ioc }

TEST(TlsClose105, PumpSilentEnding_PeerReceivesCloseNotify) {
    FIXPP_105_TLS_ENGINE_CELL(c, nullptr);
    bool const up = c.start_and_connect() && c.logon();
    bool closed = false;
    if (up) {
        c.peer->send(pr::message("FIX.4.4", "0", 2, "INITIATOR", "ACCEPTOR", stamp(*c.clock)));
        closed = c.close_notify_at_once("TlsClose105.PumpSilentEnding");
    }
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(closed) << "no close_notify at once; ending "
                        << t105::peer_ending_name(c.peer ? c.peer->ending()
                                                         : t105::peer_ending::open);
}

// The liveness loop's give-up while the pump is parked in a TLS read (B&L B-348-1's case).
TEST(TlsClose105, GiveUpWhilePumpParkedInRead_PeerReceivesCloseNotify) {
    FIXPP_105_TLS_ENGINE_CELL(c, [](SessionConfig& s) {
        s.heartbeat_interval = kHb;
        s.test_request_threshold = std::chrono::milliseconds{kHb};
    });
    bool const up = c.start_and_connect() && c.logon("10");
    bool closed = false;
    bool tr = false;
    if (up) {
        auto const read = t105::advance_until_msg_type(
            *c.clock, c.ioc, [&] { return c.peer->received; }, "1", 0, 1s,
            t105::liveness_wire_cap(kHb, 1s), "TlsClose105.GiveUp: the TestRequest");
        tr = read.has_value();
        std::chrono::nanoseconds advanced{0};
        while (c.state() == fsm_state::Active && advanced < t105::liveness_wire_cap(kHb, 1s)) {
            // Staging (ci/mock-clock-staging-sweep.sh): KIND D, the give-up instant is computed
            // from stored anchors (the last inbound frame, the TestRequest's write); each step is
            // followed by a bounded drive, and close_notify_at_once's cap makes a miss a named
            // failure.
            c.clock->advance(1s);
            advanced += 1s;
            (void)t105::drive_bounded(c.ioc, 64);
        }
        closed = c.close_notify_at_once("TlsClose105.GiveUp");
    }
    EXPECT_TRUE(up && tr) << "setup";
    EXPECT_EQ(pr::frames_of_type(c.peer ? c.peer->received : std::string{}, "5").size(), 1U)
        << "the give-up's Logout";
    EXPECT_TRUE(closed) << "no close_notify after the give-up";
}

// A refusal before Active that sends nothing: the acceptor's first frame is a faulty Logon
// (092's D-1).
TEST(TlsClose105, PreActiveSilentRefusal_PeerReceivesCloseNotify) {
    FIXPP_105_TLS_ENGINE_CELL(c, nullptr);
    bool const up = c.start_and_connect();
    bool closed = false;
    if (up) {
        c.peer->send(c.msg("A", 1,
                           "98=0\x01"
                           "108=30\x01"
                           "9x9=1\x01"));
        closed = c.close_notify_at_once("TlsClose105.PreActiveSilentRefusal");
    }
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(closed) << "no close_notify at once after the refusal";
}

TEST(TlsClose105, PeerLogoutReply_PeerReceivesCloseNotify) {
    FIXPP_105_TLS_ENGINE_CELL(c, nullptr);
    bool const up = c.start_and_connect() && c.logon();
    bool closed = false;
    std::size_t mark = 0;
    if (up) {
        mark = c.peer->received.size();
        c.peer->send(c.msg("5", 2));
        closed = c.close_notify_at_once("TlsClose105.PeerLogoutReply");
    }
    EXPECT_TRUE(up) << "setup";
    EXPECT_EQ(
        pr::frames_of_type(c.peer ? c.peer->received.substr(mark) : std::string{}, "5").size(), 1U)
        << "the reply Logout";
    EXPECT_TRUE(closed) << "no close_notify after the reply";
}

// ════════════════════════════════════════════════════════════════════════════════════════
// The direct-Session TLS rig (C-8.1 for the classes that close at a root's exit): a
// resend_gap::Scene's Session as a standalone initiator whose connection drive_reconnect() makes
// through a real TLS transport factory to a LoopbackTlsFixture listener (the factory and the
// fixture's certificates as tests/session/loopback_tls_session_harness.hpp builds them), whose
// server end is TlsPeer::accept. The Session has no pump: inbound frames reach it through
// on_inbound_frame (Scene::feed). The handshake completes only while the server end runs its
// own, so the peer is accepted before drive_reconnect() is spawned. The caller's reader that a
// standalone initiator needs (B-103-9) is reader(): it reads the installed transport and discards
// what it reads, so the TLS records the server end sends after the handshake are consumed, as a
// caller's reader consumes them; a socket closed with unread bytes is reset, which the peer
// would read as an abortive ending whatever the session's close. The wall-clock timers: the
// TLS handshake and tls_close_timeout, both met at once on loopback; the mock clock moves only
// where a cell moves it.
// ════════════════════════════════════════════════════════════════════════════════════════

namespace rg = fixpp::test_support::resend_gap;

struct DirectTlsCell {
    rg::Scene s;
    std::unique_ptr<fixpp::transport::test::LoopbackTlsFixture> fx;
    std::shared_ptr<t105::TlsPeer> peer;

    // False when the fixture directory is not set: the caller skips.
    [[nodiscard]] bool build(std::function<void(SessionConfig&)> const& tweak) {
        char const* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
        std::string const dir = env != nullptr ? env : FIXPP_TLS_FIXTURE_DIR;
        if (dir.empty()) return false;
        fx =
            std::make_unique<fixpp::transport::test::LoopbackTlsFixture>(dir, s.ioc.get_executor());
        fixpp::tls::file_cert_source::Config cs;
        cs.leaf_path = dir + "/leaf_rsa2048.pem";
        cs.private_key_path = dir + "/leaf_rsa2048.key";
        cs.ca_bundle_path = dir + "/ca.pem";
        auto source = fixpp::tls::file_cert_source::make_file_cert_source(
            cs, std::pmr::new_delete_resource());
        if (!source.has_value()) return false;
        fixpp::tls::SslCtxConfig ssl;
        ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
        ssl.cs = std::move(*source);
        ssl.clock = nullptr;
        ssl.caps = fixpp::tls::CertSourceCaps{};
        auto factory = fixpp::transport::make_asio_tls_transport_factory(
            fixpp::transport::Transport::Config{}, ssl);
        if (!factory.has_value()) return false;
        s.cfg.transport_factory_override =
            std::shared_ptr<fixpp::transport::TransportFactory>{std::move(*factory)};
        s.cfg.reconnect_endpoint = fx->server_endpoint();
        s.cfg.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::one_way_ca};
        if (tweak) tweak(s.cfg);
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
        return true;
    }

    template <class Awaitable>
    [[nodiscard]] std::optional<fixpp::core::expected_t<void>> run(Awaitable a, const char* site) {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  std::move(a), asio::use_future);
        if (!fixpp::test_support::pump_until(
                s.ioc, [&] { return rg::Scene::ready(fut); }, 5s, 5ms, site)) {
            return std::nullopt;
        }
        return fut.get();
    }

    // open(), the server end accepted, drive_reconnect(), the Logon read by the peer, then the
    // peer's Logon fed: Active.
    [[nodiscard]] bool to_active() {
        auto const opened = run(s.session->open(), "DirectTlsCell::to_active/open");
        if (!opened || !opened->has_value()) return false;
        peer = t105::TlsPeer::accept(s.ioc, *fx);
        auto const connected =
            run(s.session->drive_reconnect(), "DirectTlsCell::to_active/drive_reconnect");
        if (!connected || !connected->has_value()) return false;
        asio::co_spawn(s.ioc, reader(&s.session->live_transport()), asio::detached);
        if (!fixpp::test_support::pump_until(
                s.ioc,
                [&] { return peer->ready && !pr::frames_of_type(peer->received, "A").empty(); }, 5s,
                5ms, "the Logon at the peer")) {
            return false;
        }
        (void)s.feed(s.logon(1));
        return s.state() == fsm_state::Active;
    }

    [[nodiscard]] bool close_notify_at_once(const char* site) {
        bool const ended = t105::pump_until_peer_read_ends(
            s.ioc, [&] { return peer && peer->read_ended; }, kWallCap, site);
        return ended && peer->ending() == t105::peer_ending::close_notify;
    }

    // The caller's reader: reads until its read fails (the transport closed).
    static asio::awaitable<void> reader(fixpp::transport::Transport* t) {
        std::array<std::byte, 4096> buf{};
        for (;;) {
            auto const r = co_await t->async_read_some(std::span<std::byte>{buf});
            if (!r.has_value()) co_return;
        }
    }
};

#define FIXPP_105_DIRECT_TLS_CELL(cell, tweak) \
    DirectTlsCell cell;                        \
    if (!(cell).build(tweak)) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set"

// The liveness loop's give-up (R-1, class D) on a direct Session: the Logout, then the close at
// the loop's exit, a close_notify.
TEST(TlsClose105, DirectSessionGiveUp_PeerReceivesCloseNotify) {
    FIXPP_105_DIRECT_TLS_CELL(c, [](SessionConfig& s) {
        s.heartbeat_interval = kHb;
        s.test_request_threshold = std::chrono::milliseconds{kHb};
    });
    bool const up = c.to_active();
    bool tr = false;
    bool ended = false;
    bool closed = false;
    std::size_t mark = 0;
    if (up) {
        mark = c.peer->received.size();
        auto const read = t105::advance_until_msg_type(
            *c.s.mock, c.s.ioc, [&] { return c.peer->received; }, "1", 0, 1s,
            t105::liveness_wire_cap(kHb, 1s), "TlsClose105.DirectSessionGiveUp: the TestRequest");
        tr = read.has_value();
        std::chrono::nanoseconds advanced{0};
        while (c.s.state() == fsm_state::Active && advanced < t105::liveness_wire_cap(kHb, 1s)) {
            // Staging (ci/mock-clock-staging-sweep.sh): KIND D, the give-up instant is computed
            // from stored anchors (the last inbound frame, the TestRequest's write); each step is
            // followed by a bounded drive, and the state after the loop's cap is asserted.
            c.s.mock->advance(1s);
            advanced += 1s;
            (void)t105::drive_bounded(c.s.ioc, 64);
        }
        ended = c.s.state() == fsm_state::Disconnected;
        closed = c.close_notify_at_once("TlsClose105.DirectSessionGiveUp");
    }
    EXPECT_TRUE(up && tr) << "setup";
    EXPECT_TRUE(ended) << "setup: the give-up did not end the session";
    EXPECT_EQ(
        pr::frames_of_type(c.peer ? c.peer->received.substr(mark) : std::string{}, "5").size(), 1U)
        << "the give-up's Logout";
    EXPECT_TRUE(closed) << "no close_notify after the give-up; ending "
                        << t105::peer_ending_name(c.peer ? c.peer->ending()
                                                         : t105::peer_ending::open)
                        << ", read error "
                        << (c.peer && c.peer->read_error ? static_cast<int>(*c.peer->read_error)
                                                         : -1);
}

// Session::send whose store call fails on a persistent store (class A): nothing transmitted,
// the session ended, and the close at the call's exit, a close_notify.
TEST(TlsClose105, DirectSessionSendStoreFailure_PeerReceivesCloseNotify) {
    auto st = std::make_shared<t105::gate_store_state>();
    FIXPP_105_DIRECT_TLS_CELL(c, [st](SessionConfig& s) {
        s.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    });
    bool const up = c.to_active();
    bool failed = false;
    bool closed = false;
    std::size_t mark = 0;
    if (up) {
        mark = c.peer->received.size();
        st->fail_when = [](t105::store_call k, fixpp::session::seqnum_t) {
            return k == t105::store_call::store_outbound;
        };
        static constexpr char kPayload[] = "35=D\x01";
        auto const r =
            c.run(c.s.session->send(std::as_bytes(std::span{kPayload, sizeof(kPayload) - 1})),
                  "TlsClose105::DirectSessionSendStoreFailure/send");
        failed = r.has_value() && !r->has_value();
        closed = c.close_notify_at_once("TlsClose105.DirectSessionSendStoreFailure");
    }
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(failed) << "setup: the send did not fail";
    EXPECT_GE(st->calls_failed, 1U) << "setup: the store call did not fail";
    EXPECT_EQ(c.s.state(), fsm_state::Disconnected) << "setup: the send did not end the session";
    EXPECT_TRUE(
        pr::frames_of_type(c.peer ? c.peer->received.substr(mark) : std::string{}, "D").empty())
        << "the message was transmitted";
    EXPECT_TRUE(closed) << "no close_notify after the send returned; ending "
                        << t105::peer_ending_name(c.peer ? c.peer->ending()
                                                         : t105::peer_ending::open);
}

// ════════════════════════════════════════════════════════════════════════════════════════
// 105 T054 — the TLS arms of US1 part B (contract C-8.4, C-8.5).
// ════════════════════════════════════════════════════════════════════════════════════════

// A graceful close() during the R-2 wait runs on: the wait ends at the bound, on the mock clock,
// and the peer reads close_notify then (C-8.4). logout_disconnect_timeout_ms 30000, so
// close_grace's wall time (30 s) is far above the wall cap; the mock clock reaches the bound.
TEST(TlsClose105, CloseGracefulDuringWait_CloseNotifyAtTheBound) {
    FIXPP_105_TLS_ENGINE_CELL(c, [](SessionConfig& s) { s.logout_disconnect_timeout_ms = 30000; });
    bool const up = c.start_and_connect() && c.logon();
    bool logout = false;
    bool open_before = false;
    bool closed = false;
    if (up) {
        auto const mark = c.peer->received.size();
        c.peer->send(
            pr::message("FIX.4.2", "0", 2, "INITIATOR", "ACCEPTOR", "20230101-00:00:00.000"));
        logout = fixpp::test_support::pump_until(
            c.ioc,
            [&] { return pr::frames_of_type(c.peer->received.substr(mark), "5").size() == 1U; }, 5s,
            5ms, "the ending Logout");
        auto s = c.h->engine().lookup(c.h->acceptor_id());
        if (s) {
            asio::co_spawn(s->executor().underlying(),
                           s->close(fixpp::session::close_mode::graceful), asio::detached);
        }
        (void)t105::drive_bounded(c.ioc, 512);
        c.clock->advance(29999ms);
        (void)t105::drive_bounded(c.ioc, 512);
        open_before = !c.peer->read_ended;
        c.clock->advance(1ms);
        closed = c.close_notify_at_once("TlsClose105.CloseGracefulDuringWait");
    }
    EXPECT_TRUE(up && logout) << "setup";
    EXPECT_TRUE(open_before) << "the connection closed before the bound";
    EXPECT_TRUE(closed) << "no close_notify at the bound; ending "
                        << t105::peer_ending_name(c.peer ? c.peer->ending()
                                                         : t105::peer_ending::open);
}

// Returns the SO_SNDBUF the OS reports for the engine acceptor's accepted TLS socket, or -1 when
// the session, its TLS transport or the option is not there. On Windows it first sets the option
// to windows_bytes. Elsewhere the option keeps the size the OS gives it; the Linux
// CI comparison behind that choice is recorded in
// .specify/decisions/105-transport-liveness-gateb.md (PR #720). The Engine's accept path builds the
// accepted transport's Transport::Config itself (no SessionConfig or EngineConfig field reaches
// tcp_send_buf_bytes there), so the cell reaches the option through the test-access friend.
// Re-check with: grep -n accepted_transport_config src/session/engine.cpp
int session_send_buffer(fixpp::session::Engine& engine, fixpp::session::SessionId const& id,
                        [[maybe_unused]] int windows_bytes) {
    auto s = engine.lookup(id);
    if (!s) return -1;
    auto* tls = dynamic_cast<fixpp::transport::asio_tls_transport*>(&s->live_transport());
    if (tls == nullptr) return -1;
    auto& sock = fixpp::transport::asio_tls_transport_test_access::socket_of(*tls);
    asio::error_code ec;
#if defined(_WIN32)
    sock.set_option(asio::socket_base::send_buffer_size{windows_bytes}, ec);
    if (ec) return -1;
#endif
    asio::socket_base::send_buffer_size got;
    sock.get_option(got, ec);
    return ec ? -1 : got.value();
}

// The write limit on TLS behind a peer that stopped reading (C-8.5, L-105-g): application sends
// fill the window until one is held in its write (holding the write gate); a too-low frame then
// draws the R-2 Logout (Arm B writes no Reject first), which queues behind it. At the bound the
// session closes within the bound plus tls_close_timeout (1 s of wall time), with no Logout
// written. Off Windows close_notify cannot be written behind the held write, so the close is
// abortive; on Windows the ending is printed, not pinned.
// The condition the cell rests on: the held write stays held until the session closes it. The
// session side's SO_SNDBUF is set to a fixed size once the session is Active, on Windows only. A
// held send that completes ok means the window reopened and the R-2 Logout could be written. Off
// Windows the cell then fails as a setup failure. On Windows (owner-approved for Windows only,
// fixpp#722), when the success was first seen while the session was still Active, it prints the
// reading and passes, and the checks that rest on the hold (no Logout read, the ending) are not
// made; a success first seen at or after the session's end counts as held at the bound, and
// those checks are made. The bound's checks (open before the bound, ended by the mock-clock cap,
// the peer's read ended) are made on every platform.
// The bound (logout_disconnect_timeout_ms) is set to the seam twin's: that shortens the wall time
// between the hold and the bound; it does not explain why a window can reopen. A held send seen
// ready prints the mock-clock step and the wall time since the fill loop ended at which it was
// first seen.
// Off Windows the tail's read ends only after the peer has drained what the session queued before
// its close: the close is an ordinary close, with the FIN behind that queue. The read's cap (the
// 5s at its pump) is a wall cap that nothing derives: a drain slower than it fails the cell at the
// read.
// The bound's timing on both sides is pinned by the seam twin,
// TlsWriteLimit105.R2AfterActive_PeerStopsReading_SeamTwin_ClosedAtExactlyTheBound in
// test_105_seams.cpp.
constexpr std::uint32_t kWriteLimitBoundMs = 500;
TEST(TlsWriteLimit105, R2AfterActive_PeerStopsReading_AbortiveAtBoundPlusTlsCloseTimeout) {
    asio::io_context ioc;
    auto clock = t105::make_mock_clock(ioc.get_executor());
    fixpp::core::EngineConfig ec;
    ec.executor = ioc.get_executor();
    ec.clock = clock;
    auto h = fixpp::test_support::EngineLoopbackHarness::build(
        ioc.get_executor(), std::move(ec), true, [](SessionConfig& c) {
            fixpp::session::CompIdAuthorizationPolicy authz;
            authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");
            c.compid_authorization_policy = authz;
            c.logout_disconnect_timeout_ms = kWriteLimitBoundMs;
        });
    if (!h) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    fixpp::test_support::engine_stop_guard stop_guard{*h, ioc};
    char const* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
    std::string const dir = env != nullptr ? env : FIXPP_TLS_FIXTURE_DIR;
    auto state = [&] {
        auto s = h->engine().lookup(h->acceptor_id());
        return s ? s->state() : fsm_state::NotConnected;
    };
    bool up = h->engine().start().has_value() &&
              fixpp::test_support::pump_until(
                  ioc, [&] { return h->server_endpoint().port != 0; }, 5s, 5ms,
                  "TlsWriteLimit105::R2AfterActive_PeerStopsReading_AbortiveAtBound/bind");
    std::shared_ptr<t105::StalledTlsPeer> peer;
    if (up) {
        peer = t105::StalledTlsPeer::connect(ioc, dir, h->server_endpoint(), 1024);
        up = fixpp::test_support::pump_until(
                 ioc, [&] { return peer->ready || peer->setup_failed; }, 5s, 5ms,
                 "TlsWriteLimit105::R2AfterActive_PeerStopsReading_AbortiveAtBound/connect") &&
             peer->ready;
    }
    auto msg = [&](std::string_view type, unsigned seq, std::string_view extra = {}) {
        return pr::message("FIX.4.2", type, seq, "INITIATOR", "ACCEPTOR", stamp(*clock), extra);
    };
    if (up) {
        peer->send(msg("A", 1,
                       "98=0\x01"
                       "108=30\x01"));
        up = fixpp::test_support::pump_until(
            ioc, [&] { return state() == fsm_state::Active; }, 5s, 5ms,
            "TlsWriteLimit105::R2AfterActive_PeerStopsReading_AbortiveAtBound/logon");
    }
    int session_sndbuf = -1;
    if (up) {
        session_sndbuf = session_send_buffer(h->engine(), h->acceptor_id(), 65536);
        up = session_sndbuf > 0;
    }
    if (up) {
        peer->send(msg("0", 2));
        up = fixpp::test_support::pump_until(
            ioc, [&] { return peer->writes_done == 2U; }, 5s, 5ms,
            "TlsWriteLimit105::R2AfterActive_PeerStopsReading_AbortiveAtBound/the Heartbeat at 2");
        (void)t105::drive_bounded(ioc, 256);
    }
    // Fill the window: application sends until one does not complete within a settle.
    bool held = false;
    std::vector<std::future<fixpp::core::expected_t<void>>> sends;
    std::string const payload =
        "35=D\x01"
        "58=" +
        std::string(3000, 'x') + "\x01";
    // Engine::send takes the SessionId by reference and runs after this full expression,
    // so the id lives in a local that outlives every spawned send.
    auto const acceptor_sid = h->acceptor_id();
    for (int i = 0; up && i < 4000 && !held; ++i) {
        sends.push_back(asio::co_spawn(
            ioc,
            h->engine().send(acceptor_sid,
                             std::as_bytes(std::span{payload.data(), payload.size()})),
            asio::use_future));
        (void)fixpp::test_support::pump_until(
            ioc, [&] { return sends.back().wait_for(0s) == std::future_status::ready; }, 50ms, 5ms,
            nullptr);
        held = sends.back().wait_for(0s) != std::future_status::ready;
    }
    auto const fill_ended = std::chrono::steady_clock::now();
    bool open_before = false;
    bool ended = false;
    bool ended_by_bound = false;
    // For the ending's message: the first wait below in which the held send's future was seen
    // ready, and the mock time advanced when the session left Active. A wait's predicate notes
    // it and keeps its own answer, so the waits' lengths are unchanged.
    std::string held_ready_in = "none of the waits";
    std::chrono::milliseconds advanced{0};
    std::chrono::milliseconds const step{50};
    long long held_ready_step = -1;
    long long held_ready_wall_ms = -1;
    bool held_ready_while_active = false;
    auto note_held = [&](const char* phase) {
        if (held_ready_in == "none of the waits" && !sends.empty() &&
            sends.back().wait_for(0s) == std::future_status::ready) {
            held_ready_in = phase;
            held_ready_while_active = state() == fsm_state::Active;
            held_ready_step = advanced / step;
            held_ready_wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - fill_ended)
                                     .count();
        }
    };
    if (up && held) {
        peer->send(msg("0", 2));  // below NextNumIn: the too-low Logout, queued at the gate
        // The session reads behind its own blocked write; real time lets the frame arrive.
        (void)fixpp::test_support::pump_until(
            ioc,
            [&] {
                note_held("the 300 ms wait after the too-low frame");
                return false;
            },
            300ms, 5ms, nullptr);
        open_before = state() == fsm_state::Active;
        // The session reads the frame behind its own blocked write, so the instant its Logout's
        // write starts is not observable from here: the mock clock moves in 50 ms steps, with
        // wall time between them for the reads and for tls_close_timeout, until the session ends,
        // at most the bound plus 1 s of mock time (the bound is logout_disconnect_timeout_ms from
        // the Logout's start; the seam twin in test_105_seams.cpp pins it on both sides).
        auto const mock_cap = std::chrono::milliseconds{kWriteLimitBoundMs} + 1000ms;
        while (state() == fsm_state::Active && advanced < mock_cap) {
            clock->advance(step);
            advanced += step;
            (void)fixpp::test_support::pump_until(
                ioc,
                [&] {
                    note_held("a mock-clock step");
                    return state() != fsm_state::Active;
                },
                60ms, 5ms, nullptr);
        }
        ended_by_bound =
            state() != fsm_state::Active || fixpp::test_support::pump_until(
                                                ioc,
                                                [&] {
                                                    note_held("the wait for the session's end");
                                                    return state() != fsm_state::Active;
                                                },
                                                2500ms, 5ms, nullptr);
        // Past tls_close_timeout before the peer reads, so a close_notify could not be written
        // late into a reopened window.
        (void)fixpp::test_support::pump_until(
            ioc,
            [&] {
                note_held("the 1500 ms wait after the session's end, before the peer reads");
                return false;
            },
            1500ms, 5ms, nullptr);
        peer->start_reading();
        ended = t105::pump_until_peer_read_ends(
            ioc,
            [&] {
                note_held("the peer's read");
                return peer->read_ended;
            },
            5s, "TlsWriteLimit105");
    }
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(held) << "setup: no application send was held behind the stalled peer";
    EXPECT_TRUE(open_before) << "the session ended before the bound";
    EXPECT_TRUE(ended_by_bound)
        << "the session did not end within the mock-clock cap after its Logout";
    EXPECT_TRUE(ended) << "the peer's read did not end within the wall cap after the session's end";
    // What the peer read and what the session was given to write, for the ending's message.
    std::size_t sends_ready = 0;
    for (auto const& f : sends) {
        if (f.valid() && f.wait_for(0s) == std::future_status::ready) ++sends_ready;
    }
    std::string const got = peer ? peer->received : std::string{};
    auto const buffers = peer ? peer->buffer_sizes() : std::pair<int, int>{-1, -1};
    std::error_code const read_ec = peer ? peer->read_ec : std::error_code{};
    std::string held_result = "not ready";
    if (!sends.empty() && sends.back().wait_for(0s) == std::future_status::ready) {
        auto const r = sends.back().get();
        held_result = r.has_value() ? std::string{"ok"}
                                    : "error " + std::to_string(static_cast<int>(r.error()));
    }
    std::ostringstream reading;
    reading << "read_ec " << read_ec.value() << " [" << read_ec.category().name() << "] "
            << read_ec.message() << "; the peer read " << got.size() << " bytes, "
            << pr::frames_of_type(got, "D").size() << " frames of type D, "
            << pr::frames_of_type(got, "5").size() << " of type 5; sends spawned " << sends.size()
            << ", completed " << sends_ready << ", each payload " << payload.size()
            << " bytes; the peer's SO_RCVBUF " << buffers.first << ", SO_SNDBUF " << buffers.second
            << "; the session's SO_SNDBUF " << session_sndbuf << "; the held send completed in "
            << held_ready_in << " (mock-clock step " << held_ready_step << ", "
            << held_ready_wall_ms << " ms of wall time after the fill loop ended, the session "
            << (held_ready_while_active ? "Active" : "not Active") << "), result " << held_result
            << "; mock time advanced " << advanced.count() << " ms";
    auto const ending = peer ? peer->ending() : t105::peer_ending::open;
    // The precondition: the held send did not complete ok, so the window did not reopen before
    // the write ended. On Windows a window reopened before the session's end is a reading, not a
    // failure (the cell's comment states the condition; fixpp#722).
    bool const window_reopened = held_result == "ok";
#if defined(_WIN32)
    if (window_reopened && held_ready_while_active) {
        std::cout << "[ reading  ] TlsWriteLimit105: the window reopened, so the checks that rest "
                     "on the hold are not made; the held send completed in "
                  << held_ready_in << ", result " << held_result << "; " << reading.str()
                  << std::endl;
        return;
    }
#else
    EXPECT_FALSE(window_reopened) << "setup: the window reopened; the held send completed in "
                                  << held_ready_in << ", result " << held_result << "; "
                                  << reading.str();
    if (window_reopened) return;
#endif
    // What both platforms share: the peer reads only after the session's end, so everything it
    // reads follows the too-low frame; no Logout among it, and the connection ended.
    EXPECT_EQ(pr::frames_of_type(got, "5").size(), 0U)
        << "the R-2 Logout reached the peer; " << reading.str();
    EXPECT_NE(ending, t105::peer_ending::open)
        << "the connection was not closed; " << reading.str();
#if !defined(_WIN32)
    EXPECT_EQ(ending, t105::peer_ending::abortive)
        << "a close_notify behind a peer that stopped reading; " << reading.str();
#else
    // The Windows ending is not pinned: the line below prints the reading.
    std::cout << "[ reading  ] TlsWriteLimit105: " << t105::peer_ending_name(ending) << "; "
              << reading.str() << std::endl;
#endif
}

}  // namespace
