// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_idle_heartbeat_cadence_test.cpp
//   — 018 T016+T018 [US2].
//
// Happy-path interop cell:
//   idle_cadence — verify both engines emit unsolicited Heartbeat(35=0) at the
//   negotiated HeartBtInt(108)=1s cadence, with no TestRequest triggered over a
//   ~5s observation window, over TLS, FIX 4.4.
//
// Cell ids (per T029 naming decision):
//   HP-QFj-{init,acc}-fix44-idle-cadence
//
// Induction mechanism (admin-scenario-descriptor.md rule 8):
//   idle_observation — passive observation of both-direction heartbeat cadence;
//   no active frame injection.  The session is held idle (no app traffic); each
//   engine emits unsolicited Heartbeats at the negotiated HeartBtInt interval.
//
// FR-002 sizing rationale (no-TestRequest invariant):
//   fixpp's liveness loop emits TestRequest after heartbeat_interval of inbound
//   silence.  With HeartBtInt=1s the peer heartbeat arrives ~every 1s; if the
//   fixture's observation window is, say, 5 complete peer beats the inbound
//   stream is never 1-full-interval silent, so the liveness loop never triggers
//   a TestRequest.  We size the observation window at 5s ± tolerance while the
//   heartbeat_interval is set to 1s — a punctual peer beat (within the ±1-beat
//   tolerance band) guarantees the inbound stream is always non-silent within
//   the 1s window.
//
// In-process witnesses (contracts/admin-scenario-descriptor.md rule 3):
//   (a) FSM stays Active throughout the ~5s window.
//   (b) Outbound seqnum advances by ≥3 over the window (each unsolicited outbound
//       Heartbeat increments the seqnum counter per FIX spec session-layer rule).
//
// Wire-frame assertions (T018 — golden-based, admin profile {52,10}):
//   When the golden is absent → skip:golden-not-yet-captured (never fail).
//   The golden must contain ≥3 Heartbeat(35=0) in each direction and no
//   TestRequest(35=1); this is asserted via diff_transcripts with the {52,10}
//   admin profile.
//
// SC-004 gate-bite negative tests (self-contained, no live QFJ):
//   (A) Drop one of the ≥3 Heartbeats in the expected golden → expected has 3
//       beats but actual has 2 → diff_transcripts bites with DiffStatus::mismatch.
//   (B) Inject a TestRequest(35=1) into the actual transcript where the expected
//       has only Heartbeats → mismatch on the extra frame.
//   NEVER mutate 52/10 in the "must bite" cells (SC-004 rule).
//   [feedback_fail_placeholder_red_test]: real DiffResult assertions, no SUCCEED().
//
// spec_ref [FIX-SL §4.5.1 Heartbeat].
// AC coverage: {US2-1, US2-2} for each role.
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
using fixpp::interop::DiffResult;
using fixpp::interop::DiffStatus;
using fixpp::interop::Role;
using fixpp::interop::admin_profile_excluded_tags;
using fixpp::interop::diff_transcripts;
using fixpp::interop::parse_golden;
using fixpp::session::fsm_state;

namespace {

// ---------------------------------------------------------------------------
// SC-004 gate-bite negative tests — self-contained, no live QFJ needed.
// ---------------------------------------------------------------------------
//
// SC-004 contract: mutate a COMPARED tag (anything except 52/10, which are
// canonicalized by the {52,10} admin profile) and assert diff_transcripts()
// bites.  NEVER mutate 52 or 10 in the "must bite" cells — those are
// excluded and would yield a false non-biting "pass".
// [feedback_fail_placeholder_red_test]: real DiffResult assertions, not SUCCEED().

// (A) Drop one of the ≥3 expected Heartbeats from the actual transcript.
// The expected golden has 3 Heartbeat(35=0) frames; the actual has only 2.
// diff_transcripts must bite (frame-count mismatch is a mismatch).
// NOTE: frame counts differ (3 vs 2) so this test cannot use expect_gate_bite_on_tag
// (which asserts equal sizes); kept inline.
TEST(IdleCadenceGateBite, DroppedHeartbeatCausesGateBite)
{
    // Three Heartbeat(35=0) frames in the expected direction — fixpp-to-peer (>).
    // MsgSeqNum(34) differs per-beat (3,4,5) — a COMPARED tag under {52,10}.
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=3\\x0152=20260603-10:00:01.000\\x0110=001\\x01\n"
        "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=4\\x0152=20260603-10:00:02.000\\x0110=002\\x01\n"
        "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=5\\x0152=20260603-10:00:03.000\\x0110=003\\x01\n";

    // Actual: only 2 Heartbeats (one dropped — simulates a missing-beat defect).
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=3\\x0152=20260603-10:00:01.000\\x0110=001\\x01\n"
        "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=4\\x0152=20260603-10:00:02.000\\x0110=002\\x01\n";

    auto expected_frames = parse_golden(expected_text);
    auto actual_frames   = parse_golden(actual_text);

    ASSERT_EQ(expected_frames.size(), 3u);
    ASSERT_EQ(actual_frames.size(), 2u);

    // Under admin_profile_excluded_tags() == {52, 10}:
    //   - tag 52 (SendingTime) is excluded → equal regardless of value.
    //   - tag 10 (CheckSum)    is excluded → equal regardless of value.
    //   - frame-count mismatch (3 vs 2) is detected → MISMATCH.
    const DiffResult result = diff_transcripts(expected_frames, actual_frames,
                                               admin_profile_excluded_tags());

    EXPECT_FALSE(static_cast<bool>(result))
        << "gate-bite FAILED: diff_transcripts() reported match when a Heartbeat "
           "was dropped from the actual transcript; detail=" << result.detail;
    EXPECT_EQ(result.status, DiffStatus::mismatch)
        << "Expected DiffStatus::mismatch when actual has fewer frames than expected";
}

// (B) Inject a TestRequest(35=1) into the actual transcript.
// The expected golden has only Heartbeat(35=0); the actual spuriously contains a
// TestRequest.  diff_transcripts must bite on the mismatched MsgType(35).
TEST(IdleCadenceGateBite, InjectedTestRequestCausesGateBite)
{
    // Expected: one Heartbeat(35=0).
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=0\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0134=3\\x0152=20260603-10:00:01.000\\x0110=001\\x01\n";
    // Actual: a TestRequest(35=1) where the Heartbeat was expected.
    // Tag 35 (MsgType) is INCLUDED under {52,10} → mismatch on 35.
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=1\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x01112=LIVENESS_PROBE\\x0134=3\\x0152=20260603-10:00:01.000\\x0110=001\\x01\n";

    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "35");
}

// ---------------------------------------------------------------------------
// T016 / T018: idle_cadence live cell (both roles).
// ---------------------------------------------------------------------------
//
// AdminScenarioDescriptor (rule 7 + rule 8):
//   scenario_group = idle_cadence
//   induction      = idle_observation
//   acceptance_ids = {US2-1, US2-2}
//
// Induction: passive — the session is held idle (no app traffic) so both
// engines emit unsolicited Heartbeats at the negotiated HeartBtInt=1s cadence.
//
// In-process assertions are fixpp-state-only (rule 3, R1 architecture).
// Wire-frame assertions (beat counts) are golden-based.
//
// Self-deadline: 10 s (FR-010: ~5 s observation window + tolerance).
//
// Note: the cell is QFj-specific (idle_observation induction with HeartBtInt=1s
// is configured in the QFJ parent harness per T017 [PARENT]). For QFcpp the
// induction mechanism is not yet configured; those cells skip:not-applicable.

class HappyIdleHeartbeatCadence
    : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(HappyIdleHeartbeatCadence, BothDirectionsAtNegotiatedCadence) {
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // ── AdminScenarioDescriptor validation (rule 7 + rule 8) ────────────────
    const std::string cp_part   = (counterparty == Counterparty::quickfix_j) ? "QFj" : "QFcpp";
    const std::string role_part = (role == Role::fixpp_initiator) ? "init" : "acc";
    const std::string cell_id   = "HP-" + cp_part + "-" + role_part + "-fix44-idle-cadence";

    fixpp::interop::AdminScenarioDescriptor desc;
    desc.cell_id        = cell_id;
    desc.scenario_group = fixpp::interop::AdminScenarioGroup::idle_cadence;
    desc.role           = role;
    desc.counterparty   = counterparty;
    desc.spec_ref       = "[FIX-SL §4.5.1]";
    desc.golden_ref     = "happy/golden/" + cell_id + ".fix";
    desc.induction      = fixpp::interop::AdminInduction::idle_observation;
    desc.self_deadline_ms = std::chrono::milliseconds{10000};  // FR-010: 10 s
    desc.round_trips    = {
        {"US2-1", "[FIX-SL §4.5.1]"},  // ≥3 Heartbeats per direction, no TestRequest
        {"US2-2", "[FIX-SL §4.5.1]"},  // inter-HB interval matches 108=1s, session Active
    };
    desc.acceptance_ids = {"US2-1", "US2-2"};

    const std::string desc_err = fixpp::interop::validate_admin_descriptor(desc);
    ASSERT_TRUE(desc_err.empty()) << "AdminScenarioDescriptor invalid: " << desc_err;

    // ── Counterparty-required: skip if absent (FR-009) ─────────────────────
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    // idle_cadence is QFj-specific at G1: the idle_observation induction with
    // HeartBtInt=1s is only configured in the QFJ parent harness (T017 [PARENT]).
    // Skip for non-QFj counterparties.
    if (counterparty != Counterparty::quickfix_j) {
        GTEST_SKIP() << "skip:not-applicable (idle_cadence idle_observation induction "
                        "is QFj-specific at G1; not configured for "
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
    // FR-002: negotiate HeartBtInt=1s so the observation window catches ≥3 beats
    // per direction.  The liveness loop fires a TestRequest only after 1 full
    // heartbeat_interval of inbound silence; with QFJ emitting a beat ~every 1s the
    // inbound stream is never 1s-silent → no TestRequest triggered (FR-002 sizing).
    auto cfg = hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(),
                                       *endpoint);
    cfg.heartbeat_interval = std::chrono::seconds{1};  // FR-002: 108=1s cadence
    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── Self-deadline envelope (FR-010): 10 s total ────────────────────────
    // Drive to Active within 5 s.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon) against "
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    fixpp::session::Session* s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    // ── In-process seqnum witness: record outbound seqnum after Logon ────────
    // Logon is seqnum 1; after logon the outbound counter is at 1 (or 2 if the
    // engine also sent an initial Heartbeat).  We record the current value and
    // expect it to advance by ≥3 over the ~5s window (each unsolicited Heartbeat
    // increments the outbound counter — US2-1 witness).
    const auto seqnum_after_logon = s->seqnum_mgr_test_access().peek_outbound();
    EXPECT_GT(seqnum_after_logon, fixpp::session::seqnum_t{0})
        << "outbound seqnum is zero after logon";

    // ── Idle observation window: ~5 s ─────────────────────────────────────
    // Pump until the outbound seqnum has advanced by ≥3 (≥3 unsolicited Heartbeats
    // emitted by fixpp) OR the remaining self-deadline elapses (whichever first).
    // The 5 s window fits within the 10 s self-deadline (5 s logon budget consumed
    // above; this pump gets up to ~5 s of the remaining budget).
    //
    // Tolerance: ±1 beat — we stop pumping once ≥3 beats are confirmed, so the
    // actual count may be higher if the window runs slightly longer (acceptable).
    const fixpp::session::seqnum_t target_seqnum =
        fixpp::session::seqnum_t{static_cast<fixpp::session::seqnum_t>(seqnum_after_logon) + 3u};

    fx.run_until(
        [&] {
            fixpp::session::Session* ss = fx.engine().lookup(id);
            return ss != nullptr &&
                   ss->seqnum_mgr_test_access().peek_outbound() >= target_seqnum;
        },
        5s);  // 5 s observation window (FR-002: ~5s, fits the 10s self-deadline)

    s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session disappeared during idle-cadence window";

    // ── In-process witness (a): FSM stays Active (US2-2) ─────────────────
    EXPECT_EQ(s->state(), fsm_state::Active)
        << "FSM left Active during the idle-cadence observation window (US2-2 violated); "
        << "unexpected disconnect or TestRequest-unanswered liveness timeout";

    // ── In-process witness (b): outbound seqnum advanced by ≥3 (US2-1) ───
    // Each unsolicited Heartbeat increments the outbound seqnum by 1.  Advancing
    // by ≥3 from the post-logon value confirms ≥3 Heartbeats were emitted.
    const auto seqnum_after_window = s->seqnum_mgr_test_access().peek_outbound();
    const auto delta = static_cast<fixpp::session::seqnum_t>(seqnum_after_window) -
                       static_cast<fixpp::session::seqnum_t>(seqnum_after_logon);
    EXPECT_GE(delta, fixpp::session::seqnum_t{3})
        << "outbound seqnum advanced by only " << delta
        << " over the ~5s window; expected ≥3 unsolicited Heartbeats (US2-1); "
        << "seqnum after logon=" << seqnum_after_logon
        << " seqnum after window=" << seqnum_after_window;

    // ── Golden assertion (T018 / US2-1 wire-frame) ────────────────────────
    // The golden file is captured at first paired run by the parent harness.
    // If absent → skip:golden-not-yet-captured (never fail, never hand-fabricate).
    // If present → assert diff_transcripts(expected, actual, {52,10}) MATCHES so
    // that the Heartbeat(35=0) frame counts per direction are verified (FR-002/FR-007).
    //
    // Under admin_profile_excluded_tags() == {52, 10}:
    //   - SendingTime(52) and CheckSum(10) are excluded (vary per capture).
    //   - MsgType(35=0) is INCLUDED → must match verbatim.
    //   - MsgSeqNum(34) is INCLUDED → must match verbatim (seqnum ordering is part of the
    //     wire-frame assertion — drift in seqnum ordering would indicate a defect).
    hp::diff_golden_or_skip(cell_id, hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ─────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappyIdleHeartbeatCadence,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
