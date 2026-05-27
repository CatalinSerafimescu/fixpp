// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/asio_tls_transport.hpp — INTERNAL header (NOT installed).
//
// Declares `fixpp::transport::asio_tls_transport`: the v1.0 reference
// implementation of `TlsTransport` over ASIO tcp::socket + asio::ssl::stream
// (OpenSSL back-end). Also declares the free-function factory
// `make_asio_tls_transport(...)` consumed by non-construction-time callers
// (the 2i C-ABI / runtime reconnect mint path per [arch §5.3]).
//
// Design anchors:
//   [2h §4.5] — asio_tls_transport fields, state machine, invariants.
//   data-model E-9 — canonical field set (lines 270-290).
//   [arch §5.3] — engine-bootstrap carve-out for the throwing constructor.
//   [arch §5.6] — frozen-at-open; unique_ptr ownership by Session.
//
// CONTRACT NOTES (for T027 — the body author):
//
//   FR-007  IN-FLIGHT EXCLUSIVITY
//     At most one in-flight async_read_some AND one in-flight async_write
//     per instance. A concurrent second call returns IMMEDIATELY with
//     transport_read_in_progress / transport_write_in_progress.
//     async_connect and async_handshake are one-shot; a second call returns
//     transport_already_connected.
//     Implementation note: `read_in_flight_` / `write_in_flight_` are
//     strand-confined booleans (NOT atomics) — all Transport coroutines run
//     on `exec_`'s strand per [2d §4.8], so unlocked reads are safe.
//     `cancel()` is synchronous and thread-safe; it only modifies the
//     `cancel_signal_` (whose emission is thread-safe per ASIO docs) and
//     does NOT touch the flags or state.
//
//   FR-026  PINSET CAPTURE CACHING CONTRACT
//     `cfg.pinset->snapshot()` is captured ONCE at `async_handshake` start
//     into `captured_pinset_` BEFORE the OpenSSL handshake protocol exchange
//     begins per [2g §6.5.1] BINDING CONTRACT. The SSL_VERIFY_PEER trampoline
//     retrieves this snapshot via SSL_get_ex_data — it MUST NOT call
//     cfg.pinset->find / contains / snapshot mid-verification.
//
//   FR-029a DEFAULT SOCKET KNOBS (Clarifications 2026-05-27 Q5=A)
//     tcp_nodelay = true (Nagle OFF).
//     so_linger_enabled = false (no linger).
//     Applied by async_connect after the kernel 3-way handshake completes.
//
//   [feedback_asio_post_resume_bounces_to_spawn_executor] — D-18 REMINDER
//     Do NOT use `co_await asio::post(other_exec, use_awaitable)` to hop
//     executors — only the completion handler runs there; the coroutine body
//     bounces back to the spawn executor. To pin a coroutine body to a
//     different executor, use nested `co_spawn(other_exec, fn, use_awaitable)`
//     and put the work inside `fn`. ALL Transport coroutines must be
//     co_spawn'd on `exec_` at call sites.

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/stream.hpp>
#include <memory>
#include <memory_resource>
#include <optional>

#include <fixpp/core/error.hpp>             // core::expected_t<T>
#include <fixpp/tls/peer_identity.hpp>      // fixpp::tls::peer_identity
#include <fixpp/tls/pinset.hpp>             // fixpp::tls::pin_snapshot
#include <fixpp/tls/security_profile.hpp>   // fixpp::tls::SslCtxConfig
#include <fixpp/transport/tls_transport.hpp>  // TlsTransport + handshake_result
#include <fixpp/transport/transport.hpp>      // Transport + ConnectInfo + Config

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// asio_tls_transport
//
// Concrete implementation of TlsTransport over ASIO + OpenSSL (SSL_CTX).
// Minted by asio_tls_transport_factory::make(...) per FR-026 reconnect mint
// pattern (fresh Transport per attempt; previous Transport destroyed first).
//
// Throwing constructor (engine-bootstrap carve-out per [arch §5.3]):
//   The constructor may throw if OpenSSL context initialisation fails
//   (e.g., SSL_CTX_new returns null). This is acceptable only at session-open
//   time inside the factory's trap_throw envelope.
//
// State machine (E-1 transitions held in state_):
//   fresh       → (async_connect success)      → connected
//   connected   → (async_handshake success)    → handshaken
//   connected   → (async_handshake cancel/err) → closed
//   handshaken  → (close())                    → closed
//   *           → (close())                    → closed
//   connected / handshaken + cancel mid-IO     → state unchanged (cancel ≠ close)
//
// Thread-safety model:
//   All async methods and the strand-confined flags (read_in_flight_,
//   write_in_flight_) are confined to the session strand provided at
//   construction. cancel() is the ONLY method that may be called off-strand;
//   it emits cancel_signal_ which is thread-safe per ASIO.
// ─────────────────────────────────────────────────────────────────────────────
class asio_tls_transport final : public TlsTransport {
public:
    // ── State type ─────────────────────────────────────────────────────────
    enum class state_t : std::uint8_t {
        fresh,       // after construction; no connect attempted yet
        connected,   // TCP 3-way handshake complete; TLS not started
        handshaken,  // TLS handshake complete; read/write active
        closed       // close() called or fatal error; no further async ops
    };

    // ── TLS role (RC#A — P1-1 fix) ─────────────────────────────────────────
    // Distinguishes initiator (client) from acceptor (server) so async_handshake
    // passes the correct stream_base role to OpenSSL. The role is private to
    // the impl class; the public TlsTransport::async_handshake virtual does NOT
    // expose it ([const §XIV.2]: no extra pure-virtual added).
    enum class role_t : std::uint8_t { client, server };

    // ── Constructor (throwing — [arch §5.3] engine-bootstrap carve-out) ───
    //
    // Builds the asio::ssl::context from `ssl_cfg` fields (protocol version
    // floor/ceiling, cipher list, verify flags, etc.). May throw if OpenSSL
    // SSL_CTX_new fails or if cert/key material loading fails. The factory
    // wraps this in trap_throw and surfaces transport_factory_failed.
    //
    // MUST NOT be called outside of a trap_throw boundary at runtime.
    explicit asio_tls_transport(asio::any_io_executor     exec,
                                Transport::Config          cfg,
                                fixpp::tls::SslCtxConfig   ssl_cfg);

    // Accept-adoption constructor (US3 — asio_listener mint path per FR-024).
    //
    // Adopts an already-connected TCP socket produced by
    // asio::ip::tcp::acceptor::async_accept. The OS-side 3-way handshake has
    // already completed; state_ starts in state_t::connected. async_connect is
    // therefore one-shot-already-spent — calling it returns
    // transport_already_connected. The FSM (or test) calls async_handshake
    // directly to drive the TLS handshake.
    //
    // accepted_socket MUST be bound to the same executor as `exec` (the
    // listener typically binds via `acceptor_.async_accept(exec)` overload, or
    // the test does the equivalent). Throws on OpenSSL SSL_CTX setup failure
    // per the [arch §5.3] engine-bootstrap carve-out.
    asio_tls_transport(asio::any_io_executor       exec,
                       Transport::Config           cfg,
                       fixpp::tls::SslCtxConfig    ssl_cfg,
                       asio::ip::tcp::socket       accepted_socket);

    // Non-copyable; non-movable (ssl_stream_ holds a ref to socket_).
    asio_tls_transport(asio_tls_transport const&)            = delete;
    asio_tls_transport& operator=(asio_tls_transport const&) = delete;
    asio_tls_transport(asio_tls_transport&&)                 = delete;
    asio_tls_transport& operator=(asio_tls_transport&&)      = delete;

    ~asio_tls_transport() override = default;

    // ── Transport overrides (bodies in asio_tls_transport.cpp — T027) ─────

    [[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>>
        async_connect(Endpoint const& ep) override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
        async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
        async_write(std::span<const std::byte> bytes [[clang::lifetimebound]]) override;

    [[nodiscard]] core::expected_t<void> cancel() noexcept override;

    [[nodiscard]] core::expected_t<void> close()  noexcept override;

    // ── TlsTransport override ───────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<core::expected_t<handshake_result>>
        async_handshake(fixpp::tls::SslCtxConfig const& cfg
                            [[clang::lifetimebound]]) override;

    // ── Test-access friend ──────────────────────────────────────────────────
    //
    // Grants state_ visibility to integration tests that need to verify the
    // exact in-flight exclusivity path (e.g., inject state = connected then
    // call async_handshake twice). Used by:
    //   tests/transport/test_inflight_exclusivity.cpp (DISABLED_ integration cells)
    //   tests/transport/test_tls_handshake_pinset_rotation.cpp (rotation cell)
    friend class asio_tls_transport_test_access;

private:
    // Common SSL_CTX setup shared by both ctors. Reads ssl_cfg_, populates
    // *ssl_ctx_ (protocol bounds, cipher suites, kx groups, sigalgs, verify
    // trampoline, cert/key chain, trust anchors). Throws on any OpenSSL
    // failure — callers (both ctors) run inside the [arch §5.3] / [2a §4.2]
    // trap_throw boundary.
    void setup_ssl_ctx_();

    // Apply FR-029 / FR-029a socket options (TCP_NODELAY, SO_LINGER,
    // recv/send buffer sizes, TCP keepalive) on socket_. Called by
    // async_connect() after the OS-side connect completes (initiator leg)
    // and by the accept-adoption ctor (acceptor leg). Best-effort —
    // individual setsockopt failures are silently dropped (matches the
    // initiator-side semantics; the gate is the post-condition assertion
    // in the T019 cells).
    void apply_socket_options_() noexcept;

private:
    // ── Configuration (frozen at construction) ──────────────────────────────
    Transport::Config          cfg_;       // per-session knobs (E-7)
    fixpp::tls::SslCtxConfig   ssl_cfg_;   // TLS profile; pinset lives here

    // ── ASIO execution context ──────────────────────────────────────────────
    asio::any_io_executor      exec_;      // session executor ([2d §4.8])

    // ── Network objects ─────────────────────────────────────────────────────
    // socket_ MUST be declared before ssl_stream_ so it is destroyed last.
    asio::ip::tcp::socket      socket_;    // underlying TCP socket

    // ssl_ctx_ is the RAII-owned SSL_CTX*. Built at construction time from
    // ssl_cfg_ fields (proto version, ciphers, verify mode, cert material).
    // Must outlive ssl_stream_.
    std::unique_ptr<asio::ssl::context>    ssl_ctx_;

    // ssl_stream_ is constructed lazily at async_handshake start. It wraps
    // socket_ by reference — therefore socket_ and ssl_ctx_ MUST outlive it.
    // Declared as optional so we defer construction until the TCP socket is
    // in a connected state (the ASIO ssl::stream constructor sets up internal
    // SSL state that requires a valid socket fd on some impls).
    std::optional<asio::ssl::stream<asio::ip::tcp::socket&>>  ssl_stream_;

    // ── Handshake-time pinset snapshot (captured once per [2g §6.5.1]) ─────
    std::shared_ptr<const fixpp::tls::pin_snapshot>  captured_pinset_;

    // ── Peer identity (populated by verify_peer trampoline via ssl_cfg_) ───
    fixpp::tls::peer_identity  peer_id_;

    // ── State ───────────────────────────────────────────────────────────────
    state_t  state_  {state_t::fresh};

    // ── Role (initiator vs acceptor — set by ctor; used by async_handshake) ─
    role_t   role_   {role_t::client};

    // ── In-flight exclusivity flags (strand-confined — NOT atomics) ─────────
    //
    // These booleans are read and written exclusively from coroutines running
    // on exec_'s strand. cancel() is the only off-strand writer and it does
    // NOT touch these flags — it only fires cancel_signal_.
    bool  read_in_flight_  {false};
    bool  write_in_flight_ {false};

    // ── Cancellation ────────────────────────────────────────────────────────
    //
    // cancel() propagates via asio::ip::tcp::socket::cancel(ec) — which is
    // thread-safe and triggers operation_aborted on every in-flight read /
    // write / connect / handshake bound to socket_. Each public coroutine
    // also calls reset_cancellation_state(enable_total_cancellation()) at
    // entry (D-17), allowing FSM-emitted cancellation_type::total to fire
    // through the awaiter's native slot. No per-transport cancellation_signal
    // is held — the socket itself is the cancel sink.
};

// ─────────────────────────────────────────────────────────────────────────────
// make_asio_tls_transport — noexcept factory (2i C-ABI / runtime reconnect mint)
//
// Wraps the throwing asio_tls_transport constructor in the [2a §4.2]
// trap_throw envelope and returns expected_t::unexpected{
// error::transport_factory_failed} on any OS / OpenSSL / PMR throw.
//
// Per the FR-028 reconnect contract: each async_connect retry operates on a
// FRESH Transport minted by this call; the previous Transport MUST be destroyed
// before this function is invoked. The factory (asio_tls_transport_factory) owns
// this policy; callers of this free function are responsible for the same
// destroy-before-mint ordering.
//
// Body lives in src/transport/transport_factory.cpp (T028).
[[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
make_asio_tls_transport(asio::any_io_executor      exec,
                        Transport::Config           cfg,
                        fixpp::tls::SslCtxConfig    ssl_cfg,
                        std::pmr::memory_resource*  mr) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// make_accepted_asio_tls_transport — noexcept factory (US3 / FR-024 path).
//
// Adopts an already-connected TCP socket produced by
// asio::ip::tcp::acceptor::async_accept. Returns a Transport in
// state_t::connected; the FSM calls async_handshake to drive TLS handshake.
//
// Used exclusively by asio_listener::async_accept; no SessionConfig / 2i C-ABI
// path mints accept-side Transports.
//
// Body lives in src/transport/transport_factory.cpp alongside the initiator
// mint helper.
[[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
make_accepted_asio_tls_transport(asio::any_io_executor      exec,
                                  Transport::Config           cfg,
                                  fixpp::tls::SslCtxConfig    ssl_cfg,
                                  asio::ip::tcp::socket       accepted_socket,
                                  std::pmr::memory_resource*  mr) noexcept;

}  // namespace fixpp::transport
