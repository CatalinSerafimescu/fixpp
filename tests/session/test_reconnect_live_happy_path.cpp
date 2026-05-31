// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_reconnect_live_happy_path.cpp — T006 [P] [US1] Phase 3
//
// Live loopback-TLS reconnect happy-path witness.
//
// Anchors: FR-001/002; US1 AC1; SC-001; data-model E-1; contracts C1.
//
// Test structure:
//
//   Cell A — mock-only RED/GREEN witness.
//     RED (stub): stub returns success without calling async_connect.
//     GREEN (T009): connect_count >= 1, handshake_count >= 1.
//
//   Cell B — LIVE TLS end-to-end witness (SC-001 / FR-001 MVP).
//     Drives a REAL TLS loopback handshake via drive_reconnect_attempt(),
//     install_reconnected_transport re-enters LogonSent, feeds peer Logon-ack
//     via on_inbound_frame, and ASSERTS session.state() == fsm_state::Active.
//
//     The TLS handshake requires both client AND server sides:
//       - Client: drive_reconnect_attempt → async_connect → async_handshake
//       - Server: asio_listener::async_accept → async_handshake (server-side)
//     Both coroutines are co_spawned on the same ioc and run concurrently.
//     This is the same pattern used by the transport-layer loopback tests
//     (e.g. transport_inflight_exclusivity).
//
//     FIXPP_TLS_FIXTURE_DIR is wired unconditionally to tests/tls/fixtures/ in
//     CMakeLists.txt (same convention as transport live-TLS tests). GTEST_SKIP
//     only if the cert files are genuinely absent at runtime.
//
// SC-001 ("re-establishes a live, TLS-authenticated session … resume to Active")
// is satisfied when this cell executes and passes.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/reconnect_fsm.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

// The shared live-loopback-TLS session harness (T005 Phase 2).
// Pulls heavy concrete headers — safe here (tests/ only).
#include "loopback_tls_session_harness.hpp"
#include "support/minimal_dictionary.hpp"

// For direct LoopbackTlsFixture use (bypassing the harness Session for Cell B).
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// FIX frame helper — build a minimal well-formed Logon for on_inbound_frame.
// ─────────────────────────────────────────────────────────────────────────────
static std::string fix_field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_logon_frame(
    std::string_view begin_string,
    std::uint32_t seq,
    std::string_view sender,  // 49=
    std::string_view target)  // 56=
{
    std::string body;
    body += fix_field(35, "A");
    body += fix_field(34, std::to_string(seq));
    body += fix_field(49, sender);
    // No SendingTime(52) — harness has no clock → latency check skipped.
    body += fix_field(56, target);
    body += fix_field(98, "0");   // EncryptMethod=None
    body += fix_field(108, "30"); // HeartBtInt=30s

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
// TrackingTlsTransport — records connect/handshake calls (Cell A mock only).
// ─────────────────────────────────────────────────────────────────────────────
class TrackingTlsTransport final : public fixpp::transport::TlsTransport {
public:
    explicit TrackingTlsTransport(asio::any_io_executor exec,
                                   std::atomic<int>& connect_count,
                                   std::atomic<int>& handshake_count)
        : exec_{std::move(exec)}
        , connect_count_{connect_count}
        , handshake_count_{handshake_count}
    {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
        ++connect_count_;
        fixpp::transport::ConnectInfo info;
        info.remote = ep;
        info.local  = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>>
    async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override {
        (void)buf;
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>>
    async_write(std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        co_return buf.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override { return {}; }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::handshake_result>>
    async_handshake(fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) override {
        ++handshake_count_;
        std::pmr::memory_resource* mr = cfg.mr ? cfg.mr : std::pmr::get_default_resource();
        co_return fixpp::transport::handshake_result{
            .peer_id           = fixpp::tls::peer_identity{},
            .captured_pinset   = nullptr,
            .negotiated_cipher = std::pmr::string{"TLS_AES_128_GCM_SHA256", mr},
        };
    }

private:
    asio::any_io_executor exec_;
    std::atomic<int>& connect_count_;
    std::atomic<int>& handshake_count_;
};

// ─────────────────────────────────────────────────────────────────────────────
// TrackingFactory (Cell A mock only).
// ─────────────────────────────────────────────────────────────────────────────
class TrackingFactory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<int> make_count{0};
    std::atomic<int> connect_count{0};
    std::atomic<int> handshake_count{0};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>>
    make(asio::any_io_executor exec,
         fixpp::tls::SslCtxConfig /*ssl_cfg*/,
         std::pmr::memory_resource* /*mr*/) noexcept override
    {
        ++make_count;
        return std::make_unique<TrackingTlsTransport>(
            std::move(exec), connect_count, handshake_count);
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
class ReconnectLiveHappyPathTest : public ::testing::Test {
protected:
    asio::io_context          ioc;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        engine.executor = ioc.get_executor();
        // engine.clock is intentionally null: the harness Session will have no
        // effective_clock_, so the SendingTime latency check in on_inbound_frame
        // is skipped. This lets us feed a Logon-ack without a real timestamp.
    }

    static fixpp::transport::ReconnectPolicy make_fast_policy(std::uint32_t max_attempts) {
        fixpp::transport::ReconnectPolicy policy;
        policy.max_attempts = max_attempts;
        policy.schedule = std::pmr::vector<std::chrono::milliseconds>{
            std::pmr::get_default_resource()};
        policy.schedule.push_back(0ms);
        policy.jitter = 0.0;
        return policy;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell A — mock witness: connect+handshake are actually called.
//
// RED (stub): connect_count stays 0 → EXPECT_GE(1) FAILS.
// GREEN (T009): connect_count >= 1, handshake_count >= 1.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectLiveHappyPathTest, ConnectAndHandshakeCalledOnSuccessfulAttempt) {
    auto factory = std::make_shared<TrackingFactory>();

    fixpp::session::ReconnectFsm fsm(
        factory.get(),
        make_fast_policy(3),
        30s,
        2000ms);

    auto fut = asio::co_spawn(ioc,
        fsm.drive_reconnect_attempt(), asio::use_future);
    ioc.run_for(500ms);
    ioc.restart();

    ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
    (void)fut.get();

    EXPECT_GE(factory->connect_count.load(), 1)
        << "async_connect was never called. "
        << "RED: stub discards transport without connecting.";

    EXPECT_GE(factory->handshake_count.load(), 1)
        << "async_handshake was never called. "
        << "RED: stub never reaches the TLS handshake step.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell B — LIVE TLS end-to-end: reconnect → Active (SC-001 / FR-001 MVP).
//
// Drives a real TLS loopback handshake. Both client and server sides run
// concurrently on the same io_context:
//
//   Client coroutine (drive_reconnect_attempt):
//     factory->make() → async_connect(ep) → dynamic_cast<TlsTransport*>
//     → async_handshake(ssl_cfg) → install_reconnected_transport(session)
//     → session back in LogonSent.
//
//   Server coroutine (loopback acceptor):
//     listener->async_accept() → server_tls->async_handshake(ssl_cfg)
//     [necessary to complete the TLS handshake from the server side]
//
// After both complete:
//   - Feed peer Logon-ack via session.on_inbound_frame()
//   - Assert session.state() == fsm_state::Active   (SC-001)
//
// The fixture dir is baked in at compile time (tests/tls/fixtures/).
// GTEST_SKIP only when the cert file genuinely doesn't exist at runtime.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectLiveHappyPathTest, LiveTlsReconnectReachesActive) {
    // ── Resolve fixture dir (compile-time or env override) ─────────────────
    const char* dir_env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kCompileTimeDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kCompileTimeDir = nullptr;
#endif
    const char* fixture_dir = dir_env ? dir_env : kCompileTimeDir;
    if (!fixture_dir || fixture_dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    // Verify at least one cert file exists so we skip gracefully in minimal CI.
    {
        std::string leaf = std::string(fixture_dir) + "/leaf_rsa2048.pem";
        if (FILE* f = std::fopen(leaf.c_str(), "r")) {
            std::fclose(f);
        } else {
            GTEST_SKIP() << "leaf_rsa2048.pem absent at " << fixture_dir;
        }
    }

    // ── Build transport-layer loopback (acceptor + factory) ────────────────
    // We use LoopbackTlsFixture directly (not the session harness) so we can
    // build the Session with a permissive config for US1 testing.
    // US1 proves the TRANSPORT LAYER reconnect path; US2 adds the authorization.
    fixpp::transport::test::LoopbackTlsFixture loopback_fixture{
        std::string(fixture_dir), ioc.get_executor()};

    auto  server_ep = loopback_fixture.server_endpoint();
    auto& listener  = loopback_fixture.listener();
    auto  ssl_cfg   = loopback_fixture.ssl_cfg();

    // Build a separate TransportFactory for the Session (FR-026: SSL_CTX cached once).
    // Use the same cert/CA as the fixture.
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path        = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path   = std::string(fixture_dir) + "/ca.pem";

    auto cs_result = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_result.has_value()) << "Failed to build file_cert_source";

    fixpp::tls::SslCtxConfig session_ssl_cfg;
    session_ssl_cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;
    session_ssl_cfg.cs      = std::move(*cs_result);
    session_ssl_cfg.clock   = nullptr;
    session_ssl_cfg.caps    = fixpp::tls::CertSourceCaps{};

    auto factory_result = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, session_ssl_cfg);
    ASSERT_TRUE(factory_result.has_value()) << "Failed to build transport factory";
    auto session_factory = std::move(*factory_result);

    // Build the Session with one_way_ca profile so the authorization gate
    // takes the permissive branch (3) — no peer_identity needed.
    // US2 (T011/T012) adds mTLS binding; US1 proves the transport path only.
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id  = "INITIATOR";
    cfg.target_comp_id  = "ACCEPTOR";
    cfg.begin_string    = "FIX.4.2";
    cfg.heartbeat_interval     = std::chrono::seconds{30};
    cfg.logout_disconnect_timeout_ms = 2000;
    cfg.role            = fixpp::session::session_role::initiator;
    cfg.executor_override       = ioc.get_executor();
    // Use one_way_ca so the Logon-ack authorization gate is permissive (skip).
    // The 013 mTLS fail-closed gate (RC#A) requires either a live attached peer
    // identity or one_way_ca to avoid the fail-closed arm. US1 uses one_way_ca.
    cfg.security_profile = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::one_way_ca};
    cfg.dictionary      = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    // SessionConfig::transport_factory_override is shared_ptr (013 T011); move
    // unique_ptr into shared_ptr.
    auto session_factory_shared = std::shared_ptr<fixpp::transport::TransportFactory>{
        std::move(session_factory)};
    cfg.transport_factory_override = session_factory_shared;
    cfg.reconnect_endpoint = server_ep;

    fixpp::session::Session session{engine, cfg};

    // ── Open session → LogonSent ────────────────────────────────────────────
    {
        auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
        ioc.run_for(1s);
        ioc.restart();

        ASSERT_EQ(open_fut.wait_for(0s), std::future_status::ready)
            << "Session::open() did not complete.";
        auto open_r = open_fut.get();
        ASSERT_TRUE(open_r.has_value())
            << "Session::open() failed: " << static_cast<int>(open_r.error());
    }

    // ── Concurrent: drive_reconnect_attempt (client) + accept+handshake (server) ──
    //
    // The TLS handshake requires both sides to participate concurrently.
    // We spawn both coroutines on the same io_context and let ioc.run_for()
    // drive them to completion. This matches the transport-layer test pattern
    // (see make_handshaken_pair in test_inflight_exclusivity.cpp).

    // Reconnect FSM — drives the client side (connect + handshake).
    fixpp::session::ReconnectFsm reconnect_fsm(
        session_factory_shared.get(),
        make_fast_policy(3),
        30s,
        2000ms);
    reconnect_fsm.set_reconnect_endpoint(server_ep);
    reconnect_fsm.set_session_owner(&session);
    // Session was built with mtls_ca profile (for the factory's SSL_CTX setup).
    // Set the FSM's TLS profile so async_handshake doesn't reject with
    // transport_psk_unsupported (SecurityProfile::unset would fail).
    reconnect_fsm.set_tls_profile(fixpp::tls::SecurityProfile::mtls_ca);

    // Spawn client-side: drive_reconnect_attempt.
    auto client_fut = asio::co_spawn(
        ioc, reconnect_fsm.drive_reconnect_attempt(), asio::use_future);

    // Spawn server-side: accept the incoming TCP connection then drive the
    // server-side TLS handshake. Without this the client's async_handshake
    // will block forever (TLS requires both sides).
    asio::co_spawn(
        ioc,
        [&listener, &ssl_cfg]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(
                asio::enable_total_cancellation());
            // async_accept returns expected_t<unique_ptr<Transport>>
            auto accept_r = co_await listener.async_accept();
            if (!accept_r.has_value()) co_return;
            auto& transport_ptr = *accept_r;
            auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(
                transport_ptr.get());
            if (tls) {
                // Drive the server-side TLS handshake. Result is discarded;
                // if it fails, the client handshake will also fail.
                (void)co_await tls->async_handshake(ssl_cfg);
            }
        },
        asio::detached);

    // Run both coroutines. 5s is ample for a loopback TLS handshake.
    ioc.run_for(5s);
    ioc.restart();

    // ── Assert drive_reconnect_attempt succeeded ────────────────────────────
    ASSERT_EQ(client_fut.wait_for(0s), std::future_status::ready)
        << "drive_reconnect_attempt() did not complete within 5s.";

    auto drive_r = client_fut.get();
    ASSERT_TRUE(drive_r.has_value())
        << "drive_reconnect_attempt() failed with error: "
        << static_cast<int>(drive_r.error());

    // install_reconnected_transport() has been called → LogonSent.
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::LogonSent)
        << "After successful reconnect, session should be in LogonSent.";

    // ── Feed peer Logon-ack → Active ────────────────────────────────────────
    // Harness SessionConfig:
    //   sender_comp_id = "INITIATOR", target_comp_id = "ACCEPTOR"
    //   begin_string   = "FIX.4.2"
    //   reset_seqnum_policy = bilateral_lenient (no 141=Y required)
    //   clock = null → SendingTime latency check skipped
    auto logon_ack = make_logon_frame(
        "FIX.4.2",
        1,           // seq=1
        "ACCEPTOR",  // 49= (peer sender = our target)
        "INITIATOR"  // 56= (peer target = our sender)
    );

    {
        auto feed_fut = asio::co_spawn(
            ioc,
            session.on_inbound_frame(std::span<const std::byte>{logon_ack}),
            asio::use_future);

        ioc.run_for(1s);
        ioc.restart();

        ASSERT_EQ(feed_fut.wait_for(0s), std::future_status::ready)
            << "on_inbound_frame(Logon-ack) did not complete.";
        (void)feed_fut.get();
    }

    // ── SC-001: session must be Active ──────────────────────────────────────
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Active)
        << "Session did not reach Active after reconnect + Logon-ack. "
        << "SC-001: live TLS reconnect must resume the session to Active.";
}

}  // namespace
