// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_logon_hb_logout_test.cpp — 016 T010 [US1] (smoke).
//
// The MVP happy-path interop cell + the canonical driver template for US1:
//   Logon → idle Heartbeat → Logout, over TLS, FIX 4.4.
//
// Smoke cell (FR-022): HP-QFcpp-init-fix44-logon-hb-logout (quickfix-cpp,
// fixpp-initiator). Value-parameterized over (counterparty ∈ {quickfix-cpp,
// quickfix-j}) × (role ∈ {fixpp-initiator, fixpp-acceptor}) so this one file
// covers the 4 live cells of the chain.
//
// What this driver asserts IN-PROCESS (FR-007): fixpp's session FSM end-state
// (reaches Active on logon; clean stopped() after Logout) and the outbound
// seqnum delta. The byte-level golden match (FR-006) and the wire-observed
// counterparty terminal behavior are asserted by the PARENT gate against the
// passthrough-proxy capture (research R1: no in-library wire capture) — the
// golden HP-*.fix file is the checked-in artifact the parent diffs against.
//
// Counterparty-required (FR-023): absent ⇒ GTEST_SKIP with reason, never a
// silent pass. The QuickFIX counterparty must run as an SSL acceptor/initiator
// with a cert the `one_way_ca` baseline trusts (parent-harness config).
//
// spec_ref [FIX-SL §4.3 Logon / §4.5.1 Heartbeat / §4.6 Logout].
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

class HappyLogonHbLogout
    : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(HappyLogonHbLogout, LogonHeartbeatLogout) {
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023). For an
    // acceptor cell the counterparty is an initiator that connects to us; the
    // probe still requires the parent to have signalled availability via the
    // counterparty port env (the parent leases it either way).
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

    // For a fixpp-acceptor cell, the parent's counterparty-initiator connects to
    // fixpp's bound port. When the port is OS-assigned (INTEROP_FIXPP_PORT unset)
    // the parent must read it via Engine::acceptor_bound_endpoint() and relay it
    // to its initiator (rendezvous — see MATRIX.md). The drive below waits for
    // the resulting Logon either way.

    // ── Logon: drive to Active ───────────────────────────────────────────────
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active (logon) against "
        << hp::counterparty_token(counterparty)
        << "; reached state=" << static_cast<int>(reached);

    // ── Seqnum delta (FR-007): outbound advanced past the Logon ──────────────
    auto s = fx.engine().lookup(id);
    ASSERT_NE(s, nullptr) << "session not established";
    EXPECT_GT(s->seqnum_mgr_test_access().peek_outbound(), fixpp::session::seqnum_t{1})
        << "outbound seqnum did not advance past the Logon";

    // ── Logout: graceful stop emits Logout + closes; bounded (FR-004) ────────
    hp::expect_graceful_stop(fx);
    // Counterparty terminal behavior (received Logout(35=5) / orderly close) is
    // asserted by the parent gate from the proxy capture (FR-007), not probed here.
}

INSTANTIATE_TEST_SUITE_P(
    Fix44, HappyLogonHbLogout,
    ::testing::Combine(::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                       ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    fixpp::interop::hp::cell_name);

}  // namespace
