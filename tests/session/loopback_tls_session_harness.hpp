// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/loopback_tls_session_harness.hpp — T005 [P] Phase 2
//
// Shared live-loopback-TLS session harness for 014 US1/US2/US3/US4 tests.
//
// Provides:
//   - A TLS loopback acceptor (asio_listener + SslCtxConfig backed by
//     file_cert_source using leaf_rsa2048.pem + ca.pem).
//   - An initiator Session wired to a real asio_tls_transport_factory
//     (FR-026 SSL_CTX cached once) + ReconnectFsm via
//     SessionConfig::transport_factory_override.
//   - Inbound bytes delivered to the Session via the existing
//     Session::on_inbound_frame seam (session.cpp:862).
//   - Fixture paths resolved via FIXPP_TLS_FIXTURE_DIR env var at
//     LoopbackTlsSessionHarness construction; GTEST_SKIP() if not set.
//
// Design anchors:
//   research.md R2 ("prove resume via on_inbound_frame seam");
//   data-model.md E-5 (fixtures: leaf_rsa2048.pem + ca.pem);
//   plan.md §Testing ("Live asio_tls_transport over loopback");
//   [const §XV.9]: this header lives in tests/ only — never #included into
//     awaitable-corpus headers (session.hpp / reconnect_fsm.hpp). It includes
//     heavy concrete headers (transport_factory.hpp → tls/pinset.hpp) which
//     carry std::shared_mutex — safe here, forbidden in the corpus.
//
// Usage (in a test fixture SetUp):
//   harness_ = LoopbackTlsSessionHarness::build(ioc.get_executor(), engine);
//   if (!harness_) { GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set"; }
//
// The produced Session is an initiator; its state begins at LogonSent after
// Session::open(). The acceptor side handles TCP + TLS only (no FIX logic);
// tests drive the protocol via on_inbound_frame().
//
// Note: this is a helper header, not a test itself.

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/security_profile.hpp>  // fixpp::session::SecurityProfile kind enum
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>      // fixpp::tls::SecurityProfile + SslCtxConfig
#include <fixpp/transport/transport_factory.hpp>  // also pulls endpoint.hpp transitively

// The transport-layer loopback fixture (acceptor + factory). Pulls concrete
// asio_listener + asio_tls_transport_factory; fine here (tests/ only).
#include "transport/loopback_tls_fixture.hpp"

// Shared test support headers (minimal dictionary + security profile).
#include "support/minimal_dictionary.hpp"

using namespace std::chrono_literals;

namespace fixpp::test_support {

// ─────────────────────────────────────────────────────────────────────────────
// LoopbackTlsSessionHarness
//
// Wraps LoopbackTlsFixture (transport layer) with a Session wired to a real
// asio_tls_transport_factory. Provides the minimal plumbing T006/T011/T016/T021
// depend on; user-story tests add their own FIX message exchange on top.
//
// Ownership:
//   - transport_fixture_ — owns the asio_listener + client_factory (SSL_CTX
//     cached once per FR-026).
//   - session_factory_  — shared_ptr<TransportFactory> wired into
//     SessionConfig::transport_factory_override (required by 013 T011).
//     Distinct from the fixture's internal client_factory_ so the Session
//     holds its own owning shared_ptr (use_count()==1 hygiene check at open).
//   - session_  — the initiator Session.
//
// Threading: single io_context; all operations run on ioc.run() / ioc.run_for().
// ─────────────────────────────────────────────────────────────────────────────
class LoopbackTlsSessionHarness {
public:
    // Factory method. Returns nullptr (and calls GTEST_SKIP) when fixture dir
    // is absent. Throws std::runtime_error on factory/session construction
    // failures (test should fail, not skip — the dir was set but certs are bad).
    [[nodiscard]] static std::unique_ptr<LoopbackTlsSessionHarness>
    build(asio::any_io_executor exec, fixpp::core::EngineConfig const& engine)
    {
        const char* dir_env = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
        static const char* kCompileTimeDir = FIXPP_TLS_FIXTURE_DIR;
#else
        static const char* kCompileTimeDir = nullptr;
#endif
        const char* fixture_dir = dir_env ? dir_env : kCompileTimeDir;
        if (!fixture_dir || fixture_dir[0] == '\0') {
            return nullptr;  // caller should GTEST_SKIP()
        }

        // Build the transport-layer loopback (acceptor + factory).
        // Use raw new (not std::make_unique) because the default ctor is private;
        // std::make_unique is not a member and cannot access private ctors.
        auto h = std::unique_ptr<LoopbackTlsSessionHarness>(new LoopbackTlsSessionHarness{});
        h->fixture_dir_ = fixture_dir;

        h->transport_fixture_ = std::make_unique<fixpp::transport::test::LoopbackTlsFixture>(
            h->fixture_dir_, exec);

        // Build a SEPARATE TransportFactory for the Session (FR-026: SSL_CTX
        // cached ONCE; use_count()==1 at open per SessionConfig contract).
        fixpp::tls::file_cert_source::Config cs_cfg;
        cs_cfg.leaf_path        = h->fixture_dir_ + "/leaf_rsa2048.pem";
        cs_cfg.private_key_path = h->fixture_dir_ + "/leaf_rsa2048.key";
        cs_cfg.ca_bundle_path   = h->fixture_dir_ + "/ca.pem";

        auto cs_result = fixpp::tls::file_cert_source::make_file_cert_source(
            cs_cfg, std::pmr::new_delete_resource());
        if (!cs_result.has_value()) {
            throw std::runtime_error(
                "LoopbackTlsSessionHarness: failed to build file_cert_source");
        }

        fixpp::tls::SslCtxConfig ssl_cfg;
        ssl_cfg.profile = fixpp::tls::SecurityProfile::mtls_ca;
        ssl_cfg.cs      = std::move(*cs_result);
        ssl_cfg.clock   = nullptr;  // skip expiry — fixture certs may be stale
        ssl_cfg.caps    = fixpp::tls::CertSourceCaps{};

        auto factory_result = fixpp::transport::make_asio_tls_transport_factory(
            fixpp::transport::Transport::Config{}, ssl_cfg);
        if (!factory_result.has_value()) {
            throw std::runtime_error(
                "LoopbackTlsSessionHarness: make_asio_tls_transport_factory failed");
        }
        h->session_factory_ = std::move(*factory_result);

        // Build the initiator Session config.
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id  = "INITIATOR";
        cfg.target_comp_id  = "ACCEPTOR";
        cfg.begin_string    = "FIX.4.2";
        cfg.heartbeat_interval     = std::chrono::seconds{30};
        cfg.logout_disconnect_timeout_ms = 2000;
        cfg.role            = fixpp::session::session_role::initiator;
        cfg.executor_override       = exec;
        cfg.security_profile = fixpp::session::SecurityProfile{
            fixpp::session::SecurityProfile::kind::mtls_ca};  // session-layer kind discriminant
        cfg.dictionary      = fixpp::test_support::make_minimal_dictionary();
        cfg.reset_seqnum_policy_field =
            fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.transport_factory_override = h->session_factory_;
        // Server endpoint resolved from bound_port at construction.
        h->server_endpoint_ = h->transport_fixture_->server_endpoint();

        h->session_ = std::make_unique<fixpp::session::Session>(engine, cfg);
        return h;
    }

    // The underlying transport-layer fixture (acceptor + client factory).
    fixpp::transport::test::LoopbackTlsFixture& transport_fixture() noexcept {
        return *transport_fixture_;
    }

    // The initiator Session.
    fixpp::session::Session& session() noexcept { return *session_; }

    // The server endpoint (127.0.0.1:bound_port).
    fixpp::transport::Endpoint server_endpoint() const noexcept {
        return server_endpoint_;
    }

    // The shared TransportFactory (asio_tls_transport_factory, FR-026 cached).
    std::shared_ptr<fixpp::transport::TransportFactory> factory() const noexcept {
        return session_factory_;
    }

    // Convenience: open the Session (drives the initiator to LogonSent).
    // Returns the expected_t from open(); caller drives io_context.
    std::future<fixpp::core::expected_t<void>>
    spawn_open(asio::io_context& ioc)
    {
        return asio::co_spawn(ioc, session_->open(), asio::use_future);
    }

    // Convenience: feed inbound bytes through on_inbound_frame.
    std::future<fixpp::core::expected_t<void>>
    spawn_feed(asio::io_context& ioc, std::span<const std::byte> frame)
    {
        return asio::co_spawn(ioc, session_->on_inbound_frame(frame), asio::use_future);
    }

private:
    LoopbackTlsSessionHarness() = default;

    std::string                                                  fixture_dir_;
    std::unique_ptr<fixpp::transport::test::LoopbackTlsFixture>  transport_fixture_;
    std::shared_ptr<fixpp::transport::TransportFactory>          session_factory_;
    std::unique_ptr<fixpp::session::Session>                     session_;
    fixpp::transport::Endpoint                                   server_endpoint_{};
};

}  // namespace fixpp::test_support
