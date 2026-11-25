// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_seams.cpp — 105-transport-liveness (tasks.md T003).
//
// Every cell, or reach-proof twin, that names a symbol 105 adds (tasks.md, Execution rules,
// "Cells that cannot compile on the base"). Its [guard] static_asserts sit under
// #if FIXPP_105_STATIC_GUARDS (tasks.md, Execution rules, "Guard static_asserts").
//
// The ctest entry registers this file whether or not it holds cells yet, so it compiles on its
// own, on the base as at the head; each cell task of tasks.md appends its cells here.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "session/engine_loopback_harness.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/read_first_frame_bounded.hpp"
#include "session/resend_gap_scene.hpp"
#include "session/test_105_direct_rig.hpp"
#include "session/test_105_support.hpp"
#include "support/engine_test_access.hpp"
#include "support/log_capture.hpp"
#include "support/pump_until_ready.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_test_access.hpp"
#include "transport/asio_tls_transport.hpp"

// The friend src/transport/asio_tls_transport.hpp declares, for the write-limit twin's socket
// option; the same definition as tests/session/test_engine_session_strand.cpp's and
// tests/session/test_105_tls_close.cpp's, each in another binary.
namespace fixpp::transport {
class asio_tls_transport_test_access {
public:
    static asio::ip::tcp::socket& socket_of(asio_tls_transport& t) noexcept { return t.socket_; }
};
}  // namespace fixpp::transport

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::session_test_access;
using fixpp::session::SessionConfig;
namespace pr = fixpp::test_support::plain_rig;
namespace t105 = fixpp::test_support::t105;

constexpr std::chrono::seconds kHb{10};

// SendingTime(52) on the mock clock.
std::string stamp_now(fixpp::core::mock_clock const& clock) {
    std::array<char, 32> buf{};
    auto const r = fixpp::core::utc_time_to_fix_string(
        clock.now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}
constexpr std::chrono::milliseconds kWallCap{3000};

// Throws from toAdmin on a Heartbeat once armed: the liveness loop's emission fails and the
// loop ends the session (contract C-1 row 8).
class HeartbeatThrowingApp final : public fixpp::session::Application {
public:
    bool armed = false;
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& /*id*/) override {
        if (armed && msg.msg_type() == "0") throw std::runtime_error("HeartbeatThrowingApp");
    }
};

// Steps the mock clock until the session leaves Active or the liveness cap is reached.
template <class State>
bool step_until_ended(fixpp::core::mock_clock& clock, asio::io_context& ioc, State state) {
    std::chrono::nanoseconds advanced{0};
    while (state() == fsm_state::Active && advanced < t105::liveness_wire_cap(kHb, 1s)) {
        // Staging (ci/mock-clock-staging-sweep.sh): KIND D, the give-up instant is computed from
        // stored anchors (the last inbound frame, the TestRequest's write), so a sleep armed late
        // names an instant already passed; each step is followed by a bounded drive, and the caller
        // asserts the result.
        clock.advance(1s);
        advanced += 1s;
        (void)t105::drive_bounded(ioc, 64);
    }
    return state() != fsm_state::Active;
}

// [seam: release_connection_end_'s fail-closed catch replaced by one that leaves the latch set]
// T034 (contract C-8.6; clarification D-g): an Engine session ended by the liveness loop while
// its pump is parked in a read, with the close frame's spawn failing at its seam. The peer reads
// EOF with no inbound frame and no API call; the close frame's counter is 0 after close().
TEST(ConnectionClose105, CloseFrameSpawnFails_ReleaseAtLoopExit_PeerSeesEofWithNoLaterCall) {
    auto app = std::make_shared<HeartbeatThrowingApp>();
    pr::Rig rig{app};
    auto cfg = rig.cfg(fixpp::session::session_role::acceptor);
    cfg.heartbeat_interval = kHb;
    cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    bool const up = rig.start(std::move(cfg)) && rig.to_active();
    bool ended = false;
    bool eof = false;
    int seam_calls = 0;
    std::shared_ptr<fixpp::session::Session> s;
    if (up) {
        s = rig.session();
        session_test_access::set_close_frame_spawn_fails(*s, [&seam_calls] {
            ++seam_calls;
            return true;
        });
        app->armed = true;
        ended = step_until_ended(*rig.clock, rig.ioc, [&] { return rig.state(); });
        eof = t105::pump_until_peer_read_ends(
                  rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, "CloseFrameSpawnFails") &&
              t105::ended_in_eof(rig.peer, "CloseFrameSpawnFails");
    }
    rig.stop();
    EXPECT_TRUE(up && ended) << "setup";
    EXPECT_GE(seam_calls, 1) << "the reach proof: the close frame's spawn was not attempted";
    EXPECT_TRUE(eof) << "the peer's read did not end in EOF with no later call";
    if (s) {
        EXPECT_EQ(session_test_access::connection_close_count(*s), 0);
    }
}

// The TLS arm, on the Engine over TLS (tests/session/engine_loopback_harness.hpp, acceptor):
// the synchronous close with the pump's TLS read suspended is abortive, so the peer's read
// ends with no close_notify (L-105-h).
TEST(TlsClose105, CloseFrameSpawnFails_ReleaseAtLoopExit_AbortiveNoCloseNotify) {
    asio::io_context ioc;
    auto clock = t105::make_mock_clock(ioc.get_executor());
    auto app = std::make_shared<HeartbeatThrowingApp>();
    fixpp::core::EngineConfig ec;
    ec.executor = ioc.get_executor();
    ec.clock = clock;
    ec.application = app;
    auto h = fixpp::test_support::EngineLoopbackHarness::build(
        ioc.get_executor(), std::move(ec), true, [](fixpp::session::SessionConfig& c) {
            fixpp::session::CompIdAuthorizationPolicy authz;
            authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");
            c.compid_authorization_policy = authz;
            c.heartbeat_interval = kHb;
            c.test_request_threshold = std::chrono::milliseconds{kHb};
        });
    if (!h) GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    fixpp::test_support::engine_stop_guard stop_guard{*h, ioc};
    auto state = [&] {
        auto s = h->engine().lookup(h->acceptor_id());
        return s ? s->state() : fsm_state::NotConnected;
    };
    bool up = h->engine().start().has_value() &&
              fixpp::test_support::pump_until(
                  ioc, [&] { return h->server_endpoint().port != 0; }, 5s, 5ms,
                  "TlsClose105::CloseFrameSpawnFails_ReleaseAtLoopExit/bind");
    std::shared_ptr<t105::TlsPeer> peer;
    if (up) {
        peer = t105::TlsPeer::connect(ioc, h->transport_fixture(), h->server_endpoint());
        up = fixpp::test_support::pump_until(
                 ioc, [&] { return peer->ready || peer->setup_failed; }, 5s, 5ms,
                 "TlsClose105::CloseFrameSpawnFails_ReleaseAtLoopExit/connect") &&
             peer->ready;
    }
    if (up) {
        std::array<char, 32> buf{};
        auto const st = fixpp::core::utc_time_to_fix_string(
            clock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        peer->send(pr::message("FIX.4.2", "A", 1, "INITIATOR", "ACCEPTOR",
                               st ? std::string{st->data(), st->size()} : std::string{},
                               "98=0\x01"
                               "108=10\x01"));
        up = fixpp::test_support::pump_until(
            ioc, [&] { return state() == fsm_state::Active; }, 5s, 5ms,
            "TlsClose105::CloseFrameSpawnFails_ReleaseAtLoopExit/logon");
    }
    bool ended = false;
    bool read_ended = false;
    int seam_calls = 0;
    if (up) {
        auto s = h->engine().lookup(h->acceptor_id());
        session_test_access::set_close_frame_spawn_fails(*s, [&seam_calls] {
            ++seam_calls;
            return true;
        });
        app->armed = true;
        ended = step_until_ended(*clock, ioc, state);
        read_ended = t105::pump_until_peer_read_ends(
            ioc, [&] { return peer->read_ended; }, kWallCap, "TlsCloseFrameSpawnFails");
    }
    EXPECT_TRUE(up && ended) << "setup";
    EXPECT_GE(seam_calls, 1) << "the reach proof: the close frame's spawn was not attempted";
    EXPECT_TRUE(read_ended) << "the peer's read did not end with no later call";
    EXPECT_NE(peer ? peer->ending() : t105::peer_ending::open, t105::peer_ending::close_notify)
        << "a close_notify, where the synchronous close with a read suspended sends none";
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

// [seam] The twin of
// TlsWriteLimit105.R2AfterActive_PeerStopsReading_AbortiveAtBoundPlusTlsCloseTimeout (tasks.md
// T054; orchestrator ruling 3 of Phase 4): the same scenario, with the instant the too-low Logout's
// write starts read through session_test_access::ending_wait_phase, which that cell's file cannot
// name. The bound is then pinned on both sides of the mock clock: at the bound
// - 1 ms the phase is still `writing` and the session Active; at the bound the session ends. A
// widened or deleted write limit (T077) leaves the phase `writing` at the bound.
// The condition the twin rests on, as that cell's: the held write stays held until the session
// closes it. The session side's SO_SNDBUF is set to a fixed size once the session is Active, on
// Windows only. A held send that has completed by the bound - 1 ms, or that has
// completed ok by the bound, means the window reopened or the write ended early; the twin then
// fails as a setup failure.
// The twin's peer never reads: after the bound's observations the twin closes the peer's socket,
// so the cell's end does not wait for the peer to drain what the session queued. The ending the
// peer reads is the sibling's to assert.
TEST(TlsWriteLimit105, R2AfterActive_PeerStopsReading_SeamTwin_ClosedAtExactlyTheBound) {
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
            c.logout_disconnect_timeout_ms = 500;  // the bound this cell pins
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
                  "TlsWriteLimit105::R2AfterActive_PeerStopsReading_SeamTwin/bind");
    std::shared_ptr<t105::StalledTlsPeer> peer;
    if (up) {
        peer = t105::StalledTlsPeer::connect(ioc, dir, h->server_endpoint(), 1024);
        up = fixpp::test_support::pump_until(
                 ioc, [&] { return peer->ready || peer->setup_failed; }, 5s, 5ms,
                 "TlsWriteLimit105::R2AfterActive_PeerStopsReading_SeamTwin/connect") &&
             peer->ready;
    }
    auto msg = [&](std::string_view type, unsigned seq, std::string_view extra = {}) {
        return pr::message("FIX.4.2", type, seq, "INITIATOR", "ACCEPTOR", stamp_now(*clock), extra);
    };
    if (up) {
        peer->send(msg("A", 1,
                       "98=0\x01"
                       "108=30\x01"));
        up = fixpp::test_support::pump_until(
            ioc, [&] { return state() == fsm_state::Active; }, 5s, 5ms,
            "TlsWriteLimit105::R2AfterActive_PeerStopsReading_SeamTwin/logon");
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
            "TlsWriteLimit105::R2AfterActive_PeerStopsReading_SeamTwin/the Heartbeat at 2");
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
    using phase = session_test_access::ending_phase;
    auto phase_now = [&] {
        auto s = h->engine().lookup(h->acceptor_id());
        return s ? session_test_access::ending_wait_phase(*s) : phase::none;
    };
    bool writing = false;
    bool writing_before = false;
    bool open_before = false;
    bool ended_at_bound = false;
    bool held_before = false;
    // The held send's result as it stood at the bound: "not ready", "ok" or the error.
    std::string held_result = "not ready";
    auto read_held = [&] {
        if (!sends.empty() && sends.back().valid() &&
            sends.back().wait_for(0s) == std::future_status::ready) {
            auto const r = sends.back().get();
            held_result = r.has_value() ? std::string{"ok"}
                                        : "error " + std::to_string(static_cast<int>(r.error()));
        }
    };
    if (up && held) {
        peer->send(msg("0", 2));  // below NextNumIn: the too-low Logout, queued at the gate
        writing = fixpp::test_support::pump_until(
            ioc, [&] { return phase_now() == phase::writing; }, 3s, 5ms, nullptr);
    }
    if (writing) {
        // The mock clock has not moved since the Logout's write started.
        clock->advance(499ms);  // the bound - 1 ms
        (void)fixpp::test_support::pump_until(ioc, [] { return false; }, 100ms, 5ms, nullptr);
        writing_before = phase_now() == phase::writing;
        open_before = state() == fsm_state::Active;
        held_before = sends.back().wait_for(0s) != std::future_status::ready;
        clock->advance(1ms);
        ended_at_bound = fixpp::test_support::pump_until(
            ioc, [&] { return state() != fsm_state::Active; }, 2500ms, 5ms, nullptr);
        // At the bound, before the peer's socket closes: the close ends the held write, so the
        // result is read here, as the bound left it.
        read_held();
        peer->close();
    }
    EXPECT_TRUE(up) << "setup; the session's SO_SNDBUF " << session_sndbuf;
    EXPECT_TRUE(held) << "setup: no application send was held behind the stalled peer";
    EXPECT_TRUE(writing) << "setup: the too-low Logout's write did not start";
    if (writing) {
        EXPECT_TRUE(held_before) << "setup: the held send completed before the bound - 1 ms";
    }
    // The precondition: the held send had not completed ok by the bound, so the window did not
    // reopen before the write ended.
    if (!writing) read_held();
    bool const window_reopened = held_result == "ok";
    EXPECT_FALSE(window_reopened) << "setup: the window reopened; the held send's result "
                                  << held_result << "; the session's SO_SNDBUF " << session_sndbuf;
    if (window_reopened) return;
    EXPECT_TRUE(writing_before) << "the Logout's write was not still limited at the bound - 1 ms";
    EXPECT_TRUE(open_before) << "the session ended before the bound";
    EXPECT_TRUE(ended_at_bound) << "the session did not end at the bound";
}

// [guard: "Leaving `writing`" ignores `cut`] T052 (data-model E-3, `ending_wait_.cut`; contract
// C-2.7): a direct Session's R-2 Logout held in its write; the mock clock moved to the bound
// without draining; the ready handlers run one at a time until the bound has taken row 4 (its
// write-limit close: `cut` set); then the held write completes with success, as a completion
// already queued when the bound acted; then the drain. The Logout is whole at the peer, and no
// wait follows it: EOF with no further advance, Disconnected and onLogout once.
TEST(WriteLimit105, DirectSession_WriteCompletesAfterTheBoundFires_NoWaitArmed) {
    t105::DirectRig d{fixpp::session::session_role::acceptor};
    bool const up = d.to_active();
    ASSERT_TRUE(up) << "setup";
    d.bw->succeed_after_close = true;
    d.hold_write_of("5");
    auto frame = d.s.feed_spawn(fixpp::test_support::resend_gap::Scene::raw(
        "0", 2, "TW", d.s.sending_time(std::chrono::seconds{-600})));
    d.s.drain();
    ASSERT_TRUE(d.parked()) << "setup: the R-2 Logout's write is not held";
    // Staging (ci/mock-clock-staging-sweep.sh): KIND D, the bound sleeps until the deadline stored
    // at the Logout's write start, which predates this advance; the poll loop's cap ends in the cut
    // assertion.
    d.s.mock->advance(std::chrono::milliseconds{d.s.cfg.logout_disconnect_timeout_ms});
    bool cut = false;
    for (int i = 0; i < 10000 && !cut; ++i) {
        if (d.s.ioc.poll_one() == 0) d.s.ioc.restart();
        cut = session_test_access::ending_wait_cut(*d.s.session);
    }
    d.release();
    d.s.drain();
    // The reach proof, then the oracle.
    EXPECT_TRUE(cut) << "the reach proof: the bound did not take its write-limit close";
    EXPECT_EQ(d.completed("5"), 1U) << "the positive control: the peer does not hold the Logout";
    EXPECT_TRUE(d.closed()) << "no EOF with the clock still after the late completion";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected) << "the session was left in a wait";
    EXPECT_EQ(session_test_access::ending_wait_phase(*d.s.session),
              session_test_access::ending_phase::none)
        << "a wait was armed after the cut";
    EXPECT_EQ(d.app->logouts, 1) << "onLogout";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// ── T070: the seams of part B (tasks.md T070) ──────────────────────────────────────────────

namespace t070 {

class CountingApp105 final : public fixpp::session::Application {
public:
    int logouts = 0;
    void onLogout(const fixpp::session::SessionId& /*id*/) override { ++logouts; }
};

constexpr char kStaleSendingTime[] = "20200101-00:00:00.000";

enum class bound_spawn_route : std::uint8_t { r2_after_active, r2_before_active, reply };

// An Engine session whose ending bound cannot be spawned (clarification D-g, E-3 row 2a): the
// ending's Logout is not written, no wait follows, and the connection closes at once.
void bound_spawn_fails_cell(bound_spawn_route route) {
    auto app = std::make_shared<CountingApp105>();
    pr::Rig rig{app};
    // Before Active the initiator, whose Session exists before the peer's Logon (LogonSent's
    // posture refusal); after Active the acceptor.
    bool const before_active = route == bound_spawn_route::r2_before_active;
    auto cfg = rig.cfg(before_active ? fixpp::session::session_role::initiator
                                     : fixpp::session::session_role::acceptor);
    if (before_active) cfg.posture = fixpp::session::session_posture::production;
    bool up = rig.start(std::move(cfg));
    if (up && before_active) {
        up = rig.run_until([&] { return !pr::frames_of_type(rig.peer.received, "A").empty(); });
    } else if (up) {
        up = rig.to_active();
    }
    int seam_calls = 0;
    std::size_t mark = 0;
    bool eof = false;
    if (up) {
        session_test_access::set_ending_bound_spawn_fails(*rig.session(), [&seam_calls] {
            ++seam_calls;
            return true;
        });
        mark = rig.peer.received.size();
        switch (route) {
            case bound_spawn_route::r2_after_active:
                rig.peer.send(
                    pr::message(rig.begin_string, "0", 2, "TW", "ISLD", kStaleSendingTime));
                break;
            case bound_spawn_route::r2_before_active:
                rig.peer.send(rig.msg("A", 1,
                                      "98=0\x01"
                                      "108=30\x01"
                                      "464=Y\x01"));
                break;
            case bound_spawn_route::reply:
                rig.peer.send(rig.msg("5", 2));
                break;
        }
        eof = t105::pump_until_peer_read_ends(
                  rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, "BoundSpawnFails") &&
              t105::ended_in_eof(rig.peer, "BoundSpawnFails");
    }
    auto const logouts_written =
        pr::frames_of_type(rig.peer.received.substr(std::min(mark, rig.peer.received.size())), "5")
            .size();
    auto const state = rig.state();
    rig.stop();
    EXPECT_TRUE(up) << "setup";
    EXPECT_GE(seam_calls, 1) << "the reach proof: the bound's spawn was not attempted";
    EXPECT_EQ(logouts_written, 0U) << "a Logout was written with no bound";
    EXPECT_TRUE(eof) << "no EOF at once";
    EXPECT_EQ(state, fsm_state::Disconnected);
    if (route != bound_spawn_route::r2_before_active) {
        EXPECT_EQ(app->logouts, 1) << "onLogout";
    }
}

}  // namespace t070

// [seam: the bound's spawn failure read as "timeout 0": a Logout is written]
TEST(LogoutWait105, BoundSpawnFails_R2AfterActive_NoLogoutWritten_EofAtOnce) {
    t070::bound_spawn_fails_cell(t070::bound_spawn_route::r2_after_active);
}
TEST(LogoutWait105, BoundSpawnFails_R2BeforeActive_NoLogoutWritten_EofAtOnce) {
    t070::bound_spawn_fails_cell(t070::bound_spawn_route::r2_before_active);
}
TEST(LogoutWait105, BoundSpawnFails_Reply_NoLogoutWritten_EofAtOnce) {
    t070::bound_spawn_fails_cell(t070::bound_spawn_route::reply);
}

// [seam] The give-up with the bound's spawn failing: the slot-74 record, no Logout, EOF.
TEST(LivenessTiming105, GiveUp_BoundSpawnFails_StillEof) {
    t105::LogCapture cap;
    auto app = std::make_shared<t070::CountingApp105>();
    pr::Rig rig{app};
    auto cfg = rig.cfg(fixpp::session::session_role::acceptor);
    cfg.heartbeat_interval = kHb;
    cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    cfg.logger_override = cap.logger;
    bool const up = rig.start(std::move(cfg)) && rig.to_active();
    bool ended = false;
    bool eof = false;
    std::size_t mark = 0;
    if (up) {
        session_test_access::set_ending_bound_spawn_fails(*rig.session(), [] { return true; });
        mark = rig.peer.received.size();
        ended = step_until_ended(*rig.clock, rig.ioc, [&] { return rig.state(); });
        eof =
            t105::pump_until_peer_read_ends(
                rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, "GiveUp_BoundSpawnFails") &&
            t105::ended_in_eof(rig.peer, "GiveUp_BoundSpawnFails");
    }
    auto const logouts_written =
        pr::frames_of_type(rig.peer.received.substr(std::min(mark, rig.peer.received.size())), "5")
            .size();
    rig.stop();
    EXPECT_TRUE(up && ended) << "setup: the give-up did not end the session";
    EXPECT_EQ(
        cap.records_of(FIXPP_FORMAT_ID("session ended: TestRequest unanswered: test_req_id={} "
                                       "threshold_ms={} error={}"))
            .size(),
        1U)
        << "the slot-74 record";
    EXPECT_EQ(logouts_written, 0U) << "a Logout was written with no bound";
    EXPECT_TRUE(eof) << "no EOF after the give-up";
}

// [seam] The close frame the bound spawns, its spawn failing (clarification D-g): the
// synchronous close, so the peer sees EOF with no later call. WriteLimit: the R-2 Logout held in
// its write at the bound (row 4); BoundExpiry: the armed wait at its bound (row 10).
void close_frame_spawn_fails_at_the_bound(bool write_limit) {
    t105::DirectRig d{fixpp::session::session_role::acceptor};
    ASSERT_TRUE(d.to_active()) << "setup";
    int seam_calls = 0;
    session_test_access::set_close_frame_spawn_fails(*d.s.session, [&seam_calls] {
        ++seam_calls;
        return true;
    });
    if (write_limit) d.hold_write_of("5");
    auto frame = d.s.feed_spawn(fixpp::test_support::resend_gap::Scene::raw(
        "0", 2, "TW", d.s.sending_time(std::chrono::seconds{-600})));
    d.s.drain();
    EXPECT_EQ(write_limit ? d.parked() : d.completed("5") == 1U, true) << "setup: the Logout";
    d.pass(std::chrono::milliseconds{d.s.cfg.logout_disconnect_timeout_ms});
    EXPECT_GE(seam_calls, 1) << "the reach proof: the close frame's spawn was not attempted";
    EXPECT_TRUE(d.closed()) << "the peer saw no EOF at the bound with no later call";
    d.release();
    d.s.drain();
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(d.app->logouts, 1) << "onLogout";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}
TEST(ConnectionClose105, CloseFrameSpawnFails_WriteLimit_PeerSeesEofWithNoLaterCall) {
    close_frame_spawn_fails_at_the_bound(true);
}
TEST(ConnectionClose105, CloseFrameSpawnFails_BoundExpiry_PeerSeesEofWithNoLaterCall) {
    close_frame_spawn_fails_at_the_bound(false);
}

// [seam] The bound's in-body race seam throws std::bad_alloc before its race is awaited (data-model
// E-3 row 16): the failure exit of the phase it finds closes at once, with the clock frozen.
enum class race_route : std::uint8_t { writing, writing_store_held, armed, armed_before_active };
void race_throws_cell(race_route route) {
    auto gst = std::make_shared<t105::gate_store_state>();
    t105::DirectRig d{fixpp::session::session_role::acceptor, [&](SessionConfig& c) {
                          if (route == race_route::writing_store_held) {
                              c.store_factory = std::make_shared<t105::GateStoreFactory>(gst);
                          }
                          if (route == race_route::armed_before_active) {
                              c.posture = fixpp::session::session_posture::production;
                          }
                      }};
    using phase = session_test_access::ending_phase;
    std::optional<phase> found;  // the phase the throwing iteration found: the reach proof
    bool const armed_route = route == race_route::armed || route == race_route::armed_before_active;
    session_test_access::set_ending_bound_race_throws(*d.s.session, [&] {
        auto const p = session_test_access::ending_wait_phase(*d.s.session);
        if (armed_route && p != phase::armed) return false;
        if (!found) found = p;
        return true;
    });
    if (route == race_route::armed_before_active) {
        auto const opened = d.run(d.s.session->open(), "open");
        ASSERT_TRUE(opened && opened->has_value()) << "setup: open()";
        d.s.session->attach_accepted_transport(std::make_unique<t105::bwt::BlockingWriteTransport>(
                                                   d.s.ioc.get_executor(), d.stream, d.bw),
                                               fixpp::transport::handshake_result{});
        (void)d.s.feed(d.s.frame("A", 1,
                                 "98=0\x01"
                                 "108=30\x01"
                                 "464=Y\x01"));
    } else {
        ASSERT_TRUE(d.to_active()) << "setup";
        if (route == race_route::writing) d.hold_write_of("5");
        if (route == race_route::writing_store_held) {
            gst->hold_after_inner = true;
            gst->hold_when = [&d](t105::store_call k, fixpp::session::seqnum_t) {
                return k == t105::store_call::store_outbound && d.app->to_admin_count("5") >= 1;
            };
        }
        (void)d.s.feed_spawn(fixpp::test_support::resend_gap::Scene::raw(
            "0", 2, "TW", d.s.sending_time(std::chrono::seconds{-600})));
        d.s.drain();
    }
    bool const closed_at_once = d.closed();
    d.release();
    gst->release_all();
    d.s.drain();
    ASSERT_TRUE(found.has_value()) << "the reach proof: the race seam never ran";
    EXPECT_EQ(*found, armed_route ? phase::armed : phase::writing) << "the phase the exit found";
    EXPECT_TRUE(closed_at_once) << "no EOF at once with the clock frozen";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected);
    if (route != race_route::armed_before_active) {
        EXPECT_EQ(d.app->logouts, 1) << "onLogout";
    }
    EXPECT_EQ(session_test_access::ending_logout_bound_count(*d.s.session), 0)
        << "the bound outlived its failure";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "close");
}
TEST(LogoutWait105, EndingBound_RaceThrows_Writing_EofAtOnce) {
    race_throws_cell(race_route::writing);
}
TEST(LogoutWait105, EndingBound_RaceThrows_Writing_StoreHeld_EofAtOnce) {
    race_throws_cell(race_route::writing_store_held);
}
TEST(LogoutWait105, EndingBound_RaceThrows_Armed_EofAtOnce) { race_throws_cell(race_route::armed); }
TEST(LogoutWait105, EndingBound_RaceThrows_ArmedBeforeActive_EofAtOnce) {
    race_throws_cell(race_route::armed_before_active);
}

// [seam] T053's reach-proof twins (tasks.md T061): the cells of
// LogoutWait105Direct.EndingBound_DeadlineArmAbsorbsException_*, with the phase the bound's
// failure exit finds read through session_test_access before the drive that reaches it.
namespace t053 {
struct CountingDirect {
    t105::DirectRig d;
    std::shared_ptr<t105::CountingClock> cc;
    explicit CountingDirect(fixpp::session::session_role role,
                            std::function<void(SessionConfig&)> const& tweak = {})
        : d{role, tweak} {
        cc = std::make_shared<t105::CountingClock>(d.s.mock);
        d.s.cfg.clock_override = cc;
        d.s.session = std::make_unique<fixpp::session::Session>(d.s.engine, d.s.cfg);
    }
    [[nodiscard]] fixpp::core::steady_time_point bound_instant() const {
        return d.s.mock->steady_now() +
               std::chrono::milliseconds{d.s.cfg.logout_disconnect_timeout_ms};
    }
    [[nodiscard]] std::vector<std::byte> stale(fixpp::session::seqnum_t seq) {
        return fixpp::test_support::resend_gap::Scene::raw(
            "0", seq, "TW", d.s.sending_time(std::chrono::seconds{-600}));
    }
    [[nodiscard]] session_test_access::ending_phase phase() const {
        return session_test_access::ending_wait_phase(*d.s.session);
    }
};
}  // namespace t053

TEST(LogoutWait105Direct, EndingBound_DeadlineArmAbsorbsException_Writing_ReachProof) {
    t053::CountingDirect c{fixpp::session::session_role::acceptor};
    ASSERT_TRUE(c.d.to_active()) << "setup";
    c.cc->throw_once_at(c.bound_instant());
    c.d.hold_write_of("5");
    auto f = c.d.s.feed_spawn(c.stale(2));
    c.d.s.drain();
    // The throw is taken by the race built at the bound's first loop, during the write; only the
    // exit from `writing` sets `cut`.
    EXPECT_TRUE(session_test_access::ending_wait_cut(*c.d.s.session))
        << "the reach proof: the failure exit did not find `writing`";
    EXPECT_FALSE(c.cc->throw_pending()) << "the bound's sleep was never requested";
    EXPECT_TRUE(c.d.closed()) << "the bound's failure during the write did not close at once";
    c.d.release();
    c.d.s.drain();
    EXPECT_EQ(c.d.app->logouts, 1);
    (void)c.d.run(c.d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

TEST(LogoutWait105Direct, EndingBound_DeadlineArmAbsorbsException_Armed_ReachProof) {
    t053::CountingDirect c{fixpp::session::session_role::acceptor};
    ASSERT_TRUE(c.d.to_active()) << "setup";
    auto const at = c.bound_instant();
    auto f = c.d.s.feed_spawn(c.stale(2));
    // Handlers one at a time until the frame returns, so the bound has not yet rebuilt its
    // race after the leaf's wake.
    for (int i = 0; i < 4096 && !fixpp::test_support::resend_gap::Scene::ready(f); ++i) {
        if (c.d.s.ioc.run_one_for(std::chrono::milliseconds{1}) == 0) c.d.s.ioc.restart();
    }
    ASSERT_EQ(c.d.completed("5"), 1U) << "setup: the Logout is not on the wire";
    EXPECT_EQ(c.phase(), session_test_access::ending_phase::armed) << "the phase the exit finds";
    c.cc->throw_once_at(at);
    (void)t105::drive_bounded(c.d.s.ioc, 4096);
    EXPECT_FALSE(c.cc->throw_pending()) << "the armed bound's sleep was never requested";
    EXPECT_FALSE(session_test_access::ending_wait_cut(*c.d.s.session))
        << "the exit found `writing`, not `armed`";
    EXPECT_TRUE(c.d.closed()) << "the bound's failure in the wait did not close at once";
    EXPECT_EQ(c.d.app->logouts, 1);
    (void)c.d.run(c.d.s.session->close(fixpp::session::close_mode::terminal), "close");
}

// ════════════════════════════════════════════════════════════════════════════════════════
// 105 T086 (research D5; data-model E-4; contract C-5.4): the TestReqID window and the parse of
// detail::is_earlier_test_req_id. constexpr cells: each static_assert sits under
// FIXPP_105_STATIC_GUARDS, and each cell's runtime twin asserts the same values with EXPECT_*, so
// a mutant built with the option OFF reads as a test failure, not a compile error.
// ════════════════════════════════════════════════════════════════════════════════════════

namespace trw {
using fixpp::session::detail::is_earlier_test_req_id;
using fixpp::session::detail::test_req_id_in_window;
constexpr std::uint32_t kTop = 0xFFFFFFFFU;

// [guard: a non-modular comparison] The window's top edge at std::uint32_t's maximum.
constexpr bool window_at_uint32_max() {
    return test_req_id_in_window(kTop, kTop - 15U, kTop) &&
           test_req_id_in_window(kTop - 14U, kTop - 15U, kTop) &&
           !test_req_id_in_window(kTop - 15U, kTop - 15U, kTop) &&
           !test_req_id_in_window(0U, kTop - 15U, kTop);
}
// [guard: a non-modular comparison] Numbers issued across the wrap: floor kTop - 1, next 3.
constexpr bool window_across_the_wrap() {
    return test_req_id_in_window(kTop, kTop - 1U, 3U) && test_req_id_in_window(0U, kTop - 1U, 3U) &&
           test_req_id_in_window(3U, kTop - 1U, 3U) && !test_req_id_in_window(4U, kTop - 1U, 3U) &&
           !test_req_id_in_window(kTop - 1U, kTop - 1U, 3U);
}
// [guard: the full-cycle case dropped, next == floor read as a full window] After 2^32 numbers on
// one connection next == floor and no number is in the window.
constexpr bool window_one_full_cycle() {
    return !test_req_id_in_window(7U, 7U, 7U) && !test_req_id_in_window(8U, 7U, 7U) &&
           !test_req_id_in_window(6U, 7U, 7U) && !test_req_id_in_window(0U, 7U, 7U);
}

// The parse cells' window: floor kTop - 15 and next 5, so 0 to 5 and kTop - 14 to kTop are in it.
constexpr std::uint32_t kFloor = kTop - 15U;
constexpr std::uint32_t kNext = 5U;
constexpr bool parse_canonical_earlier() {
    return is_earlier_test_req_id("TR3", kFloor, kNext) &&
           is_earlier_test_req_id("TR0", kFloor, kNext) &&
           is_earlier_test_req_id("TR4294967295", kFloor, kNext) &&
           !is_earlier_test_req_id("TR6", kFloor, kNext);
}
// [guard: the canonical-form test deleted]
constexpr bool parse_leading_zero_mismatch() {
    return !is_earlier_test_req_id("TR03", kFloor, kNext) &&
           !is_earlier_test_req_id("TR00", kFloor, kNext);
}
// [guard: the sign refusal deleted]
constexpr bool parse_sign_mismatch() {
    return !is_earlier_test_req_id("TR-3", kFloor, kNext) &&
           !is_earlier_test_req_id("TR+3", kFloor, kNext) &&
           !is_earlier_test_req_id("TR-", kFloor, kNext);
}
// [guard: the overflow test deleted] 2^32 and 2^32 + 3 wrap to numbers in the window.
constexpr bool parse_overflow_mismatch() {
    return !is_earlier_test_req_id("TR4294967296", kFloor, kNext) &&
           !is_earlier_test_req_id("TR4294967299", kFloor, kNext) &&
           !is_earlier_test_req_id("TR99999999999999999999", kFloor, kNext);
}
// [guard: the TR-length test deleted] "TR" alone would read as 0, which is in the window.
constexpr bool parse_prefix_alone_mismatch() {
    return !is_earlier_test_req_id("TR", kFloor, kNext) &&
           !is_earlier_test_req_id("T", kFloor, kNext) &&
           !is_earlier_test_req_id("", kFloor, kNext) &&
           !is_earlier_test_req_id("XR3", kFloor, kNext);
}
}  // namespace trw

#if FIXPP_105_STATIC_GUARDS
static_assert(trw::window_at_uint32_max());
static_assert(trw::window_across_the_wrap());
static_assert(trw::window_one_full_cycle());
static_assert(trw::parse_canonical_earlier());
static_assert(trw::parse_leading_zero_mismatch());
static_assert(trw::parse_sign_mismatch());
static_assert(trw::parse_overflow_mismatch());
static_assert(trw::parse_prefix_alone_mismatch());
#endif

TEST(TestReqIdWindow105, AtUint32Max) { EXPECT_TRUE(trw::window_at_uint32_max()); }
TEST(TestReqIdWindow105, AcrossTheWrap) { EXPECT_TRUE(trw::window_across_the_wrap()); }
TEST(TestReqIdWindow105, OneFullCycle_NoValueEarlier) { EXPECT_TRUE(trw::window_one_full_cycle()); }
TEST(TestReqIdParse105, Canonical_Earlier) { EXPECT_TRUE(trw::parse_canonical_earlier()); }
TEST(TestReqIdParse105, LeadingZero_Mismatch) { EXPECT_TRUE(trw::parse_leading_zero_mismatch()); }
TEST(TestReqIdParse105, Sign_Mismatch) { EXPECT_TRUE(trw::parse_sign_mismatch()); }
TEST(TestReqIdParse105, Overflow_Mismatch) { EXPECT_TRUE(trw::parse_overflow_mismatch()); }
TEST(TestReqIdParse105, PrefixAlone_Mismatch) { EXPECT_TRUE(trw::parse_prefix_alone_mismatch()); }

// The parse's refusals by form, each called with arguments read at run time (a std::string built
// through a volatile length, and volatile window ends), so the call is not constant-evaluated and
// the instrumented build runs each refusal. The window is the constexpr cells' (trw::kFloor,
// trw::kNext); the first loop's ids are the positive controls, earlier and in the window.
TEST(TestReqIdParse105, RefusalsByForm_AtRunTime) {
    auto at_run_time = [](std::string_view s) {
        volatile std::size_t n = s.size();
        return std::string{s.data(), n};
    };
    volatile std::uint32_t floor_v = trw::kFloor;
    volatile std::uint32_t next_v = trw::kNext;
    std::uint32_t const floor = floor_v;
    std::uint32_t const next = next_v;
    for (std::string_view const id : {"TR3", "TR0", "TR4294967295"}) {
        EXPECT_TRUE(trw::is_earlier_test_req_id(at_run_time(id), floor, next)) << id;
    }
    // The refused ids, in the order of the refusals they reach in is_earlier_test_req_id: the
    // length or prefix ("", "T", "TR", "XR3", "TX3"), a byte below '0' ("TR/3"), above '9'
    // ("TR3x"), the sign ("TR-3"), the leading zero ("TR03"), past std::uint32_t ("TR4294967296"),
    // past the digit limit ("TR99999999999999999999"), and the window ("TR12").
    for (std::string_view const id : {"", "T", "TR", "XR3", "TX3", "TR/3", "TR3x", "TR-3", "TR03",
                                      "TR4294967296", "TR99999999999999999999", "TR12"}) {
        EXPECT_FALSE(trw::is_earlier_test_req_id(at_run_time(id), floor, next)) << id;
    }
}

// ════════════════════════════════════════════════════════════════════════════════════════
// 105 T097 (research D6; data-model E-4): the default threshold helper and the give-up instant's
// guard. constexpr cells: each static_assert sits under FIXPP_105_STATIC_GUARDS, and each cell's
// runtime twin asserts the same values with EXPECT_*.
// ════════════════════════════════════════════════════════════════════════════════════════

namespace ltg {
using fixpp::session::detail::default_test_request_threshold;
using fixpp::session::detail::held_test_request_cap;
using fixpp::session::detail::test_request_give_up_instant;
using std::chrono::milliseconds;
using std::chrono::seconds;
using tp = fixpp::core::steady_time_point;
constexpr milliseconds::rep kLargestExact = std::numeric_limits<milliseconds::rep>::max() / 1200;

// [guard: the product formed unsaturated] H × 1200 ms, exact below the saturation point and
// milliseconds::max() above it.
constexpr bool default_threshold_saturates() {
    return default_test_request_threshold(seconds{10}) == milliseconds{12000} &&
           default_test_request_threshold(seconds{1}) == milliseconds{1200} &&
           default_test_request_threshold(seconds{kLargestExact}) ==
               milliseconds{kLargestExact * 1200} &&
           default_test_request_threshold(seconds{kLargestExact + 1}) == milliseconds::max() &&
           default_test_request_threshold(seconds::max()) == milliseconds::max();
}

// [guard: the guard added by hand to a saturated instant] steady_time_point::max() in, max()
// out; and the guard is one nanosecond where the instant is representable.
constexpr bool guard_at_the_saturated_instant() {
    return test_request_give_up_instant(tp::max(), milliseconds{0}) == tp::max() &&
           test_request_give_up_instant(tp::max(), milliseconds{12000}) == tp::max() &&
           test_request_give_up_instant(tp::max() - seconds{24}, milliseconds{12000}) ==
               tp::max() &&
           test_request_give_up_instant(tp{}, milliseconds{12000}) ==
               tp{} + seconds{24} + std::chrono::nanoseconds{1};
}

// [guard: the later instant dropped] the owner's fresh-window ruling (the held TestRequest's
// fresh window): the give-up instant of a TestRequest written at `written` is the later of the
// two-threshold instant and `written` + T + 1 ns, each side saturating.
constexpr bool fresh_window_is_the_later_instant() {
    constexpr auto ns1 = std::chrono::nanoseconds{1};
    return test_request_give_up_instant(tp{}, tp{} + seconds{36}, milliseconds{12000}) ==
               tp{} + seconds{48} + ns1 &&
           test_request_give_up_instant(tp{}, tp{} + seconds{12}, milliseconds{12000}) ==
               tp{} + seconds{24} + ns1 &&
           test_request_give_up_instant(tp{} + seconds{30}, tp{} + seconds{12},
                                        milliseconds{12000}) == tp{} + seconds{54} + ns1 &&
           test_request_give_up_instant(tp{}, tp::max(), milliseconds{12000}) == tp::max() &&
           test_request_give_up_instant(tp::max() - seconds{5}, tp{}, milliseconds{12000}) ==
               tp::max();
}

// [guard: the held TestRequest's cap formed as a product, or without its guard] the owner's cap
// ruling: three thresholds after the last inbound frame, then 1 ns, every step saturating.
constexpr bool held_cap_is_three_thresholds_and_the_guard() {
    constexpr auto ns1 = std::chrono::nanoseconds{1};
    return held_test_request_cap(tp{}, milliseconds{12000}) == tp{} + seconds{36} + ns1 &&
           held_test_request_cap(tp{} + seconds{7}, milliseconds{10000}) ==
               tp{} + seconds{37} + ns1 &&
           held_test_request_cap(tp{}, milliseconds{0}) == tp{} + ns1 &&
           held_test_request_cap(tp::max(), milliseconds{0}) == tp::max() &&
           held_test_request_cap(tp::max() - seconds{36}, milliseconds{12000}) == tp::max() &&
           held_test_request_cap(tp{}, milliseconds::max()) == tp::max();
}
}  // namespace ltg

#if FIXPP_105_STATIC_GUARDS
static_assert(ltg::default_threshold_saturates());
static_assert(ltg::guard_at_the_saturated_instant());
static_assert(ltg::fresh_window_is_the_later_instant());
static_assert(ltg::held_cap_is_three_thresholds_and_the_guard());
#endif

TEST(LivenessTiming105, DefaultThresholdHelper_Saturates) {
    EXPECT_TRUE(ltg::default_threshold_saturates());
}
TEST(LivenessTiming105, GuardAtTheSaturatedInstant) {
    EXPECT_TRUE(ltg::guard_at_the_saturated_instant());
}
TEST(LivenessTiming105, FreshWindow_IsTheLaterInstant) {
    EXPECT_TRUE(ltg::fresh_window_is_the_later_instant());
}
TEST(LivenessTiming105, HeldCap_IsThreeThresholdsAndTheGuard) {
    EXPECT_TRUE(ltg::held_cap_is_three_thresholds_and_the_guard());
}

// The default threshold at HeartBtInt <= 0, where the liveness loop is off: zero, as
// default_test_request_threshold documents. HeartBtInt is read at run time (volatile), so the call
// is not constant-evaluated and the instrumented build runs that return.
TEST(LivenessTiming105, DefaultThresholdHelper_ZeroAtHeartBtIntNotPositive) {
    for (ltg::seconds::rep const h : {ltg::seconds::rep{0}, ltg::seconds::rep{-1},
                                      std::numeric_limits<ltg::seconds::rep>::min()}) {
        volatile ltg::seconds::rep h_v = h;
        EXPECT_EQ(ltg::default_test_request_threshold(ltg::seconds{h_v}), ltg::milliseconds::zero())
            << "HeartBtInt " << h;
    }
}

// 105 (tasks.md T106) [seam], C-7.3's generic arm: no configuration makes the listener's
// construction throw anything but std::system_error (asio_listener's constructor and
// make_bind_endpoint throw it for every bind, listen and address failure), so the seam throws
// std::bad_alloc in its place. One error record, error 0. Its mutant deletes the catch (...)
// arm's record (tasks.md T107).
TEST(PreSessionLog105, ListenerConstructionThrowsNonSystemError_OneRecordErrorZero) {
    constexpr char kBindFormat[] =
        "acceptor listener could not bind: begin_string={} sender_comp_id={} target_comp_id={} "
        "error={}";
    pr::Rig rig;
    fixpp::test_support::LogCapture log;
    auto c = rig.cfg();
    c.logger_override = log.logger;
    fixpp::core::EngineConfig ec;
    ec.executor = rig.ioc.get_executor();
    ec.clock = rig.clock;
    rig.engine = std::make_unique<fixpp::session::Engine>(rig.ioc.get_executor(), std::move(ec));
    int calls = 0;
    fixpp::session::engine_test_access::set_listener_construction_fails(*rig.engine, [&calls] {
        ++calls;
        return true;
    });
    rig.id = fixpp::session::SessionId::from_config(c);
    bool const registered = rig.engine->register_session(std::move(c)).has_value();
    bool const started = registered && rig.engine->start().has_value();
    EXPECT_TRUE(started) << "setup: the engine did not start";
    if (started) {
        (void)rig.run_until(
            [&] {
                for (auto const& r : log.sink->records()) {
                    if (r.format_id == FIXPP_FORMAT_ID(kBindFormat)) return true;
                }
                return false;
            },
            std::chrono::seconds{2});
    }
    rig.stop();
    auto const recs = log.records_of(FIXPP_FORMAT_ID(kBindFormat));
    EXPECT_EQ(calls, 1) << "the seam was not consulted once";
    ASSERT_EQ(recs.size(), 1U) << "the listener record";
    EXPECT_EQ(recs.front().level, fixpp::log::Level::error);
    ASSERT_EQ(recs.front().arg_count, 4U);
    auto const text = [](fixpp::log::ArgValue const& a) {
        return a.kind == fixpp::log::ArgValue::Kind::inline_str ? std::string{a.inl.data, a.inl.len}
                                                                : std::string{"<not inline>"};
    };
    EXPECT_EQ(text(recs.front().args[0]), "FIX.4.2");
    EXPECT_EQ(text(recs.front().args[1]), "ISLD");
    EXPECT_EQ(text(recs.front().args[2]), "TW");
    EXPECT_EQ(recs.front().args[3].kind, fixpp::log::ArgValue::Kind::i64);
    EXPECT_EQ(recs.front().args[3].i64, 0) << "error";
}

// [seam] The listener-construction seam installed and answering false: the construction goes
// ahead, the listener binds, and no listener record is written. The seam is consulted once.
TEST(PreSessionLog105, ListenerConstructionSeamFalse_ListenerBinds) {
    constexpr char kBindFormat[] =
        "acceptor listener could not bind: begin_string={} sender_comp_id={} target_comp_id={} "
        "error={}";
    pr::Rig rig;
    fixpp::test_support::LogCapture log;
    auto c = rig.cfg();
    c.logger_override = log.logger;
    fixpp::core::EngineConfig ec;
    ec.executor = rig.ioc.get_executor();
    ec.clock = rig.clock;
    rig.engine = std::make_unique<fixpp::session::Engine>(rig.ioc.get_executor(), std::move(ec));
    int calls = 0;
    fixpp::session::engine_test_access::set_listener_construction_fails(*rig.engine, [&calls] {
        ++calls;
        return false;
    });
    rig.id = fixpp::session::SessionId::from_config(c);
    bool const registered = rig.engine->register_session(std::move(c)).has_value();
    bool const started = registered && rig.engine->start().has_value();
    EXPECT_TRUE(started) << "setup: the engine did not start";
    bool bound = false;
    if (started) {
        bound =
            rig.run_until([&] { return rig.engine->acceptor_bound_endpoint(rig.id).port != 0; });
    }
    rig.stop();
    EXPECT_EQ(calls, 1) << "the seam was not consulted once";
    EXPECT_TRUE(bound) << "the listener did not bind";
    EXPECT_TRUE(log.records_of(FIXPP_FORMAT_ID(kBindFormat)).empty()) << "a listener record";
}

// ════════════════════════════════════════════════════════════════════════════════════════
// 105 T143a (tasks.md): cells for added lines and branches that the coverage reading of T143
// found unreached. Each comment names the branch it reaches and the observable ending it
// asserts. Re-derive a branch's uncovered side as T143 does: llvm-cov show
// -show-branches=count over the owning binary's own profdata, -sources the file.
// ════════════════════════════════════════════════════════════════════════════════════════

namespace t143a {

using phase = session_test_access::ending_phase;
using fixpp::session::session_role;
using rgs_scene = fixpp::test_support::resend_gap::Scene;
// A Scene's HeartBtInt.
constexpr auto kSceneHb = fixpp::test_support::resend_gap::kHeartBtInt;

std::vector<std::byte> stale(t105::DirectRig& d, fixpp::session::seqnum_t seq) {
    return rgs_scene::raw("0", seq, "TW", d.s.sending_time(std::chrono::seconds{-600}));
}
phase phase_of(t105::DirectRig const& d) {
    return session_test_access::ending_wait_phase(*d.s.session);
}

// What the session wrote, for a failure message: MsgType(35), MsgSeqNum(34) and Text(58) of each
// write, in write-start order, an incomplete write marked.
std::string wire_of(t105::DirectRig const& d) {
    std::string out;
    for (auto const& w : d.bw->recorded) {
        out += " [35=" + std::string{fixpp::test_support::extract_tag(w.bytes, 35)} +
               " 34=" + std::string{fixpp::test_support::extract_tag(w.bytes, 34)};
        auto const text = fixpp::test_support::extract_tag(w.bytes, 58);
        if (!text.empty()) out += " 58=" + std::string{text};
        out += w.completed ? "]" : " held]";
    }
    return out;
}

// The peer's Logon as a direct initiator's LogonSent arm reads it, with extra after 98 and 108.
std::vector<std::byte> logon_reply(std::string_view sending_time, std::string_view extra = {}) {
    return rgs_scene::raw("A", 1, "TW", sending_time,
                          "98=0\x01"
                          "108=30\x01" +
                              std::string{extra});
}

// A direct initiator in LogonSent: open(), then drive_reconnect(), which writes its Logon.
bool to_logon_sent(t105::DirectRig& d) {
    auto const opened = d.run(d.s.session->open(), "T143a/open");
    if (!opened || !opened->has_value()) return false;
    auto const c = d.run(d.s.session->drive_reconnect(), "T143a/drive_reconnect");
    return c && c->has_value() && d.s.state() == fsm_state::LogonSent && d.started("A") == 1U;
}

}  // namespace t143a

// [Leaving writing, its entry-state side] An R-2 Logout whose write returns success after a
// close(terminal) made during it: the session left Active while the Logout was written, so no
// wait is armed for it.
TEST(LogoutWait105Direct, LogoutWrittenAcrossCloseTerminal_NoWaitArmed) {
    t105::DirectRig d{t143a::session_role::acceptor};
    ASSERT_TRUE(d.to_active()) << "setup";
    d.bw->succeed_after_close = true;
    d.hold_write_of("5");
    auto frame = d.s.feed_spawn(t143a::stale(d, 2));
    d.s.drain();
    bool const held = d.parked();
    auto const closed =
        d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/across");
    d.s.drain();
    EXPECT_TRUE(held) << "setup: the Logout's write is not held";
    EXPECT_TRUE(closed.has_value()) << "close() did not return";
    EXPECT_EQ(d.completed("5"), 1U) << "setup: the Logout's write did not complete";
    EXPECT_EQ(t143a::phase_of(d), t143a::phase::none)
        << "a wait was armed for a session that had left Active during the write";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(d.app->logouts, 1) << "onLogout";
}

// [the ending response's route, MsgSeqNum(34) 0] A Logout with 34=0 during a wait armed after
// Active takes the off-NextNumIn route: the wait ends at once, nothing is delivered and
// NextNumIn does not move.
TEST(LogoutWait105Direct, PeerLogoutMsgSeqNumZero_DuringWait_EndsWaitNoAdvance) {
    t105::DirectRig d{t143a::session_role::acceptor};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(t143a::stale(d, 2));
    bool const armed = d.completed("5") == 1U && t143a::phase_of(d) == t143a::phase::armed;
    bool const open_before = !d.closed();
    auto const next_in = d.s.next_in();
    (void)d.s.feed(t143a::rgs_scene::raw("5", 0, "TW", d.s.sending_time()));
    EXPECT_TRUE(armed && open_before) << "setup: no wait armed after the ending Logout";
    EXPECT_TRUE(d.closed()) << "the Logout with MsgSeqNum 0 did not end the wait at once";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(d.s.next_in(), next_in) << "NextNumIn moved";
    EXPECT_EQ(d.app->from_admin_logouts, 0) << "the Logout was delivered";
    EXPECT_EQ(d.app->logouts, 1) << "onLogout";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/zero");
}

// [the ending response at NextNumIn, check_inbound refusing] NextNumIn at seqnum_max, where no
// next value exists (092 FR-019): the Logout at NextNumIn is refused by check_inbound and takes
// the out-of-sequence arm, so it is not delivered and the session ends at once.
TEST(LogoutWait105Direct, PeerLogoutAtNextNumIn_CheckInboundRefuses_NotDelivered) {
    t105::DirectRig d{t143a::session_role::acceptor};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(t143a::stale(d, 2));
    bool const armed = d.completed("5") == 1U && t143a::phase_of(d) == t143a::phase::armed;
    auto& mgr = session_test_access::seqnum_mgr(*d.s.session);
    fixpp::session::seqnum_manager_test_access::set_counters(mgr, fixpp::session::seqnum_max,
                                                             mgr.peek_outbound());
    (void)d.s.feed(
        t143a::rgs_scene::raw("5", fixpp::session::seqnum_max, "TW", d.s.sending_time()));
    EXPECT_TRUE(armed) << "setup: no wait armed after the ending Logout";
    EXPECT_EQ(d.app->from_admin_logouts, 0) << "the refused Logout was delivered";
    EXPECT_EQ(d.s.next_in(), fixpp::session::seqnum_max) << "NextNumIn moved";
    EXPECT_TRUE(d.closed()) << "the session did not end at once";
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected);
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/max");
}

// [the pre-Active wait's Logout test, each conjunct's failing side] A direct acceptor's posture
// refusal arms a wait before Active; a Logout that fails exactly one conjunct of the test is
// drained (the wait stays, the connection open), and the positive control, a well-formed Logout
// with the right identity, then ends it. With check_comp_id off a Logout from another CompID
// ends it.
namespace t143a {
enum class drained_frame : std::uint8_t {
    faulted_header,  // a field whose tag is not a number
    msg_type_not_third,
    other_begin_string,
    other_sender,
    other_target,
};

std::vector<std::byte> drained(drained_frame k, std::string const& st) {
    std::string const tail = "52=" + st + "\x01";
    switch (k) {
        case drained_frame::faulted_header:
            return pr::to_bytes(pr::frame("FIX.4.2",
                                          "35=5\x01"
                                          "34=2\x01"
                                          "49=TW\x01" +
                                              tail + "56=ISLD\x01" + "X1=1\x01"));
        case drained_frame::msg_type_not_third:
            return pr::to_bytes(pr::frame("FIX.4.2",
                                          "34=2\x01"
                                          "35=5\x01"
                                          "49=TW\x01" +
                                              tail + "56=ISLD\x01"));
        case drained_frame::other_begin_string:
            return pr::to_bytes(pr::message("FIX.4.4", "5", 2, "TW", "ISLD", st));
        case drained_frame::other_sender:
            return pr::to_bytes(pr::message("FIX.4.2", "5", 2, "OTHER", "ISLD", st));
        case drained_frame::other_target:
            return pr::to_bytes(pr::message("FIX.4.2", "5", 2, "TW", "ELSEWHERE", st));
    }
    return {};
}

// The posture refusal on a direct acceptor; true once the refusal's Logout is written, the
// session Disconnected and the wait armed before Active.
bool refused_before_active(t105::DirectRig& d) {
    auto const opened = d.run(d.s.session->open(), "T143a/open/acceptor");
    if (!opened || !opened->has_value()) return false;
    d.s.session->attach_accepted_transport(
        std::make_unique<t105::bwt::BlockingWriteTransport>(d.s.ioc.get_executor(), d.stream, d.bw),
        fixpp::transport::handshake_result{});
    (void)d.s.feed(d.s.frame("A", 1,
                             "98=0\x01"
                             "108=30\x01"
                             "464=Y\x01"));
    return d.completed("5") == 1U && d.s.state() == fsm_state::Disconnected &&
           phase_of(d) == phase::armed && !d.closed();
}

void drained_cell(drained_frame k) {
    t105::DirectRig d{session_role::acceptor, [](SessionConfig& c) {
                          c.posture = fixpp::session::session_posture::production;
                      }};
    ASSERT_TRUE(refused_before_active(d)) << "setup: no wait armed before Active";
    (void)d.s.feed(drained(k, d.s.sending_time()));
    EXPECT_FALSE(d.closed()) << "the frame ended the wait";
    EXPECT_EQ(phase_of(d), phase::armed) << "the frame ended the wait";
    (void)d.s.feed(rgs_scene::raw("5", 2, "TW", d.s.sending_time()));
    EXPECT_TRUE(d.closed()) << "the positive control: the peer's Logout did not end the wait";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/drained");
}
}  // namespace t143a

TEST(LogoutWait105Direct, PreActiveWait_FaultedHeaderLogout_Drained) {
    t143a::drained_cell(t143a::drained_frame::faulted_header);
}
TEST(LogoutWait105Direct, PreActiveWait_MsgTypeNotThirdLogout_Drained) {
    t143a::drained_cell(t143a::drained_frame::msg_type_not_third);
}
TEST(LogoutWait105Direct, PreActiveWait_OtherBeginStringLogout_Drained) {
    t143a::drained_cell(t143a::drained_frame::other_begin_string);
}
TEST(LogoutWait105Direct, PreActiveWait_OtherSenderLogout_Drained) {
    t143a::drained_cell(t143a::drained_frame::other_sender);
}
TEST(LogoutWait105Direct, PreActiveWait_OtherTargetLogout_Drained) {
    t143a::drained_cell(t143a::drained_frame::other_target);
}
TEST(LogoutWait105Direct, PreActiveWait_CheckCompIdOff_OtherSenderLogoutEndsWait) {
    t105::DirectRig d{t143a::session_role::acceptor, [](SessionConfig& c) {
                          c.posture = fixpp::session::session_posture::production;
                          c.check_comp_id = false;
                      }};
    ASSERT_TRUE(t143a::refused_before_active(d)) << "setup: no wait armed before Active";
    (void)d.s.feed(t143a::drained(t143a::drained_frame::other_sender, d.s.sending_time()));
    EXPECT_TRUE(d.closed()) << "with check_comp_id off a Logout from another CompID did not end it";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/compid");
}

// ── The give-up and the replay gate (T143a) ────────────────────────────────────────────────

namespace t143a {

// A direct acceptor whose answer to the peer's ResendRequest is held at its first retrieve (a
// GateStore), with a TestRequest pending and the ResendRequest an inbound frame since it.
// HeartBtInt and T are kSceneHb; the TestRequest is written at T, the request one second later.
struct HeldAnswer {
    std::shared_ptr<t105::gate_store_state> gst = std::make_shared<t105::gate_store_state>();
    t105::DirectRig d;
    std::future<fixpp::core::expected_t<void>> answer;
    bool up = false;

    HeldAnswer()
        : d{session_role::acceptor, [this](SessionConfig& c) {
                c.store_factory = std::make_shared<t105::GateStoreFactory>(gst);
                c.test_request_threshold = std::chrono::milliseconds{kSceneHb};
            }} {
        if (!d.to_active()) return;
        d.pass(kSceneHb);
        if (d.started("1") != 1U || !d.s.test_request_pending()) return;
        d.pass(std::chrono::seconds{1});
        gst->hold_when = [](t105::store_call k, fixpp::session::seqnum_t) {
            return k == t105::store_call::retrieve;
        };
        answer = d.s.feed_spawn(d.s.resend_request(d.s.next_in()));
        d.s.drain();
        up = gst->parked == 1U && session_test_access::replay_gate(*d.s.session).exclusive;
    }
    // From the request to strictly past the held TestRequest's cap (three thresholds and the
    // guard after the request), in one step: the give-up is decided there and waits at the gate.
    bool to_give_up_at_the_cap() {
        d.pass(3 * kSceneHb + std::chrono::milliseconds{1});
        return d.s.state() == fsm_state::Active && phase_of(d) == phase::writing &&
               session_test_access::replay_gate(*d.s.session).exclusive;
    }
    // Releases the held answer, then moves the clock past the write limit, so whatever the cell
    // left in progress ends before the Scene's teardown.
    void release() {
        gst->hold_when = nullptr;
        gst->release_all();
        d.s.drain();
        d.pass(std::chrono::milliseconds{d.s.cfg.logout_disconnect_timeout_ms});
    }
};

}  // namespace t143a

// [the held-cap wait's cancellation test] close(terminal) while the liveness loop waits at the
// replay gate with a TestRequest pending: close() returns with the clock still and the loop writes
// nothing more. The cancellation test is not the only exit on this path: close() wakes the gate and
// writes Disconnected before it emits, so the loop's next state test ends it as well.
TEST(LivenessTiming105Direct, CloseTerminalDuringTheHeldCapWait_LoopEndsNothingWritten) {
    t143a::HeldAnswer h;
    ASSERT_TRUE(h.up) << "setup: the answer's retrieve is not held";
    auto const waits = session_test_access::replay_gate(*h.d.s.session).timer_waits;
    // The Heartbeat instant, one HeartBtInt after the TestRequest's write, falls behind the held
    // answer with the TestRequest pending: the loop waits at the gate, bounded by the cap.
    h.d.pass(t143a::kSceneHb - std::chrono::seconds{1});
    bool const parked = session_test_access::replay_gate(*h.d.s.session).timer_waits > waits &&
                        h.d.s.state() == fsm_state::Active;
    auto const heartbeats = h.d.started("0");
    auto const closed =
        h.d.run(h.d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/heldcap");
    bool const loop_ended = session_test_access::liveness_count(*h.d.s.session) == 0;
    h.release();
    EXPECT_TRUE(parked) << "setup: the loop did not wait at the gate";
    EXPECT_TRUE(closed.has_value()) << "close() did not return with the clock still";
    EXPECT_TRUE(loop_ended) << "the liveness loop outlived close()";
    EXPECT_EQ(h.d.started("1"), 1U) << "a TestRequest after the held answer";
    EXPECT_EQ(h.d.started("0"), heartbeats) << "a Heartbeat after close()";
    EXPECT_EQ(h.d.started("5"), 0U) << "a Logout after close()";
}

// [session.hpp ending_in_progress_, its writing side; W-8 during R-1's write] The give-up decided
// at the cap is in its write (waiting at the gate for the held answer) when a frame from another
// CompID, an R-2 ending, arrives: it is absorbed, so the session stays Active with the give-up's
// write in progress. Once the answer is released the give-up writes its one Logout.
TEST(LivenessTiming105Direct, GiveUpWaitingAtTheGate_SecondEndingIsAbsorbed) {
    t143a::HeldAnswer h;
    ASSERT_TRUE(h.up) << "setup: the answer's retrieve is not held";
    bool const decided = h.to_give_up_at_the_cap();
    auto const r =
        h.d.s.feed(t143a::rgs_scene::raw("0", h.d.s.next_in(), "OTHER", h.d.s.sending_time()));
    auto const state = h.d.s.state();
    auto const ph = t143a::phase_of(h.d);
    bool const open = !h.d.closed();
    auto const logouts_during = h.d.started("5");
    h.release();
    EXPECT_TRUE(decided) << "setup: no give-up waiting at the gate";
    EXPECT_TRUE(r.has_value()) << "the absorbed ending returned an error";
    EXPECT_EQ(state, fsm_state::Active)
        << "the second ending ended the session during the give-up's write";
    EXPECT_EQ(ph, t143a::phase::writing) << "the give-up's write did not stay in progress";
    EXPECT_TRUE(open) << "the second ending closed the connection";
    EXPECT_EQ(logouts_during, 0U) << "a Logout during the held answer:" << t143a::wire_of(h.d);
    EXPECT_EQ(h.d.started("5"), 1U) << "the give-up's one Logout:" << t143a::wire_of(h.d);
}

// [the R-1 leaf, the session no longer Active] The give-up waits at the gate; the held retrieve
// then fails, so the answer's walk ends, releasing the gate, and the session ends. The leaf finds
// the session no longer Active, writes no Logout and leaves the ending state machine, so the
// connection closes with the clock still.
TEST(LivenessTiming105Direct, GiveUpWaitingAtTheGate_AnswerFails_LeafWritesNothingEofAtOnce) {
    t143a::HeldAnswer h;
    ASSERT_TRUE(h.up) << "setup: the answer's retrieve is not held";
    bool const decided = h.to_give_up_at_the_cap();
    bool const open_before = !h.d.closed();
    h.gst->fail_held = true;
    h.gst->release_all();
    h.d.s.drain();
    bool const closed = h.d.closed();
    auto const state = h.d.s.state();
    auto const ph = t143a::phase_of(h.d);
    h.release();
    EXPECT_TRUE(decided && open_before) << "setup: no give-up waiting at the gate";
    EXPECT_EQ(h.gst->calls_failed, 1U) << "setup: the held retrieve did not fail";
    EXPECT_TRUE(closed) << "the connection was not closed with the clock still";
    EXPECT_EQ(state, fsm_state::Disconnected);
    EXPECT_EQ(ph, t143a::phase::none) << "the give-up did not leave the ending state machine";
    EXPECT_EQ(h.d.started("5"), 0U) << "a Logout";
}

// [the bound's live predicate, Engine::stop noted] Engine::stop's first step noted during a wait
// armed after Active: at the bound the bound exits without its expiry, so it neither ends the
// session nor closes the connection; Engine::stop's own close owns both.
TEST(LogoutWait105Direct, EngineStopNotedDuringWait_BoundExitsWithoutItsExpiry) {
    t105::DirectRig d{t143a::session_role::acceptor};
    ASSERT_TRUE(d.to_active()) << "setup";
    (void)d.s.feed(t143a::stale(d, 2));
    bool const armed = d.completed("5") == 1U && t143a::phase_of(d) == t143a::phase::armed;
    session_test_access::note_engine_stop(*d.s.session);
    d.pass(std::chrono::milliseconds{d.s.cfg.logout_disconnect_timeout_ms});
    EXPECT_TRUE(armed) << "setup: no wait armed after the ending Logout";
    EXPECT_EQ(t143a::phase_of(d), t143a::phase::resolved) << "the bound did not exit at the bound";
    EXPECT_EQ(d.s.state(), fsm_state::Active)
        << "the bound ended the session with Engine::stop noted";
    EXPECT_FALSE(d.closed()) << "the bound closed the connection with Engine::stop noted";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/stop");
}

// [the bound's live predicate, the drain] drive_reconnect during a wait armed before Active (a
// direct initiator's LogonSent refusal): the drain's wake finds the drain in flight, so the bound
// exits and the drain, which joins it, returns with the clock still and the reconnect writes its
// Logon.
TEST(LogoutWait105Direct, DriveReconnectDuringAPreActiveWait_BoundYieldsToTheDrain) {
    t105::DirectRig d{t143a::session_role::initiator};
    ASSERT_TRUE(t143a::to_logon_sent(d)) << "setup: not in LogonSent";
    (void)d.s.feed(t143a::logon_reply(d.s.sending_time(std::chrono::seconds{-600})));
    bool const armed = d.completed("5") == 1U && d.s.state() == fsm_state::Disconnected &&
                       t143a::phase_of(d) == t143a::phase::armed;
    auto const r = d.run(d.s.session->drive_reconnect(), "T143a/drive_reconnect/drain");
    EXPECT_TRUE(armed) << "setup: no wait armed before Active";
    EXPECT_TRUE(r.has_value() && r->has_value())
        << "drive_reconnect did not return with the clock still";
    EXPECT_EQ(d.started("A"), 2U) << "the reconnect wrote no Logon";
    EXPECT_NE(t143a::phase_of(d), t143a::phase::armed) << "the wait outlived the drain";
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/drain");
}

// ── Before Active: the bound that cannot be spawned, and the NextExpectedMsgSeqNum Logout ──

namespace t143a {
// The LogonSent refusals of a direct initiator; each writes no Logout here, and the session ends.
// When a bound ran, its exit closes the connection at once; with no bound (its spawn refused) the
// connection closes at the session's next call: a direct Session has no pump to close it
// (contract C-1.c).
void expect_no_logout(t105::DirectRig& d, bool closed_at_once, const char* site) {
    EXPECT_EQ(d.started("5"), 0U) << site << ": a Logout was written:" << wire_of(d);
    EXPECT_EQ(d.s.state(), fsm_state::Disconnected) << site;
    if (closed_at_once) {
        EXPECT_TRUE(d.closed()) << site << ": the connection was not closed at once";
    }
    EXPECT_EQ(phase_of(d), phase::none) << site << ": the ending state machine was not left";
}
}  // namespace t143a

// [the LogonSent Logout leaf, its bound refused; clarification D-g] A Logon with an inaccurate
// SendingTime(52), the bound's spawn failing: no Logout.
TEST(LogoutWait105Direct, LogonSentSendingTimeRefusal_BoundSpawnFails_NoLogout) {
    t105::DirectRig d{t143a::session_role::initiator};
    ASSERT_TRUE(t143a::to_logon_sent(d)) << "setup: not in LogonSent";
    int seam_calls = 0;
    session_test_access::set_ending_bound_spawn_fails(*d.s.session, [&seam_calls] {
        ++seam_calls;
        return true;
    });
    (void)d.s.feed(t143a::logon_reply(d.s.sending_time(std::chrono::seconds{-600})));
    EXPECT_GE(seam_calls, 1) << "the reach proof: the bound's spawn was not attempted";
    t143a::expect_no_logout(d, false, "SendingTimeRefusal_BoundSpawnFails");
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/st-dg");
}

namespace t143a {
void with_789(SessionConfig& c) { c.enable_next_expected_msg_seq_num = true; }
}  // namespace t143a

// [the NextExpectedMsgSeqNum(789) Logout, its bound refused; ruling R-789, clarification D-g]
TEST(LogoutWait105Direct, Logon789TooHigh_BoundSpawnFails_NoLogout) {
    t105::DirectRig d{t143a::session_role::initiator, t143a::with_789};
    ASSERT_TRUE(t143a::to_logon_sent(d)) << "setup: not in LogonSent";
    int seam_calls = 0;
    session_test_access::set_ending_bound_spawn_fails(*d.s.session, [&seam_calls] {
        ++seam_calls;
        return true;
    });
    (void)d.s.feed(t143a::logon_reply(d.s.sending_time(), "789=999\x01"));
    EXPECT_GE(seam_calls, 1) << "the reach proof: the bound's spawn was not attempted";
    t143a::expect_no_logout(d, false, "789_BoundSpawnFails");
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/789-dg");
}

// [the NextExpectedMsgSeqNum(789) Logout, toAdmin throwing] The application's toAdmin throws on
// the Logout: none is written, and the session ends with the connection closed at once.
TEST(LogoutWait105Direct, Logon789TooHigh_ToAdminThrows_NoLogoutClosedAtOnce) {
    t105::DirectRig d{t143a::session_role::initiator, t143a::with_789};
    ASSERT_TRUE(t143a::to_logon_sent(d)) << "setup: not in LogonSent";
    d.app->to_admin_hook = [](std::string_view m) {
        if (m == "5") throw std::runtime_error("T143a: toAdmin on the Logout");
    };
    (void)d.s.feed(t143a::logon_reply(d.s.sending_time(), "789=999\x01"));
    EXPECT_EQ(d.app->to_admin_count("5"), 1) << "the reach proof: toAdmin was not called";
    t143a::expect_no_logout(d, true, "789_ToAdminThrows");
    // Past the bound, so an ending left in progress ends before close().
    d.pass(std::chrono::milliseconds{d.s.cfg.logout_disconnect_timeout_ms});
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/789-ta");
}

// [the NextExpectedMsgSeqNum(789) Logout, which does not fit] The peer's CompID is long enough that
// the Logout overflows honor_peer_next_expected_'s buffer while the session's Logon fits
// (the recipe of test_105_connection_close.cpp's too_low_fit).
namespace t143a {
constexpr std::size_t kLogoutBuffer = 256;  // honor_peer_next_expected_'s Logout buffer
// Its length is the Logout's Text(58) length for a one-digit expected number.
constexpr char k789Text[] = "NextExpectedMsgSeqNum too high, expecting 3 but received 999";
std::size_t overflowing_789_length(std::string const& st) {
    for (std::size_t len = 1; len < kLogoutBuffer; ++len) {
        std::string const p(len, 'P');
        std::array<std::byte, 4096> big{};
        auto const lo = fixpp::session::build_logout(big, 2, "ISLD", p, k789Text, "FIX.4.2", st);
        if (lo && lo->size() > kLogoutBuffer) {
            auto const on = fixpp::session::build_logon(big, 1, "ISLD", p, "FIX.4.2", 30, st);
            return on && on->size() <= kLogoutBuffer ? len : 0U;
        }
    }
    return 0U;
}
}  // namespace t143a

TEST(LogoutWait105Direct, Logon789TooHigh_LogoutDoesNotFit_NoLogoutClosedAtOnce) {
    t105::DirectRig d{t143a::session_role::initiator, t143a::with_789};
    std::size_t const len = t143a::overflowing_789_length(d.s.sending_time());
    ASSERT_NE(len, 0U) << "setup: no CompID length overflows the Logout and fits the Logon";
    std::string const peer(len, 'P');
    d.s.cfg.target_comp_id = peer;
    d.s.session = std::make_unique<fixpp::session::Session>(d.s.engine, d.s.cfg);
    ASSERT_TRUE(t143a::to_logon_sent(d)) << "setup: not in LogonSent";
    (void)d.s.feed(pr::to_bytes(pr::message("FIX.4.2", "A", 1, peer, "ISLD", d.s.sending_time(),
                                            "98=0\x01"
                                            "108=30\x01"
                                            "789=999\x01")));
    EXPECT_EQ(d.app->to_admin_count("5"), 0) << "the Logout was built";
    t143a::expect_no_logout(d, true, "789_LogoutDoesNotFit");
    // Past the bound, so an ending left in progress ends before close().
    d.pass(std::chrono::milliseconds{d.s.cfg.logout_disconnect_timeout_ms});
    (void)d.run(d.s.session->close(fixpp::session::close_mode::terminal), "T143a/close/789-fit");
}

// ── The establishment cap's failure exit (T143a) ───────────────────────────────────────────

namespace t143a {

// An Engine acceptor's posture refusal (TestMessageIndicator(464)=Y on a production session): its
// wait before Active is capped by the establishment deadline on the engine clock. The clock that
// arm names throws once at that arm's instant (the bound's instant on
// SessionConfig::clock_override, the cap's on the engine clock), so the capped race completes
// before its instant: the failure exit, which closes the connection at once with the clocks still.
enum class capped_arm : std::uint8_t { deadline, cap };

void capped_failure_cell(capped_arm arm) {
    pr::Rig rig;
    auto counting = std::make_shared<t105::CountingClock>(rig.clock);
    auto cfg = rig.cfg(session_role::acceptor);
    cfg.posture = fixpp::session::session_posture::production;
    cfg.logon_timeout_ms = 10000;
    if (arm == capped_arm::deadline) {
        cfg.clock_override = counting;
    } else {
        rig.engine_clock_override = counting;
    }
    auto const instant =
        rig.clock->steady_now() + std::chrono::milliseconds{arm == capped_arm::deadline
                                                                ? cfg.logout_disconnect_timeout_ms
                                                                : cfg.logon_timeout_ms};
    bool const up = rig.start(std::move(cfg)) && rig.connect_peer();
    auto const calls_before = counting->sleep_calls_at(instant);
    if (up) counting->throw_once_at(instant);
    auto const mark = rig.peer.received.size();
    if (up) {
        rig.peer.send(rig.msg("A", 1,
                              "98=0\x01"
                              "108=30\x01"
                              "464=Y\x01"));
    }
    bool const refused = up && rig.run_until([&] {
        return pr::frames_of_type(rig.peer.received.substr(mark), "5").size() == 1U;
    });
    char const* const site =
        arm == capped_arm::deadline ? "T143a/CappedFailure/deadline" : "T143a/CappedFailure/cap";
    bool const eof = refused &&
                     t105::pump_until_peer_read_ends(
                         rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, site) &&
                     t105::ended_in_eof(rig.peer, site);
    bool const thrown = !counting->throw_pending();
    rig.stop();
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(refused) << "setup: no refusal Logout";
    EXPECT_EQ(calls_before, 0U) << "setup: a sleep at the arm's instant before the bound's";
    EXPECT_TRUE(thrown) << "the reach proof: the arm's sleep at its instant was never requested";
    EXPECT_TRUE(eof) << "the capped race's failure exit did not close the connection at once";
}

}  // namespace t143a

// [the capped bound's failure exit, its deadline arm]
TEST(LogoutWait105Acceptor, PreActiveCappedWait_DeadlineArmFails_EofAtOnce) {
    t143a::capped_failure_cell(t143a::capped_arm::deadline);
}
// [the capped bound's failure exit, its cap arm on the engine clock]
TEST(LogoutWait105Acceptor, PreActiveCappedWait_CapArmFails_EofAtOnce) {
    t143a::capped_failure_cell(t143a::capped_arm::cap);
}

}  // namespace
