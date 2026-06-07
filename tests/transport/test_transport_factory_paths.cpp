// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "transport/asio_tls_transport.hpp"

#ifndef FIXPP_TLS_FIXTURE_DIR
#define FIXPP_TLS_FIXTURE_DIR ""
#endif

namespace {

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::tls::Certificate;
using fixpp::tls::SecurityProfile;
using fixpp::tls::SslCtxConfig;
using fixpp::tls::async_signer_ref;
using fixpp::tls::cert_source;
using fixpp::tls::file_cert_source;
using fixpp::tls::local_credentials;
using fixpp::transport::Endpoint;
using fixpp::transport::TlsTransport;
using fixpp::transport::Transport;
using fixpp::transport::TransportFactory;
using fixpp::transport::asio_tls_transport_factory;
using fixpp::transport::make_asio_tls_transport_factory;

std::string fixture_path(const char* leaf) {
    return std::string(FIXPP_TLS_FIXTURE_DIR) + "/" + leaf;
}

std::shared_ptr<cert_source> make_fixture_cert_source() {
    file_cert_source::Config cfg;
    cfg.leaf_path = fixture_path("leaf_rsa2048.pem");
    cfg.private_key_path = fixture_path("leaf_rsa2048.key");
    cfg.ca_bundle_path = fixture_path("ca.pem");

    auto result = file_cert_source::make_file_cert_source(cfg, std::pmr::new_delete_resource());
    if (!result.has_value()) {
        throw std::runtime_error("make_fixture_cert_source failed");
    }
    return std::move(*result);
}

SslCtxConfig make_fixture_ssl_cfg(std::shared_ptr<cert_source> cs) {
    SslCtxConfig cfg;
    cfg.profile = SecurityProfile::mtls_ca;
    cfg.cs = std::move(cs);
    cfg.clock = nullptr;
    return cfg;
}

class failing_load_cert_source final : public cert_source {
public:
    [[nodiscard]] asio::awaitable<expected_t<local_credentials>> load_credentials() override {
        co_return expected_t<local_credentials>{std::unexpect, error::tls_load_cancelled};
    }

    [[nodiscard]] expected_t<std::span<const Certificate>> load_trust_anchors()
        [[clang::lifetimebound]] override {
        return std::span<const Certificate>{};
    }
};

class mutating_cert_source final : public cert_source {
public:
    enum class mode { empty_leaf_der, async_signer };

    mutating_cert_source(std::shared_ptr<cert_source> inner, mode mutation)
        : inner_{std::move(inner)}, mutation_{mutation} {}

    [[nodiscard]] asio::awaitable<expected_t<local_credentials>> load_credentials() override {
        auto loaded = co_await inner_->load_credentials();
        if (!loaded.has_value()) {
            co_return expected_t<local_credentials>{std::unexpect, loaded.error()};
        }

        auto creds = *loaded;
        switch (mutation_) {
            case mode::empty_leaf_der:
                creds.leaf = Certificate{};
                break;
            case mode::async_signer:
                creds.signer = async_signer_ref{
                    [](fixpp::tls::sign_request const&)
                        -> asio::awaitable<expected_t<fixpp::tls::sign_response>> {
                        co_return expected_t<fixpp::tls::sign_response>{
                            std::unexpect, error::tls_sign_callback_unavailable};
                    }};
                break;
        }

        co_return expected_t<local_credentials>{std::move(creds)};
    }

    [[nodiscard]] expected_t<std::span<const Certificate>> load_trust_anchors()
        [[clang::lifetimebound]] override {
        return inner_->load_trust_anchors();
    }

private:
    std::shared_ptr<cert_source> inner_;
    mode mutation_;
};

asio_tls_transport_factory* expect_concrete_factory(TransportFactory* factory) {
    auto* concrete = dynamic_cast<asio_tls_transport_factory*>(factory);
    EXPECT_NE(concrete, nullptr) << "expected the default concrete transport factory";
    return concrete;
}

}  // namespace

TEST(TransportFactoryPaths, SnapshotInitiallyMatchesConfiguredSourceAndReloads) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto source_a = make_fixture_cert_source();
    auto source_b = make_fixture_cert_source();
    auto factory_result = make_asio_tls_transport_factory({}, make_fixture_ssl_cfg(source_a));

    ASSERT_TRUE(factory_result.has_value()) << static_cast<int>(factory_result.error());
    auto* concrete = expect_concrete_factory(factory_result->get());
    ASSERT_NE(concrete, nullptr);

    EXPECT_EQ(concrete->cert_source_snapshot(), source_a);

    auto null_reload = concrete->reload_credentials(nullptr);
    ASSERT_FALSE(null_reload.has_value());
    EXPECT_EQ(null_reload.error(), error::session_invalid_argument);
    EXPECT_EQ(concrete->cert_source_snapshot(), source_a);

    auto valid_reload = concrete->reload_credentials(source_b);
    ASSERT_TRUE(valid_reload.has_value()) << static_cast<int>(valid_reload.error());
    EXPECT_EQ(concrete->cert_source_snapshot(), source_b);
}

TEST(TransportFactoryPaths, FactoryConstructionRejectsNullCertSource) {
    SslCtxConfig cfg;
    cfg.profile = SecurityProfile::mtls_ca;

    auto factory_result = make_asio_tls_transport_factory({}, cfg);

    ASSERT_FALSE(factory_result.has_value());
    EXPECT_EQ(factory_result.error(), error::transport_factory_failed);
}

TEST(TransportFactoryPaths, FactoryConstructionMapsCredentialLoadFailure) {
    auto factory_result =
        make_asio_tls_transport_factory({}, make_fixture_ssl_cfg(std::make_shared<failing_load_cert_source>()));

    ASSERT_FALSE(factory_result.has_value());
    EXPECT_EQ(factory_result.error(), error::transport_factory_failed);
}

TEST(TransportFactoryPaths, FactoryConstructionRejectsMalformedCredentials) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto empty_leaf_result = make_asio_tls_transport_factory(
        {}, make_fixture_ssl_cfg(std::make_shared<mutating_cert_source>(
                make_fixture_cert_source(), mutating_cert_source::mode::empty_leaf_der)));
    ASSERT_FALSE(empty_leaf_result.has_value());
    EXPECT_EQ(empty_leaf_result.error(), error::transport_factory_failed);

    auto async_signer_result = make_asio_tls_transport_factory(
        {}, make_fixture_ssl_cfg(std::make_shared<mutating_cert_source>(
                make_fixture_cert_source(), mutating_cert_source::mode::async_signer)));
    ASSERT_FALSE(async_signer_result.has_value());
    EXPECT_EQ(async_signer_result.error(), error::transport_factory_failed);
}

TEST(TransportFactoryPaths, MakeAcceptedAdoptsRealAcceptedSocketAndHandshakes) {
    if (std::string(FIXPP_TLS_FIXTURE_DIR).empty()) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    auto server_cfg = make_fixture_ssl_cfg(make_fixture_cert_source());
    auto client_cfg = make_fixture_ssl_cfg(make_fixture_cert_source());
    auto server_factory_result = make_asio_tls_transport_factory({}, server_cfg);
    auto client_factory_result = make_asio_tls_transport_factory({}, client_cfg);

    ASSERT_TRUE(server_factory_result.has_value()) << static_cast<int>(server_factory_result.error());
    ASSERT_TRUE(client_factory_result.has_value()) << static_cast<int>(client_factory_result.error());

    auto* server_factory = expect_concrete_factory(server_factory_result->get());
    ASSERT_NE(server_factory, nullptr);

    asio::io_context ioc;
    asio::ip::tcp::acceptor acceptor{
        ioc, asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), 0}};
    const auto port = acceptor.local_endpoint().port();

    auto accepted_future = acceptor.async_accept(asio::use_future);
    auto client_transport_result = (*client_factory_result)->make(ioc.get_executor(), client_cfg, nullptr);
    ASSERT_TRUE(client_transport_result.has_value()) << static_cast<int>(client_transport_result.error());

    auto client_transport = std::move(*client_transport_result);
    auto* client_tls = dynamic_cast<TlsTransport*>(client_transport.get());
    ASSERT_NE(client_tls, nullptr);

    expected_t<fixpp::transport::handshake_result> client_handshake =
        std::unexpected{error::transport_handshake_cancelled};
    expected_t<fixpp::transport::handshake_result> server_handshake =
        std::unexpected{error::transport_handshake_cancelled};

    asio::co_spawn(
        ioc.get_executor(),
        [&, client = std::move(client_transport)]() mutable -> asio::awaitable<void> {
            auto connected = co_await client->async_connect(Endpoint{"127.0.0.1", port, 0});
            if (!connected.has_value()) {
                co_return;
            }
            client_handshake = co_await client_tls->async_handshake(client_cfg);
        },
        asio::use_future);

    ioc.run_for(2s);
    ASSERT_EQ(accepted_future.wait_for(0s), std::future_status::ready)
        << "raw accept did not complete within the deadline";
    auto accepted_socket = accepted_future.get();
    ioc.restart();

    auto server_transport_result = server_factory->make_accepted(std::move(accepted_socket), nullptr);
    ASSERT_TRUE(server_transport_result.has_value()) << static_cast<int>(server_transport_result.error());
    ASSERT_NE(server_transport_result->get(), nullptr);

    auto* server_tls = dynamic_cast<TlsTransport*>(server_transport_result->get());
    ASSERT_NE(server_tls, nullptr);

    asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            server_handshake = co_await server_tls->async_handshake(server_cfg);
        },
        asio::use_future);

    ioc.run_for(10s);

    ASSERT_TRUE(server_handshake.has_value()) << static_cast<int>(server_handshake.error());
    EXPECT_TRUE(client_handshake.has_value()) << static_cast<int>(client_handshake.error());
}
