// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/parity/resend_abort_on_failing_write_test.cpp — 016 T023 [US3].
//
// Parity GAP closure — QFJ-646: the resend reply aborts when the transport
// write returns false mid-resend (fixpp must not keep writing into a failed
// transport). Plumbing exists (session.cpp resend-reply loop: every
// transmit_async() failure does record_state_transition_(Disconnected) + return),
// so this witness flips the row GAP → COVERED.
//
// Standalone (no counterparty): an acceptor session is driven to Active, the
// outbound transport_send_ is armed to fail, and an inbound ResendRequest is
// fed. The session's resend reply attempts a write, the injected failure maps to
// a transmit_async() false, and the session aborts the resend → Disconnected.
//
// spec_ref [FIX-SL §4.3.5 message recovery] + QFJ-646.

#include "parity_support.hpp"

namespace fixpp::interop::parity {
namespace {

using ResendAbortOnFailingWrite = ParityAcceptorFixture;

TEST_F(ResendAbortOnFailingWrite, FailedWriteMidResendAbortsAndDisconnects) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s)) << "precondition: session must reach Active";
    ASSERT_EQ(s.state(), fixpp::session::fsm_state::Active);

    const std::size_t writes_at_logon = capture.frames.size();

    // Arm the transport to fail the next write — the resend reply that the
    // ResendRequest below triggers. transmit_async() wraps the sync send in
    // try/catch and maps the throw to a write failure (session.cpp ~1781).
    capture.fail_writes = true;

    // Peer asks us to replay [2..0] (through current outbound). The reply write
    // hits the armed failure.
    (void)feed(s, make_resend_request("FIX.4.2", 2, "TW", "ISLD", /*begin=*/2, /*end=*/0));

    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Disconnected)
        << "a failed write mid-resend MUST abort the resend and disconnect "
           "(QFJ-646); session stayed in state=" << static_cast<int>(s.state());

    // The abort means no further resend frames were captured after the failure
    // (the failing write threw before append; the session did not keep writing).
    EXPECT_EQ(capture.frames.size(), writes_at_logon)
        << "resend must not continue writing into a failed transport";
}

}  // namespace
}  // namespace fixpp::interop::parity
