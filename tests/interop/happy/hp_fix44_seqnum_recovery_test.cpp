// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_seqnum_recovery_test.cpp — 016 T013 [US1] / 018 T011+T014+T015.
//
// Happy-path interop cell:
//   Logon -> higher-numbered peer message -> ResendRequest/GapFill recovery,
//   over TLS, FIX 4.4.
//
// 016 T013 role: the original US1 live-cell driver (skip:counterparty-unavailable,
//   in-process seqnum/FSM witness). Its HP-*-fix44-seqnum-recovery cells are NOT
//   `inrepo_golden` and never were — no HP-*-seqnum-recovery.fix has ever existed.
//   The wire-frame gate is the 018 TEST_P's, below; this one's witness is the FSM.
//
// 018 T011 extension (recovery_inbound / AC {US3-1, US3-2, US3-4}):
//   Induction = withhold_frame (parent withholds one QFJ→fixpp frame so the next
//   received MsgSeqNum exceeds expected). Golden asserts fixpp's ResendRequest
//   (35=2, 7=BeginSeqNo, 16=EndSeqNo) and QFJ's reply (GapFill or replay).
//   In-process witnesses:
//     (a) FSM stays Active throughout (recovery runs as AwaitingResend transient,
//         NOT a separate fsm_state — session stays Active during gap-fill dialogue).
//     (b) Outbound seqnum advances beyond Logon (at least the ResendRequest emitted).
//     (c) Expected inbound seqnum reaches the post-GapFill FLOOR — past the frame
//         that revealed the gap, which only applying NewSeqNo(36) can reach. A bare
//         "advanced at all" is satisfied by the GapFill's own in-sequence +1 and is
//         a spurious hit; the derivation is at the assertion site.
//     (d) Session returns to Active (not Disconnected) after the recovery window.
//   The golden asserts tags 7/16 (ResendRequest range) and 123/36/43 (reply)
//   verbatim; see the T014 note below for why 122 is the one excluded.
//
// 018 T014 (golden assertion for recovery_inbound cells) — WIRED by fixpp#462:
//   Cell id is HP-QFj-{init,acc}-fix44-recovery-inbound — its OWN id, not the
//   T029 reuse-and-enrich of HP-*-seqnum-recovery the 018 plan assumed. The reason
//   is NOT that a filter can name only one case — a gtest filter takes
//   ':'-separated patterns. It is that a CELL carries one counterparty environment:
//   the 016 smoke TEST_P below is a witness only while the peer sends nothing
//   unusual, so running it under the withhold induction would retire it. Both ids
//   now exist and both TEST_Ps run. `run_interop_cell.py` registers the pair with
//   `cp_withhold_frame=True`, under which the QFJ counterparty SKIPS one outbound
//   MsgSeqNum and sends a Heartbeat at the next one — the induction that makes
//   this test's "the parent withholds a QFJ->fixpp frame" true rather than
//   aspirational (it had no implementation at all until #462).
//   The golden check runs in the harness's `_finalize`, on THIS run's capture,
//   as `interop_golden_check --check verbatim-poss-dup` (fixpp#445's fail-closed
//   path). The profile is poss-dup ({52,10,122}), NOT admin ({52,10}): QFJ has no
//   stored frame at the withheld number to copy a SendingTime from, so it stamps
//   the GapFill's OrigSendingTime(122) with "now" and a 122-verbatim golden would
//   drift on every run. 122's VALUE and PRESENCE are therefore unasserted here;
//   34, 7, 16, 43, 123 and 36 — everything FR-007 is about — stay verbatim.
//
// 018 T015 (SC-004 gate-bite negative test for recovery tags):
//   Mutate tag 7 (BeginSeqNo), 16 (EndSeqNo), or 123 (GapFillFlag) — tags compared
//   under the {52,10} admin profile — and assert diff_transcripts() bites with
//   DiffStatus::mismatch. Never mutate 52/10 (canonicalized away → false non-biting pass).
//   [feedback_fail_placeholder_red_test]: real DiffResult assertion, not SUCCEED().
//
// spec_ref [FIX-SL §4.5.3 / §4.8.1 / §4.8.2 / §4.8.5].
// AC coverage (T011): {US3-1, US3-2, US3-4} for each role.
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <string>
#include <tuple>

#include "hp_support.hpp"
#include "support/scenario_descriptor.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

// ---------------------------------------------------------------------------
// SC-004 gate-bite negative tests (T015) — self-contained, no live QFJ needed.
// ---------------------------------------------------------------------------
//
// Contract: mutate a COMPARED tag (7, 16, or 123) and assert diff_transcripts()
// bites. NEVER mutate 52 or 10 — those are canonicalized by {52,10} and would
// yield a false non-biting "pass" (SC-004 rule).
// [feedback_fail_placeholder_red_test]: real DiffResult assertion, not SUCCEED().

TEST(RecoveryInboundGateBite, MutatedTag7BeginSeqNoCausesGateBite) {
    // Synthetic ResendRequest (35=2) with BeginSeqNo(7) and EndSeqNo(16).
    // The admin normalization profile {52,10} excludes ONLY SendingTime and CheckSum.
    // Tag 7 (BeginSeqNo) is a COMPARED tag — a mutation must make the gate bite.
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=2\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x017=3\\x0116=0\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    // Actual: BeginSeqNo mutated from 3 to 5 (different gap range).
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=2\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x017=5\\x0116=0\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

    // Under admin_profile_excluded_tags() == {52, 10}:
    //   - tag 52 (SendingTime) is excluded → equal regardless of value.
    //   - tag 10 (CheckSum)    is excluded → equal regardless of value.
    //   - tag 7  (BeginSeqNo) is INCLUDED → must match verbatim → MISMATCH.
    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "7");
}

TEST(RecoveryInboundGateBite, MutatedTag16EndSeqNoCausesGateBite) {
    // Synthetic ResendRequest (35=2) — mutate EndSeqNo(16) which is a COMPARED tag.
    const char* expected_text =
        "> 8=FIX.4.4\\x0135=2\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x017=3\\x0116=0\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    // Actual: EndSeqNo changed from 0 (open-ended) to 10.
    const char* actual_text =
        "> 8=FIX.4.4\\x0135=2\\x0149=FIXPP_INIT\\x0156=CPTY_ACC"
        "\\x017=3\\x0116=10\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

    // Tag 16 (EndSeqNo) is INCLUDED under {52,10} → must match verbatim → MISMATCH.
    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "16");
}

TEST(RecoveryInboundGateBite, MutatedTag123GapFillFlagCausesGateBite) {
    // Synthetic SequenceReset-GapFill (35=4) reply from QFJ with GapFillFlag(123=Y).
    // Mutate 123 (GapFillFlag) — a COMPARED tag under {52,10} → must bite.
    const char* expected_text =
        "< 8=FIX.4.4\\x0135=4\\x0149=CPTY_ACC\\x0156=FIXPP_INIT"
        "\\x01123=Y\\x0136=5\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";
    // Actual: GapFillFlag absent (or changed to N).
    const char* actual_text =
        "< 8=FIX.4.4\\x0135=4\\x0149=CPTY_ACC\\x0156=FIXPP_INIT"
        "\\x01123=N\\x0136=5\\x0152=20260603-10:00:00.000\\x0110=001\\x01\n";

    // Tag 123 (GapFillFlag) is INCLUDED under {52,10} → must match verbatim → MISMATCH.
    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "123");
}

// ---------------------------------------------------------------------------
// T011 / T014: recovery_inbound live cell (both roles).
// ---------------------------------------------------------------------------
//
// AdminScenarioDescriptor (rule 7 + rule 8):
//   scenario_group = recovery_inbound
//   induction      = withhold_frame
//   acceptance_ids = {US3-1, US3-2, US3-4}
//
// In-process witnesses (contracts/admin-scenario-descriptor.md rule 3):
//   (a) FSM stays Active throughout (recovery is AwaitingResend transient; NOT a
//       distinct fsm_state — the session remains Active during the gap-fill dialogue).
//   (b) Outbound seqnum advances beyond Logon (ResendRequest + any admin in the window).
//   (c) Inbound seqnum expected value advances after recovery (no prefix loss).
//
// Wire-frame assertions: golden-based only (rule 3 + R1 architecture).
// Self-deadline: 30 s (FR-010 recovery).

class HappySeqnumRecoveryInbound : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {
};

TEST_P(HappySeqnumRecoveryInbound, GapInductionResendRequestAndReturn) {
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // ── AdminScenarioDescriptor validation (rule 7 + rule 8) ────────────────
    const std::string cp_part = (counterparty == Counterparty::quickfix_j) ? "QFj" : "QFcpp";
    const std::string role_part = (role == Role::fixpp_initiator) ? "init" : "acc";
    const std::string cell_id = "HP-" + cp_part + "-" + role_part + "-fix44-recovery-inbound";

    fixpp::interop::AdminScenarioDescriptor desc;
    desc.cell_id = cell_id;
    desc.scenario_group = fixpp::interop::AdminScenarioGroup::recovery_inbound;
    desc.role = role;
    desc.counterparty = counterparty;
    desc.spec_ref = "[FIX-SL §4.5.3/§4.8.2/§4.8.5]";
    desc.golden_ref = "happy/golden/" + cell_id + ".fix";
    desc.induction = fixpp::interop::AdminInduction::withhold_frame;
    desc.self_deadline_ms = std::chrono::milliseconds{30000};  // FR-010: 30 s
    desc.round_trips = {
        {.ac_ref = "US3-1",
         .spec_ref = "[FIX-SL §4.5.3]"},  // fixpp detects gap, emits ResendRequest(7/16)
        {.ac_ref = "US3-2",
         .spec_ref = "[FIX-SL §4.8.5]"},  // QFJ replies with GapFill/replay; fixpp applies
        {.ac_ref = "US3-4", .spec_ref = "[FIX-SL §4.8.2]"},  // both peers at Active, no prefix loss
    };
    desc.acceptance_ids = {"US3-1", "US3-2", "US3-4"};

    const std::string desc_err = fixpp::interop::validate_admin_descriptor(desc);
    ASSERT_TRUE(desc_err.empty()) << "AdminScenarioDescriptor invalid: " << desc_err;

    // ── Counterparty-required: skip if absent (FR-009) ─────────────────────
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    // recovery_inbound admin round-trip is QFj-only at G1: the withhold_frame
    // induction is only configured in the QFJ parent harness (T013 [PARENT]).
    // Skip for non-QFj counterparties.
    if (counterparty != Counterparty::quickfix_j) {
        GTEST_SKIP() << "skip:not-applicable (recovery_inbound admin round-trip is QFj-only at G1; "
                        "not configured for "
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
    auto cfg =
        hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(), *endpoint);
    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── Drive to Active (logon) — 5 s budget ─────────────────────────────
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon) against " << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    // ⚠️ The post-recovery witnesses below are ABSOLUTE, not relative to a snapshot
    // taken here. A snapshot is a RACE: the counterparty's induction fires ~1 s
    // after ITS onLogon, `drive_to_active` pumps in slices and only tests its
    // predicate between them, so a slow logon can put part (or all) of the induced
    // exchange BEFORE this line. A relative floor then moves with the race and the
    // cell fails on a correct run — trading the spurious hit for a spurious miss.
    //
    // The scenario is closed, so the postconditions are derivable and fixed. Both
    // roles run the identical sequence (for the acceptor the peer's Logon carries
    // 141=Y and fixpp's reset-and-restore leaves expected inbound at 2 just the
    // same), which is why one pair of constants covers both:
    //
    //   peer Logon        34=1  -> fixpp expected inbound becomes 2
    //   WITHHELD                -> nothing is ever sent at 2
    //   peer Heartbeat    34=3  -> reveals the gap
    //   fixpp ResendRequest 34=2 -> fixpp next OUTBOUND becomes 3
    //   peer GapFill      34=2, 36=4 -> ordinary check_inbound 2->3, THEN the
    //                                   NewSeqNo jump 3->4
    //
    // So: inbound must reach 4, outbound must reach 3. Derived from the induction's
    // definition — if the induction changes, this arithmetic is what must change
    // with it, and the golden pins the same frames on the wire.
    static constexpr auto kPostRecoveryInbound = fixpp::session::seqnum_t{4};
    static constexpr auto kPostRecoveryOutbound = fixpp::session::seqnum_t{3};

    // ── In-process witness (b), part 1: the Logon was sent ─────────────────
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // Diagnostics only — deliberately NOT an input to any assertion (see above).
    const auto inbound_before_recovery = s->seqnum_mgr_test_access().next_inbound_unsafe();

    // ── Recovery window: 25 s budget (total self-deadline is 30 s; 5 s for logon) ─
    // The parent withholds a QFJ→fixpp frame so the next received MsgSeqNum exceeds
    // expected. fixpp detects the gap, emits ResendRequest(35=2,7,16), and QFJ replies
    // with GapFill or replay. The session stays Active throughout (AwaitingResend is
    // a transient flag, not a distinct fsm_state).
    //
    // ⚠️ "inbound advanced AT ALL" would be a SPURIOUS HIT, not a witness. The
    // GapFill arrives IN SEQUENCE, at the withheld number: Session's ordinary
    // check_inbound advances the counter by one BEFORE apply_inbound_sequence_reset
    // performs the absolute NewSeqNo(36) jump (the two steps are named in that
    // order in src/session/session.cpp's GapFill branch). So a fixpp that RECEIVED
    // the GapFill and never APPLIED it still lands on 3 — which is why the
    // postcondition is 4 and not "more than it was".
    //
    // The PUMP predicate stays a floor (`>=`) deliberately, so an over-advancing
    // fixpp stops pumping immediately and is caught by the equality below rather
    // than burning the whole 25 s window first. Predicate and postcondition are
    // different jobs: one decides when to stop looking, the other decides the verdict.
    fx.run_until(
        [&] {
            auto ss = fx.engine().lookup(id);
            return ss != nullptr &&
                   ss->seqnum_mgr_test_access().next_inbound_unsafe() >= kPostRecoveryInbound;
        },
        25s);

    s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session disappeared during recovery window";

    // ── In-process witness (a): FSM stays Active (US3-4 partial — fixpp side) ─
    // The session MUST NOT have disconnected during the recovery dialogue.
    EXPECT_EQ(s->state(), fsm_state::Active)
        << "FSM left Active during/after the recovery_inbound window (US3-4 violated)";

    // ── In-process witness (c): the gap-fill was APPLIED (no prefix loss) ──
    // EQUALITY, not a floor. A floor is open on the OVER-advance side, and an
    // over-advance is a defect of exactly the same kind: install NewSeqNo + 1 and
    // fixpp expects 5, silently losing the next legitimate frame — while the wire
    // is unchanged, so both goldens still match and `>= 4` still passes. The
    // scenario is closed, so the correct value is a single number and anything
    // else is wrong in one direction or the other.
    EXPECT_EQ(s->seqnum_mgr_test_access().next_inbound_unsafe(), kPostRecoveryInbound)
        << "inbound expected seqnum is not " << kPostRecoveryInbound
        << " after the recovery window (NewSeqNo(36) may have been received but not "
        << "applied; US3-2/US3-4). Pre-window reading was " << inbound_before_recovery
        << " — if that is not 2 the induction outran the pump and this cell's "
        << "arithmetic needs re-deriving, not just retrying";

    // ── In-process witness (b), part 2: the ResendRequest was emitted ──────
    // 35=2 is an outbound admin frame, so the counter is exactly 3 here. Equality
    // for the same reason as (c): a double increment overshoots to 4, the wire is
    // unchanged, and a floor would pass it while the next outbound frame carries a
    // seqnum the peer will gap on.
    EXPECT_EQ(s->seqnum_mgr_test_access().peek_outbound(), kPostRecoveryOutbound)
        << "outbound seqnum is not " << kPostRecoveryOutbound
        << "; the ResendRequest may not have been sent, or the counter over-advanced";

    // ── Golden assertion (T014 / US3-1/US3-2) — in the parent harness ──────
    // Not a call site here, by #445's design: the wire-frame check runs in
    // `_finalize` against THIS run's capture, as `interop_golden_check --check
    // verbatim-poss-dup --golden happy/golden/<cell_id>.fix`. #462 is what made
    // the invocation reachable — the HP-QFj-*-fix44-recovery-inbound cells select
    // this TEST_P and carry the withhold induction. The in-process witnesses
    // above and that golden are two different instruments: the witnesses cannot
    // see the wire, and the golden cannot see the FSM.

    // ── Graceful stop (Logout) ─────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappySeqnumRecoveryInbound,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

// ---------------------------------------------------------------------------
// 016 T013 original cell (preserved intact — extends in place per brief).
// ---------------------------------------------------------------------------
//
// ResynchronizesWithoutFatalDisconnect — the original US1 smoke witness:
// drives to Active, holds a bounded window, asserts Active + seqnum > 1.
// No AdminScenarioDescriptor (016 T013 predates the 018 descriptor contract).
// Golden assertion deferred to the T011/T014 enriched cell above.

class HappySeqnumRecovery : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(HappySeqnumRecovery, ResynchronizesWithoutFatalDisconnect) {
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

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
    auto cfg =
        hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(), *endpoint);
    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon) against " << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // This cell runs with NO induction: nothing injects a gap, so the window
    // below observes an idle Active session. The gap and fixpp's
    // ResendRequest/SequenceReset-GapFill exchange belong to the 018 TEST_P
    // above and its own recovery-inbound cells (fixpp#462).
    fx.run_until(
        [&] {
            auto current = fx.engine().lookup(id);
            return current == nullptr || current->state() != fsm_state::Active;
        },
        1500ms);
    s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session disappeared during seqnum recovery window";
    EXPECT_EQ(s->state(), fsm_state::Active)
        << "session did not remain Active after seqnum recovery window";

    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappySeqnumRecovery,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
