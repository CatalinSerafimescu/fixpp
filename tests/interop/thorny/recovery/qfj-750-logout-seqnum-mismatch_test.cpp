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
//   * too-HIGH MsgSeqNum → feature 103 (FR-010, fixpp#577; research D4.2 to D4.5): fixpp
//     keeps a pending-Logout mark (the Logout's 34 and arrival time, not the frame), sends
//     its ResendRequest and stays Active, then acknowledges the Logout and records
//     Disconnected once NextNumIn passes the Logout's 34 or when
//     logout_disconnect_timeout_ms runs out, whichever is first; with that timeout 0 or no
//     clock, at once with no ResendRequest. QFJ-750 disconnects at once.
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

// Arm: inbound Logout with a too-HIGH MsgSeqNum (FR-010). The fixture's mock clock gives
// the session a clock, so the Logout is pending: a ResendRequest, Active, and the
// acknowledgement only when the clock reaches the bound, with no gap fill.
TEST_F(ThornyRecoveryFixture, Qfj750_LogoutTooHighSeqnum_RecoversTheGapThenAcknowledgesAtTheBound) {
    auto const cfg = make_acceptor_cfg();
    fixpp::session::Session s{engine, cfg};
    ASSERT_TRUE(drive_to_active(s)) << "precondition: session must reach Active";
    ASSERT_EQ(next_inbound(s), 2U);

    // Peer Logout(35=5) at seq=999 — far above the expected next-inbound of 2.
    (void)feed(s,
               fixpp::interop::parity::make_fix_frame("FIX.4.2", "5", /*seq=*/999, "TW", "ISLD"));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "a too-high Logout is pending until the gap passes it or the bound";
    EXPECT_EQ(capture.count_msg_type("2"), 1U) << "the sequence gap must draw one ResendRequest";
    EXPECT_EQ(capture.count_msg_type("5"), 0U) << "acknowledged before the bound";
    EXPECT_EQ(next_inbound(s), 2U)
        << "next-expected-inbound must NOT advance past the gap until it is filled";

    // The bound: logout_disconnect_timeout_ms from the Logout's arrival.
    clock->advance(std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms});
    EXPECT_TRUE(fixpp::test_support::pump_until(
        ioc,
        [&] {
            return s.state() == fixpp::session::fsm_state::Disconnected &&
                   fixpp::session::session_test_access::peer_logout_bound_count(s) == 0;
        },
        "Qfj750_LogoutTooHighSeqnum/bound"))
        << "no acknowledgement at the bound";
    EXPECT_EQ(capture.count_msg_type("5"), 1U) << "the acknowledgement was not written once";
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Disconnected);
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
