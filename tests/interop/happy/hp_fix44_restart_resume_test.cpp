// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_restart_resume_test.cpp — 029 T002 [Setup] / T014 [Polish]
//
// Live restart-resume interop cells — both roles (W10 / SC-001 / SC-002 / SC-004).
//
// T014 (Polish): both-role live cells: restart a fixpp initiator (and separately
//   an acceptor) mid-session vs a running QFcpp/QFJ peer; resumes both counters
//   from the persisted FileStore; peer-ahead inbound recovers via ResendRequest
//   (enable_next_expected_msg_seq_num=true on the fixpp side so behind-side
//   tolerance applies — the knob-off Logon gate has no ResendRequest arm and
//   would fatal+reconnect, per SC-004 / W5 / L-029-1); no fatal too-low/too-high.
//
//   Cell group: RestartResume_Initiator (fixpp-INITIATOR restarts, QFcpp/QFJ acceptor)
//     Acceptance (counterparty-PRESENT):
//       (a) Session reaches Active after restart.
//       (b) Outbound seqnum resumes from the persisted value (> 1, not reset to 1).
//       (c) Inbound seqnum resumes from the persisted value (> 1, not reset to 1).
//       (d) No fatal disconnect on the Logon gate (both directions): Active is
//           reached without an intervening Disconnected — witnessable because
//           drive_to_active() returns Active (not Disconnected / LogonSent).
//
//   Cell group: RestartResume_Acceptor (fixpp-ACCEPTOR restarts, QFcpp/QFJ initiator)
//     Acceptance (counterparty-PRESENT): same four assertions as above.
//
// Knob state: enable_next_expected_msg_seq_num=true on the fixpp side so the
// Logon gate's behind-side tolerance (789) covers the peer-ahead inbound recovery
// via ResendRequest rather than fatal+reconnect (SC-004, W5, L-029-1).
//
// This is the "post-restart" leg of the interop matrix.  The pre-restart session
// that advance seqnums is a parent-repo harness orchestration concern (cross-repo
// follow-up, outside this submodule).  Locally — without a counterparty — these
// cells SKIP via INTEROP_REQUIRE_COUNTERPARTY.  The four acceptance assertions are
// present and substantive: they fire only on the counterparty-PRESENT path.
//
// LIVE CELLS: require a counterparty. INTEROP_REQUIRE_COUNTERPARTY skips with
// reason when the counterparty port env is absent (FR-023). Never a silent pass.
//
// Parent harness MUST configure the counterparty with (cross-repo follow-up):
//   QFcpp: EnableNextExpectedMsgSeqNum=Y + PersistMessages=Y in the session config
//   QFJ:   EnableNextExpectedMsgSeqNum=Y + PersistMessages=Y in the session settings
// AND must arrange a prior session run (to advance seqnums) before this cell
// connects as a restart. The pre-restart seqnum setup is a parent-repo harness
// orchestration concern (cross-repo, outside this submodule).
//
// Anchors: tasks.md T002 (Setup skeleton), T014 (Polish full); contracts/seqnum-hydrate.md
//          C2/C3; spec.md SC-001/SC-002/SC-004; data-model.md W10; research.md D-1/D-9.
//          plan.md §VII.6 (interop acceptance criteria).
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <string>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;

namespace {

// ── Watchdog ─────────────────────────────────────────────────────────────────
constexpr std::chrono::milliseconds kStopWatchdog{5000};

// ── The "resumed" lower bound.  The parent harness must have advanced the fixpp
//    side's seqnums to at least kMinResumedSeqnum during the pre-restart session
//    so the post-restart counters are observably > 1.
constexpr seqnum_t kMinResumedSeqnum{2};

// ── Local param-name formatters (alphanumeric+underscore only) ───────────────
// GoogleTest rejects names containing dashes (e.g. "quickfix-cpp").
// Mirrors hp_fix44_next_expected_test.cpp's local pattern.

std::string restart_resume_initiator_name(const ::testing::TestParamInfo<Counterparty>& info) {
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_init" : "QFj_init";
}

std::string restart_resume_acceptor_name(const ::testing::TestParamInfo<Counterparty>& info) {
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_acc" : "QFj_acc";
}

// ── Cell 1: RestartResume_Initiator ──────────────────────────────────────────
//
// fixpp INITIATOR with a persistent FileStore restarts and connects to a live
// counterparty acceptor; both counters resume from the persisted store.
//
// Acceptance assertions (counterparty-PRESENT path):
//   (a) FSM reaches Active.
//   (b) Outbound seqnum > 1 (resumed, not reset to 1) — Logon was sent at
//       seqnum N>1 from the persisted store (SC-001).
//   (c) Inbound seqnum > 1 (resumed, not reset to 1) — the pre-restart Logon
//       and any admin frames were durably tracked; the hydrated value reflects
//       the persisted store (SC-002).
//   (d) Active is reached without a fatal disconnect on the Logon gate:
//       if the peer-ahead inbound seqnum (peer's side advanced more than ours)
//       would have fataled the Logon gate WITHOUT the 789 knob, the knob-on
//       ResendRequest recovery path is EXPECTED here and the session still
//       reaches Active (SC-004 / W5).  drive_to_active() returns Active
//       (not Disconnected) — this directly witnesses no fatal.
//
// Knob state: enable_next_expected_msg_seq_num=true — required so the behind-side
// tolerance (789) covers the peer-ahead inbound and admits ResendRequest recovery
// instead of a fatal+reconnect (L-029-1).

class RestartResume_Initiator : public ::testing::TestWithParam<Counterparty> {};

TEST_P(RestartResume_Initiator, BothCountersResumeFromStore) {
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023).
    // The skip guard covers ONLY the counterparty-ABSENT path; the four
    // acceptance assertions below fire when the peer is up.
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

    // Knob: enable_next_expected_msg_seq_num=true so the Logon-gate behind-side
    // tolerance (789) covers the peer-ahead inbound recovery via ResendRequest
    // rather than fatal+reconnect (SC-004 / W5 / L-029-1).  The knob-off Logon
    // gate has NO ResendRequest arm and would fatal+reconnect instead.
    cfg.enable_next_expected_msg_seq_num = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // Witness (a): FSM reaches Active after the restart.
    // Also witnesses (d) indirectly: drive_to_active returns Active (not
    // Disconnected), so the Logon gate did NOT fatally disconnect.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active after restart against "
        << hp::counterparty_token(counterparty)
        << "; knob on (789 behind-side tolerance), counterparty must have"
           " EnableNextExpectedMsgSeqNum=Y + PersistMessages=Y and a prior seqnum run";

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    auto& mgr = s->seqnum_mgr_test_access();

    // Witness (b): outbound seqnum resumed from the persisted store — > 1.
    // The pre-restart session advanced the fixpp outbound counter; the hydrate-
    // on-open path loaded that value into the manager before the Logon was sent.
    // If the counter were reset to 1 (no hydrate), this assertion would fail.
    EXPECT_GE(mgr.peek_outbound(), kMinResumedSeqnum)
        << "outbound seqnum was not resumed from the persisted store (got " << mgr.peek_outbound()
        << "); expected >= " << kMinResumedSeqnum
        << " — hydrate-on-open must have loaded the persisted outbound counter";

    // Witness (c): inbound seqnum resumed from the persisted store — > 1.
    // The pre-restart session durably tracked received messages via
    // persist_inbound_advance_(); the hydrate-on-open path loaded that value.
    // If the inbound counter were not durably tracked and resumed, next_inbound
    // would still be 1 after restart.
    EXPECT_GE(mgr.next_inbound_unsafe(), kMinResumedSeqnum)
        << "inbound seqnum was not resumed from the persisted store (got "
        << mgr.next_inbound_unsafe() << "); expected >= " << kMinResumedSeqnum
        << " — durable inbound tracking + hydrate-on-open must have loaded"
           " the persisted inbound counter";

    // Graceful stop: Logout + disconnect within the watchdog.
    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog) << "Engine::stop() took " << elapsed.count()
                                      << " ms (watchdog " << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(AllCounterparties, RestartResume_Initiator,
                         ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                         restart_resume_initiator_name);

// ── Cell 2: RestartResume_Acceptor ────────────────────────────────────────────
//
// fixpp ACCEPTOR with a persistent FileStore restarts; the counterparty INITIATOR
// reconnects; both counters resume from the persisted store.
//
// Acceptance assertions (counterparty-PRESENT path): same four assertions as Cell 1.
//   (a) FSM reaches Active after restart.
//   (b) Outbound seqnum > 1 (resumed, not reset).
//   (c) Inbound seqnum > 1 (resumed, not reset).
//   (d) Active is reached without a fatal disconnect on the Logon gate.
//
// Knob state: enable_next_expected_msg_seq_num=true — same reasoning as Cell 1.
// The acceptor's inbound Logon from the peer will carry a seqnum reflecting the
// peer's prior outbound counter; the hydrated inbound on the fixpp side must be
// admitted without a too-high fatal.  789 advertised by both sides provides the
// behind-side tolerance for any mismatch.

class RestartResume_Acceptor : public ::testing::TestWithParam<Counterparty> {};

TEST_P(RestartResume_Acceptor, BothCountersResumeFromStore) {
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

    // Knob: enable_next_expected_msg_seq_num=true so the Logon-gate behind-side
    // tolerance (789) covers the peer-ahead inbound recovery via ResendRequest
    // rather than fatal+reconnect (SC-004 / W5 / L-029-1).
    cfg.enable_next_expected_msg_seq_num = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // Witness (a): FSM reaches Active after the acceptor restart.
    // Witness (d): drive_to_active returns Active (not Disconnected) →
    // the Logon gate did NOT fatally disconnect.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active after acceptor restart against "
        << hp::counterparty_token(counterparty)
        << "; knob on (789 behind-side tolerance), counterparty must have"
           " EnableNextExpectedMsgSeqNum=Y + PersistMessages=Y and a prior seqnum run";

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    auto& mgr = s->seqnum_mgr_test_access();

    // Witness (b): outbound seqnum resumed from the persisted store — > 1.
    EXPECT_GE(mgr.peek_outbound(), kMinResumedSeqnum)
        << "outbound seqnum was not resumed from the persisted store (got " << mgr.peek_outbound()
        << "); expected >= " << kMinResumedSeqnum
        << " — hydrate-on-open must have loaded the persisted outbound counter";

    // Witness (c): inbound seqnum resumed from the persisted store — > 1.
    EXPECT_GE(mgr.next_inbound_unsafe(), kMinResumedSeqnum)
        << "inbound seqnum was not resumed from the persisted store (got "
        << mgr.next_inbound_unsafe() << "); expected >= " << kMinResumedSeqnum
        << " — durable inbound tracking + hydrate-on-open must have loaded"
           " the persisted inbound counter";

    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog) << "Engine::stop() took " << elapsed.count()
                                      << " ms (watchdog " << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(AllCounterparties, RestartResume_Acceptor,
                         ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                         restart_resume_acceptor_name);

// ── Cell 3: StandbyRehydrate_Initiator ───────────────────────────────────────
//
// 025-refresh-on-logon standby re-hydrate live cell (T041 Polish).
//
// A fixpp initiator configured as a STANDBY (refresh_on_logon=true,
// bilateral_lenient, persistent FileStore) connects vs a QFcpp/QFJ acceptor
// that has already accumulated seqnums from a prior primary session.  On
// (re)connect, the standby re-reads the store (force=true bypasses the 029
// one-shot hydrated_ latch) and resumes at the primary-advanced counters.
//
// No golden capture: the prior primary run that advanced the store is a
// parent-harness concern (see RestartResume_Initiator header comment); we
// assert behavioral postconditions only.  "Resumed" means both counters are
// >= kMinResumedSeqnum.
//
// INTEROP_REQUIRE_COUNTERPARTY skips cleanly without a counterparty (no hang).
//
// Acceptance assertions (counterparty-PRESENT path):
//   (a) Session reaches Active.
//   (b) Outbound seqnum >= kMinResumedSeqnum (resumed from store, not reset to 1).
//   (c) Inbound seqnum >= kMinResumedSeqnum (resumed from store).
//   (d) Active reached without a fatal disconnect — drive_to_active() returns Active.
//
// Knob state: refresh_on_logon=true + reset_seqnum_policy bilateral_lenient.
//   bilateral_strict suppresses the re-hydrate (INV-RoL-3) so this cell uses
//   lenient.  enable_next_expected_msg_seq_num=true covers the peer-ahead
//   inbound case (same reasoning as RestartResume cells).
//
// Anchors: specs/025-refresh-on-logon/data-model.md W9 (interop);
//          tasks.md T041; data-model.md INV-RoL-3 / INV-RoL-4; [const §VII.6].

class StandbyRehydrate_Initiator : public ::testing::TestWithParam<Counterparty> {};

TEST_P(StandbyRehydrate_Initiator, RehydratesOnReconnect) {
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

    // Knob: refresh_on_logon=true (the 025 knob under test).
    // bilateral_lenient required: bilateral_strict suppresses re-hydrate (INV-RoL-3).
    cfg.refresh_on_logon = true;
    cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    // enable_next_expected_msg_seq_num=true so the 789 behind-side tolerance covers
    // any peer-ahead inbound seqnum after the primary run (SC-004 / W5 / L-029-1).
    cfg.enable_next_expected_msg_seq_num = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // Witness (a): FSM reaches Active after the standby connects.
    // Witness (d): drive_to_active() returns Active (not Disconnected) —
    // the Logon gate did NOT fatally disconnect.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "standby session did not reach Active against "
        << hp::counterparty_token(counterparty)
        << "; refresh_on_logon=true + bilateral_lenient; counterparty must have"
           " a prior seqnum run (primary-advanced store) + PersistMessages=Y";

    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";

    auto& mgr = s->seqnum_mgr_test_access();

    // Witness (b): outbound seqnum resumed from the primary-advanced store — > 1.
    // The refresh_on_logon re-hydrate (force=true) reloaded the store's outbound
    // counter and the Logon was sent at that resumed value.
    EXPECT_GE(mgr.peek_outbound(), kMinResumedSeqnum)
        << "standby outbound seqnum was not resumed from the primary-advanced store "
           "(got " << mgr.peek_outbound() << "); expected >= " << kMinResumedSeqnum
        << " — refresh_on_logon re-hydrate must reload the persisted outbound counter";

    // Witness (c): inbound seqnum resumed from the primary-advanced store — > 1.
    EXPECT_GE(mgr.next_inbound_unsafe(), kMinResumedSeqnum)
        << "standby inbound seqnum was not resumed from the primary-advanced store "
           "(got " << mgr.next_inbound_unsafe() << "); expected >= " << kMinResumedSeqnum
        << " — refresh_on_logon re-hydrate must reload the persisted inbound counter";

    // Graceful stop: Logout + disconnect within the watchdog.
    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog) << "Engine::stop() took " << elapsed.count()
                                      << " ms (watchdog " << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

std::string standby_rehydrate_initiator_name(
    const ::testing::TestParamInfo<Counterparty>& info) {
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_standby" : "QFj_standby";
}

INSTANTIATE_TEST_SUITE_P(AllCounterparties, StandbyRehydrate_Initiator,
                         ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                         standby_rehydrate_initiator_name);

}  // namespace
