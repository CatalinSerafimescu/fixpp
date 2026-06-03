// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_recovery_outbound_answer_test.cpp
//   — 018 T012+T014+T015 [US3].
//
// Happy-path interop cell:
//   recovery_outbound — fixpp answers a QuickFIX-J ResendRequest with replay
//   (PossDup 43=Y, OrigSendingTime 122=) and/or SequenceReset-GapFill (35=4,
//   123=Y), over TLS, FIX 4.4.
//
// Cell id (per T029 naming decision):
//   HP-QFj-{init,acc}-fix44-recovery-outbound
//
// Induction mechanism (admin-scenario-descriptor.md rule 8):
//   qfj_restart_resend — QFJ restarts / reconnects expecting a lower sequence
//   number, causing it to issue a ResendRequest(35=2) against fixpp.
//   fixpp answers via 013's build_sequence_reset_gapfill outbound path (replay
//   or SequenceReset-GapFill).  The session remains Active throughout.
//
// In-process witnesses (contracts/admin-scenario-descriptor.md rule 3):
//   (a) FSM stays Active (fixpp processes the ResendRequest on the live session;
//       no Disconnect is expected for a spec-legal ResendRequest; Active is the
//       expected terminal for US3-4).
//   (b) Outbound seqnum advances beyond Logon (the ResendRequest reply — replay
//       or GapFill — advances the outbound counter).
//   (c) Session returns to Active (not Disconnected) after the outbound-answer window.
//   Wire-frame assertions (golden-based only per R1 architecture):
//     QFJ's ResendRequest(35=2) + fixpp's answering replay(43=Y,122=) and/or
//     SequenceReset-GapFill(35=4,123=Y) are asserted via the golden under {52,10}.
//
// 018 T014 (golden assertion):
//   When the golden is absent → skip:golden-not-yet-captured (never fail).
//   Golden captured at first paired run by the parent harness.
//
// 018 T015 (SC-004 gate-bite negative tests):
//   Mutate tag 16 (EndSeqNo, QFJ's ResendRequest range) and tag 123 (GapFillFlag,
//   in fixpp's SequenceReset-GapFill reply) — compared tags under {52,10} — and
//   assert diff_transcripts() bites with DiffStatus::mismatch.
//   [feedback_fail_placeholder_red_test]: real DiffResult assertion, not SUCCEED().
//
// spec_ref [FIX-SL §4.8.2 / §4.8.5 / §4.8.6].
// AC coverage: {US3-3, US3-4} for each role.
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"
#include "support/golden_diff.hpp"
#include "support/scenario_descriptor.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::DiffResult;
using fixpp::interop::DiffStatus;
using fixpp::interop::Role;
using fixpp::interop::admin_profile_excluded_tags;
using fixpp::interop::diff_transcripts;
using fixpp::interop::parse_golden;
using fixpp::session::fsm_state;

namespace {

// ---------------------------------------------------------------------------
// SC-004 gate-bite negative tests (T015) — self-contained, no live QFJ needed.
// ---------------------------------------------------------------------------
//
// Contract: mutate a COMPARED tag (16 or 123) and assert diff_transcripts()
// bites. NEVER mutate 52 or 10 — those are canonicalized by {52,10} and would
// yield a false non-biting "pass" (SC-004 rule).
// [feedback_fail_placeholder_red_test]: real DiffResult assertion, not SUCCEED().

TEST(RecoveryOutboundGateBite, MutatedTag16EndSeqNoCausesGateBite)
{
    // Synthetic QFJ ResendRequest (35=2) with EndSeqNo(16) — a COMPARED tag
    // under the {52,10} admin profile.  A mutation must make the gate bite.
    const char* expected_text =
        "< 8=FIX.4.4\\x0135=2\\x0149=CPTY_ACC\\x0156=FIXPP_INIT"
        "\\x017=2\\x0116=5\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    // Actual: EndSeqNo changed from 5 to 7 (different requested range).
    const char* actual_text =
        "< 8=FIX.4.4\\x0135=2\\x0149=CPTY_ACC\\x0156=FIXPP_INIT"
        "\\x017=2\\x0116=7\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

    auto expected_frames = parse_golden(expected_text);
    auto actual_frames   = parse_golden(actual_text);

    ASSERT_EQ(expected_frames.size(), 1u);
    ASSERT_EQ(actual_frames.size(), 1u);

    // Under admin_profile_excluded_tags() == {52, 10}:
    //   - tag 52 (SendingTime) is excluded → equal regardless of value.
    //   - tag 10 (CheckSum)    is excluded → equal regardless of value.
    //   - tag 16 (EndSeqNo)   is INCLUDED → must match verbatim → MISMATCH.
    const DiffResult result = diff_transcripts(expected_frames, actual_frames,
                                               admin_profile_excluded_tags());

    EXPECT_FALSE(static_cast<bool>(result))
        << "gate-bite FAILED: diff_transcripts() reported match when EndSeqNo(16) differs; "
        << "detail=" << result.detail;
    EXPECT_EQ(result.status, DiffStatus::mismatch)
        << "Expected DiffStatus::mismatch when tag 16 (EndSeqNo) is mutated";
    EXPECT_NE(result.detail.find("16"), std::string::npos)
        << "detail should mention tag 16 as the differing field; got: " << result.detail;
}

TEST(RecoveryOutboundGateBite, MutatedTag123GapFillFlagInAnswerCausesGateBite)
{
    // Synthetic fixpp SequenceReset-GapFill (35=4) reply with GapFillFlag(123=Y).
    // Tag 123 is a COMPARED tag under {52,10} — mutation must make the gate bite.
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=4\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x01123=Y\\x0136=6\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    // Actual: GapFillFlag mutated from Y to N.
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=4\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x01123=N\\x0136=6\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

    auto expected_frames = parse_golden(expected_text);
    auto actual_frames   = parse_golden(actual_text);

    ASSERT_EQ(expected_frames.size(), 1u);
    ASSERT_EQ(actual_frames.size(), 1u);

    // Tag 123 (GapFillFlag) is INCLUDED under {52,10} → must match verbatim → MISMATCH.
    const DiffResult result = diff_transcripts(expected_frames, actual_frames,
                                               admin_profile_excluded_tags());

    EXPECT_FALSE(static_cast<bool>(result))
        << "gate-bite FAILED: diff_transcripts() reported match when GapFillFlag(123) differs; "
        << "detail=" << result.detail;
    EXPECT_EQ(result.status, DiffStatus::mismatch)
        << "Expected DiffStatus::mismatch when tag 123 (GapFillFlag) is mutated";
    EXPECT_NE(result.detail.find("123"), std::string::npos)
        << "detail should mention tag 123 as the differing field; got: " << result.detail;
}

TEST(RecoveryOutboundGateBite, CanonicalizedTag52DoesNotBite)
{
    // Sanity: mutating ONLY tag 52 (canonicalized by {52,10}) MUST NOT bite.
    // Confirms the SC-004 "never mutate 52/10" invariant in the outbound-answer context.
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=4\\x01123=Y\\x0136=6\\x0152=20260603-10:00:00\\x0110=001\\x01\n";
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=4\\x01123=Y\\x0136=6\\x0152=20260604-11:11:11\\x0110=999\\x01\n";

    auto expected_frames = parse_golden(expected_text);
    auto actual_frames   = parse_golden(actual_text);

    const DiffResult result = diff_transcripts(expected_frames, actual_frames,
                                               admin_profile_excluded_tags());

    EXPECT_TRUE(static_cast<bool>(result))
        << "Canonicalized tags 52/10 should not cause a mismatch; detail=" << result.detail;
}

// ---------------------------------------------------------------------------
// Helper: resolve the golden path for recovery_outbound cells (T014 / T029).
// Cell ids (NEW per T029, QFj-only at G1):
//   HP-QFj-{init,acc}-fix44-recovery-outbound
// ---------------------------------------------------------------------------
std::string recovery_outbound_golden_path(Counterparty cp, Role role)
{
    const char* tls_dir = fixpp::interop::hp::tls_fixture_dir();
    if (tls_dir == nullptr || tls_dir[0] == '\0') {
        return {};
    }
    std::string base{tls_dir};
    const std::string suffix = "/tls/fixtures";
    if (base.size() > suffix.size() &&
        base.substr(base.size() - suffix.size()) == suffix) {
        base.resize(base.size() - suffix.size());
    }
    // New cell id (recovery_outbound is a distinct scenario from seqnum-recovery).
    const std::string cp_part   = (cp == Counterparty::quickfix_j) ? "QFj" : "QFcpp";
    const std::string role_part = (role == Role::fixpp_initiator)  ? "init" : "acc";
    return base + "/interop/happy/golden/HP-" + cp_part + "-" + role_part +
           "-fix44-recovery-outbound.fix";
}

// ---------------------------------------------------------------------------
// T012 / T014: recovery_outbound live cell (both roles).
// ---------------------------------------------------------------------------
//
// AdminScenarioDescriptor (rule 7 + rule 8):
//   scenario_group = recovery_outbound
//   induction      = qfj_restart_resend
//   acceptance_ids = {US3-3, US3-4}
//
// Induction: QFJ restarts/reconnects at a lower sequence number, causing QFJ to
// emit ResendRequest(35=2) against fixpp. fixpp answers via the outbound replay
// path (013 build_sequence_reset_gapfill): replay(43=Y,122=) and/or
// SequenceReset-GapFill(35=4,123=Y). After the dialogue QFJ resynchronises
// and the session returns to Active.
//
// In-process assertions are fixpp-state-only (rule 3, R1 architecture).
// Wire-frame assertions are golden-based.
// Self-deadline: 30 s (FR-010 recovery).

class HappyRecoveryOutboundAnswer
    : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(HappyRecoveryOutboundAnswer, FixppAnswersResendRequestAndPeerResyncs) {
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // ── AdminScenarioDescriptor validation (rule 7 + rule 8) ────────────────
    const std::string cp_part   = (counterparty == Counterparty::quickfix_j) ? "QFj" : "QFcpp";
    const std::string role_part = (role == Role::fixpp_initiator) ? "init" : "acc";
    const std::string cell_id   = "HP-" + cp_part + "-" + role_part + "-fix44-recovery-outbound";

    fixpp::interop::AdminScenarioDescriptor desc;
    desc.cell_id       = cell_id;
    desc.scenario_group = fixpp::interop::AdminScenarioGroup::recovery_outbound;
    desc.role          = role;
    desc.counterparty  = counterparty;
    desc.spec_ref      = "[FIX-SL §4.8.2/§4.8.5/§4.8.6]";
    desc.golden_ref    = "happy/golden/" + cell_id + ".fix";
    desc.induction     = fixpp::interop::AdminInduction::qfj_restart_resend;
    desc.self_deadline_ms = std::chrono::milliseconds{30000};  // FR-010: 30 s
    desc.round_trips   = {
        {"US3-3", "[FIX-SL §4.8.2]"},  // QFJ issues ResendRequest; fixpp answers correctly
        {"US3-4", "[FIX-SL §4.8.6]"},  // both peers at Active, QFJ resynced, no data loss
    };
    desc.acceptance_ids = {"US3-3", "US3-4"};

    const std::string desc_err = fixpp::interop::validate_admin_descriptor(desc);
    ASSERT_TRUE(desc_err.empty()) << "AdminScenarioDescriptor invalid: " << desc_err;

    // ── Counterparty-required: skip if absent (FR-009) ─────────────────────
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    // G1 recovery_outbound is QFj-only (the qfj_restart_resend induction mechanism
    // is specific to QFJ's restart/reconnect protocol; QFcpp has a different resend
    // choreography). Skip for non-QFj counterparties.
    if (counterparty != Counterparty::quickfix_j) {
        GTEST_SKIP() << "skip:not-applicable (recovery_outbound qfj_restart_resend induction "
                        "is QFj-specific; not exercised for "
                     << hp::counterparty_token(counterparty) << ")";
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
    auto cfg = hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(),
                                       *endpoint);
    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── Drive to Active (logon) — 5 s budget ─────────────────────────────
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon) against "
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    fixpp::session::Session* s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    // ── In-process witness (b): outbound seqnum after Logon ────────────────
    const auto seqnum_after_logon = s->seqnum_mgr_test_access().peek_outbound();
    EXPECT_GT(seqnum_after_logon, fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // ── Outbound-answer window: 25 s budget ────────────────────────────────
    // The parent drives QFJ to reconnect at a lower sequence number (qfj_restart_resend
    // induction). QFJ issues ResendRequest(35=2) to fixpp. fixpp answers using the
    // 013 outbound replay path (replay or SequenceReset-GapFill). After the dialogue
    // the session returns to Active.
    //
    // We pump until the outbound seqnum advances beyond the post-logon value (the
    // GapFill/replay frames advance it) OR the window expires.
    fx.run_until(
        [&] {
            fixpp::session::Session* ss = fx.engine().lookup(id);
            return ss != nullptr &&
                   ss->seqnum_mgr_test_access().peek_outbound() > seqnum_after_logon;
        },
        25s);

    s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session disappeared during recovery_outbound window";

    // ── In-process witness (a): FSM stays Active (US3-4 — fixpp side) ─────
    // fixpp must not disconnect on receipt of a spec-legal ResendRequest from QFJ.
    // After answering, the session returns to Active.
    EXPECT_EQ(s->state(), fsm_state::Active)
        << "FSM left Active during/after the recovery_outbound window (US3-4 violated); "
        << "fixpp disconnected on QFJ ResendRequest — check the outbound answer path";

    // ── In-process witness (b): outbound seqnum advanced ──────────────────
    // The reply (replay or SequenceReset-GapFill) is an outbound emission; the
    // outbound counter must have advanced beyond the post-logon value.
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), seqnum_after_logon)
        << "outbound seqnum did not advance past logon; fixpp may not have replied to QFJ's "
        << "ResendRequest (US3-3 outbound-answer path not triggered)";

    // ── Golden assertion (T014 / US3-3) ───────────────────────────────────
    // The golden file is captured at first paired run by the parent harness.
    // If absent → skip:golden-not-yet-captured (never fail, never hand-fabricate).
    // If present → assert diff_transcripts(expected, actual, {52,10}) MATCHES so
    // that QFJ's ResendRequest(7/16) and fixpp's answer (123/122/43) are verified
    // verbatim under the admin profile (FR-007).
    const std::string gpath = recovery_outbound_golden_path(counterparty, role);
    if (gpath.empty()) {
        GTEST_SKIP() << "skip:golden-not-yet-captured (FIXPP_TLS_FIXTURE_DIR unresolvable)";
    }
    {
        std::ifstream gfile{gpath};
        if (!gfile.is_open()) {
            GTEST_SKIP() << "skip:golden-not-yet-captured (file absent: " << gpath << ")";
        }
        std::ostringstream oss;
        oss << gfile.rdbuf();
        const std::string golden_text = oss.str();
        if (golden_text.empty()) {
            GTEST_SKIP() << "skip:golden-not-yet-captured (file empty: " << gpath << ")";
        }

        // The capture sidecar is written by the parent proxy alongside the golden.
        const std::string capture_path = gpath.substr(0, gpath.size() - 4) + "-capture.fix";
        std::ifstream cfile{capture_path};
        if (!cfile.is_open()) {
            GTEST_SKIP() << "skip:golden-not-yet-captured (capture sidecar absent: "
                         << capture_path << ")";
        }
        std::ostringstream css;
        css << cfile.rdbuf();
        const std::string capture_text = css.str();
        if (capture_text.empty()) {
            GTEST_SKIP() << "skip:golden-not-yet-captured (capture sidecar empty)";
        }

        const auto expected_frames = parse_golden(golden_text);
        const auto actual_frames   = parse_golden(capture_text);

        // admin_profile_excluded_tags() = {52, 10} — NOT the 016 default which
        // would drop tags 16/122/123, making the outbound-answer assertions un-assertable.
        const DiffResult diff = diff_transcripts(expected_frames, actual_frames,
                                                 admin_profile_excluded_tags());
        EXPECT_TRUE(static_cast<bool>(diff))
            << "Golden transcript mismatch for " << cell_id
            << " (admin profile {52,10}): " << diff.detail
            << "\n  expected golden: " << gpath
            << "\n  actual capture:  " << capture_path;
    }

    // ── Graceful stop (Logout) ─────────────────────────────────────────────
    const auto stop_elapsed = fx.stop_within(3s);
    EXPECT_LT(stop_elapsed, 3s)
        << "Engine::stop() (graceful Logout) exceeded the watchdog: "
        << stop_elapsed.count() << " ms";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappyRecoveryOutboundAnswer,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
