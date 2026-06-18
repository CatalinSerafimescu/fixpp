// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_reject_invalid_admin_test.cpp — 016 T012 [US1] / 018 T019+T021+SC-004.
//
// Happy-path interop cell:
//   Logon -> invalid admin message -> Reject(35=3), over TLS, FIX 4.4.
//
// 016 T012 role: the original US1 live-cell driver (skip:counterparty-unavailable,
//   in-process seqnum/FSM witness). The parent gate golden-diffs the proxy capture
//   against HP-*-reject-invalid-admin.fix.
//
// 018 T019 extension (session_reject / AC {US4-1, US4-2}):
//   Induction = proxy_corrupt (parent corrupts an admin frame in flight so fixpp
//   receives a malformed message and emits Reject(35=3, 45, 373[, 371]) per
//   [FIX-SL §4.5.4] / S-007). fixpp NEVER originates malformed bytes — the
//   corruption is parent-proxy-injected. In-process witnesses:
//     (a) FSM stays Active throughout — a session-level Reject is non-fatal.
//     (b) Outbound seqnum advances beyond the post-Logon value, confirming the
//         Reject(35=3) was emitted (the seqnum is consumed for the Reject).
//     (c) Session continues to exchange Heartbeats after the Reject
//         (seqnum keeps advancing in the hold window).
//   Wire-frame assertions: golden-based only (rule 3 + R1 architecture).
//   Self-deadline: 10 s (FR-010).
//
// 018 T021 (golden assertion for session_reject cells):
//   Cell ids (reuse-and-enrich per T029):
//     HP-QFj-{init,acc}-fix44-reject-invalid-admin
//   Golden asserts Reject(35=3, 45, 373[, 371]) and survival Heartbeat verbatim
//   under the {52,10} admin profile.
//   When the golden is absent → skip:golden-not-yet-captured (never fail/fabricate).
//   Also appended: reject-vs-disconnect peer divergence note in KNOWN-LIMITATIONS.md.
//
// 018 SC-004 gate-bite negative tests:
//   Mutate a COMPARED tag (45 RefSeqNum or 373 SessionRejectReason) on a Reject(35=3)
//   frame and assert diff_transcripts() bites with DiffStatus::mismatch.
//   Companion: tag 52/10 (canonicalized) mutations MUST NOT bite.
//   [feedback_fail_placeholder_red_test]: real DiffResult assertion, not SUCCEED().
//
// spec_ref [FIX-SL §4.5.4 Rejecting invalid messages].
// AC coverage (T019): {US4-1, US4-2} for each role.
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <string>
#include <tuple>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
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
// SC-004 gate-bite negative tests — self-contained, no live QFJ needed.
// ---------------------------------------------------------------------------
//
// Contract: mutate a COMPARED tag on a Reject(35=3) frame (45 RefSeqNum or
// 373 SessionRejectReason) and assert diff_transcripts() bites.
// NEVER mutate 52 or 10 — those are canonicalized by {52,10} and would
// yield a false non-biting "pass" (SC-004 rule).
// [feedback_fail_placeholder_red_test]: real DiffResult assertion, not SUCCEED().

TEST(SessionRejectGateBite, MutatedTag45RefSeqNumCausesGateBite)
{
    // Synthetic Reject(35=3) with RefSeqNum(45=2) and SessionRejectReason(373=0).
    // The admin normalization profile {52,10} excludes ONLY SendingTime and CheckSum.
    // Tag 45 (RefSeqNum) is a COMPARED tag — a mutation must make the gate bite.
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=3\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0145=2\\x01373=0\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    // Actual: RefSeqNum mutated from 2 to 5 (references a different sequence number).
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=3\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0145=5\\x01373=0\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

    // Under admin_profile_excluded_tags() == {52, 10}:
    //   - tag 52 (SendingTime) is excluded → equal regardless of value.
    //   - tag 10 (CheckSum)    is excluded → equal regardless of value.
    //   - tag 45 (RefSeqNum)  is INCLUDED → must match verbatim → MISMATCH.
    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "45");
}

TEST(SessionRejectGateBite, MutatedTag373SessionRejectReasonCausesGateBite)
{
    // Synthetic Reject(35=3) — mutate SessionRejectReason(373) which is a COMPARED tag.
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=3\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0145=3\\x01373=0\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    // Actual: SessionRejectReason changed from 0 (InvalidTagNumber) to 5 (ValueIsIncorrect).
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=3\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x0145=3\\x01373=5\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

    // Tag 373 (SessionRejectReason) is INCLUDED under {52,10} → must match → MISMATCH.
    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "373");
}

// ---------------------------------------------------------------------------
// T019 / T021: session_reject live cell (both roles).
// ---------------------------------------------------------------------------
//
// AdminScenarioDescriptor (rule 7 + rule 8):
//   scenario_group = session_reject
//   induction      = proxy_corrupt
//   acceptance_ids = {US4-1, US4-2}
//
// In-process witnesses (contracts/admin-scenario-descriptor.md rule 3):
//   (a) FSM stays Active throughout — Reject(35=3) is a non-fatal session event.
//   (b) Outbound seqnum advances beyond post-logon value (Reject consumed a seqnum).
//   (c) Seqnum keeps advancing in the hold window (survival Heartbeats flow,
//       confirming the session continued normal admin exchange, AC US4-2).
//
// Wire-frame assertions: golden-based only (rule 3 + R1 architecture).
// Self-deadline: 10 s (FR-010).
// fixpp NEVER originates malformed bytes — the corruption is parent-proxy-injected.

class HappyRejectInvalidAdmin
    : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(HappyRejectInvalidAdmin, RejectInvalidAdminSurvives) {
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // ── AdminScenarioDescriptor validation (rule 7 + rule 8) ────────────────
    const std::string cp_part   = (counterparty == Counterparty::quickfix_j) ? "QFj" : "QFcpp";
    const std::string role_part = (role == Role::fixpp_initiator) ? "init" : "acc";
    const std::string cell_id   = "HP-" + cp_part + "-" + role_part + "-fix44-reject-invalid-admin";

    fixpp::interop::AdminScenarioDescriptor desc;
    desc.cell_id        = cell_id;
    desc.scenario_group = fixpp::interop::AdminScenarioGroup::session_reject;
    desc.role           = role;
    desc.counterparty   = counterparty;
    desc.spec_ref       = "[FIX-SL §4.5.4]";
    desc.golden_ref     = "happy/golden/" + cell_id + ".fix";
    desc.induction      = fixpp::interop::AdminInduction::proxy_corrupt;
    desc.self_deadline_ms = std::chrono::milliseconds{10000};  // FR-010: 10 s
    desc.round_trips    = {
        {"US4-1", "[FIX-SL §4.5.4]"},  // fixpp emits Reject(35=3,45,373[,371]); session stays Active
        {"US4-2", "[FIX-SL §4.5.4]"},  // subsequent heartbeats flow (non-fatal reject)
    };
    desc.acceptance_ids = {"US4-1", "US4-2"};

    const std::string desc_err = fixpp::interop::validate_admin_descriptor(desc);
    ASSERT_TRUE(desc_err.empty()) << "AdminScenarioDescriptor invalid: " << desc_err;

    // ── Counterparty-required: skip if absent (FR-009) ─────────────────────
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    // session_reject admin round-trip is QFj-only at G1: the proxy_corrupt
    // induction is only configured in the QFJ parent harness (T020 [PARENT]).
    // Skip for non-QFj counterparties.
    if (counterparty != Counterparty::quickfix_j) {
        GTEST_SKIP() << "skip:not-applicable (session_reject admin round-trip is QFj-only at G1; "
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

    // Phase 9.H (proxy_corrupt C2): the dictionary tables PMR-allocate on this
    // resource; it is declared BEFORE `fx` so it outlives the session that holds
    // cfg.dictionary (destruction is reverse-declaration order: fx, then dict_mr).
    std::pmr::monotonic_buffer_resource dict_mr;

    fixpp::interop::InteropEngineFixture fx;
    auto cfg = hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(),
                                       *endpoint);

    // ── Phase 9.H (proxy_corrupt C2): enable inbound dictionary validation ──
    // The counterparty induces one out-of-context Symbol(55)=BAD on a 35=1
    // TestRequest (INTEROP_CP_CORRUPT_ADMIN). With validate_inbound_messages +
    // the production FIX44 dictionary, fixpp's validate gate flags the
    // unexpected tag and emits Reject(35=3, 373=2) (S-007 path). The legit admin
    // Logon validates cleanly — viability-proven on the bench by
    // TableViewTest.Fix44ValidationSurfaceForProxyCorruptCell. The dictionary
    // XML path is injected by the parent shim (run_interop_cell.py) as
    // FIXPP_FIX44_DICT_XML; absent it (e.g. a non-shim direct run) the cell
    // falls back to the default config and the golden gate skips.
    if (const char* dict_xml = std::getenv("FIXPP_FIX44_DICT_XML");
        dict_xml != nullptr && dict_xml[0] != '\0') {
        auto loaded = fixpp::dict::XmlLoader{}.load(dict_xml, &dict_mr);
        cfg.dictionary = std::make_shared<const fixpp::dict::Dictionary>(std::move(loaded));
        cfg.validate_inbound_messages = true;
    }

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

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    // ── In-process witness (b): seqnum snapshot after logon ────────────────
    // Confirms at least Logon(34=1) was sent; the Reject will push it further.
    const auto seqnum_after_logon = s->seqnum_mgr_test_access().peek_outbound();
    EXPECT_GT(seqnum_after_logon, fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // ── Hold session live for up to 4 s (US4-2: subsequent heartbeats) ────
    // The parent proxy will inject a corrupted admin frame in this window.
    // fixpp emits Reject(35=3, 45, 373[, 371]) (S-007 path) and continues
    // normal session operation — the seqnum advances beyond the post-logon
    // snapshot, confirming (b) the Reject was emitted and (c) heartbeats flow.
    //
    // NOTE: fixpp NEVER originates the malformed bytes — the corruption is
    // parent-proxy-injected in flight. We assert nothing about fixpp emitting
    // malformed content (there is no such API).
    fx.run_until(
        [&] {
            auto ss = fx.engine().lookup(id);
            return ss != nullptr &&
                   ss->seqnum_mgr_test_access().peek_outbound() > seqnum_after_logon;
        },
        4s);

    // ── In-process witness (a): FSM still Active (US4-1 — non-fatal reject) ─
    {
        auto ss = fx.engine().lookup(id);
        ASSERT_NE(ss, nullptr) << "session vanished mid-cell (unexpected disconnect)";
        EXPECT_EQ(ss->state(), fsm_state::Active)
            << "FSM left Active after the Reject window (session_reject must be non-fatal)";
    }

    // ── In-process witness (b+c): seqnum advanced (Reject + heartbeats sent) ─
    {
        auto ss = fx.engine().lookup(id);
        ASSERT_NE(ss, nullptr);
        EXPECT_GT(ss->seqnum_mgr_test_access().peek_outbound(), seqnum_after_logon)
            << "outbound seqnum did not advance past the post-logon snapshot; "
               "expected Reject(35=3) + heartbeats to push it forward";
    }

    // ── Golden assertion (T021 / US4-1 + US4-2) ──────────────────────────
    // The golden is captured at first paired run by the parent proxy capture.
    // If absent: skip with reason (never FAIL, never hand-fabricate).
    // If present: assert diff_transcripts(expected, actual, {52,10}) MATCHES,
    // so that tags 45 (RefSeqNum) and 373 (SessionRejectReason) are verified
    // verbatim (FR-007). Tag 371 (RefTagID) is also compared if present.
    hp::diff_golden_or_skip(cell_id, hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ─────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappyRejectInvalidAdmin,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
