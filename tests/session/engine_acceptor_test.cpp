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

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <cstddef>
#include <cstdlib>
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
#include <future>
#include <string>
#include <vector>

#include "engine_loopback_harness.hpp"
#include "support/minimal_dictionary.hpp"

// src/ path for asio_listener.hpp (internal header needed for the TLS client fixture)
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;

namespace {

// Build a valid FIX Logon frame with given sender/target.
static std::vector<std::byte> make_logon_frame(std::string_view begin_str, std::string_view sender,
                                               std::string_view target) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "A");  // MsgType = Logon
    body += field(34, "1");  // MsgSeqNum
    body += field(49, sender);
    body += field(56, target);
    body += field(98, "0");    // EncryptMethod = none
    body += field(108, "30");  // HeartBtInt

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
static asio::awaitable<void> run_test_initiator(asio::io_context& ioc,
                                                fixpp::transport::test::LoopbackTlsFixture& fixture,
                                                uint16_t acceptor_port, std::string sender,
                                                std::string target) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

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
        auto write_r = co_await client->async_write(std::span<const std::byte>{logon_bytes});
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
    } catch (...) {
    }
}

// Build a valid FIX Heartbeat (35=0) frame with a given MsgSeqNum.
static std::vector<std::byte> make_heartbeat_frame(std::string_view begin_str,
                                                   std::string_view sender, std::string_view target,
                                                   int seq) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "0");                  // MsgType = Heartbeat
    body += field(34, std::to_string(seq));  // MsgSeqNum
    body += field(49, sender);
    body += field(56, target);

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

// F-015-001: send the first Logon SPLIT across two writes with a gap, so the
// acceptor's bounded first-frame read observes it as TWO separate reads. The
// incremental-feed fix must reassemble + admit it (the pre-fix whole-buffer
// re-feed duplicated the carried prefix → malformed → rejected).
static asio::awaitable<void> run_test_initiator_fragmented(
    asio::io_context& ioc, fixpp::transport::test::LoopbackTlsFixture& fixture,
    uint16_t acceptor_port, std::string sender, std::string target) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        auto client = fixture.make_client(ioc.get_executor());
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(client.get());
        if (!tls) co_return;
        fixpp::transport::Endpoint ep{"127.0.0.1", acceptor_port};
        if (!(co_await client->async_connect(ep)).has_value()) co_return;
        if (!(co_await tls->async_handshake(fixture.ssl_cfg())).has_value()) co_return;

        auto logon = make_logon_frame("FIX.4.2", sender, target);
        std::size_t split = logon.size() / 2;
        (void)co_await client->async_write(std::span<const std::byte>{logon.data(), split});
        // Gap forces the acceptor's first read to see only the first half.
        asio::steady_timer gap{ioc};
        gap.expires_after(150ms);
        co_await gap.async_wait(asio::use_awaitable);
        (void)co_await client->async_write(
            std::span<const std::byte>{logon.data() + split, logon.size() - split});

        asio::steady_timer t{ioc};
        t.expires_after(5s);
        co_await t.async_wait(asio::use_awaitable);
        (void)client->close();
    } catch (...) {
    }
}

// F-015-002: send Logon (seq 1) + Heartbeat (seq 2) COALESCED in a single write.
// The acceptor must admit the Logon AND drain the Heartbeat surplus through the
// read-pump (next_inbound advances past the post-Logon value of 2 to 3). The
// pre-fix code delivered the whole buffer as the "Logon" and dropped the surplus.
static asio::awaitable<void> run_test_initiator_coalesced(
    asio::io_context& ioc, fixpp::transport::test::LoopbackTlsFixture& fixture,
    uint16_t acceptor_port, std::string sender, std::string target) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        auto client = fixture.make_client(ioc.get_executor());
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(client.get());
        if (!tls) co_return;
        fixpp::transport::Endpoint ep{"127.0.0.1", acceptor_port};
        if (!(co_await client->async_connect(ep)).has_value()) co_return;
        if (!(co_await tls->async_handshake(fixture.ssl_cfg())).has_value()) co_return;

        auto logon = make_logon_frame("FIX.4.2", sender, target);
        auto hb = make_heartbeat_frame("FIX.4.2", sender, target, /*seq=*/2);
        std::vector<std::byte> both;
        both.reserve(logon.size() + hb.size());
        both.insert(both.end(), logon.begin(), logon.end());
        both.insert(both.end(), hb.begin(), hb.end());
        (void)co_await client->async_write(std::span<const std::byte>{both});

        asio::steady_timer t{ioc};
        t.expires_after(5s);
        co_await t.async_wait(asio::use_awaitable);
        (void)client->close();
    } catch (...) {
    }
}

// Shared mTLS-acceptor rig for the fragmented/coalesced first-frame tests:
// builds the TLS factory + on-list policy {CN fixpp-leaf-rsa2048 → INITIATOR},
// registers the acceptor {ACCEPTOR,INITIATOR} on `engine`, starts it, and returns
// {acc_id, bound_port}. `fac_keepalive` must outlive `engine` (the Session holds
// the factory). Returns nullopt if the fixtures are missing or bind fails.
static std::optional<std::pair<fixpp::session::SessionId, uint16_t>> start_mtls_acceptor(
    asio::io_context& ioc, fixpp::session::Engine& engine, const std::string& fixture_dir,
    std::shared_ptr<fixpp::transport::TransportFactory>& fac_keepalive) {
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = fixture_dir + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = fixture_dir + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = fixture_dir + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    if (!cs_r.has_value()) return std::nullopt;

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};
    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    if (!fac_r.has_value()) return std::nullopt;
    fac_keepalive = std::move(*fac_r);

    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");

    fixpp::session::SessionConfig acc;
    acc.sender_comp_id = "ACCEPTOR";
    acc.target_comp_id = "INITIATOR";
    acc.begin_string = "FIX.4.2";
    acc.role = fixpp::session::session_role::acceptor;
    acc.executor_override = ioc.get_executor();
    acc.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    acc.compid_authorization_policy = authz;
    acc.dictionary = fixpp::test_support::make_minimal_dictionary();
    acc.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    acc.transport_factory_override = fac_keepalive;
    acc.heartbeat_interval = std::chrono::seconds{30};
    acc.logout_disconnect_timeout_ms = 2000;
    acc.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    acc.transport_send = [](std::span<const std::byte>) {};

    auto acc_id = fixpp::session::SessionId::from_config(acc);
    if (!engine.register_session(std::move(acc)).has_value()) return std::nullopt;

    engine.start();
    ioc.run_for(50ms);
    ioc.restart();
    uint16_t port = engine.acceptor_bound_endpoint(acc_id).port;
    if (port == 0) return std::nullopt;
    return std::make_pair(acc_id, port);
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
    if (!fixture_dir || fixture_dir[0] == '\0') GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";

    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    // Build a shared TLS factory + cert_source.
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_r.has_value()) << "cert_source build failed";

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

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
    acc.sender_comp_id = "ACCEPTOR";
    acc.target_comp_id = "INITIATOR";
    acc.begin_string = "FIX.4.2";
    acc.role = fixpp::session::session_role::acceptor;
    acc.executor_override = ioc.get_executor();
    acc.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    acc.compid_authorization_policy = authz;  // on-list policy
    acc.dictionary = fixpp::test_support::make_minimal_dictionary();
    acc.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
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
    fixpp::transport::test::LoopbackTlsFixture fixture{std::string(fixture_dir),
                                                       ioc.get_executor()};

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

    std::string state_str =
        (acc_session != nullptr) ? std::to_string(static_cast<int>(acc_session->state())) : "null";

    // Stop cleanly.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    EXPECT_TRUE(established) << "SC-001: the acceptor session must reach Active (or LogonReceived) "
                             << "after an on-list initiator connects over loopback-TLS. "
                             << "state=" << state_str
                             << ". GREEN after T011/T012/T013: full accept→handshake→resolve→"
                             << "attach→arm(1-live)→admit; session reaches established state.";
}

// ── F-015-001: a Logon fragmented across two reads is reassembled + admitted ──
// Regression witness for the GPT-5.5 review finding. The acceptor's bounded
// first-frame read must feed only newly-read bytes into the stateful Framer; the
// pre-fix code re-fed the whole accumulated buffer, duplicating the carried prefix
// so a split Logon parsed as malformed and was rejected (the acceptor would never
// reach established state for a peer whose Logon spans two TLS reads).

TEST(EngineAcceptorTest, FragmentedFirstLogonAdmitted) {
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
    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};
    std::shared_ptr<fixpp::transport::TransportFactory> fac;
    auto rig = start_mtls_acceptor(ioc, engine, fixture_dir, fac);
    ASSERT_TRUE(rig.has_value()) << "acceptor rig setup failed";
    auto [acc_id, port] = *rig;

    fixpp::transport::test::LoopbackTlsFixture fixture{std::string(fixture_dir),
                                                       ioc.get_executor()};
    asio::co_spawn(ioc, run_test_initiator_fragmented(ioc, fixture, port, "INITIATOR", "ACCEPTOR"),
                   asio::detached);

    ioc.run_for(3s);
    ioc.restart();

    fixpp::session::Session* s = engine.lookup(acc_id);
    bool established = (s != nullptr) && (s->state() == fixpp::session::fsm_state::Active ||
                                          s->state() == fixpp::session::fsm_state::LogonReceived);
    std::string state_str = s ? std::to_string(static_cast<int>(s->state())) : "null";

    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    EXPECT_TRUE(established)
        << "F-015-001: a valid Logon split across two TLS reads must be reassembled "
        << "and admitted, not rejected as malformed. state=" << state_str;
}

// ── F-015-002: a coalesced Logon‖Heartbeat admits + drains the surplus ────────
// The acceptor must deliver ONLY the first frame (Logon) to the gate and carry
// the trailing Heartbeat into the read-pump. The pre-fix code returned the whole
// accumulated buffer as the "Logon" and the surplus Heartbeat was never delivered
// (next_inbound would stop at 2). With the fix the Heartbeat (seq 2) is processed
// → next_inbound advances to 3.

TEST(EngineAcceptorTest, CoalescedFirstFrameSurplusDelivered) {
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
    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};
    std::shared_ptr<fixpp::transport::TransportFactory> fac;
    auto rig = start_mtls_acceptor(ioc, engine, fixture_dir, fac);
    ASSERT_TRUE(rig.has_value()) << "acceptor rig setup failed";
    auto [acc_id, port] = *rig;

    fixpp::transport::test::LoopbackTlsFixture fixture{std::string(fixture_dir),
                                                       ioc.get_executor()};
    asio::co_spawn(ioc, run_test_initiator_coalesced(ioc, fixture, port, "INITIATOR", "ACCEPTOR"),
                   asio::detached);

    ioc.run_for(3s);
    ioc.restart();

    fixpp::session::Session* s = engine.lookup(acc_id);
    bool established = (s != nullptr) && (s->state() == fixpp::session::fsm_state::Active ||
                                          s->state() == fixpp::session::fsm_state::LogonReceived);
    int next_inbound = s ? static_cast<int>(s->seqnum_mgr_test_access().next_inbound_unsafe()) : -1;

    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    EXPECT_TRUE(established)
        << "F-015-002: the coalesced Logon must be admitted (only the first frame "
        << "delivered to the gate).";
    EXPECT_EQ(next_inbound, 3)
        << "F-015-002: the trailing Heartbeat (seq 2) coalesced into the same read "
        << "as the Logon must be drained through the read-pump — next_inbound must "
        << "advance past the post-Logon value of 2 to 3. next_inbound=" << next_inbound
        << " (==2 means the surplus was dropped — the pre-fix bug).";
}

// ── FR-005 / C7: unmatched reversed-CompID Logon is rejected, NO session ─────
//
// Static-routing default (R2): the accept loop resolves the inbound Logon by its
// reversed SenderCompID/TargetCompID; a Logon matching no registered acceptor
// SessionId is rejected at the connection level (close + session_unknown_acceptor_
// session = 121) with NO Session created — there is no fail-open path.
//
// This is the control's twin: identical harness, identical successful TLS
// handshake + well-formed Logon as OnListIdentityAdmitsToEstablished — only the
// CompIDs differ. The acceptor is registered {sender=ACCEPTOR, target=INITIATOR}
// (SessionId {FIX.4.2, ACCEPTOR, INITIATOR}). The client sends a Logon with
// SenderCompID="STRANGER", TargetCompID="NOBODY" → reversed = {FIX.4.2, sender=
// NOBODY, target=STRANGER} → resolved_id != session_id → the no-match arm
// (engine.cpp run_accept_loop: close transport + continue, slot 121) rejects the
// connection. The acceptor Session is opened EARLY (lookup-addressable), so the
// witness is NOT "no session" but "the session is never ADMITTED": it stays
// NotConnected — the non-matching peer's transport is never attached and no Logon
// is ever delivered to the gate. Contrast OnList, which reaches Active/LogonReceived.

// ── FR-005 no-match: lookup() must be nullptr after an unmatched Logon ───────
// (FQ-2 / gate-b/r1: rewritten to assert the engine.hpp:200-204 contract)
//
// Contract (engine.hpp:200-204; realized-behavior.md C1 step 6):
//   "Returns nullptr if id is … registered but not yet established (e.g.
//    acceptor with no peer yet)" AND "No match → … create NO session".
// So after an unmatched Logon is rejected:
//   - lookup(acc_id) MUST be nullptr (no session was constructed).
// A positive control (matching CompIDs → non-null + admitted) is covered by
// EngineAcceptorTest.OnListIdentityAdmitsToEstablished above.

TEST(EngineAcceptorTest, UnmatchedReversedCompIdRejectedNoSession) {
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

    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_r.has_value()) << "cert_source build failed";

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    ASSERT_TRUE(fac_r.has_value()) << "transport factory build failed";
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fac_r)};

    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};

    fixpp::session::SessionConfig acc;
    acc.sender_comp_id = "ACCEPTOR";
    acc.target_comp_id = "INITIATOR";
    acc.begin_string = "FIX.4.2";
    acc.role = fixpp::session::session_role::acceptor;
    acc.executor_override = ioc.get_executor();
    acc.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    acc.compid_authorization_policy = authz;
    acc.dictionary = fixpp::test_support::make_minimal_dictionary();
    acc.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    acc.transport_factory_override = fac;
    acc.heartbeat_interval = std::chrono::seconds{30};
    acc.logout_disconnect_timeout_ms = 2000;
    acc.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    acc.transport_send = [](std::span<const std::byte>) {};

    auto acc_id = fixpp::session::SessionId::from_config(acc);
    ASSERT_TRUE(engine.register_session(std::move(acc)).has_value());

    engine.start();
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t bound_port = engine.acceptor_bound_endpoint(acc_id).port;
    ASSERT_NE(bound_port, 0u) << "acceptor listener did not bind (port is 0)";

    // Contract witness #1: after start() but BEFORE any peer connects,
    // lookup() must be nullptr (lazy/match-gated construction). [Gate A New-3]
    EXPECT_EQ(engine.lookup(acc_id), nullptr)
        << "lookup() must be nullptr for a registered acceptor with no peer yet "
        << "(lazy + match-gated construction; engine.hpp:200-204)";

    fixpp::transport::test::LoopbackTlsFixture fixture{std::string(fixture_dir),
                                                       ioc.get_executor()};

    // Client sends a Logon whose reversed CompID matches NO registered acceptor.
    asio::co_spawn(ioc,
                   run_test_initiator(ioc, fixture, bound_port,
                                      /*sender=*/"STRANGER", /*target=*/"NOBODY"),
                   asio::detached);

    ioc.run_for(3s);
    ioc.restart();

    // Contract witness #2 (FQ-2 / gate-b/r1): after the unmatched Logon is
    // rejected, lookup(acc_id) must STILL be nullptr — no session was
    // constructed for a no-match connection per data-model C1 step 6 and
    // realized-behavior.md C7. [engine.hpp:200-204]
    EXPECT_EQ(engine.lookup(acc_id), nullptr)
        << "FR-005 / C7: no-match → no Session constructed. lookup(acc_id) must "
        << "be nullptr after an unmatched Logon is rejected. "
        << "(The accept loop closed the transport + continued; no Session was "
        << "created. Contrast OnListIdentityAdmitsToEstablished which shows the "
        << "matching-CompID path produces a non-null Session.)";

    // Clean teardown.
    auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    stop_fut.get();
}

// ── Gate A New-3: lookup() returns null before start ─────────────────────────

TEST(EngineAcceptorTest, LookupNullBeforeStart) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();

    auto harness =
        fixpp::test_support::EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
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
