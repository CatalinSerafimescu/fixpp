// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 fixpp contributors
//
// CONTRACT (Phase 1 / 043) — fixpp::transport::asio_plain_transport.
// Plain-TCP Transport: implements the FIVE base Transport pure-virtuals over a
// plain asio::ip::tcp::socket. NOT a TlsTransport — no async_handshake, no
// OpenSSL. Sibling to asio_tls_transport; the TLS-specific members
// (ssl_ctx_/ssl_stream_/captured_pinset_/peer_id_/handshake role_) are dropped.
//
// Re-emitted as a contract; /speckit-tasks turns this into
// src/transport/asio_plain_transport.{hpp,cpp}. No behavioural change between
// contract and impl without re-running Gate A. See research.md D-1/D-2,
// data-model.md E-2.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory_resource>
#include <span>

namespace fixpp::transport {

class ListenerEvents;  // fwd — acceptor-side event sink (set on accepted transports)

// ─────────────────────────────────────────────────────────────────────────────
// asio_plain_transport — plain-TCP Transport (no TLS).
//
// state_t collapses the TLS {fresh, connected, handshaken, closed} to
// {fresh, connected, closed} — there is no handshake stage. After close(),
// every async_* returns transport_already_closed.
//
// In-flight exclusivity, cancellation→*_cancelled, idempotent close, and
// caller-owned read/write buffers follow the base Transport contract verbatim.
// ─────────────────────────────────────────────────────────────────────────────
class asio_plain_transport final : public Transport {
public:
    enum class state_t : std::uint8_t { fresh, connected, closed };

    // Initiator ctor — fresh socket, async_connect drives fresh→connected.
    asio_plain_transport(asio::any_io_executor exec, Transport::Config cfg) noexcept;

    // Acceptor adoption ctor — adopts an already-accepted socket (state=connected).
    struct from_accepted_tag {};
    asio_plain_transport(from_accepted_tag, asio::any_io_executor exec, Transport::Config cfg,
                         asio::ip::tcp::socket accepted_socket) noexcept;

    ~asio_plain_transport() override = default;

    // (1) TCP connect (resolve → connect → socket options); NO handshake follows.
    //     cancellation_type::total → transport_connect_cancelled. By ENTRY STATE (#339):
    //     connected → 97; closed → 98; fresh with an attempt IN FLIGHT → 97 (#342);
    //     fresh and idle → attempts (a failed attempt stays fresh).
    [[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>> async_connect(
        Endpoint const& ep) override;

    // (2) Read into caller-owned buffer from socket_. EOF → transport_read_eof.
    //     total → transport_read_cancelled.
    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override;

    // (3) Composed write of all bytes to socket_. Short write → transport_write_short.
    //     total → transport_write_cancelled.
    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> bytes [[clang::lifetimebound]]) override;

    // (4) Cancel in-flight connect/read/write. socket_.cancel(); does NOT close.
    [[nodiscard]] core::expected_t<void> cancel() noexcept override;

    // (5) Close: socket_.close() directly. NO TLS bidi shutdown / close-notify,
    //     NO tls_close_timeout wait (FR-011). Idempotent.
    [[nodiscard]] core::expected_t<void> close() noexcept override;

    // Acceptor event sink (mirrors asio_tls_transport::set_listener_events).
    void set_listener_events(ListenerEvents* ev) noexcept;

private:
    void apply_socket_options_() noexcept;  // tcp_nodelay/keepalive/bufs/linger (FR-010)

    Transport::Config cfg_;
    asio::any_io_executor exec_;
    asio::ip::tcp::socket socket_;
    state_t state_{state_t::fresh};
    bool read_in_flight_{false};
    bool write_in_flight_{false};
    ListenerEvents* listener_events_{nullptr};
};

}  // namespace fixpp::transport
