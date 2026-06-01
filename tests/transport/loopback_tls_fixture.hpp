// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/loopback_tls_fixture.hpp
//
// Shared loopback TLS pair fixture for gate-b/r2 transport tests:
//   RC#A Cells 7-8  (listener_acceptor test — server-role handshake)
//   RC#D live witness (truncated-close → transport_read_truncated)
//   RC#E (in-flight exclusivity runtime cells)
//
// Design:
//   - Server side: asio_listener + SslCtxConfig backed by file_cert_source.
//   - Client side: asio_tls_transport_factory (FR-026: SSL_CTX built ONCE at
//     fixture construction; each make_client() is a fresh Transport referencing
//     the cached context per FR-028 fresh-mint-per-attempt).
//   - Both sides use leaf_rsa2048 cert + ca.pem trust (mtls_ca profile).
//   - Loopback port is OS-assigned (port = 0 bind).
//
// Anchor: .specify/2h-transport.md §4.7 FR-026 + FR-024.

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <fixpp/core/error.hpp>
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

#include "transport/asio_listener.hpp"

namespace fixpp::transport::test {

// ─────────────────────────────────────────────────────────────────────────────
// LoopbackTlsFixture
//
// Minimal loopback TLS pair for gate-b/r2 transport integration tests.
// Both sides use the same leaf_rsa2048 cert signed by ca (self-trust mTLS).
// ─────────────────────────────────────────────────────────────────────────────
class LoopbackTlsFixture {
public:
    explicit LoopbackTlsFixture(std::string fixture_dir, asio::any_io_executor exec)
        : fixture_dir_{std::move(fixture_dir)} {
        tls::file_cert_source::Config cs_cfg;
        cs_cfg.leaf_path = fixture_dir_ + "/leaf_rsa2048.pem";
        cs_cfg.private_key_path = fixture_dir_ + "/leaf_rsa2048.key";
        cs_cfg.ca_bundle_path = fixture_dir_ + "/ca.pem";

        auto cs_result =
            tls::file_cert_source::make_file_cert_source(cs_cfg, std::pmr::new_delete_resource());
        if (!cs_result.has_value()) {
            throw std::runtime_error("LoopbackTlsFixture: failed to build file_cert_source");
        }

        ssl_cfg_.profile = tls::SecurityProfile::mtls_ca;
        ssl_cfg_.cs = std::move(*cs_result);
        ssl_cfg_.clock = nullptr;  // skip expiry — fixture certs may be stale
        ssl_cfg_.caps = tls::CertSourceCaps{};

        // Client factory (FR-026: SSL_CTX built ONCE here; cheap per-attempt mint).
        auto factory_result = make_asio_tls_transport_factory(Transport::Config{}, ssl_cfg_);
        if (!factory_result.has_value()) {
            throw std::runtime_error("LoopbackTlsFixture: make_asio_tls_transport_factory failed");
        }
        client_factory_ = std::move(*factory_result);

        // Listener — binds to 127.0.0.1:0 (OS-assigned port).
        asio_listener::Config listener_cfg;
        listener_cfg.bind_endpoint = Endpoint{"127.0.0.1", 0, /*backlog=*/16};
        listener_cfg.ssl_cfg = ssl_cfg_;
        listener_ = std::make_unique<asio_listener>(exec, listener_cfg);
        bound_port_ = listener_->bound_endpoint().port;
    }

    // Suppress copy/move — fixture owns unique resources.
    LoopbackTlsFixture(LoopbackTlsFixture const&) = delete;
    LoopbackTlsFixture& operator=(LoopbackTlsFixture const&) = delete;
    LoopbackTlsFixture(LoopbackTlsFixture&&) = delete;
    LoopbackTlsFixture& operator=(LoopbackTlsFixture&&) = delete;

    // Port the server is listening on (OS-assigned).
    std::uint16_t bound_port() const noexcept { return bound_port_; }

    // Endpoint clients should connect to.
    Endpoint server_endpoint() const noexcept { return Endpoint{"127.0.0.1", bound_port_, 0}; }

    // Mint a fresh client Transport using the cached SSL_CTX (FR-026).
    // The returned Transport is in state fresh; caller drives async_connect.
    std::unique_ptr<Transport> make_client(asio::any_io_executor exec) {
        auto result = client_factory_->make(exec, ssl_cfg_, nullptr);
        if (!result.has_value()) {
            throw std::runtime_error("LoopbackTlsFixture::make_client failed");
        }
        return std::move(*result);
    }

    // SslCtxConfig for async_handshake calls (same config for both sides).
    tls::SslCtxConfig const& ssl_cfg() const noexcept { return ssl_cfg_; }

    // The underlying listener (for tests that need direct access).
    asio_listener& listener() noexcept { return *listener_; }

    // Cancel the listener (Option-A 3-action contract).
    void cancel_listener() noexcept {
        if (listener_) {
            (void)listener_->cancel();
        }
    }

private:
    std::string fixture_dir_;
    std::uint16_t bound_port_{0};
    tls::SslCtxConfig ssl_cfg_;
    std::unique_ptr<TransportFactory> client_factory_;
    std::unique_ptr<asio_listener> listener_;
};

}  // namespace fixpp::transport::test
