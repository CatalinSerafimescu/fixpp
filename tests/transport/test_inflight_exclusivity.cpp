// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_inflight_exclusivity.cpp
// T017 — [2h §9 seam #15] — in-flight exclusivity contract (FR-007).
//
// Cells 1-4 wired against LoopbackTlsFixture (gate-b/r2 RC#E close).
//
//   Cell 1: async_write OVERLAP → second call returns transport_write_in_progress.
//   Cell 2: async_read_some OVERLAP → second call returns transport_read_in_progress.
//   Cell 3: async_connect SEQUENTIAL one-shot → transport_already_connected.
//   Cell 4: async_handshake SEQUENTIAL one-shot → transport_already_connected.
//   Cell 5: async_connect OVERLAP → refused with transport_already_connected (#342).
//   Cell 6: async_handshake OVERLAP → refused with transport_already_connected (#342).
//   Cell 7: async_connect retry after a FAILED attempt → really attempts (#342).
//
// ⚠️ Cells 3 and 4 were billed as OVERLAP witnesses until 2026-09-02 and are
// NOT: cell 3 co_awaits the first connect to completion before issuing the
// second, and cell 4 runs on an already fully handshaken pair. They are correct
// SEQUENTIAL one-shot tests — they are the evidence that the answer from a
// SUCCEEDED state is 97 — and are kept as such under honest names (#342).
//
// Cells 5-7 are the overlap witnesses. They could not have passed before #342:
// the one-shot test is a STATE test and state_ leaves `fresh` only on SUCCESS,
// so an overlapping second async_connect used to pass through and really
// attempt. Cell 7 is the arm that catches the opposite failure — a guard that
// sets the flag and never clears it passes cells 5 and 6 and fails cell 7.
//
// All cells verify per [2h §4.1] normative API-level exclusivity contract.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <fixpp/transport/transport_factory.hpp>
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
using fixpp::transport::make_asio_plain_transport_factory;
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
// Cell 3: connect one-shot from a SUCCEEDED state → transport_already_connected.
// SEQUENTIAL, not an overlap witness — the first connect is co_awaited to
// completion before the second is issued (#342). Cell 5 is the overlap one.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, ConnectOneShotFromSucceededState) {
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
// Cell 4: handshake one-shot from a SUCCEEDED state → transport_already_connected.
// SEQUENTIAL, not an overlap witness — it runs on an already fully handshaken
// pair (#342). Cell 6 is the overlap one.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, HandshakeOneShotFromSucceededState) {
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

// ─────────────────────────────────────────────────────────────────────────────
// Connected-but-NOT-handshaken pair — cell 6 needs a Transport in `connected`,
// which make_handshaken_pair above has already moved past.
// ─────────────────────────────────────────────────────────────────────────────
struct ConnectedPair {
    std::unique_ptr<Transport> client;
    std::unique_ptr<Transport> server;
};

ConnectedPair make_connected_pair(LoopbackTlsFixture& fixture, asio::io_context& ioc) {
    auto client = fixture.make_client(ioc.get_executor());
    Transport* client_raw = client.get();
    const auto ep = fixture.server_endpoint();

    std::optional<expected_t<ConnectInfo>> connect_result;
    std::optional<expected_t<std::unique_ptr<Transport>>> accept_result;

    asio::co_spawn(
        ioc.get_executor(),
        [&connect_result, client_raw, ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            connect_result = co_await client_raw->async_connect(ep);
        },
        asio::detached);

    asio::co_spawn(
        ioc.get_executor(),
        [&accept_result, listener = &fixture.listener()]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            accept_result = co_await listener->async_accept();
        },
        asio::detached);

    ioc.run_for(10s);
    ioc.restart();

    if (!connect_result || !connect_result->has_value())
        throw std::runtime_error("make_connected_pair: client connect failed");
    if (!accept_result || !accept_result->has_value())
        throw std::runtime_error("make_connected_pair: server accept failed");

    return ConnectedPair{
        .client = std::move(client),
        .server = std::move(accept_result->value()),
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 5: async_connect OVERLAP → transport_already_connected (#342).
//
// A and B are bound to the SAME strand. A issues async_connect and suspends at
// async_resolve (every this_coro awaiter before it is await_ready()==true, so
// the resolve is the first real suspension point). B then runs on that strand
// and must be REFUSED while A is still in flight.
//
// ⚠️ SPURIOUS-HIT ARM. 97 is also what the one-shot STATE test answers once A
// has SUCCEEDED — so a cell that only asserts "B got 97" would pass even if the
// overlap guard did not exist, simply by letting A finish first. The
// a_inflight_when_b_issued capture is what distinguishes the two: it is read
// inside B with NO suspension point between it and the guard read inside
// async_connect, so it cannot go stale.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, ConnectOverlapRefusedWhileFirstInFlight) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    auto client = fixture.make_client(ioc.get_executor());
    Transport* client_raw = client.get();
    const auto ep = fixture.server_endpoint();

    std::optional<expected_t<ConnectInfo>> result_a;
    std::optional<expected_t<ConnectInfo>> result_b;
    std::optional<bool> a_inflight_when_b_issued;

    auto strand = asio::make_strand(ioc.get_executor());

    asio::co_spawn(
        strand,
        [&result_a, client_raw, ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            result_a = co_await client_raw->async_connect(ep);
        },
        asio::detached);

    asio::co_spawn(
        strand,
        [&result_b, &result_a, &a_inflight_when_b_issued, client_raw,
         ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            a_inflight_when_b_issued = !result_a.has_value();
            result_b = co_await client_raw->async_connect(ep);
        },
        asio::detached);

    ioc.run_for(5s);

    ASSERT_TRUE(a_inflight_when_b_issued.has_value()) << "Coroutine B never ran";
    EXPECT_TRUE(*a_inflight_when_b_issued)
        << "SPURIOUS-HIT: A had already completed when B issued, so a 97 below would "
           "come from the one-shot state test, not from the overlap guard";

    ASSERT_TRUE(result_b.has_value()) << "B must complete (guard answers immediately)";
    ASSERT_FALSE(result_b->has_value()) << "B must be refused, not attempt";
    EXPECT_EQ(result_b->error(), error::transport_already_connected);

    ASSERT_TRUE(result_a.has_value()) << "A must still complete";
    EXPECT_TRUE(result_a->has_value())
        << "A must still SUCCEED — before #342 the overlapping attempt called "
           "socket_.close() underneath it";

    (void)client->close();
    ioc.run_for(200ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 6: async_handshake OVERLAP → transport_already_connected (#342).
//
// Same shape and same spurious-hit arm as cell 5. A cannot complete before B
// runs by construction: A suspends inside the OpenSSL exchange waiting on the
// peer, and the server's handshake is spawned AFTER B on the same io_context.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, HandshakeOverlapRefusedWhileFirstInFlight) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    auto pair = make_connected_pair(fixture, ioc);

    auto* client_tls = dynamic_cast<TlsTransport*>(pair.client.get());
    auto* server_tls = dynamic_cast<TlsTransport*>(pair.server.get());
    ASSERT_NE(client_tls, nullptr) << "Client transport must be TlsTransport";
    ASSERT_NE(server_tls, nullptr) << "Server transport must be TlsTransport";
    const auto& ssl_cfg = fixture.ssl_cfg();

    std::optional<expected_t<handshake_result>> hs_a;
    std::optional<expected_t<handshake_result>> hs_b;
    std::optional<expected_t<handshake_result>> hs_server;
    std::optional<bool> a_inflight_when_b_issued;

    auto strand = asio::make_strand(ioc.get_executor());

    asio::co_spawn(
        strand,
        [&hs_a, client_tls, &ssl_cfg]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            hs_a = co_await client_tls->async_handshake(ssl_cfg);
        },
        asio::detached);

    asio::co_spawn(
        strand,
        [&hs_b, &hs_a, &a_inflight_when_b_issued, client_tls, &ssl_cfg]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            a_inflight_when_b_issued = !hs_a.has_value();
            hs_b = co_await client_tls->async_handshake(ssl_cfg);
        },
        asio::detached);

    asio::co_spawn(
        ioc.get_executor(),
        [&hs_server, server_tls, &ssl_cfg]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            hs_server = co_await server_tls->async_handshake(ssl_cfg);
        },
        asio::detached);

    ioc.run_for(10s);

    ASSERT_TRUE(a_inflight_when_b_issued.has_value()) << "Coroutine B never ran";
    EXPECT_TRUE(*a_inflight_when_b_issued)
        << "SPURIOUS-HIT: A had already completed when B issued, so a 97 below would "
           "come from the one-shot state test, not from the overlap guard";

    ASSERT_TRUE(hs_b.has_value()) << "B must complete (guard answers immediately)";
    ASSERT_FALSE(hs_b->has_value()) << "B must be refused, not attempt";
    EXPECT_EQ(hs_b->error(), error::transport_already_connected);

    ASSERT_TRUE(hs_a.has_value()) << "A must still complete";
    EXPECT_TRUE(hs_a->has_value()) << "A's handshake must still succeed";

    (void)pair.client->close();
    (void)pair.server->close();
    ioc.run_for(200ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 7: the flag CLEARS — a retry after a FAILED attempt really attempts.
//
// Cells 5 and 6 prove the guard FIRES. They cannot catch the opposite defect: a
// guard that sets connect_in_flight_ and never clears it passes both of them and
// silently wedges the Transport into permanent transport_already_connected,
// breaking FR-007's "a failed attempt stays `fresh` and is retryable".
//
// The first attempt fails in RESOLUTION (an RFC 6761 `.invalid` host), which
// returns before any state write, so the Transport must stay `fresh` AND clear
// the flag -- and the retry to the live fixture endpoint must really connect.
// (A refused CONNECT would have been the obvious choice and is not usable here;
// the body explains why.)
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, ConnectRetryableAfterFailedAttempt) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    // The failing first attempt is a RESOLVE failure: ".invalid" is reserved by
    // RFC 6761 as guaranteed-NXDOMAIN, so this fails fast and deterministically,
    // and async_connect returns transport_resolve_failed WITHOUT ever leaving
    // state_ == fresh -- exactly the "failed attempt is retryable" case FR-007
    // names.
    //
    // ⚠️ NOT a refused connect, which is the obvious choice and is not
    // constructible here: this sandbox DROPS connects to unbound loopback ports
    // rather than refusing them (measured: 127.0.0.1 ports 1/2/9/65000 all time
    // out, none gives ECONNREFUSED), so a "connect must fail" arm built that way
    // hangs to its bound instead of failing. A bound-then-closed EPHEMERAL port
    // is worse still: the first version of this cell used one and the connect
    // SUCCEEDED, leaving the fixture listener's accept pending for the full 10 s
    // -- the client's outbound socket draws from the same ephemeral range, and a
    // loopback connect whose source and destination ports coincide can
    // self-connect.
    const fixpp::transport::Endpoint unresolvable{"no.such.host.invalid", 12345};

    auto client = fixture.make_client(ioc.get_executor());
    Transport* client_raw = client.get();
    const auto good_ep = fixture.server_endpoint();

    std::optional<expected_t<ConnectInfo>> first;
    std::optional<expected_t<ConnectInfo>> second;

    asio::co_spawn(
        ioc.get_executor(),
        [&first, &second, client_raw, unresolvable, good_ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            first = co_await client_raw->async_connect(unresolvable);
            second = co_await client_raw->async_connect(good_ep);
        },
        asio::detached);

    asio::co_spawn(
        ioc.get_executor(),
        [listener = &fixture.listener()]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            (void)co_await listener->async_accept();
        },
        asio::detached);

    ioc.run_for(10s);

    ASSERT_TRUE(first.has_value()) << "First attempt must complete";
    ASSERT_FALSE(first->has_value()) << "Connect to an unresolvable host must fail";
    EXPECT_EQ(first->error(), error::transport_resolve_failed)
        << "Pinned so this cell cannot start passing via some other failure mode";

    ASSERT_TRUE(second.has_value()) << "Retry must complete";
    EXPECT_TRUE(second->has_value())
        << "Retry after a FAILED attempt must really attempt. A guard that never "
           "clears connect_in_flight_ answers transport_already_connected here "
           "while still passing cells 5 and 6. Got error: "
        << (second->has_value() ? 0 : static_cast<int>(second->error()));

    (void)client->close();
    ioc.run_for(200ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cells 8-9: the PLAINTEXT transport's overlap guard.
//
// ⚠️ Cells 5 and 7 above exercise only the TLS transport -- LoopbackTlsFixture's
// make_client mints an asio_tls_transport. #342 added an INDEPENDENT guard to
// asio_plain_transport, and without these two cells deleting it would leave
// every other cell green while plaintext overlapping connects went back to
// closing the socket out from under each other. A per-implementation guard
// needs a per-implementation witness; a mutation row that says "delete the
// connect overlap guard" is otherwise true of only one of the two.
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<Transport> make_plain_client(asio::io_context& ioc) {
    auto factory = make_asio_plain_transport_factory(Transport::Config{});
    if (!factory) throw std::runtime_error("plain factory failed");
    fixpp::tls::SslCtxConfig unused{};
    auto t = (*factory)->make(ioc.get_executor(), unused, nullptr);
    if (!t) throw std::runtime_error("plain make() failed");
    return std::move(*t);
}

asio::ip::tcp::endpoint make_loopback_acceptor(asio::ip::tcp::acceptor& acc) {
    asio::ip::tcp::endpoint ep{asio::ip::address_v4::loopback(), 0};
    acc.open(ep.protocol());
    acc.set_option(asio::ip::tcp::acceptor::reuse_address{true});
    acc.bind(ep);
    acc.listen();
    return acc.local_endpoint();
}

// Cell 8: plaintext async_connect OVERLAP → transport_already_connected (#342).
// Same shape and same spurious-hit arm as cell 5.
TEST(InflightExclusivity, PlaintextConnectOverlapRefusedWhileFirstInFlight) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    const auto bound = make_loopback_acceptor(acc);
    const fixpp::transport::Endpoint ep{"127.0.0.1", bound.port()};

    auto client = make_plain_client(ioc);
    Transport* client_raw = client.get();

    std::optional<expected_t<ConnectInfo>> result_a;
    std::optional<expected_t<ConnectInfo>> result_b;
    std::optional<bool> a_inflight_when_b_issued;

    asio::ip::tcp::socket accepted{ioc};
    acc.async_accept(accepted, [](asio::error_code) {});

    auto strand = asio::make_strand(ioc.get_executor());

    asio::co_spawn(
        strand,
        [&result_a, client_raw, ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            result_a = co_await client_raw->async_connect(ep);
        },
        asio::detached);

    asio::co_spawn(
        strand,
        [&result_b, &result_a, &a_inflight_when_b_issued, client_raw,
         ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            a_inflight_when_b_issued = !result_a.has_value();
            result_b = co_await client_raw->async_connect(ep);
        },
        asio::detached);

    ioc.run_for(5s);

    ASSERT_TRUE(a_inflight_when_b_issued.has_value()) << "Coroutine B never ran";
    EXPECT_TRUE(*a_inflight_when_b_issued)
        << "SPURIOUS-HIT: A had already completed when B issued, so a 97 below would "
           "come from the one-shot state test, not from the overlap guard";

    ASSERT_TRUE(result_b.has_value()) << "B must complete (guard answers immediately)";
    ASSERT_FALSE(result_b->has_value()) << "B must be refused, not attempt";
    EXPECT_EQ(result_b->error(), error::transport_already_connected);

    ASSERT_TRUE(result_a.has_value()) << "A must still complete";
    EXPECT_TRUE(result_a->has_value()) << "A must still succeed";

    (void)client->close();
    asio::error_code ignored;
    acc.close(ignored);
    ioc.run_for(200ms);
}

// Cell 9: plaintext flag CLEARS — retry after a failed attempt really attempts.
TEST(InflightExclusivity, PlaintextConnectRetryableAfterFailedAttempt) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    const auto bound = make_loopback_acceptor(acc);
    const fixpp::transport::Endpoint good{"127.0.0.1", bound.port()};
    const fixpp::transport::Endpoint unresolvable{"no.such.host.invalid", 12345};

    auto client = make_plain_client(ioc);
    Transport* client_raw = client.get();

    std::optional<expected_t<ConnectInfo>> first;
    std::optional<expected_t<ConnectInfo>> second;

    asio::ip::tcp::socket accepted{ioc};
    acc.async_accept(accepted, [](asio::error_code) {});

    asio::co_spawn(
        ioc.get_executor(),
        [&first, &second, client_raw, unresolvable, good]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            first = co_await client_raw->async_connect(unresolvable);
            second = co_await client_raw->async_connect(good);
        },
        asio::detached);

    ioc.run_for(10s);

    ASSERT_TRUE(first.has_value());
    ASSERT_FALSE(first->has_value()) << "unresolvable host must fail";
    EXPECT_EQ(first->error(), error::transport_resolve_failed);

    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(second->has_value())
        << "retry after a FAILED attempt must really attempt — a guard that never clears "
           "connect_in_flight_ answers transport_already_connected here";

    (void)client->close();
    asio::error_code ignored;
    acc.close(ignored);
    ioc.run_for(200ms);
}


// ─────────────────────────────────────────────────────────────────────────────
// Cells 10-11: close() DURING DNS RESOLUTION must not resurrect the Transport
// (#347).
//
// close() is strand-confined and so is async_connect, but async_resolve is not
// the socket: `close()` calls socket_.close(), which the RESOLVER never
// observes. So a close() landing while the connect is suspended in resolution
// used to be overwritten -- the coroutine resumed, asio's composed
// async_connect OPENED the socket it needed, and `state_ = connected` published
// a live socket behind a close() that had already returned, violating FR-006.
//
// Ordering is structural, not timed: A and B share a strand, A suspends in
// async_resolve (the first real suspension point), so B's close() runs while A
// is parked there.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, TlsCloseDuringResolveDoesNotResurrect) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    auto client = fixture.make_client(ioc.get_executor());
    Transport* client_raw = client.get();
    const auto ep = fixture.server_endpoint();

    std::optional<expected_t<ConnectInfo>> connect_result;
    std::optional<bool> closed_while_connect_inflight;

    auto strand = asio::make_strand(ioc.get_executor());

    asio::co_spawn(
        strand,
        [&connect_result, client_raw, ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            connect_result = co_await client_raw->async_connect(ep);
        },
        asio::detached);

    asio::co_spawn(
        strand,
        [&connect_result, &closed_while_connect_inflight, client_raw]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            closed_while_connect_inflight = !connect_result.has_value();
            (void)client_raw->close();
            co_return;
        },
        asio::detached);

    ioc.run_for(5s);

    ASSERT_TRUE(closed_while_connect_inflight.has_value()) << "closer coroutine never ran";
    EXPECT_TRUE(*closed_while_connect_inflight)
        << "VACUOUS: the connect had already finished when close() ran, so this cell would "
           "prove nothing about the resolve window";

    ASSERT_TRUE(connect_result.has_value()) << "connect must complete";
    ASSERT_FALSE(connect_result->has_value())
        << "connect must NOT succeed after close() returned (FR-006)";
    EXPECT_EQ(connect_result->error(), error::transport_already_closed);

    ioc.run_for(200ms);
}

TEST(InflightExclusivity, PlaintextCloseDuringResolveDoesNotResurrect) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    const auto bound = make_loopback_acceptor(acc);
    const fixpp::transport::Endpoint ep{"127.0.0.1", bound.port()};

    auto client = make_plain_client(ioc);
    Transport* client_raw = client.get();

    // No accept is armed: the connect is refused by close() and never lands, so
    // a pending accept would only hold the io_context open for the full window.
    std::optional<expected_t<ConnectInfo>> connect_result;
    std::optional<bool> closed_while_connect_inflight;

    auto strand = asio::make_strand(ioc.get_executor());

    asio::co_spawn(
        strand,
        [&connect_result, client_raw, ep]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            connect_result = co_await client_raw->async_connect(ep);
        },
        asio::detached);

    asio::co_spawn(
        strand,
        [&connect_result, &closed_while_connect_inflight, client_raw]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            closed_while_connect_inflight = !connect_result.has_value();
            (void)client_raw->close();
            co_return;
        },
        asio::detached);

    ioc.run_for(5s);

    ASSERT_TRUE(closed_while_connect_inflight.has_value()) << "closer coroutine never ran";
    EXPECT_TRUE(*closed_while_connect_inflight) << "VACUOUS: connect already finished";

    ASSERT_TRUE(connect_result.has_value());
    ASSERT_FALSE(connect_result->has_value())
        << "connect must NOT succeed after close() returned (FR-006)";
    EXPECT_EQ(connect_result->error(), error::transport_already_closed);

    asio::error_code ignored;
    acc.close(ignored);
    ioc.run_for(200ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 12: what a peer ACTUALLY observes when close() ends a handshaken TLS
// session (#348).
//
// ⚠️ THIS CELL PINS A DEFECT, NOT DESIRED BEHAVIOUR. close()'s published
// contract says it "initiates best-effort bidi TLS shutdown (SSL_shutdown
// close-notify)" bounded by tls_close_timeout, and that a truncated close
// "surfaces as transport_read_truncated and is NOT treated as a hard error".
// MEASURED, deterministically over repeated runs: the peer's in-flight read
// completes with transport_read_error (104) -- an OS-level error, neither the
// clean transport_read_eof of a real close_notify nor the documented
// transport_read_truncated.
//
// Cause (#348): close() calls SSL_shutdown() on the NATIVE handle. asio's
// ssl::stream writes through a BIO pair -- ssl::detail::engine::shutdown()
// generates the alert into the BIO and ssl::detail::io drains it to the socket.
// Nothing drains it here, and socket_.close() immediately after discards it.
//
// It is pinned rather than left unwitnessed so that fixing #348 has to come
// through this assertion deliberately instead of changing behaviour silently.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, CloseDoesNotDeliverCloseNotify_PinsDefect348) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    auto pair = make_handshaken_pair(fixture, ioc);

    std::optional<expected_t<std::size_t>> server_read;
    std::byte buf{0};
    Transport* server_raw = pair.server.get();

    asio::co_spawn(
        ioc.get_executor(),
        [&server_read, server_raw, &buf]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            server_read = co_await server_raw->async_read_some(std::span<std::byte>{&buf, 1});
        },
        asio::detached);
    ioc.run_for(300ms);
    ASSERT_FALSE(server_read.has_value()) << "server read must still be pending before close()";

    (void)pair.client->close();
    ioc.run_for(2s);

    ASSERT_TRUE(server_read.has_value()) << "server read must complete after peer close()";
    ASSERT_FALSE(server_read->has_value());
    EXPECT_EQ(server_read->error(), error::transport_read_error)
        << "#348: measured behaviour. If this now reports transport_read_eof, close() has "
           "started delivering close_notify and #348 is FIXED — update the contract in "
           "transport.hpp and this cell together. If it reports transport_read_truncated, the "
           "alert is being dropped differently than measured; re-derive before editing docs.";

    (void)pair.server->close();
    ioc.run_for(200ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// #348: close_async() DOES deliver close_notify — asserted at the PEER.
//
// The paired positive of cell 12 above, and deliberately the same shape so the
// two can be read against each other: identical fixture, identical pending
// server read, the ONLY difference is client->close_async() instead of
// client->close(). Cell 12 measures transport_read_error; this one must measure
// a clean transport_read_eof.
//
// That difference is the wire-level assertion #348 asks for. It is not "the
// call returned {}" — close() returns {} too, and returns it having thrown the
// alert away. The peer's read outcome is the only thing that can tell a drained
// BIO from an undrained one from outside the process.
//
// ⚠️ RED-ARM CONTRACT: swap close_async() for close() below and this cell must
// fail with transport_read_error — i.e. it must turn into cell 12.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, CloseAsyncDeliversCloseNotify_Fixes348) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};
    auto pair = make_handshaken_pair(fixture, ioc);

    std::optional<expected_t<std::size_t>> server_read;
    std::byte buf{0};
    Transport* server_raw = pair.server.get();

    asio::co_spawn(
        ioc.get_executor(),
        [&server_read, server_raw, &buf]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            server_read = co_await server_raw->async_read_some(std::span<std::byte>{&buf, 1});
        },
        asio::detached);
    // client_raw is hoisted ABOVE the pump deliberately. Left below it, the
    // `pair.client.get()` lands inside ci/pump-census.sh's lookahead window,
    // whose `get_re` matches ANY `ident.get(` and so cannot tell a
    // unique_ptr::get() from the future::get() the census is actually hunting
    // (#289). That made this cell a census FALSE POSITIVE. Hoisting removes it
    // without pinning a site that is not one -- cell 12 above already declares
    // its raw pointer this way.
    Transport* client_raw = pair.client.get();

    ioc.run_for(300ms);
    ASSERT_FALSE(server_read.has_value()) << "server read must still be pending before close";

    std::optional<expected_t<void>> closed;
    const auto t0 = std::chrono::steady_clock::now();
    asio::co_spawn(
        ioc.get_executor(),
        [&closed, client_raw]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            closed = co_await client_raw->close_async();
        },
        asio::detached);
    ioc.run_for(3s);
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    ASSERT_TRUE(closed.has_value()) << "close_async must complete";
    EXPECT_TRUE(closed->has_value());

    // ⚠️ WITHOUT THIS THE CELL CANNOT SEE THE QUICK-SHUTDOWN FIX. The alert is
    // written either way, so the peer's clean EOF below is satisfied even when
    // async_shutdown blocks for the peer's ANSWERING alert and is released only
    // by the deadline. This cell passed at 1.32 s before SSL_set_shutdown existed
    // — inside the same 3 s pump — so the pump alone witnesses nothing.
    //
    // The budget is DERIVED from the competing timeout rather than written as a
    // literal: a reversion parks on tls_close_timeout exactly, so anything
    // comfortably below it discriminates, and the assertion tracks the Config
    // default if it ever moves.
    const auto budget = Transport::Config{}.tls_close_timeout;
    EXPECT_LT(elapsed, budget / 2)
        << "close_async took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
        << ", i.e. it waited on the peer's answering close_notify and was released by the "
           "tls_close_timeout deadline. The SSL_set_shutdown(SSL_RECEIVED_SHUTDOWN) quick "
           "shutdown is missing or ineffective (#348).";

    ASSERT_TRUE(server_read.has_value()) << "server read must complete after the peer's close";
    ASSERT_FALSE(server_read->has_value());
    EXPECT_EQ(server_read->error(), error::transport_read_eof)
        << "#348: close_async() must drain the close_notify alert to the wire, so the peer sees a "
           "CLEAN EOF. transport_read_error here means the alert was generated into the BIO and "
           "discarded — the exact defect cell 12 pins for close().";

    (void)pair.server->close();
    ioc.run_for(200ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 13: D-4.0 destroy-with-no-drain must not fault in the in-flight guard.
//
// Destroys the Transport while async_connect is suspended in resolution, then
// destroys the io_context WITHOUT running it, so the suspended frame is
// DESTROYED rather than resumed — the one path on which a coroutine's in-scope
// destructors run against a Transport that is already gone.
//
// ⚠️ This cell reproduced a REAL heap-use-after-free (ASan: WRITE of size 1 in
// ~inflight_flag_guard) when the in-flight flags were plain Transport members.
// It is green only because the flags now live in the separately-owned
// timer_epoch_state block, which outlives the Transport. Bind the guard to a
// Transport member again and this cell reds under ASan.
//
// ⚠️ ONLY MEANINGFUL UNDER A SANITIZER. Without ASan the stray one-byte write
// lands in freed-but-mapped memory and the cell passes regardless — so a green
// run in the plain debug preset is not evidence. The linux-clang-asan preset is
// where this one earns its keep.
// ─────────────────────────────────────────────────────────────────────────────
TEST(InflightExclusivity, DestroyWithNoDrainDoesNotFaultInFlightGuard) {
    std::optional<expected_t<ConnectInfo>> result;
    {
        asio::io_context ioc;
        asio::ip::tcp::acceptor acc{ioc};
        const auto bound = make_loopback_acceptor(acc);
        const fixpp::transport::Endpoint ep{"127.0.0.1", bound.port()};

        auto client = make_plain_client(ioc);
        Transport* raw = client.get();

        asio::co_spawn(
            ioc.get_executor(),
            [&result, raw, ep]() -> asio::awaitable<void> {
                co_await asio::this_coro::reset_cancellation_state(
                    asio::enable_total_cancellation());
                result = co_await raw->async_connect(ep);
            },
            asio::detached);

        ioc.poll();          // start the coroutine; it suspends in async_resolve
        client.reset();      // D-4.0: destroy the Transport with the op in flight
        asio::error_code ig;
        acc.close(ig);
        // ioc destructor here destroys the suspended frame -> ~inflight_flag_guard
    }
    SUCCEED() << "no fault under the sanitizer";
}

// ─────────────────────────────────────────────────────────────────────────────
// #346 — read/write in-flight flags are RAII-guarded; the wedge they prevent has
// NO behavioural witness here, deliberately.
//
// The CONDITION: a wedge requires a read/write frame DESTROYED while suspended
// AND a Transport that survives to exhibit the stuck flag. asio destroys a
// suspended frame only when the handler chain is destroyed unrun -- via
// ~awaitable_thread, i.e. io_context destruction -- and the Transport's executor
// does not outlive that. (~awaitable also destroys a frame, but only one stopped
// at initial_suspend, where the guard was never constructed.) Cancellation, by
// contrast, RESUMES the frame with operation_aborted, which is why a plain
// assignment below the co_await survives that path and the cell built on it was
// vacuous — which is why cell 13 above
// can only assert "no fault" and not "not wedged".
//
// Two public-surface shapes were built to break that and BOTH were measured
// unsound — see #346 and the gate record before rebuilding either. ⚠️ Do not
// discharge this by adding a cell that goes green: show it RED first on a tree
// with the guards reverted to plain assignment, which is the step that killed
// both attempts. The claim rests on STRUCTURE, the way B-339-1 does.
// ─────────────────────────────────────────────────────────────────────────────

}  // namespace
