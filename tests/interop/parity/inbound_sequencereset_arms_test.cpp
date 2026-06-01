// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/parity/inbound_sequencereset_arms_test.cpp — 016 T025 [US3].
//
// Parity GAP closure — inbound SequenceReset(35=4) NewSeqNo(36) arms
// (>/=/<), confirming the S-023/#90 implementation covers the audit rows
// (FR-016/FR-017). The authoritative behavioral coverage lives in
// tests/session/test_inbound_sequence_reset.cpp (the S-023/#90 witness); this
// parity witness re-confirms the three canonical NewSeqNo arms in the interop
// suite as the COVERED citation for the parity matrix.
//
// Canonical arms (QuickFIX-cpp Session::nextSequenceReset):
//   NewSeqNo > expected  → advance next-expected-inbound (Reset mode bypasses
//                          seqnum ordering, FIX-SL §4.8.6)
//   NewSeqNo == expected → no-op
//   NewSeqNo < expected  → Reject(SessionRejectReason=5, ValueIsIncorrect)
//
// Standalone (no counterparty). spec_ref [FIX-SL §4.8.6 / §4.5.4].

#include "parity_support.hpp"

namespace fixpp::interop::parity {
namespace {

// A Reject(35=3) carrying SessionRejectReason(373)=5 (ValueIsIncorrect).
bool any_reject_value_incorrect(const OutboundCapture& cap) {
    for (const auto& f : cap.frames) {
        std::string wire(reinterpret_cast<const char*>(f.data()), f.size());
        if (wire.find("35=3\x01") != std::string::npos &&
            wire.find("373=5\x01") != std::string::npos) {
            return true;
        }
    }
    return false;
}

using InboundSequenceResetArms = ParityAcceptorFixture;

// Arm: NewSeqNo > expected (Reset mode) hard-sets next-expected-inbound and
// bypasses the too-high gate (no ResendRequest, stays Active).
TEST_F(InboundSequenceResetArms, NewSeqNoAboveExpectedAdvances) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);

    (void)feed(s, make_sequence_reset("FIX.4.2", 50, "TW", "ISLD", /*new_seqno=*/200,
                                      /*gap_fill=*/false));

    EXPECT_EQ(next_inbound(s), 200U) << "NewSeqNo > expected must advance next-expected-inbound";
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "Reset mode bypasses the too-high gate — must stay Active";
    EXPECT_EQ(capture.count_msg_type("2"), 0U) << "Reset mode must NOT emit a ResendRequest(35=2)";
}

// Arm: NewSeqNo == expected is a no-op (no Reject, counter unchanged).
TEST_F(InboundSequenceResetArms, NewSeqNoEqualExpectedIsNoOp) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);

    (void)feed(s, make_sequence_reset("FIX.4.2", 2, "TW", "ISLD", /*new_seqno=*/2,
                                      /*gap_fill=*/false));

    EXPECT_EQ(next_inbound(s), 2U) << "NewSeqNo == expected is a no-op";
    EXPECT_FALSE(any_reject_value_incorrect(capture)) << "NewSeqNo == expected must not Reject";
}

// Arm: NewSeqNo < expected → Reject(373=5), counter unmoved.
TEST_F(InboundSequenceResetArms, NewSeqNoBelowExpectedRejects) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);

    (void)feed(s, make_sequence_reset("FIX.4.2", 2, "TW", "ISLD", /*new_seqno=*/1,
                                      /*gap_fill=*/false));

    EXPECT_TRUE(any_reject_value_incorrect(capture))
        << "NewSeqNo < expected must emit Reject(35=3, 373=5 ValueIsIncorrect)";
    EXPECT_EQ(next_inbound(s), 2U) << "a below-expected NewSeqNo must not move the counter backward";
}

}  // namespace
}  // namespace fixpp::interop::parity
