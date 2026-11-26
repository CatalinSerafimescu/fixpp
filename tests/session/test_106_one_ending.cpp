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
    r.at(t106::saturating_add(t0, r.p.threshold));
    ASSERT_TRUE(r.d.parked() && r.d.started("1") == 1U) << "setup: the TestRequest is not held";
    auto const disconnected_before = r.disconnected();
    auto const logouts_before = r.d.app->logouts;
    auto const g = t106::give_up_instant_emit_not_returned(r.last_inbound(), r.p.threshold);
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

INSTANTIATE_TEST_SUITE_P(BothRoles, OneEnding106,
                         ::testing::Values(session_role::acceptor, session_role::initiator),
                         [](::testing::TestParamInfo<session_role> const& i) {
                             return i.param == session_role::acceptor ? "Acceptor" : "Initiator";
                         });

}  // namespace
