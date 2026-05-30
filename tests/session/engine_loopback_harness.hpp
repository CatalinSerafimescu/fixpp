// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_loopback_harness.hpp — T007 [P] Phase 2
//
// Shared live-loopback-TLS Engine harness for 015 US1/US2/US3 tests.
// Mirrors loopback_tls_session_harness.hpp but wraps the public Engine API.
// Anchors: tasks.md T007; data-model.md E-1/E-6; research.md R7(b);
//          [const §XV.9]: tests/-only; concrete transport headers are safe here.
//
// Usage:
//   auto h = EngineLoopbackHarness::build(ioc.get_executor(),
//                                         std::move(engine_cfg));
//   if (!h) { GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set"; }
//   h->engine().start();
//   // … drive ioc … then:
//   asio::co_spawn(ioc, h->engine().stop(), asio::use_future).get();

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <cstdlib>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "transport/loopback_tls_fixture.hpp"
#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace fixpp::test_support {

class EngineLoopbackHarness {
public:
    // Build. Returns nullptr + GTEST_SKIP() intent when fixture dir absent.
    // Takes EngineConfig by move (it contains non-copyable unique_ptr members).
    [[nodiscard]] static std::unique_ptr<EngineLoopbackHarness>
    build(asio::any_io_executor exec,
          fixpp::core::EngineConfig engine_cfg,
          bool register_sessions = true)
    {
        const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
        static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
        static const char* kDir = nullptr;
#endif
        const char* fixture_dir = dir ? dir : kDir;
        if (!fixture_dir || fixture_dir[0] == '\0') return nullptr;

        auto h = std::unique_ptr<EngineLoopbackHarness>(new EngineLoopbackHarness{});
        h->fixture_dir_ = fixture_dir;
        h->transport_fixture_ =
            std::make_unique<fixpp::transport::test::LoopbackTlsFixture>(
                h->fixture_dir_, exec);
        h->engine_ = std::make_unique<fixpp::session::Engine>(
            exec, std::move(engine_cfg));
        if (register_sessions) h->register_default_sessions(exec);
        return h;
    }

    fixpp::session::Engine& engine() noexcept { return *engine_; }
    fixpp::transport::test::LoopbackTlsFixture& transport_fixture() noexcept {
        return *transport_fixture_;
    }
    fixpp::transport::Endpoint server_endpoint() const noexcept {
        return transport_fixture_->server_endpoint();
    }
    fixpp::session::SessionId acceptor_id()  const noexcept { return acceptor_id_; }
    fixpp::session::SessionId initiator_id() const noexcept { return initiator_id_; }

    std::future<void> spawn_stop(asio::io_context& ioc) {
        return asio::co_spawn(ioc, engine_->stop(), asio::use_future);
    }

private:
    EngineLoopbackHarness() = default;

    void register_default_sessions(asio::any_io_executor exec)
    {
        // Shared TLS factory (FR-026: SSL_CTX cached once).
        fixpp::tls::file_cert_source::Config cs_cfg;
        cs_cfg.leaf_path        = fixture_dir_ + "/leaf_rsa2048.pem";
        cs_cfg.private_key_path = fixture_dir_ + "/leaf_rsa2048.key";
        cs_cfg.ca_bundle_path   = fixture_dir_ + "/ca.pem";
        auto cs = fixpp::tls::file_cert_source::make_file_cert_source(
            cs_cfg, std::pmr::new_delete_resource());
        if (!cs) throw std::runtime_error{"EngineLoopbackHarness: cert_source failed"};

        fixpp::tls::SslCtxConfig ssl;
        ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
        ssl.cs      = std::move(*cs);
        ssl.clock   = nullptr;
        ssl.caps    = fixpp::tls::CertSourceCaps{};

        auto fres = fixpp::transport::make_asio_tls_transport_factory(
            fixpp::transport::Transport::Config{}, ssl);
        if (!fres) throw std::runtime_error{"EngineLoopbackHarness: factory failed"};
        std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fres)};

        auto make_cfg = [&](const char* sender, const char* target,
                            fixpp::session::session_role role) {
            fixpp::session::SessionConfig c;
            c.sender_comp_id  = sender;
            c.target_comp_id  = target;
            c.begin_string    = "FIX.4.2";
            c.role            = role;
            c.executor_override = exec;
            c.security_profile = fixpp::session::SecurityProfile{
                fixpp::session::SecurityProfile::kind::mtls_ca};
            c.dictionary     = fixpp::test_support::make_minimal_dictionary();
            c.reset_seqnum_policy_field =
                fixpp::session::reset_seqnum_policy::bilateral_lenient;
            c.transport_factory_override = fac;
            c.heartbeat_interval = std::chrono::seconds{30};
            c.logout_disconnect_timeout_ms = 2000;
            return c;
        };

        auto acc = make_cfg("ACCEPTOR", "INITIATOR",
                            fixpp::session::session_role::acceptor);
        // Rebindable send-slot: no-op until attach_accepted_transport (T011/E-1/R7(b)).
        acc.transport_send = [](std::span<const std::byte>) {};
        acceptor_id_ = fixpp::session::SessionId::from_config(acc);
        if (!engine_->register_session(std::move(acc)))
            throw std::runtime_error{"EngineLoopbackHarness: register acceptor failed"};

        auto ini = make_cfg("INITIATOR", "ACCEPTOR",
                            fixpp::session::session_role::initiator);
        ini.reconnect_endpoint = transport_fixture_->server_endpoint();
        initiator_id_ = fixpp::session::SessionId::from_config(ini);
        if (!engine_->register_session(std::move(ini)))
            throw std::runtime_error{"EngineLoopbackHarness: register initiator failed"};
    }

    std::string fixture_dir_;
    std::unique_ptr<fixpp::transport::test::LoopbackTlsFixture> transport_fixture_;
    std::unique_ptr<fixpp::session::Engine>                     engine_;
    fixpp::session::SessionId                                   acceptor_id_;
    fixpp::session::SessionId                                   initiator_id_;
};

}  // namespace fixpp::test_support
