// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/thorny/recovery/qfj-271-sequencereset-large-gapfill_test.cpp
//   — 016 T020 [US2] thorny corpus.
//
// Provenance: quickfix-j#271 (closed-with-fix). Upstream bug + fix:
// `testSequenceResetStackOverflow` — processing SequenceReset over a large
// queued backlog could recurse deeply enough to overflow the stack.
//
// fixpp DIVERGES architecturally: it uses store-replay recovery and has no
// inbound reorder queue to recursively drain. A large SequenceReset-GapFill
// therefore resynchronizes the expected inbound sequence number directly without
// a backlog-recursion path to overflow.
//
// Standalone (no counterparty): drives crafted admin frames through
// Session::on_inbound_frame() via the shared parity ParityAcceptorFixture.
//
// category: high-volume / SequenceReset. disposition: pass (divergence documented).
// spec_ref [FIX-SL §4.8.6].

#include "parity/parity_support.hpp"

namespace fixpp::interop::thorny {
namespace {

using ThornyRecoveryFixture = fixpp::interop::parity::ParityAcceptorFixture;

TEST_F(ThornyRecoveryFixture, Qfj271_LargeGapFillSequenceResetResynchronizesWithoutRecursion) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s)) << "precondition: session must reach Active";
    ASSERT_EQ(next_inbound(s), 2U);

    (void)feed(s, fixpp::interop::parity::make_sequence_reset("FIX.4.2",
                                                              /*seq=*/2,
                                                              "TW",
                                                              "ISLD",
                                                              /*new_seqno=*/20000,
                                                              /*gap_fill=*/true));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "large SequenceReset-GapFill should resynchronize without disconnecting";
    EXPECT_EQ(next_inbound(s), 20000U)
        << "GapFill NewSeqNo must become the next expected inbound sequence number";
}

}  // namespace
}  // namespace fixpp::interop::thorny
