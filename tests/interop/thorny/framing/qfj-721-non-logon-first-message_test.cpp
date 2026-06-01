// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/thorny/framing/qfj-721-non-logon-first-message_test.cpp
//   — 016 T021 [US2] thorny corpus.
//
// Provenance: quickfix-j#721 (closed-with-fix). Upstream bug + fix:
// `testNonLogonMessageNonFIXT` — QuickFIX-J hit a NullPointerException when the
// first message on a non-FIXT session was not a Logon.
//
// fixpp's spec-conformant behavior is clean refusal: an acceptor that has only
// been opened must not enter LogonReceived/Active when the peer's first message
// is Heartbeat(35=0). The test reaching its assertions without crashing is the
// no-NPE differentiator for this upstream issue.
//
// Standalone (no counterparty): drives crafted admin frames through
// Session::on_inbound_frame() via the shared parity ParityAcceptorFixture.
//
// category: Logon/Logout race / reject. disposition: pass.
// spec_ref [FIX-SL §4.5.1].

#include "parity/parity_support.hpp"

namespace fixpp::interop::thorny {
namespace {

using ThornyFramingFixture = fixpp::interop::parity::ParityAcceptorFixture;

TEST_F(ThornyFramingFixture, Qfj721_NonLogonFirstMessageRefusesWithoutCrash) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(run_open(s).has_value());

    (void)feed(s, fixpp::interop::parity::make_fix_frame("FIX.4.2", "0", /*seq=*/1, "TW", "ISLD"));

    const auto state = s.state();
    EXPECT_NE(state, fixpp::session::fsm_state::Active)
        << "first inbound non-Logon message must not establish the session";
    EXPECT_NE(state, fixpp::session::fsm_state::LogonReceived)
        << "first inbound non-Logon message must not enter the acceptor LogonReceived path";
}

}  // namespace
}  // namespace fixpp::interop::thorny
