// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_live_identity_binding.cpp — T011 [P] [US2] Phase 4
//
// On the live initiator reconnect path the identity passed to
// CompIdAuthorizationPolicy::authorize() is the real handshake_result.peer_id
// from the completed TLS handshake — no fabricated/stand-in identity.
//
// SC-003 / FR-006 / US2 AC1 / I-4 / data-model E-2.
//
// Test structure:
//
//   Cell A (MOCK) — identity-interception witness using a tracking factory.
//     The mock handshake injects CN="PEER-PROD-01" in peer_id.
//     The policy binds CN="PEER-PROD-01" → "ACCEPTOR".
//     No logon_peer_identity_override; mTLS profile.
//
//     RED: the live path currently ignores hr.peer_id; arm (2) mTLS fail-closed
//          fires → session never reaches Active; EXPECT_EQ(Active) FAILS.
//     GREEN after T014/T015: arm (1-live) uses hr.peer_id → CN="PEER-PROD-01"
//          → authorized → session reaches Active.
//
//   Cell B (LIVE TLS) — full loopback: the live cert CN of leaf_rsa2048.pem
//     drives authorize(). Policy binds "fixpp-test-leaf" → "ACCEPTOR".
//     No logon_peer_identity_override; mTLS profile.
//
//     RED: arm (2) fail-closed (mTLS without override, live arm not yet wired).
//     GREEN after T014/T015: real cert CN → policy lookup → admitted → Active.
//
// Anchors: FR-006; US2 AC1; SC-003; I-4; data-model E-2; contracts C2.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
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
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/reconnect_fsm.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "support/minimal_dictionary.hpp"
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// FIX frame helper
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
// IdentityInjectingTlsTransport — injects a peer_identity with known CN into
// the handshake result. Simulates what the live TLS path would yield.
// ─────────────────────────────────────────────────────────────────────────────
class IdentityInjectingTlsTransport final
    : public fixpp::transport::TlsTransport {
public:
    explicit IdentityInjectingTlsTransport(asio::any_io_executor exec,
                                            std::string injected_cn)
        : exec_{std::move(exec)}
        , injected_cn_{std::move(injected_cn)}
    {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
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

    // Injects a peer_identity with injected_cn_ as the subject DN (CN= format).
    // The authorize() call extracts CN from the subject_dn — this is the
    // first-wins canonical extraction order (FR-022 / D-8).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::handshake_result>>
    async_handshake(fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) override {
        std::pmr::memory_resource* mr = cfg.mr ? cfg.mr : std::pmr::get_default_resource();

        fixpp::tls::peer_identity pid;
        // subject_dn in "CN=<value>" format so parse_cn_from_dn_local extracts it.
        pid.subject_dn = std::pmr::string{"CN=" + injected_cn_, mr};
        pid.leaf_fingerprint = {};

        co_return fixpp::transport::handshake_result{
            .peer_id           = std::move(pid),
            .captured_pinset   = nullptr,
            .negotiated_cipher = std::pmr::string{"TLS_AES_128_GCM_SHA256", mr},
        };
    }

private:
    asio::any_io_executor exec_;
    std::string injected_cn_;
};

// ─────────────────────────────────────────────────────────────────────────────
// IdentityInjectingFactory
// ─────────────────────────────────────────────────────────────────────────────
class IdentityInjectingFactory final : public fixpp::transport::TransportFactory {
public:
    explicit IdentityInjectingFactory(std::string injected_cn)
        : injected_cn_{std::move(injected_cn)} {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>>
    make(asio::any_io_executor exec,
         fixpp::tls::SslCtxConfig /*ssl_cfg*/,
         std::pmr::memory_resource* /*mr*/) noexcept override
    {
        return std::make_unique<IdentityInjectingTlsTransport>(
            std::move(exec), injected_cn_);
    }

    [[nodiscard]] fixpp::core::expected_t<void>
    reload_credentials(std::shared_ptr<fixpp::tls::cert_source> /*s*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source>
    cert_source_snapshot() const noexcept override { return nullptr; }

private:
    std::string injected_cn_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────
class LiveIdentityBindingTest : public ::testing::Test {
protected:
    asio::io_context          ioc;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        engine.executor = ioc.get_executor();
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

    // Build a session reaching LogonSent via drive_reconnect_attempt.
    // Returns the ReconnectFsm (must stay alive through the Logon-ack feed).
    template<typename FactoryT>
    static std::unique_ptr<fixpp::session::ReconnectFsm>
    drive_to_logon_sent(asio::io_context& ioc,
                        fixpp::session::Session& session,
                        FactoryT* factory,
                        fixpp::transport::Endpoint ep)
    {
        auto fsm = std::make_unique<fixpp::session::ReconnectFsm>(
            factory,
            make_fast_policy(3),
            30s,
            2000ms);
        fsm->set_reconnect_endpoint(ep);
        fsm->set_session_owner(&session);
        fsm->set_tls_profile(fixpp::tls::SecurityProfile::mtls_ca);
        return fsm;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Cell A (mock) — injected CN="PEER-PROD-01" in handshake result must drive
// the live-identity arm and reach Active.
//
// RED: T014/T015 not implemented → arm (2) mTLS fail-closed → Disconnected.
// GREEN after T014/T015: live peer_id CN drives authorize() → Active.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(LiveIdentityBindingTest, MockHandshakeIdentityDrivesAuthorization) {
    // Policy: CN="PEER-PROD-01" → allowed for CompID "ACCEPTOR".
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("PEER-PROD-01", "ACCEPTOR");

    auto factory = std::make_shared<IdentityInjectingFactory>("PEER-PROD-01");

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id  = "INITIATOR";
    cfg.target_comp_id  = "ACCEPTOR";
    cfg.begin_string    = "FIX.4.2";
    cfg.heartbeat_interval     = std::chrono::seconds{30};
    cfg.logout_disconnect_timeout_ms = 2000;
    cfg.role            = fixpp::session::session_role::initiator;
    cfg.executor_override       = ioc.get_executor();
    // mTLS: the live-identity arm (1-live) must fire (not arm 2 fail-closed).
    cfg.security_profile = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::mtls_ca};
    cfg.compid_authorization_policy = std::move(policy);
    cfg.dictionary      = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    // NO logon_peer_identity_override — live handshake must supply identity.
    cfg.transport_factory_override = factory;
    // Dummy endpoint (mock never really connects).
    cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19876};

    fixpp::session::Session session{engine, cfg};

    // open() will fail (no real server), but that sets up internal state.
    {
        auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(open_fut.wait_for(0s), std::future_status::ready);
        (void)open_fut.get();
    }

    // Drive reconnect via a standalone FSM (bypasses the initial open() path).
    fixpp::session::ReconnectFsm reconnect_fsm(
        factory.get(),
        make_fast_policy(3),
        30s,
        2000ms);
    reconnect_fsm.set_reconnect_endpoint(fixpp::transport::Endpoint{"127.0.0.1", 19876});
    reconnect_fsm.set_session_owner(&session);
    reconnect_fsm.set_tls_profile(fixpp::tls::SecurityProfile::mtls_ca);

    auto drive_fut = asio::co_spawn(
        ioc, reconnect_fsm.drive_reconnect_attempt(), asio::use_future);
    ioc.run_for(2s);
    ioc.restart();

    ASSERT_EQ(drive_fut.wait_for(0s), std::future_status::ready);
    auto drive_r = drive_fut.get();

    // The mock always connects+handshakes successfully.
    ASSERT_TRUE(drive_r.has_value())
        << "drive_reconnect_attempt failed unexpectedly (mock factory). "
        << "Error: " << static_cast<int>(drive_r.error());

    // install_reconnected_transport puts the session into LogonSent.
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::LogonSent)
        << "After successful reconnect the session must be in LogonSent.";

    // Feed the Logon-ack. The three-way guard in on_inbound_frame runs:
    // arm (1-live, T015): uses hr.peer_id CN="PEER-PROD-01" → authorize() OK →
    //   session_event_peer_identity_bound → Active.
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

    // SC-003 / I-4: no fabricated identity — the real hr.peer_id CN drives auth.
    // RED: arm (2) mTLS fail-closed (T014/T015 not yet wired) → Disconnected.
    // GREEN after T014/T015: arm (1-live) → CN="PEER-PROD-01" → Active.
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Active)
        << "Session must reach Active using the live handshake peer_id. "
        << "RED (T014/T015 not yet implemented): mTLS without override → arm (2) "
        << "fail-closed → Disconnected (not Active). "
        << "GREEN after T014/T015: arm (1-live) fires with CN='PEER-PROD-01' → "
        << "authorize() succeeds → Active. "
        << "SC-003: no fabricated/stand-in identity on the live initiator path.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell B (LIVE TLS) — real leaf cert CN from loopback handshake drives auth.
//
// Policy binds CN="fixpp-test-leaf" → "ACCEPTOR" (the test cert subject CN).
// No logon_peer_identity_override; mTLS profile.
//
// RED: arm (2) mTLS fail-closed (live arm not yet wired) → Disconnected.
// GREEN after T014/T015: real cert CN → policy lookup → admitted → Active.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(LiveIdentityBindingTest, LiveTlsCertCnDrivesAuthorizationDecision) {
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
    {
        std::string leaf = std::string(fixture_dir) + "/leaf_rsa2048.pem";
        if (FILE* f = std::fopen(leaf.c_str(), "r")) {
            std::fclose(f);
        } else {
            GTEST_SKIP() << "leaf_rsa2048.pem absent at " << fixture_dir;
        }
    }

    // Build the loopback TLS fixture (provides server-side accept+handshake).
    fixpp::transport::test::LoopbackTlsFixture loopback_fixture{
        std::string(fixture_dir), ioc.get_executor()};

    auto  server_ep = loopback_fixture.server_endpoint();
    auto& listener  = loopback_fixture.listener();
    auto  ssl_cfg   = loopback_fixture.ssl_cfg();

    // Session-side factory using the same certs.
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

    auto factory_result = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, session_ssl_cfg);
    ASSERT_TRUE(factory_result.has_value()) << "Failed to build transport factory";
    auto session_factory_shared = std::shared_ptr<fixpp::transport::TransportFactory>{
        std::move(*factory_result)};

    // Policy: CN="fixpp-leaf-rsa2048" → allowed for "ACCEPTOR".
    // The test cert subject CN from tests/tls/fixtures/leaf_rsa2048.pem is
    // "fixpp-leaf-rsa2048" (verified: openssl x509 -noout -subject -in leaf_rsa2048.pem).
    fixpp::session::CompIdAuthorizationPolicy live_policy;
    live_policy.add_binding("fixpp-leaf-rsa2048", "ACCEPTOR");

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id  = "INITIATOR";
    cfg.target_comp_id  = "ACCEPTOR";
    cfg.begin_string    = "FIX.4.2";
    cfg.heartbeat_interval     = std::chrono::seconds{30};
    cfg.logout_disconnect_timeout_ms = 2000;
    cfg.role            = fixpp::session::session_role::initiator;
    cfg.executor_override       = ioc.get_executor();
    // mTLS: the live-identity arm (1-live) must fire, not arm (2) fail-closed.
    cfg.security_profile = fixpp::session::SecurityProfile{
        fixpp::session::SecurityProfile::kind::mtls_ca};
    cfg.compid_authorization_policy = std::move(live_policy);
    cfg.dictionary      = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    // NO logon_peer_identity_override — live handshake must supply the identity.
    cfg.transport_factory_override = session_factory_shared;
    cfg.reconnect_endpoint = server_ep;

    fixpp::session::Session session{engine, cfg};

    // Open (may succeed or fail; we only need internal state set up).
    {
        auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        ASSERT_EQ(open_fut.wait_for(0s), std::future_status::ready);
        (void)open_fut.get();
    }

    // Drive client + server sides concurrently.
    fixpp::session::ReconnectFsm reconnect_fsm(
        session_factory_shared.get(),
        make_fast_policy(3),
        30s,
        2000ms);
    reconnect_fsm.set_reconnect_endpoint(server_ep);
    reconnect_fsm.set_session_owner(&session);
    reconnect_fsm.set_tls_profile(fixpp::tls::SecurityProfile::mtls_ca);

    auto client_fut = asio::co_spawn(
        ioc, reconnect_fsm.drive_reconnect_attempt(), asio::use_future);

    asio::co_spawn(
        ioc,
        [&listener, &ssl_cfg]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(
                asio::enable_total_cancellation());
            auto accept_r = co_await listener.async_accept();
            if (!accept_r.has_value()) co_return;
            auto& transport_ptr = *accept_r;
            auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(
                transport_ptr.get());
            if (tls) {
                (void)co_await tls->async_handshake(ssl_cfg);
            }
        },
        asio::detached);

    ioc.run_for(5s);
    ioc.restart();

    ASSERT_EQ(client_fut.wait_for(0s), std::future_status::ready)
        << "drive_reconnect_attempt did not complete within 5s.";
    auto drive_r = client_fut.get();
    ASSERT_TRUE(drive_r.has_value())
        << "drive_reconnect_attempt failed: " << static_cast<int>(drive_r.error());

    ASSERT_EQ(session.state(), fixpp::session::fsm_state::LogonSent)
        << "After successful reconnect the session must be in LogonSent.";

    // Feed Logon-ack to trigger the authorize() guard.
    auto logon_ack = make_logon_frame("FIX.4.2", 1, "ACCEPTOR", "INITIATOR");
    {
        auto feed_fut = asio::co_spawn(
            ioc,
            session.on_inbound_frame(std::span<const std::byte>{logon_ack}),
            asio::use_future);
        ioc.run_for(1s);
        ioc.restart();
        ASSERT_EQ(feed_fut.wait_for(0s), std::future_status::ready);
        (void)feed_fut.get();
    }

    // SC-003 / I-4: real cert CN must drive authorize() — no fabricated identity.
    // RED: arm (2) mTLS fail-closed (live arm not yet wired) → Disconnected.
    // GREEN after T014/T015: real cert CN="fixpp-test-leaf" → admitted → Active.
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Active)
        << "Session must reach Active using the live TLS cert CN='fixpp-test-leaf'. "
        << "RED (T014/T015 not yet): mTLS without override → arm (2) fail-closed. "
        << "GREEN after T014/T015: live peer_id from handshake → authorize() → Active.";
}

}  // namespace
