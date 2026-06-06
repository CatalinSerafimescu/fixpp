// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_reset_on_logon_test.cpp — 024 T016+T017 [P].
//
// Live ResetOnLogon interop cells — both roles (SC-005 / FR-010 / C6.1, C6.2).
//
// T016 — reset_on_logon_initiator (C6.1):
//   fixpp INITIATOR with reset_on_logon=true against a live QFcpp/QFJ ACCEPTOR.
//   The fixpp outbound Logon carries ResetSeqNumFlag(141)=Y + MsgSeqNum=1.
//   The live acceptor accepts. Both sides resync from seqnum 1.
//   In-process witnesses:
//     (a) fixpp FSM reaches Active — the counterparty accepted the Logon (implicitly
//         confirms the counterparty saw 141=Y + 34=1 as expected; if either were absent
//         or wrong the counterparty would disconnect / Logout instead of reaching Active).
//     (b) Outbound seqnum >= 2 after Active (the Logon was sent at 34=1 and the
//         counter advanced; proves the reset ran and the Logon consumed seqnum 1).
//   The byte-level golden (141=Y + 34=1 verbatim field order) is asserted by the PARENT
//   gate against the proxy capture (research R1: no in-library wire-capture seam in the
//   interop fixture). Unit witness ResetOnLogon_Initiator_ResetsAndEmits141 asserts
//   the byte-level 141=Y + 34=1 directly via captured_frames.
//
// T017 — reset_on_logon_acceptor (C6.2):
//   A live QFcpp/QFJ INITIATOR sends a Logon with 141=Y + fresh 34=1. The fixpp
//   ACCEPTOR with reset_on_logon=true resets before validation and admits the fresh
//   34=1 (no too-low disconnect, no ResendRequest). Reaches Active. Resyncs from 1.
//   In-process witnesses:
//     (a) fixpp FSM reaches Active (no disconnect/Logout from fresh 34=1).
//     (b) Outbound seqnum advanced past Logon (acceptor sent its reply Logon).
//     (c) Inbound seqnum == 2 after the exchange (peer sent 34=1 Logon; next expected
//         is 2 — proves no ResendRequest was issued for the seqnum below 1).
//
// Both roles are value-parameterized over counterparty ∈ {quickfix-cpp, quickfix-j}
// using a SINGLE file (following the hp_fix44_logon_hb_logout_test.cpp template
// convention: one parameterized file covers both roles × both engines).
//
// LIVE CELLS: require a counterparty. INTEROP_REQUIRE_COUNTERPARTY skips with reason
// when the counterparty port env is absent (FR-023). Never a silent pass.
//
// Golden artifacts (parent harness captures at first paired live run):
//   happy/golden/RL-QFcpp-init-fix44-reset-on-logon.fix
//   happy/golden/RL-QFcpp-acc-fix44-reset-on-logon.fix
//   happy/golden/RL-QFj-init-fix44-reset-on-logon.fix
//   happy/golden/RL-QFj-acc-fix44-reset-on-logon.fix
// The parent diff asserts 141=Y + 34=1 verbatim under the {52,10} admin profile.
// Absent → skip:golden-not-yet-captured (diff_golden_or_skip convention).
//
// Parent harness MUST:
//   For fixpp-initiator cells (T016): set INTEROP_<TOKEN>_PORT + optionally
//     INTEROP_<TOKEN>_HOST pointing at the counterparty's TLS acceptor.
//     The counterparty must tolerate reset_on_logon=true (accept 141=Y Logons).
//   For fixpp-acceptor cells (T017): set INTEROP_FIXPP_PORT (or let OS-assign);
//     the counterparty initiator must send 141=Y + 34=1. Both QFcpp and QFJ support
//     ResetOnLogon=Y in their session configs — the parent harness sets that.
//
// Anchors: tasks.md T016/T017; contracts/reset-knobs.md C6.1/C6.2;
//          quickstart.md "Live interop cells — both roles";
//          FR-010/SC-005 (both-roles live-interop requirement).
//
// spec_ref [FIX-SL §4.3 Logon / §4.1.1 ResetSeqNumFlag].
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <tuple>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

// ── T016 — reset_on_logon_initiator (C6.1) ────────────────────────────────────
//
// fixpp INITIATOR with reset_on_logon=true. Both counterparties × initiator role.
// (The acceptor role is covered by T017 / ResetOnLogonAcceptor.)
//
// make_session_config_with_reset builds on the hp:: support helper, setting
// reset_on_logon=true after construction. This is minimal and backward-compatible:
// hp::make_session_config has not been modified; the extra field is set inline here.

class ResetOnLogonInitiator : public ::testing::TestWithParam<Counterparty> {};

TEST_P(ResetOnLogonInitiator, LogonCarries141AndResyncsFrom1) {
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023).
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    const auto endpoint = hp::cell_endpoint(counterparty, Role::fixpp_initiator);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    fixpp::interop::InteropEngineFixture fx;
    auto cfg = hp::make_session_config(Role::fixpp_initiator, "FIX.4.4", factory,
                                       fx.ioc().get_executor(), *endpoint);
    // ── 024 T016: set reset_on_logon knob ────────────────────────────────────
    cfg.reset_on_logon = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── In-process witness (a): FSM reaches Active (C6.1 / FR-010) ──────────
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon with reset_on_logon=true) against "
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    // ── In-process witness (b): outbound seqnum >= 2 after Active (C6.1 / C2.1)
    // After the reset the Logon was sent at MsgSeqNum=1 (seqnum 1 consumed);
    // peek_outbound() returns the NEXT outbound seqnum, which is >= 2 once the
    // session is Active. Asserting >= 2 (not just >= 1) proves the Logon was
    // actually sent (consumed seqnum 1) rather than merely that a session exists.
    // The unit witness ResetOnLogon_Initiator_ResetsAndEmits141 additionally
    // asserts 34=1 and 141=Y on the captured Logon frame bytes directly.
    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";
    EXPECT_GE(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{2})
        << "outbound seqnum should be >= 2 after Active (Logon at 34=1 was sent and "
           "consumed seqnum 1; the reset ran and next outbound advanced past 1)";

    // The golden assertion (141=Y + 34=1 verbatim, admin profile {52,10}) is
    // performed by the parent gate against the proxy capture. Golden file:
    //   happy/golden/RL-<cp>-init-fix44-reset-on-logon.fix
    const std::string cp_part   = (counterparty == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    const std::string cell_id   = "RL-" + cp_part + "-init-fix44-reset-on-logon";
    hp::diff_golden_or_skip(cell_id, hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ────────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, ResetOnLogonInitiator,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    [](const ::testing::TestParamInfo<Counterparty>& info) {
        return (info.param == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    });

// ── T017 — reset_on_logon_acceptor (C6.2) ─────────────────────────────────────
//
// fixpp ACCEPTOR with reset_on_logon=true. A live QFcpp/QFJ INITIATOR sends a
// Logon with 141=Y + fresh 34=1. fixpp resets before validation, admits 34=1,
// reaches Active. Both counterparties × acceptor role.
//
// The parent harness MUST configure its counterparty initiator with ResetOnLogon=Y
// (or equivalent) so the initiator sends 141=Y + 34=1. Both QFcpp and QFJ support
// this — the harness config is described in the MATRIX.md parent-harness section.

class ResetOnLogonAcceptor : public ::testing::TestWithParam<Counterparty> {};

TEST_P(ResetOnLogonAcceptor, AdmitsFresh34eq1AndResyncsFrom1) {
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023). For the
    // acceptor cell the counterparty is the initiator that connects to fixpp;
    // the probe token tests that the parent has signalled the counterparty is
    // available (it leases the port either way).
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    const auto endpoint = hp::cell_endpoint(counterparty, Role::fixpp_acceptor);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    fixpp::interop::InteropEngineFixture fx;
    auto cfg = hp::make_session_config(Role::fixpp_acceptor, "FIX.4.4", factory,
                                       fx.ioc().get_executor(), *endpoint);
    // ── 024 T017: set reset_on_logon knob on acceptor ────────────────────────
    // This causes fixpp to reset before check_inbound so the fresh peer 34=1 is
    // admitted without a too-low disconnect (C6.2 / C2.2 live-interop proof).
    cfg.reset_on_logon = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // For a fixpp-acceptor cell, the parent's counterparty-initiator connects to
    // fixpp's bound port. When the port is OS-assigned (INTEROP_FIXPP_PORT unset),
    // the parent must read it via Engine::acceptor_bound_endpoint() and relay it
    // to its initiator (acceptor rendezvous — see MATRIX.md).

    // ── In-process witness (a): FSM reaches Active — no disconnect on 34=1 ────
    // With reset_on_logon=true, the pre-validation reset runs before check_inbound,
    // so the peer's fresh 34=1 is admitted. If the reset were absent (or placed
    // after check_inbound) the session would disconnect on the too-low seqnum.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "acceptor did not reach Active on peer 141=Y + 34=1 Logon (C6.2 violated) "
        << "— check reset_on_logon pre-check_inbound placement; counterparty="
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    // ── In-process witness (b): outbound seqnum advanced past Logon ──────────
    // The acceptor's reply Logon was sent (at least one outbound frame emitted).
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the reply Logon";

    // ── In-process witness (c): inbound seqnum == 2 (no ResendRequest issued) ─
    // The peer sent its Logon at 34=1; after accepting it, fixpp's next expected
    // inbound is 2. If fixpp had issued a ResendRequest, the dialogue would
    // differ (more frames, different seqnum). This witness proves no ResendRequest
    // was issued for seqnums below the reset point (C6.2 / C2.5).
    EXPECT_EQ(s->seqnum_mgr_test_access().next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "next_inbound should be 2 after accepting peer Logon at 34=1 "
        << "(a ResendRequest would advance it differently — C2.5/C6.2 check)";

    // The golden assertion (141=Y echo + 34=1 peer Logon, admin profile {52,10})
    // is performed by the parent gate against the proxy capture. Golden file:
    //   happy/golden/RL-<cp>-acc-fix44-reset-on-logon.fix
    const std::string cp_part = (counterparty == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    const std::string cell_id = "RL-" + cp_part + "-acc-fix44-reset-on-logon";
    hp::diff_golden_or_skip(cell_id, hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ────────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, ResetOnLogonAcceptor,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    [](const ::testing::TestParamInfo<Counterparty>& info) {
        return (info.param == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    });

}  // namespace
