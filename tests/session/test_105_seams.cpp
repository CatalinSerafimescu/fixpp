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

#include <array>
#include <asio/io_context.hpp>
#include <chrono>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "session/engine_loopback_harness.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/test_105_support.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::session_test_access;
namespace pr = fixpp::test_support::plain_rig;
namespace t105 = fixpp::test_support::t105;

constexpr std::chrono::seconds kHb{10};
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
    if (s) EXPECT_EQ(session_test_access::connection_close_count(*s), 0);
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
                  ioc, [&] { return h->server_endpoint().port != 0; }, 5s, 5ms, "bind");
    std::shared_ptr<t105::TlsPeer> peer;
    if (up) {
        peer = t105::TlsPeer::connect(ioc, h->transport_fixture(), h->server_endpoint());
        up = fixpp::test_support::pump_until(
                 ioc, [&] { return peer->ready || peer->setup_failed; }, 5s, 5ms, "connect") &&
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
            ioc, [&] { return state() == fsm_state::Active; }, 5s, 5ms, "logon");
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

}  // namespace
