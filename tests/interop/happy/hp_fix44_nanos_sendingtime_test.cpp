// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_nanos_sendingtime_test.cpp — 026 T018+T019 [P].
//
// Live nanosecond-SendingTime interop cells — both roles (C7.1/C7.2/SC-001/SC-003/SC-004).
//
// T018 — NanosSendingTimeInitiator (C7.1 / SC-001):
//   fixpp INITIATOR with sending_time_precision = nanos against a live QFcpp/QFJ ACCEPTOR.
//   In-process witnesses (what this cell can assert in-process):
//     (a) fixpp FSM reaches Active — the live counterparty accepted the Logon with a
//         27-char nanos 52= (a counterparty that Rejected the timestamp would Logout /
//         disconnect instead of reaching Active).
//     (b) Outbound seqnum >= 2 after Active — the Logon was sent (seqnum 1 consumed),
//         proving fixpp was active and not immediately disconnected.
//   NOTE: in-process byte capture of the outbound Logon frame is not reachable in this
//   interop fixture (no in-library wire-capture seam on the live outbound path). The
//   byte-level assertion that the Logon carried a 27-char 52= is provided by two sources:
//     - Unit witness SendingTimePrecision_Nanos_Emits27Char52
//       (tests/session/test_sending_time_precision.cpp): directly asserts 52= length == 27
//       on captured Logon frame bytes.
//     - Parent golden (diff_golden_or_skip below): asserts 52= length == 27 verbatim
//       under the {52,10} admin profile against the live proxy capture (first paired run).
//
// T019 — NanosSendingTimeAcceptor (C7.2 / SC-003 / SC-004):
//   A live QFcpp/QFJ INITIATOR configured for NANOS sends a 27-char 52= Logon. The fixpp
//   ACCEPTOR parses it (lenient parser accepts 1–9-digit fractions), processes it,
//   and reaches Active — no malformed-field Reject.
//   In-process witnesses:
//     (a) fixpp FSM reaches Active — no Reject/Logout from a 27-char nanos 52=.
//         This proves the lenient parser accepted the nanos timestamp (SC-003).
//     (b) Outbound seqnum >= 2 after Active — acceptor's reply Logon was sent.
//     (c) MaxLatency is computed correctly: a correctly-parsed nanos instant is
//         within 120 s of the real clock so no stale-timestamp Reject fires (SC-004).
//         Assured by the same check_sending_time arithmetic (FR-007/New-4: no new
//         boundary logic; the existing path operates on the parsed utc_time_point).
//   NOTE: the parent harness MUST configure its counterparty initiator with nanos
//   precision (or equivalent) so it sends a 27-char 52=. Both QFcpp and QFJ support
//   this — the parent harness config is described in the MATRIX.md and the
//   026 specs/quickstart.md.
//
// Both roles are value-parameterized over counterparty ∈ {quickfix-cpp, quickfix-j}
// using a SINGLE file (one-file-both-roles pattern per 024 precedent:
// hp_fix44_reset_on_logon_test.cpp).
//
// LIVE CELLS: require a counterparty. INTEROP_REQUIRE_COUNTERPARTY skips with reason
// when the counterparty port env is absent (FR-023). Never a silent pass.
//
// Golden artifacts (parent harness captures at first paired live run):
//   happy/golden/NST-QFcpp-init-fix44-nanos-sendingtime.fix
//   happy/golden/NST-QFcpp-acc-fix44-nanos-sendingtime.fix
//   happy/golden/NST-QFj-init-fix44-nanos-sendingtime.fix
//   happy/golden/NST-QFj-acc-fix44-nanos-sendingtime.fix
// The parent diff asserts 52= length == 27 verbatim under the {52,10} admin profile.
// Absent → skip:golden-not-yet-captured (diff_golden_or_skip convention).
//
// Parent harness MUST:
//   For fixpp-initiator cells (T018): set INTEROP_<TOKEN>_PORT + optionally
//     INTEROP_<TOKEN>_HOST pointing at the counterparty's TLS acceptor.
//     The counterparty must tolerate a 27-char nanos 52= without Rejecting it.
//   For fixpp-acceptor cells (T019): set INTEROP_FIXPP_PORT (or let OS-assign);
//     the counterparty initiator must be configured for nanos precision so it
//     sends a 27-char 52= on each message. Both QFcpp and QFJ support
//     DataDictionary sub-second precision (or equivalent config) for this.
//
// Anchors: tasks.md T018/T019; contracts/sendingtime-precision.md C7 (C7.1/C7.2);
//          quickstart.md "Live interop"; 026 FR-007/SC-001/SC-003/SC-004/I-NST-5.
//
// spec_ref [FIX-SL §4.2.3] Validation of SendingTime(52); UTCTimestamp data type.
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <tuple>

#include <fixpp/core/fix_time.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

// ── T018 — NanosSendingTimeInitiator (C7.1 / SC-001) ─────────────────────────
//
// fixpp INITIATOR with sending_time_precision = nanos. Both counterparties ×
// initiator role. (Acceptor role is covered by T019 / NanosSendingTimeAcceptor.)
//
// make_session_config builds on the hp:: support helper, setting
// sending_time_precision = nanos after construction. Minimal and backward-compatible.

class NanosSendingTimeInitiator : public ::testing::TestWithParam<Counterparty> {};

TEST_P(NanosSendingTimeInitiator, LogonAcceptedWithNanos52) {
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023 / C7.1).
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
    // ── 026 T018: configure nanos emit precision (C7.1 / SC-001) ─────────────
    cfg.sending_time_precision = fixpp::core::fix_time_precision::nanos;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── In-process witness (a): FSM reaches Active (C7.1 / SC-001) ───────────
    // A counterparty that Rejects the nanos 52= timestamp would Logout /
    // disconnect instead of reaching Active — so Active proves the counterparty
    // accepted the 27-char nanos SendingTime without sending a Reject(35=3).
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon with nanos sending_time_precision) "
           "against " << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached)
        << "; [C7.1 / SC-001 / contract sendingtime-precision.md]";

    // ── In-process witness (b): outbound seqnum >= 2 after Active ─────────────
    // The Logon was sent (consuming seqnum 1); peek_outbound() >= 2 proves
    // seqnum advancement past 1 — the session is genuinely active, not merely
    // registered. Does NOT prove 52= was 27 chars (that is the unit-witness
    // SendingTimePrecision_Nanos_Emits27Char52 + the parent golden's domain).
    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not found after drive_to_active";
    EXPECT_GE(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{2})
        << "outbound seqnum should be >= 2 after Active (Logon at 34=1 was sent "
           "with nanos 52=; next outbound advanced past 1)";

    // The golden assertion (52= length == 27 verbatim, admin profile {52,10}) is
    // performed by the parent gate against the proxy capture. Golden file:
    //   happy/golden/NST-<cp>-init-fix44-nanos-sendingtime.fix
    const std::string cp_part = (counterparty == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    const std::string cell_id = "NST-" + cp_part + "-init-fix44-nanos-sendingtime";
    hp::diff_golden_or_skip(cell_id, hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ────────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, NanosSendingTimeInitiator,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    [](const ::testing::TestParamInfo<Counterparty>& info) {
        return (info.param == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    });

// ── T019 — NanosSendingTimeAcceptor (C7.2 / SC-003 / SC-004) ─────────────────
//
// fixpp ACCEPTOR receiving a 27-char nanos 52= from a live QFcpp/QFJ INITIATOR.
// The lenient parser (T014) accepts any 1–9-digit fraction. Both counterparties ×
// acceptor role.
//
// The parent harness MUST configure its counterparty initiator with nanos precision
// so it sends a 27-char 52= on each message (both QFcpp and QFJ support this).

class NanosSendingTimeAcceptor : public ::testing::TestWithParam<Counterparty> {};

TEST_P(NanosSendingTimeAcceptor, AcceptsNanos52WithoutReject) {
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023 / C7.2).
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
    // ── 026 T019: fixpp emit precision = millis (default) ────────────────────
    // The test validates that fixpp ACCEPTS a peer's nanos 52= (C7.2). fixpp's
    // own emit precision is irrelevant to this cell — it remains millis (default)
    // to ensure the acceptance is from the lenient parser (T014), not from
    // matching the local emit precision. The peer is the one sending nanos.

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // For a fixpp-acceptor cell, the parent's counterparty-initiator connects to
    // fixpp's bound port and sends a Logon with a 27-char nanos 52=. When the
    // port is OS-assigned (INTEROP_FIXPP_PORT unset), the parent must read it via
    // Engine::acceptor_bound_endpoint() and relay it to its initiator.

    // ── In-process witness (a): FSM reaches Active — no Reject on nanos 52= ───
    // With the lenient parser (T014), a 27-char nanos 52= from the peer is
    // accepted (check_sending_time passes on the parsed utc_time_point). If the
    // parser were strict (pre-T014), it would reject length 27 →
    // Reject(35=3 RefTagID=52) → Logout → Disconnect → NOT Active.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "acceptor did not reach Active on peer nanos 52= Logon (C7.2 violated) "
           "— check lenient parser accepts 27-char timestamp; counterparty="
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached)
        << "; [SC-003 / C7.2 / I-NST-3 / FR-007/SC-004]";

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not found after drive_to_active";

    // ── In-process witness (b): outbound seqnum >= 2 (reply Logon was sent) ───
    EXPECT_GE(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{2})
        << "outbound seqnum did not advance past the acceptor reply Logon";

    // ── In-process witness (c): inbound seqnum == 2 (no ResendRequest issued) ─
    // The peer sent its Logon at 34=1; after accepting it, fixpp's next expected
    // inbound is 2. This proves no ResendRequest was issued for a spurious too-high
    // or parse-error mis-routing, and that MaxLatency was satisfied — i.e. the
    // parsed nanos instant was within the 120 s threshold (FR-007/SC-004/New-4).
    // check_sending_time operates on the parsed utc_time_point, so it handles
    // nanos correctly without any new boundary logic (FR-007 unchanged).
    EXPECT_EQ(s->seqnum_mgr_test_access().next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "next_inbound should be 2 after accepting peer Logon at 34=1 with nanos 52= "
           "(no ResendRequest was issued; MaxLatency correctly computed; "
           "C7.2/SC-004/FR-007/New-4 check)";

    // The golden assertion (peer's 27-char 52= accepted, admin profile {52,10})
    // is performed by the parent gate against the proxy capture. Golden file:
    //   happy/golden/NST-<cp>-acc-fix44-nanos-sendingtime.fix
    const std::string cp_part = (counterparty == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    const std::string cell_id = "NST-" + cp_part + "-acc-fix44-nanos-sendingtime";
    hp::diff_golden_or_skip(cell_id, hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ────────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, NanosSendingTimeAcceptor,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    [](const ::testing::TestParamInfo<Counterparty>& info) {
        return (info.param == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    });

}  // namespace
