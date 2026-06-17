// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_asio_plain_transport_config.cpp
// T006 — asio_plain_transport Transport::Config TCP knob + fast-close witness.
//
// Verifies SC-008 (FR-010 / FR-011):
//   (a) Non-default Transport::Config TCP knobs take effect on the established
//       plain socket (tcp_keepalive observable via get_option after connect;
//       tcp_keepalive is non-default in fixpp AND kernel-default is OFF, so
//       reading back true discriminates that apply_socket_options_() ran).
//   (b) close() returns promptly: NO tls_close_timeout delay, NO TLS
//       close-notify bytes emitted. Measured by timing: < 500 ms with explicit
//       tls_close_timeout=2s in cfg (ensures the TLS path would block but plain
//       does not). Also verified that the peer sees clean EOF on socket_.close()
//       and does NOT receive any TLS handshake / close-notify bytes (0x15/0x16).
//
// Design anchors: research.md D-2/D-12, data-model.md E-2, spec.md FR-010/FR-011,
//   contracts/asio_plain_transport.hpp, contracts/plain_transport_factory.hpp.

#include <gtest/gtest.h>

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "transport/asio_plain_transport.hpp"

namespace fixpp::transport {

// Test-access class — mirrors asio_tls_transport_test_access pattern.
// Declared as friend in asio_plain_transport.hpp; grants read-only access
// to the private socket_ for get_option verification (T006 SC-008).
class asio_plain_transport_test_access {
public:
    static const asio::ip::tcp::socket& socket_of(
        const asio_plain_transport& t) noexcept {
        return t.socket_;
    }
};

}  // namespace fixpp::transport

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::transport::asio_plain_transport;
using fixpp::transport::asio_plain_transport_test_access;
using fixpp::transport::Transport;

// ── Helper ────────────────────────────────────────────────────────────────────
asio::ip::tcp::endpoint make_loopback_acceptor(asio::io_context& ioc,
                                               asio::ip::tcp::acceptor& acc) {
    asio::ip::tcp::endpoint ep{asio::ip::address_v4::loopback(), 0};
    acc.open(ep.protocol());
    acc.set_option(asio::ip::tcp::acceptor::reuse_address{true});
    acc.bind(ep);
    acc.listen();
    return acc.local_endpoint();
}

// ── Test (a): TCP knobs observable on the established socket ──────────────────
//
// Sets tcp_keepalive=true (non-default — FR-029 default is false; kernel default
// is also off), asserts get_option(SO_KEEPALIVE) reads back true after connect.
// This uses asio_plain_transport_test_access to reach the private socket_ directly.
// Discriminates BOTH "apply_socket_options_ ran" AND "Config was honored" because
// keepalive is OFF by default in both fixpp and the kernel.
TEST(AsioPlainTransportConfig, TcpKeepaliveApplied) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    bool timed_out{false};
    bool done{false};
    bool keepalive_observed{false};

    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(std::chrono::seconds{10});
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec && !done) {
            timed_out = true;
            asio::error_code ignored;
            acc.close(ignored);
        }
    });

    // Server: accept and close immediately.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            asio::ip::tcp::socket peer{co_await asio::this_coro::executor};
            co_await acc.async_accept(peer, asio::redirect_error(asio::use_awaitable, ec));
        },
        asio::detached);

    // Client: connect with tcp_keepalive=true, then read back the option.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            Transport::Config cfg{};
            cfg.tcp_keepalive = true;  // non-default in fixpp + kernel-default is OFF
            asio_plain_transport client{co_await asio::this_coro::executor, cfg};

            fixpp::transport::Endpoint endpoint;
            endpoint.host = "127.0.0.1";
            endpoint.port = ep.port();

            auto conn = co_await client.async_connect(endpoint);
            if (!conn) co_return;

            // Verify via test-access friend — discriminates that apply_socket_options_()
            // actually ran AND honored the config knob.
            const auto& sock = asio_plain_transport_test_access::socket_of(client);
            asio::socket_base::keep_alive keepalive_opt;
            asio::error_code get_ec;
            sock.get_option(keepalive_opt, get_ec);
            if (!get_ec) {
                keepalive_observed = keepalive_opt.value();
            }

            done = true;
            watchdog.cancel();
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{12});

    ASSERT_FALSE(timed_out) << "test timed out";
    EXPECT_TRUE(keepalive_observed)
        << "tcp_keepalive=true must be observable via SO_KEEPALIVE after connect; "
           "apply_socket_options_() must have run and honored the config";
}

// ── Test (b): close() is prompt — no tls_close_timeout delay ──────────────────
//
// Sets tls_close_timeout=2s in cfg (even though plain transport ignores it).
// Measures wall time of close(): must complete in < 500 ms.
// Also: the peer side drains its socket and verifies the first byte read
// from any data the closing side might have sent is NOT 0x15 (TLS
// close-notify alert) and NOT 0x16 (TLS handshake record) — confirming
// no TLS shutdown bytes were emitted.
TEST(AsioPlainTransportConfig, CloseIsPromptNoTlsCloseNotify) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    bool timed_out{false};
    bool done{false};
    std::chrono::steady_clock::duration close_duration{};
    // Byte received at peer just before close (if any).
    std::optional<std::byte> first_byte_at_peer;
    bool peer_got_eof{false};

    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(std::chrono::seconds{10});
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec && !done) {
            timed_out = true;
            asio::error_code ignored;
            acc.close(ignored);
        }
    });

    // Server: accept raw socket, read everything until EOF or error.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            asio::ip::tcp::socket peer{co_await asio::this_coro::executor};
            co_await acc.async_accept(peer, asio::redirect_error(asio::use_awaitable, ec));
            if (ec) co_return;

            // Read until EOF/error.
            std::array<std::byte, 64> buf{};
            while (true) {
                std::size_t n = co_await peer.async_read_some(
                    asio::buffer(buf.data(), buf.size()),
                    asio::redirect_error(asio::use_awaitable, ec));
                if (ec == asio::error::eof || ec == asio::error::connection_reset) {
                    peer_got_eof = true;
                    break;
                }
                if (ec) break;
                if (n > 0 && !first_byte_at_peer) {
                    first_byte_at_peer = buf[0];
                }
            }
        },
        asio::detached);

    // Client: connect, then call close() and measure duration.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            // cfg with a deliberately large tls_close_timeout to prove plain transport
            // does NOT wait for it.
            Transport::Config cfg{};
            cfg.tls_close_timeout = std::chrono::seconds{2};
            asio_plain_transport client{co_await asio::this_coro::executor, cfg};

            fixpp::transport::Endpoint endpoint;
            endpoint.host = "127.0.0.1";
            endpoint.port = ep.port();

            auto conn = co_await client.async_connect(endpoint);
            if (!conn) co_return;

            auto t0 = std::chrono::steady_clock::now();
            (void)client.close();
            auto t1 = std::chrono::steady_clock::now();
            close_duration = t1 - t0;

            done = true;
            watchdog.cancel();
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{12});

    ASSERT_FALSE(timed_out) << "test timed out";

    // FR-011: plain close() must NOT block for tls_close_timeout.
    auto close_ms = std::chrono::duration_cast<std::chrono::milliseconds>(close_duration).count();
    EXPECT_LT(close_ms, 500)
        << "close() must be prompt (< 500ms); plain transport must NOT block on "
           "tls_close_timeout. Got: " << close_ms << "ms (cfg had 2s tls_close_timeout)";

    // SC-001 / FR-011: plain transport emits ZERO bytes before close.
    // The peer must see EOF with NO bytes received (no TLS close-notify 0x15,
    // no TLS handshake record 0x16, and no bytes at all). This is a MANDATORY
    // assertion — not gated on first_byte_at_peer — because the contract is
    // "no bytes emitted on close", so the correct outcome is no first byte.
    EXPECT_FALSE(first_byte_at_peer.has_value())
        << "plain transport close() must emit NO bytes (0 bytes before EOF); "
           "got first_byte=0x" << (first_byte_at_peer
               ? static_cast<unsigned>(*first_byte_at_peer) : 0u)
        << " — TLS close-notify=0x15, TLS handshake=0x16 are both forbidden";

    // Peer saw a clean EOF (socket was closed, not just cancelled).
    EXPECT_TRUE(peer_got_eof) << "peer must see clean EOF when plain transport closes";
}

// ── Test (c): async_connect on a fresh-state transport returns ConnectInfo ─────
TEST(AsioPlainTransportConfig, AsyncConnectReturnsConnectInfo) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    bool timed_out{false};
    bool done{false};
    bool got_connect_info{false};

    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(std::chrono::seconds{10});
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec && !done) {
            timed_out = true;
            asio::error_code ignored;
            acc.close(ignored);
        }
    });

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            asio::ip::tcp::socket peer{co_await asio::this_coro::executor};
            co_await acc.async_accept(peer, asio::redirect_error(asio::use_awaitable, ec));
        },
        asio::detached);

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            Transport::Config cfg{};
            asio_plain_transport client{co_await asio::this_coro::executor, cfg};

            fixpp::transport::Endpoint endpoint;
            endpoint.host = "127.0.0.1";
            endpoint.port = ep.port();

            auto conn = co_await client.async_connect(endpoint);
            if (conn) {
                // ConnectInfo must have valid remote host/port.
                got_connect_info =
                    !conn->remote.host.empty() && conn->remote.port == ep.port();
            }

            // Second call must return transport_already_connected.
            auto conn2 = co_await client.async_connect(endpoint);
            EXPECT_FALSE(conn2.has_value());
            if (!conn2.has_value()) {
                EXPECT_EQ(conn2.error(), error::transport_already_connected)
                    << "second async_connect on connected transport must return "
                       "transport_already_connected";
            }

            done = true;
            watchdog.cancel();
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{12});

    ASSERT_FALSE(timed_out) << "test timed out";
    EXPECT_TRUE(got_connect_info) << "async_connect must return valid ConnectInfo";
}

}  // namespace
