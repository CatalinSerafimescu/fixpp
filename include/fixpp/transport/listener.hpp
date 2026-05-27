// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::Listener — abstract multi-session acceptor.
// Re-emitted from `specs/012-2h-transport/contracts/listener.hpp` per T035.
//
// Surface mirrors [2h §4.6]:810 verbatim: EXACTLY 1 pure-virtual
// (`async_accept`), well under the [const §XIV.2] ≤ 5 cap.
//
// `cancel()` is NOT a pure-virtual on the abstract base. The engine-scoped
// Listener-cancel behaviour is published as a service row in
// [2h §6.4.1]:1124; the FR-025 3-action contract binds as a CONCRETE-IMPL
// method on `asio_listener` (see src/transport/asio_listener.hpp). This
// preserves the [const §XIV.2] count and the inherited surface from
// [2h §4.6] verbatim — Gate A round 1 RC#1 (Codex P1-2) close per Opus
// reviewer overruling Codex's framing.
//
// Reference-engine convergence (Clarifications 2026-05-27 Q4=A sweep):
//   - QuickFIX-cpp SocketAcceptor::onStop():146-150 closes only listening
//     socket.
//   - Fix8 ~ServerSessionBase():511-514 only deletes server socket.
//   - QuickFIX/J kills managed sessions at the SESSION layer, NOT Transport;
//     in our model Session is the post-this-feature Phase-4 concern.

#pragma once

#include <asio/awaitable.hpp>
#include <memory>

#include <fixpp/core/error.hpp>          // core::expected_t<T>
#include <fixpp/transport/transport.hpp>  // Transport

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// Listener — abstract acceptor interface. EXACTLY 1 pure-virtual method per
// [2h §4.6]:810. Each accepted connection MUST produce a freshly-minted
// Transport instance per spec FR-023. Lifetime is engine lifetime (outlives
// many Transport instances).
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
    //     per [2h §6.6]:1191.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<std::unique_ptr<Transport>>>
        async_accept() = 0;
};

}  // namespace fixpp::transport
