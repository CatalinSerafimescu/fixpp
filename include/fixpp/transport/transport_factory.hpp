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
#include <asio/ip/tcp.hpp>
#include <atomic>
#include <memory>
#include <memory_resource>

#include <fixpp/core/error.hpp>             // defines core::expected_t<T>
#include <fixpp/tls/cert_source.hpp>        // for reload_credentials (013 T012)
#include <fixpp/tls/security_profile.hpp>   // [2g §4.5] SslCtxConfig (LOCKED)
#include <fixpp/transport/transport.hpp>

namespace fixpp::transport {

class asio_tls_transport;

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

    // 013 T012 — FR-030 / FR-033 / D-11 — atomic credential rotation.
    // Performs an atomic STORE on the factory-internal cert_source_slot_
    // (std::atomic<std::shared_ptr<fixpp::tls::cert_source>>); O(1),
    // strand-free. Both initiator-side and acceptor-side rotation route
    // through the SAME factory call per [[feedback_half_restructure_symmetric_api]]
    // — the factory IS the symmetric authority (no separate Listener method).
    //
    // nullptr → returns error::session_invalid_argument (slot 119) immediately;
    // no store performed.
    //
    // The atomic-swap contract: the next drive_reconnect_attempt reads the
    // NEW cert_source via cert_source_snapshot(); the in-flight handshake (if
    // any) observes the OLD source because the FSM captured a
    // std::shared_ptr<cert_source> BY VALUE COPY before make(...) was called.
    //
    // Pure-virtual count moves 1→2 (under [const §XIV.2] 5/5 cap).
    [[nodiscard]] virtual core::expected_t<void>
        reload_credentials(
            std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept = 0;

    // 014 T003 — C4 promotion: cert_source_snapshot() moves from the concrete
    // asio_tls_transport_factory to a pure-virtual on the abstract base.
    // Pure-virtual count moves 2→3 (under [const §XIV.2] 5/5 cap).
    //
    // Returns the current cert_source strong-ref BY VALUE. NEVER returns raw
    // pointer or weak_ptr per [[feedback_weak_ptr_cache_needs_owning_context]].
    // Called by ReconnectFsm at drive_reconnect_attempt entry to capture the
    // snapshot for the rotation-detect check (data-model E-3). The FSM holds
    // the abstract TransportFactory* (reconnect_fsm.hpp:152); this method must
    // be on the abstract base for the call to compile through that pointer.
    // noexcept — no state change; atomic load acquire.
    [[nodiscard]] virtual std::shared_ptr<fixpp::tls::cert_source>
        cert_source_snapshot() const noexcept = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// asio_tls_transport_factory — default factory wrapping the asio_tls_transport
// reference impl.
//
// FR-026 long-lived caching contract: the factory builds the asio::ssl::context
// (SSL_CTX) ONCE at factory construction, then shares it across every
// asio_tls_transport instance minted by make(). Sharing is safe because
// asio::ssl::stream<> holds the SSL_CTX by reference (reference-counted
// internally by OpenSSL via SSL_CTX_up_ref). The per-attempt mint cost is
// therefore Transport-only construction — NOT a full SSL_CTX rebuild.
//
// cert_source::load_credentials() is also called ONCE at factory construction
// to resolve the local_credentials (leaf cert + private key). The resolved
// credentials are installed into the cached SSL_CTX immediately; subsequent
// reconnect attempts share the same SSL_CTX without reloading credentials.
//
// NOTE: mid-session cert rotation via a future 2j `ReloadCertSource` mechanism
// will require minting a NEW factory (or replacing ssl_ctx_ + credentials_).
// That is out of 012 scope per [[project_2e_recovery_v1_upgrade_obligation]]
// parallel pattern.
//
// NOTE: both the initiator ctor and the accept-adoption ctor share the single
// cached SSL_CTX from this factory. A future per-connection SslCtxConfig
// override path is not supported by 012; document this deferral here.
//
// Factory construction is NOT noexcept — it runs cert loading synchronously and
// may fail if cert_source::load_credentials() fails. Use the noexcept factory
// function make_asio_tls_transport_factory() for safe construction.
//
// Signature only; body lives in src/transport/transport_factory.cpp.
// ─────────────────────────────────────────────────────────────────────────────
class asio_tls_transport_factory final : public TransportFactory {
public:
    // Internal constructor — takes the pre-built shared context + transport config.
    // Called by make_asio_tls_transport_factory(). NOT for direct use.
    // The shared context is type-erased as shared_ptr<void> to avoid pulling
    // <asio/ssl/context.hpp> into this public header (which would require every
    // test executable to link OpenSSL even if it never constructs a factory).
    // The actual type is asio::ssl::context*; the factory's make() casts it back.
    struct shared_ctx_tag {};
    asio_tls_transport_factory(shared_ctx_tag,
                               Transport::Config cfg,
                               fixpp::tls::SslCtxConfig ssl_cfg,
                               std::shared_ptr<void> ctx) noexcept;

    [[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
        make(asio::any_io_executor             exec,
             fixpp::tls::SslCtxConfig          ssl_cfg,
             std::pmr::memory_resource*        mr) noexcept override;

    [[nodiscard]] core::expected_t<std::unique_ptr<asio_tls_transport>>
        make_accepted(asio::ip::tcp::socket accepted_socket,
                      std::pmr::memory_resource* mr) noexcept;

    // 013 T012 — FR-033 / D-11 — atomic credential rotation override.
    // Stores new_source into cert_source_slot_ via atomic store. Returns
    // error::session_invalid_argument on nullptr. Both initiator and acceptor
    // paths share this single slot per [[feedback_half_restructure_symmetric_api]].
    // Body lives in src/transport/transport_factory.cpp.
    [[nodiscard]] core::expected_t<void>
        reload_credentials(
            std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept override;

    // 014 T003 — override of the abstract pure-virtual promoted in this pass (C4).
    // Returns the current cert_source strong-ref BY VALUE.
    // NEVER returns raw pointer or weak_ptr per
    // [[feedback_weak_ptr_cache_needs_owning_context]]. Called by ReconnectFsm
    // at drive_reconnect_attempt entry to capture the snapshot. noexcept.
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source>
        cert_source_snapshot() const noexcept override;

private:
    Transport::Config        cfg_;
    fixpp::tls::SslCtxConfig ssl_cfg_;
    std::shared_ptr<void>    ssl_ctx_;  // actually asio::ssl::context*; type-erased

    // 013 T012 — FR-033 atomic cert_source slot. Initiator and acceptor both
    // read this slot via cert_source_snapshot(); operator rotates via
    // reload_credentials(). Initialized at factory construction from
    // ssl_cfg_.cert_source (or nullptr if none). [data-model §E-7]
    std::atomic<std::shared_ptr<fixpp::tls::cert_source>> cert_source_slot_;
};

// ─────────────────────────────────────────────────────────────────────────────
// make_asio_tls_transport_factory — noexcept factory function.
//
// Builds the SSL_CTX and loads credentials ONCE via ssl_cfg. Returns an
// expected_t<unique_ptr<TransportFactory>> so callers can handle cert-load
// failures without exceptions.
//
// On success: the returned factory's make() mints fresh Transport instances
// sharing the pre-built SSL_CTX (FR-026 caching contract).
// On failure: returns transport_factory_failed.
//
// Body lives in src/transport/transport_factory.cpp.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<std::unique_ptr<TransportFactory>>
make_asio_tls_transport_factory(Transport::Config         cfg,
                                 fixpp::tls::SslCtxConfig  ssl_cfg) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// SessionConfig::transport_factory_override field shape (Appendix D §D.2,
// reconciled by 013 T011 — ownership updated from unique_ptr → shared_ptr):
//
//   // ── Transport (reconciled by 013 T011) ──────────────────────────────
//   // Resolved factory = transport_factory_override.value_or(
//   //                       EngineConfig::default_transport_factory).
//   // Factory is std::shared_ptr<TransportFactory> per 010 FR-001a precedent
//   // (unique_ptr → shared_ptr for SessionConfig copy semantics; the
//   // static_assert(std::is_copy_constructible_v<SessionConfig>) at
//   // session_config.hpp:176 is the forcing constraint). The "no factory
//   // shared across Sessions" invariant is preserved via a Session::open-time
//   // hygiene assertion (use_count()==1), NOT by storage type.
//   // Cross-Session sharing is FORBIDDEN; the shared_ptr is for COPY SEMANTICS.
//   std::shared_ptr<fixpp::transport::TransportFactory> transport_factory_override;
//
// The [2d §4.4] default_transport_factory field type remains unique_ptr in
// EngineConfig (Appendix D §D.1 sign-off — different owner, no copy semantics).
// ─────────────────────────────────────────────────────────────────────────────

}  // namespace fixpp::transport
