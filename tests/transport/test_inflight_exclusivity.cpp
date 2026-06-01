// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_inflight_exclusivity.cpp
// T017 — [2h §9 seam #15] — in-flight exclusivity contract (FR-007).
//
// Cells 1-4 wired against LoopbackTlsFixture (gate-b/r2 RC#E close).
//
//   Cell 1: async_write overlap → second call returns transport_write_in_progress.
//   Cell 2: async_read_some overlap → second call returns transport_read_in_progress.
//   Cell 3: async_connect overlap → second call returns transport_already_connected.
//   Cell 4: async_handshake overlap → second call returns transport_already_connected.
//
// All four cells verify per [2h §4.1] normative API-level exclusivity contract.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "transport/loopback_tls_fixture.hpp"

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::transport::ConnectInfo;
using fixpp::transport::handshake_result;
using fixpp::transport::TlsTransport;
using fixpp::transport::Transport;
using fixpp::transport::test::LoopbackTlsFixture;
namespace fe = fixpp::transport::errors;
using namespace std::chrono_literals;

#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time contract: error codes for the exclusivity contract.
// ─────────────────────────────────────────────────────────────────────────────

TEST(InflightExclusivity, ErrorCodesPresent) {
    EXPECT_EQ(static_cast<int>(error::transport_write_in_progress), 100);
    EXPECT_EQ(static_cast<int>(error::transport_read_in_progress), 99);
    EXPECT_EQ(static_cast<int>(error::transport_already_connected), 97);
}

TEST(InflightExclusivity, ExclusivityCodesDistinct) {
    EXPECT_NE(error::transport_write_in_progress, error::transport_read_in_progress);
    EXPECT_NE(error::transport_write_in_progress, error::transport_already_connected);
    EXPECT_NE(error::transport_read_in_progress, error::transport_already_connected);
}

TEST(InflightExclusivity, NamespaceAliasConsistency) {
    EXPECT_EQ(fe::transport_write_in_progress, error::transport_write_in_progress);
    EXPECT_EQ(fe::transport_read_in_progress, error::transport_read_in_progress);
    EXPECT_EQ(fe::transport_already_connected, error::transport_already_connected);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared: build a fully handshaken loopback pair.
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
    const auto& ssl_cfg = fixture.ssl_cfg();

    Transport* client_raw = client.get();

    asio::co_spawn(
        ioc.get_executor(),
        [&client_raw, &connect_result, &client_hs, &ssl_cfg,
         ep = fixture.server_endpoint()]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            connect_result = co_await client_raw->async_connect(ep);
            if (connect_result && connect_result->has_value()) {
                auto* tls = dynamic_cast<TlsTransport*>(client_raw);
                if (tls) {
                    client_hs = co_await tls->async_handshake(ssl_cfg);
                }
            }
        },
        asio::detached);

    asio::co_spawn(
        ioc.get_executor(),
        [&accept_result, &server_hs, &ssl_cfg,
         listener = &fixture.listener()]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            accept_result = co_await listener->async_accept();
            if (accept_result && accept_result->has_value()) {
                auto* tls = dynamic_cast<TlsTransport*>(accept_result->value().get());
                if (tls) {
                    server_hs = co_await tls->async_handshake(ssl_cfg);
                }
            }
        },
        asio::detached);

    ioc.run_for(10s);
    ioc.restart();

    if (!connect_result || !connect_result->has_value())
        throw std::runtime_error("make_handshaken_pair: client connect failed");
    if (!accept_result || !accept_result->has_value())
        throw std::runtime_error("make_handshaken_pair: server accept failed");
    if (!client_hs || !client_hs->has_value())
        throw std::runtime_error("make_handshaken_pair: client handshake failed");
    if (!server_hs || !server_hs->has_value())
        throw std::runtime_error("make_handshaken_pair: server handshake failed");

    return HandshakenPair{
        .client = std::move(client),
        .server = std::move(accept_result->value()),
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1: write-overlap → transport_write_in_progress.
//
// Coroutine A (on strand) starts a large async_write → suspends on back-pressure.
// Coroutine B (on same strand) calls async_write → write_in_flight_ is true →
// returns transport_write_in_progress IMMEDIATELY (no OS write initiated).
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, WriteOverlapReturnImmediately) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    auto pair = make_handshaken_pair(fixture, ioc);
    Transport* client_raw = pair.client.get();

    // 2 MiB buffer — saturates kernel socket buffer to cause back-pressure.
    static constexpr std::size_t kBufSize = 2 * 1024 * 1024;
    std::vector<std::byte> big_buf(kBufSize, std::byte{0xAB});

    std::optional<expected_t<std::size_t>> result_a;
    std::optional<expected_t<std::size_t>> result_b;

    // Both coroutines on the same strand: B runs only after A suspends.
    auto strand = asio::make_strand(ioc.get_executor());

    // Coroutine A: write large buffer (suspends on back-pressure).
    asio::co_spawn(
        strand,
        [&result_a, client_raw, &big_buf]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            result_a = co_await client_raw->async_write(
                std::span<const std::byte>{big_buf.data(), big_buf.size()});
        },
        asio::detached);

    // Coroutine B: also tries async_write; must get write_in_progress immediately.
    std::byte tiny{0};
    asio::co_spawn(
        strand,
        [&result_b, client_raw, &tiny]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            result_b = co_await client_raw->async_write(std::span<const std::byte>{&tiny, 1});
        },
        asio::detached);

    // Run for 500 ms — enough for A to start and suspend, B to fire.
    ioc.run_for(500ms);

    ASSERT_TRUE(result_b.has_value())
        << "Coroutine B must complete (exclusivity guard fires immediately)";
    ASSERT_FALSE(result_b->has_value()) << "Coroutine B must return an error, not success";
    EXPECT_EQ(result_b->error(), error::transport_write_in_progress)
        << "Expected transport_write_in_progress from second async_write";

    // Cleanup: cancel A and close both sides.
    (void)pair.client->cancel();
    (void)pair.server->close();
    ioc.run_for(300ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2: read-overlap → transport_read_in_progress.
//
// Coroutine A reads from client; server does NOT write → A suspends.
// Coroutine B also calls async_read_some → returns transport_read_in_progress.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, ReadOverlapReturnImmediately) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    auto pair = make_handshaken_pair(fixture, ioc);
    Transport* client_raw = pair.client.get();

    std::optional<expected_t<std::size_t>> result_a;
    std::optional<expected_t<std::size_t>> result_b;

    auto strand = asio::make_strand(ioc.get_executor());

    std::byte buf_a{0}, buf_b{0};

    // Coroutine A: read (server doesn't write → suspends).
    asio::co_spawn(
        strand,
        [&result_a, client_raw, &buf_a]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            result_a = co_await client_raw->async_read_some(std::span<std::byte>{&buf_a, 1});
        },
        asio::detached);

    // Coroutine B: also reads → read_in_progress.
    asio::co_spawn(
        strand,
        [&result_b, client_raw, &buf_b]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            result_b = co_await client_raw->async_read_some(std::span<std::byte>{&buf_b, 1});
        },
        asio::detached);

    ioc.run_for(500ms);

    ASSERT_TRUE(result_b.has_value())
        << "Coroutine B must complete (exclusivity guard fires immediately)";
    ASSERT_FALSE(result_b->has_value()) << "Coroutine B must return an error, not success";
    EXPECT_EQ(result_b->error(), error::transport_read_in_progress)
        << "Expected transport_read_in_progress from second async_read_some";

    (void)pair.client->cancel();
    (void)pair.server->close();
    ioc.run_for(300ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 3: connect one-shot guard → transport_already_connected.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, ConnectOneShot) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    auto client = fixture.make_client(ioc.get_executor());
    Transport* client_raw = client.get();

    std::optional<expected_t<ConnectInfo>> connect1;
    std::optional<expected_t<ConnectInfo>> connect2;
    const auto ep = fixture.server_endpoint();

    asio::co_spawn(
        ioc.get_executor(),
        [&connect1, &connect2, client_raw, &ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            connect1 = co_await client_raw->async_connect(ep);
            // state is now connected; second call must return transport_already_connected.
            connect2 = co_await client_raw->async_connect(ep);
        },
        asio::detached);

    ioc.run_for(5s);

    ASSERT_TRUE(connect1.has_value());
    EXPECT_TRUE(connect1->has_value()) << "First async_connect must succeed";

    ASSERT_TRUE(connect2.has_value());
    ASSERT_FALSE(connect2->has_value()) << "Second async_connect must fail (one-shot guard)";
    EXPECT_EQ(connect2->error(), error::transport_already_connected);

    (void)client->close();
    ioc.run_for(200ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 4: handshake one-shot guard → transport_already_connected.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, HandshakeOneShot) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    auto pair = make_handshaken_pair(fixture, ioc);

    // Client is already in state handshaken; second async_handshake must return
    // transport_already_connected immediately (state != connected guard).
    const auto& ssl_cfg = fixture.ssl_cfg();
    auto* tls = dynamic_cast<TlsTransport*>(pair.client.get());
    ASSERT_NE(tls, nullptr) << "Client transport must be TlsTransport";

    std::optional<expected_t<handshake_result>> hs2;

    asio::co_spawn(
        ioc.get_executor(),
        [&hs2, tls, &ssl_cfg]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            hs2 = co_await tls->async_handshake(ssl_cfg);
        },
        asio::detached);

    ioc.run_for(2s);

    ASSERT_TRUE(hs2.has_value());
    ASSERT_FALSE(hs2->has_value()) << "Second async_handshake must fail (one-shot guard)";
    EXPECT_EQ(hs2->error(), error::transport_already_connected);

    (void)pair.client->close();
    (void)pair.server->close();
    ioc.run_for(200ms);
}

}  // namespace
