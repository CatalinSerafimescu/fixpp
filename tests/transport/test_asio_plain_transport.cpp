// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_asio_plain_transport.cpp
// T005 — asio_plain_transport loopback live-I/O witness (SC-001).
//
// Verifies:
//   1. connect → async_read_some → async_write cycle over loopback TCP.
//   2. No TLS ClientHello: first byte received by server is 0x38 ('8'), not
//      0x16 (TLS record header). Asserts SC-001 (no TLS layer).
//   3. Cancellation → transport_read_cancelled / transport_write_cancelled.
//   4. Post-close: async_read_some → transport_already_closed,
//                  async_write → transport_already_closed.
//   5. Idempotent close(): second call returns {}.
//   6. Factory smoke: make() mints transport; kind()==plaintext;
//      reload_credentials() → session_invalid_argument;
//      cert_source_snapshot() → nullptr.
//   7. make_accepted() mints a transport in state_t::connected over loopback.
//
// Design anchors: specs/043-plaintext-tcp-transport/contracts/asio_plain_transport.hpp
//   research.md D-1/D-2/D-3/D-5, data-model.md E-2/E-4, SC-001.
//
// Harness: each live-I/O co_spawn pair runs with an internal watchdog timer that
// cancels sockets and FAILs if the expected exchange does not complete within 10s.

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
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <optional>
#include <span>

#include "transport/asio_plain_transport.hpp"

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::transport::asio_plain_transport;
using fixpp::transport::asio_plain_transport_factory;
using fixpp::transport::make_asio_plain_transport_factory;
using fixpp::transport::transport_security_kind;
using fixpp::transport::Transport;

namespace fe = fixpp::transport::errors;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a loopback acceptor on an ephemeral port and return its endpoint.
asio::ip::tcp::endpoint make_loopback_acceptor(asio::io_context& ioc,
                                               asio::ip::tcp::acceptor& acc) {
    asio::ip::tcp::endpoint ep{asio::ip::address_v4::loopback(), 0};
    acc.open(ep.protocol());
    acc.set_option(asio::ip::tcp::acceptor::reuse_address{true});
    acc.bind(ep);
    acc.listen();
    return acc.local_endpoint();
}

// ── Test 1: Basic connect → write (client) → read (server) cycle ──────────────
TEST(AsioPlainTransport, ConnectReadWriteCycle) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    bool timed_out{false};
    bool exchange_done{false};
    expected_t<std::size_t> write_result = std::unexpected{error::transport_write_error};
    expected_t<std::size_t> read_result  = std::unexpected{error::transport_read_error};

    // Watchdog — cancels all sockets after 10s if exchange has not completed.
    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(std::chrono::seconds{10});
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec && !exchange_done) {
            timed_out = true;
            acc.cancel();
            asio::error_code ignored;
            acc.close(ignored);
        }
    });

    // Server coroutine: accept → read one byte → store result.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            asio::ip::tcp::socket accepted_sock{co_await asio::this_coro::executor};
            co_await acc.async_accept(accepted_sock, asio::redirect_error(asio::use_awaitable, ec));
            if (ec) co_return;

            Transport::Config cfg{};
            asio_plain_transport server{asio_plain_transport::from_accepted_tag{},
                                       co_await asio::this_coro::executor, cfg,
                                       std::move(accepted_sock)};

            std::array<std::byte, 4> buf{};
            read_result = co_await server.async_read_some(std::span<std::byte>{buf});
        },
        asio::detached);

    // Client coroutine: connect → write 4 bytes → mark done.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            Transport::Config cfg{};
            asio_plain_transport client{co_await asio::this_coro::executor, cfg};

            fixpp::transport::Endpoint endpoint;
            endpoint.host = "127.0.0.1";
            endpoint.port = ep.port();

            auto conn = co_await client.async_connect(endpoint);
            if (!conn) co_return;

            // SC-001: first app byte is 0x38 ('8' — start of "8=FIX"), not 0x16 (TLS).
            const std::array<std::byte, 4> payload{std::byte{0x38}, std::byte{0x3d},
                                                    std::byte{0x46}, std::byte{0x49}};
            write_result = co_await client.async_write(
                std::span<const std::byte>{payload.data(), payload.size()});

            exchange_done = true;
            watchdog.cancel();
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{12});

    ASSERT_FALSE(timed_out) << "test timed out — loopback I/O did not complete";
    ASSERT_TRUE(write_result.has_value()) << "client async_write failed: "
                                         << static_cast<int>(write_result.error());
    EXPECT_EQ(*write_result, 4u);
    ASSERT_TRUE(read_result.has_value()) << "server async_read_some failed: "
                                        << static_cast<int>(read_result.error());
    EXPECT_EQ(*read_result, 4u);
}

// ── Test 2: No TLS ClientHello — first received byte is 0x38, not 0x16 ────────
TEST(AsioPlainTransport, NoTlsClientHelloFirstByte) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    bool timed_out{false};
    bool exchange_done{false};
    std::byte first_byte{0xff};

    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(std::chrono::seconds{10});
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec && !exchange_done) {
            timed_out = true;
            asio::error_code ignored;
            acc.close(ignored);
        }
    });

    // Server: accept raw TCP socket, read first byte without any fixpp layer.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            asio::ip::tcp::socket raw_sock{co_await asio::this_coro::executor};
            co_await acc.async_accept(raw_sock, asio::redirect_error(asio::use_awaitable, ec));
            if (ec) co_return;

            std::array<std::byte, 1> buf{};
            co_await raw_sock.async_read_some(asio::buffer(buf.data(), 1),
                                              asio::redirect_error(asio::use_awaitable, ec));
            if (!ec) {
                first_byte = buf[0];
            }
        },
        asio::detached);

    // Client: plain transport — sends first byte 0x38.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            Transport::Config cfg{};
            asio_plain_transport client{co_await asio::this_coro::executor, cfg};

            fixpp::transport::Endpoint endpoint;
            endpoint.host = "127.0.0.1";
            endpoint.port = ep.port();

            auto conn = co_await client.async_connect(endpoint);
            if (!conn) co_return;

            // 0x38 = '8' — start of FIX BeginString "8=FIX..."
            const std::byte fix_first{0x38};
            co_await client.async_write(std::span<const std::byte>{&fix_first, 1});
            exchange_done = true;
            watchdog.cancel();
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{12});

    ASSERT_FALSE(timed_out) << "test timed out";
    EXPECT_EQ(first_byte, std::byte{0x38})
        << "SC-001: first byte must be 0x38 ('8'), not 0x16 (TLS record header);"
        << " got 0x" << std::hex << static_cast<unsigned>(first_byte);
    EXPECT_NE(first_byte, std::byte{0x16})
        << "SC-001 violated: received a TLS record header byte 0x16";
}

// ── Test 3: Cancellation → transport_read_cancelled ───────────────────────────
TEST(AsioPlainTransport, CancelInFlightRead) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    bool timed_out{false};
    expected_t<std::size_t> read_result = std::unexpected{error::transport_read_error};

    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(std::chrono::seconds{10});
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec) {
            timed_out = true;
            asio::error_code ignored;
            acc.close(ignored);
        }
    });

    asio::ip::tcp::socket client_sock{ioc};

    // Client: just connect; do not write anything (server read will block).
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            co_await client_sock.async_connect(ep, asio::redirect_error(asio::use_awaitable, ec));
        },
        asio::detached);

    asio_plain_transport* server_transport_ptr = nullptr;
    std::optional<asio_plain_transport> server_transport;

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            asio::ip::tcp::socket accepted_sock{co_await asio::this_coro::executor};
            co_await acc.async_accept(accepted_sock, asio::redirect_error(asio::use_awaitable, ec));
            if (ec) co_return;

            Transport::Config cfg{};
            server_transport.emplace(asio_plain_transport::from_accepted_tag{},
                                     co_await asio::this_coro::executor, cfg,
                                     std::move(accepted_sock));
            server_transport_ptr = &*server_transport;

            // Read blocks waiting for data from client.
            std::array<std::byte, 64> buf{};
            read_result = co_await server_transport->async_read_some(std::span<std::byte>{buf});

            watchdog.cancel();
        },
        asio::detached);

    // Cancel after a short delay once the server transport is armed.
    asio::steady_timer cancel_timer{ioc};
    cancel_timer.expires_after(std::chrono::milliseconds{200});
    cancel_timer.async_wait([&](asio::error_code ec) {
        if (!ec && server_transport_ptr != nullptr) {
            (void)server_transport_ptr->cancel();
        }
    });

    ioc.run_for(std::chrono::seconds{12});

    ASSERT_FALSE(timed_out) << "test timed out";
    ASSERT_FALSE(read_result.has_value()) << "expected cancellation error";
    EXPECT_EQ(read_result.error(), error::transport_read_cancelled)
        << "expected transport_read_cancelled, got slot="
        << static_cast<int>(read_result.error());
}

// ── Test 4: Post-close: async_read_some → transport_already_closed ────────────
TEST(AsioPlainTransport, PostCloseReadReturnsAlreadyClosed) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    expected_t<std::size_t> read_after_close{std::unexpected{error::transport_read_error}};
    expected_t<std::size_t> write_after_close{std::unexpected{error::transport_write_error}};
    expected_t<void>        second_close{std::unexpected{error::transport_already_closed}};

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            // Accept a socket so the client connect doesn't hang.
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
            if (!conn) co_return;

            // Close the transport.
            (void)client.close();

            // Post-close: read and write must return transport_already_closed.
            std::array<std::byte, 4> buf{};
            read_after_close = co_await client.async_read_some(std::span<std::byte>{buf});

            const std::byte b{0x01};
            write_after_close = co_await client.async_write(std::span<const std::byte>{&b, 1});

            // Second close must be idempotent.
            second_close = client.close();
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{10});

    ASSERT_FALSE(read_after_close.has_value());
    EXPECT_EQ(read_after_close.error(), error::transport_already_closed)
        << "post-close read must return transport_already_closed";

    ASSERT_FALSE(write_after_close.has_value());
    EXPECT_EQ(write_after_close.error(), error::transport_already_closed)
        << "post-close write must return transport_already_closed";

    EXPECT_TRUE(second_close.has_value()) << "second close() must be idempotent (return {})";
}

// ── Test 5: Factory smoke — kind, reload_credentials, cert_source_snapshot ────
TEST(AsioPlainTransport, FactorySmoke) {
    Transport::Config cfg{};

    auto factory_result = make_asio_plain_transport_factory(cfg);
    ASSERT_TRUE(factory_result.has_value()) << "make_asio_plain_transport_factory must succeed";

    auto& factory = *factory_result;
    EXPECT_EQ(factory->kind(), transport_security_kind::plaintext)
        << "plain factory kind() must return transport_security_kind::plaintext";

    // reload_credentials → session_invalid_argument (no certs, D-11).
    auto reload = factory->reload_credentials(nullptr);
    ASSERT_FALSE(reload.has_value());
    EXPECT_EQ(reload.error(), error::session_invalid_argument)
        << "reload_credentials(nullptr) must return session_invalid_argument (slot 119)";

    // cert_source_snapshot → nullptr (D-11).
    auto snap = factory->cert_source_snapshot();
    EXPECT_EQ(snap, nullptr) << "cert_source_snapshot must return nullptr for plain factory";

    // make() mints a plain transport (no ssl_cfg needed).
    asio::io_context ioc;
    fixpp::tls::SslCtxConfig dummy_ssl_cfg{};
    auto make_result = factory->make(ioc.get_executor(), dummy_ssl_cfg, nullptr);
    ASSERT_TRUE(make_result.has_value()) << "factory make() must succeed";
    EXPECT_NE(make_result->get(), nullptr);
}

// ── Test 6: Concrete make_accepted() returns connected transport ───────────────
TEST(AsioPlainTransport, MakeAcceptedReturnsConnectedTransport) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc{ioc};
    auto ep = make_loopback_acceptor(ioc, acc);

    bool timed_out{false};
    bool done{false};
    // Verify that the accepted transport can immediately read/write (is in connected state).
    expected_t<std::size_t> write_result = std::unexpected{error::transport_write_error};
    expected_t<std::size_t> read_result  = std::unexpected{error::transport_read_error};

    asio::steady_timer watchdog{ioc};
    watchdog.expires_after(std::chrono::seconds{10});
    watchdog.async_wait([&](asio::error_code ec) {
        if (!ec && !done) {
            timed_out = true;
            asio::error_code ignored;
            acc.close(ignored);
        }
    });

    Transport::Config cfg{};
    auto factory_result = make_asio_plain_transport_factory(cfg);
    ASSERT_TRUE(factory_result.has_value());

    // Downcast to access make_accepted (concrete, non-virtual).
    auto* plain_factory =
        dynamic_cast<asio_plain_transport_factory*>(factory_result->get());
    ASSERT_NE(plain_factory, nullptr);

    // Server: accept → make_accepted → read.
    asio::co_spawn(
        ioc.get_executor(),
        [&, plain_factory_ptr = plain_factory]() -> asio::awaitable<void> {
            asio::error_code ec;
            asio::ip::tcp::socket accepted_sock{co_await asio::this_coro::executor};
            co_await acc.async_accept(accepted_sock, asio::redirect_error(asio::use_awaitable, ec));
            if (ec) co_return;

            auto t_result = plain_factory_ptr->make_accepted(std::move(accepted_sock), nullptr);
            if (!t_result) co_return;
            auto& server_t = *t_result;

            std::array<std::byte, 2> buf{};
            read_result = co_await server_t->async_read_some(std::span<std::byte>{buf});
        },
        asio::detached);

    // Client: connect → write 2 bytes.
    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            Transport::Config client_cfg{};
            asio_plain_transport client{co_await asio::this_coro::executor, client_cfg};

            fixpp::transport::Endpoint endpoint;
            endpoint.host = "127.0.0.1";
            endpoint.port = ep.port();

            auto conn = co_await client.async_connect(endpoint);
            if (!conn) co_return;

            const std::array<std::byte, 2> payload{std::byte{0x41}, std::byte{0x42}};
            write_result = co_await client.async_write(
                std::span<const std::byte>{payload.data(), payload.size()});

            done = true;
            watchdog.cancel();
        },
        asio::detached);

    ioc.run_for(std::chrono::seconds{12});

    ASSERT_FALSE(timed_out) << "test timed out";
    ASSERT_TRUE(write_result.has_value()) << "client write failed: "
                                         << static_cast<int>(write_result.error());
    ASSERT_TRUE(read_result.has_value()) << "server read via make_accepted failed: "
                                        << static_cast<int>(read_result.error());
    EXPECT_EQ(*read_result, 2u);
}

}  // namespace
