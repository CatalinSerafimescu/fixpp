// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::Listener — abstract multi-session acceptor (1 pure-virtual).
// + asio_listener default impl signature. Re-emitted verbatim from [2h §4.6].
//
// Discharges T-005. Pluggable; one default impl ships in v1.0. The pure-virtual
// surface is small (1 method); pluggability lets HFT users substitute a
// custom acceptor (e.g., epoll-driven or io_uring-driven) without touching
// Transport.
//
// Listener::cancel() Option-A contract per Clarifications 2026-05-27 Q4=A
// — async-coroutine-natural ownership semantics:
//   (1) Close the listening socket so no new TCP connections complete.
//   (2) Complete any in-flight async_accept awaitable not yet resumed with
//       transport_accept_cancelled (mapped from listener_accept_cancelled per
//       [2h §6.6]).
//   (3) Leave already-resumed-but-not-yet-consumed unique_ptr<Transport>
//       results UNAFFECTED — ownership has passed at async_accept return;
//       Listener has no handle to them.
//
// Reference-engine convergence (Clarifications Q4 sweep):
//   - QuickFIX-cpp SocketAcceptor::onStop():146-150 closes only listening
//     socket.
//   - Fix8 ~ServerSessionBase():511-514 only deletes server socket.
//   - QuickFIX/J kills managed sessions but at the SESSION layer, NOT Transport;
//     in our model Session is the post-this-feature Phase-4 concern.

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <memory>
#include <memory_resource>

#include <fixpp/core/expected.hpp>
#include <fixpp/tls/security_profile.hpp>   // [2g §4.5] SslCtxConfig (LOCKED)
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// Listener — abstract acceptor interface (1 pure-virtual; well under the
// [const §XIV.2] ≤ 5 cap).
//
// Each accepted connection MUST produce a freshly-minted Transport instance
// per spec FR-023. Lifetime is engine lifetime (outlives many Transport
// instances).
// ─────────────────────────────────────────────────────────────────────────────
class Listener {
public:
    virtual ~Listener() = default;

    // (1) Accept the next inbound connection and return a fresh Transport
    //     wrapping the accepted socket. The returned Transport is initially
    //     in the "connected" state (TCP handshake done by the OS); the FSM
    //     issues async_handshake (TLS) immediately.
    //
    //     Cancellation: cancellation_type::total → transport_accept_cancelled
    //     (mapped from the named variant listener_accept_cancelled per
    //     [2h §6.6]).
    [[nodiscard]] virtual asio::awaitable<core::expected_t<std::unique_ptr<Transport>>>
        async_accept() = 0;

    // (2) Cancel the listener per the Option-A contract above. Synchronous;
    //     thread-safe; idempotent. After cancel(), subsequent async_accept
    //     calls complete with transport_accept_cancelled; already-produced
    //     Transports (those whose async_accept awaitable has already resumed
    //     with the unique_ptr result) are UNAFFECTED — ownership has passed.
    //
    //     The v0.3 [2h §4.6] does not surface cancel() as a separate method;
    //     the Listener exposes a cancel() per [2h §6.4.1] engine-scoped row
    //     (Listener::async_accept honours Listener-owned cancel which is
    //     engine-scoped). Spec FR-025 codifies the 3-action behaviour.
    [[nodiscard]] virtual core::expected_t<void> cancel() noexcept = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// asio_listener — default impl wrapping asio::ip::tcp::acceptor. Signature
// only; body lives in src/transport/asio_listener.cpp.
// ─────────────────────────────────────────────────────────────────────────────
class asio_listener final : public Listener {
public:
    struct Config {
        // Local bind address + port + backlog. backlog ≥ 64 recommended for
        // multi-session deployments (per [2h §6.3] / spec US3).
        Endpoint                   bind_endpoint;

        // SO_REUSEADDR: typical acceptor default (true).
        bool                       so_reuseaddr {true};

        // SO_REUSEPORT: Linux-only kernel-side load-balancing across multiple
        // listeners on the same port; off by default.
        bool                       so_reuseport {false};

        // The accepted Transport's Config — passed through to each minted
        // Transport (Nagle OFF + no linger per Clarifications Q5=A defaults).
        Transport::Config          accepted_transport_config {};

        // Per-acceptor TLS profile. One SslCtxConfig per Listener — built by
        // the engine bootstrap. Per-counterparty profiles use one Listener
        // per counterparty per [2h §4.6] notes.
        fixpp::tls::SslCtxConfig   ssl_cfg;
    };

    // Factory: returns expected_t<...> for non-construction-time callers.
    // Throwing-on-failure constructor is permitted at engine bootstrap per
    // [arch §5.3] carve-out.
    [[nodiscard]] static core::expected_t<std::unique_ptr<Listener>>
        make_asio_listener(asio::any_io_executor       exec,
                           Config                      cfg,
                           std::pmr::memory_resource*  mr) noexcept;

    explicit asio_listener(asio::any_io_executor exec, Config cfg);
    ~asio_listener() override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::unique_ptr<Transport>>>
        async_accept() override;

    [[nodiscard]] core::expected_t<void> cancel() noexcept override;

private:
    Config                      cfg_;
    asio::any_io_executor       exec_;
    asio::ip::tcp::acceptor     acceptor_;
};

}  // namespace fixpp::transport
