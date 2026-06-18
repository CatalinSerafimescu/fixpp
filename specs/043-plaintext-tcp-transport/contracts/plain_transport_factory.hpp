// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 fixpp contributors
//
// CONTRACT (Phase 1 / 043) — plaintext transport factory + the shared
// TransportFactory::kind() query. Additions to
// include/fixpp/transport/transport_factory.hpp. See research.md D-3/D-5,
// data-model.md E-3/E-4. No behavioural change vs impl without re-running Gate A.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/ip/tcp.hpp>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>  // SslCtxConfig (ignored by the plain factory)
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>  // TransportFactory base
#include <memory>
#include <memory_resource>

namespace fixpp::transport {

class asio_plain_transport;

// ─────────────────────────────────────────────────────────────────────────────
// transport_security_kind — discriminant for the FR-008 profile↔factory
// consistency check (D-5). Returned by the new TransportFactory::kind().
// ─────────────────────────────────────────────────────────────────────────────
enum class transport_security_kind : std::uint8_t { tls, plaintext };

// ── Added to the abstract TransportFactory (DEFAULTED virtual; pure count stays 3, ≤5 cap) ──
//
//   // Reports whether this factory mints TLS or plaintext transports. Consumed
//   // by Session::open()'s FR-008 consistency check. noexcept; no state.
//   // DEFAULTED to `tls` (safe default): a factory that forgets to override
//   // mismatches a plaintext profile → fail-closed reject, never a silent
//   // downgrade. Defaulting (not pure) avoids breaking the ~11 tests/session/
//   // TransportFactory test doubles (D-5).
//   [[nodiscard]] virtual transport_security_kind kind() const noexcept {
//       return transport_security_kind::tls;
//   }
//
// asio_tls_transport_factory overrides kind() → transport_security_kind::tls (explicit).

// ─────────────────────────────────────────────────────────────────────────────
// asio_plain_transport_factory — credential-free factory minting
// asio_plain_transport. Builds NO SSL_CTX, loads NO credentials. (D-3)
// ─────────────────────────────────────────────────────────────────────────────
class asio_plain_transport_factory final : public TransportFactory {
public:
    explicit asio_plain_transport_factory(Transport::Config cfg) noexcept;

    // ssl_cfg is IGNORED (plaintext); mints asio_plain_transport via trap_throw.
    [[nodiscard]] core::expected_t<std::unique_ptr<Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig ssl_cfg,
        std::pmr::memory_resource* mr) noexcept override;

    // Acceptor symmetry (FR-004): adopt an accepted plain socket.
    [[nodiscard]] core::expected_t<std::unique_ptr<asio_plain_transport>> make_accepted(
        asio::ip::tcp::socket accepted_socket, std::pmr::memory_resource* mr) noexcept;

    // No certs to rotate → session_invalid_argument (slot 119). (D-11)
    [[nodiscard]] core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept override;

    // No cert source → nullptr (FSM rotation-detect tolerant). (D-11)
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override;

    // D-5: this factory mints plaintext transports.
    [[nodiscard]] transport_security_kind kind() const noexcept override;

private:
    Transport::Config cfg_;
};

// noexcept factory function — no SslCtxConfig argument (credential-free).
[[nodiscard]] core::expected_t<std::unique_ptr<TransportFactory>>
make_asio_plain_transport_factory(Transport::Config cfg) noexcept;

}  // namespace fixpp::transport
