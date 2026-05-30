// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_compid_binding_seam.cpp — T012 [P] [US2] Phase 4
//
// Drive the logon_peer_identity_override seam (session_config.hpp:224):
//   - On-list identity → session reaches Active.
//   - Off-list identity → fail-closed (NOT Active, emits
//       session_compid_unauthorized + session_event_compid_authorization_failed).
//   - Absent identity under mTLS → fail-closed (same as off-list).
//
// The inherited 013 extraction order and event shapes are unchanged (FR-007).
// This is a PURE-SEAM test: it tests the existing three-way guard (already
// present in session.cpp) via the logon_peer_identity_override seam — it
// does NOT require T014/T015 to be implemented to go GREEN.
//
// Specifically:
//   - Cell: OnListIdentityAdmitsToActive — uses logon_peer_identity_override
//     with a CN that is on the policy → Active. (GREEN today; stays GREEN.)
//   - Cell: OffListIdentityFailsClosed — uses logon_peer_identity_override
//     with a CN that is NOT on the policy → NOT Active, emits auth-failed
//     event + session_compid_unauthorized. (GREEN today; stays GREEN.)
//   - Cell: AbsentIdentityMtlsFailsClosed — NO override, mTLS profile →
//     arm (2) fail-closed → NOT Active, emits auth-failed event.
//     (GREEN today — RC#A already implements this.)
//
// All cells assert the actual FSM STATE (not just the event) to prove
// fail-CLOSED (anti-shallow-witness per Phase-3 lesson).
//
// Anchors: FR-007; US2 AC2/AC3; SC-003; data-model E-2; contracts C2.
// [[feedback_simplify_pass_catches_9th_burn]]: assert mutated state (not Active),
// not just event emission.

#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// FIX frame helper (same pattern as Phase 3 tests)
// ─────────────────────────────────────────────────────────────────────────────
static std::string fix_field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_logon_frame(
    std::string_view begin_string,
    std::uint32_t seq,
    std::string_view sender,
    std::string_view target)
{
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
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>>
    make(asio::any_io_executor /*exec*/,
         fixpp::tls::SslCtxConfig /*ssl_cfg*/,
         std::pmr::memory_resource* /*mr*/) noexcept override
    {
        return std::unexpected{fixpp::core::error::transport_factory_failed};
    }

    [[nodiscard]] fixpp::core::expected_t<void>
    reload_credentials(std::shared_ptr<fixpp::tls::cert_source> /*s*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source>
    cert_source_snapshot() const noexcept override { return nullptr; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────
class CompIdBindingSeamTest : public ::testing::Test {
protected:
    asio::io_context          ioc;
    fixpp::core::EngineConfig engine{};
    MinimalTransportFactory   factory;

    void SetUp() override {
        engine.executor = ioc.get_executor();
    }

    // Build a base SessionConfig with an mTLS profile and the given policy.
    fixpp::session::SessionConfig make_cfg(
        fixpp::session::CompIdAuthorizationPolicy policy,
        std::optional<fixpp::tls::peer_identity>  override_id = std::nullopt,
        fixpp::session::SecurityProfile::kind sk =
            fixpp::session::SecurityProfile::kind::mtls_ca)
    {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id  = "INITIATOR";
        cfg.target_comp_id  = "ACCEPTOR";
        cfg.begin_string    = "FIX.4.2";
        cfg.heartbeat_interval     = std::chrono::seconds{30};
        cfg.logout_disconnect_timeout_ms = 2000;
        cfg.role            = fixpp::session::session_role::initiator;
        cfg.executor_override       = ioc.get_executor();
        cfg.security_profile = fixpp::session::SecurityProfile{sk};
        cfg.compid_authorization_policy = std::move(policy);
        cfg.dictionary      = fixpp::test_support::make_minimal_dictionary();
        cfg.reset_seqnum_policy_field =
            fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.logon_peer_identity_override = std::move(override_id);
        // Minimal factory (no actual reconnect in these seam tests).
        cfg.transport_factory_override =
            std::make_shared<MinimalTransportFactory>();
        return cfg;
    }

    // Open the session and force it to LogonSent by directly installing a
    // transport via the reconnect_fsm (bypassing open()'s initial Logon).
    // For binding seam tests we only need the session in LogonSent state
    // so on_inbound_frame can process the Logon-ack through the guard.
    //
    // We reach LogonSent by calling record_state_transition_ indirectly:
    // the easiest way is to open() the session (which emits Logon via
    // transport_send_, but since no transport is set, the send is a no-op)
    // and then verify the state.
    //
    // Actually: Session::open() drives the initiator into LogonSent via
    // record_state_transition_(LogonSent) — but only if send succeeds.
    // With no transport_send_ set (transport_factory_override that always fails
    // make()), open() will try factory->make() → fails → LogonSent is never
    // entered. We need an in-LogonSent state to test the Logon-ack guard.
    //
    // Solution: use the same approach as test_compid_binding_mtls_fail_closed.cpp
    // from 013 — the initiator open() calls record_state_transition_(LogonSent)
    // BEFORE emitting the Logon frame, so even if transport_send_ is null,
    // the state is LogonSent when on_inbound_frame is later called.
    //
    // We set transport_send_ via cfg.transport_send (a simpler write-only sink).
    fixpp::session::SessionConfig add_null_transport_send(fixpp::session::SessionConfig cfg) {
        cfg.transport_send = [](std::span<const std::byte>) noexcept {};
        return cfg;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1 — On-list identity → admitted to Active.
//
// Policy: CN="PEER-PROD-01" → "ACCEPTOR".
// logon_peer_identity_override = peer_identity{subject_dn="CN=PEER-PROD-01"}.
// mTLS profile; seam arm (1) fires.
//
// Expected: session reaches Active + NO compid_authorization_failed event.
// This test exercises the EXISTING gate (arm 1 in session.cpp) via the seam.
// GREEN today and GREEN after T015 (arm ordering preserved).
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompIdBindingSeamTest, OnListIdentityAdmitsToActive) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("PEER-PROD-01", "ACCEPTOR");

    // Build a peer_identity with CN="PEER-PROD-01".
    fixpp::tls::peer_identity override_pid;
    {
        std::pmr::memory_resource* mr = std::pmr::get_default_resource();
        override_pid.subject_dn = std::pmr::string{"CN=PEER-PROD-01", mr};
        override_pid.leaf_fingerprint = {};
    }

    auto cfg = add_null_transport_send(
        make_cfg(std::move(policy), std::move(override_pid)));

    fixpp::session::Session session{engine, cfg};

    // Open → LogonSent.
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

    // Feed Logon-ack (seq=1, sender="ACCEPTOR", target="INITIATOR").
    auto logon_ack = make_logon_frame("FIX.4.2", 1, "ACCEPTOR", "INITIATOR");
    {
        auto feed_fut = asio::co_spawn(
            ioc,
            session.on_inbound_frame(std::span<const std::byte>{logon_ack}),
            asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(feed_fut.wait_for(0s), std::future_status::ready);
        (void)feed_fut.get();
    }

    // On-list CN → authorize() succeeds → Active (not Disconnected).
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Active)
        << "On-list CN='PEER-PROD-01' with policy binding 'PEER-PROD-01'→'ACCEPTOR' "
        << "must be admitted to Active. Seam arm (1) should authorize and proceed.";

    // No compid_authorization_failed event should be in recent_events().
    bool auth_failed_emitted = false;
    for (const auto& ev : session.recent_events()) {
        if (std::holds_alternative<
                fixpp::session::session_event_compid_authorization_failed>(ev)) {
            auth_failed_emitted = true;
            break;
        }
    }
    EXPECT_FALSE(auth_failed_emitted)
        << "On-list identity must NOT emit compid_authorization_failed.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2 — Off-list identity → fail-closed (NOT Active, event emitted).
//
// Policy: CN="PEER-PROD-01" → "ACCEPTOR".
// logon_peer_identity_override = peer_identity{subject_dn="CN=ROGUE-PEER"}.
// mTLS profile; seam arm (1) fires but authorize() returns unauthorized.
//
// Expected:
//   - Session does NOT reach Active (Disconnected).
//   - session_compid_unauthorized emitted via session_event_compid_authorization_failed.
//
// Asserts the mutated STATE (Disconnected), not just the event — per
// [[feedback_simplify_pass_catches_9th_burn]] "lying event" anti-pattern.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompIdBindingSeamTest, OffListIdentityFailsClosed) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("PEER-PROD-01", "ACCEPTOR");

    // Off-list: CN="ROGUE-PEER" is NOT in the policy.
    fixpp::tls::peer_identity override_pid;
    {
        std::pmr::memory_resource* mr = std::pmr::get_default_resource();
        override_pid.subject_dn = std::pmr::string{"CN=ROGUE-PEER", mr};
        override_pid.leaf_fingerprint = {};
    }

    auto cfg = add_null_transport_send(
        make_cfg(std::move(policy), std::move(override_pid)));

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
            ioc,
            session.on_inbound_frame(std::span<const std::byte>{logon_ack}),
            asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(feed_fut.wait_for(0s), std::future_status::ready);
        (void)feed_fut.get();
    }

    // Fail-closed: state is Disconnected (NOT Active) — the key assertion.
    // [[feedback_simplify_pass_catches_9th_burn]]: assert the mutated state,
    // not just that an event was emitted.
    EXPECT_NE(session.state(), fixpp::session::fsm_state::Active)
        << "Off-list identity must NOT result in Active state (fail-closed).";
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Disconnected)
        << "Off-list identity must result in Disconnected (fail-closed). "
        << "FR-007: off-list identity fails closed, refuses Active.";

    // Verify the event shape (FR-007 / 013 extraction order unchanged).
    bool auth_failed_emitted = false;
    for (const auto& ev : session.recent_events()) {
        if (std::holds_alternative<
                fixpp::session::session_event_compid_authorization_failed>(ev)) {
            auth_failed_emitted = true;
            break;
        }
    }
    EXPECT_TRUE(auth_failed_emitted)
        << "Off-list identity must emit session_event_compid_authorization_failed. "
        << "FR-007: fail-closed emits the inherited 013 event shape.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 3 — Absent identity under mTLS → fail-closed (arm 2).
//
// No logon_peer_identity_override; mTLS profile → arm (2) fires unconditionally
// (no peer_identity available on the pre-T015 path).
//
// Expected:
//   - Session does NOT reach Active (Disconnected) — arm (2) fail-closed.
//   - session_event_compid_authorization_failed emitted.
//
// This is the RC#A baseline (already GREEN). It stays GREEN after T015 only
// when no live peer_id is installed and no override is set. The live-path
// (T015) arm is inserted AHEAD of arm (2) so when a live peer_id IS available
// it takes arm (1-live) instead. Here no live peer_id is threaded.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompIdBindingSeamTest, AbsentIdentityMtlsFailsClosed) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("PEER-PROD-01", "ACCEPTOR");

    // No override — arm (2) mTLS fail-closed fires.
    auto cfg = add_null_transport_send(
        make_cfg(std::move(policy),
                 std::nullopt,  // no override
                 fixpp::session::SecurityProfile::kind::mtls_ca));

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
            ioc,
            session.on_inbound_frame(std::span<const std::byte>{logon_ack}),
            asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(feed_fut.wait_for(0s), std::future_status::ready);
        (void)feed_fut.get();
    }

    // Fail-closed: arm (2) mTLS without peer_identity → Disconnected.
    // [[feedback_simplify_pass_catches_9th_burn]]: assert state, not just event.
    EXPECT_NE(session.state(), fixpp::session::fsm_state::Active)
        << "Absent identity under mTLS must NOT result in Active (fail-closed, arm 2).";
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Disconnected)
        << "Absent identity under mTLS → arm (2) fail-closed → Disconnected. "
        << "RC#A (013 gate-b/r1): mTLS + no peer_identity → fail CLOSED.";

    bool auth_failed_emitted = false;
    for (const auto& ev : session.recent_events()) {
        if (std::holds_alternative<
                fixpp::session::session_event_compid_authorization_failed>(ev)) {
            auth_failed_emitted = true;
            break;
        }
    }
    EXPECT_TRUE(auth_failed_emitted)
        << "Absent identity under mTLS must emit session_event_compid_authorization_failed.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 4 — non-mTLS (one_way_ca) + absent identity → permissive skip → Active.
//
// For completeness: verify the permissive arm (3) is unchanged (backward compat).
// This cell exercises the non-mTLS skip (arm 3) and must remain GREEN.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(CompIdBindingSeamTest, NonMtlsAbsentIdentityPermissiveSkip) {
    fixpp::session::CompIdAuthorizationPolicy policy;
    // Empty policy (default-deny), but we use one_way_ca so the gate is skipped.
    // (FR-007: non-mTLS → gate skipped, no authorization required.)

    // one_way_ca: no client cert → CompID binding inapplicable (permissive).
    auto cfg = add_null_transport_send(
        make_cfg(std::move(policy),
                 std::nullopt,  // no override
                 fixpp::session::SecurityProfile::kind::one_way_ca));

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
            ioc,
            session.on_inbound_frame(std::span<const std::byte>{logon_ack}),
            asio::use_future);
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
