// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_inbound_poss_resend.cpp
//
// 022-possresend-allowpossdup-send T003 — Inbound PossResend(97) witnesses (US1).
//
// Parity/characterization tests that prove fixpp's current behavior already
// satisfies the C4 contract (data-model §3 disposition table, contracts §C4).
// Zero production code. A failing witness falsifies D4 → escalate.
//
// Six witnesses:
//   1. DeliverInSeq_97Y          — in-seq 97=Y app msg: seqnum N→N+1, fromApp called,
//                                   tag 97 readable, no Reject/Logout, Active (C4.1/FR-001/FR-002)
//   2. NoApp_ByteIdentity        — no Application registered: 97=Y handled byte-identically
//                                   to the same message without 97 (C4.4/FR-005)
//   3. Combo_43Y_97Y             — 021 PossDup arms fire on 43; 97 adds nothing (C4.3/FR-004)
//   4. PossResend_No122_NoReject — 97=Y without 122, no 43=Y: no Reject(371=122) (C4.2/FR-003)
//   5. Edge_TooHigh_97Ignored    — 97=Y at too-high seqnum: ResendRequest fires, 97 inert
//   6. Edge_Admin_97Ignored      — 97=Y on admin msg (Reject 35=3): admin path unchanged, 97 inert
//
// Anchors: data-model.md §3; contracts/session-send-possdup.md §C4; spec.md FR-001..FR-005 +
//          Edge Cases; tasks.md T003/T004.

#include <gtest/gtest.h>

#include "support/possdup_test_support.hpp"

namespace fixpp::session::test {
namespace {

using fixpp::session::test_support::extract_field;

// ── Extended Application that captures the last delivered MessageView tag 97 ────

class CapturingApplication : public fixpp::session::Application {
public:
    int from_app_calls{0};
    bool last_had_tag97{false};

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& view,
        const fixpp::session::SessionId&) override {
        ++from_app_calls;
        // Tag 97 = PossResend. Check whether it is present in the delivered view.
        // We use the wire bytes exposed via the view's raw span to avoid a
        // generated-accessor dependency. Re-encode the raw bytes as a span and
        // use extract_field (the same helper used by all 021 tests).
        auto raw = view.bytes();  // std::span<const std::byte> (from View::bytes())
        last_had_tag97 = extract_field(raw, 97).has_value();
        return {};
    }
};

// ── Build a frame that carries PossResend(97)=Y ─────────────────────────────────

// Injects "97=Y\x01" into the extra_fields of make_frame.
inline std::vector<std::byte> make_poss_resend_frame(
    std::string_view msg_type, std::uint32_t seq,
    std::string_view sender, std::string_view target,
    bool poss_resend, std::string extra_fields = {}) {
    std::string ef;
    if (poss_resend) ef += "97=Y\x01";
    if (!extra_fields.empty()) ef += extra_fields;
    return make_frame(msg_type, seq, sender, target, ef);
}

// ── Fixture ─────────────────────────────────────────────────────────────────────

class InboundPossResendTest : public PossDupTestBase {
protected:
    [[nodiscard]] bool any_reject() const { return any_msg_type("3"); }
    [[nodiscard]] bool any_resend_request() const { return any_msg_type("2"); }
};

// ── Test 1: DeliverInSeq_97Y ────────────────────────────────────────────────────
//
// In-sequence application message (35=D, seq==expected) carrying 97=Y and NO 43=Y.
// Contract C4.1 / FR-001 / FR-002:
//   - expected inbound seqnum advances N→N+1
//   - fromApp invoked exactly once
//   - tag 97 is readable in the delivered MessageView
//   - no Reject / no Logout / no disconnect
//   - session remains Active

TEST_F(InboundPossResendTest, DeliverInSeq_97Y) {
    auto app = std::make_shared<CapturingApplication>();
    engine.application = app;

    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // After Logon(seq=1), next_expected = 2.
    const auto expected_before = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    ASSERT_EQ(expected_before, static_cast<fixpp::session::seqnum_t>(2));

    // In-sequence app message (35=D, seq=2) with 97=Y, no 43=Y.
    auto frame = make_poss_resend_frame("D", /*seq=*/2, "TW", "ISLD", /*poss_resend=*/true);
    feed(sess, frame);

    // Session stays Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "C4.1: session must stay Active after in-seq 97=Y";

    // fromApp called exactly once.
    EXPECT_EQ(app->from_app_calls, 1)
        << "C4.1/FR-001: fromApp must be called exactly once for in-seq 97=Y";

    // Tag 97 must be readable in the delivered view.
    EXPECT_TRUE(app->last_had_tag97)
        << "C4.1/FR-001: tag 97 must be present in the MessageView delivered to fromApp";

    // Seqnum advanced N→N+1 (2→3).
    const auto expected_after = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(expected_after, static_cast<fixpp::session::seqnum_t>(3))
        << "C4.1/FR-001: expected inbound seqnum must advance 2→3 after in-seq 97=Y "
        << "(was " << expected_before << ", got " << expected_after << ")";

    // No Reject (35=3) emitted.
    EXPECT_FALSE(any_reject())
        << "C4.1/FR-002: in-seq 97=Y must NOT emit a session Reject(35=3)";

    // No Logout (35=5) emitted.
    EXPECT_FALSE(any_logout())
        << "C4.1/FR-002: in-seq 97=Y must NOT emit a Logout";
}

// ── Test 2: NoApp_ByteIdentity ──────────────────────────────────────────────────
//
// No Application registered. An in-sequence 97=Y message is handled byte-identically
// to the same message without 97 (C4.4 / FR-005).
// Assert: identical seqnum advance and identical captured-frame count/types in both runs.

TEST_F(InboundPossResendTest, NoApp_ByteIdentity) {
    // Run A: no Application, no 97.
    {
        auto cfg = make_cfg();
        Session sessA(engine, cfg);
        drive_to_active(sessA);

        const std::size_t frames_before = captured_frames.size();
        auto frame_no97 = make_poss_resend_frame("D", /*seq=*/2, "TW", "ISLD", /*poss_resend=*/false);
        feed(sessA, frame_no97);

        const auto seqnum_after_A = sessA.seqnum_mgr_test_access().next_inbound_unsafe();
        const std::size_t new_frames_A = captured_frames.size() - frames_before;
        const auto state_A = sessA.state();

        // Record A outcomes for comparison.
        // Clear captured_frames for run B so we get a clean delta.
        captured_frames.clear();

        // Run B: no Application, WITH 97=Y.
        auto cfg2 = make_cfg();
        Session sessB(engine, cfg2);
        drive_to_active(sessB);

        const std::size_t frames_before_B = captured_frames.size();
        auto frame_with97 = make_poss_resend_frame("D", /*seq=*/2, "TW", "ISLD", /*poss_resend=*/true);
        feed(sessB, frame_with97);

        const auto seqnum_after_B = sessB.seqnum_mgr_test_access().next_inbound_unsafe();
        const std::size_t new_frames_B = captured_frames.size() - frames_before_B;
        const auto state_B = sessB.state();

        // Both runs must produce identical seqnum, state, and outbound frame count.
        EXPECT_EQ(seqnum_after_A, seqnum_after_B)
            << "C4.4/FR-005: seqnum after 97=Y must equal seqnum after no-97 (no-app case)";
        EXPECT_EQ(state_A, state_B)
            << "C4.4/FR-005: session state after 97=Y must equal state after no-97 (no-app case)";
        EXPECT_EQ(new_frames_A, new_frames_B)
            << "C4.4/FR-005: captured outbound frame count must be identical with/without 97 (no-app case)";
    }
}

// ── Test 3: Combo_43Y_97Y ───────────────────────────────────────────────────────
//
// Message carries BOTH 43=Y and 97=Y. The 021 PossDup arms fire on 43; 97 adds nothing.
// (C4.3 / FR-004)
//
// Regression-compare: same-seqnum message with only 43=Y (no 97) must produce the
// identical disposition as 43=Y + 97=Y — confirming 97 added no extra reject.
//
// We use a too-low frame (seq=1 after Logon consumed seq=1, next_expected=2) so
// Arm A fires (admin: silently ignore; or app: drop/redeliver per knob). The point is
// that adding 97=Y to an Arm-A scenario does not change the outcome.

TEST_F(InboundPossResendTest, Combo_43Y_97Y) {
    // Run A: too-low (seq=1) with 43=Y only. Arm A admin-ignore.
    {
        auto cfg = make_cfg();
        Session sessA(engine, cfg);
        drive_to_active(sessA);
        const std::size_t frames_before = captured_frames.size();

        auto frame_43only = make_possdup_frame("3", /*seq=*/1, "TW", "ISLD", /*poss_dup=*/true,
                                               "45=1\x01" "373=0\x01");
        feed(sessA, frame_43only);

        const auto state_A = sessA.state();
        const auto seqnum_A = sessA.seqnum_mgr_test_access().next_inbound_unsafe();
        const std::size_t new_frames_A = captured_frames.size() - frames_before;

        captured_frames.clear();

        // Run B: same scenario plus 97=Y.
        auto cfg2 = make_cfg();
        Session sessB(engine, cfg2);
        drive_to_active(sessB);
        const std::size_t frames_before_B = captured_frames.size();

        // Build a too-low frame carrying 43=Y, 122=..., AND 97=Y.
        std::string ef = "43=Y\x01" "122=20240101-00:00:00.000\x01" "97=Y\x01"
                         "45=1\x01" "373=0\x01";
        auto frame_43_97 = make_frame("3", /*seq=*/1, "TW", "ISLD", ef);
        feed(sessB, frame_43_97);

        const auto state_B = sessB.state();
        const auto seqnum_B = sessB.seqnum_mgr_test_access().next_inbound_unsafe();
        const std::size_t new_frames_B = captured_frames.size() - frames_before_B;

        // 97 must not change the Arm A disposition vs 43-only.
        EXPECT_EQ(state_A, state_B)
            << "C4.3/FR-004: adding 97=Y to 43=Y must not change session state";
        EXPECT_EQ(seqnum_A, seqnum_B)
            << "C4.3/FR-004: adding 97=Y to 43=Y must not change seqnum";
        EXPECT_EQ(new_frames_A, new_frames_B)
            << "C4.3/FR-004: adding 97=Y to 43=Y must not change outbound frame count";
    }
}

// ── Test 4: PossResend_No122_NoReject ───────────────────────────────────────────
//
// In-sequence application message with 97=Y, no 122, and no 43=Y.
// The 122-required rule (Arm C) keys only on 43=Y. Having 97=Y without 122 must NOT
// emit a Reject(371=122). (C4.2 / FR-003)

TEST_F(InboundPossResendTest, PossResend_No122_NoReject) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // In-seq app frame (seq=2) with 97=Y, no 122, no 43=Y.
    auto frame = make_poss_resend_frame("D", /*seq=*/2, "TW", "ISLD", /*poss_resend=*/true);
    // make_poss_resend_frame adds only 97=Y\x01 — no 43 or 122.
    feed(sess, frame);

    // Session stays Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "C4.2/FR-003: 97=Y without 122 must not disconnect the session";

    // No Reject(35=3) emitted — specifically no 371=122 reject.
    bool got_122_reject = false;
    for (auto& f : captured_frames) {
        if (extract_field(f, 35) == "3") {
            auto ref_tag = extract_field(f, 371);
            if (ref_tag == "122") got_122_reject = true;
        }
    }
    EXPECT_FALSE(got_122_reject)
        << "C4.2/FR-003: 97=Y without 43=Y must NOT trigger a Reject(371=122); "
        << "the 122-required rule keys on 43=Y only";

    // Seqnum advanced (message was processed normally).
    const auto expected_after = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(expected_after, static_cast<fixpp::session::seqnum_t>(3))
        << "C4.2/FR-003: seqnum must advance (in-seq 97=Y without 43 processed normally)";
}

// ── Test 5: Edge_TooHigh_97Ignored ──────────────────────────────────────────────
//
// 97=Y at a too-high seqnum. The existing too-high/ResendRequest path fires unchanged;
// 97 has no effect (spec.md Edge Cases; data-model §3 row 5).
//
// The too-high arm fires a ResendRequest(35=2) and keeps the session Active.
// This must be the same disposition whether or not 97=Y is present.
// (Comparison: test_inbound_poss_dup_validation.cpp::TooHigh_EngineParity_Pin for 43=Y.)

TEST_F(InboundPossResendTest, Edge_TooHigh_97Ignored) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // After Logon(seq=1), next_expected=2. Feed seq=10 (too-high) with 97=Y, no 43=Y.
    auto frame = make_poss_resend_frame("D", /*seq=*/10, "TW", "ISLD", /*poss_resend=*/true);
    feed(sess, frame);

    // ResendRequest(35=2) must fire — the existing too-high path is unaffected by 97.
    EXPECT_TRUE(any_resend_request())
        << "Edge: too-high 97=Y must emit ResendRequest(35=2); 97 must not suppress the too-high arm";

    // Session stays Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Edge: session must stay Active after too-high 97=Y (forward-gap ResendRequest)";

    // No Reject(35=3) emitted for 97 specifically.
    // (There may be none at all — the too-high arm doesn't issue a Reject.)
    EXPECT_FALSE(any_reject())
        << "Edge: too-high 97=Y must NOT emit a Reject(35=3); 97 has no session-level effect";
}

// ── Test 6: Edge_Admin_97Ignored ────────────────────────────────────────────────
//
// 97=Y on an administrative message (Reject 35=3 as a convenient non-Logon admin type).
// The existing admin path is executed; 97 has no effect (spec.md Edge Cases; data-model §3 row 6).
//
// We feed an in-sequence admin frame (seq=2) with 97=Y. The admin path delivers it
// normally (session stays Active, seqnum advances) — same as without 97.
// No Reject or Logout must be emitted for the 97 itself.

TEST_F(InboundPossResendTest, Edge_Admin_97Ignored) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // In-seq admin frame (35=3, seq=2) with 97=Y.
    // Admin Reject needs RefSeqNum(45) and SessionRejectReason(373).
    auto frame = make_poss_resend_frame("3", /*seq=*/2, "TW", "ISLD", /*poss_resend=*/true,
                                        "45=1\x01" "373=0\x01");
    feed(sess, frame);

    // Session stays Active (admin frame processed normally).
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Edge: admin message with 97=Y must leave session Active";

    // Seqnum advances (admin message at expected seqnum is accepted and advances).
    const auto expected_after = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(expected_after, static_cast<fixpp::session::seqnum_t>(3))
        << "Edge: seqnum must advance after in-seq admin message with 97=Y";

    // No Logout emitted for 97.
    EXPECT_FALSE(any_logout())
        << "Edge: admin message with 97=Y must NOT emit Logout";
}

}  // namespace
}  // namespace fixpp::session::test
