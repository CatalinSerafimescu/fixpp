// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::TransportFactory — frozen-at-open factory consumed by
// EngineConfig::default_transport_factory + SessionConfig::
// transport_factory_override. Re-emitted verbatim from [2h §4.7].
//
// Ownership shape (mirrors [2e §4.4] MessageStoreFactory precedent):
//   - Factory held by EngineConfig as std::unique_ptr<TransportFactory>
//     (Appendix D §D.1 flips [2d §4.4] default_transport_factory from
//     shared_ptr → unique_ptr at sign-off; mirrors [2e §4.4] precedent).
//   - Factory held by SessionConfig as std::unique_ptr<TransportFactory>
//     override (Appendix D §D.2 adds SessionConfig::transport_factory_override
//     per the engine-anchor + session-*_override pattern).
//   - make() returns std::unique_ptr<Transport> (ownership transferred to
//     Session per [arch §5.6] frozen-at-open).
//
// RECONNECT MINTS A FRESH TRANSPORT per Clarifications 2026-05-27 Q1=B —
// matching QuickFIX-cpp / QuickFIX/J / Fix8 industry pattern. The factory is
// invoked at session open AND at every reconnect attempt. Long-lived state
// shared across attempts (SslCtxConfig carrying OpenSSL SSL_CTX*, engine PMR
// root, engine clock) MUST be cached at the factory level per spec FR-026
// — NEVER re-built per attempt. The per-attempt mint cost is bounded by the
// back-off envelope, NOT by make(...) runtime.

#pragma once

#include <asio/any_io_executor.hpp>
#include <memory>
#include <memory_resource>

#include <fixpp/core/expected.hpp>
#include <fixpp/tls/security_profile.hpp>   // [2g §4.5] SslCtxConfig (LOCKED)
#include <fixpp/transport/transport.hpp>

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// TransportFactory — abstract pluggable factory.
//
// noexcept per [2h §4.7] Codex P2 #6 close: the pure-virtual carries `noexcept`
// to prevent third-party impls from throwing across the virtual boundary;
// implementations MUST trap any internal PMR or system throws via the
// [2a §4.2] trap_throw pattern and surface failure as
// expected_t::unexpected{transport_factory_failed}. Matches the [2e §4.4]
// MessageStoreFactory and [2g §4.2] make_file_cert_source factory `noexcept`
// precedents.
// ─────────────────────────────────────────────────────────────────────────────
class TransportFactory {
public:
    virtual ~TransportFactory() = default;

    // Mint a fresh Transport for a session. The factory captures any per-
    // engine configuration at construction (e.g., the SslCtxConfig built from
    // EngineConfig::default_cert_source); per-session overrides (SessionConfig
    // ::cert_source override, SessionConfig::pinset for mid-session-mutable
    // Pinset access) are passed through the SslCtxConfig that the SESSION-OPEN
    // sequencer builds via [2g §4.5] make_ssl_ctx_config.
    //
    // Errors:
    //   - transport_factory_failed: impl reports inability to construct (OS
    //     resource exhaustion at socket creation; OpenSSL SSL_CTX_new failure;
    //     PMR throw routed through trap_throw).
    //
    // Lifetime: returned unique_ptr is consumed by the Session; the Transport
    // instance is engine-anchored at Session::open and destructed at
    // Session::close (after async_close completes or close timeout fires per
    // [2h §6.4]).
    [[nodiscard]] virtual core::expected_t<std::unique_ptr<Transport>>
        make(asio::any_io_executor             exec,
             fixpp::tls::SslCtxConfig          ssl_cfg,
             std::pmr::memory_resource*        mr) noexcept = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// asio_tls_transport_factory — default factory wrapping the asio_tls_transport
// reference impl. Caches the SslCtxConfig (and OpenSSL SSL_CTX* inside it) at
// factory level per FR-026 — long-lived state shared across reconnect attempts
// is cached HERE, NOT re-built per attempt.
//
// Signature only; body lives in src/transport/transport_factory.cpp.
// ─────────────────────────────────────────────────────────────────────────────
class asio_tls_transport_factory final : public TransportFactory {
public:
    explicit asio_tls_transport_factory(Transport::Config c = {}) noexcept;

    [[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
        make(asio::any_io_executor             exec,
             fixpp::tls::SslCtxConfig          ssl_cfg,
             std::pmr::memory_resource*        mr) noexcept override;

private:
    Transport::Config cfg_;
};

// ─────────────────────────────────────────────────────────────────────────────
// SessionConfig::transport_factory_override field shape (Appendix D §D.2 —
// orchestrator-applied at 012 sign-off):
//
//   // ── Transport (locked by 2h) ─────────────────────────────────────────
//   // Resolved factory = transport_factory_override.value_or(
//   //                       EngineConfig::default_transport_factory).
//   // Factory is unique_ptr per [arch §5.6] (no mid-session swap, no shared
//   // factory across sessions) — mirrors [2e §4.4] MessageStoreFactory shape.
//   std::unique_ptr<fixpp::transport::TransportFactory> transport_factory_override;
//
// The [2d §4.4] default_transport_factory field type also flips from
// shared_ptr → unique_ptr at sign-off (Appendix D §D.1).
// ─────────────────────────────────────────────────────────────────────────────

}  // namespace fixpp::transport
