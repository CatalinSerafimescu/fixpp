// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_asio_tls_transport_timer_epochs.cpp
// T036 (088-firstframe-budget-timer-lifetime, US4) — asio_tls_transport
// timer-epoch retirement witnesses (SC-014).
//
// Cells:
//   T4 (TLS connect)   — timer_epochs()->connect retired past its armed
//                         value by the time a real successful async_connect
//                         returns.
//   T5 (TLS handshake) — timer_epochs()->handshake retired past its armed
//                         value by the time a real successful async_handshake
//                         returns.
//
// C4 requires one pin per site (research.md D-6.1/D-6.4), so T4 and T5 are
// kept as two distinct TEST cells in this one file rather than merged.
//
// RED basis: the retire-point-omitted mutant only (research.md D-6.4) —
// delete the `++` at the retire site immediately before the existing
// `timer.cancel()`. The sibling "guard omitted" mutant has no killer and is
// discharged structurally (D-9); no cell here claims to kill it.
//
// Resolves plan.md:240's "(TLS transport test target)" placeholder — no
// existing file fits: test_asio_tls_transport_error_paths.cpp is scoped to
// error paths, and the other TLS files (pinset rotation, CompID identity
// binding, validation taxonomy) are unrelated.
//
// Design anchors: research.md D-4.1, D-5, D-6.1, D-6.4; tasks.md T036.

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <chrono>
#include <cstdint>
#include <fixpp/transport/transport.hpp>
#include <memory>

#include "transport/asio_listener.hpp"
#include "transport/asio_tls_transport.hpp"
#include "transport/loopback_tls_fixture.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::transport::asio_tls_transport;
using fixpp::transport::Transport;
using fixpp::transport::test::LoopbackTlsFixture;

#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

// ── T4 (SC-014) — connect epoch retired after a real successful TLS connect ──
//
// Drives a real successful async_connect against the fixture's loopback
// listener (a stub peer here — the server side only needs to accept the TCP
// connection; no handshake is driven on it, since T4's construction is the
// connect leg only). A fresh transport starts at connect == 0; the arm site
// increments it to 1 when the attempt begins, and the retire site increments
// it again to 2 immediately before timer.cancel() — so a successful connect
// leaves connect == 2, past the armed value of 1. Deleting the retire-site
// `++` (retire-point-omitted) leaves it at 1, equal to the armed value — RED.
TEST(AsioTlsTransportTimerEpochs, T4ConnectEpochRetiredAfterConnect) {
    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    bool timed_out{false};
    bool done{false};
    bool connect_ok{false};
    std::uint64_t epoch_after_connect{0};

    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(10s);
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec && !done) {
            timed_out = true;
            fixture.cancel_listener();
        }
    });

    // Server: accept the TCP connection and hold it open (dropped when the
    // coroutine ends). T4 only needs the connect leg to succeed.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            (void)co_await fixture.listener().async_accept();
        },
        asio::detached);

    // Client: real successful connect, then read the epoch before the
    // transport (a coroutine-frame local) leaves scope.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            auto client = fixture.make_client(co_await asio::this_coro::executor);
            auto* tls = dynamic_cast<asio_tls_transport*>(client.get());
            if (tls == nullptr) co_return;

            auto conn = co_await tls->async_connect(fixture.server_endpoint());
            connect_ok = conn.has_value();
            epoch_after_connect = tls->timer_epochs()->connect;

            done = true;
            watchdog.cancel();
        },
        asio::detached);

    ioc.run_for(12s);

    ASSERT_FALSE(timed_out) << "test timed out";
    ASSERT_TRUE(connect_ok) << "async_connect must succeed for T4's construction";
    EXPECT_EQ(epoch_after_connect, 2u)
        << "T4 (SC-014): connect epoch must be retired (advanced past the armed "
           "value 1) by the time async_connect returns — arm (0->1) then retire "
           "(1->2); got "
        << epoch_after_connect;
}

// ── T5 (SC-014) — handshake epoch retired after a real successful handshake ──
//
// Same shape as T4, against timer_epochs()->handshake — a SEPARATE counter
// per D-4.1's class-body rationale (split even though today's callers happen
// to serialize connect then handshake). Drives a real successful
// async_connect THEN async_handshake against the fixture's real mTLS
// listener (both sides use the same leaf_rsa2048 cert / ca.pem trust —
// mtls_ca profile), so the peer's own handshake must also complete.
TEST(AsioTlsTransportTimerEpochs, T5HandshakeEpochRetiredAfterHandshake) {
    asio::io_context ioc;
    LoopbackTlsFixture fixture{FIXPP_TLS_FIXTURE_DIR, ioc.get_executor()};

    bool timed_out{false};
    bool done{false};
    bool handshake_ok{false};
    std::uint64_t epoch_after_handshake{0};

    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(10s);
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec && !done) {
            timed_out = true;
            fixture.cancel_listener();
        }
    });

    // Server: accept + handshake (a real peer is needed for the client's
    // handshake to complete at all).
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            auto accept_result = co_await fixture.listener().async_accept();
            if (!accept_result.has_value()) co_return;
            auto* tls = dynamic_cast<asio_tls_transport*>(accept_result->get());
            if (tls == nullptr) co_return;
            (void)co_await tls->async_handshake(fixture.ssl_cfg());
        },
        asio::detached);

    // Client: real successful connect + handshake, then read the epoch
    // before the transport leaves scope.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            auto client = fixture.make_client(co_await asio::this_coro::executor);
            auto* tls = dynamic_cast<asio_tls_transport*>(client.get());
            if (tls == nullptr) co_return;

            auto conn = co_await tls->async_connect(fixture.server_endpoint());
            if (!conn.has_value()) co_return;

            auto hs = co_await tls->async_handshake(fixture.ssl_cfg());
            handshake_ok = hs.has_value();
            epoch_after_handshake = tls->timer_epochs()->handshake;

            done = true;
            watchdog.cancel();
        },
        asio::detached);

    ioc.run_for(12s);

    ASSERT_FALSE(timed_out) << "test timed out";
    ASSERT_TRUE(handshake_ok) << "async_handshake must succeed for T5's construction";
    EXPECT_EQ(epoch_after_handshake, 2u)
        << "T5 (SC-014): handshake epoch must be retired (advanced past the "
           "armed value 1) by the time async_handshake returns — arm (0->1) "
           "then retire (1->2); got "
        << epoch_after_handshake;
}

}  // namespace
