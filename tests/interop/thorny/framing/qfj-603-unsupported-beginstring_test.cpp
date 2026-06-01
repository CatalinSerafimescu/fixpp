// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/thorny/framing/qfj-603-unsupported-beginstring_test.cpp
//   — 016 T021 [US2] thorny corpus.
//
// Provenance: quickfix-j#603 (closed-with-fix). Upstream bug + fix:
// `testUnsupportedVersion` — unsupported BeginString on the first Logon should
// refuse session establishment rather than logging on.
//
// fixpp's spec-conformant behavior is the refusal outcome: an acceptor that has
// only been opened must not enter LogonReceived/Active when the peer's Logon uses
// unsupported BeginString FIX.3.9, and it must not emit a Logon reply.
//
// Standalone (no counterparty): drives crafted admin frames through
// Session::on_inbound_frame() via the shared parity ParityAcceptorFixture.
//
// category: reject/negotiation. disposition: pass.
// spec_ref [FIX-SL §4.5.4].

#include "parity/parity_support.hpp"

namespace fixpp::interop::thorny {
namespace {

using ThornyFramingFixture = fixpp::interop::parity::ParityAcceptorFixture;

TEST_F(ThornyFramingFixture, Qfj603_UnsupportedBeginStringRefusesLogon) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(run_open(s).has_value());

    (void)feed(s, fixpp::interop::parity::make_logon("FIX.3.9", /*seq=*/1, "TW", "ISLD"));

    const auto state = s.state();
    EXPECT_NE(state, fixpp::session::fsm_state::Active)
        << "unsupported BeginString must not establish the session";
    EXPECT_NE(state, fixpp::session::fsm_state::LogonReceived)
        << "unsupported BeginString must not enter the acceptor LogonReceived path";
    EXPECT_EQ(capture.count_msg_type("A"), 0U)
        << "fixpp must not emit a Logon reply for an unsupported BeginString";
}

}  // namespace
}  // namespace fixpp::interop::thorny
