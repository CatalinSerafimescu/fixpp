// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::TlsTransport — TLS-aware sub-interface (1 additional
// pure-virtual extending Transport's 5) + handshake_result owning-by-value POD.
// Re-emitted verbatim from [2h §4.2]; design doc is binding upstream.
//
// Sub-interface ≤5 cap budget per [const §XIV.2]: uses 1 of 5
// (async_handshake). Remaining 4 slots are headroom for post-v1 PSK session
// callback (T-012 P2 deferred per [const §XII.6]), explicit renegotiation
// control, or an early-data hook (banned in v1.0 per [const §XII.3]).

#pragma once

#include <asio/awaitable.hpp>
#include <fixpp/core/error.hpp>            // defines core::expected_t<T>
#include <fixpp/tls/peer_identity.hpp>     // [2g §4.5] peer_identity (LOCKED)
#include <fixpp/tls/pinset.hpp>            // [2g §4.3] pin_snapshot (LOCKED)
#include <fixpp/tls/security_profile.hpp>  // [2g §4.5] SslCtxConfig (LOCKED)
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <memory_resource>
#include <string>

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// handshake_result — value-typed POD returned by async_handshake.
//
// All three members are OWNING:
//   - peer_id: OWNING per [2g §4.5]; PMR-allocated SAN strings against
//              SslCtxConfig::mr.
//   - captured_pinset: shared_ptr that outlives any concurrent Pinset rotation
//              per [2g §4.3] / [2g §6.5.1]. NULL IFF SecurityProfile is
//              mtls_ca or one_way_ca (those profiles don't consume a pinset
//              per [2g §4.5.1] table). Non-null under mtls_pinned.
//   - negotiated_cipher: PMR-allocated pmr::string against SslCtxConfig::mr.
//
// The FSM holds handshake_result BY VALUE across the session lifetime per the
// [2c §4.8] owning_message_t<> precedent. Post-handshake reads (T-041 binding
// at session open per the post-this-feature session-module spec; 2k OTel
// cert-event spans per [2g §7.8]; 2j ReloadCertSource handler-side identity
// readout per [2g §7.7]) are DIRECT member reads on the FSM-held value — no
// virtual dispatch, no dynamic_cast.
//
// View accessors on peer_id (subject_dn_view(), san_dns_names(), san_uris())
// carry [[clang::lifetimebound]] at their abstract-base declaration site per
// [2g §4.5]; consumer aliasing is bounded by peer_id's lifetime = handshake_
// result's lifetime by composition.
// ─────────────────────────────────────────────────────────────────────────────
struct handshake_result {
    fixpp::tls::peer_identity peer_id;
    std::shared_ptr<const fixpp::tls::pin_snapshot>
        captured_pinset;  // null IFF non-pinned profile.
    std::pmr::string negotiated_cipher;
};

// ─────────────────────────────────────────────────────────────────────────────
// TlsTransport — TLS-aware sub-interface.
//
// Inherits VIRTUALLY from Transport so TlsTransport* IS-A Transport*. The
// session FSM holds Transport* uniformly; the TLS specialisation is reached
// via EXACTLY ONE dynamic_cast<TlsTransport*>(transport_.get()) at session
// open (async_handshake issue site). The cast result is stored once on the
// Session object as a typed pointer; subsequent code paths use the typed
// pointer with no further casts. Post-handshake artefact reads consume the
// FSM-held handshake_result value (no cast, no virtual dispatch).
//
// v1.0 partition: every Transport returned by the v1.0 factory is TLS-capable
// per [FIX-SL §4.3.1] + [FIXS §1.1] mandatory-TLS reality and [2h §4.5]'s drop
// of the v0.1 plain-TCP-via-empty-SslCtxConfig narrative.
// ─────────────────────────────────────────────────────────────────────────────
class TlsTransport : public virtual Transport {
public:
    ~TlsTransport() override = default;

    // (1) Run the TLS handshake. Issued by the session FSM AFTER async_connect
    //     completes successfully (FSM owns the order: connect → handshake →
    //     Logon). Returns a value-typed handshake_result carrying the
    //     negotiated peer_identity (OWNING per [2g §4.5]), the captured
    //     Pinset::snapshot() shared_ptr (per [2g §6.5.1] capture-once contract),
    //     and the negotiated cipher-suite name.
    //
    //     The SslCtxConfig comes from [2g §4.5] make_ssl_ctx_config(profile,
    //     cert_source, clock, pinset, mr) — the FSM (post-this-feature) builds
    //     it at session open and passes it in. 2h does NOT construct it; 2h
    //     consumes it.
    //
    //     [2g §6.5.1] HANDSHAKE-TIME PINSET CAPTURE BINDING (BINDING):
    //     async_handshake captures cfg.pinset->snapshot() ONCE at handshake
    //     start (BEFORE the OpenSSL handshake protocol exchange begins) and
    //     stores the captured snapshot in transport-owned state for the
    //     duration of the handshake. The SSL_VERIFY_PEER callback reads the
    //     captured snapshot from that state — NEVER calls cfg.pinset->find /
    //     contains / snapshot mid-verification. Mid-handshake rotation per
    //     [2d §7.5] does NOT affect an in-flight handshake by construction.
    //
    //     Cancellation: cancellation_type::total → transport_handshake_cancelled
    //     (the OpenSSL handshake aborts mid-flight; SSL* state is broken; caller
    //     MUST close() to clean up — see [2h §6.4]).
    //
    //     Timeout: Transport::Config::tls_handshake_timeout (default 30 s)
    //     bounds the handshake's wall-clock duration; on timeout → transport_
    //     handshake_timeout. Timeout via Clock::sleep_until composed under the
    //     awaiter's cancellation slot per [2d §6.5].
    //
    //     In-flight exclusivity: handshake is one-shot per Transport lifetime
    //     (like async_connect — see Transport in-flight exclusivity contract);
    //     second async_handshake → transport_already_connected. Reconnect path
    //     destroys the dead Transport, mints a fresh one via TransportFactory
    //     ::make(...), then handshakes on the new instance (Clarifications
    //     2026-05-27 Q1=B).
    //
    //     [[clang::lifetimebound]] on cfg — caller MUST keep SslCtxConfig
    //     alive past awaitable completion.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<handshake_result>> async_handshake(
        fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) = 0;
};

}  // namespace fixpp::transport
