// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_close_truncated_mapping.cpp
// RC#D (P2-1 + gate-b/r2) — async_read_some truncated-close vs EOF distinct
// variant witness.
//
// Verifies SC-006: "Every distinct transport-side failure mode ... surfaces as a
// distinct, named transport_* variant the operator can recognise in logs / metrics.
// No 'transport failed' catch-all exists in the public surface."
//
// Prior to RC#D, async_read_some collapsed both asio::error::eof AND
// asio::ssl::error::stream_truncated → transport_read_eof. SC-006 requires them
// to be distinct: eof → transport_read_eof; truncated → transport_read_truncated.
//
// This file provides:
//   1. Compile-time slot-distinctness assertions.
//   2. Runtime slot-distinctness test.
//   3. Live loopback witness (gate-b/r2 RC#D): full TLS handshake via
//      LoopbackTlsFixture; peer raw-TCP close (no SSL_shutdown) → server
//      async_read_some returns transport_read_truncated.
//      Requires FIXPP_TLS_FIXTURE_DIR (skipped when empty).
//
// Design anchor: spec.md FR-006 (RC#D resolved contradiction) + SC-006.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/error.hpp>
#include <asio/ssl/host_name_verification.hpp>
#include <asio/ssl/stream.hpp>
#include <asio/ssl/verify_mode.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>

#include <fixpp/core/error.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>

#include "transport/asio_listener.hpp"
#include "transport/loopback_tls_fixture.hpp"

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
namespace fe = fixpp::transport::errors;

}  // namespace

// ── SC-006 slot-distinctness: eof ≠ truncated ────────────────────────────────
static_assert(static_cast<int>(error::transport_read_eof) != static_cast<int>(error::transport_read_truncated),
    "SC-006: transport_read_eof and transport_read_truncated must be distinct slots");
static_assert(static_cast<int>(error::transport_read_eof)       == 102,
    "transport_read_eof must occupy slot 102");
static_assert(static_cast<int>(error::transport_read_truncated) == 103,
    "transport_read_truncated must occupy slot 103 (distinct from 102)");

// ── ASIO error codes are distinct system-level values ────────────────────────
// Confirms the two ASIO error codes we branch on in async_read_some are not
// equal — a defensive guard against OpenSSL/ASIO version drift collapsing them.
TEST(CloseTruncatedMapping, AsioEofAndStreamTruncatedAreDistinct) {
    const asio::error_code eof_ec = asio::error::eof;
    const asio::error_code truncated_ec = asio::ssl::error::stream_truncated;

    EXPECT_NE(eof_ec, truncated_ec)
        << "asio::error::eof and asio::ssl::error::stream_truncated must be distinct "
           "error codes so that async_read_some can branch on them independently";
}

// ── SC-006 runtime: transport_read_eof and transport_read_truncated are distinct ──
TEST(CloseTruncatedMapping, TransportReadEofAndTruncatedAreDistinct) {
    // SC-006 compliance: each failure mode maps to a distinct named variant.
    EXPECT_NE(error::transport_read_eof, error::transport_read_truncated);
    EXPECT_EQ(static_cast<int>(error::transport_read_eof),       102);
    EXPECT_EQ(static_cast<int>(error::transport_read_truncated), 103);
}

// ── namespace alias exports ───────────────────────────────────────────────────
TEST(CloseTruncatedMapping, NamespaceAliasConsistency) {
    EXPECT_EQ(fe::transport_read_eof,       error::transport_read_eof);
    EXPECT_EQ(fe::transport_read_truncated, error::transport_read_truncated);
}

// ── Live loopback witness (gate-b/r2 RC#D) ───────────────────────────────────
//
// Protocol:
//   1. LoopbackTlsFixture: asio_listener + asio_tls_transport_factory.
//   2. Server: async_accept → async_handshake → async_read_some (waits).
//   3. Client: raw asio::ssl::stream — connect + SSL handshake + raw TCP close
//      WITHOUT ssl::stream::shutdown (no TLS close_notify sent).
//   4. Assert: server's async_read_some returns transport_read_truncated
//      (not transport_read_eof; not transport_read_in_progress).
//
// Anchor: .specify/2h-transport.md FR-006 (SC-006 distinct-variant mandate).
// ─────────────────────────────────────────────────────────────────────────────
TEST(CloseTruncatedMapping, LiveLoopbackTruncatedClose) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    using namespace fixpp::transport;
    using namespace fixpp::transport::test;

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    // Result captured from the server-side async_read_some.
    expected_t<std::size_t> read_result =
        std::unexpected{error::transport_read_in_progress};

    // Server coroutine: accept → handshake → one read (will get truncated).
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            auto ar = co_await fixture.listener().async_accept();
            if (!ar.has_value()) co_return;
            std::unique_ptr<Transport> server_t = std::move(*ar);

            auto* tls = dynamic_cast<TlsTransport*>(server_t.get());
            if (!tls) co_return;

            auto hs = co_await tls->async_handshake(fixture.ssl_cfg());
            if (!hs.has_value()) co_return;

            // Now wait for data — the client will close without SSL_shutdown.
            std::array<std::byte, 256> buf{};
            read_result = co_await server_t->async_read_some(
                std::span<std::byte>{buf});
        },
        asio::detached);

    // Client coroutine: raw asio::ssl::stream — handshake then raw TCP close.
    // Use the same cert material as the fixture so the server accepts our cert.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            // Build a minimal TLS client context (same certs as fixture).
            asio::ssl::context raw_ctx{asio::ssl::context::tls_client};
            raw_ctx.load_verify_file(std::string{FIXPP_TLS_FIXTURE_DIR} + "/ca.pem");
            raw_ctx.use_certificate_chain_file(
                std::string{FIXPP_TLS_FIXTURE_DIR} + "/leaf_rsa2048.pem");
            raw_ctx.use_private_key_file(
                std::string{FIXPP_TLS_FIXTURE_DIR} + "/leaf_rsa2048.key",
                asio::ssl::context::pem);
            // Fixture cert has CN=fixpp-leaf-rsa2048 (no IP SAN) — disable
            // hostname check; the server still verifies our client cert via CA.
            raw_ctx.set_verify_mode(asio::ssl::verify_none);

            auto ep = fixture.server_endpoint();
            asio::ip::tcp::endpoint asio_ep{
                asio::ip::make_address(ep.host), ep.port};

            using ssl_sock = asio::ssl::stream<asio::ip::tcp::socket>;
            ssl_sock stream{co_await asio::this_coro::executor, raw_ctx};

            // TCP connect.
            asio::error_code ec;
            co_await stream.lowest_layer().async_connect(
                asio_ep, asio::redirect_error(asio::use_awaitable, ec));
            if (ec) co_return;

            // TLS handshake (client role).
            co_await stream.async_handshake(
                asio::ssl::stream_base::client,
                asio::redirect_error(asio::use_awaitable, ec));
            if (ec) co_return;

            // Raw TCP close WITHOUT ssl::stream::async_shutdown — this omits
            // the TLS close_notify, so the server's next read sees stream_truncated.
            stream.lowest_layer().close(ec);
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{10});

    ASSERT_TRUE(read_result.has_value() == false)
        << "server async_read_some must fail on truncated close";
    EXPECT_EQ(read_result.error(), error::transport_read_truncated)
        << "truncated peer close (no SSL_shutdown) must map to transport_read_truncated "
           "(SC-006 distinct variant); got slot="
        << static_cast<int>(read_result.error());
}
