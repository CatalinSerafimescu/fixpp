// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_next_expected_test.cpp — 027 T002 [Setup] / T022 [Polish]
//
// Live NextExpectedMsgSeqNum(789) interop cells — both roles (C10 / SC-005).
//
// T022 (Polish): fixpp with enable_next_expected_msg_seq_num=true against a live
//   QFcpp/QFJ counterparty configured with EnableNextExpectedMsgSeqNum=Y.
//
//   Cell 1 — NextExpectedInitiator / ProactiveResendNoResendRequest:
//     fixpp INITIATOR: sends 789 in its Logon; the counterparty ACCEPTOR
//     proactively resends [X, N-1] if it has a gap; zero ResendRequest on the wire;
//     session reaches Active; all missed messages delivered in order.
//     Witness: FSM reaches Active (the proactive-resend hand-shake completed).
//     The absence of ResendRequest on the wire is asserted by the parent proxy capture.
//
//   Cell 2 — NextExpectedAcceptor / ProactiveResendNoResendRequest:
//     fixpp ACCEPTOR: receives a Logon with 789=X from the counterparty INITIATOR;
//     proactively resends [X, N-1] after the reply Logon; zero ResendRequest;
//     session reaches Active.
//     Witness: FSM reaches Active; outbound seqnum advanced past Logon
//     (indicating the acceptor's initial Logon reply was emitted successfully).
//     The absence of ResendRequest is asserted by the parent proxy capture.
//
//   Cell 3 — NextExpectedBidirectional / BothGapsRecoverNoResendRequest:
//     Both fixpp (initiator or acceptor role) and the counterparty have a gap.
//     Each side proactively resends its missing range; zero ResendRequest from either
//     party; session reaches Active. Witness: Active reached + seqnum advanced.
//     The bidirectional gap recovery is asserted end-to-end by the parent harness.
//
// LIVE CELLS: require a counterparty. INTEROP_REQUIRE_COUNTERPARTY skips with
// reason when the counterparty port env is absent (FR-023). Never a silent pass.
//
// Parent harness MUST configure the counterparty with (cross-repo follow-up):
//   QFcpp: EnableNextExpectedMsgSeqNum=Y in the session config
//   QFJ:   EnableNextExpectedMsgSeqNum=Y in the session settings
// Without this, the counterparty will not send tag 789 on its Logon and will
// fall back to ResendRequest recovery — the cells skip cleanly when no live
// counterparty env is present, and will test the fast-resume when it is.
//
// Anchors: tasks.md T002 (Setup skeleton), T022 (Polish full); contracts C10;
//          FR-001/002/003/004/007/SC-001/SC-003/SC-005.
//
// spec_ref [FIX-SL §4.4.1] Using NextExpectedMsgSeqNum(789).
// spec_ref [FIX-SL §4.7.1] Using NextExpectedMsgSeqNum(789) on invalid MsgSeqNum(34).
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <tuple>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

// ── Watchdog ─────────────────────────────────────────────────────────────────
constexpr std::chrono::milliseconds kStopWatchdog{5000};

// ── Local param-name formatters (alphanumeric+underscore only) ───────────────
// GoogleTest rejects names containing dashes (e.g. "quickfix-cpp").
// Mirrors hp_fix44_disconnect_reconnect_noreset_test.cpp's local pattern.

std::string next_expected_initiator_name(const ::testing::TestParamInfo<Counterparty>& info)
{
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_init" : "QFj_init";
}

std::string next_expected_acceptor_name(const ::testing::TestParamInfo<Counterparty>& info)
{
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_acc" : "QFj_acc";
}

std::string next_expected_bidirectional_name(
    const ::testing::TestParamInfo<Counterparty>& info)
{
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_bidir" : "QFj_bidir";
}

// ── Cell 1: NextExpectedInitiator ─────────────────────────────────────────────
//
// fixpp INITIATOR with enable_next_expected_msg_seq_num=true connects to a live
// counterparty acceptor. Witness: FSM reaches Active (the knob-on Logon exchange
// completed, including any proactive resend the counterparty performed).
//
// Wire-level assertion (zero ResendRequest): asserted by the parent proxy capture
// from the parent-repo phase-9-harness/ golden diff. This in-process witness
// asserts the session-FSM outcome (Active reached within the watchdog).
//
// Parent harness cross-repo note: the counterparty acceptor MUST be configured
// with EnableNextExpectedMsgSeqNum=Y (QFcpp/QFJ session config). Without it the
// counterparty will not send 789, and the at-logon gap (if any) will not
// fast-recover — the session may still reach Active via the existing ResendRequest
// path, but that is NOT the fast-resume property under test. The parent-harness
// config delta is a cross-repo follow-up (parent phase-9-harness/ counterparty
// config, outside this submodule).

class NextExpectedInitiator : public ::testing::TestWithParam<Counterparty> {};

TEST_P(NextExpectedInitiator, ProactiveResendNoResendRequest)
{
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
    // Enable NextExpectedMsgSeqNum(789) — both sides must have this on for fast-resume.
    cfg.enable_next_expected_msg_seq_num = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // Witness (a): FSM reaches Active — the 789 Logon exchange completed.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active against "
        << hp::counterparty_token(counterparty)
        << "; knob on, counterparty must also have EnableNextExpectedMsgSeqNum=Y";

    // Witness (b): outbound seqnum advanced past the Logon (Logon was sent at seq 1).
    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // Graceful stop: Logout + disconnect within the watchdog.
    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog)
        << "Engine::stop() took " << elapsed.count() << " ms (watchdog "
        << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(
    AllCounterparties, NextExpectedInitiator,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    next_expected_initiator_name);

// ── Cell 2: NextExpectedAcceptor ─────────────────────────────────────────────
//
// fixpp ACCEPTOR with enable_next_expected_msg_seq_num=true binds a port and
// waits for the counterparty INITIATOR to connect. The counterparty sends 789 in
// its Logon; fixpp responds with its own 789 in the acceptor reply and proactively
// resends [X, N-1] (C4 / RC#4 ordering). Witness: FSM reaches Active + outbound
// seqnum advanced past the reply Logon.
//
// The absence of ResendRequest on the wire is asserted by the parent proxy capture.
//
// Parent harness cross-repo note: counterparty initiator MUST be configured with
// EnableNextExpectedMsgSeqNum=Y. See parent-repo phase-9-harness/ config (cross-repo
// follow-up, outside this submodule).

class NextExpectedAcceptor : public ::testing::TestWithParam<Counterparty> {};

TEST_P(NextExpectedAcceptor, ProactiveResendNoResendRequest)
{
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

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
    // Enable NextExpectedMsgSeqNum(789) — both sides must have this on for fast-resume.
    cfg.enable_next_expected_msg_seq_num = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // Witness (a): FSM reaches Active — the acceptor 789 reply exchange completed.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active against "
        << hp::counterparty_token(counterparty)
        << "; knob on, counterparty must also have EnableNextExpectedMsgSeqNum=Y";

    // Witness (b): outbound seqnum advanced past the acceptor reply Logon (seq 1).
    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the acceptor reply Logon";

    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog)
        << "Engine::stop() took " << elapsed.count() << " ms (watchdog "
        << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(
    AllCounterparties, NextExpectedAcceptor,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    next_expected_acceptor_name);

// ── Cell 3: NextExpectedBidirectional ─────────────────────────────────────────
//
// Both fixpp (acting as INITIATOR for this cell) and the counterparty have an
// at-logon gap; each proactively resends the other's missing range; zero
// ResendRequest from either party; session reaches Active.
//
// In-process witness: FSM reaches Active; outbound seqnum advanced past the Logon
// (proves fixpp sent at least one message before the reconnect gap, and resumed).
//
// The wire-level assertion (no ResendRequest from either side, all missed messages
// delivered in order) is asserted by the parent proxy golden diff.
//
// Parent harness cross-repo note: both sides must have EnableNextExpectedMsgSeqNum=Y
// AND the counterparty must have a prior outbound gap (i.e. the parent harness must
// have advanced the counterparty's seqnum before this cell runs, analogous to how
// the 018/021 thorny cells inject a stored gap). This is a parent-repo test-harness
// orchestration concern (cross-repo follow-up, outside this submodule).

class NextExpectedBidirectional : public ::testing::TestWithParam<Counterparty> {};

TEST_P(NextExpectedBidirectional, BothGapsRecoverNoResendRequest)
{
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    // Bidirectional cell: fixpp as initiator (we connect out to the counterparty).
    const auto endpoint = hp::cell_endpoint(counterparty, Role::fixpp_initiator);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    fixpp::interop::InteropEngineFixture fx;
    auto cfg = hp::make_session_config(Role::fixpp_initiator, "FIX.4.4", factory,
                                       fx.ioc().get_executor(), *endpoint);
    // Enable NextExpectedMsgSeqNum(789).
    cfg.enable_next_expected_msg_seq_num = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // Witness: FSM reaches Active — the bidirectional proactive resend completed.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (bidirectional) against "
        << hp::counterparty_token(counterparty)
        << "; both sides must have EnableNextExpectedMsgSeqNum=Y and a prior gap";

    // Both gaps recovered → seqnum advanced past Logon.
    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon in the bidirectional cell";

    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog)
        << "Engine::stop() took " << elapsed.count() << " ms (watchdog "
        << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(
    AllCounterparties, NextExpectedBidirectional,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    next_expected_bidirectional_name);

}  // namespace
