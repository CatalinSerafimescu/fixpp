// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_seqnum_recovery_test.cpp - 016 T013 [US1].
//
// Happy-path interop cell:
//   Logon -> higher-numbered peer message -> ResendRequest/GapFill recovery,
//   over TLS, FIX 4.4.
//
// What this driver asserts IN-PROCESS (FR-007): fixpp reaches Active on Logon,
// advances the outbound seqnum past the Logon, remains Active after a bounded
// recovery window, and graceful stop completes within a watchdog bound. The gap
// is injected counterparty-side; fixpp's ResendRequest/SequenceReset-GapFill
// exchange is golden-diffed by the parent against the proxy capture.
//
// spec_ref [FIX-SL §4.5.3 / §4.8.1 / §4.8.2 / §4.8.5].
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

class HappySeqnumRecovery
    : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

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

    // The parent gate asserts the counterparty-injected gap and fixpp's
    // ResendRequest/SequenceReset-GapFill exchange from the proxy golden diff.
    fx.run_until(
        [&] {
            const fixpp::session::Session* current = fx.engine().lookup(id);
            return current == nullptr || current->state() != fsm_state::Active;
        },
        1500ms);
    s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session disappeared during seqnum recovery window";
    EXPECT_EQ(s->state(), fsm_state::Active)
        << "session did not remain Active after seqnum recovery window";

    const auto stop_elapsed = fx.stop_within(3s);
    EXPECT_LT(stop_elapsed, 3s)
        << "Engine::stop() (graceful Logout) exceeded the watchdog: "
        << stop_elapsed.count() << " ms";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappySeqnumRecovery,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
