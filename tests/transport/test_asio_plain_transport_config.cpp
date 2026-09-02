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
#include <memory>
#include <optional>
#include <span>
#include <vector>

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

// ─────────────────────────────────────────────────────────────────────────────
// Socket-option knobs that apply_socket_options_() honours but nothing read
// back: SO_LINGER (enabled arm), SO_RCVBUF and SO_SNDBUF.
//
// These three `if` arms were the only untaken branches left in
// apply_socket_options_(): the defaults are so_linger_enabled=false and
// {recv,send}_buf_bytes=0, so every existing cell drove the OTHER side of each
// branch. A knob that is written but never read back is indistinguishable from
// a knob that is silently ignored -- which is the whole reason the keepalive
// cell above reads its option back rather than trusting the assignment.
//
// ⚠️ The kernel is allowed to ROUND buffer sizes (Linux doubles SO_RCVBUF/
// SO_SNDBUF and enforces its own floor), so these assert "changed from the
// default in the direction we asked", NOT equality with the requested value.
// An equality assertion here would be a cell that fails on the platform rather
// than on the defect.
// ─────────────────────────────────────────────────────────────────────────────
// Well above any platform's SO_{RCV,SND}BUF floor, so a kernel round is always
// upward and >= cannot be satisfied by the floor alone.
static constexpr int kRequestedBufBytes = 256 * 1024;

TEST(AsioPlainTransportConfig, LingerAndBufferSizeKnobsApplied) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    bool done{false};
    bool linger_on{false};
    int linger_secs{-1};
    int recv_buf{0};
    int send_buf{0};

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
            cfg.so_linger_enabled = true;  // default false -> takes the else arm
            cfg.so_linger_seconds = 3;
            cfg.tcp_recv_buf_bytes = kRequestedBufBytes;  // default 0 -> block not entered
            cfg.tcp_send_buf_bytes = kRequestedBufBytes;
            asio_plain_transport client{co_await asio::this_coro::executor, cfg};

            fixpp::transport::Endpoint endpoint;
            endpoint.host = "127.0.0.1";
            endpoint.port = ep.port();

            auto conn = co_await client.async_connect(endpoint);
            if (!conn) co_return;

            const auto& sock = asio_plain_transport_test_access::socket_of(client);
            asio::error_code get_ec;
            asio::socket_base::linger linger_opt;
            sock.get_option(linger_opt, get_ec);
            if (!get_ec) {
                linger_on = linger_opt.enabled();
                linger_secs = linger_opt.timeout();
            }
            asio::socket_base::receive_buffer_size r;
            asio::socket_base::send_buffer_size w;
            sock.get_option(r, get_ec);
            recv_buf = r.value();
            sock.get_option(w, get_ec);
            send_buf = w.value();

            done = true;
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{12});

    ASSERT_TRUE(done) << "client coroutine did not complete";
    EXPECT_TRUE(linger_on) << "so_linger_enabled=true must reach SO_LINGER";
    EXPECT_EQ(linger_secs, 3) << "so_linger_seconds must be the value configured";
    // ⚠️ NOT a comparison against an unconnected probe socket. That was the first
    // shape here and its SEND arm was VACUOUS: Linux raises SO_SNDBUF at connect
    // time on its own (measured on this host: 16384 unconnected -> 87040
    // connected with NO option set), so `connected > unconnected` was satisfied
    // by the kernel and stayed green with the tcp_send_buf_bytes block DELETED —
    // the exact mutation the cell exists to catch. The recv arm happened to be
    // sound (131072 both ways), which is why one rationale covering both arms hid
    // the asymmetry. The two knobs are separate `if` blocks, so the live arm
    // could not cover the dead one.
    //
    // Assert against the REQUESTED value instead: the kernel may round UP (Linux
    // returns 2x the request, Windows returns it exactly) but never silently
    // down for a request this far above any floor, so >= discriminates in both
    // directions without encoding a platform's doubling.
    EXPECT_GE(recv_buf, kRequestedBufBytes)
        << "tcp_recv_buf_bytes must reach SO_RCVBUF";
    EXPECT_GE(send_buf, kRequestedBufBytes)
        << "tcp_send_buf_bytes must reach SO_SNDBUF";
}

// ─────────────────────────────────────────────────────────────────────────────
// The PLAINTEXT read/write overlap guards (FR-007) -- 99 and 100.
//
// test_inflight_exclusivity.cpp witnesses the overlap guards on the TLS
// transport and the CONNECT guard on both, but the plaintext read and write
// guards had no cell at all: both `if (..._in_flight)` branches were untaken in
// coverage. That is the same per-implementation gap the #342 cells call out --
// a mutation deleting the plaintext read guard would leave every existing cell
// green.
//
// Shape: connect to a peer that accepts and stays SILENT, so the first read
// suspends and is genuinely still in flight when the second is issued.
// ─────────────────────────────────────────────────────────────────────────────
TEST(AsioPlainTransportConfig, PlaintextReadAndWriteOverlapRefused) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    asio::ip::tcp::socket peer{ioc};
    acc.async_accept(peer, [](asio::error_code) {});

    std::optional<fixpp::core::expected_t<std::size_t>> second_read;
    std::optional<fixpp::core::expected_t<std::size_t>> second_write;
    bool connected{false};

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
            Transport::Config cfg{};
            auto client =
                std::make_unique<asio_plain_transport>(co_await asio::this_coro::executor, cfg);

            fixpp::transport::Endpoint endpoint;
            endpoint.host = "127.0.0.1";
            endpoint.port = ep.port();
            auto conn = co_await client->async_connect(endpoint);
            if (!conn) co_return;
            connected = true;

            auto* raw = client.get();
            std::array<std::byte, 32> buf1{};
            std::array<std::byte, 32> buf2{};

            // First read: suspends (peer is silent) and stays in flight.
            asio::co_spawn(
                co_await asio::this_coro::executor,
                [raw, &buf1]() -> asio::awaitable<void> {
                    co_await asio::this_coro::reset_cancellation_state(
                        asio::enable_total_cancellation());
                    (void)co_await raw->async_read_some(std::span<std::byte>{buf1});
                },
                asio::detached);

            // Yield so the first read reaches its suspension point.
            asio::steady_timer t{co_await asio::this_coro::executor};
            t.expires_after(std::chrono::milliseconds{100});
            co_await t.async_wait(asio::use_awaitable);

            second_read = co_await raw->async_read_some(std::span<std::byte>{buf2});

            // Same for write: a 4 MiB payload against a peer that never reads
            // cannot drain, so the first write stays in flight.
            // shared_ptr, not static: the detached write coroutine outlives this
            // frame, so a frame-local buffer would dangle — but a static would
            // retain 4 MiB for the process lifetime (measured: +4 MiB maxrss)
            // and add a guard on every pass. Size is chosen to defeat
            // sndbuf+peer-rcvbuf autotuning; do not shrink it.
            auto big = std::make_shared<std::vector<std::byte>>(1 << 22, std::byte{0xCD});
            std::array<std::byte, 8> small{};
            asio::co_spawn(
                co_await asio::this_coro::executor,
                [raw, big]() -> asio::awaitable<void> {
                    co_await asio::this_coro::reset_cancellation_state(
                        asio::enable_total_cancellation());
                    (void)co_await raw->async_write(std::span<const std::byte>{*big});
                },
                asio::detached);
            t.expires_after(std::chrono::milliseconds{100});
            co_await t.async_wait(asio::use_awaitable);

            second_write = co_await raw->async_write(std::span<const std::byte>{small});

            (void)client->close();
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{15});

    ASSERT_TRUE(connected) << "client failed to connect";
    ASSERT_TRUE(second_read.has_value()) << "overlapping read must answer IMMEDIATELY, not suspend";
    ASSERT_FALSE(second_read->has_value());
    EXPECT_EQ(second_read->error(), fixpp::core::error::transport_read_in_progress)
        << "plaintext read overlap must be refused with 99 (FR-007)";

    ASSERT_TRUE(second_write.has_value())
        << "overlapping write must answer IMMEDIATELY, not suspend";
    ASSERT_FALSE(second_write->has_value());
    EXPECT_EQ(second_write->error(), fixpp::core::error::transport_write_in_progress)
        << "plaintext write overlap must be refused with 100 (FR-007)";
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
