// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/thorny/reject/qfj-557-generatereject-advances-seqnum_test.cpp
//   — 016 Gate-B/r1 [US2] thorny corpus C-102.
//
// Provenance: quickfix-j#557 (closed-with-fix). Upstream bug: QuickFIX-J's
// GenerateReject() did not advance the target (inbound) sequence number after
// emitting a Reject, so repeated invalid app messages at consecutive seqnums
// could desynchronize the inbound counter.
//
// fixpp disposition: PASS.
// fixpp advances next_inbound_unsafe() past each invalid message that triggers
// a session-level Reject(35=3), consistent with [FIX-SL §4.5.4].
//
// This witness drives two invalid app messages (35=D) at consecutive sequence
// numbers (seq=2, seq=3) in Active state and asserts:
//   1. Two Reject(35=3) frames are emitted.
//   2. next_inbound_unsafe() advances past BOTH messages (== 4 after two rejects).
//   3. The session remains Active throughout.
//
// category: reject. disposition: pass.
// spec_ref [FIX-SL §4.5.4].

#include "parity/parity_support.hpp"

namespace fixpp::interop::thorny {
namespace {

using ThornyRejectFixture = fixpp::interop::parity::ParityAcceptorFixture;

// C-102 witness: two consecutive invalid app messages each trigger a Reject
// AND advance the inbound sequence counter.
//
// Pre-condition: Active, next_inbound == 2.
// Action: feed app msg 35=D at seq=2, then 35=D at seq=3.
// Expected:
//   - Two Reject(35=3) frames emitted (one per invalid message).
//   - next_inbound_unsafe() == 4 (advanced past both messages).
//   - Session stays Active.
TEST_F(ThornyRejectFixture, Qfj557_GenerateRejectAdvancesInboundSeqnumOverRun) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s)) << "precondition: session must reach Active";
    // After Logon(seq=1), next-expected-inbound = 2.
    ASSERT_EQ(next_inbound(s), 2U) << "precondition: next_inbound must be 2 after Logon";

    const std::size_t before = capture.frames.size();

    // First invalid app message at seq=2 (35=D, NewOrderSingle — not a session admin msg).
    (void)feed(s, fixpp::interop::parity::make_fix_frame("FIX.4.2", "D", 2, "TW", "ISLD"));
    // Second invalid app message at seq=3.
    (void)feed(s, fixpp::interop::parity::make_fix_frame("FIX.4.2", "D", 3, "TW", "ISLD"));

    // Count Reject(35=3) frames emitted after our two invalid messages.
    std::size_t reject_count = 0;
    for (std::size_t i = before; i < capture.frames.size(); ++i) {
        if (fixpp::interop::parity::frame_is_msg_type(capture.frames[i], "3")) {
            ++reject_count;
        }
    }
    EXPECT_GE(reject_count, 2U)
        << "two consecutive invalid app messages must each produce a Reject(35=3); "
           "got " << reject_count << " — quickfix-j#557 seqnum-advancement gap";

    // The inbound counter must have advanced past BOTH messages.
    EXPECT_EQ(next_inbound(s), 4U)
        << "next_inbound_unsafe() must be 4 after two rejects (seq=2 + seq=3 both consumed); "
           "quickfix-j#557: GenerateReject advances target seqnum over a run";

    // Session must remain Active.
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "session must stay Active after two session-level Rejects";
}

}  // namespace
}  // namespace fixpp::interop::thorny
