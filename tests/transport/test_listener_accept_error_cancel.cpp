// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <future>
#include <system_error>

#include "support/pump_until_ready.hpp"

#define private public
#include "transport/asio_listener.hpp"
#undef private

namespace {

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::transport::Endpoint;
using fixpp::transport::Listener;
using fixpp::transport::asio_listener;
using fixpp::transport::make_asio_listener;

fixpp::tls::SslCtxConfig stub_ssl_cfg() { return fixpp::tls::SslCtxConfig{}; }

asio_listener::Config make_listener_cfg(std::uint16_t port = 0, std::uint32_t backlog = 16) {
    asio_listener::Config cfg;
    cfg.bind_endpoint = Endpoint{"127.0.0.1", port, backlog};
    cfg.ssl_cfg = stub_ssl_cfg();
    return cfg;
}

TEST(ListenerAcceptErrorCancel, TotalCancellationReturnsContractError) {
    asio::io_context ioc;
    asio_listener listener{ioc.get_executor(), make_listener_cfg()};
    asio::cancellation_signal signal;
    asio::steady_timer cancel_timer{ioc};

    auto fut = asio::co_spawn(ioc.get_executor(), listener.async_accept(),
                              asio::bind_cancellation_slot(signal.slot(), asio::use_future));

    cancel_timer.expires_after(10ms);
    cancel_timer.async_wait([&](asio::error_code ec) {
        if (!ec) {
            signal.emit(asio::cancellation_type::total);
        }
    });

    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, fut, 500ms, 10ms))
        << "async_accept did not complete after total cancel";

    try {
        auto result = fut.get();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), error::transport_accept_cancelled);
    } catch (std::system_error const& ex) {
        FAIL() << "async_accept cancellation must map to expected_t, not throw: " << ex.what();
    }
}

TEST(ListenerAcceptErrorCancel, NonListeningAcceptorMapsToTransportFactoryFailed) {
    asio::io_context ioc;
    asio_listener listener{ioc.get_executor(), make_listener_cfg()};

    asio::error_code ec;
    listener.acceptor_.close(ec);
    ASSERT_FALSE(ec) << ec.message();

    // Open an acceptor but never bind/listen → async_accept must fail. Use asio's
    // own open() (portable across POSIX sockets / winsock) rather than adopting a
    // raw native fd.
    asio::ip::tcp::acceptor broken_acceptor{ioc.get_executor()};
    broken_acceptor.open(asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec) << "open(v4) failed: " << ec.message();
    listener.acceptor_ = std::move(broken_acceptor);
    ASSERT_TRUE(listener.acceptor_.is_open());

    auto fut = asio::co_spawn(ioc.get_executor(), listener.async_accept(), asio::use_future);

    ASSERT_TRUE(fixpp::test_support::pump_until_ready(ioc, fut, 500ms, 10ms))
        << "async_accept on a non-listening acceptor did not complete with an error";

    auto result = fut.get();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::transport_factory_failed);
}

TEST(ListenerAcceptErrorCancel, FactoryMapsInvalidBindHostToTransportFactoryFailed) {
    asio::io_context ioc;
    auto cfg = make_listener_cfg();
    cfg.bind_endpoint.host = "not-an-ip-literal";

    auto made = make_asio_listener(ioc.get_executor(), std::move(cfg), nullptr);

    ASSERT_FALSE(made.has_value());
    EXPECT_EQ(made.error(), error::transport_factory_failed);
}

TEST(ListenerAcceptErrorCancel, FactoryMapsDuplicateBindToTransportFactoryFailed) {
    asio::io_context ioc;
    asio_listener first{ioc.get_executor(), make_listener_cfg()};
    const auto port = first.bound_endpoint().port;

    auto made = make_asio_listener(ioc.get_executor(), make_listener_cfg(port), nullptr);

    ASSERT_FALSE(made.has_value());
    EXPECT_EQ(made.error(), error::transport_factory_failed);
}

}  // namespace
