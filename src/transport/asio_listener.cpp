// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/asio_listener.cpp — body of fixpp::transport::asio_listener
// and the noexcept factory `make_asio_listener`.
//
// Design anchors:
//   [2h §4.6]      — Listener abstract surface (1 pure-virtual).
//   [2h §6.4.1]   — see the anchor list in asio_listener.hpp.
//   [2h §6.6]:1191 — transport_accept_cancelled error mapping.
//   [2a §4.2]     — trap_throw envelope for engine-bootstrap throws.
//   data-model E-10 — concrete asio_listener field set.
//   research.md D-17 — co_spawn default cancellation is terminal-only;
//                      each public coroutine resets to
//                      enable_total_cancellation() at entry.
//
// `cancel()` Option-A (Clarifications 2026-05-27 Q4=A) does EXACTLY:
//   (1) acceptor_.close() — no new TCP connections complete; subsequent
//       clients receive TCP RST or connection-refused per OS.
//   (2) Emit cancel on any in-flight async_accept so pending awaitables
//       receive `operation_aborted`, mapped to `transport_accept_cancelled`.
//   (3) Already-resumed `unique_ptr<Transport>` results are UNAFFECTED —
//       ownership has passed; the listener has no handle to them.
//
// Matches QuickFIX-cpp `SocketAcceptor::onStop():146-150` +
// Fix8 `~ServerSessionBase():511-514`.

#include "asio_listener.hpp"

#include <asio/error.hpp>
#include <asio/ip/address.hpp>
#include <asio/redirect_error.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <new>  // std::bad_alloc
#include <string>
#include <system_error>
#include <utility>

#include "asio_plain_transport.hpp"  // 043 T015: asio_plain_transport_factory::make_accepted
#include "asio_tls_transport.hpp"

namespace fixpp::transport {

namespace {

// Build an asio endpoint from a fixpp::transport::Endpoint. Bind side: host
// "" / "0.0.0.0" → IPv4 any; "::" → IPv6 any. Anything else parses as an IP
// literal (the v1.0 acceptor surface does NOT resolve hostnames at bind time
// per [2h §4.6] — the FR-024 initiator path resolves via tcp::resolver).
[[nodiscard]] asio::ip::tcp::endpoint make_bind_endpoint(Endpoint const& ep) {
    if (ep.host.empty() || ep.host == "0.0.0.0") {
        return asio::ip::tcp::endpoint{asio::ip::tcp::v4(), ep.port};
    }
    if (ep.host == "::") {
        return asio::ip::tcp::endpoint{asio::ip::tcp::v6(), ep.port};
    }
    asio::ip::address addr = asio::ip::make_address(ep.host);
    return asio::ip::tcp::endpoint{addr, ep.port};
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// asio_listener — throwing ctor ([arch §5.3] engine-bootstrap carve-out).
//
// Opens the acceptor, applies SO_REUSEADDR / SO_REUSEPORT, binds to
// `cfg_.bind_endpoint`, and starts the OS listen queue with depth
// `bind_endpoint.backlog` (clamped by OS at `/proc/sys/net/core/somaxconn`
// on Linux).
//
// Any ASIO / OS exception propagates out and is caught by
// `make_asio_listener`'s `trap_throw` envelope → transport_factory_failed.
// ─────────────────────────────────────────────────────────────────────────────
asio_listener::asio_listener(asio::any_io_executor exec, Config cfg)
    : cfg_{std::move(cfg)}, exec_{exec}, acceptor_{exec_} {
    const auto bind_ep = make_bind_endpoint(cfg_.bind_endpoint);

    acceptor_.open(bind_ep.protocol());

#ifdef _WIN32
    // Winsock divergence: SO_REUSEADDR on Windows lets a SECOND bind to the same
    // addr+port SUCCEED (port hijacking) — the inverse of POSIX, where a second
    // bind fails with EADDRINUSE. SO_EXCLUSIVEADDRUSE restores POSIX-like
    // exclusivity (duplicate bind → WSAEADDRINUSE) while still permitting the
    // same process to rebind after a clean restart. The two options are mutually
    // exclusive on Windows, so we set this INSTEAD of reuse_address. Raw
    // setsockopt (int value, char* cast) mirrors the SO_REUSEPORT call below and
    // avoids pulling in <windows.h>.
    const int exclusive = 1;
    ::setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                 reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
    if (cfg_.so_reuseaddr) {
        acceptor_.set_option(asio::socket_base::reuse_address{true});
    }
#endif

#ifdef SO_REUSEPORT
    if (cfg_.so_reuseport) {
        // SO_REUSEPORT is Linux-only kernel-side load-balancing. ASIO does not
        // expose a typed option; use the raw setsockopt level via native handle.
        const int on = 1;
        ::setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    }
#endif

    acceptor_.bind(bind_ep);

    const int backlog = static_cast<int>(cfg_.bind_endpoint.backlog);
    acceptor_.listen(backlog > 0 ? backlog : asio::socket_base::max_listen_connections);

    // Update cfg_.bind_endpoint.port with the OS-picked port when caller
    // passed port=0. This lets the engine / test query the resolved port.
    if (cfg_.bind_endpoint.port == 0) {
        const auto resolved = acceptor_.local_endpoint();
        cfg_.bind_endpoint.port = resolved.port();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// async_accept — listener accept loop body.
//
// One call → one accept → one fresh Transport. The caller (engine session
// bootstrap) typically wraps in its own per-counterparty co_spawn loop.
//
// Cancellation: D-17 reset to enable_total_cancellation lets the engine's
// total-cancel signal flow through `acceptor_.async_accept`; ASIO surfaces
// `operation_aborted` on the accepted socket result, mapped here to
// `transport_accept_cancelled` per [2h §6.6]:1191.
// ─────────────────────────────────────────────────────────────────────────────
asio::awaitable<core::expected_t<std::unique_ptr<Transport>>> asio_listener::async_accept() {
    using E = core::error;

    // Enable total cancellation (co_spawn defaults to terminal-only per D-17).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // Pre-accept cancellation reap.
    auto cs = co_await asio::this_coro::cancellation_state;
    if (cs.cancelled() != asio::cancellation_type::none) {
        co_return std::unexpected{E::transport_accept_cancelled};
    }

    // RC#H (P3-1): if the acceptor handle is already closed (e.g., cancel()
    // was called before this coroutine resumed) short-circuit immediately with
    // transport_accept_cancelled. This avoids dispatching to OS async_accept
    // on a closed handle — which on some platforms returns bad_descriptor
    // (mapped to transport_factory_failed) instead of the expected variant.
    if (!acceptor_.is_open()) {
        co_return std::unexpected{E::transport_accept_cancelled};
    }

    // `exec_` per the Thread-safety model note in asio_listener.hpp. A
    // consumer may rebind via socket.release() + assign(); v1.0 does not.
    asio::error_code accept_ec;
    asio::ip::tcp::socket accepted_socket{exec_};
    co_await acceptor_.async_accept(accepted_socket,
                                    asio::redirect_error(asio::use_awaitable, accept_ec));

    if (accept_ec) {
        if (accept_ec == asio::error::operation_aborted) {
            co_return std::unexpected{E::transport_accept_cancelled};
        }
        // Any other accept failure (OS resource exhaustion, ECONNABORTED,
        // etc.) surfaces as transport_factory_failed — the listener cannot
        // mint a Transport. The session FSM treats this as a transient
        // failure and may retry by spawning another async_accept.
        co_return std::unexpected{E::transport_factory_failed};
    }

    // Mint a fresh Transport adopting the accepted socket (FR-024).
    // PMR comes from cfg_.accepted_transport_config.mr (engine default if null).
    // 043 T015 (D-4/E-7): select the concrete accept factory based on transport_kind.
    // make_accepted() is concrete (non-virtual), so we branch on kind and call
    // it on the concretely-typed pointer — a base TransportFactory* would not compile.
    if (cfg_.transport_kind == transport_security_kind::plaintext) {
        // ── Plaintext accept path ─────────────────────────────────────────────
        if (!accept_factory_plain_) {
            auto made = make_asio_plain_transport_factory(cfg_.accepted_transport_config);
            if (!made.has_value()) {
                co_return std::unexpected{E::transport_factory_failed};
            }
            accept_factory_plain_ = std::shared_ptr<asio_plain_transport_factory>{
                static_cast<asio_plain_transport_factory*>(made->release())};
        }
        auto minted = accept_factory_plain_->make_accepted(std::move(accepted_socket), nullptr);
        if (!minted) {
            co_return std::unexpected{minted.error()};
        }
        // set_listener_events is wired for symmetry but is INERT on plaintext
        // (no TLS validation events emitted). [data-model §E-7; L-043-x]
        (*minted)->set_listener_events(&cfg_.events);
        co_return std::unique_ptr<Transport>(std::move(*minted));
    }

    // ── TLS accept path (default) ─────────────────────────────────────────────
    if (!accept_factory_) {
        auto made = make_asio_tls_transport_factory(cfg_.accepted_transport_config, cfg_.ssl_cfg);
        if (!made.has_value()) {
            co_return std::unexpected{E::transport_factory_failed};
        }
        accept_factory_ = std::shared_ptr<asio_tls_transport_factory>{
            static_cast<asio_tls_transport_factory*>(made->release())};
    }
    // cppcheck-suppress accessMoved  // FP: the plaintext branch above (:165-183)
    // always co_returns, so the std::move(accepted_socket) at :175 and this one are
    // in mutually-exclusive paths — accepted_socket is moved at most once. cppcheck
    // does not model co_return as a terminator. [043 T015]
    auto minted = accept_factory_->make_accepted(std::move(accepted_socket), nullptr);

    if (!minted) {
        co_return std::unexpected{minted.error()};
    }

    // 013 T039: wire the listener's event ring into the accepted transport so
    // async_handshake can emit session_event_tls_validation_failed on verify_peer
    // rejection. ListenerEvents is owned by cfg_.events (lifetime = listener);
    // the transport holds a non-owning pointer valid for the listener lifetime.
    // [FR-028 / data-model §E-5 / §E-6]
    (*minted)->set_listener_events(&cfg_.events);

    co_return std::unique_ptr<Transport>(std::move(*minted));
}

// ─────────────────────────────────────────────────────────────────────────────
// cancel — Option-A 3-action contract per FR-025.
//
// Synchronous; thread-safe. Idempotent on already-closed acceptors.
//   (1) `acceptor_.close()` — closing the OS handle. Subsequent client
//       connects receive TCP RST or ECONNREFUSED.
//   (2) `acceptor_.cancel()` is implied by close() per ASIO docs (cancels
//       any in-flight async_accept by surfacing operation_aborted). Both
//       call discharges the contract on its own.
//   (3) Already-resumed `unique_ptr<Transport>` results are owned by the
//       caller; the listener never recorded a handle, so there's nothing
//       to do (the contract is "leave alone"). Consumers that want
//       close-everything-I-produced track their own Transports.
// ─────────────────────────────────────────────────────────────────────────────
core::expected_t<void> asio_listener::cancel() noexcept {
    using E = core::error;

    asio::error_code ec;

    // close() cancels in-flight async_accept per ASIO contract; the
    // explicit acceptor_.cancel(...) defence-in-depth was redundant and
    // was dropped per /simplify Agent-2 P2 audit. Already-resumed
    // Transports are caller-owned per Option-A and unaffected.
    acceptor_.close(ec);

    if (ec && ec != asio::error::bad_descriptor) {
        // Genuine OS-level error during close. Map to a transport_* code
        // available in the error set; reuse transport_factory_failed for
        // CONFIG-class infrastructure failures.
        return std::unexpected{E::transport_factory_failed};
    }

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// make_asio_listener — noexcept factory (engine bootstrap / 2i C-ABI bridge).
//
// Wraps the throwing constructor in [2a §4.2] trap_throw and surfaces
// transport_factory_failed on any PMR / OS / OpenSSL exception. The mr
// parameter is reserved for future PMR-aware acceptor allocation; v1.0
// allocates only the OS-side acceptor handle so it is currently unused.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<std::unique_ptr<Listener>> make_asio_listener(
    asio::any_io_executor exec, asio_listener::Config cfg,
    std::pmr::memory_resource* /*mr*/) noexcept {
    try {
        auto ptr = std::make_unique<asio_listener>(std::move(exec), std::move(cfg));
        return std::unique_ptr<Listener>(std::move(ptr));
    } catch (std::bad_alloc const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (std::system_error const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (...) {
        return std::unexpected{core::error::transport_factory_failed};
    }
}

}  // namespace fixpp::transport
