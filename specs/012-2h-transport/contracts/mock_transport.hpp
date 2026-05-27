// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::test::mock_transport — public test header.
// In-memory deterministic TlsTransport impl for FSM-under-test scenarios.
// Re-emitted verbatim from [2h §4.8].
//
// CONSUMABLE FROM tests/-ONLY TRANSLATION UNITS per [const §VII]. Lives at
// include/fixpp/transport/test/mock_transport.hpp; the build system MUST
// exclude this directory from production target sources. Guarded by a
// #error on non-test compilation contexts (the implementing header carries
// the guard; this contract artifact does not, by design).
//
// Drives the session FSM through a pre-recorded byte stream + handshake-
// success/failure scripted outcomes. Implements TlsTransport (so the FSM's
// TLS-aware paths fire) but the handshake is purely virtual — no real OpenSSL
// calls. The mock binary does NOT need to link OpenSSL or ASIO networking.
//
// CANCELLATION HONOUR (BINDING per spec FR-037): every async method honours
// the awaiter's cancellation_state per [const §XI.2] — the mock does NOT
// short-circuit cancellation tests by completing instantly. Cancellation-
// aware completions surface transport_*_cancelled EXACTLY as the production
// asio_tls_transport does. FSM tests that detect cancellation-propagation
// regressions do so under the mock alone, with no real socket and no OpenSSL
// link dependency.

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <fixpp/core/error.hpp>   // defines core::expected_t<T>
#include <fixpp/tls/peer_identity.hpp>      // [2g §4.5] peer_identity (LOCKED)
#include <fixpp/tls/pinset.hpp>             // [2g §4.3] pin_snapshot (LOCKED)
#include <fixpp/tls/security_profile.hpp>   // [2g §4.5] SslCtxConfig (LOCKED)
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>

namespace fixpp::transport::test {

// ─────────────────────────────────────────────────────────────────────────────
// Script — test-author-supplied programmatic interface enumerating expected
// reads / writes / errors / cancellations in sequence per spec FR-038. FSM
// tests are deterministic without timing dependence.
// ─────────────────────────────────────────────────────────────────────────────
struct Script {
    // Queued bytes delivered by async_read_some calls. async_read_some completes
    // with up to buf.size() bytes per call, draining from inbound_bytes via
    // read_cursor_. When the cursor reaches inbound_bytes.size(), subsequent
    // reads complete with transport_read_eof (the v1.0 default; alternative
    // modes are out of scope).
    std::vector<std::byte> inbound_bytes;

    // Optional: expected outbound writes verified against outbound_seen_ on
    // each async_write call. Empty vector = no verification (any output
    // accepted). When non-empty, mismatches throw a GoogleTest assertion via
    // the test diagnostic accessors.
    std::vector<std::vector<std::byte>> expected_outbound_writes;

    // Handshake outcome control. true → async_handshake completes with the
    // populated peer_identity / pinset_snapshot / negotiated_cipher.
    // false → async_handshake completes with transport_handshake_failed.
    bool handshake_succeeds {true};

    // Populated when handshake_succeeds — returned in handshake_result.peer_id.
    // The peer_identity carries OWNING SAN strings allocated against the
    // engine's default upstream resource (or the test fixture's PMR if the
    // test wires one).
    fixpp::tls::peer_identity peer_identity_to_return;

    // Pinset snapshot to return in handshake_result.captured_pinset. May be
    // null under mtls_ca mode (per E-4 / [2g §4.5.1]); MUST be non-null
    // under mtls_pinned-mode tests that consume the snapshot downstream.
    std::shared_ptr<const fixpp::tls::pin_snapshot> pinset_snapshot_to_return;

    // Returned in handshake_result.negotiated_cipher. Defaults to TLS 1.3
    // AES-128-GCM; tests can override to exercise cipher-suite-specific paths.
    std::string negotiated_cipher = "TLS_AES_128_GCM_SHA256";

    // Latency injection points — tests for cancellation race conditions. Each
    // async method composes asio::steady_timer waits of the corresponding
    // latency BEFORE the awaitable completes; the awaitable cancels cleanly
    // mid-wait per the cancellation-honour binding.
    std::chrono::milliseconds read_latency {0};
    std::chrono::milliseconds write_latency {0};
    std::chrono::milliseconds handshake_latency {0};
};

// ─────────────────────────────────────────────────────────────────────────────
// mock_transport — deterministic TlsTransport impl for FSM tests.
// ─────────────────────────────────────────────────────────────────────────────
class mock_transport final : public TlsTransport {
public:
    explicit mock_transport(asio::any_io_executor exec, Script script);
    ~mock_transport() override = default;

    // ── Transport overrides ─────────────────────────────────────────────────
    [[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>>
        async_connect(Endpoint const& ep) override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
        async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
        async_write(std::span<const std::byte> bytes [[clang::lifetimebound]]) override;

    [[nodiscard]] core::expected_t<void> cancel() noexcept override;
    [[nodiscard]] core::expected_t<void> close() noexcept override;

    // ── TlsTransport override ───────────────────────────────────────────────
    [[nodiscard]] asio::awaitable<core::expected_t<handshake_result>>
        async_handshake(fixpp::tls::SslCtxConfig const& cfg
                            [[clang::lifetimebound]]) override;

    // ── Test-only diagnostics (read-only accessors) ─────────────────────────
    [[nodiscard]] std::size_t bytes_read_so_far() const noexcept;
    [[nodiscard]] std::vector<std::byte> outbound_bytes_seen() const;
    [[nodiscard]] std::size_t async_writes_observed() const noexcept;

private:
    asio::any_io_executor  exec_;
    Script                 script_;
    std::size_t            read_cursor_ {0};
    std::vector<std::byte> outbound_seen_;
    bool                   closed_     {false};
    bool                   handshaken_ {false};
};

}  // namespace fixpp::transport::test
