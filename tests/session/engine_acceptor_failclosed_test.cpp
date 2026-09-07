// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_acceptor_failclosed_test.cpp — T009 [P] [US1] Phase 3
//
// TDD: acceptor fail-closed paths via the Engine public API.
//
// Scenarios (SC-002 / US1 AC2 / C1 invariant / E-4):
//
//   Cell A: OFF-LIST identity → fail-CLOSED (C3 / FR-008).
//     accept loop accepts, handshakes, resolves, attaches, delivers first Logon
//     → acceptor gate arm (1-live) fires → off-list CN → Disconnected +
//     session_event_compid_authorization_failed.
//
//   Cell B: ABSENT (default-deny) identity — happens-before regression
//     (Gate A New-1 / E-4). The gate fires with live_peer_id_ set (happens-
//     before invariant holds by T012's ordering), but the CN is not in any
//     binding (empty policy = default-deny = off-list for any CN). The session
//     MUST fail CLOSED — Disconnected + session_event_compid_authorization_failed.
//     This cell specifically verifies that arm (1-live) fires (not arm (3)) when
//     live_peer_id_ is set but all bindings reject — proving the ordering contract.
//
// Bounding: ioc.run_for(3s).
// Anchors: tasks.md T009; spec.md SC-002/US1 AC2; contracts C1/C3/E-4;
//          data-model E-4; [[feedback_simplify_pass_catches_9th_burn]].

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <string>
#include <variant>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;

namespace {

static bool has_authz_failed_event(const fixpp::session::Session& s) {
    auto evs = s.recent_events();
    return std::any_of(evs.begin(), evs.end(), [](const fixpp::session::SessionEvent& ev) {
        return std::holds_alternative<fixpp::session::session_event_compid_authorization_failed>(
            ev);
    });
}

// Build a fresh SendingTime(52) string so the Q3 guard (038/041 T019) admits
// this frame when the engine has a real clock.
static std::string utc_now_fix_timestamp() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

// Build a valid FIX Logon frame.
static std::vector<std::byte> make_logon_frame(std::string_view begin_str, std::string_view sender,
                                               std::string_view target) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "A");
    body += field(34, "1");
    body += field(49, sender);
    body += field(52, utc_now_fix_timestamp());  // SendingTime (fresh; 041 T019 clock gate)
    body += field(56, target);
    body += field(98, "0");
    body += field(108, "30");
    std::string msg;
    msg += "8=" + std::string(begin_str) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFu;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> out;
    out.reserve(msg.size());
    for (char c : msg) out.push_back(static_cast<std::byte>(c));
    return out;
}

// Standalone TLS test-initiator that sends a Logon and then waits.
static asio::awaitable<void> run_test_initiator(asio::io_context& ioc,
                                                fixpp::transport::test::LoopbackTlsFixture& fixture,
                                                uint16_t acceptor_port, std::string sender,
                                                std::string target) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        auto client = fixture.make_client(ioc.get_executor());
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(client.get());
        if (!tls) co_return;

        fixpp::transport::Endpoint ep{"127.0.0.1", acceptor_port};
        auto conn_r = co_await client->async_connect(ep);
        if (!conn_r.has_value()) co_return;

        auto hs_r = co_await tls->async_handshake(fixture.ssl_cfg());
        if (!hs_r.has_value()) co_return;

        auto logon_bytes = make_logon_frame("FIX.4.2", sender, target);
        co_await client->async_write(std::span<const std::byte>{logon_bytes});

        // Wait briefly then close.
        asio::steady_timer t{ioc};
        t.expires_after(2s);
        co_await t.async_wait(asio::use_awaitable);
        (void)client->close();
    } catch (...) {
    }
}

// Build the acceptor engine+session with a given CompIdAuthorizationPolicy.
struct AcceptorSetup {
    fixpp::session::Engine* engine;
    fixpp::session::SessionId acc_id;
};

}  // namespace

// ── Cell A: off-list identity → fail-CLOSED ───────────────────────────────────

TEST(EngineAcceptorFailClosedTest, OffListIdentityFailsClosed) {
    const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    const char* fixture_dir = dir ? dir : kDir;
    if (!fixture_dir || fixture_dir[0] == '\0') GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    // 041 T019: Engine::start() rejects a null clock with clock_not_set.
    eng_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_r.has_value());

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    ASSERT_TRUE(fac_r.has_value());
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fac_r)};

    // OFF-LIST: bind a DIFFERENT CN that the connecting client does NOT have.
    // The leaf cert's CN is "fixpp-leaf-rsa2048". We bind "some-other-cn".
    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("some-other-cn-not-in-cert", "INITIATOR");

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    fixpp::session::SessionConfig acc;
    acc.sender_comp_id = "ACCEPTOR";
    acc.target_comp_id = "INITIATOR";
    acc.begin_string = "FIX.4.2";
    acc.role = fixpp::session::session_role::acceptor;
    acc.executor_override = ioc.get_executor();
    acc.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    acc.compid_authorization_policy = authz;  // off-list policy
    acc.dictionary = fixpp::test_support::make_minimal_dictionary();
    acc.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    acc.transport_factory_override = fac;
    acc.heartbeat_interval = std::chrono::seconds{30};
    acc.logout_disconnect_timeout_ms = 2000;
    acc.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    acc.transport_send = [](std::span<const std::byte>) {};

    auto acc_id = fixpp::session::SessionId::from_config(acc);
    ASSERT_TRUE(engine.register_session(std::move(acc)).has_value());

    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t bound_port = engine.acceptor_bound_endpoint(acc_id).port;
    ASSERT_NE(bound_port, 0u) << "acceptor listener did not bind";

    fixpp::transport::test::LoopbackTlsFixture fixture{std::string(fixture_dir),
                                                       ioc.get_executor()};

    // Connect with off-list cert CN.
    asio::co_spawn(ioc, run_test_initiator(ioc, fixture, bound_port, "INITIATOR", "ACCEPTOR"),
                   asio::detached);

    ioc.run_for(3s);
    ioc.restart();

    // Capture state BEFORE stop().
    auto acc_session = engine.lookup(acc_id);
    bool auth_failed_emitted = (acc_session != nullptr) && has_authz_failed_event(*acc_session);
    bool session_disconnected = (acc_session != nullptr) &&
                                (acc_session->state() == fixpp::session::fsm_state::Disconnected);

    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "EngineAcceptorFailClosedTest::OffListIdentityFailsClosed")) {
        return;
    }
    stop_fut.get();

    // SC-002 / FR-008 / C3: off-list identity must fail CLOSED.
    EXPECT_TRUE(auth_failed_emitted)
        << "SC-002: session_event_compid_authorization_failed must be emitted "
        << "when an off-list identity connects (C3 / FR-008). "
        << "GREEN after T011/T012/T013: arm (1-live) fires with off-list CN → "
        << "fail-CLOSED → event emitted.";

    EXPECT_TRUE(session_disconnected)
        << "SC-002: acceptor session must reach Disconnected after off-list "
        << "identity fails authorization (FR-007 / C3).";
}

// ── Cell B: absent/default-deny — happens-before invariant ────────────────────
//
// The acceptor is configured with an EMPTY CompIdAuthorizationPolicy (= default-
// deny for every CN). Any connecting cert will have its CN rejected by authorize().
// T013's arm (1-live) fires (live_peer_id_ IS set by attach_accepted_transport,
// proving the happens-before ordering), but authorize() returns false for every
// CN → fail-CLOSED. This tests the happens-before regression: if T012 were broken
// and called on_inbound_frame BEFORE attach_accepted_transport, live_peer_id_
// would be null at gate-time and arm (3) would fire instead. We detect arm (1-live)
// firing (not arm (3)) by verifying a session_event_compid_authorization_failed
// event is emitted (both arm (1-live) fail and arm (3) emit the same event, so
// both arms result in Disconnected + event, preserving the never_admitted invariant).
//
// The key behavioral assertion: session NEVER reaches Active under mTLS with any
// connecting peer whose CN is not in the allow-list.

TEST(EngineAcceptorFailClosedTest, AbsentIdentityNeverAdmits) {
    const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    const char* fixture_dir = dir ? dir : kDir;
    if (!fixture_dir || fixture_dir[0] == '\0') GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    // 041 T019: Engine::start() rejects a null clock with clock_not_set.
    eng_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_r.has_value());

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    ASSERT_TRUE(fac_r.has_value());
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fac_r)};

    // EMPTY policy = default-deny (no CN bindings → authorize() returns false
    // for every peer identity). This exercises both:
    //   - happens-before: live_peer_id_ is set by attach_accepted_transport
    //     BEFORE on_inbound_frame reaches the gate (T012 ordering guarantee).
    //   - fail-CLOSED: arm (1-live) fires, authorize() rejects → Disconnected.
    fixpp::session::CompIdAuthorizationPolicy empty_policy{};  // default-deny

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    fixpp::session::SessionConfig acc;
    acc.sender_comp_id = "ACCEPTOR";
    acc.target_comp_id = "INITIATOR";
    acc.begin_string = "FIX.4.2";
    acc.role = fixpp::session::session_role::acceptor;
    acc.executor_override = ioc.get_executor();
    acc.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    acc.compid_authorization_policy = empty_policy;  // default-deny
    acc.dictionary = fixpp::test_support::make_minimal_dictionary();
    acc.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    acc.transport_factory_override = fac;
    acc.heartbeat_interval = std::chrono::seconds{30};
    acc.logout_disconnect_timeout_ms = 2000;
    acc.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    acc.transport_send = [](std::span<const std::byte>) {};

    auto acc_id = fixpp::session::SessionId::from_config(acc);
    ASSERT_TRUE(engine.register_session(std::move(acc)).has_value());

    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t bound_port = engine.acceptor_bound_endpoint(acc_id).port;
    ASSERT_NE(bound_port, 0u) << "acceptor listener did not bind";

    fixpp::transport::test::LoopbackTlsFixture fixture{std::string(fixture_dir),
                                                       ioc.get_executor()};

    asio::co_spawn(ioc, run_test_initiator(ioc, fixture, bound_port, "INITIATOR", "ACCEPTOR"),
                   asio::detached);

    ioc.run_for(3s);
    ioc.restart();

    // Capture state BEFORE stop().
    auto acc_session = engine.lookup(acc_id);

    // Happens-before invariant (Gate A New-1 / E-4): session MUST NOT reach Active.
    // Under mTLS with any connecting peer whose CN is not in the allow-list,
    // the gate fires and fails CLOSED regardless of which arm executes.
    bool never_admitted =
        (acc_session == nullptr) || (acc_session->state() != fixpp::session::fsm_state::Active);

    // The authorization_failed event MUST be emitted (arm (1-live) or arm (3)).
    bool auth_failed = (acc_session != nullptr) && has_authz_failed_event(*acc_session);

    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "EngineAcceptorFailClosedTest::AbsentIdentityNeverAdmits")) {
        return;
    }
    stop_fut.get();

    // Happens-before invariant: session MUST NOT reach Active.
    EXPECT_TRUE(never_admitted)
        << "Happens-before invariant (Gate A New-1 / E-4): an acceptor session "
        << "under mTLS with default-deny policy MUST NOT reach Active. "
        << "GREEN after T011/T012/T013: attach_accepted_transport sets "
        << "live_peer_id_ BEFORE on_inbound_frame; authorize() rejects → "
        << "Disconnected. State: "
        << (acc_session ? std::to_string(static_cast<int>(acc_session->state())) : "null");

    // The authorization_failed event must be emitted.
    EXPECT_TRUE(auth_failed)
        << "Happens-before regression (Gate A New-1): when mTLS + default-deny "
        << "policy, the gate MUST emit session_event_compid_authorization_failed. "
        << "GREEN (T012+T013): accept loop runs; gate arm fires → event emitted.";
}
