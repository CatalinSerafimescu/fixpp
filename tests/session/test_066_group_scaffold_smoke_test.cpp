// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_066_group_scaffold_smoke_test.cpp
//
// 066-dict-backed-inbound-parse T001 — compile/smoke check for the shared
// test scaffolding: the group-bearing FIX44 ExecutionReport(35=8) frame
// builders (tests/support/fix44_group_frame_bodies.hpp) build byte-valid,
// framer-parseable frames, and the real-Session-dispatch harness
// (tests/session/support/group_dispatch_fixture.hpp) delivers them to an
// application callback through the SHIPPED path
// (Session::on_inbound_frame -> parse_and_dispatch_).
//
// HARD CONSTRAINT (research.md Decision 6 / [const Art VII §3]): this file
// asserts ONLY that the callback fires and a message is delivered — it makes
// NO assertion about group extents, membership, instance counts, or
// trailing-field absence. Those assertions are T004/T005/T010 and MUST be
// written and observed RED later, against the unchanged dict-free parse; an
// assertion here would destroy that RED-first proof.
//
// Anchors: tasks.md T001; spec.md US1 Independent Test; research.md
// Decision 1/2/6; contracts/inbound-parse.md C1/C3.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/fix44_group_frame_bodies.hpp"
#include "support/group_dispatch_fixture.hpp"

using fixpp::session::test066::GroupDispatchFixture;

namespace fixpp::session::test066 {
namespace {

// (a) NoLegs x2 + trailing TransactTime(60) frame: builds and dispatches.
TEST(GroupScaffoldSmoke, TwoLegsTrailingFrameDispatches) {
    GroupDispatchFixture f;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    ASSERT_EQ(f.app->from_app_calls, 0) << "no app message dispatched yet";

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    ASSERT_FALSE(frame.empty()) << "frame builder must produce a non-empty frame";

    f.feed(sess, frame);

    // Smoke assertion ONLY: the callback fired and the session survived.
    // NO assertion on group contents/counts/extents (T001 hard constraint).
    EXPECT_EQ(f.app->from_app_calls, 1) << "ExecutionReport must reach fromApp";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active) << "session must stay Active";
}

// (b) NoLegs x2 with an interior undeclared tag (entry #1) frame: builds and
// dispatches. Same smoke-only scope as (a).
TEST(GroupScaffoldSmoke, InteriorUndeclaredTagFrameDispatches) {
    GroupDispatchFixture f;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    auto suffix = fixpp_test_support::execution_report_interior_undeclared_tag_suffix();
    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    ASSERT_FALSE(frame.empty()) << "frame builder must produce a non-empty frame";

    f.feed(sess, frame);

    EXPECT_EQ(f.app->from_app_calls, 1) << "ExecutionReport (interior-undeclared variant) must "
                                           "reach fromApp";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active) << "session must stay Active";
}

}  // namespace
}  // namespace fixpp::session::test066
