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
#include <memory>
#include <memory_resource>
#include <span>

// The shared timer-epoch counter block (D-4.1) — internal, src/ only.
#include "timer_epoch_state.hpp"

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
// Thread-safety: all async methods and the strand-confined in-flight flags
// (timer_epochs_->read_in_flight / write_in_flight, #346) are confined to the
// session strand provided at construction. ⚠️ cancel() was documented off-strand-safe here;
// struck 2026-08-31 (#333) — see Transport::cancel(). Treat as strand-confined.
// ─────────────────────────────────────────────────────────────────────────────
class asio_plain_transport final : public Transport {
public:
    enum class state_t : std::uint8_t { fresh, connected, closed };

    // ── Timer-epoch guard (research.md D-4.1, FR-014) ───────────────────────
    //
    // The counter block is the SHARED `fixpp::transport::timer_epoch_state`
    // from "transport/timer_epoch_state.hpp" — one type for both transports,
    // as data-model.md §4 and D-4.1 specify. This transport uses `connect`
    // only; the `handshake` field is TLS-only and unused here.

    // Initiator constructor — fresh socket; async_connect drives fresh→connected.
    // NOT noexcept (088/FR-014 widening): the default member initializer for
    // timer_epochs_ below does a make_shared, which can throw bad_alloc. Both
    // call sites (transport_factory.cpp asio_plain_transport_factory::make /
    // ::make_accepted) already wrap construction in the [2a §4.2] trap_throw
    // try/catch, previously vacuous, now load-bearing. See
    // .specify/decisions/088-firstframe-budget-timer-lifetime-verify.md
    // "Cross-feature surface note" for why this does not breach 043's
    // contract (internal type, not an installed/C-ABI surface).
    asio_plain_transport(asio::any_io_executor exec, Transport::Config cfg);

    // Acceptor adoption constructor — adopts an already-accepted TCP socket.
    // State starts in state_t::connected (TCP 3-way handshake already complete
    // at the OS level). async_connect returns transport_already_connected.
    // apply_socket_options_() is called immediately to apply cfg_ knobs.
    // NOT noexcept — same 088/FR-014 reason as the initiator ctor above.
    struct from_accepted_tag {};
    asio_plain_transport(from_accepted_tag, asio::any_io_executor exec, Transport::Config cfg,
                         asio::ip::tcp::socket accepted_socket);

    // Non-copyable; non-movable (socket_ tied to executor).
    asio_plain_transport(asio_plain_transport const&) = delete;
    asio_plain_transport& operator=(asio_plain_transport const&) = delete;
    asio_plain_transport(asio_plain_transport&&) = delete;
    asio_plain_transport& operator=(asio_plain_transport&&) = delete;

    // Not `= default`: the destructor BODY retires the armed timer epoch
    // (D-4.1 item 3) before members are destroyed, so a stranded timer
    // handler that observes the retirement is guaranteed `this` is still
    // alive. Members are still destroyed in reverse declaration order after
    // the body runs. Body in the .cpp.
    ~asio_plain_transport() override;

    // ── Transport pure-virtual overrides ──────────────────────────────────────

    // (1) TCP connect (resolve → timer-armed connect → apply_socket_options_).
    //     NO TLS handshake. state fresh→connected.
    //     cancellation_type::total → transport_connect_cancelled.
    //     By ENTRY STATE (#339, #342): connected → 97; closed → 98; fresh with an
    //     attempt IN FLIGHT → 97; fresh and idle → attempts (a failed attempt
    //     stays fresh and is retryable).
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
    //     Strand-confined — asio withholds its thread-safety carve-out from
    //     cancel (#333; see Transport::cancel() for the vendored-header cite).
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

    // ── Debug strand-confinement assert (T016/D-13/R8) ────────────────────────
    //
    // Returns the underlying TCP socket's associated executor. Used by the
    // engine's `assert_transport_on_session_strand` to verify INV-7 (the socket
    // executor MUST equal the per-session strand at every construction site).
    // Mirrors asio_tls_transport::socket_executor(). Non-const because
    // get_executor() is not const on asio::ip::tcp::socket.
    [[nodiscard]] asio::any_io_executor socket_executor() noexcept {
        return socket_.get_executor();
    }

    // ── Timer-epoch accessor (D-4.1 mechanism 2; D-9 mechanism 2) ───────────
    //
    // Used by the T3-T5 witness cells to observe the retire-point-omitted
    // mutant (D-6.4). This is exactly what it buys — not broader guard
    // coverage; the sibling "guard omitted" mutant has no killer and is
    // discharged structurally (D-9). Const, internal-header-only: no
    // production branch, no FIXPP_TEST_HOOKS.
    [[nodiscard]] std::shared_ptr<const timer_epoch_state> timer_epochs() const noexcept {
        return timer_epochs_;
    }

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

    // ── Timer-epoch guard (D-4.1) ────────────────────────────────────────────
    // Held by shared_ptr, not by value: a timer handler captures a copy of
    // this pointer (never `this`), so `*timer_epochs_` outlives the
    // transport unconditionally even when the owner destroys `this` with no
    // drain (D-4.0 — reconnect_fsm.cpp:250-252 and engine.cpp:841-844 both
    // destroy synchronously on the failure arm).
    std::shared_ptr<timer_epoch_state> timer_epochs_{std::make_shared<timer_epoch_state>()};

    // #346: read/write in-flight flags live in *timer_epochs_, managed by
    // detail::inflight_flag_guard — rationale in that header.

    // ── Acceptor event sink (null on initiator side) ───────────────────────────
    ListenerEvents* listener_events_{nullptr};

    // Test-access friend — T006 socket-option verification.
    // Mirrors asio_tls_transport::asio_tls_transport_test_access pattern.
    friend class asio_plain_transport_test_access;
};

}  // namespace fixpp::transport
