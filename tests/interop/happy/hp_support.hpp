// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_support.hpp — 016 US1 happy-path driver support.
//
// Shared wiring for the US1 happy-path interop cells so each per-cell driver is
// thin: build the baseline TLS transport factory, build a fixpp SessionConfig for
// a (counterparty, role) cell, resolve the parent-harness-leased endpoint, and
// drive a registered session to Active.
//
// All-TLS baseline (FR-025, reconciled 2026-06-01): fixpp ships TLS-only, so every
// live cell runs over TLS. The baseline uses the server-auth `one_way_ca` profile
// (the legacy-interop path: the counterparty presents a server cert fixpp's CA
// trusts; fixpp-as-acceptor presents its leaf and requires no client cert).
// App-layer client-cert IDENTITY binding (013/014 fail-closed CompID↔cert,
// session profile mtls_ca) is the v1.1 mTLS reach — NOT exercised here; the
// session authz profile is kept `one_way_ca` (permissive Logon-ack gate, the
// branch test_reconnect_live_happy_path.cpp uses).
//
// SUT-declared env contract (the parent harness must satisfy):
//   fixpp-initiator cell: INTEROP_<TOKEN>_PORT / _HOST = the counterparty's
//                         SSL acceptor (fixpp connects out).
//   fixpp-acceptor  cell: INTEROP_FIXPP_PORT = the port fixpp binds; the parent
//                         points its counterparty-initiator there (rendezvous).
//                         Unset → OS-assigns ({127.0.0.1,0}); the bound port is
//                         readable via Engine::acceptor_bound_endpoint() for the
//                         parent to relay (documented in MATRIX.md).
//
// [const §XV.9]: tests/-only; concrete transport/session headers are safe here.
#pragma once

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "support/counterparty_probe.hpp"
#include "support/interop_fixture.hpp"
#include "support/minimal_dictionary.hpp"  // tests/support/ (via tests/ include dir)
#include "support/scenario_descriptor.hpp"

namespace fixpp::interop::hp {

using namespace std::chrono_literals;

// Counterparty → probe/env token (matches counterparty_probe env_token()).
inline std::string counterparty_token(Counterparty c) {
    switch (c) {
        case Counterparty::quickfix_cpp:
            return "quickfix-cpp";
        case Counterparty::quickfix_j:
            return "quickfix-j";
        case Counterparty::fix8:
            return "fix8";
    }
    return "unknown";
}

// The TLS cert fixture dir (compile-time define from CMake; env override).
inline const char* tls_fixture_dir() {
#ifdef FIXPP_TLS_FIXTURE_DIR
    return FIXPP_TLS_FIXTURE_DIR;
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded test setup.
    return std::getenv("FIXPP_TLS_FIXTURE_DIR");
#endif
}

// Build the baseline interop TLS factory (server-auth `one_way_ca`). Returns
// nullptr if the cert fixtures are absent so the caller can GTEST_SKIP.
inline std::shared_ptr<fixpp::transport::TransportFactory> make_interop_tls_factory(
    const std::string& dir) {
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = dir + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = dir + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = dir + "/ca.pem";
    auto cs = fixpp::tls::file_cert_source::make_file_cert_source(cs_cfg,
                                                                 std::pmr::new_delete_resource());
    if (!cs) {
        return nullptr;
    }

    fixpp::tls::SslCtxConfig ssl;
    // Server-auth baseline. `one_way_ca` is [[deprecated]] as a NEW-deployment
    // posture (fixpp prefers mutual TLS), but it is precisely the legacy-interop
    // path an interop gate exists to exercise (counterparty presents a server
    // cert; no client cert required). Suppress the deprecation locally — using it
    // here is intentional and spec-faithful (server-auth v1.0 baseline; mutual
    // client-cert mTLS is the v1.1 reach, FR-025).
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    ssl.profile = fixpp::tls::SecurityProfile::one_way_ca;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    ssl.cs = std::move(*cs);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

    auto fres = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    if (!fres) {
        return nullptr;
    }
    return std::shared_ptr<fixpp::transport::TransportFactory>{std::move(*fres)};
}

// Resolve the endpoint for a cell. Initiator → counterparty's leased SSL acceptor
// (INTEROP_<TOKEN>_PORT/_HOST). Acceptor → fixpp's bind endpoint (INTEROP_FIXPP_PORT
// or OS-assigned). Returns nullopt for an initiator cell whose port env is unset
// (the probe would already have skipped, but this guards direct callers).
inline std::optional<fixpp::transport::Endpoint> cell_endpoint(Counterparty cp, Role role) {
    auto getenv_s = [](const std::string& k) -> const char* {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded test setup.
        return std::getenv(k.c_str());
    };
    if (role == Role::fixpp_initiator) {
        const std::string tok = env_token(counterparty_token(cp));
        const char* port = getenv_s("INTEROP_" + tok + "_PORT");
        if (port == nullptr || *port == '\0') {
            return std::nullopt;
        }
        const char* host = getenv_s("INTEROP_" + tok + "_HOST");
        return fixpp::transport::Endpoint{
            (host != nullptr && *host != '\0') ? host : "127.0.0.1",
            static_cast<std::uint16_t>(std::atoi(port))};  // NOLINT(cert-err34-c)
    }
    // fixpp-acceptor: bind endpoint. Parent-leased fixed port if provided, else
    // OS-assigned (port 0) — readable post-start via acceptor_bound_endpoint().
    const char* bind = getenv_s("INTEROP_FIXPP_PORT");
    return fixpp::transport::Endpoint{
        "127.0.0.1",
        (bind != nullptr && *bind != '\0') ? static_cast<std::uint16_t>(std::atoi(bind))  // NOLINT
                                           : std::uint16_t{0}};
}

// Build a fixpp SessionConfig for a (counterparty, role) cell over the baseline
// TLS factory. CompIDs follow the convention fixpp=SUT, counterparty=peer.
inline fixpp::session::SessionConfig make_session_config(
    Role role, const std::string& begin_string,
    std::shared_ptr<fixpp::transport::TransportFactory> factory, asio::any_io_executor exec,
    fixpp::transport::Endpoint endpoint) {
    fixpp::session::SessionConfig c;
    const bool initiator = (role == Role::fixpp_initiator);
    c.sender_comp_id = initiator ? "FIXPP_INIT" : "FIXPP_ACC";
    c.target_comp_id = initiator ? "CPTY_ACC" : "CPTY_INIT";
    c.begin_string = begin_string;
    c.role = initiator ? fixpp::session::session_role::initiator
                       : fixpp::session::session_role::acceptor;
    c.executor_override = exec;
    // Permissive authz gate (no app-layer cert binding — v1.1 mTLS reach).
    c.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::one_way_ca};
    c.dictionary = fixpp::test_support::make_minimal_dictionary();
    c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    c.transport_factory_override = std::move(factory);
    c.heartbeat_interval = std::chrono::seconds{30};
    c.logout_disconnect_timeout_ms = 2000;
    // For an acceptor, reconnect_endpoint is repurposed as the bind endpoint
    // (Engine "Listener acquisition" design). For an initiator it is the connect
    // target. Either way it is the cell endpoint.
    c.reconnect_endpoint = endpoint;
    c.transport_send = [](std::span<const std::byte>) {};  // rebound on attach (E-1/R7(b)).
    return c;
}

// Drive a registered session to Active (logon complete) within `deadline`.
// Returns the reached fsm_state (Active on success). Pumps the fixture io_context
// with an internal wall-clock deadline (R5 — never relies on ioc.run() ending).
inline fixpp::session::fsm_state drive_to_active(InteropEngineFixture& fx,
                                                 const fixpp::session::SessionId& id,
                                                 std::chrono::milliseconds deadline) {
    fx.run_until(
        [&] {
            const fixpp::session::Session* s = fx.engine().lookup(id);
            return s != nullptr && s->state() == fixpp::session::fsm_state::Active;
        },
        deadline);
    const fixpp::session::Session* s = fx.engine().lookup(id);
    return s != nullptr ? s->state() : fixpp::session::fsm_state::NotConnected;
}

}  // namespace fixpp::interop::hp
