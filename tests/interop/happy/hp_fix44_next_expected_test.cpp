// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_next_expected_test.cpp — 027 T002 [Setup] / T022 [Polish]
//
// Live NextExpectedMsgSeqNum(789) interop cells — both roles (C10 / SC-005).
//
// T002 (Setup): skeleton with skip-without-counterparty guard. Body is a stub
//   for now; the full live implementation is filled in by Polish T022.
//
// T022 (Polish, to be filled later):
//   fixpp with enable_next_expected_msg_seq_num=true against a live
//   QFcpp/QFJ counterparty configured with EnableNextExpectedMsgSeqNum=Y.
//
//   Cell 1 — next_expected_initiator:
//     fixpp INITIATOR: sends 789 in its Logon; the counterparty ACCEPTOR
//     proactively resends [X, N-1] if it has a gap; zero ResendRequest on the wire;
//     session reaches Active; all missed messages delivered in order.
//
//   Cell 2 — next_expected_acceptor:
//     fixpp ACCEPTOR: receives a Logon with 789=X from the counterparty INITIATOR;
//     proactively resends [X, N-1] after the reply Logon; zero ResendRequest;
//     session reaches Active.
//
//   Cell 3 — next_expected_bidirectional (optional / T022 deliverable):
//     Both sides have a gap; each proactively resends its missing range;
//     zero ResendRequest, both sides deliver all missed messages.
//
// LIVE CELLS: require a counterparty. INTEROP_REQUIRE_COUNTERPARTY skips with
// reason when the counterparty port env is absent (FR-023). Never a silent pass.
//
// Parent harness for T022 MUST configure the counterparty with:
//   QFcpp: EnableNextExpectedMsgSeqNum=Y in the session config
//   QFJ:   EnableNextExpectedMsgSeqNum=Y in the session config
//
// Anchors: tasks.md T002 (Setup skeleton), T022 (Polish full); contracts C10;
//          FR-001/002/003/004/007/SC-001/SC-003/SC-005.
//
// spec_ref [FIX-SL §4.5 / §4.5.1].
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

// ── T002 (Setup skeleton) + T022 (Polish implementation) ─────────────────────
//
// NextExpectedMsgSeqNum_Initiator: fixpp INITIATOR with
//   enable_next_expected_msg_seq_num=true connects to a live counterparty
//   acceptor configured with EnableNextExpectedMsgSeqNum=Y.
//
// T002 skeleton: skips when no counterparty (FR-023). Body filled by T022.

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
    // ── T022 (Polish): set enable_next_expected_msg_seq_num knob ──────────────
    // (Uncommented by T022 when the config field is wired in T003/T013.)
    // cfg.enable_next_expected_msg_seq_num = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // ── T022 witness (a): FSM reaches Active ──────────────────────────────────
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active against "
        << hp::counterparty_token(counterparty);

    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    AllCounterparties, NextExpectedInitiator,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    next_expected_initiator_name);

// ── NextExpectedAcceptor: fixpp ACCEPTOR ─────────────────────────────────────

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
    // ── T022 (Polish): set enable_next_expected_msg_seq_num knob ──────────────
    // (Uncommented by T022 when the config field is wired in T003/T013.)
    // cfg.enable_next_expected_msg_seq_num = true;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active against "
        << hp::counterparty_token(counterparty);

    hp::expect_graceful_stop(fx);
}

INSTANTIATE_TEST_SUITE_P(
    AllCounterparties, NextExpectedAcceptor,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    next_expected_acceptor_name);

}  // namespace
