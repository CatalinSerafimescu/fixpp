// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/asio_listener.hpp — INTERNAL header (NOT installed).
//
// Declares `fixpp::transport::asio_listener`: the v1.0 reference Listener
// over `asio::ip::tcp::acceptor`. The free factory
// `make_asio_listener(...)` (declared at the bottom as a noexcept free
// function in `fixpp::transport`) is the non-construction-time entry point
// per [arch §5.3] — engine bootstrap may use the throwing constructor
// directly via the [2a §4.2] `trap_throw` envelope; runtime / 2i C-ABI
// callers go through the factory.
//
// Design anchors:
//   [2h §4.6]      — Listener abstract surface (1 pure-virtual).
//   [2h §4.6]:810 — verbatim async_accept signature.
//   [2h §6.4.1]   — engine service-strand row (Listener-owned cancel is
//                   engine-scoped; the FR-025 3-action concrete contract
//                   binds here).
//   [2h §6.6]:1191 — transport_accept_cancelled error mapping.
//   data-model E-10 — concrete asio_listener fields (cfg_/exec_/acceptor_).
//   [arch §5.3]    — engine-bootstrap throwing-ctor carve-out.
//   [arch §4.5]    — namespace placement: `fixpp::transport::`.
//   [arch §6]      — PMR rule 4 (engine default).
//
// `asio_listener::cancel()` does EXACTLY three things per FR-025 +
// Clarifications 2026-05-27 Q4=A (Option-A async-coroutine-natural ownership):
//   (1) Close the listening socket (no new TCP connections complete;
//       subsequent clients receive TCP RST / connection-refused per OS).
//   (2) Emit cancel on any in-flight async_accept awaitable so pending
//       awaitables receive `operation_aborted`, mapped to
//       `transport_accept_cancelled`.
//   (3) DO NOT track or close already-produced Transports — ownership has
//       transferred at async_accept return; the listener has no handle to
//       them. Consumers that want close-everything-I-produced MUST track
//       their own held Transports and call `transport.close()` on each.
//
// Matches QuickFIX-cpp `SocketAcceptor::onStop():146-150` +
// Fix8 `~ServerSessionBase():511-514` async-coroutine-natural ownership.

#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <fixpp/core/error.hpp>                 // core::expected_t<T>
#include <fixpp/tls/security_profile.hpp>       // fixpp::tls::SslCtxConfig
#include <fixpp/transport/endpoint.hpp>         // Endpoint
#include <fixpp/transport/listener.hpp>         // abstract Listener
#include <fixpp/transport/listener_events.hpp>  // 013 T039: ListenerEvents
#include <fixpp/transport/transport.hpp>        // Transport + Transport::Config
#include <fixpp/transport/transport_factory.hpp>  // transport_security_kind, asio_plain_transport_factory
#include <memory>
#include <memory_resource>
#include <span>

namespace fixpp::transport {

class asio_tls_transport_factory;
class asio_plain_transport_factory;

// ─────────────────────────────────────────────────────────────────────────────
// asio_listener — concrete Listener wrapping asio::ip::tcp::acceptor.
//
// Throwing constructor (engine-bootstrap carve-out per [arch §5.3]):
//   May throw on PMR exhaustion or ASIO acceptor setup failure (bind/listen).
//   The factory `make_asio_listener(...)` wraps in [2a §4.2] trap_throw and
//   surfaces transport_factory_failed.
//
// Thread-safety model:
//   `async_accept()` runs on the engine service strand (`exec_`) per
//   [2h §6.4.1]. `cancel()` is the synchronous half — it may be called
//   off-strand; `acceptor_.close()` and `acceptor_.cancel()` are
//   thread-safe per ASIO and surface operation_aborted on the in-flight
//   awaitable. Already-resumed `unique_ptr<Transport>` results are owned
//   by the caller and are NOT touched by cancel().
// ─────────────────────────────────────────────────────────────────────────────
class asio_listener final : public Listener {
public:
    struct Config {
        // Local bind address + port + backlog. backlog ≥ 64 recommended for
        // multi-session deployments (per [2h §6.3] / spec US3).
        Endpoint bind_endpoint;

        // SO_REUSEADDR: typical acceptor default (true).
        bool so_reuseaddr{true};

        // SO_REUSEPORT: Linux-only kernel-side load-balancing across multiple
        // listeners on the same port; off by default.
        bool so_reuseport{false};

        // The accepted Transport's Config — passed through to each minted
        // Transport (Nagle OFF + no linger per Clarifications Q5=A defaults).
        Transport::Config accepted_transport_config{};

        // Per-acceptor TLS profile. One SslCtxConfig per Listener — built by
        // the engine bootstrap. Per-counterparty profiles use one Listener
        // per counterparty per [2h §4.6] notes.
        fixpp::tls::SslCtxConfig ssl_cfg;

        // 043 T015 (D-4/E-7): transport kind selector. Default = tls preserves
        // backward compatibility for all existing callers. Set to
        // transport_security_kind::plaintext for insecure_plain_tcp sessions.
        transport_security_kind transport_kind{transport_security_kind::tls};

        // 013 T039 — per-listener session-event ring for pre-Session TLS
        // validation events. Owned by Config (and therefore by asio_listener).
        // Lifetime: spans the Listener instance — any non-owning pointer into
        // this member (e.g., held by an accepted asio_tls_transport) is valid
        // for the listener's lifetime. [FR-028 / data-model §E-5 / §E-6]
        ListenerEvents events;
    };

    // Throwing constructor — engine-bootstrap carve-out per [arch §5.3].
    // MUST NOT be called outside of a `trap_throw` boundary at runtime.
    explicit asio_listener(asio::any_io_executor exec, Config cfg);

    asio_listener(asio_listener const&) = delete;
    asio_listener& operator=(asio_listener const&) = delete;
    asio_listener(asio_listener&&) = delete;
    asio_listener& operator=(asio_listener&&) = delete;

    ~asio_listener() override = default;

    // ── Listener override ──────────────────────────────────────────────────
    [[nodiscard]] asio::awaitable<core::expected_t<std::unique_ptr<Transport>>> async_accept()
        override;

    // ── Concrete-impl-only API per spec FR-023 / FR-025 ────────────────────
    //
    // NOT `override` — the abstract Listener base does NOT publish cancel().
    // The engine-scoped Listener-cancel surface is published at
    // [2h §6.4.1]:1124. Honours the 3-action contract per the file-level
    // Option-A note above.
    [[nodiscard]] core::expected_t<void> cancel() noexcept;

    // Returns the OS-resolved bind endpoint. When the caller passed port=0,
    // the ctor records the OS-picked port back into cfg_.bind_endpoint.port
    // so consumers (e.g., the engine or tests) can connect clients to the
    // listener without out-of-band coordination.
    [[nodiscard]] Endpoint bound_endpoint() const noexcept { return cfg_.bind_endpoint; }

    // 013 T040 — operator-observation surface for pre-Session TLS validation
    // events (FR-028 / FR-035). Returns a membership-witness view over the
    // last ≤16 emitted SessionEvents (physical ring order; NOT chronological).
    // Parallel shape to Session::recent_events(). [data-model §E-6]
    [[nodiscard]] std::span<const fixpp::session::SessionEvent> recent_events() const noexcept {
        return cfg_.events.recent_events();
    }

private:
    Config cfg_;
    asio::any_io_executor exec_;
    asio::ip::tcp::acceptor acceptor_;
    // Concretely-typed accept factories (make_accepted() is non-virtual —
    // E-4/E-7). Exactly one is used, selected by cfg_.transport_kind.
    // Lazily initialised on first async_accept() call (mirrors TLS pattern).
    std::shared_ptr<asio_tls_transport_factory> accept_factory_;
    std::shared_ptr<asio_plain_transport_factory> accept_factory_plain_;  // 043 T015
};

// ─────────────────────────────────────────────────────────────────────────────
// make_asio_listener — noexcept factory (engine bootstrap / 2i C-ABI bridge).
//
// Wraps the throwing `asio_listener` constructor in the [2a §4.2] `trap_throw`
// envelope and returns `expected_t::unexpected{transport_factory_failed}` on
// any OS / PMR throw. Body lives in src/transport/asio_listener.cpp.
//
// The `mr` parameter is reserved for future PMR-aware acceptor allocation
// per [arch §6] rule 4 — currently unused (the v1.0 acceptor allocates only
// the OS-side acceptor handle).
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<std::unique_ptr<Listener>> make_asio_listener(
    asio::any_io_executor exec, asio_listener::Config cfg, std::pmr::memory_resource* mr) noexcept;

}  // namespace fixpp::transport
