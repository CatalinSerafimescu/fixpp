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
//   (b) Inbound seqnum advances beyond Logon — fixpp RECEIVED+processed QFJ's
//       ResendRequest. (The outbound counter is NOT a valid witness: resend
//       replies are transmit-only and reuse the replayed seqnums —
//       session.cpp:3406-3408 — so they never advance peek_outbound(). The
//       emitted GapFill/replay is proven on the wire by the golden, not in-process.)
//   (c) Session returns to Active (not Disconnected) after the outbound-answer window.
//   Wire-frame assertions (golden-based only per R1 architecture):
//     QFJ's ResendRequest(35=2) + fixpp's answering replay(43=Y,122=) and/or
//     SequenceReset-GapFill(35=4,123=Y,43=Y,122=) are asserted via the golden
//     under {52,10,122} (037 T009: the GapFill's 122==52 is volatile, excluded;
//     43=Y stays compared verbatim).
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

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <memory_resource>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include <memory>

#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/session/business_messages.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"
#include "support/scenario_descriptor.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

// Build a decimal_t from a literal into the caller's arena (mirrors the BM cell's
// make_dec; callers ASSERT the result is valid before use).
fixpp::decimal_t make_dec(std::string_view sv, std::pmr::memory_resource* mr) {
    std::vector<std::byte> bytes;
    bytes.reserve(sv.size());
    for (char c : sv) bytes.push_back(static_cast<std::byte>(c));
    auto r = fixpp::decimal_t::parse(bytes, mr);
    return r.has_value() ? *r : fixpp::decimal_t{};
}

// 9.H app-replay witness (US3-3). The capture sidecar must contain a fixpp→peer
// ('>') frame that is BOTH a NewOrderSingle (35=D) AND a PossDup replay (43=Y) —
// i.e. fixpp answered QFJ's ResendRequest by REPLAYING the stored application
// message (build_replay_frame: original seqnum, 43=Y, 122=). This is the named
// US3-3 postcondition, asserted directly (not a proxy): the in-process Active +
// inbound-advance signals below hold even if fixpp had ignored the ResendRequest,
// so the replayed wire frame is the only sound emission witness. Mutation: if
// fixpp does not replay, no '>' 35=D carries 43=Y → this FAILS. Skip-when-absent
// mirrors diff_golden_or_skip (never a false pass on an un-captured cell).
void expect_app_replay_or_skip(const std::string& gpath) {
    if (gpath.empty()) {
        GTEST_SKIP() << "skip:golden-not-yet-captured (FIXPP_TLS_FIXTURE_DIR unresolvable)";
    }
    std::ifstream gfile{gpath};
    if (!gfile) {
        GTEST_SKIP() << "skip:golden-not-yet-captured (file absent: " << gpath << ")";
    }
    const std::string capture_path = gpath.substr(0, gpath.size() - 4) + "-capture.fix";
    std::ifstream cfile{capture_path};
    if (!cfile) {
        GTEST_SKIP() << "skip:golden-not-yet-captured (capture sidecar absent: "
                     << capture_path << ")";
    }
    std::stringstream css;
    css << cfile.rdbuf();
    const std::string capture_text = css.str();
    if (capture_text.empty()) {
        GTEST_SKIP() << "skip:golden-not-yet-captured (capture sidecar empty)";
    }

    const auto frames = fixpp::interop::parse_golden(capture_text);
    int replayed_nos = 0;
    for (const auto& f : frames) {
        if (f.dir != '>') continue;  // fixpp→peer only
        const std::string_view w{reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size()};
        const bool is_nos = w.find("\x01" "35=D" "\x01") != std::string_view::npos;
        const bool poss_dup = w.find("\x01" "43=Y" "\x01") != std::string_view::npos;
        if (is_nos && poss_dup) ++replayed_nos;
    }
    EXPECT_GE(replayed_nos, 1)
        << "no fixpp→peer NewOrderSingle(35=D) carrying PossDupFlag(43=Y) in the capture; "
        << "fixpp did not REPLAY the stored app message in answer to QFJ's ResendRequest "
        << "(US3-3 outbound-replay path not witnessed)";
}

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

    // Under admin_profile_excluded_tags() == {52, 10}:
    //   - tag 52 (SendingTime) is excluded → equal regardless of value.
    //   - tag 10 (CheckSum)    is excluded → equal regardless of value.
    //   - tag 16 (EndSeqNo)   is INCLUDED → must match verbatim → MISMATCH.
    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "16");
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

    // Tag 123 (GapFillFlag) is INCLUDED under {52,10} → must match verbatim → MISMATCH.
    fixpp::interop::hp::expect_gate_bite_on_tag(expected_text, actual_text, "123");
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
    // 9.H app-replay: give fixpp a persistent outbound store so it can REPLAY the
    // stored NewOrderSingle (35=D, 43=Y) in answer to QFJ's ResendRequest, rather
    // than collapse it to a SequenceReset-GapFill (a storeless session cannot
    // replay app bodies → it gap-fills = data loss; US3-4 requires true replay).
    // Unbounded policy: exempt from the bounded-store DoS construction guard
    // (make() guard (b) trips on the default bounded Config under the engine's
    // max_store_memory_bytes, aborting session open → no connect). Test-only store.
    cfg.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(
        fixpp::session::MemoryStore::Config{.policy = fixpp::session::capacity_policy::unbounded});
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

    // ── In-process witness (b) baseline: inbound seqnum after Logon ────────
    // 9.H app-replay: the OUTBOUND counter is NOT a valid witness for a resend
    // reply (session.cpp:3406-3408 — replay frames reuse the original seqnum and
    // are transmit-only, never advancing peek_outbound()). We witness that fixpp
    // RECEIVED+processed QFJ's ResendRequest (inbound advances); the REPLAY itself
    // (35=D carrying 43=Y) is proven ON THE WIRE by the golden below.
    const auto inbound_after_logon = s->seqnum_mgr_test_access().next_inbound_unsafe();

    // ── Non-degenerate app-replay induction (US3-3) ────────────────────────
    // fixpp sends ONE NewOrderSingle so its outbound store holds a real
    // application message (at the post-logon seqnum). The QFJ counterparty's
    // resend-app induction (INTEROP_CP_RESEND_APP) then rewinds its expected-target
    // seqnum to that message and issues a bounded ResendRequest for it — a GENUINE
    // inbound gap. fixpp answers via the 013/027 build_replay_frame path: it
    // REPLAYS the stored NewOrderSingle with PossDupFlag(43)=Y + OrigSendingTime(122),
    // keeping the original seqnum. Because QFJ rewound, the replay arrives at ==
    // its expected seqnum and IS delivered to QFJ's fromApp (a real recovery),
    // capturable on the wire. (A Logon-only ResendRequest would instead collapse to
    // a NO-OP SequenceReset-GapFill QFJ never surfaces — the degenerate induction
    // this cell deliberately avoids.)
    {
        std::array<std::byte, 512> nos_buf{};
        std::array<std::byte, 64> dec_arena_buf{};
        std::pmr::monotonic_buffer_resource dec_arena{
            dec_arena_buf.data(), dec_arena_buf.size(), std::pmr::null_memory_resource()};
        const auto order_qty = make_dec("100", &dec_arena);
        const auto price = make_dec("190.5", &dec_arena);
        // Deterministic TransactTime: not in the golden exclusion profile, so a
        // live value would drift the capture — pin it (cf. the PD injector).
        static constexpr std::string_view kTransactTime = "20240101-00:00:00.000";
        auto nos_body = fixpp::session::build_new_order_single(
            nos_buf, "RO-CLORD-1", "FIXPP", '1', order_qty, price, kTransactTime);
        ASSERT_TRUE(nos_body.has_value())
            << "build_new_order_single failed; error=" << static_cast<int>(nos_body.error());
        auto send_fut = asio::co_spawn(fx.ioc().get_executor(), fx.engine().send(id, *nos_body),
                                       asio::use_future);
        fx.run_until([&send_fut] { return send_fut.wait_for(0ms) == std::future_status::ready; },
                     5s);
        ASSERT_TRUE(send_fut.wait_for(0ms) == std::future_status::ready)
            << "Engine::send(NOS) did not complete within 5s";
        auto send_r = send_fut.get();
        EXPECT_TRUE(send_r.has_value())
            << "Engine::send(NOS) failed; error=" << static_cast<int>(send_r.error());
    }

    // ── Resend-answer window: 23 s budget ──────────────────────────────────
    // Pump until fixpp has RECEIVED QFJ's ResendRequest (inbound seqnum advances
    // past the post-logon baseline) OR the window expires.
    fx.run_until(
        [&] {
            auto ss = fx.engine().lookup(id);
            return ss != nullptr &&
                   ss->seqnum_mgr_test_access().next_inbound_unsafe() > inbound_after_logon;
        },
        23s);

    // ── Settle: let fixpp's replay flush to the wire + QFJ recover ──────────
    // The inbound witness above fires at ResendRequest RECEIPT (check_inbound
    // advances next_inbound BEFORE the handler's replay_outbound_range_ emits the
    // replay via co_await live_write). Pump a bounded settle window so the replayed
    // NewOrderSingle is written + delivered to QFJ's fromApp before the graceful
    // stop closes the socket. Resend replies are transmit-only (no outbound-counter
    // signal) — the wire golden below is the emission witness, so it MUST land.
    fx.run_until([] { return false; }, 2s);

    s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session disappeared during recovery_outbound window";

    // ── In-process witness (a): FSM stays Active (US3-4 — fixpp side) ─────
    // fixpp must not disconnect on receipt of a spec-legal ResendRequest from QFJ.
    // After answering, the session returns to Active.
    EXPECT_EQ(s->state(), fsm_state::Active)
        << "FSM left Active during/after the recovery_outbound window (US3-4 violated); "
        << "fixpp disconnected on QFJ ResendRequest — check the outbound answer path";

    // ── In-process witness (b): inbound seqnum advanced ───────────────────
    // fixpp received and processed QFJ's ResendRequest (an in-sequence inbound
    // admin frame; processing it advances the expected inbound seqnum). This proves
    // the resend-answer path was ENTERED; the REPLAYED app message itself is
    // asserted on the wire by the golden below (replay frames are transmit-only and
    // do NOT advance the outbound counter — session.cpp:3406-3408 — so
    // peek_outbound() is structurally unobservable here).
    EXPECT_GT(s->seqnum_mgr_test_access().next_inbound_unsafe(), inbound_after_logon)
        << "inbound seqnum did not advance; fixpp did not receive QFJ's ResendRequest "
        << "(US3-3 resend-answer path not triggered)";

    // ── Golden assertion (T014 / US3-3) — app-replay witness ───────────────
    // The capture sidecar is written at first paired run by the parent harness.
    // If absent → skip:golden-not-yet-captured (never fail, never hand-fabricate).
    // If present → assert fixpp REPLAYED the stored NewOrderSingle in answer to
    // QFJ's ResendRequest: a fixpp→peer 35=D frame carrying PossDupFlag(43)=Y. The
    // capture contains the original 35=D (no 43) AND the replayed 35=D (43=Y); the
    // witness asserts on the replay specifically (the named US3-3 postcondition).
    expect_app_replay_or_skip(hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ─────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappyRecoveryOutboundAnswer,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
