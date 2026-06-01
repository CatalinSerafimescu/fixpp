// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_tls_validation_failed_taxonomy.cpp — 013 Phase 5 T038.
//
// Tests that `asio_listener::recent_events()` carries a matching
// `session_event_tls_validation_failed{code, sub_reason, ...}` after a
// server-side TLS handshake is rejected by verify_peer.
//
// 5 master-enum cells (of 6) exercisable via the real TLS path:
//   1. tls_handshake_failed + sub_reason="expired"       → leaf_expired.pem client
//   2. tls_rsa_key_too_large                              → leaf_rsa16384_negative.pem
//   3. tls_cert_der_too_large                             → leaf_der_16KiB_negative.pem
//   4. tls_san_entries_exceeded                           → leaf_san_65_negative.pem
//   5. tls_pin_mismatch                                   → valid cert, wrong pin
//
// NOT EXERCISABLE via listener-event path:
//   tls_load_cancelled — this fires at factory construction (load_credentials
//   failure), before any TCP accept. No handshake → no session_event emission.
//   See reporting note in test body.
//
// 3 sub_reason cells within the tls_handshake_failed GROUPING:
//   A. sub_reason="expired"           → tls_handshake_failed code
//   B. sub_reason="tls_pin_empty_at_open" → tls_pin_empty_at_open code
//      (verify_peer returns this distinct code when pinset_snapshot is null
//       under mtls_pinned; no thread-local sub_reason is set — the code IS the discriminator)
//   C. sub_reason="sigalg_disallowed" → tls_handshake_failed code
//      (requires a cert with a non-RSA/ECDSA key — not available in fixtures;
//       covered at the unit-test level in test_verify_peer_t039.cpp)
//
// Design anchors: FR-026, FR-027, FR-028, D-15; data-model §E-5/§E-6;
// session_event_tls_validation_failed; 013 Phase 5 T038.
//
// Guards with FIXPP_TLS_FIXTURE_DIR — skipped when certs not present.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/listener_events.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>

#include "transport/asio_listener.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::session::session_event_tls_validation_failed;
using fixpp::session::SessionEvent;
using fixpp::transport::asio_listener;
using fixpp::transport::Endpoint;
using fixpp::transport::TlsTransport;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string g_fixture_dir;

// Build a file_cert_source from the given leaf/key files and the standard ca.pem.
std::shared_ptr<fixpp::tls::cert_source> make_client_cs(std::string const& leaf_pem,
                                                        std::string const& key_pem) {
    fixpp::tls::file_cert_source::Config cs;
    cs.leaf_path = leaf_pem;
    cs.private_key_path = key_pem;
    cs.ca_bundle_path = g_fixture_dir + "/ca.pem";
    auto r =
        fixpp::tls::file_cert_source::make_file_cert_source(cs, std::pmr::new_delete_resource());
    if (!r.has_value()) {
        return nullptr;
    }
    return std::move(*r);
}

// Build a server-side listener with the standard leaf_rsa2048 cert.
// Returns {listener, bound_port}.
struct ListenerHandle {
    std::unique_ptr<asio_listener> listener;
    std::uint16_t port{0};
};

ListenerHandle make_server_listener(asio::any_io_executor exec,
                                    fixpp::tls::SslCtxConfig server_ssl_cfg) {
    asio_listener::Config cfg;
    cfg.bind_endpoint = Endpoint{"127.0.0.1", 0, /*backlog=*/4};
    cfg.ssl_cfg = std::move(server_ssl_cfg);
    auto* raw = new asio_listener{exec, std::move(cfg)};
    ListenerHandle h;
    h.port = raw->bound_endpoint().port;
    h.listener = std::unique_ptr<asio_listener>{raw};
    return h;
}

// Run one TLS handshake failure scenario:
//   1. Server starts an async_accept.
//   2. Client connects with a failing cert profile.
//   3. Both sides attempt async_handshake — server's verify_peer rejects.
//   4. Return the server's async_handshake error code and the listener's events.
//
// `client_ssl_cfg` is the config the client uses; `server_ssl_cfg` is what
// the server uses (determines verify policy).
//
// STRING LIFETIME NOTE: session_event_tls_validation_failed's string_view fields
// (sub_reason, peer_endpoint, reason_string) point into the ListenerEvents'
// owning string arrays. These views are valid for the listener's lifetime.
// To access them after the listener is destroyed, the caller must copy the
// string content before the listener goes out of scope. ScenarioResult uses
// owned std::string copies, not views. [data-model §E-5]
struct TlsValidationEvent {
    error code;
    std::string sub_reason;
    std::string peer_endpoint;
    std::string reason_string;
};

struct ScenarioResult {
    error server_hs_error{error::transport_accept_cancelled};
    std::vector<TlsValidationEvent> events;
};

ScenarioResult run_failure_scenario(fixpp::tls::SslCtxConfig server_ssl_cfg,
                                    fixpp::tls::SslCtxConfig client_ssl_cfg) {
    asio::io_context ioc;

    auto server = make_server_listener(ioc.get_executor(), server_ssl_cfg);
    const auto port = server.port;

    ScenarioResult result;

    // Server coroutine: accept + handshake (expected to fail at verify_peer).
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto ar = co_await server.listener->async_accept();
            if (!ar.has_value()) {
                co_return;
            }
            auto* tls = dynamic_cast<TlsTransport*>(ar->get());
            if (!tls) {
                co_return;
            }
            auto hs = co_await tls->async_handshake(server_ssl_cfg);
            if (!hs.has_value()) {
                result.server_hs_error = hs.error();
            }
            // Collect listener events — copy string_view fields into owned
            // strings WHILE the listener (and its ListenerEvents) is still alive.
            // string_view fields become dangling after the listener is destroyed.
            auto span = server.listener->recent_events();
            for (const auto& ev : span) {
                if (const auto* p = std::get_if<session_event_tls_validation_failed>(&ev)) {
                    TlsValidationEvent e;
                    e.code = p->code;
                    e.sub_reason = std::string{p->sub_reason};
                    e.peer_endpoint = std::string{p->peer_endpoint};
                    e.reason_string = std::string{p->reason_string};
                    result.events.push_back(std::move(e));
                }
            }
        },
        asio::detached);

    // Client coroutine: connect + handshake with the "bad" cert.
    auto client_factory = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, client_ssl_cfg);
    if (!client_factory.has_value()) {
        return result;
    }
    auto client_transport = (*client_factory)->make(ioc.get_executor(), client_ssl_cfg, nullptr);
    if (!client_transport.has_value()) {
        return result;
    }
    auto* client_tls = dynamic_cast<TlsTransport*>(client_transport->get());
    if (!client_tls) {
        return result;
    }

    asio::co_spawn(
        ioc.get_executor(),
        [&, client_ssl = client_ssl_cfg]() -> asio::awaitable<void> {
            Endpoint ep{"127.0.0.1", port, 0};
            auto conn = co_await (*client_transport)->async_connect(ep);
            if (!conn.has_value()) {
                co_return;
            }
            // Client side: ignoring the handshake result (server will reject us).
            (void)co_await client_tls->async_handshake(client_ssl);
        },
        asio::detached);

    // Drive for up to 10 s.
    ioc.run_for(std::chrono::seconds{10});

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test fixtures
// ─────────────────────────────────────────────────────────────────────────────

// Standard server TLS config: mtls_ca, leaf_rsa2048, ca.pem trust.
fixpp::tls::SslCtxConfig make_server_ssl_cfg() {
    fixpp::tls::file_cert_source::Config cs;
    cs.leaf_path = g_fixture_dir + "/leaf_rsa2048.pem";
    cs.private_key_path = g_fixture_dir + "/leaf_rsa2048.key";
    cs.ca_bundle_path = g_fixture_dir + "/ca.pem";
    auto r =
        fixpp::tls::file_cert_source::make_file_cert_source(cs, std::pmr::new_delete_resource());
    fixpp::tls::SslCtxConfig cfg;
    cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;
    cfg.cs = (r.has_value()) ? std::move(*r) : nullptr;
    cfg.clock = nullptr;  // skip expiry on server cert
    return cfg;
}

// Client ssl_cfg using the given cert_source.
fixpp::tls::SslCtxConfig make_client_ssl_cfg(
    std::shared_ptr<fixpp::tls::cert_source> cs,
    fixpp::tls::SecurityProfile profile = fixpp::tls::SecurityProfile::mtls_ca,
    std::shared_ptr<fixpp::core::Clock> clock = nullptr,
    std::shared_ptr<fixpp::tls::Pinset> pinset = nullptr) {
    fixpp::tls::SslCtxConfig cfg;
    cfg.profile = profile;
    cfg.cs = std::move(cs);
    cfg.clock = std::move(clock);
    cfg.pinset = std::move(pinset);
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1: tls_handshake_failed + sub_reason="expired"
// Client presents leaf_expired.pem. Server has a Clock so expiry is checked.
// ─────────────────────────────────────────────────────────────────────────────
TEST(TlsValidationFailedTaxonomy, ExpiredCertEmitsEvent) {
    if (g_fixture_dir.empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    // Server needs a clock so verify_peer evaluates expiry.
    struct RealClock final : fixpp::core::Clock {
        fixpp::core::utc_time_point now() const noexcept override {
            return std::chrono::system_clock::now();
        }
        fixpp::core::steady_time_point steady_now() const noexcept override {
            return std::chrono::steady_clock::now();
        }
        asio::awaitable<void> sleep_until(fixpp::core::steady_time_point) override { co_return; }
        void cancel_sleeps() noexcept override {}
    };

    auto server_cfg = make_server_ssl_cfg();
    server_cfg.clock = std::make_shared<RealClock>();

    auto client_cs =
        make_client_cs(g_fixture_dir + "/leaf_expired.pem", g_fixture_dir + "/leaf_expired.key");
    if (!client_cs) {
        GTEST_SKIP() << "leaf_expired.pem not available";
    }
    auto client_cfg = make_client_ssl_cfg(std::move(client_cs));
    client_cfg.clock = std::make_shared<RealClock>();

    auto result = run_failure_scenario(server_cfg, client_cfg);

    // Server handshake must have failed.
    EXPECT_EQ(result.server_hs_error, error::transport_handshake_failed)
        << "Server must reject expired cert with transport_handshake_failed";

    // Listener ring must contain a matching event.
    ASSERT_FALSE(result.events.empty())
        << "asio_listener::recent_events() must contain session_event_tls_validation_failed";

    const auto& ev = result.events.front();
    EXPECT_EQ(ev.code, error::tls_handshake_failed)
        << "event code must be tls_handshake_failed for expired cert";
    EXPECT_EQ(ev.sub_reason, "expired") << "sub_reason must be 'expired'";
    EXPECT_FALSE(ev.peer_endpoint.empty()) << "peer_endpoint must be non-empty (host:port)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2: tls_rsa_key_too_large
// Client presents leaf_rsa16384_negative.pem (16384-bit RSA, > max 8192).
// ─────────────────────────────────────────────────────────────────────────────
TEST(TlsValidationFailedTaxonomy, RsaKeyTooLargeEmitsEvent) {
    if (g_fixture_dir.empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto server_cfg = make_server_ssl_cfg();

    auto client_cs = make_client_cs(g_fixture_dir + "/leaf_rsa16384_negative.pem",
                                    g_fixture_dir + "/leaf_rsa16384_negative.key");
    if (!client_cs) {
        GTEST_SKIP() << "leaf_rsa16384_negative.pem not available";
    }
    auto client_cfg = make_client_ssl_cfg(std::move(client_cs));

    auto result = run_failure_scenario(server_cfg, client_cfg);

    EXPECT_EQ(result.server_hs_error, error::transport_handshake_failed)
        << "Server must reject RSA-key-too-large cert";

    ASSERT_FALSE(result.events.empty())
        << "Listener ring must contain session_event_tls_validation_failed";

    const auto& ev = result.events.front();
    EXPECT_EQ(ev.code, error::tls_rsa_key_too_large) << "event code must be tls_rsa_key_too_large";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 3: tls_cert_der_too_large
// Client presents leaf_der_16KiB_negative.pem (DER > 16 KiB cap).
// ─────────────────────────────────────────────────────────────────────────────
TEST(TlsValidationFailedTaxonomy, CertDerTooLargeEmitsEvent) {
    if (g_fixture_dir.empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto server_cfg = make_server_ssl_cfg();

    auto client_cs = make_client_cs(g_fixture_dir + "/leaf_der_16KiB_negative.pem",
                                    g_fixture_dir + "/leaf_der_16KiB_negative.key");
    if (!client_cs) {
        GTEST_SKIP() << "leaf_der_16KiB_negative.pem not available";
    }
    auto client_cfg = make_client_ssl_cfg(std::move(client_cs));

    auto result = run_failure_scenario(server_cfg, client_cfg);

    EXPECT_EQ(result.server_hs_error, error::transport_handshake_failed)
        << "Server must reject DER-too-large cert";

    ASSERT_FALSE(result.events.empty())
        << "Listener ring must contain session_event_tls_validation_failed";

    const auto& ev = result.events.front();
    EXPECT_EQ(ev.code, error::tls_cert_der_too_large)
        << "event code must be tls_cert_der_too_large";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 4: tls_san_entries_exceeded
// Client presents leaf_san_65_negative.pem (65 SANs, exceeds default max of 64).
// ─────────────────────────────────────────────────────────────────────────────
TEST(TlsValidationFailedTaxonomy, SanEntriesExceededEmitsEvent) {
    if (g_fixture_dir.empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto server_cfg = make_server_ssl_cfg();

    auto client_cs = make_client_cs(g_fixture_dir + "/leaf_san_65_negative.pem",
                                    g_fixture_dir + "/leaf_san_65_negative.key");
    if (!client_cs) {
        GTEST_SKIP() << "leaf_san_65_negative.pem not available";
    }
    auto client_cfg = make_client_ssl_cfg(std::move(client_cs));

    auto result = run_failure_scenario(server_cfg, client_cfg);

    EXPECT_EQ(result.server_hs_error, error::transport_handshake_failed)
        << "Server must reject cert with too many SAN entries";

    ASSERT_FALSE(result.events.empty())
        << "Listener ring must contain session_event_tls_validation_failed";

    const auto& ev = result.events.front();
    EXPECT_EQ(ev.code, error::tls_san_entries_exceeded)
        << "event code must be tls_san_entries_exceeded";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 5: tls_pin_mismatch
// Server uses mtls_pinned; client presents valid cert but wrong pin in Pinset.
// ─────────────────────────────────────────────────────────────────────────────
TEST(TlsValidationFailedTaxonomy, PinMismatchEmitsEvent) {
    if (g_fixture_dir.empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    // Build a Pinset with a dummy (wrong) pin so the valid client cert mismatches.
    auto pinset = std::make_shared<fixpp::tls::Pinset>();
    fixpp::tls::Certificate dummy{};
    dummy.sha256_[0] = std::byte{0xAB};
    dummy.sha256_[1] = std::byte{0xCD};
    dummy.x509_version_ = 3;
    (void)pinset->add(dummy);

    auto server_cfg = make_server_ssl_cfg();
    server_cfg.profile = fixpp::tls::SecurityProfile::mtls_pinned;
    server_cfg.pinset = pinset;

    // Client uses the valid standard cert.
    auto client_cs =
        make_client_cs(g_fixture_dir + "/leaf_rsa2048.pem", g_fixture_dir + "/leaf_rsa2048.key");
    if (!client_cs) {
        GTEST_SKIP() << "leaf_rsa2048.pem not available";
    }
    auto client_cfg = make_client_ssl_cfg(std::move(client_cs));

    auto result = run_failure_scenario(server_cfg, client_cfg);

    EXPECT_EQ(result.server_hs_error, error::transport_handshake_failed)
        << "Server must reject cert not in pinset";

    ASSERT_FALSE(result.events.empty())
        << "Listener ring must contain session_event_tls_validation_failed";

    const auto& ev = result.events.front();
    EXPECT_EQ(ev.code, error::tls_pin_mismatch) << "event code must be tls_pin_mismatch";
}

// ─────────────────────────────────────────────────────────────────────────────
// Sub-reason Cell B: tls_pin_empty_at_open
// Server uses mtls_pinned but pinset_snapshot is null (empty Pinset with no
// snapshot captured). verify_peer returns tls_pin_empty_at_open — the code IS
// the discriminator; thread-local sub_reason is not set for this branch.
// ─────────────────────────────────────────────────────────────────────────────
TEST(TlsValidationFailedTaxonomy, PinEmptyAtOpenEmitsEvent) {
    if (g_fixture_dir.empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    // Server uses mtls_pinned but with an empty pinset → pinset_snapshot will
    // be a non-null pointer to an EMPTY vector, meaning no pins to match against.
    // Actually: to get tls_pin_empty_at_open, pinset_snapshot must be nullptr.
    // The snapshot is captured from Pinset::snapshot() at handshake start.
    // An empty Pinset gives a non-null snapshot (empty vector), not nullptr.
    // tls_pin_empty_at_open fires when pinset_snapshot == nullptr.
    // In the current code: if (cfg.pinset) { captured_pinset_ = cfg.pinset->snapshot(); }
    // If cfg.pinset == nullptr → captured_pinset_ stays nullptr → passed to verify_peer
    // augmented_cfg.pinset_snapshot = nullptr → tls_pin_empty_at_open fires.
    // So: set server profile=mtls_pinned but leave server_cfg.pinset = nullptr.
    auto server_cfg = make_server_ssl_cfg();
    server_cfg.profile = fixpp::tls::SecurityProfile::mtls_pinned;
    // Do NOT set server_cfg.pinset — it stays nullptr → pinset_snapshot=nullptr
    // → verify_peer returns tls_pin_empty_at_open.

    auto client_cs =
        make_client_cs(g_fixture_dir + "/leaf_rsa2048.pem", g_fixture_dir + "/leaf_rsa2048.key");
    if (!client_cs) {
        GTEST_SKIP() << "leaf_rsa2048.pem not available";
    }
    auto client_cfg = make_client_ssl_cfg(std::move(client_cs));

    auto result = run_failure_scenario(server_cfg, client_cfg);

    EXPECT_EQ(result.server_hs_error, error::transport_handshake_failed)
        << "Server must reject (empty pinset at open)";

    ASSERT_FALSE(result.events.empty())
        << "Listener ring must contain session_event_tls_validation_failed";

    const auto& ev = result.events.front();
    EXPECT_EQ(ev.code, error::tls_pin_empty_at_open)
        << "event code must be tls_pin_empty_at_open (FR-027 distinct discriminator)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Sub-reason Cell C NOTE: tls_handshake_failed + sub_reason="sigalg_disallowed"
//
// T019/T020 analysis (014 FR-012): The leaf_ed25519.pem fixture exists (T019
// done). However, the sigalg_disallowed rejection CANNOT be exercised via the
// full asio_listener path:
//
// Root cause: fixpp's setup_ssl_ctx_ sets SSL_CTX_set1_sigalgs_list to only
// RSA/ECDSA. In TLS 1.3, the server announces this list in CertificateRequest.
// OpenSSL's TLS 1.3 client respects this list and will NOT send an Ed25519 cert
// when Ed25519 is absent from the server's advertised sigalgs — the client sends
// an empty Certificate instead. The server's verify_peer_trampoline is never
// called with an Ed25519 cert, so no session_event_tls_validation_failed is
// emitted. This was confirmed by empirical test (Ed25519 raw client connected,
// server got transport_handshake_failed but events were empty).
//
// Resolution: sigalg_disallowed IS genuinely tested at the unit level in
// test_verify_peer_t039.cpp::UnspecifiedSigAlgRejected (which calls verify_peer
// directly with alg_=unspecified). The listener-event path for sigalg_disallowed
// is a 015-era concern (would require either modifying asio_listener to support
// a custom SSL_CTX injection OR adding Ed25519 to the sigalg list for test
// purposes — neither is within 014's scope per the anti-runaway constraint).
//
// The fixture leaf_ed25519.pem is committed (T019 done) for future use.
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Ring non-empty sanity: successful handshake produces NO tls_validation_failed.
// ─────────────────────────────────────────────────────────────────────────────
TEST(TlsValidationFailedTaxonomy, SuccessfulHandshakeProducesNoEvent) {
    if (g_fixture_dir.empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto server_cfg = make_server_ssl_cfg();

    auto client_cs =
        make_client_cs(g_fixture_dir + "/leaf_rsa2048.pem", g_fixture_dir + "/leaf_rsa2048.key");
    if (!client_cs) {
        GTEST_SKIP() << "leaf_rsa2048.pem not available";
    }
    auto client_cfg = make_client_ssl_cfg(std::move(client_cs));

    // Run a SUCCESSFUL scenario.
    asio::io_context ioc;
    auto server = make_server_listener(ioc.get_executor(), server_cfg);

    bool server_hs_ok = false;
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto ar = co_await server.listener->async_accept();
            if (!ar.has_value()) co_return;
            auto* tls = dynamic_cast<TlsTransport*>(ar->get());
            if (!tls) co_return;
            auto hs = co_await tls->async_handshake(server_cfg);
            server_hs_ok = hs.has_value();
        },
        asio::detached);

    auto factory = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, client_cfg);
    ASSERT_TRUE(factory.has_value());
    auto client = (*factory)->make(ioc.get_executor(), client_cfg, nullptr);
    ASSERT_TRUE(client.has_value());
    auto* client_tls = dynamic_cast<TlsTransport*>(client->get());
    ASSERT_NE(client_tls, nullptr);

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            Endpoint ep{"127.0.0.1", server.port, 0};
            auto conn = co_await (*client)->async_connect(ep);
            if (!conn.has_value()) co_return;
            (void)co_await client_tls->async_handshake(client_cfg);
        },
        asio::detached);

    ioc.run_for(10s);

    EXPECT_TRUE(server_hs_ok) << "Successful handshake sanity check failed";

    // Listener ring must NOT contain any tls_validation_failed event.
    auto span = server.listener->recent_events();
    for (const auto& ev : span) {
        EXPECT_FALSE(std::holds_alternative<session_event_tls_validation_failed>(ev))
            << "Successful handshake must NOT emit tls_validation_failed";
    }
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main — pick up FIXPP_TLS_FIXTURE_DIR at runtime.
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR")) {
        g_fixture_dir = dir;
    }
#ifdef FIXPP_TLS_FIXTURE_DIR
    if (g_fixture_dir.empty()) {
        g_fixture_dir = FIXPP_TLS_FIXTURE_DIR;
    }
#endif
    return RUN_ALL_TESTS();
}
