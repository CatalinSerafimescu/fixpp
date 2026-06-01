// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/parity/replay_subsumes_reorder_queue_test.cpp — 016 T024 [US3].
//
// Parity GAP closure — Bucket-4 MODEL CONFIRMATION: fixpp's store-replay +
// ResendRequest recovery subsumes QuickFIX's reorder-queue protocol OUTCOME.
// fixpp has NO reorder-queue implementation by design; instead an out-of-order
// (too-high) inbound drives the FIX resend protocol (AwaitingResend +
// ResendRequest), and a peer GapFill resynchronizes the session. This witness
// confirms that recovery OUTCOME (the reorder-queue end-state) is reached
// without a reorder queue.
//
// Per contracts/parity-disposition.md: "If the witness FAILS, stop and surface a
// scoped session-layer finding (do NOT silently absorb)." A failure here is a
// real finding, not a flake.
//
// Standalone (no counterparty). spec_ref [FIX-SL §4.5.3 gap detection /
// §4.8.1 ordered processing / §4.8.5 gap fill].

#include "parity_support.hpp"

namespace fixpp::interop::parity {
namespace {

using ReplaySubsumesReorderQueue = ParityAcceptorFixture;

// An out-of-order (too-high) inbound does NOT get queued in a reorder buffer;
// it drives the resend protocol: the session emits a ResendRequest and stays
// Active (AwaitingResend transient), then a peer GapFill resynchronizes it.
TEST_F(ReplaySubsumesReorderQueue, TooHighInboundRecoversViaResendNotReorderQueue) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);  // expecting seq 2

    // Peer sends a too-high message (seq=5 while we expect 2) — a gap of [2..4].
    // QuickFIX would queue messages > expected in a reorder buffer; fixpp instead
    // detects the gap and recovers via ResendRequest. Use a Heartbeat as the
    // out-of-order admin carrier.
    (void)feed(s, make_fix_frame("FIX.4.2", "0", /*seq=*/5, "TW", "ISLD"));

    EXPECT_GE(capture.count_msg_type("2"), 1U)
        << "too-high inbound must drive a ResendRequest(35=2) (resend recovery), "
           "not silent reorder-queue buffering";
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "gap recovery is an AwaitingResend transient on Active — not a disconnect";
    EXPECT_EQ(next_inbound(s), 2U)
        << "the counter must NOT advance past the gap until it is filled";

    // Peer fills the gap with SequenceReset-GapFill{NewSeqNo=6} covering [2..5].
    // The session resynchronizes to next-expected=6 and stays Active — the
    // reorder-queue OUTCOME (ordered resynchronization) reached via store-replay.
    (void)feed(s, make_sequence_reset("FIX.4.2", 2, "TW", "ISLD", /*new_seqno=*/6,
                                      /*gap_fill=*/true));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "after the gap is filled the session must be resynchronized + Active";
    EXPECT_EQ(next_inbound(s), 6U)
        << "GapFill NewSeqNo=6 must resynchronize next-expected-inbound to 6 "
           "(store-replay recovery subsumes the reorder-queue end-state)";
}

}  // namespace
}  // namespace fixpp::interop::parity
