// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_disconnect_reconnect_noreset_test.cpp - 016 T014 [US1].
//
// Happy-path interop cell:
//   Logon -> abrupt disconnect -> reconnect with ResetSeqNumFlag=N and sequence
//   continuity preserved, over TLS, FIX 4.4.
//
// What this driver asserts IN-PROCESS (FR-007): fixpp reaches Active on Logon,
// advances the outbound seqnum past the Logon, and graceful stop completes
// within a watchdog bound. The abrupt disconnect, reconnect continuity, and
// absence of a spurious reset are asserted by the parent gate from the proxy
// capture and golden diff. Reconnect is an initiator concern, so this cell fixes
// Role::fixpp_initiator and parameterizes only over the counterparty.
//
// spec_ref [FIX-SL §4.4.2 / §4.4.3 / §4.8.6].
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/reconnect_policy.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

constexpr std::chrono::milliseconds kStopWatchdog{3000};

class HappyDisconnectReconnectNoReset : public ::testing::TestWithParam<Counterparty> {};

TEST_P(HappyDisconnectReconnectNoReset, ReconnectPreservesSequenceContinuity) {
    const auto counterparty = GetParam();
    constexpr Role role = Role::fixpp_initiator;
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
    auto cfg = hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(),
                                       *endpoint);
    auto policy = fixpp::transport::ReconnectPolicy::defaults_quickfix_compat(nullptr);
    policy.max_attempts = 5;
    cfg.reconnect_policy = policy;

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon) against "
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    fixpp::session::Session* s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // The parent gate asserts the abrupt disconnect, reconnect Logon with
    // ResetSeqNumFlag=N, continuity of sequence numbers, and no spurious reset
    // from the proxy capture and golden diff.
    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog)
        << "Engine::stop() (graceful Logout) took " << elapsed.count()
        << " ms (watchdog " << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

std::string cell_name(const ::testing::TestParamInfo<Counterparty>& info) {
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_init" : "QFj_init";
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappyDisconnectReconnectNoReset,
    ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
    cell_name);

}  // namespace
