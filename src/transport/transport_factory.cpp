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

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ssl/context.hpp>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <fixpp/core/decimal_helpers.hpp>   // fixpp::core::detail::trap_throw
#include <fixpp/core/error.hpp>             // core::error::transport_factory_failed
#include <fixpp/tls/cert_source.hpp>        // local_credentials + cert_source
#include <fixpp/tls/security_profile.hpp>   // SslCtxConfig
#include <fixpp/transport/transport_factory.hpp>  // asio_tls_transport_factory decl

#include <optional>
#include <stdexcept>
#include <string>

// Forward-declare the verify_peer_trampoline defined in asio_tls_transport.cpp.
// Both TUs link into the same library; the trampoline is extern "C" for the
// OpenSSL C-ABI and must be registered on the SSL_CTX in prepare_ssl_ctx_().
extern "C" int verify_peer_trampoline(int, X509_STORE_CTX*) noexcept;

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
// make_accepted_asio_tls_transport — noexcept free function
//
// US3 / FR-024 mint path for tests still wiring the legacy accept-adoption
// seam directly. Production callers use asio_tls_transport_factory::make_accepted().
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
make_accepted_asio_tls_transport(asio::any_io_executor      exec,
                                 Transport::Config          cfg,
                                 fixpp::tls::SslCtxConfig   ssl_cfg,
                                 asio::ip::tcp::socket      accepted_socket,
                                 std::pmr::memory_resource* mr) noexcept
{
    if (mr != nullptr) {
        cfg.mr = mr;
    }
    try {
        auto ptr = std::make_unique<asio_tls_transport>(
            std::move(exec), std::move(cfg), std::move(ssl_cfg), std::move(accepted_socket));
        return std::unique_ptr<Transport>(std::move(ptr));
    } catch (std::bad_alloc const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (std::system_error const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (...) {
        return std::unexpected{core::error::transport_factory_failed};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// asio_tls_transport_factory — internal constructor (from_factory_tag variant)
//
// Takes the pre-built shared SSL_CTX from make_asio_tls_transport_factory().
// Direct construction is intentionally private from external callers; use
// make_asio_tls_transport_factory() instead.
// ─────────────────────────────────────────────────────────────────────────────
asio_tls_transport_factory::asio_tls_transport_factory(shared_ctx_tag,
                                                       Transport::Config cfg,
                                                       fixpp::tls::SslCtxConfig ssl_cfg,
                                                       std::shared_ptr<void> ctx) noexcept
    : cfg_{std::move(cfg)},
      ssl_cfg_{std::move(ssl_cfg)},
      ssl_ctx_{std::move(ctx)}
{}

// ─────────────────────────────────────────────────────────────────────────────
// asio_tls_transport_factory::make
//
// Mints a FRESH asio_tls_transport per the FR-028 reconnect contract.
// Uses the cached shared SSL_CTX (FR-026): no SSL_CTX rebuild, no cert reload.
// The ssl_cfg parameter carries handshake-time fields (pinset, mr, profile,
// caps) that are NOT baked into the SSL_CTX. The mr parameter overrides
// ssl_cfg.mr when non-null.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
asio_tls_transport_factory::make(asio::any_io_executor     exec,
                                 fixpp::tls::SslCtxConfig   ssl_cfg,
                                 std::pmr::memory_resource* mr) noexcept
{
    Transport::Config cfg = cfg_;
    if (mr != nullptr) {
        cfg.mr = mr;
    }
    // Recover the typed shared_ptr from the type-erased storage.
    // The factory constructor stored an asio::ssl::context* — recover it here.
    auto typed_ctx = std::shared_ptr<asio::ssl::context>{
        ssl_ctx_, static_cast<asio::ssl::context*>(ssl_ctx_.get())};

    // Mint a fresh transport using the cached shared SSL_CTX (FR-026).
    // Each make() call returns a new asio_tls_transport instance with its own
    // socket_ and ssl_stream_, but sharing ssl_ctx_ across all instances from
    // this factory. ssl_stream_ is constructed lazily at async_handshake start.
    try {
        auto ptr = std::make_unique<asio_tls_transport>(
            asio_tls_transport::from_factory_tag{},
            std::move(exec),
            std::move(cfg),
            std::move(ssl_cfg),
            std::move(typed_ctx));  // shared_ptr — safe; asio::ssl::stream refs SSL_CTX
        return std::unique_ptr<Transport>(std::move(ptr));
    } catch (std::bad_alloc const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (std::system_error const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (...) {
        return std::unexpected{core::error::transport_factory_failed};
    }
}

[[nodiscard]] core::expected_t<std::unique_ptr<asio_tls_transport>>
asio_tls_transport_factory::make_accepted(asio::ip::tcp::socket accepted_socket,
                                          std::pmr::memory_resource* mr) noexcept
{
    Transport::Config cfg = cfg_;
    if (mr != nullptr) {
        cfg.mr = mr;
    }
    auto typed_ctx = std::shared_ptr<asio::ssl::context>{
        ssl_ctx_, static_cast<asio::ssl::context*>(ssl_ctx_.get())};
    try {
        return std::make_unique<asio_tls_transport>(
            asio_tls_transport::from_factory_tag{},
            accepted_socket.get_executor(),
            std::move(cfg),
            ssl_cfg_,
            std::move(typed_ctx),
            std::move(accepted_socket));
    } catch (std::bad_alloc const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (std::system_error const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (...) {
        return std::unexpected{core::error::transport_factory_failed};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// prepare_ssl_ctx_ — build the asio::ssl::context from SslCtxConfig.
//
// Extracted from asio_tls_transport::setup_ssl_ctx_() to run ONCE at factory
// construction. Mirrors the same steps (proto version, options, ciphers,
// curves, sigalgs, verify mode, cert install, CA trust store) but operates on
// a fresh ssl::context allocated here, returning it as a shared_ptr.
//
// Credentials are loaded synchronously via a temporary io_context (same
// pattern as the per-ctor path; now elevated to factory construction level).
//
// On failure, throws std::runtime_error (caller wraps in try/catch).
// ─────────────────────────────────────────────────────────────────────────────
static std::shared_ptr<asio::ssl::context>
prepare_ssl_ctx_(fixpp::tls::SslCtxConfig const& ssl_cfg)
{
    using namespace fixpp::tls;

    auto ctx_ptr = std::make_shared<asio::ssl::context>(asio::ssl::context::tls);
    SSL_CTX* ctx = ctx_ptr->native_handle();

    // ── Protocol version bounds ───────────────────────────────────────────────
    if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
        throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_set_min_proto_version TLS1_2 failed");
    }
    if (SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1) {
        throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_set_max_proto_version TLS1_3 failed");
    }

    // ── Options ───────────────────────────────────────────────────────────────
    constexpr long opts = SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET;
    SSL_CTX_set_options(ctx, opts);

    // ── TLS 1.3 cipher suites ─────────────────────────────────────────────────
    {
        std::string tls13_list;
        bool first = true;
        for (auto sv : CipherPolicy::tls13_suites) {
            if (!first) tls13_list += ':';
            tls13_list.append(sv.data(), sv.size());
            first = false;
        }
        if (SSL_CTX_set_ciphersuites(ctx, tls13_list.c_str()) != 1) {
            throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_set_ciphersuites failed");
        }
    }

    // ── TLS 1.2 cipher list ───────────────────────────────────────────────────
    {
        std::string tls12_list;
        bool first = true;
        for (auto sv : CipherPolicy::tls12_suites) {
            if (!first) tls12_list += ':';
            tls12_list.append(sv.data(), sv.size());
            first = false;
        }
        if (SSL_CTX_set_cipher_list(ctx, tls12_list.c_str()) != 1) {
            throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_set_cipher_list failed");
        }
    }

    // ── Key exchange groups (curves) ──────────────────────────────────────────
    {
        std::string groups_list;
        bool first = true;
        for (auto sv : CipherPolicy::kx_groups) {
            if (!first) groups_list += ':';
            groups_list.append(sv.data(), sv.size());
            first = false;
        }
        if (SSL_CTX_set1_curves_list(ctx, groups_list.c_str()) != 1) {
            throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_set1_curves_list failed");
        }
    }

    // ── Signature algorithms ──────────────────────────────────────────────────
    {
        std::string sigalg_list;
        bool first = true;
        for (auto sv : CipherPolicy::sig_algs) {
            if (!first) sigalg_list += ':';
            sigalg_list.append(sv.data(), sv.size());
            first = false;
        }
        if (SSL_CTX_set1_sigalgs_list(ctx, sigalg_list.c_str()) != 1) {
            throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_set1_sigalgs_list failed");
        }
    }

    // ── Verify mode + trampoline ──────────────────────────────────────────────
    // verify_peer_trampoline is forward-declared at file scope above (extern "C").
    SSL_CTX_set_verify(ctx,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       verify_peer_trampoline);

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);

    // ── Load credentials from cert_source (ONCE at factory construction) ──────
    if (!ssl_cfg.cs) {
        throw std::runtime_error("prepare_ssl_ctx_: null cert_source in SslCtxConfig");
    }

    std::optional<fixpp::tls::local_credentials> creds_opt;
    {
        asio::io_context tmp_io;
        std::optional<core::expected_t<fixpp::tls::local_credentials>> creds_result;
        asio::co_spawn(
            tmp_io.get_executor(),
            [&]() -> asio::awaitable<void> {
                creds_result = co_await ssl_cfg.cs->load_credentials();
            },
            asio::detached);
        tmp_io.run();
        if (!creds_result || !creds_result->has_value()) {
            throw std::runtime_error("prepare_ssl_ctx_: load_credentials failed");
        }
        creds_opt = std::move(**creds_result);
    }

    auto& creds = *creds_opt;

    // ── Install the leaf certificate ──────────────────────────────────────────
    {
        struct X509Del { void operator()(X509* x) const noexcept { X509_free(x); } };
        using X509Ptr = std::unique_ptr<X509, X509Del>;

        auto leaf_der = creds.leaf.raw_der();
        if (leaf_der.empty()) {
            throw std::runtime_error("prepare_ssl_ctx_: empty leaf cert DER");
        }
        const unsigned char* p = reinterpret_cast<const unsigned char*>(leaf_der.data());
        X509Ptr leaf_x509{d2i_X509(nullptr, &p, static_cast<long>(leaf_der.size()))};
        if (!leaf_x509) {
            throw std::runtime_error("prepare_ssl_ctx_: d2i_X509 failed for leaf cert");
        }
        if (SSL_CTX_use_certificate(ctx, leaf_x509.get()) != 1) {
            throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_use_certificate failed");
        }
    }

    // ── Install the private key ───────────────────────────────────────────────
    {
        auto* sw_key = std::get_if<fixpp::tls::software_key_ref>(&creds.signer);
        if (!sw_key) {
            throw std::runtime_error(
                "prepare_ssl_ctx_: async_signer_ref (HSM) not supported in v1.0");
        }
        EVP_PKEY* pkey = static_cast<EVP_PKEY*>(sw_key->handle.ossl_pkey);
        if (!pkey) {
            throw std::runtime_error("prepare_ssl_ctx_: null EVP_PKEY in software_key_ref");
        }
        if (SSL_CTX_use_PrivateKey(ctx, pkey) != 1) {
            throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_use_PrivateKey failed");
        }
        if (SSL_CTX_check_private_key(ctx) != 1) {
            throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_check_private_key failed");
        }
    }

    // ── Install intermediate chain certs ──────────────────────────────────────
    {
        struct X509Del { void operator()(X509* x) const noexcept { X509_free(x); } };
        using X509Ptr = std::unique_ptr<X509, X509Del>;

        for (auto const& chain_cert : creds.chain) {
            auto chain_der = chain_cert.raw_der();
            if (chain_der.empty()) continue;
            const unsigned char* p = reinterpret_cast<const unsigned char*>(chain_der.data());
            X509Ptr x509{d2i_X509(nullptr, &p, static_cast<long>(chain_der.size()))};
            if (!x509) {
                throw std::runtime_error("prepare_ssl_ctx_: d2i_X509 failed for chain cert");
            }
            if (SSL_CTX_add_extra_chain_cert(ctx, x509.get()) != 1) {
                throw std::runtime_error("prepare_ssl_ctx_: SSL_CTX_add_extra_chain_cert failed");
            }
            (void)x509.release();
        }
    }

    // ── Install trust anchors (CA certs) ──────────────────────────────────────
    {
        struct X509Del { void operator()(X509* x) const noexcept { X509_free(x); } };
        using X509Ptr = std::unique_ptr<X509, X509Del>;

        auto trust_result = ssl_cfg.cs->load_trust_anchors();
        if (trust_result) {
            X509_STORE* store = SSL_CTX_get_cert_store(ctx);
            if (store) {
                for (auto const& ca_cert : *trust_result) {
                    auto ca_der = ca_cert.raw_der();
                    if (ca_der.empty()) continue;
                    const unsigned char* p =
                        reinterpret_cast<const unsigned char*>(ca_der.data());
                    X509Ptr x509{d2i_X509(nullptr, &p, static_cast<long>(ca_der.size()))};
                    if (x509) {
                        X509_STORE_add_cert(store, x509.get());
                    }
                }
            }
        }
    }

    return ctx_ptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// make_asio_tls_transport_factory — noexcept factory function (FR-026).
//
// Builds the SSL_CTX + loads credentials ONCE. Returns an expected_t wrapping
// the factory on success, or transport_factory_failed on any error.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<std::unique_ptr<TransportFactory>>
make_asio_tls_transport_factory(Transport::Config         cfg,
                                 fixpp::tls::SslCtxConfig  ssl_cfg) noexcept
{
    try {
        auto ctx = prepare_ssl_ctx_(ssl_cfg);
        // Type-erase the context pointer to avoid exposing asio::ssl::context in
        // the public header. The factory's make() recovers it via static_cast.
        std::shared_ptr<void> ctx_void = ctx;
        auto factory = std::make_unique<asio_tls_transport_factory>(
            asio_tls_transport_factory::shared_ctx_tag{},
            std::move(cfg),
            std::move(ssl_cfg),
            std::move(ctx_void));
        return std::unique_ptr<TransportFactory>(std::move(factory));
    } catch (std::bad_alloc const&) {
        return std::unexpected{core::error::transport_factory_failed};
    } catch (...) {
        return std::unexpected{core::error::transport_factory_failed};
    }
}

}  // namespace fixpp::transport
