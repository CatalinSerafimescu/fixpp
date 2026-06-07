// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors

#include <gtest/gtest.h>

#include <array>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/stream.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "transport/asio_listener.hpp"
#include "transport/loopback_tls_fixture.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::session_event_tls_validation_failed;
using fixpp::transport::asio_listener;
using fixpp::transport::ConnectInfo;
using fixpp::transport::Endpoint;
using fixpp::transport::handshake_result;
using fixpp::transport::TlsTransport;
using fixpp::transport::Transport;
using fixpp::transport::test::LoopbackTlsFixture;

#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

std::string fixture_path(const char* leaf) {
    return std::string(FIXPP_TLS_FIXTURE_DIR) + "/" + leaf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a cert source from leaf + key PEM files.
// ─────────────────────────────────────────────────────────────────────────────
std::shared_ptr<fixpp::tls::cert_source> make_cert_source(std::string const& leaf_pem,
                                                          std::string const& key_pem) {
    fixpp::tls::file_cert_source::Config cfg;
    cfg.leaf_path = leaf_pem;
    cfg.private_key_path = key_pem;
    cfg.ca_bundle_path = fixture_path("ca.pem");
    auto result =
        fixpp::tls::file_cert_source::make_file_cert_source(cfg, std::pmr::new_delete_resource());
    if (!result.has_value()) {
        return nullptr;
    }
    return std::move(*result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build an SslCtxConfig from a cert source.
// ─────────────────────────────────────────────────────────────────────────────
fixpp::tls::SslCtxConfig make_ssl_cfg(std::shared_ptr<fixpp::tls::cert_source> cs) {
    fixpp::tls::SslCtxConfig cfg;
    cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;
    cfg.cs = std::move(cs);
    cfg.clock = nullptr;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: mint a fresh client Transport using a factory.
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<Transport> make_client_transport(asio::any_io_executor exec,
                                                 fixpp::tls::SslCtxConfig const& cfg,
                                                 Transport::Config transport_cfg = {}) {
    auto factory_result = fixpp::transport::make_asio_tls_transport_factory(transport_cfg, cfg);
    if (!factory_result.has_value()) {
        throw std::runtime_error("make_asio_tls_transport_factory failed");
    }
    auto transport_result = (*factory_result)->make(exec, cfg, nullptr);
    if (!transport_result.has_value()) {
        throw std::runtime_error("TransportFactory::make failed");
    }
    return std::move(*transport_result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: fully handshaken loopback pair (same pattern as sibling tests).
// ─────────────────────────────────────────────────────────────────────────────
struct HandshakenPair {
    std::unique_ptr<Transport> client;
    std::unique_ptr<Transport> server;
};

HandshakenPair make_handshaken_pair(LoopbackTlsFixture& fixture, asio::io_context& ioc) {
    std::optional<expected_t<ConnectInfo>> connect_result;
    std::optional<expected_t<handshake_result>> client_hs;
    std::optional<expected_t<std::unique_ptr<Transport>>> accept_result;
    std::optional<expected_t<handshake_result>> server_hs;

    auto client = fixture.make_client(ioc.get_executor());
    Transport* client_raw = client.get();
    auto const& ssl_cfg = fixture.ssl_cfg();

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            connect_result = co_await client_raw->async_connect(fixture.server_endpoint());
            if (connect_result && connect_result->has_value()) {
                auto* tls = dynamic_cast<TlsTransport*>(client_raw);
                if (tls != nullptr) {
                    client_hs = co_await tls->async_handshake(ssl_cfg);
                }
            }
        },
        asio::detached);

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            accept_result = co_await fixture.listener().async_accept();
            if (accept_result && accept_result->has_value()) {
                auto* tls = dynamic_cast<TlsTransport*>(accept_result->value().get());
                if (tls != nullptr) {
                    server_hs = co_await tls->async_handshake(ssl_cfg);
                }
            }
        },
        asio::detached);

    ioc.run_for(10s);
    ioc.restart();

    if (!connect_result || !connect_result->has_value()) {
        throw std::runtime_error("client connect failed");
    }
    if (!accept_result || !accept_result->has_value()) {
        throw std::runtime_error("server accept failed");
    }
    if (!client_hs || !client_hs->has_value()) {
        throw std::runtime_error("client handshake failed");
    }
    if (!server_hs || !server_hs->has_value()) {
        throw std::runtime_error("server handshake failed");
    }

    return HandshakenPair{
        .client = std::move(client),
        .server = std::move(accept_result->value()),
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a server listener bound to 127.0.0.1:0 (OS-assigned port).
// ─────────────────────────────────────────────────────────────────────────────
struct ListenerHandle {
    std::unique_ptr<asio_listener> listener;
    std::uint16_t port{0};
};

ListenerHandle make_server_listener(asio::any_io_executor exec,
                                    fixpp::tls::SslCtxConfig server_ssl_cfg) {
    asio_listener::Config cfg;
    cfg.bind_endpoint = Endpoint{"127.0.0.1", 0, 4};
    cfg.ssl_cfg = std::move(server_ssl_cfg);
    auto listener = std::make_unique<asio_listener>(exec, std::move(cfg));
    const auto port = listener->bound_endpoint().port;
    return ListenerHandle{.listener = std::move(listener), .port = port};
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: get an ephemeral port guaranteed to have no listener.
//
// Bind an acceptor to 127.0.0.1:0 (OS assigns a free port) — this gives us
// a port number known to be in the ephemeral range.  We then connect to
// 127.0.0.2:<port>: nothing ever listens on 127.0.0.2 (a different loopback
// address on the same machine), so the connect reliably receives ECONNREFUSED.
// All 127/8 addresses are loopback on Linux; we hold the acceptor open to
// prevent port reuse confusion on the first address.
// ─────────────────────────────────────────────────────────────────────────────
struct RefusedPort {
    asio::io_context holder_ioc;
    asio::ip::tcp::acceptor holder;
    std::uint16_t port;
    std::string host;  // 127.0.0.2 — a different loopback address, never bound

    RefusedPort()
        : holder{holder_ioc,
                 asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), 0}}
        , port{holder.local_endpoint().port()}
        , host{"127.0.0.2"} {}
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: raw-client + fixpp-server pair for read/write error scenarios.
//
// Uses an asio::ssl::stream<tcp::socket> as the client so we can control
// shutdown precisely without going through the Transport abstraction.
// ─────────────────────────────────────────────────────────────────────────────
using RawSslClient = asio::ssl::stream<asio::ip::tcp::socket>;

struct RawClientServerPair {
    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    std::optional<asio::ssl::context> raw_ctx;
    std::optional<RawSslClient> raw_client;
    std::unique_ptr<Transport> server;
};

std::unique_ptr<RawClientServerPair> make_raw_client_server_pair() {
    auto pair = std::make_unique<RawClientServerPair>();
    std::optional<expected_t<std::unique_ptr<Transport>>> accept_result;
    std::optional<expected_t<handshake_result>> server_hs;
    asio::error_code client_ec;

    asio::co_spawn(
        pair->ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            accept_result = co_await pair->fixture.listener().async_accept();
            if (accept_result && accept_result->has_value()) {
                auto* tls = dynamic_cast<TlsTransport*>(accept_result->value().get());
                if (tls != nullptr) {
                    server_hs = co_await tls->async_handshake(pair->fixture.ssl_cfg());
                }
            }
        },
        asio::detached);

    // Build a raw asio::ssl::context (same certs as fixture, no hostname check).
    pair->raw_ctx.emplace(asio::ssl::context::tls_client);
    pair->raw_ctx->load_verify_file(fixture_path("ca.pem"));
    pair->raw_ctx->use_certificate_chain_file(fixture_path("leaf_rsa2048.pem"));
    pair->raw_ctx->use_private_key_file(fixture_path("leaf_rsa2048.key"),
                                        asio::ssl::context::pem);
    pair->raw_ctx->set_verify_mode(asio::ssl::verify_none);

    asio::co_spawn(
        pair->ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            pair->raw_client.emplace(co_await asio::this_coro::executor, *pair->raw_ctx);
            auto ep = pair->fixture.server_endpoint();
            co_await pair->raw_client->lowest_layer().async_connect(
                asio::ip::tcp::endpoint{asio::ip::make_address(ep.host), ep.port},
                asio::redirect_error(asio::use_awaitable, client_ec));
            if (client_ec) {
                co_return;
            }
            co_await pair->raw_client->async_handshake(
                asio::ssl::stream_base::client,
                asio::redirect_error(asio::use_awaitable, client_ec));
        },
        asio::detached);

    pair->ioc.run_for(10s);
    pair->ioc.restart();

    if (!accept_result || !accept_result->has_value()) {
        throw std::runtime_error("server accept failed");
    }
    if (!server_hs || !server_hs->has_value()) {
        throw std::runtime_error("server handshake failed");
    }
    if (client_ec) {
        throw std::runtime_error("raw client handshake failed");
    }

    pair->server = std::move(accept_result->value());
    return pair;
}

// ─────────────────────────────────────────────────────────────────────────────
// Case 1: async_connect to a port with no listener → transport_connect_refused
//
// Branch covered: async_connect @ asio_tls_transport.cpp ~946-949
//   connect_ec (connection_refused or other non-aborted) → transport_connect_refused
//
// We bind an acceptor on 127.0.0.1:0 to get a free port number, then connect
// to 127.0.0.2:<port>.  Nothing ever listens on 127.0.0.2 (a different loopback
// address), so the connect reliably returns ECONNREFUSED.
// ─────────────────────────────────────────────────────────────────────────────
TEST(AsioTlsTransportErrorPaths, ConnectRefusedMapsToTransportConnectRefused) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    // Keep the acceptor alive to anchor the port number; connect to the OTHER
    // loopback address (127.0.0.2) where nothing listens → ECONNREFUSED.
    RefusedPort refused;

    asio::io_context ioc;
    auto cs = make_cert_source(fixture_path("leaf_rsa2048.pem"), fixture_path("leaf_rsa2048.key"));
    ASSERT_NE(cs, nullptr);
    auto client = make_client_transport(ioc.get_executor(), make_ssl_cfg(cs));

    std::optional<expected_t<ConnectInfo>> result;
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            result = co_await client->async_connect(Endpoint{refused.host, refused.port, 0});
        },
        asio::detached);

    ioc.run_for(5s);

    ASSERT_TRUE(result.has_value()) << "connect must complete within 5 s";
    ASSERT_FALSE(result->has_value());
    EXPECT_EQ(result->error(), error::transport_connect_refused);
}

// ─────────────────────────────────────────────────────────────────────────────
// Case 2: async_handshake times out mid-handshake → transport_handshake_timeout
//
// Branch covered: async_handshake @ asio_tls_transport.cpp ~1048-1053
//   operation_aborted + cancellation_state.cancelled() == none
//       → transport_handshake_timeout
//
// The server (plain TCP acceptor) never speaks TLS so the handshake blocks.
// A 200 ms built-in timeout fires socket_.cancel() → operation_aborted →
// cs.cancelled()==none → transport_handshake_timeout.
//
// The sibling branch (transport_handshake_cancelled when cs.cancelled()!=none)
// is exercised by the external-signal path tracked in the DISABLED
// CancellationPropagation cells (test_cancellation_propagation.cpp).
// [[feedback_asio_cospawn_total_cancellation_default]]
// ─────────────────────────────────────────────────────────────────────────────
TEST(AsioTlsTransportErrorPaths, HandshakeTimeoutMapsToTransportHandshakeTimeout) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;

    // Plain TCP acceptor — it accepts the connection but never speaks TLS,
    // so the client handshake will wait indefinitely until cancelled.
    asio::ip::tcp::acceptor acceptor{
        ioc, asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), 0}};
    const auto port = static_cast<std::uint16_t>(acceptor.local_endpoint().port());

    auto cs = make_cert_source(fixture_path("leaf_rsa2048.pem"), fixture_path("leaf_rsa2048.key"));
    ASSERT_NE(cs, nullptr);
    auto client = make_client_transport(ioc.get_executor(), make_ssl_cfg(cs));
    auto* client_tls = dynamic_cast<TlsTransport*>(client.get());
    ASSERT_NE(client_tls, nullptr);

    // Phase 1: connect (inside run_for so it completes before phase 2).
    std::optional<expected_t<ConnectInfo>> connect_result;
    std::optional<asio::ip::tcp::socket> accepted_socket;

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            connect_result = co_await client->async_connect(Endpoint{"127.0.0.1", port, 0});
        },
        asio::detached);

    // Accept on the server side so the TCP handshake completes.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            auto sock = co_await acceptor.async_accept(
                asio::redirect_error(asio::use_awaitable, ec));
            if (!ec) {
                accepted_socket.emplace(std::move(sock));
            }
        },
        asio::detached);

    ioc.run_for(5s);
    ioc.restart();

    ASSERT_TRUE(connect_result.has_value()) << "connect must complete";
    ASSERT_TRUE(connect_result->has_value())
        << "connect must succeed: " << static_cast<int>(connect_result->error());
    ASSERT_TRUE(accepted_socket.has_value()) << "server must accept the TCP connection";

    // Phase 2: start TLS handshake with a short built-in timeout.
    //
    // The server (plain TCP acceptor) never speaks TLS, so the handshake blocks.
    // We configure tls_handshake_timeout = 200 ms on a fresh client so the
    // transport's internal timer fires quickly and cancels the socket:
    //   socket_.cancel() → operation_aborted, cs.cancelled() == none
    //   → transport_handshake_timeout (the sibling branch to transport_handshake_cancelled)
    //
    // This exercises async_handshake @ asio_tls_transport.cpp ~1046-1053:
    //   if (handshake_ec == operation_aborted)
    //     if (cs.cancelled() != none) → transport_handshake_cancelled   [external cancel]
    //     else                        → transport_handshake_timeout      [internal timer]  ← HERE
    //
    // The external-cancel path (transport_handshake_cancelled) requires cs.cancelled()
    // to be set by the co_spawn's own cancellation slot.  Testing that path reliably
    // requires the signal to propagate from an external co_spawn into async_handshake's
    // own cancellation state — a nuanced asio propagation chain that the disabled
    // CancellationPropagation cells (test_cancellation_propagation.cpp) track.
    // We test the timeout sub-branch here because it exercises the same operationally
    // important branch and is reliably demonstrable in a single-host test.
    //
    // [[feedback_asio_cospawn_total_cancellation_default]]
    std::optional<expected_t<handshake_result>> hs_result;

    // Build a new transport with a 200 ms handshake timeout.
    Transport::Config short_timeout_cfg;
    short_timeout_cfg.tls_handshake_timeout = std::chrono::milliseconds{200};
    auto client2 = make_client_transport(ioc.get_executor(), make_ssl_cfg(cs), short_timeout_cfg);
    auto* client2_tls = dynamic_cast<TlsTransport*>(client2.get());
    ASSERT_NE(client2_tls, nullptr);

    // Connect the short-timeout transport to the same plain acceptor.
    std::optional<expected_t<ConnectInfo>> connect_result2;
    std::optional<asio::ip::tcp::socket> accepted_socket2;

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            connect_result2 = co_await client2->async_connect(Endpoint{"127.0.0.1", port, 0});
        },
        asio::detached);

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            auto sock = co_await acceptor.async_accept(
                asio::redirect_error(asio::use_awaitable, ec));
            if (!ec) {
                accepted_socket2.emplace(std::move(sock));
            }
        },
        asio::detached);

    ioc.run_for(3s);
    ioc.restart();

    ASSERT_TRUE(connect_result2.has_value()) << "second connect must complete";
    ASSERT_TRUE(connect_result2->has_value());
    ASSERT_TRUE(accepted_socket2.has_value());

    // Start the handshake with the short timeout.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            hs_result = co_await client2_tls->async_handshake(make_ssl_cfg(cs));
        },
        asio::detached);

    // Run for 2 s — enough for the 200 ms timer to fire.
    ioc.run_for(2s);

    if (accepted_socket.has_value()) {
        asio::error_code ec;
        accepted_socket->close(ec);
    }
    if (accepted_socket2.has_value()) {
        asio::error_code ec;
        accepted_socket2->close(ec);
    }

    ASSERT_TRUE(hs_result.has_value()) << "handshake must complete (timed out) within 2 s";
    ASSERT_FALSE(hs_result->has_value());
    // Internal timer → socket_.cancel() → operation_aborted → cs.cancelled() == none
    // → transport_handshake_timeout (sibling of transport_handshake_cancelled).
    EXPECT_EQ(hs_result->error(), error::transport_handshake_timeout);
}

// NOTE: an async_read_some external-cancellation case was removed — it was flaky
// (passed ~1/3 under ASan). Root cause: (1) spawn-ordering race — the cancel
// coroutine can run before the read coroutine reaches co_await, so the read then
// blocks until the deadline; and (2) async_read_some calls
// reset_cancellation_state(enable_total_cancellation()) early, which does not
// reliably propagate an external slot from the enclosing lambda (same structural
// reason the handshake-cancellation case was replaced by a deterministic timeout).
// The transport_read_cancelled branch is not reliably reachable from an external
// loopback test without production changes; a flaky test is worse than the gap.

// ─────────────────────────────────────────────────────────────────────────────
// Case 4: graceful TLS peer shutdown → transport_read_eof
//
// Branch covered: async_read_some @ asio_tls_transport.cpp ~1165-1166
//   asio::error::eof → transport_read_eof
// ─────────────────────────────────────────────────────────────────────────────
TEST(AsioTlsTransportErrorPaths, GracefulTlsShutdownMapsToTransportReadEof) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto pair = make_raw_client_server_pair();
    Transport* server_raw = pair->server.get();

    std::array<std::byte, 8> buf{};
    std::optional<expected_t<std::size_t>> read_result;
    asio::error_code shutdown_ec;

    // Server waits for data; client sends TLS close_notify (async_shutdown).
    asio::co_spawn(
        pair->ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            read_result = co_await server_raw->async_read_some(std::span<std::byte>{buf});
        },
        asio::detached);

    asio::co_spawn(
        pair->ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await pair->raw_client->async_shutdown(
                asio::redirect_error(asio::use_awaitable, shutdown_ec));
        },
        asio::detached);

    // Pump only until the server read completes (close_notify → EOF). We do NOT
    // drain all work: the client's async_shutdown waits for a reciprocal
    // close_notify the server never sends, so it stays pending — draining would
    // burn the full 10 s deadline. Poll for the observable; 10 s is a safety cap.
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!read_result.has_value() && std::chrono::steady_clock::now() < deadline) {
        pair->ioc.run_for(50ms);
    }

    ASSERT_TRUE(read_result.has_value()) << "server read must complete within 10 s";
    ASSERT_FALSE(read_result->has_value());
    EXPECT_EQ(read_result->error(), error::transport_read_eof);
}

// ─────────────────────────────────────────────────────────────────────────────
// Case 5: write after peer TCP close → transport_write_error
//
// Branch covered: async_write @ asio_tls_transport.cpp ~1227
//   any write ec (not aborted) → transport_write_error
//
// Protocol:
//   1. Raw client closes the TCP socket WITHOUT SSL_shutdown (abrupt close).
//   2. Server's async_read_some returns transport_read_truncated
//      (stream_truncated from OpenSSL — peer dropped TCP without close_notify).
//   3. Server then tries to write to the dead connection → transport_write_error.
//
// We use raw TCP close (not async_shutdown) because async_shutdown leaves the
// TCP connection half-open: the server can still send data into its kernel
// TX buffer even after the client sent TLS close_notify.  A raw TCP close
// causes an RST, making the subsequent server write fail reliably.
// ─────────────────────────────────────────────────────────────────────────────
TEST(AsioTlsTransportErrorPaths, WriteAfterPeerTcpCloseMapsToTransportWriteError) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto pair = make_raw_client_server_pair();
    Transport* server_raw = pair->server.get();

    // Phase 1: server reads; client closes TCP socket with RST (SO_LINGER=0).
    //
    // Setting SO_LINGER with l_linger=0 on the client socket causes close() to
    // send an RST instead of a FIN.  The server receives an RST, and its next
    // write after the RST arrives will fail with ECONNRESET → transport_write_error.
    // Without SO_LINGER=0 (default), close() sends FIN (half-close), and the
    // server's write can succeed into the kernel TX buffer before the FIN-ACK
    // cycle delivers the signal.
    std::array<std::byte, 8> read_buf{};
    std::optional<expected_t<std::size_t>> read_result;

    asio::co_spawn(
        pair->ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            read_result = co_await server_raw->async_read_some(std::span<std::byte>{read_buf});
        },
        asio::detached);

    // Set SO_LINGER=0 to force RST on close, then close the socket.
    {
        asio::socket_base::linger linger_opt{true, 0};
        asio::error_code set_ec;
        pair->raw_client->lowest_layer().set_option(linger_opt, set_ec);
        asio::error_code close_ec;
        pair->raw_client->lowest_layer().close(close_ec);
    }

    pair->ioc.run_for(5s);
    pair->ioc.restart();

    ASSERT_TRUE(read_result.has_value()) << "server read must complete within 5 s";
    ASSERT_FALSE(read_result->has_value());
    // SO_LINGER=0 close sends RST; server receives ECONNRESET → transport_read_error.
    // (stream_truncated / transport_read_truncated only fires when TLS sees a FIN
    // without prior close_notify, not an RST.)
    ASSERT_EQ(read_result->error(), error::transport_read_error);

    // Phase 2: server writes to the dead connection → write error.
    // The RST has already been received, so the kernel will fail the write.
    const std::array<std::byte, 4> payload{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    std::optional<expected_t<std::size_t>> write_result;

    asio::co_spawn(
        pair->ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            write_result =
                co_await server_raw->async_write(std::span<const std::byte>{payload});
        },
        asio::detached);

    pair->ioc.run_for(5s);

    ASSERT_TRUE(write_result.has_value()) << "server write must complete within 5 s";
    ASSERT_FALSE(write_result->has_value());
    EXPECT_EQ(write_result->error(), error::transport_write_error);
}

// ─────────────────────────────────────────────────────────────────────────────
// Case 6: TLS handshake failure with incompatible sigalg → transport_handshake_failed
//         AND does NOT emit session_event_tls_validation_failed
//
// Branches covered: async_handshake @ asio_tls_transport.cpp ~1046-1086
//   handshake_ec set (OpenSSL-level failure) → transport_handshake_failed
//   hctx.verify_error not set for sigalg mismatch → no tls_validation_failed event
//
// Ed25519 cert is a valid cert signed by the CA but uses an unsupported sigalg
// in this OpenSSL build's policy for the mtls_ca profile.
// ─────────────────────────────────────────────────────────────────────────────
TEST(AsioTlsTransportErrorPaths, Ed25519HandshakeFailureDoesNotEmitValidationEvent) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto server_cs =
        make_cert_source(fixture_path("leaf_rsa2048.pem"), fixture_path("leaf_rsa2048.key"));
    auto client_cs =
        make_cert_source(fixture_path("leaf_ed25519.pem"), fixture_path("leaf_ed25519.key"));
    if (client_cs == nullptr) {
        GTEST_SKIP() << "leaf_ed25519 fixtures not available";
    }
    ASSERT_NE(server_cs, nullptr);

    auto server_cfg = make_ssl_cfg(server_cs);
    auto client_cfg = make_ssl_cfg(client_cs);

    asio::io_context ioc;
    auto server = make_server_listener(ioc.get_executor(), server_cfg);

    std::optional<expected_t<handshake_result>> server_handshake;
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto ar = co_await server.listener->async_accept();
            if (!ar.has_value()) {
                co_return;
            }
            auto* tls = dynamic_cast<TlsTransport*>(ar->get());
            if (tls == nullptr) {
                co_return;
            }
            server_handshake = co_await tls->async_handshake(server_cfg);
        },
        asio::detached);

    auto client = make_client_transport(ioc.get_executor(), client_cfg);
    auto* client_tls = dynamic_cast<TlsTransport*>(client.get());
    ASSERT_NE(client_tls, nullptr);

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto connect_result =
                co_await client->async_connect(Endpoint{"127.0.0.1", server.port, 0});
            if (!connect_result.has_value()) {
                co_return;
            }
            (void)co_await client_tls->async_handshake(client_cfg);
        },
        asio::detached);

    ioc.run_for(10s);

    ASSERT_TRUE(server_handshake.has_value()) << "server handshake must complete within 10 s";
    ASSERT_FALSE(server_handshake->has_value());
    EXPECT_EQ(server_handshake->error(), error::transport_handshake_failed);

    auto events = server.listener->recent_events();
    for (auto const& event : events) {
        EXPECT_FALSE(std::holds_alternative<session_event_tls_validation_failed>(event))
            << "Ed25519 sigalg mismatch path must not emit tls_validation_failed: "
               "verify_peer_trampoline does not set verify_error on this OpenSSL-level failure";
    }
}

}  // namespace
