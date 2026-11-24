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
#include "support/session_test_access.hpp"

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
              t105::ending_of(rig.peer) == t105::peer_ending::eof;
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

// [seam] The twin of
// TlsWriteLimit105.R2AfterActive_PeerStopsReading_AbortiveAtBoundPlusTlsCloseTimeout (tasks.md
// T054; orchestrator ruling 3 of Phase 4): the same scenario, with the instant the too-low Logout's
// write starts read through session_test_access::ending_wait_phase, which that cell's file cannot
// name. The bound is then pinned on both sides of the mock clock: at the bound
// - 1 ms the phase is still `writing`, the session Active and the peer's read open; at the bound
// the session ends. A widened or deleted write limit (T077) leaves the phase `writing` at the
// bound.
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
        open_before = state() == fsm_state::Active && !peer->read_ended;
        clock->advance(1ms);
        ended_at_bound = fixpp::test_support::pump_until(
            ioc, [&] { return state() != fsm_state::Active; }, 2500ms, 5ms, nullptr);
        peer->start_reading();
        (void)t105::pump_until_peer_read_ends(
            ioc, [&] { return peer->read_ended; }, 5s, "TlsWriteLimit105.SeamTwin");
    }
    EXPECT_TRUE(up) << "setup";
    EXPECT_TRUE(held) << "setup: no application send was held behind the stalled peer";
    EXPECT_TRUE(writing) << "setup: the too-low Logout's write did not start";
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
              t105::ending_of(rig.peer) == t105::peer_ending::eof;
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
            t105::ending_of(rig.peer) == t105::peer_ending::eof;
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

// ════════════════════════════════════════════════════════════════════════════════════════
// 105 T097 (research D6; data-model E-4): the default threshold helper and the give-up instant's
// guard. constexpr cells: each static_assert sits under FIXPP_105_STATIC_GUARDS, and each cell's
// runtime twin asserts the same values with EXPECT_*.
// ════════════════════════════════════════════════════════════════════════════════════════

namespace ltg {
using fixpp::session::detail::default_test_request_threshold;
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
}  // namespace ltg

#if FIXPP_105_STATIC_GUARDS
static_assert(ltg::default_threshold_saturates());
static_assert(ltg::guard_at_the_saturated_instant());
static_assert(ltg::fresh_window_is_the_later_instant());
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

}  // namespace
