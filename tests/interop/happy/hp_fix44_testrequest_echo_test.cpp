// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_testrequest_echo_test.cpp — 016 T011 [US1] / 018 T007-T010.
//
// Happy-path interop cell:
//   Logon -> idle Heartbeat -> TestRequest echo, over TLS, FIX 4.4.
//
// 016 role: the original US1 live-cell driver (skip:counterparty-unavailable,
//   in-process seqnum/FSM witness). The parent gate golden-diffs the proxy
//   capture against HP-*-testrequest-echo.fix.
//
// 018 T007 extension: bidirectional admin round-trip — both fixpp-originated
//   TestRequest (via inbound silence, 10 s self-deadline per FR-010) and
//   QFJ-originated TestRequest → fixpp Heartbeat. In-process witnesses:
//   FSM stays Active, outbound seqnum advances past Logon.  Golden-based echo
//   correlation (FR-001): both directions asserted from the golden file under
//   the admin normalization profile {52,10} (FR-007 / admin_profile_excluded_tags()).
//
// 018 T009: golden assertion for HP-QFj-{init,acc}-fix44-testrequest-echo.fix.
//   When the golden file is absent (no first paired run yet) the test emits
//   skip:golden-not-yet-captured rather than failing (never hand-fabricate).
//
// 018 T010: SC-004 gate-bite negative test — mutate tag 112 in a synthetic
//   golden pair and assert diff_transcripts() reports a mismatch on 112.
//   Self-contained; runs without a live counterparty. [feedback_fail_placeholder_red_test]
//
// AC coverage (T007/T009): {US1-1, US1-2, US1-3} for each role.
// spec_ref [FIX-SL §4.5.1 Heartbeat / §4.5.5 TestRequest].
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <tuple>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"
#include "support/scenario_descriptor.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

// ---------------------------------------------------------------------------
// SC-004 gate-bite negative test (T010) — self-contained, no live QFJ needed
// ---------------------------------------------------------------------------
//
// Contract: mutate tag 112 (a *compared* tag under the {52,10} admin profile)
// and assert diff_transcripts() bites.  Never mutate 52 or 10 — those are
// canonicalized away and would yield a false non-biting "pass" (SC-004 rule).
// [feedback_fail_placeholder_red_test]: real DiffResult assertion, not SUCCEED().

TEST(TestRequestEchoGateBite, MutatedTag112CausesGateBite)
{
    // Minimal synthetic FIX 4.4 TestRequest (35=1) with 112=TESTREQ_ID_A
    // SOH rendered as \x01 (the checked-in golden escape, decoded by parse_golden).
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=1\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x01112=TESTREQ_ID_A\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    // Actual: same frame but 112 mutated to TESTREQ_ID_B.
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=1\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x01112=TESTREQ_ID_B\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

    // Under admin_profile_excluded_tags() == {52, 10}:
    //   - tag 52 (SendingTime) is excluded → equal regardless of value.
    //   - tag 10 (CheckSum)    is excluded → equal regardless of value.
    //   - tag 112 (TestReqID) is INCLUDED → must match verbatim → MISMATCH.
    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "112");
}

// ---------------------------------------------------------------------------
// Shared CanonicalizedTags5210DoNotBite — one test, not one per cell.
// ---------------------------------------------------------------------------
//
// Contract: mutating ONLY tags 52 (SendingTime) and 10 (CheckSum) — the two
// tags excluded by admin_profile_excluded_tags() == {52, 10} — MUST NOT cause
// a diff_transcripts() mismatch. This tests a property of the profile itself,
// not any specific cell. All five per-cell CanonicalizedTag52DoesNotBite tests
// previously duplicated this assertion; they are collapsed here (SC-004 rule).
// [feedback_fail_placeholder_red_test]: real DiffResult assertion, not SUCCEED().

TEST(AdminProfileGateBite, CanonicalizedTags5210DoNotBite)
{
    // Synthetic FIX 4.4 TestRequest (35=1): payload tag 112 is identical in both
    // frames; only SendingTime(52) and CheckSum(10) differ between expected and actual.
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=1\\x01112=TESTREQ_ID_X\\x0152=20260603-10:00:00\\x0110=001\\x01\n";
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=1\\x01112=TESTREQ_ID_X\\x0152=20260604-11:11:11\\x0110=999\\x01\n";

    auto expected_frames = fixpp::interop::parse_golden(expected_text);
    auto actual_frames   = fixpp::interop::parse_golden(actual_text);

    const fixpp::interop::DiffResult result = fixpp::interop::diff_transcripts(
        expected_frames, actual_frames, fixpp::interop::admin_profile_excluded_tags());

    EXPECT_TRUE(static_cast<bool>(result))
        << "Canonicalized tags 52/10 should not cause a mismatch; detail=" << result.detail;
}

// ---------------------------------------------------------------------------
// T007 / T009: bidirectional TestRequest→Heartbeat live cell
// ---------------------------------------------------------------------------
//
// Each (counterparty, role) cell:
//  1. Skip if counterparty absent  (FR-009, skip:counterparty-unavailable).
//  2. Drive to Active, assert FSM=Active + seqnum > 1  (US1-1 in-process).
//  3. Hold session live for up to 7 s (inbound-silence window, US1-1+US1-2).
//     — fixpp's liveness loop emits TestRequest after heartbeat_interval with
//       no inbound frame; the heartbeat_interval for this cell is set to 3 s
//       so the TestRequest fires within the 10 s self-deadline.
//     — The QFJ-originated TestRequest arrives during this window (parent
//       harness drives it) — correlated by the 112 observed in the golden.
//  4. Assert FSM still Active after the window (US1-1, US1-2: session survives).
//  5. Load the golden for this cell; skip if absent (no first paired run yet).
//     If present, assert diff_transcripts(expected, actual, {52,10}) MATCHES,
//     confirming the echoed 112 in both directions (US1-3).
//  6. Graceful stop within the remaining watchdog bound.
//
// AdminScenarioDescriptor (rule 7+8): declared inline and validated at entry.

class HappyTestRequestEcho
    : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(HappyTestRequestEcho, BidirectionalTestRequestEcho) {
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // ── AdminScenarioDescriptor validation (rule 7 + rule 8) ────────────────
    const std::string cp_part = (counterparty == Counterparty::quickfix_j) ? "QFj" : "QFcpp";
    const std::string role_part = (role == Role::fixpp_initiator) ? "init" : "acc";
    const std::string cell_id = "HP-" + cp_part + "-" + role_part + "-fix44-testrequest-echo";

    fixpp::interop::AdminScenarioDescriptor desc;
    desc.cell_id = cell_id;
    desc.scenario_group = fixpp::interop::AdminScenarioGroup::testrequest_echo;
    desc.role = role;
    desc.counterparty = counterparty;
    desc.spec_ref = "[FIX-SL §4.5.1/§4.5.5]";
    desc.golden_ref = "happy/golden/" + cell_id + ".fix";
    desc.induction = fixpp::interop::AdminInduction::inbound_silence;
    desc.self_deadline_ms = std::chrono::milliseconds{10000};  // FR-010: 10 s
    desc.round_trips = {
        {"US1-1", "[FIX-SL §4.5.5]"},  // fixpp TestRequest → peer Heartbeat
        {"US1-2", "[FIX-SL §4.5.1]"},  // peer TestRequest → fixpp Heartbeat
        {"US1-3", "[FIX-SL §4.5.5]"},  // golden 112 echo correlation
    };
    desc.acceptance_ids = {"US1-1", "US1-2", "US1-3"};

    const std::string desc_err = fixpp::interop::validate_admin_descriptor(desc);
    ASSERT_TRUE(desc_err.empty()) << "AdminScenarioDescriptor invalid: " << desc_err;

    // ── Counterparty-required: skip if absent (FR-009) ────────────────────
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    // testrequest_echo admin round-trip is QFj-only at G1: the inbound_silence
    // induction with 3s heartbeat_interval is only configured in the QFJ parent
    // harness (T008 [PARENT]). Skip for non-QFj counterparties.
    if (counterparty != Counterparty::quickfix_j) {
        GTEST_SKIP() << "skip:not-applicable (testrequest_echo admin round-trip is QFj-only at G1; "
                        "not configured for " << hp::counterparty_token(counterparty) << ")";
    }

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    const auto endpoint = hp::cell_endpoint(counterparty, role);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    fixpp::interop::InteropEngineFixture fx;
    // Use a 3 s heartbeat_interval so the liveness loop emits TestRequest
    // within the 10 s self-deadline when held inbound-silent (US1-1 induction).
    auto cfg = hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(),
                                       *endpoint);
    cfg.heartbeat_interval = std::chrono::seconds{3};  // inbound-silence induction (US1-1)
    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── Self-deadline envelope (FR-010): 10 s total ────────────────────────
    // Drive to Active within 5 s (same as other cells).
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon) against "
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    fixpp::session::Session* s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    // ── In-process seqnum witness (US1-1 / New-4) ─────────────────────────
    // Outbound seqnum > 1 confirms at least the Logon was sent.
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // ── Inbound-silence window: hold session live for up to 4 s ──────────
    // The liveness loop will emit TestRequest after ~3 s of silence (US1-1).
    // The QFJ-originated TestRequest (US1-2) also arrives in this window.
    // We pump until seqnum advances beyond 2 (Logon + at least one admin msg)
    // OR the window expires — whichever comes first.  With a live peer this
    // observes the TestRequest emission; without one the window expires and
    // the cell skips on the golden assertion below.
    const auto seqnum_after_logon = s->seqnum_mgr_test_access().peek_outbound();
    fx.run_until(
        [&] {
            fixpp::session::Session* ss = fx.engine().lookup(id);
            return ss != nullptr &&
                   ss->seqnum_mgr_test_access().peek_outbound() > seqnum_after_logon;
        },
        4s);

    // ── FSM still Active (US1-1, US1-2: session survives the round-trip) ──
    {
        fixpp::session::Session* ss = fx.engine().lookup(id);
        ASSERT_NE(ss, nullptr) << "session vanished mid-cell";
        EXPECT_EQ(ss->state(), fsm_state::Active)
            << "FSM left Active during the TestRequest round-trip window";
    }

    // ── Golden assertion (T009 / US1-3) ───────────────────────────────────
    // The golden file is captured at first paired run by the parent harness.
    // If absent: skip with reason (never FAIL, never hand-fabricate).
    // If present: assert diff_transcripts(expected, actual, {52,10}) MATCHES
    // so that tag 112 is verified verbatim (FR-007).
    //
    // NOTE: in the local test environment (no live QFJ) the cell has already
    // been skipped above via INTEROP_REQUIRE_COUNTERPARTY, so this block is
    // only reached when QFJ is present.  The golden file is expected to have
    // been committed after the first paired run.  If QFJ is up but the golden
    // was not yet captured and committed, skip with reason.
    hp::diff_golden_or_skip(cell_id, hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ─────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappyTestRequestEcho,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
