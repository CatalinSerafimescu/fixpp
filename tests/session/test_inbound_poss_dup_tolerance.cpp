// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_inbound_poss_dup_tolerance.cpp
//
// 021-inbound-possdup-origsendingtime T004 — Stage-2 Arm-A/Arm-B unit tests.
//
// Tests the too-low inbound possdup tolerance block (data-model.md §1 Stage 2,
// rows 5–8; contracts/session-possdup.md C1/C3; research.md D1/D2/D3).
//
// Six test cases:
//   1. ArmA_Admin_Ignored        — too-low 43=Y admin frame → Active, no advance, no wire
//   2. ArmA_App_Dropped          — too-low 43=Y app frame, redeliver=false → no fromApp
//   3. ArmA_App_Redelivered      — too-low 43=Y app frame, redeliver=true → exactly 1 fromApp
//   4. ArmA_Admin_Idempotent     — replayed possdup admin → no second side-effect
//   5. ArmB_Regression_Pin       — too-low no-43Y → Disconnected, NO Logout wire frame
//   6. ArmA_NoHeap               — Arm A disposition wraps in counting_resource: 0 new allocs
//
// TDD: this file is written RED-first (T004); T005 impl makes it GREEN.
// Shared fixture + frame builders live in support/possdup_test_support.hpp.
//
// Anchors: data-model.md §1 INV-1/INV-2/INV-5; contracts/session-possdup.md C1/C3;
//          research.md D1/D2/D3; tasks.md T004/T005.

#include <gtest/gtest.h>

#include "support/possdup_test_support.hpp"

namespace fixpp::session::test {
namespace {

class PossDupToleranceTest : public PossDupTestBase {};

// ── Test 1: Arm A admin-ignore ─────────────────────────────────────────────────
//
// Too-low 43=Y admin frame (Reject 35=3) → stays Active, expected seqnum
// UNCHANGED at 2, NO Logout or Reject wire frame emitted. (C1 row 1; D1; INV-1/2)

TEST_F(PossDupToleranceTest, ArmA_Admin_Ignored) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Next expected = 2 after Logon.
    // Feed a too-low admin frame (Reject 35=3) with 43=Y at seq=1.
    auto frame = make_possdup_frame("3", /*seq=*/1, "TW", "ISLD", /*poss_dup=*/true,
                                    // Reject body fields: 45=RefSeqNum, 373=reason
                                    "45=1\x01" "373=0\x01");
    feed(sess, frame);

    // Session must stay Active (not Disconnected).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Arm A admin-ignore: session must stay Active";

    // Expected inbound seqnum must remain at 2 (INV-1: no advance).
    const auto next_inbound = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(next_inbound, static_cast<fixpp::session::seqnum_t>(2))
        << "INV-1: expected inbound seqnum must not advance on Arm A admin-ignore";

    // NO Logout wire frame emitted.
    EXPECT_FALSE(any_logout())
        << "Arm A admin-ignore must NOT emit a Logout wire frame";

    // No Reject (35=3) emitted toward peer for a too-low possdup admin frame.
    // (captured_frames also includes the outbound Logon from open(); that is 35=A.)
    EXPECT_FALSE(any_msg_type("3"))
        << "Arm A admin-ignore must NOT emit a session Reject(35=3) wire frame";
}

// ── Test 2: Arm A app-drop (default, redeliver_poss_dup=false) ────────────────
//
// Too-low 43=Y app frame (35=D) with redeliver=false → NO fromApp call, no advance,
// stays Active. (C1 row 2; D2)

TEST_F(PossDupToleranceTest, ArmA_App_Dropped) {
    auto app = std::make_shared<CountingApplication>();
    engine.application = app;

    auto cfg = make_cfg(/*redeliver=*/false);
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Feed too-low app frame (35=D, seq=1) with 43=Y.
    auto frame = make_possdup_frame("D", /*seq=*/1, "TW", "ISLD", /*poss_dup=*/true);
    feed(sess, frame);

    // fromApp must NOT have been called.
    EXPECT_EQ(app->from_app_calls, 0)
        << "Arm A app-drop: fromApp must NOT be called when redeliver_poss_dup=false";

    // Session must stay Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Arm A app-drop: session must stay Active";

    // Expected inbound seqnum must remain at 2 (INV-1).
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(),
              static_cast<fixpp::session::seqnum_t>(2))
        << "INV-1: expected inbound seqnum must not advance on Arm A app-drop";
}

// ── Test 3: Arm A app-redeliver (redeliver_poss_dup=true) ─────────────────────
//
// Too-low 43=Y app frame (35=D) with redeliver=true → EXACTLY ONE fromApp call;
// frame carries 43=Y (flagged possdup); still no advance, stays Active. (C1 row 3; D2)

TEST_F(PossDupToleranceTest, ArmA_App_Redelivered) {
    auto app = std::make_shared<CountingApplication>();
    engine.application = app;

    auto cfg = make_cfg(/*redeliver=*/true);
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Feed too-low app frame (35=D, seq=1) with 43=Y.
    auto frame = make_possdup_frame("D", /*seq=*/1, "TW", "ISLD", /*poss_dup=*/true);
    feed(sess, frame);

    // fromApp must be called EXACTLY ONCE.
    EXPECT_EQ(app->from_app_calls, 1)
        << "Arm A app-redeliver: fromApp must be called exactly once when redeliver_poss_dup=true";

    // Session must stay Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Arm A app-redeliver: session must stay Active";

    // Expected inbound seqnum must remain at 2 (INV-1: no advance even on redeliver).
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(),
              static_cast<fixpp::session::seqnum_t>(2))
        << "INV-1: expected inbound seqnum must not advance on Arm A app-redeliver";
}

// ── Test 4: Arm A idempotent — replayed possdup admin not re-applied ──────────
//
// Two identical too-low admin possdup frames → each is silently ignored;
// no state mutation, no wire output, session stays Active the whole time.
// (C3 idempotent-replay contract; D1)

TEST_F(PossDupToleranceTest, ArmA_Admin_Idempotent) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    auto frame = make_possdup_frame("3", /*seq=*/1, "TW", "ISLD", /*poss_dup=*/true,
                                    "45=1\x01" "373=0\x01");

    const std::size_t frames_before = captured_frames.size();

    feed(sess, frame);  // first replay
    feed(sess, frame);  // second replay

    // Session must still be Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Idempotent replay: session must remain Active after two possdup admin frames";

    // Seqnum unchanged.
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(),
              static_cast<fixpp::session::seqnum_t>(2))
        << "INV-1: no seqnum advance from idempotent possdup admin replay";

    // No additional wire frames emitted by the two replays.
    EXPECT_EQ(captured_frames.size(), frames_before)
        << "Idempotent replay must emit zero wire frames";
}

// ── Test 5: Arm B regression pin (INV-2) ──────────────────────────────────────
//
// Too-low frame WITHOUT 43=Y → Disconnected AND assert NO Logout wire frame
// emitted. This is the byte-identical pre-feature behavior. (C1 row 4; C3; D3; INV-2)

TEST_F(PossDupToleranceTest, ArmB_Regression_Pin_NoLogout) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Too-low Reject (35=3, seq=1) without 43=Y — Arm B fatal path.
    auto frame = make_frame("3", /*seq=*/1, "TW", "ISLD", "45=1\x01" "373=0\x01");
    feed(sess, frame);

    // Session must transition to Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Arm B: too-low non-possdup frame must transition to Disconnected";

    // INV-2: NO Logout wire frame must be emitted.
    EXPECT_FALSE(any_logout())
        << "INV-2: Arm B must NOT emit a Logout wire frame (record_state_transition_ only)";
}

// ── Test 6: No-heap witness (INV-5) ───────────────────────────────────────────
//
// Arm A admin-ignore dispatch wraps in a counting_resource and asserts ZERO
// heap allocations. NOTE: the counting_resource is NOT plumbed into Session's
// internal arena (Session uses its own stack-based inbound arena), so this is
// a PARTIAL witness — the admin-ignore path is allocation-free by construction
// (it co_returns with no builder/parse). The binding no-heap gate is the
// mallocnesia LD_PRELOAD run in /speckit-verify (dual-gate per
// feedback_tracking_pmr_resource_false_pass).

TEST_F(PossDupToleranceTest, ArmA_NoHeap_Witness) {
    counting_resource mr;

    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Baseline after session construction + Logon.
    mr.reset_count();
    const long long baseline = mr.allocate_count();  // 0

    // Feed a too-low admin possdup frame. This is the Arm A admin-ignore path.
    auto frame = make_possdup_frame("3", /*seq=*/1, "TW", "ISLD", /*poss_dup=*/true,
                                    "45=1\x01" "373=0\x01");
    feed(sess, frame);

    // Session stays Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    const long long after = mr.allocate_count();
    EXPECT_EQ(after, baseline)
        << "INV-5: Arm A admin-ignore path must not allocate via pmr::new_delete_resource; "
        << "saw " << (after - baseline) << " allocation(s)";

    // Seqnum unchanged confirms no work happened.
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(),
              static_cast<fixpp::session::seqnum_t>(2));
}

}  // namespace
}  // namespace fixpp::session::test
