// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/thorny/recovery/qfj-750-logout-seqnum-mismatch_test.cpp
//   — 016 T020 [US2] thorny corpus C-004.
//
// Provenance: quickfix-j#750 (closed-with-fix). Upstream bug + fix:
// `testLogoutMsgSeqNumTooHighOrLow` — QuickFIX-J special-cases an inbound Logout
// to DISCONNECT even when its MsgSeqNum is out of range.
//
// fixpp DIVERGES (spec-defensibly) and this witness documents the divergence
// (FR-018 — reconcile to the FIX session spec, NOT to a reference engine; a
// fixpp-vs-engine disagreement is encoded against the spec mandate):
//
//   * too-LOW MsgSeqNum  → Disconnected (session-fatal; matches both the spec's
//     lower-than-expected rule and QFJ-750).
//   * too-HIGH MsgSeqNum → the FIX-SL §4.5.3 sequence-gap rule takes precedence:
//     fixpp issues a ResendRequest and stays Active (AwaitingResend transient,
//     013 FR-009), recovering the gap BEFORE the Logout message-type is
//     dispatched. fixpp applies its general too-high policy uniformly rather than
//     QFJ-750's Logout special-case-to-disconnect. The Logout is not lost — it is
//     re-processed after the gap is filled.
//
// This is a documented divergence dispositioned `pass` (fixpp's behavior is
// spec-conformant), NOT a known-limitation. See thorny/CORPUS-INDEX.md C-004.
//
// Standalone (no counterparty): drives crafted admin frames through
// Session::on_inbound_frame() via the shared parity ParityAcceptorFixture.
//
// category: Logon/Logout race. disposition: pass (divergence documented).
// spec_ref [FIX-SL §4.5.3 (sequence gap recovery) / §4.7 (Logout)].

#include "parity/parity_support.hpp"

namespace fixpp::interop::thorny {
namespace {

using ThornyRecoveryFixture = fixpp::interop::parity::ParityAcceptorFixture;

// Arm: inbound Logout with a too-HIGH MsgSeqNum → gap-recovery precedence.
// fixpp emits a ResendRequest and stays Active (it does NOT take QFJ-750's
// disconnect-on-Logout-gap special-case path).
TEST_F(ThornyRecoveryFixture, Qfj750_LogoutTooHighSeqnumRecoversGapNotDisconnect) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s)) << "precondition: session must reach Active";
    ASSERT_EQ(next_inbound(s), 2U);

    // Peer Logout(35=5) at seq=999 — far above the expected next-inbound of 2.
    // The FIX-SL §4.5.3 sequence-gap rule takes precedence over the logout: fixpp
    // recovers the gap (ResendRequest) rather than special-casing the Logout to
    // disconnect (the QFJ-750 divergence).
    (void)feed(s, fixpp::interop::parity::make_fix_frame("FIX.4.2", "5", /*seq=*/999, "TW", "ISLD"));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "too-high Logout must trigger gap recovery (AwaitingResend on Active), "
           "not QFJ-750's disconnect-on-gap special-case";
    EXPECT_GE(capture.count_msg_type("2"), 1U)
        << "the sequence gap must drive a ResendRequest(35=2) before the Logout is dispatched";
    EXPECT_EQ(next_inbound(s), 2U)
        << "next-expected-inbound must NOT advance past the gap until it is filled";
}

// Arm: inbound Logout with a too-LOW MsgSeqNum still disconnects.
TEST_F(ThornyRecoveryFixture, Qfj750_LogoutTooLowSeqnumStillDisconnects) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s)) << "precondition: session must reach Active";
    ASSERT_EQ(next_inbound(s), 2U);

    // Peer Logout(35=5) at seq=1 — below the expected next-inbound of 2.
    (void)feed(s, fixpp::interop::parity::make_fix_frame("FIX.4.2", "5", /*seq=*/1, "TW", "ISLD"));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Disconnected)
        << "inbound Logout with a too-low MsgSeqNum must still disconnect (quickfix-j#750)";
}

}  // namespace
}  // namespace fixpp::interop::thorny
