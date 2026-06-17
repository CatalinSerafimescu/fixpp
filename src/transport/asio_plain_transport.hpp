// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/asio_plain_transport.hpp — INTERNAL header (NOT installed).
//
// Declares `fixpp::transport::asio_plain_transport`: a plain-TCP Transport
// (no TLS) over asio::ip::tcp::socket. Implements the 5 base Transport
// pure-virtuals. NOT a TlsTransport — no async_handshake, no OpenSSL.
//
// This is the sibling of asio_tls_transport; TLS-specific fields
// (ssl_ctx_, ssl_stream_, captured_pinset_, peer_id_, role_) are dropped.
//
// Design anchors:
//   specs/043-plaintext-tcp-transport/contracts/asio_plain_transport.hpp
//   research.md D-1/D-2/D-12 — no handshake, close = socket_.close, same TCP knobs
//   data-model.md E-2 — field set and state transitions

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/listener_events.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory_resource>
#include <span>

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// asio_plain_transport — plain-TCP Transport (no TLS).
//
// state_t collapses {fresh, connected, handshaken, closed} to
// {fresh, connected, closed} — there is no handshake stage.
//
// State transitions:
//   fresh      → (async_connect success)          → connected
//   fresh      → (from_accepted_tag ctor)          → connected
//   connected  → (close())                         → closed
//   *          → (close())                         → closed
//   connected  + cancel mid-IO                    → state unchanged (cancel ≠ close)
//
// Thread-safety: all async methods and the strand-confined flags
// (read_in_flight_, write_in_flight_) are confined to the session strand
// provided at construction. cancel() is the ONLY method that may be called
// off-strand; it calls socket_.cancel() which is ASIO-documented thread-safe.
// ─────────────────────────────────────────────────────────────────────────────
class asio_plain_transport final : public Transport {
public:
    enum class state_t : std::uint8_t { fresh, connected, closed };

    // Initiator constructor — fresh socket; async_connect drives fresh→connected.
    asio_plain_transport(asio::any_io_executor exec, Transport::Config cfg) noexcept;

    // Acceptor adoption constructor — adopts an already-accepted TCP socket.
    // State starts in state_t::connected (TCP 3-way handshake already complete
    // at the OS level). async_connect returns transport_already_connected.
    // apply_socket_options_() is called immediately to apply cfg_ knobs.
    struct from_accepted_tag {};
    asio_plain_transport(from_accepted_tag, asio::any_io_executor exec, Transport::Config cfg,
                         asio::ip::tcp::socket accepted_socket) noexcept;

    // Non-copyable; non-movable (socket_ tied to executor).
    asio_plain_transport(asio_plain_transport const&) = delete;
    asio_plain_transport& operator=(asio_plain_transport const&) = delete;
    asio_plain_transport(asio_plain_transport&&) = delete;
    asio_plain_transport& operator=(asio_plain_transport&&) = delete;

    ~asio_plain_transport() override = default;

    // ── Transport pure-virtual overrides ──────────────────────────────────────

    // (1) TCP connect (resolve → timer-armed connect → apply_socket_options_).
    //     NO TLS handshake. state fresh→connected.
    //     cancellation_type::total → transport_connect_cancelled.
    //     Second call → transport_already_connected.
    [[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>> async_connect(
        Endpoint const& ep) override;

    // (2) Read into caller-owned buffer from socket_.
    //     state != connected → transport_already_closed.
    //     in-flight conflict → transport_read_in_progress.
    //     EOF → transport_read_eof.
    //     cancelled → transport_read_cancelled.
    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override;

    // (3) Composed write (asio::async_write) of all bytes to socket_.
    //     state != connected → transport_already_closed.
    //     in-flight conflict → transport_write_in_progress.
    //     cancelled → transport_write_cancelled.
    //     partial write → transport_write_short.
    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> bytes [[clang::lifetimebound]]) override;

    // (4) Cancel in-flight operations. socket_.cancel(); does NOT close.
    //     Thread-safe per ASIO docs.
    [[nodiscard]] core::expected_t<void> cancel() noexcept override;

    // (5) Close: socket_.close() directly. NO TLS bidi shutdown, NO
    //     tls_close_timeout wait (D-2, FR-011). Idempotent.
    [[nodiscard]] core::expected_t<void> close() noexcept override;

    // ── Acceptor event sink (set_listener_events) ─────────────────────────────
    //
    // Called by asio_listener after make_accepted(). The pointer is non-owning.
    // On plain transport, this is wired for symmetry but is INERT — plaintext
    // accepted transports run no handshake and emit no TLS-validation events.
    // [data-model §E-7; L-043-x]
    void set_listener_events(ListenerEvents* ev) noexcept { listener_events_ = ev; }

private:
    // Apply FR-029 / FR-029a socket options (tcp_nodelay, SO_LINGER, recv/send
    // buffer sizes, TCP keepalive). Called by async_connect after TCP connect
    // completes (initiator), and by the from_accepted_tag ctor (acceptor).
    // Best-effort — individual setsockopt failures are silently dropped (D-12).
    void apply_socket_options_() noexcept;

    // ── Configuration (frozen at construction) ────────────────────────────────
    Transport::Config cfg_;

    // ── ASIO execution context ─────────────────────────────────────────────────
    asio::any_io_executor exec_;

    // ── Network object ─────────────────────────────────────────────────────────
    asio::ip::tcp::socket socket_;

    // ── State ─────────────────────────────────────────────────────────────────
    state_t state_{state_t::fresh};

    // ── In-flight exclusivity flags (strand-confined — NOT atomics) ───────────
    bool read_in_flight_{false};
    bool write_in_flight_{false};

    // ── Acceptor event sink (null on initiator side) ───────────────────────────
    ListenerEvents* listener_events_{nullptr};
};

}  // namespace fixpp::transport
