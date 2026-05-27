// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/transport_factory.cpp
//
// Implements:
//   - fixpp::transport::asio_tls_transport_factory  (T028 — declared in
//     include/fixpp/transport/transport_factory.hpp; forward-declared body)
//   - fixpp::transport::make_asio_tls_transport     (free function — declared
//     in src/transport/asio_tls_transport.hpp)
//
// Design anchors:
//   [2h §4.7]      — TransportFactory interface + noexcept contract.
//   data-model E-11 — factory caching contract (FR-026).
//   [2a §4.2]      — trap_throw pattern for PMR / OS / OpenSSL throws.
//   [arch §5.6]    — frozen-at-open; session holds unique_ptr<Transport>.
//
// FR-026 CACHING CONTRACT (BINDING):
//   The SslCtxConfig (carrying the OpenSSL SSL_CTX*, pinset, clock, caps),
//   the engine PMR root resource, and the engine clock are long-lived objects
//   cached at the factory level. They MUST NOT be rebuilt per reconnect
//   attempt. Building an SSL_CTX (with cert load + chain build + OCSP check)
//   takes ~10-50 ms; rebuilding per-attempt would amortise badly against the
//   bounded back-off envelope ([2h §6.2] D-15).
//
//   This impl stores the template Config at construction. The SslCtxConfig is
//   accepted by value from the caller of make() — it IS the cached object: the
//   caller (typically Session::open) constructs it ONCE via make_ssl_ctx_config
//   and passes it into every make() invocation. The factory does not re-call
//   make_ssl_ctx_config; it forwards the already-built config to
//   asio_tls_transport's constructor.
//
// FR-028 RECONNECT CONTRACT (BINDING):
//   Factory SELECTION is frozen-at-open per [arch §5.6]:
//     resolved factory = SessionConfig::transport_factory_override OR
//                        EngineConfig::default_transport_factory.
//   Factory INVOCATION is per-attempt:
//     Each async_connect retry MUST operate on a fresh Transport minted by
//     make(). The previous Transport MUST be destroyed BEFORE this factory is
//     invoked for a new attempt. The FSM holds at most one live Transport per
//     session at any time.

#include "asio_tls_transport.hpp"  // asio_tls_transport + make_asio_tls_transport decl

#include <fixpp/core/decimal_helpers.hpp>   // fixpp::core::detail::trap_throw
#include <fixpp/core/error.hpp>             // core::error::transport_factory_failed
#include <fixpp/transport/transport_factory.hpp>  // asio_tls_transport_factory decl

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// make_asio_tls_transport — noexcept free function
//
// This is the canonical construction point for asio_tls_transport instances.
// The factory's make() delegates here; the 2i C-ABI bridge also calls this
// function directly without going through the factory object.
//
// The mr parameter is advisory for callers that want to nominate a PMR root
// for the Transport's internal allocations (SSL_CTX arena, handshake-time SAN
// strings). When non-null it is stored in cfg.mr (overriding the template's
// mr field) before forwarding. When null, cfg.mr is used as-is.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
make_asio_tls_transport(asio::any_io_executor      exec,
                        Transport::Config           cfg,
                        fixpp::tls::SslCtxConfig    ssl_cfg,
                        std::pmr::memory_resource*  mr) noexcept
{
    // Override cfg.mr if the call site supplies an explicit resource.
    if (mr != nullptr) {
        cfg.mr = mr;
    }

    // Wrap the throwing constructor in [2a §4.2] trap_throw. Any OS exception
    // (ENOMEM from SSL_CTX_new), std::bad_alloc, or any other std::exception
    // is caught and mapped to transport_factory_failed. This is the ONLY place
    // asio_tls_transport's constructor may be called outside of a test.
    //
    // Note: trap_throw in decimal_helpers.hpp maps any non-bad_alloc catch to
    // error::decimal_invalid_input, which is wrong for this context. We inline
    // the pattern with the correct error variant instead of reusing the
    // template directly.
    try {
        auto ptr = std::make_unique<asio_tls_transport>(
            std::move(exec), std::move(cfg), std::move(ssl_cfg));
        return std::unique_ptr<Transport>(std::move(ptr));
    } catch (std::bad_alloc const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (std::system_error const&) {
        // ASIO / OS errors during SSL_CTX or socket initialisation.
        return std::unexpected{core::error::transport_factory_failed};
    } catch (...) {
        // OpenSSL errors (e.g. SSL_CTX_new returns null → we throw) and any
        // other unexpected exceptions from cert loading.
        return std::unexpected{core::error::transport_factory_failed};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// asio_tls_transport_factory — constructor
//
// Accepts a Transport::Config template applied to every Transport minted by
// this factory. The SslCtxConfig is NOT captured at construction — it is
// passed per make() call from the session-open sequencer (which builds it
// once via make_ssl_ctx_config and holds it across reconnect attempts,
// satisfying the FR-026 caching contract from the session side).
// ─────────────────────────────────────────────────────────────────────────────
asio_tls_transport_factory::asio_tls_transport_factory(Transport::Config c) noexcept
    : cfg_{std::move(c)}
{}

// ─────────────────────────────────────────────────────────────────────────────
// asio_tls_transport_factory::make
//
// Mints a fresh asio_tls_transport per the FR-028 reconnect contract.
// Delegates to the free function make_asio_tls_transport(). The mr parameter
// overrides cfg_.mr when non-null; when null, cfg_.mr is forwarded as-is.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
asio_tls_transport_factory::make(asio::any_io_executor     exec,
                                 fixpp::tls::SslCtxConfig   ssl_cfg,
                                 std::pmr::memory_resource* mr) noexcept
{
    return make_asio_tls_transport(
        std::move(exec),
        cfg_,          // template config (mr field may be overridden inside)
        std::move(ssl_cfg),
        mr);
}

}  // namespace fixpp::transport
