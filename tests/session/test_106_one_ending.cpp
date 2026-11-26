// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_one_ending.cpp — 106-logon-guards-write-bounds (tasks.md T003, T075).
//
// The one-ending cells (research D11; contract C-4.6) that compile on the base: write_timeout_ms
// unset. A pair that needs the limit is written in tasks.md T105. Each asserts FR-017: one new
// Disconnected entry, onLogout at most once (once from Active), one close, and the reason records
// FR-017 names (one for the give-up; none for close()).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "session/resend_gap_scene.hpp"
#include "session/test_106_support.hpp"
#include "support/session_executor_of.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::session_role;
namespace t106 = fixpp::test_support::t106;
namespace rgs = fixpp::test_support::resend_gap;

class OneEnding106 : public ::testing::TestWithParam<session_role> {};

// tasks.md T075: the give-up decided while the loop's TestRequest write is held, then close() at
// C = G + 100 ms, inside B = 5000 ms (logout_disconnect_timeout_ms), the mock clock stepped to
// G + B at once after close() was spawned. close() takes no claim: one Disconnected entry, onLogout
// once, the give-up's one slot-74 record, the transport closed, close() returned. Inequality: C
// precedes G + B by 4.9 s, so close() runs while the give-up's ending is writing; close()'s grace
// is a wall-clock timer of B, which the pump's budget exceeds.
TEST_P(OneEnding106, GiveUpThenClose) {
    t106::GiveUpParams p;
    p.logout_ms = 5000;
    t106::GiveUpRig r{GetParam(), p};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    auto const t0 = r.now();
    r.d.hold_write_of("1");
    r.at(t106::saturating_add(t0, r.threshold()));
    ASSERT_TRUE(r.d.parked() && r.d.started("1") == 1U) << "setup: the TestRequest is not held";
    auto const disconnected_before = r.disconnected();
    auto const logouts_before = r.d.app->logouts;
    auto const g = t106::give_up_instant_emit_not_returned(r.last_inbound(), r.threshold());
    r.at(g);
    EXPECT_EQ(r.records(), 1U) << "no give-up record at G";
    r.at(g + 100ms);
    auto close_fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(r.session(), r.d.s.ioc),
                       r.session().close(fixpp::session::close_mode::graceful), asio::use_future);
    r.at(g + std::chrono::milliseconds{r.p.logout_ms});
    EXPECT_TRUE(r.d.s.pump_until([&] { return rgs::Scene::ready(close_fut); }, 15s))
        << "close() did not return";
    r.d.s.drain();
    EXPECT_TRUE(r.d.closed()) << "the transport is open";
    EXPECT_EQ(r.d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
    EXPECT_EQ(r.d.app->logouts - logouts_before, 1) << "onLogout calls";
    EXPECT_EQ(r.records(), 1U) << "give-up records";
}

// tasks.md T089 (research D11 N-12): the loop's give-up block meets a claim held elsewhere in the
// state that leaves it reachable: close() between setting closing and its Disconnected record,
// with the session still Active. close()'s phase 1 leaves Active once its Logout has been written
// (LogoutSent), so the cell holds that Logout's write. The TestRequest is written at L + T, so the
// loop sleeps to G = L + 2T + 1 ns (105's instant for a written TestRequest); close(graceful) is
// spawned at C = G - 100 ms and its Logout's write is held. At G the loop wakes into the block,
// whose claim is refused (closing_or_stopped_()): no slot-74 record, and the loop leaves there (its
// count drops to 0 at G, the one return the loop takes at G with the session Active and no ending
// phase entered). Inequalities: close()'s grace is a wall-clock timer of B = 2000 ms and the mock
// steps after close() are issued at once, so the grace has not run out at G; at its expiry close()
// closes the transport, which fails the held Logout. FR-017: one new Disconnected entry
// (close()'s), onLogout once, the transport closed, no reason record.
TEST_P(OneEnding106, CloseDuringGiveUpBlock_NoRecord) {
    t106::GiveUpParams p;
    p.logout_ms = 2000;
    t106::GiveUpRig r{GetParam(), p};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    auto const l = r.last_inbound();
    r.at(t106::saturating_add(l, r.threshold()));
    ASSERT_EQ(r.completed_since("1", 0), 1U) << "setup: the TestRequest was not written at L + T";
    auto const g =
        t106::give_up_instant_written(l, r.threshold(), t106::saturating_add(l, r.threshold()));
    auto const c = g - 100ms;
    r.at(c);
    ASSERT_EQ(r.d.s.state(), fsm_state::Active) << "setup: the session ended before C";
    ASSERT_EQ(fixpp::session::session_test_access::liveness_count(r.session()), 1)
        << "setup: the liveness loop is not running at C";
    auto const disconnected_before = r.disconnected();
    auto const logouts_before = r.d.app->logouts;
    r.d.hold_write_of("5");
    auto close_fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(r.session(), r.d.s.ioc),
                       r.session().close(fixpp::session::close_mode::graceful), asio::use_future);
    r.d.s.drain();
    ASSERT_TRUE(r.d.parked() && r.d.started("5") == 1U)
        << "setup: close()'s Logout is not held in its write";
    r.at(g - 1ns);
    ASSERT_EQ(r.d.s.state(), fsm_state::Active) << "setup: the session left Active before G";
    ASSERT_EQ(fixpp::session::session_test_access::liveness_count(r.session()), 1)
        << "setup: the liveness loop left before G";
    r.at(g);
    EXPECT_EQ(fixpp::session::session_test_access::liveness_count(r.session()), 0)
        << "the liveness loop did not leave at G: the give-up block was not reached";
    EXPECT_EQ(r.records(), 0U) << "the give-up block recorded with the claim held by close()";
    EXPECT_TRUE(r.d.s.pump_until([&] { return rgs::Scene::ready(close_fut); }, 15s))
        << "close() did not return";
    r.d.s.drain();
    EXPECT_TRUE(r.d.closed()) << "the transport is open";
    EXPECT_EQ(r.d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
    EXPECT_EQ(r.d.app->logouts - logouts_before, 1) << "onLogout calls";
    EXPECT_EQ(r.records(), 0U) << "give-up records";
}

INSTANTIATE_TEST_SUITE_P(BothRoles, OneEnding106,
                         ::testing::Values(session_role::acceptor, session_role::initiator),
                         [](::testing::TestParamInfo<session_role> const& i) {
                             return i.param == session_role::acceptor ? "Acceptor" : "Initiator";
                         });

}  // namespace
