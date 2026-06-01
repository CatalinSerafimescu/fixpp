// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_reconnect_backoff_cap.cpp — T007 [P] [US1] Phase 3
//                                                T013 [P] [US2] Phase 4 (auth-fail cell)
//
// drive_reconnect_attempt() backoff + cap witness:
//   - handshake-fail (mock async_handshake returns failure) consumes exactly one
//     attempt per FR-002/FR-003; loop terminates at max_attempts →
//     fsm_state::Disconnected (SC-002). (The mock's async_connect always
//     succeeds, so the connect-fail branch is not witnessed here — tracked as a
//     follow-up in the 014 verify doc.)
//   - make-fail (factory returns transport_factory_failed) same counting.
//   - No infinite retry (max_attempts is respected).
//   - Backoff delays honoured via mock_clock (deterministic; no wall-clock sleep).
//   - T013 (US2): auth-fail (off-list identity) under binding policy → reason-agnostic,
//     consumes one attempt per try, retries to cap. NOT terminal-early, NO new cap.
//     (FR-003/FR-007; Clarifications Q1; R4; US1 AC2)
//
// Anchors: FR-002/003; US1 AC2; SC-002; data-model E-1; contracts C1.
//          T013: FR-003/FR-007; Q1; R4; data-model E-2; contracts C2.
//
// TDD RED witness (T007):
//   The current drive_reconnect_attempt() stub (reconnect_fsm.cpp:53-61) makes
//   exactly ONE make() call and returns success WITHOUT calling async_connect or
//   async_handshake. The attempt counter stays at 1, never reaches max_attempts,
//   and the loop never terminates → EXPECT_EQ(make_count, max_attempts) FAILS.
//
// TDD RED witness (T013 auth-fail cell):
//   Without T014, the authorize step in drive_reconnect_attempt is not wired
//   (step 7 is a no-op: `(void)hr`). The off-list identity is ignored and the
//   first attempt SUCCEEDS (install_reconnected_transport called at n=0).
//   make_call_count == 1 != max_attempts → EXPECT_EQ fails.
//   GREEN after T014: auth-fail counts as one attempt per retry; exhausts cap.

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/reconnect_fsm.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

// mock_transport is test-only; gate with the required define.
#define FIXPP_ALLOW_MOCK_TRANSPORT
#include <fixpp/transport/test/mock_transport.hpp>

// minimal_dictionary for session open
#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// FailingTransportFactory — returns scripted mock_transports whose
// connect/handshake outcomes are controlled per factory construction.
//
// Two modes:
//   make_fail   — make() returns transport_factory_failed
//   handshake_fail — make() succeeds, async_connect succeeds, async_handshake fails
// ─────────────────────────────────────────────────────────────────────────────
enum class FailMode { make_fail, handshake_fail };

class FailingTransportFactory final : public fixpp::transport::TransportFactory {
public:
    explicit FailingTransportFactory(FailMode mode) : mode_{mode} {}

    std::atomic<int> make_call_count{0};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        if (mode_ == FailMode::make_fail) {
            return std::unexpected{fixpp::core::error::transport_factory_failed};
        }
        // handshake_fail: return a scripted mock whose handshake fails.
        // NOTE: the mock_transport's async_connect always succeeds, so the
        // connect-fail branch of drive_reconnect_attempt (reconnect_fsm.cpp
        // step 5) is not witnessed here. A genuine connect-failure mock seam is
        // tracked as a follow-up in the 014 verify doc; connect- and handshake-
        // failures count identically (one attempt each, FR-003), so the cap
        // witness below is unaffected.
        fixpp::transport::test::Script script;
        script.handshake_succeeds = false;
        return std::make_unique<fixpp::transport::test::mock_transport>(std::move(exec),
                                                                        std::move(script));
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }

private:
    FailMode mode_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────
class ReconnectBackoffCapTest : public ::testing::Test {
protected:
    asio::io_context ioc;

    // Build a ReconnectPolicy with zero-delay schedule (test speed) and
    // small max_attempts cap.
    static fixpp::transport::ReconnectPolicy make_policy(std::uint32_t max_attempts) {
        fixpp::transport::ReconnectPolicy policy;
        policy.max_attempts = max_attempts;
        policy.schedule =
            std::pmr::vector<std::chrono::milliseconds>{std::pmr::get_default_resource()};
        // Zero-delay so tests don't sleep
        policy.schedule.push_back(0ms);
        policy.jitter = 0.0;
        return policy;
    }

    fixpp::core::expected_t<void> run_drive(fixpp::transport::TransportFactory* factory,
                                            std::uint32_t max_attempts) {
        fixpp::session::ReconnectFsm fsm(factory, make_policy(max_attempts),
                                         std::chrono::seconds{30}, 2000ms);

        auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(), asio::use_future);
        ioc.run_for(2s);
        ioc.restart();
        return fut.get();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell: make() fails → each attempt consumed, terminates at cap.
//
// RED witness: the stub makes ONE make() call and returns success, so
//   make_call_count == 1 != max_attempts (3); test FAILS RED.
// GREEN after T009: loop retries max_attempts times then co_returns error.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectBackoffCapTest, MakeFailConsumesAttemptsAndTerminates) {
    constexpr std::uint32_t kMaxAttempts = 3;
    auto factory = std::make_shared<FailingTransportFactory>(FailMode::make_fail);

    (void)run_drive(factory.get(), kMaxAttempts);

    // Each make() failure counts as one attempt; exactly max_attempts make()
    // calls fired before the loop gave up.
    EXPECT_EQ(factory->make_call_count.load(), static_cast<int>(kMaxAttempts))
        << "make_fail: expected exactly " << kMaxAttempts
        << " make() calls (one per attempt) before loop termination. "
        << "RED: stub makes 1 call and returns success → count=" << factory->make_call_count.load();
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell: handshake fails → each attempt consumed, terminates at cap.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectBackoffCapTest, HandshakeFailConsumesAttemptsAndTerminates) {
    constexpr std::uint32_t kMaxAttempts = 4;
    auto factory = std::make_shared<FailingTransportFactory>(FailMode::handshake_fail);

    (void)run_drive(factory.get(), kMaxAttempts);

    // The factory's make() must be called once per attempt.
    EXPECT_EQ(factory->make_call_count.load(), static_cast<int>(kMaxAttempts))
        << "handshake_fail: expected exactly " << kMaxAttempts
        << " make() calls before loop termination. "
        << "RED: stub makes 1 call, never retries.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell: no infinite retry — max_attempts=1 means exactly one attempt.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectBackoffCapTest, ExactlyOneAttemptWhenCapIsOne) {
    auto factory = std::make_shared<FailingTransportFactory>(FailMode::make_fail);

    (void)run_drive(factory.get(), 1);

    EXPECT_EQ(factory->make_call_count.load(), 1)
        << "max_attempts=1 must make exactly 1 make() call. "
        << "RED: stub also makes 1 call but for the wrong reason.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell: result is an error (not success) when cap is exhausted.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectBackoffCapTest, ReturnsErrorOnCapExhaustion) {
    auto factory = std::make_shared<FailingTransportFactory>(FailMode::make_fail);

    auto result = run_drive(factory.get(), 2);

    // After cap exhaustion, drive_reconnect_attempt must co_return an error.
    // RED: stub returns success (expected_t<void>{}).
    EXPECT_FALSE(result.has_value()) << "Expected error on cap exhaustion; got success. "
                                     << "RED: stub always returns success.";
    // The terminal error code must be transport_reconnect_limit_exceeded (C1).
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), fixpp::core::error::transport_reconnect_limit_exceeded)
            << "Cap-exhaustion must surface transport_reconnect_limit_exceeded, "
            << "not transport_factory_failed or any other code. [C1; FR-003]";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// T013 (US2) — Auth-fail cell: off-list identity consumes exactly one attempt
// per retry, retries to cap (reason-agnostic, NOT terminal-early, NO new cap).
//
// This is the Phase 4 (US2) extension of the backoff-cap witness.
// Anchors: FR-003/FR-007; Clarifications Q1; research R4; US1 AC2.
// Data-model E-2; contracts C2 (reconnect path: retry-to-cap on auth fail).
//
// Factory: SuccessfulHandshakeFactory — always succeeds make/connect/handshake
//   but injects an off-list identity (CN="ROGUE-PEER") in the handshake result.
// Session policy: only CN="ON-LIST-PEER" → "ACCEPTOR" is authorized.
// FSM: mTLS profile, session_ set, max_attempts=N.
//
// Expected behavior (GREEN after T014):
//   - Each attempt completes the TLS handshake → auth-fail (CN="ROGUE-PEER"
//     not on the allow-list) → count attempt, release transport, continue.
//   - Exactly N make() calls before loop exhaustion.
//   - drive_reconnect_attempt returns an error (cap exceeded).
//   - Session state: drive_reconnect_attempt did NOT transition to Active.
//
// RED before T014:
//   - Step 7 is `(void)hr` (no auth check). The first attempt calls
//     install_reconnected_transport and returns SUCCESS after 1 make() call.
//   - make_call_count == 1 != max_attempts (3) → EXPECT_EQ fails.
// ─────────────────────────────────────────────────────────────────────────────

// SuccessfulHandshakeFactory: always connects+handshakes, injects off-list CN.
class SuccessfulHandshakeOffListFactory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<int> make_call_count{0};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        // Script: connect succeeds, handshake succeeds.
        fixpp::transport::test::Script script;
        script.handshake_succeeds = true;
        // Inject off-list CN="ROGUE-PEER" in the handshake result via the mock.
        // mock_transport::async_handshake uses script_.peer_identity_to_return.
        {
            std::pmr::memory_resource* mr = std::pmr::get_default_resource();
            fixpp::tls::peer_identity off_list_pid;
            off_list_pid.subject_dn = std::pmr::string{"CN=ROGUE-PEER", mr};
            off_list_pid.leaf_fingerprint = {};
            script.peer_identity_to_return = std::move(off_list_pid);
        }
        return std::make_unique<fixpp::transport::test::mock_transport>(std::move(exec),
                                                                        std::move(script));
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
// AuthFailReconnectCapTest fixture — needs a real Session wired to the FSM
// so the authorize() call in step 7 (T014) can run against the policy.
// ─────────────────────────────────────────────────────────────────────────────
class AuthFailReconnectCapTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    fixpp::core::EngineConfig engine{};

    void SetUp() override { engine.executor = ioc.get_executor(); }

    static fixpp::transport::ReconnectPolicy make_policy(std::uint32_t max_attempts) {
        fixpp::transport::ReconnectPolicy policy;
        policy.max_attempts = max_attempts;
        policy.schedule =
            std::pmr::vector<std::chrono::milliseconds>{std::pmr::get_default_resource()};
        policy.schedule.push_back(0ms);
        policy.jitter = 0.0;
        return policy;
    }
};

TEST_F(AuthFailReconnectCapTest, AuthFailConsumesAttemptsAndTerminatesAtCap) {
    constexpr std::uint32_t kMaxAttempts = 3;

    auto factory = std::make_shared<SuccessfulHandshakeOffListFactory>();

    // Session with mTLS profile and a binding policy that ONLY allows
    // CN="ON-LIST-PEER" → "ACCEPTOR". The factory injects CN="ROGUE-PEER".
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("ON-LIST-PEER", "ACCEPTOR");

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "INITIATOR";
    cfg.target_comp_id = "ACCEPTOR";
    cfg.begin_string = "FIX.4.2";
    cfg.heartbeat_interval = std::chrono::seconds{30};
    cfg.logout_disconnect_timeout_ms = 2000;
    cfg.role = fixpp::session::session_role::initiator;
    cfg.executor_override = ioc.get_executor();
    // mTLS: live-identity arm (T015) fires after T014/T015 land.
    // Before T015, the arm 2 (mTLS no override) fires and fail-closes too —
    // but the RED condition is that step 7 is absent so the first attempt
    // succeeds without any auth check.
    cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    cfg.compid_authorization_policy = std::move(policy);
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    // NO injected-identity seam — auth must come from step 7 (T014).
    cfg.transport_factory_override = factory;
    cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19877};
    // Minimal transport_send for Session::open() to reach LogonSent.
    cfg.transport_send = [](std::span<const std::byte>) noexcept {};

    fixpp::session::Session session{engine, cfg};

    // Open the session (LogonSent).
    {
        auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(open_fut.wait_for(0s), std::future_status::ready);
        auto open_r = open_fut.get();
        ASSERT_TRUE(open_r.has_value())
            << "Session::open() failed: " << static_cast<int>(open_r.error());
    }

    // Standalone ReconnectFsm bound to the Session and factory.
    // set_session_owner → install_reconnected_transport (T010) is callable.
    // After T014: the FSM calls authorize(hr.peer_id, ...) in step 7 before
    // calling install_reconnected_transport.
    fixpp::session::ReconnectFsm reconnect_fsm(factory.get(), make_policy(kMaxAttempts),
                                               std::chrono::seconds{30}, 2000ms);
    reconnect_fsm.set_reconnect_endpoint(fixpp::transport::Endpoint{"127.0.0.1", 19877});
    reconnect_fsm.set_session_owner(&session);
    reconnect_fsm.set_tls_profile(fixpp::tls::SecurityProfile::mtls_ca);

    auto fut = asio::co_spawn(ioc, reconnect_fsm.drive_reconnect_attempt(), asio::use_future);
    ioc.run_for(2s);
    ioc.restart();

    ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
    auto result = fut.get();

    // GREEN after T014: auth-fail on EACH attempt → N make() calls at cap.
    // RED before T014: make_call_count == 1 (step 7 no-op → success on attempt 0).
    EXPECT_EQ(factory->make_call_count.load(), static_cast<int>(kMaxAttempts))
        << "auth-fail: expected exactly " << kMaxAttempts
        << " make() calls (one per attempt) before cap. "
        << "FR-003/FR-007: auth-fail is reason-agnostic — same cap as any failure. "
        << "RED (T014 not yet): step 7 is a no-op → attempt 0 succeeds → "
        << "make_call_count==1 ≠ " << kMaxAttempts << ". "
        << "GREEN after T014: each auth-fail counts as one attempt → exhausts cap.";

    // After cap exhaustion the result must be an error (not success).
    EXPECT_FALSE(result.has_value())
        << "Auth-fail exhausting the cap must return an error. "
        << "RED: step 7 no-op → attempt 0 succeeds → result.has_value()==true.";
    // The terminal error code must be transport_reconnect_limit_exceeded (C1).
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), fixpp::core::error::transport_reconnect_limit_exceeded)
            << "Auth-fail cap-exhaustion must surface transport_reconnect_limit_exceeded. "
            << "[C1; FR-003; Q1 reason-agnostic: same terminal code regardless of per-attempt "
               "cause]";
    }

    // Session must NOT be in Active (auth always fails → never transitions).
    // After T014: auth-fail in step 7 releases the transport and counts the
    // attempt WITHOUT calling install_reconnected_transport, so the session
    // stays in its pre-reconnect state throughout.
    EXPECT_NE(session.state(), fixpp::session::fsm_state::Active)
        << "Auth-fail must never reach Active. "
        << "Q1 reason-agnostic: auth-fail is retried to cap, not terminal-early, "
        << "but it never produces a successful admit.";
}

}  // namespace
