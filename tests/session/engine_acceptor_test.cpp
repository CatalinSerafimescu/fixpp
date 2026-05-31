// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_acceptor_test.cpp — T008 [P] [US1] Phase 3
//
// TDD: acceptor happy-path via the Engine public API.
//
// Scenario (SC-001 / US1 AC1 / C1/C3):
//   Start the engine with an acceptor registered. A standalone mTLS test-
//   initiator connects to the engine's accept loop bound port, completes TLS,
//   sends a FIX Logon with an ON-LIST identity → accept loop runs the
//   handshake, resolves by reversed CompID, attaches, direct-delivers the first
//   Logon → acceptor gate arm (1-live) admits → session reaches established
//   state (Active or LogonReceived).
//
// GREEN (T011/T012/T013): full accept→handshake→resolve→attach→admit wired.
//
// Bounding: ioc.run_for() bounds — no hang. The standalone client uses an
//   internal self-deadline (matching engine_firstframe_test.cpp pattern).
// Anchors: tasks.md T008; spec.md US1 AC1 / SC-001; contracts C1/C3;
//          data-model E-2/E-4; research R3/R4/R7.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
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

#include "engine_loopback_harness.hpp"
#include "support/minimal_dictionary.hpp"

// src/ path for asio_listener.hpp (internal header needed for the TLS client fixture)
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;

namespace {

// Build a valid FIX Logon frame with given sender/target.
static std::vector<std::byte> make_logon_frame(
    std::string_view begin_str,
    std::string_view sender,
    std::string_view target)
{
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "A");   // MsgType = Logon
    body += field(34, "1");   // MsgSeqNum
    body += field(49, sender);
    body += field(56, target);
    body += field(98, "0");   // EncryptMethod = none
    body += field(108, "30"); // HeartBtInt

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

// ── Standalone mTLS test-initiator coroutine ─────────────────────────────────
// Connects to the acceptor's bound port, completes TLS handshake, then sends
// a FIX Logon. The acceptor's accept loop will process it.
// After sending, we wait briefly for the acceptor to reply (it will send a
// reply Logon once it admits the session), then close.
static asio::awaitable<void>
run_test_initiator(asio::io_context& ioc,
                   fixpp::transport::test::LoopbackTlsFixture& fixture,
                   uint16_t acceptor_port,
                   std::string sender,
                   std::string target)
{
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation());

    try {
        // Mint a fresh TLS client transport.
        auto client = fixture.make_client(ioc.get_executor());
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(client.get());
        if (!tls) co_return;

        // Connect TCP to the acceptor.
        fixpp::transport::Endpoint ep{"127.0.0.1", acceptor_port};
        auto conn_r = co_await client->async_connect(ep);
        if (!conn_r.has_value()) co_return;

        // TLS handshake.
        auto hs_r = co_await tls->async_handshake(fixture.ssl_cfg());
        if (!hs_r.has_value()) co_return;

        // Send a valid FIX Logon frame.
        auto logon_bytes = make_logon_frame("FIX.4.2", sender, target);
        auto write_r = co_await client->async_write(
            std::span<const std::byte>{logon_bytes});
        (void)write_r;

        // Stay connected past the test's 3s state-capture window. Since US2
        // (T015) the acceptor read-pump detects peer EOF and drives the session
        // to Disconnected via close(terminal); if this client closed at 2s the
        // SC-001 capture at 3s would observe Disconnected instead of established.
        // The peer must remain connected while the test asserts established state;
        // stop() tears the session down at end-of-test.
        asio::steady_timer t{ioc};
        t.expires_after(5s);
        co_await t.async_wait(asio::use_awaitable);

        (void)client->close();
    } catch (...) {}
}

}  // namespace

// ── SC-001: acceptor admits an on-list live identity to established state ─────
//
// The leaf cert CN is "fixpp-leaf-rsa2048" (per test_live_identity_binding.cpp).
// We configure the acceptor's CompIdAuthorizationPolicy to bind
//   "fixpp-leaf-rsa2048" → INITIATOR (the peer's CompID the acceptor expects).
// The standalone TLS client sends a Logon with SenderCompID="INITIATOR",
// TargetCompID="ACCEPTOR". The reversed-CompID lookup resolves to the acceptor's
// SessionId {FIX.4.2, sender=ACCEPTOR, target=INITIATOR}.
// T013's arm (1-live) fires: live peer_id CN="fixpp-leaf-rsa2048" → on-list →
// admit → session reaches LogonReceived (and then Active after reply Logon sent).

TEST(EngineAcceptorTest, OnListIdentityAdmitsToEstablished) {
    const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    const char* fixture_dir = dir ? dir : kDir;
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    // Build a shared TLS factory + cert_source.
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path        = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path   = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_r.has_value()) << "cert_source build failed";

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs      = std::move(*cs_r);
    ssl.clock   = nullptr;
    ssl.caps    = fixpp::tls::CertSourceCaps{};

    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    ASSERT_TRUE(fac_r.has_value()) << "transport factory build failed";
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fac_r)};

    // CompID authorization policy: on-list CN → allow.
    // The leaf cert's CN is "fixpp-leaf-rsa2048" per loopback fixture convention.
    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");

    // Register the acceptor with the on-list policy.
    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    fixpp::session::SessionConfig acc;
    acc.sender_comp_id  = "ACCEPTOR";
    acc.target_comp_id  = "INITIATOR";
    acc.begin_string    = "FIX.4.2";
    acc.role            = fixpp::session::session_role::acceptor;
    acc.executor_override = ioc.get_executor();
    acc.security_profile = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::mtls_ca};
    acc.compid_authorization_policy = authz;  // on-list policy
    acc.dictionary     = fixpp::test_support::make_minimal_dictionary();
    acc.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    acc.transport_factory_override = fac;
    acc.heartbeat_interval = std::chrono::seconds{30};
    acc.logout_disconnect_timeout_ms = 2000;
    acc.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    acc.transport_send = [](std::span<const std::byte>) {};

    auto acc_id = fixpp::session::SessionId::from_config(acc);
    ASSERT_TRUE(engine.register_session(std::move(acc)).has_value());

    engine.start();

    // Run executor briefly to let the accept loop bind the listener.
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t bound_port = engine.acceptor_bound_endpoint(acc_id).port;
    ASSERT_NE(bound_port, 0u) << "acceptor listener did not bind (port is 0)";

    // Build the loopback TLS fixture (for client-side transport).
    fixpp::transport::test::LoopbackTlsFixture fixture{
        std::string(fixture_dir), ioc.get_executor()};

    // Spawn the standalone test-initiator client.
    asio::co_spawn(ioc,
        run_test_initiator(ioc, fixture, bound_port,
                           /*sender=*/"INITIATOR", /*target=*/"ACCEPTOR"),
        asio::detached);

    // Run for up to 3s to allow accept→handshake→auth→admit.
    ioc.run_for(3s);
    ioc.restart();

    // Capture state BEFORE stop() frees the session.
    fixpp::session::Session* acc_session = engine.lookup(acc_id);
    bool established = (acc_session != nullptr) &&
        (acc_session->state() == fixpp::session::fsm_state::Active ||
         acc_session->state() == fixpp::session::fsm_state::LogonReceived);

    std::string state_str = (acc_session != nullptr)
        ? std::to_string(static_cast<int>(acc_session->state()))
        : "null";

    // Stop cleanly.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    EXPECT_TRUE(established)
        << "SC-001: the acceptor session must reach Active (or LogonReceived) "
        << "after an on-list initiator connects over loopback-TLS. "
        << "state=" << state_str
        << ". GREEN after T011/T012/T013: full accept→handshake→resolve→"
        << "attach→arm(1-live)→admit; session reaches established state.";
}

// ── Gate A New-3: lookup() returns null before start ─────────────────────────

TEST(EngineAcceptorTest, LookupNullBeforeStart) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    auto harness = fixpp::test_support::EngineLoopbackHarness::build(
        ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    // Before start(): lazy construction — no Session exists yet (Gate A New-3).
    EXPECT_EQ(harness->engine().lookup(harness->acceptor_id()), nullptr)
        << "lookup() must return null for a registered-but-not-yet-started "
        << "session (lazy construction, Gate A New-3).";

    // Stop without start (no loops to join).
    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();
}
