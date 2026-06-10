// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_received_reset_test.cpp — 030 T028 [P].
//
// Live received-141 inbound-advance interop cell (SC-001 / FR-001 / FR-002).
//
// This is the live close-out of the conformance defect that MOTIVATED feature 030:
// found via a failed live acceptor cell vs QuickFIX-cpp/J. It is the received-141
// (knob-OFF) acceptor path — DISTINCT from the 024 reset_on_logon-ON acceptor cell
// (hp_fix44_reset_on_logon_test.cpp T017):
//
//   - 024 T017: fixpp acceptor `reset_on_logon = true` → the knob-driven reset runs
//     BEFORE check_inbound. (Always produced next_inbound==2; never the 030 bug.)
//   - 030 T028 (here): fixpp acceptor `reset_on_logon = false` → a live QFcpp/QFJ
//     INITIATOR sends `Logon(141=Y, 34=1)`. fixpp takes the 013-only received-141
//     path (reset AFTER check_inbound). Pre-030 this clobbered the consumed seq-1
//     reset Logon's inbound advance back to 1 → a SPURIOUS ResendRequest on the
//     peer's next message at 34=2. Post-030 the advance is restored → next_inbound
//     ends at 2 and the peer's 34=2 message is accepted in-sequence (no ResendRequest).
//
// In-process witnesses (what this cell can assert in-process):
//   (a)  fixpp FSM reaches Active — the live counterparty's 141=Y + 34=1 Logon was
//        accepted on the received-141 path (no too-low disconnect / Logout).
//   (b') session_event_sequence_numbers_reset{by_peer_request=true} in the in-process
//        event ring — THE discriminating witness: it is emitted ONLY when the peer sent
//        141=Y (session.cpp:1969, reset_on_logon=false here), so it proves fixpp took
//        the received-141 reset path. A plain non-reset Logon emits no such event.
//   (b)  inbound seqnum == 2 after the Logon exchange — the 030 correction (the consumed
//        seq-1 reset Logon advanced next-expected-inbound to 2). NOT a path discriminator
//        on its own (a plain Logon also lands at 2); it is the no-spurious-ResendRequest
//        harm witness — pre-030 this read 1, making the peer's 34=2 read too-high.
//   (c)  outbound seqnum advanced past the reply Logon (acceptor sent its reply at 34=1).
//
// The end-to-end "no ResendRequest emitted + peer's 34=2 accepted" assertion is the
// parent harness's golden-diff job (the proxy capture shows the absence of a 35=2
// ResendRequest in the fixpp→peer stream); witness (b)/next_inbound==2 is its in-process
// proxy (next_inbound==2 ⇔ no ResendRequest was issued for the seqnum below the reset point).
//
// Value-parameterized over counterparty ∈ {quickfix-cpp, quickfix-j} (acceptor role —
// the SC-001 subject; the initiator received-141 arm (FR-009) is witnessed in-process
// in test_reset_on_lifecycle.cpp and may grow a live cell under the Item-1 effort).
//
// LIVE CELL: requires a counterparty. INTEROP_REQUIRE_COUNTERPARTY skips with reason
// when the counterparty port env is absent (FR-023). Never a silent pass. The golden
// capture (RR-<cp>-acc-fix44-received-reset.fix) is performed by the parent harness at
// the first paired live run (the Item-1 live-interop golden-capture effort); absent →
// skip:golden-not-yet-captured (diff_golden_or_skip convention).
//
// Parent harness MUST: configure its counterparty INITIATOR with ResetOnLogon=Y (QFcpp
// `ResetOnLogon=Y` / QFJ `ResetOnLogon=Y`) so it sends `Logon(141=Y, 34=1)` then a
// subsequent admin/app message at 34=2; set INTEROP_FIXPP_PORT (or let OS-assign +
// relay via Engine::acceptor_bound_endpoint()). fixpp's OWN reset_on_logon stays OFF.
//
// Anchors: 030 tasks.md T028; spec.md SC-001/FR-001/FR-002; B-030-1;
//          [[project_live_quickfix_hostile_scenarios]] (Item-1 live golden-capture).
//
// spec_ref [FIX-SL §4.4.2 Using ResetSeqNumFlag(141) for 24-hour connectivity].
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <variant>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

// ── 030 T028 — received-141 acceptor (knob OFF) — SC-001 ──────────────────────
//
// fixpp ACCEPTOR with reset_on_logon=FALSE (default). A live QFcpp/QFJ INITIATOR
// configured with ResetOnLogon=Y sends Logon(141=Y, 34=1). fixpp takes the 013-only
// received-141 path; under 030 next-expected-inbound nets 2 and no spurious
// ResendRequest is emitted for the peer's subsequent 34=2 message.

class ReceivedResetAcceptor : public ::testing::TestWithParam<Counterparty> {};

TEST_P(ReceivedResetAcceptor, Received141AdvancesInboundToTwoNoResend) {
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

    const auto endpoint = hp::cell_endpoint(counterparty, Role::fixpp_acceptor);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    fixpp::interop::InteropEngineFixture fx;
    auto cfg = hp::make_session_config(Role::fixpp_acceptor, "FIX.4.4", factory,
                                       fx.ioc().get_executor(), *endpoint);
    // 030 received-141 path: fixpp's OWN reset_on_logon stays OFF (default). The peer
    // (QF initiator with ResetOnLogon=Y) drives the 141=Y; bilateral_lenient (the
    // make_session_config default) accepts the peer's 141=Y without requiring fixpp
    // to send it. This is the knob-OFF received-141 arm 030 corrects (NOT the 024
    // knob-driven arm — see T017 for that).
    ASSERT_FALSE(cfg.reset_on_logon)
        << "received-141 cell requires fixpp reset_on_logon=false (013-only path)";

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── In-process witness (a): FSM reaches Active — no disconnect on 141=Y+34=1 ──
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "acceptor did not reach Active on peer received-141 Logon(141=Y,34=1) "
        << "(reset_on_logon=false / 013-only path); counterparty="
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    // ── In-process witness (b'): received-141 reset path actually ran ─────────────
    // recent_events() exposes the in-process SessionEvent ring directly — NO
    // Application / event sink needed (emit_event writes the ring unconditionally,
    // session.cpp:204). The received-141 acceptor arm emits
    // session_event_sequence_numbers_reset{by_peer_request=true} ONLY when the peer
    // sent 141=Y (session.cpp:1969, reset_on_logon=false here). A plain non-reset
    // Logon emits NO such event. This discriminates received-141 from a plain Logon
    // (which would also reach Active with next_inbound==2..3 under a harness
    // misconfiguration). Idiom mirrors test_reset_seqnum_policy_matrix.cpp:653-662.
    {
        bool reset_event_seen = false;
        for (const auto& ev : s->recent_events()) {
            if (auto* r =
                    std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
                EXPECT_TRUE(r->by_peer_request)
                    << "received-141 cell: by_peer_request must be true (peer sent 141=Y)";
                reset_event_seen = true;
            }
        }
        EXPECT_TRUE(reset_event_seen)
            << "no session_event_sequence_numbers_reset emitted — fixpp did NOT take the "
               "received-141 reset path; a plain non-reset Logon would not emit this event, "
               "so the in-process witnesses (a)/(c)/next_inbound==2/==3 are non-discriminating "
               "without it";
    }

    // ── In-process witness (c): outbound advanced past the reply Logon ───────────
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the reply Logon";

    // ── In-process witness (b): inbound seqnum == 2 — the 030 correction (SC-001) ─
    // The peer's seq-1 reset Logon was consumed; after the received-141 reset, 030
    // restores next-expected-inbound to 2 (QuickFIX reset-then-increment parity).
    // Pre-030 this read 1 → the peer's next message at 34=2 read too-high → a spurious
    // ResendRequest. next_inbound==2 in-process witnesses that no such ResendRequest
    // was issued (the live golden confirms the wire stream end-to-end).
    EXPECT_EQ(s->seqnum_mgr_test_access().next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "next_inbound must be 2 after the received-141 Logon (030 FR-001/SC-001): the "
           "consumed seq-1 reset Logon advances next-expected-inbound to 2. If 1, the 030 "
           "fix regressed and a spurious ResendRequest would be emitted for the peer's 34=2.";

    // ── In-process witness (d): peer's post-reset 34=2 accepted in-sequence ─────
    // After reaching Active with next_inbound==2 (witness b), wait for the peer's
    // post-logon message at 34=2 to be accepted: next_inbound reaches 3.
    // This is a harm-repro guard against the pre-030 regression: the bug clobbered
    // next_inbound back to 1 after the received-141 reset, so the peer's 34=2 read
    // too-high → a spurious ResendRequest was emitted, blocking next_inbound at 2.
    // Reaching 3 proves the peer's 34=2 was accepted in-sequence (no spurious
    // ResendRequest fired). NOTE: next_inbound==3 does NOT by itself discriminate
    // received-141 from a plain non-reset Logon (both paths accept the peer's 34=2
    // in-sequence) — that discrimination is the job of witness (b') above.
    // (The parent harness configures the peer initiator with ResetOnLogon=Y, so the
    // peer sends 141=Y + a subsequent admin message at 34=2.)
    const bool got_seq2 = fx.run_until(
        [&]{ auto s2 = fx.engine().lookup(id);
             return s2 && s2->seqnum_mgr_test_access().next_inbound_unsafe()
                             == fixpp::session::seqnum_t{3}; },
        std::chrono::milliseconds{5000});
    EXPECT_TRUE(got_seq2)
        << "peer's post-reset 34=2 not accepted in-sequence (next_inbound did not reach 3) — "
           "the received-141 arm + no-spurious-ResendRequest harm-repro is unproven in-process";

    // The golden assertion (received-141 exchange, no fixpp ResendRequest, peer 34=2
    // accepted, admin profile {52,10}) is performed by the parent gate against the
    // proxy capture. Golden file: happy/golden/RR-<cp>-acc-fix44-received-reset.fix
    const std::string cp_part = (counterparty == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    const std::string cell_id = "RR-" + cp_part + "-acc-fix44-received-reset";
    hp::diff_golden_or_skip(cell_id, hp::admin_golden_path(cell_id));

    // ── Graceful stop (Logout) ────────────────────────────────────────────────
    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, ReceivedResetAcceptor,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    [](const ::testing::TestParamInfo<Counterparty>& info) {
        return (info.param == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    });

}  // namespace
