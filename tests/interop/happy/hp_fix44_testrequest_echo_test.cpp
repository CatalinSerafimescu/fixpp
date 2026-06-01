// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_testrequest_echo_test.cpp - 016 T011 [US1].
//
// Happy-path interop cell:
//   Logon -> idle Heartbeat -> TestRequest echo, over TLS, FIX 4.4.
//
// What this driver asserts IN-PROCESS (FR-007): fixpp reaches Active on Logon,
// advances the outbound seqnum past the Logon, and graceful stop completes
// within a watchdog bound. The TestRequest/Heartbeat echo is golden-diffed by
// the parent against HP-*-testrequest-echo.fix from the passthrough-proxy
// capture; the driver does not inject counterparty-side messages.
//
// spec_ref [FIX-SL §4.5.1 Heartbeat / §4.5.5 TestRequest].
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <tuple>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

class HappyTestRequestEcho
    : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(HappyTestRequestEcho, TestRequestEcho) {
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
    auto cfg = hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(),
                                       *endpoint);
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

    // The parent gate asserts the TestRequest(35=1) -> Heartbeat(35=0,112=...)
    // echo by golden-diffing the proxy capture against HP-*-testrequest-echo.fix.
    const auto stop_elapsed = fx.stop_within(3s);
    EXPECT_LT(stop_elapsed, 3s)
        << "Engine::stop() (graceful Logout) exceeded the watchdog: "
        << stop_elapsed.count() << " ms";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

std::string cell_name(const ::testing::TestParamInfo<std::tuple<Counterparty, Role>>& info) {
    const auto [cp, role] = info.param;
    std::string n = (cp == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    n += (role == Role::fixpp_initiator) ? "_init" : "_acc";
    return n;
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappyTestRequestEcho,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    cell_name);

}  // namespace
