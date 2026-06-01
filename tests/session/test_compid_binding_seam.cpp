// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_compid_binding_no_identity_arms.cpp (legacy filename:
//   test_compid_binding_seam.cpp) — 015 T021 [US4] Phase 6.
//
// The two on-list/off-list cells that drove the removed
// SessionConfig peer-identity test seam (013 T036) are DELETED (SC-006/FR-009):
// the live on-list→Active and off-list→fail-CLOSED paths are now proved over a
// real handshake identity by test_live_identity_binding.cpp (initiator) and the
// US1 engine acceptor tests (engine_acceptor_test / engine_acceptor_failclosed).
//
// What remains here are the two gate arms that need NO injected identity:
//   - AbsentIdentityMtlsFailsClosed — mTLS + no live identity → arm (2) fail
//     CLOSED → Disconnected (the RC#A baseline; the safe default when the
//     happens-before attach is absent).
//   - NonMtlsAbsentIdentityPermissiveSkip — one_way_ca → arm (3) permissive
//     skip → Active (backward-compat for non-mTLS sessions).
//
// Both cells assert the actual FSM STATE (not just the event) to prove
// fail-CLOSED (anti-shallow-witness per Phase-3 lesson).
//
// Anchors: FR-007/FR-009; US2 AC3; SC-003/SC-006; data-model E-2; contracts C2.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// FIX frame helper (same pattern as Phase 3 tests)
// ─────────────────────────────────────────────────────────────────────────────
static std::string fix_field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target) {
    std::string body;
    body += fix_field(35, "A");
    body += fix_field(34, std::to_string(seq));
    body += fix_field(49, sender);
    body += fix_field(56, target);
    body += fix_field(98, "0");
    body += fix_field(108, "30");

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// ─────────────────────────────────────────────────────────────────────────────
// MinimalTransportFactory — satisfies SessionConfig::transport_factory_override
// for tests that don't drive the transport layer. No factory calls expected.
// ─────────────────────────────────────────────────────────────────────────────
class MinimalTransportFactory final : public fixpp::transport::TransportFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor /*exec*/, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        return std::unexpected{fixpp::core::error::transport_factory_failed};
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*s*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────
class CompIdBindingNoIdentityTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    fixpp::core::EngineConfig engine{};

    void SetUp() override { engine.executor = ioc.get_executor(); }

    // Build a base SessionConfig with the given policy + TLS profile and a
    // null write-only sink (open() drives the initiator into LogonSent without
    // a real transport). No identity is injected — these cells exercise the
    // no-identity gate arms (fail-CLOSED under mTLS / permissive under non-mTLS).
    fixpp::session::SessionConfig make_cfg(fixpp::session::CompIdAuthorizationPolicy policy,
                                           fixpp::session::SecurityProfile::kind sk) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "INITIATOR";
        cfg.target_comp_id = "ACCEPTOR";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.logout_disconnect_timeout_ms = 2000;
        cfg.role = fixpp::session::session_role::initiator;
        cfg.executor_override = ioc.get_executor();
        cfg.security_profile = fixpp::session::SecurityProfile{sk};
        cfg.compid_authorization_policy = std::move(policy);
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.transport_factory_override = std::make_shared<MinimalTransportFactory>();
        cfg.transport_send = [](std::span<const std::byte>) noexcept {};
        return cfg;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1 — Absent identity under mTLS → fail-closed (arm 2).
//
// No live identity injected; mTLS profile → arm (2) fires unconditionally.
//
// Expected:
//   - Session does NOT reach Active (Disconnected) — arm (2) fail-closed.
//   - session_event_compid_authorization_failed emitted.
//
// This is the RC#A baseline: the safe default when no live handshake identity
// has been attached (the happens-before attach is absent).
// [[feedback_simplify_pass_catches_9th_burn]]
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompIdBindingNoIdentityTest, AbsentIdentityMtlsFailsClosed) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("PEER-PROD-01", "ACCEPTOR");

    auto cfg = make_cfg(std::move(policy), fixpp::session::SecurityProfile::kind::mtls_ca);

    fixpp::session::Session session{engine, cfg};

    {
        auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(open_fut.wait_for(0s), std::future_status::ready);
        auto open_r = open_fut.get();
        ASSERT_TRUE(open_r.has_value())
            << "Session::open() failed: " << static_cast<int>(open_r.error());
    }

    ASSERT_EQ(session.state(), fixpp::session::fsm_state::LogonSent)
        << "Session must be in LogonSent after open().";

    auto logon_ack = make_logon_frame("FIX.4.2", 1, "ACCEPTOR", "INITIATOR");
    {
        auto feed_fut = asio::co_spawn(
            ioc, session.on_inbound_frame(std::span<const std::byte>{logon_ack}), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(feed_fut.wait_for(0s), std::future_status::ready);
        (void)feed_fut.get();
    }

    // Fail-closed: arm (2) mTLS without identity → Disconnected.
    EXPECT_NE(session.state(), fixpp::session::fsm_state::Active)
        << "Absent identity under mTLS must NOT result in Active (fail-closed, arm 2).";
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Disconnected)
        << "Absent identity under mTLS → arm (2) fail-closed → Disconnected. "
        << "RC#A: mTLS + no live peer_identity → fail CLOSED.";

    bool auth_failed_emitted = false;
    for (const auto& ev : session.recent_events()) {
        if (std::holds_alternative<fixpp::session::session_event_compid_authorization_failed>(ev)) {
            auth_failed_emitted = true;
            break;
        }
    }
    EXPECT_TRUE(auth_failed_emitted)
        << "Absent identity under mTLS must emit session_event_compid_authorization_failed.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2 — non-mTLS (one_way_ca) + absent identity → permissive skip → Active.
//
// Verifies the permissive arm (3) is unchanged (backward compat).
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompIdBindingNoIdentityTest, NonMtlsAbsentIdentityPermissiveSkip) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    // Empty policy (default-deny), but one_way_ca so the gate is skipped.

    auto cfg = make_cfg(std::move(policy), fixpp::session::SecurityProfile::kind::one_way_ca);

    fixpp::session::Session session{engine, cfg};

    {
        auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(open_fut.wait_for(0s), std::future_status::ready);
        auto open_r = open_fut.get();
        ASSERT_TRUE(open_r.has_value())
            << "Session::open() failed: " << static_cast<int>(open_r.error());
    }

    ASSERT_EQ(session.state(), fixpp::session::fsm_state::LogonSent)
        << "Session must be in LogonSent after open().";

    auto logon_ack = make_logon_frame("FIX.4.2", 1, "ACCEPTOR", "INITIATOR");
    {
        auto feed_fut = asio::co_spawn(
            ioc, session.on_inbound_frame(std::span<const std::byte>{logon_ack}), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(feed_fut.wait_for(0s), std::future_status::ready);
        (void)feed_fut.get();
    }

    // Non-mTLS permissive skip → Active (arm 3, backward compat).
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Active)
        << "one_way_ca (non-mTLS) + absent identity → permissive skip (arm 3) → Active. "
        << "FR-007: non-mTLS gate is skipped; backward compat for one_way_ca sessions.";
}

}  // namespace
