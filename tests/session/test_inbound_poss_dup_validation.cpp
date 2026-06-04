// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_inbound_poss_dup_validation.cpp
//
// 021-inbound-possdup-origsendingtime T007 — Stage-1 Arm-C/D/E validation tests.
//
// Seven test cases:
//   1. ArmC_MissingOrigSendingTime     — 43=Y, missing 122 → Reject 371=122/373=1, Active
//   2. ArmD_OrigSendingTimeAfter52     — 43=Y, 122 > 52 strict → Reject 373=10 + Logout + Disc.
//   3. Boundary_122Equals52Accepted    — 122 == 52 → not Arm D; session survives (INV-4)
//   4. ArmE_SeqResetExempt             — 35=4 + 43=Y, no 122 → NOT rejected (Arm E)
//   5. AtExpected_ArmC_NoAdvance (AS4) — 34==expected, 43=Y, no 122 → Arm C + seqnum NOT adv.
//   6. AtExpected_ArmD (AS5)           — 34==expected, 43=Y, 122 > 52 → Arm D
//   7. TooHigh_EngineParity_Pin        — too-high seq, 43=Y, no 122 → ResendRequest(35=2), Active
//
// TDD: this file is written RED-first (T007); T008 impl makes it GREEN.
// Shared fixture + frame builders live in support/possdup_test_support.hpp.
//
// Anchors: data-model.md §1 Stage 1 rows 0–4; INV-3/INV-4;
//          contracts/session-possdup.md C1/C3; research.md D4/D5/D6; tasks.md T007.

#include <gtest/gtest.h>

#include "support/possdup_test_support.hpp"

namespace fixpp::session::test {
namespace {

using fixpp::session::test_support::extract_field;

// ── Fixture (adds reject/resend inspection on top of the shared base) ──────────

class PossDupValidationTest : public PossDupTestBase {
protected:
    [[nodiscard]] bool any_reject() const { return any_msg_type("3"); }
    [[nodiscard]] bool any_resend_request() const { return any_msg_type("2"); }

    // Find the LAST Reject frame and return its 371/373 values.
    struct RejectFields {
        std::string ref_tag_id;  // 371
        std::string reason;      // 373
    };
    [[nodiscard]] RejectFields find_last_reject() const {
        RejectFields r;
        for (auto& f : captured_frames) {
            if (extract_field(f, 35) == "3") {
                r.ref_tag_id = std::string(extract_field(f, 371).value_or(""));
                r.reason = std::string(extract_field(f, 373).value_or(""));
            }
        }
        return r;
    }
};

// ── Test 1: Arm C — 43=Y, missing 122 → Reject 371=122/373=1, session Active ──
//
// data-model.md §1 row 2; contracts C1 row 5; research D4.
// Any seqnum (here too-low seq=1 after Logon advanced to next_expected=2).
// Session must stay Active; seqnum NOT advanced.

TEST_F(PossDupValidationTest, ArmC_MissingOrigSendingTime) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // 43=Y, no 122 — Arm C: missing OrigSendingTime.
    // seq=1 (too-low). Stage-1 must fire before Stage-2.
    auto frame = make_frame("D", /*seq=*/1, "TW", "ISLD", "43=Y\x01");
    feed(sess, frame);

    // Session must stay Active (Arm C is survive-not-disconnect).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Arm C: session must stay Active after missing-122 reject";

    // A Reject(35=3) must be emitted.
    ASSERT_TRUE(any_reject()) << "Arm C: a Reject(35=3) must be emitted";

    // The Reject must carry 371=122 (RefTagID pointing to OrigSendingTime).
    auto rj = find_last_reject();
    EXPECT_EQ(rj.ref_tag_id, "122") << "Arm C: Reject must carry 371=122 (RefTagID=OrigSendingTime)";

    // The Reject must carry 373=1 (RequiredTagMissing).
    EXPECT_EQ(rj.reason, "1") << "Arm C: Reject must carry 373=1 (RequiredTagMissing)";

    // No Logout emitted (Arm C is survive, not disconnect).
    EXPECT_FALSE(any_logout()) << "Arm C: must NOT emit a Logout";

    // Expected inbound seqnum must NOT advance (contracts C1: verify-returns-false, QFJ:1843).
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(),
              static_cast<fixpp::session::seqnum_t>(2))
        << "Arm C: expected inbound seqnum must not advance";
}

// ── Test 2: Arm D — 43=Y, 122 > 52 strict → Reject 373=10 + Logout + Disconnected ──
//
// data-model.md §1 row 3; contracts C1 row 6; research D5.
// 52 = "20240101-00:00:00.000" (hardcoded in make_frame).
// 122 = "20240101-00:00:01.000" (1 second later = strictly after).

TEST_F(PossDupValidationTest, ArmD_OrigSendingTimeAfter52) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // 43=Y, 122 > 52 strict — Arm D.
    // seq=1 (too-low). Stage-1 fires before Stage-2.
    auto frame = make_frame("D", /*seq=*/1, "TW", "ISLD",
                            "43=Y\x01"
                            "122=20240101-00:00:01.000\x01");  // 122 > 52
    feed(sess, frame);

    // Session must transition to Disconnected (Arm D is fatal).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Arm D: session must transition to Disconnected";

    // A Reject(35=3) must be emitted.
    ASSERT_TRUE(any_reject()) << "Arm D: a Reject(35=3) must be emitted";

    // Reject must carry 371=122 (research D5: QFJ uses OrigSendingTime.FIELD).
    auto rj = find_last_reject();
    EXPECT_EQ(rj.ref_tag_id, "122") << "Arm D: Reject must carry 371=122";

    // Reject must carry 373=10 (SendingTimeAccuracyProblem).
    EXPECT_EQ(rj.reason, "10") << "Arm D: Reject must carry 373=10 (SendingTimeAccuracyProblem)";

    // A Logout(35=5) must also be emitted.
    EXPECT_TRUE(any_logout()) << "Arm D: must emit a Logout(35=5) before disconnecting";
}

// ── Test 3: Boundary — 122 == 52 → NOT Arm D (INV-4) ─────────────────────────
//
// data-model.md INV-4: strict > only; equality is accepted.
// 52 == 122 == "20240101-00:00:00.000". Session must stay Active, no Reject, no Logout.

TEST_F(PossDupValidationTest, Boundary_122Equals52Accepted) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // 43=Y, 122 == 52 exactly — should be accepted (Stage-1 row 4 "else" fallthrough).
    // seq=1 (too-low). Arm A admin path handles it after Stage-1.
    // Use msg_type=3 (admin) so Arm A ignores it silently.
    auto frame = make_frame("3", /*seq=*/1, "TW", "ISLD",
                            "43=Y\x01"
                            "122=20240101-00:00:00.000\x01"  // 122 == 52
                            "45=1\x01"
                            "373=0\x01");
    feed(sess, frame);

    // Session must stay Active (122==52 is not Arm D).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "INV-4: 122==52 boundary must not trigger Arm D; session stays Active";

    // No Reject emitted for the 122==52 case (Stage-1 passes, Arm A admin silently ignores).
    // No Logout either.
    EXPECT_FALSE(any_logout()) << "INV-4: 122==52 must NOT emit Logout";

    // The only captures are the outbound Logon; no Reject after Logon.
    EXPECT_FALSE(any_reject()) << "INV-4: 122==52 must NOT emit Reject(35=3)";
}

// ── Test 4: Arm E — 35=4 (SequenceReset) + 43=Y, no 122 → NOT rejected ────────
//
// data-model.md §1 row 0 / INV-3; research D6; contracts C3.
// A SequenceReset bearing PossDup is exempt from the 122 check — routes to
// the existing reset/gap-fill path. Assert no 371=122/373=1 Reject is emitted.

TEST_F(PossDupValidationTest, ArmE_SeqResetExempt) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Build a Reset-mode SequenceReset (35=4) with 43=Y and no 122.
    // Set NewSeqNo(36)=3 to advance the seqnum without gap-fill complications.
    auto frame = make_frame("4", /*seq=*/1, "TW", "ISLD",
                            "43=Y\x01"
                            "36=3\x01");  // NewSeqNo=3, no GapFillFlag → Reset mode
    feed(sess, frame);

    // No Reject with 371=122/373=1 must be emitted (Arm E exempt).
    bool arm_c_reject = false;
    for (auto& f : captured_frames) {
        if (extract_field(f, 35) == "3") {
            auto ref_tag = extract_field(f, 371);
            auto reason = extract_field(f, 373);
            if (ref_tag == "122" && reason == "1") {
                arm_c_reject = true;
            }
        }
    }
    EXPECT_FALSE(arm_c_reject)
        << "INV-3 / Arm E: SequenceReset(35=4) with 43=Y must NOT trigger the 371=122/373=1 reject";

    // No Logout either (SequenceReset is handled by existing reset/gap-fill path).
    EXPECT_FALSE(any_logout()) << "Arm E: SequenceReset(35=4) must not emit Logout";
}

// ── Test 5: At-expected AS4 — 34==expected, 43=Y, no 122 → Arm C + NO seqnum advance ──
//
// contracts C1 row 5 + C3 at-expected pin; data-model D2b.
// Stage-1 validation is seqnum-independent: fires at-expected too.
// QFJ Session.java:1843: verify-returns-false → expected NOT incremented.

TEST_F(PossDupValidationTest, AtExpected_ArmC_NoAdvance) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // After Logon(seq=1), next_expected = 2. Feed seq=2 with 43=Y, no 122.
    const auto expected_before = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    ASSERT_EQ(expected_before, static_cast<fixpp::session::seqnum_t>(2));

    auto frame = make_frame("D", /*seq=*/2, "TW", "ISLD", "43=Y\x01");
    feed(sess, frame);

    // Session must stay Active (Arm C survives).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "AS4: at-expected Arm C must stay Active";

    // Reject(35=3) emitted with 371=122/373=1.
    ASSERT_TRUE(any_reject()) << "AS4: at-expected Arm C must emit Reject(35=3)";
    auto rj = find_last_reject();
    EXPECT_EQ(rj.ref_tag_id, "122") << "AS4: Reject must carry 371=122";
    EXPECT_EQ(rj.reason, "1") << "AS4: Reject must carry 373=1";

    // CRITICAL: seqnum must NOT advance (verify-returns-false per QFJ:1843).
    const auto expected_after = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(expected_after, expected_before)
        << "AS4: expected inbound seqnum must NOT advance when Arm C fires at-expected "
        << "(was " << expected_before << ", got " << expected_after << ")";
}

// ── Test 6: At-expected AS5 — 34==expected, 43=Y, 122 > 52 → Arm D ──────────
//
// contracts C1 row 6 + C3 at-expected pin; data-model D2b.
// Stage-1 Arm D also fires at-expected.

TEST_F(PossDupValidationTest, AtExpected_ArmD) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Feed seq=2 (at-expected) with 43=Y and 122 > 52.
    auto frame = make_frame("D", /*seq=*/2, "TW", "ISLD",
                            "43=Y\x01"
                            "122=20240101-00:00:01.000\x01");  // 122 > 52
    feed(sess, frame);

    // Session must transition to Disconnected (Arm D).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "AS5: at-expected Arm D must transition to Disconnected";

    // Reject with 371=122/373=10.
    ASSERT_TRUE(any_reject()) << "AS5: at-expected Arm D must emit Reject(35=3)";
    auto rj = find_last_reject();
    EXPECT_EQ(rj.ref_tag_id, "122") << "AS5: Reject must carry 371=122";
    EXPECT_EQ(rj.reason, "10") << "AS5: Reject must carry 373=10";

    // Logout emitted before disconnect.
    EXPECT_TRUE(any_logout()) << "AS5: Arm D must emit Logout before disconnecting";
}

// ── Test 7: Too-high engine-parity pin ─────────────────────────────────────────
//
// User decision 2026-06-04: Stage-1 runs AFTER the too-high arm.
// A too-high 43=Y missing 122 must STILL emit ResendRequest(35=2) (not Reject/Logout).
// This pins that Stage-1 does NOT preempt the forward-gap ResendRequest path.
// contracts implicit; brief explicit user-decision anchor.

TEST_F(PossDupValidationTest, TooHigh_EngineParity_Pin) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // After Logon(seq=1), next_expected=2. Feed seq=10 (too-high) with 43=Y, no 122.
    // The too-high arm must fire (ResendRequest), NOT Stage-1 Arm C (no Reject 371=122).
    auto frame = make_frame("D", /*seq=*/10, "TW", "ISLD", "43=Y\x01");
    feed(sess, frame);

    // A ResendRequest(35=2) must be emitted.
    EXPECT_TRUE(any_resend_request())
        << "Engine-parity: too-high 43=Y missing 122 must emit ResendRequest(35=2), not Arm C reject";

    // Session must stay Active (too-high arm keeps Active per FR-009).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Engine-parity: session must stay Active after too-high (forward-gap ResendRequest)";

    // No Arm C reject must be emitted (Stage-1 does not preempt the too-high arm).
    bool arm_c_reject = false;
    for (auto& f : captured_frames) {
        if (extract_field(f, 35) == "3") {
            auto ref_tag = extract_field(f, 371);
            auto reason = extract_field(f, 373);
            if (ref_tag == "122" && reason == "1") {
                arm_c_reject = true;
            }
        }
    }
    EXPECT_FALSE(arm_c_reject)
        << "Engine-parity: Stage-1 Arm C must NOT fire before the too-high ResendRequest arm";
}

}  // namespace
}  // namespace fixpp::session::test
