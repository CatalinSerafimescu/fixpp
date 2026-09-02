// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/asio_tls_transport.cpp — T027
//
// Implements fixpp::transport::asio_tls_transport: the v1.0 reference
// TlsTransport over ASIO tcp::socket + asio::ssl::stream (OpenSSL back-end).
//
// Design anchors:
//   [2h §4.5]      — state machine, fields, invariants.
//   data-model E-9  — canonical field set.
//   [arch §5.3]    — engine-bootstrap carve-out for the throwing constructor.
//   [2g §6.5.1]    — pinset capture-once BINDING CONTRACT.
//   research.md D-16 — verify_peer_trampoline ex_data pattern.
//   research.md D-17 — cancellation_type::total + co_spawn defaults.
//   research.md D-18 — no co_await asio::post(...) for executor hops.
//   [const §VIII.5] — zero allocation on read-path completion-handler dispatch.
//   [const §XI.2]  — cancellation_type::total → transport_*_cancelled variants.

#include "asio_tls_transport.hpp"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <array>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/error.hpp>
#include <asio/ssl/stream.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/pinset.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/listener_events.hpp>  // 013 T039: ListenerEvents emit
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "inflight_flag_guard.hpp"

// asio::async_connect is a free function in <asio/connect.hpp>.
// Use it via namespace alias to avoid conflict with our member function name.
namespace asio_free = asio;

namespace fixpp::transport {

// ─────────────────────────────────────────────────────────────────────────────
// file-internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// ex_data index on SSL* for the transport HandshakeCtx pointer.
// Initialized once via SSL_get_ex_new_index; thread-safe per OpenSSL docs.
int ssl_ex_data_transport_idx() noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static const int idx = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return idx;
}

// RAII wrapper for X509 objects allocated in the trampoline.
struct X509Deleter {
    void operator()(X509* x) const noexcept { X509_free(x); }
};
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

// RAII wrapper for GENERAL_NAMES.
struct GeneralNamesDeleter {
    void operator()(GENERAL_NAMES* g) const noexcept { GENERAL_NAMES_free(g); }
};
using GeneralNamesPtr = std::unique_ptr<GENERAL_NAMES, GeneralNamesDeleter>;

// Per-handshake context placed in the SSL* ex_data slot.
// Populated by async_handshake before the OpenSSL handshake begins.
struct HandshakeCtx {
    // Augmented SslCtxConfig (shallow copy of the caller's cfg with
    // pinset_snapshot filled from the captured snapshot per [2g §6.5.1]).
    fixpp::tls::SslCtxConfig augmented_cfg;

    // PMR resource for Certificate string allocations in the trampoline.
    // Must not be null (resolved from cfg_.mr or new_delete_resource).
    std::pmr::memory_resource* mr{nullptr};

    // Output: peer_identity written by the trampoline on accept.
    fixpp::tls::peer_identity peer_id;

    // Trampoline result: true iff verify_peer accepted all depths.
    bool accepted{false};

    // 013 T039 — precise tls_* error code captured by the trampoline when
    // verify_peer rejects. Populated only when accepted==false AND the rejection
    // came from verify_peer (not from OOM or other exception path).
    // async_handshake reads this to emit session_event_tls_validation_failed.
    // [FR-026 / FR-027 / D-15]
    std::optional<fixpp::core::error> verify_error;

    // 013 T039 — copy of last_handshake_sub_reason() at the moment verify_peer
    // returned. Captured here (by-value copy into std::string) so the value
    // survives past the trampoline return and across any thread-boundary before
    // async_handshake reads it. [data-model §E-5 sub_reason capture semantics]
    std::string sub_reason_copy;

    // 013 T039 — pointer to the listener's ListenerEvents ring. Set by
    // asio_tls_transport when a ListenerEvents is associated (acceptor side).
    // Null on initiator side — no emission on that path.
    // Lifetime: ListenerEvents is owned by asio_listener::Config which outlives
    // the transport. [FR-028 pre-Session event surface]
    fixpp::transport::ListenerEvents* listener_events{nullptr};
};

// Map an ASN1_TIME to a chrono::system_clock::time_point.
// On parse failure returns the epoch (not fatal; verify_peer handles expiry).
std::chrono::system_clock::time_point asn1_time_to_tp(const ASN1_TIME* t) noexcept {
    if (!t) {
        return {};
    }
    struct tm tm_val{};
    if (ASN1_TIME_to_tm(t, &tm_val) != 1) {
        return {};
    }
    // timegm is POSIX; _mkgmtime is the MSVC equivalent (both interpret UTC).
    // Mirrors src/tls/certificate.cpp.
#ifdef _WIN32
    time_t epoch_seconds = _mkgmtime(&tm_val);
#else
    time_t epoch_seconds = timegm(&tm_val);
#endif
    if (epoch_seconds < 0) {
        return {};
    }
    return std::chrono::system_clock::from_time_t(epoch_seconds);
}

// Parse one X509* into a fixpp::tls::Certificate backed by heap storage.
// Returns false on any allocation or parse failure.
// Out parameters: `der_bytes`, `subject_buf`, `issuer_buf`, `san_*_storage`
// all provide backing storage for the returned Certificate's views.
bool parse_x509_to_certificate(X509* x509, std::pmr::memory_resource& mr,
                               fixpp::tls::Certificate& cert,
                               std::pmr::vector<std::byte>& der_bytes,
                               std::pmr::string& subject_buf, std::pmr::string& issuer_buf,
                               std::pmr::vector<std::string>& san_dns_storage,
                               std::pmr::vector<std::string_view>& san_dns_views,
                               std::pmr::vector<std::string>& san_uri_storage,
                               std::pmr::vector<std::string_view>& san_uri_views) noexcept {
    // ── DER bytes ────────────────────────────────────────────────────────────
    int der_len = i2d_X509(x509, nullptr);
    if (der_len <= 0) {
        return false;
    }
    try {
        der_bytes.resize(static_cast<std::size_t>(der_len));
    } catch (...) {
        return false;
    }
    {
        unsigned char* p = reinterpret_cast<unsigned char*>(der_bytes.data());
        if (i2d_X509(x509, &p) <= 0) {
            return false;
        }
    }
    cert.raw_der_ = std::span<const std::byte>{der_bytes.data(), der_bytes.size()};

    // ── SHA-256 fingerprint ──────────────────────────────────────────────────
    {
        unsigned int md_len = 0;
        unsigned char md_buf[32]{};
        if (X509_digest(x509, EVP_sha256(), md_buf, &md_len) != 1 || md_len != 32) {
            return false;
        }
        static_assert(sizeof(cert.sha256_) == 32);
        std::memcpy(cert.sha256_.data(), md_buf, 32);
    }

    // ── Subject DN ──────────────────────────────────────────────────────────
    {
        X509_NAME* name = X509_get_subject_name(x509);
        char* oneline = X509_NAME_oneline(name, nullptr, 0);
        if (!oneline) {
            return false;
        }
        try {
            subject_buf.assign(oneline);
        } catch (...) {
            OPENSSL_free(oneline);
            return false;
        }
        OPENSSL_free(oneline);
    }
    cert.subject_dn_ = std::string_view{subject_buf.data(), subject_buf.size()};

    // ── Issuer DN ────────────────────────────────────────────────────────────
    {
        X509_NAME* issuer = X509_get_issuer_name(x509);
        char* oneline = X509_NAME_oneline(issuer, nullptr, 0);
        if (!oneline) {
            return false;
        }
        try {
            issuer_buf.assign(oneline);
        } catch (...) {
            OPENSSL_free(oneline);
            return false;
        }
        OPENSSL_free(oneline);
    }
    cert.issuer_dn_ = std::string_view{issuer_buf.data(), issuer_buf.size()};

    // ── X.509 version ────────────────────────────────────────────────────────
    // OpenSSL returns 0-based (0=v1, 1=v2, 2=v3); convert to 1-based.
    cert.x509_version_ = static_cast<int>(X509_get_version(x509)) + 1;

    // ── Validity window ──────────────────────────────────────────────────────
    cert.not_before_ = asn1_time_to_tp(X509_get0_notBefore(x509));
    cert.not_after_ = asn1_time_to_tp(X509_get0_notAfter(x509));

    // ── Signature algorithm + key info ───────────────────────────────────────
    {
        EVP_PKEY* pkey = X509_get0_pubkey(x509);
        if (pkey) {
            int pkey_id = EVP_PKEY_base_id(pkey);
            if (pkey_id == EVP_PKEY_RSA || pkey_id == EVP_PKEY_RSA_PSS) {
                cert.alg_ = fixpp::tls::signature_algorithm::rsa_pss;
                cert.rsa_key_bits_ = static_cast<std::size_t>(EVP_PKEY_bits(pkey));
            } else if (pkey_id == EVP_PKEY_EC) {
                cert.alg_ = fixpp::tls::signature_algorithm::ecdsa;
                // OpenSSL 3.x: use EVP_PKEY_get_group_name rather than deprecated EC_KEY API.
                char gname[64]{};
                if (EVP_PKEY_get_group_name(pkey, gname, sizeof(gname), nullptr)) {
                    if (std::string_view{gname} == "prime256v1" ||
                        std::string_view{gname} == "P-256") {
                        cert.curve_ = fixpp::tls::ecdsa_curve::p256;
                    } else if (std::string_view{gname} == "secp384r1" ||
                               std::string_view{gname} == "P-384") {
                        cert.curve_ = fixpp::tls::ecdsa_curve::p384;
                    }
                }
            }
        }
    }

    // ── SAN entries ──────────────────────────────────────────────────────────
    {
        GeneralNamesPtr sans{static_cast<GENERAL_NAMES*>(
            X509_get_ext_d2i(x509, NID_subject_alt_name, nullptr, nullptr))};
        if (sans) {
            int n = sk_GENERAL_NAME_num(sans.get());
            try {
                for (int i = 0; i < n; ++i) {
                    GENERAL_NAME* gn = sk_GENERAL_NAME_value(sans.get(), i);
                    if (!gn) {
                        continue;
                    }
                    if (gn->type == GEN_DNS) {
                        const unsigned char* data = ASN1_STRING_get0_data(gn->d.ia5);
                        int len = ASN1_STRING_length(gn->d.ia5);
                        if (data && len > 0) {
                            san_dns_storage.emplace_back(reinterpret_cast<const char*>(data),
                                                         static_cast<std::size_t>(len));
                        }
                    } else if (gn->type == GEN_URI) {
                        const unsigned char* data =
                            ASN1_STRING_get0_data(gn->d.uniformResourceIdentifier);
                        int len = ASN1_STRING_length(gn->d.uniformResourceIdentifier);
                        if (data && len > 0) {
                            san_uri_storage.emplace_back(reinterpret_cast<const char*>(data),
                                                         static_cast<std::size_t>(len));
                        }
                    }
                }
            } catch (...) {
                return false;
            }
        }
    }

    // Build the string_view spans from the owned strings.
    try {
        san_dns_views.reserve(san_dns_storage.size());
        for (auto const& s : san_dns_storage) {
            san_dns_views.emplace_back(s.data(), s.size());
        }
        san_uri_views.reserve(san_uri_storage.size());
        for (auto const& s : san_uri_storage) {
            san_uri_views.emplace_back(s.data(), s.size());
        }
    } catch (...) {
        return false;
    }

    cert.san_dns_names_ =
        std::span<const std::string_view>{san_dns_views.data(), san_dns_views.size()};
    cert.san_uris_ = std::span<const std::string_view>{san_uri_views.data(), san_uri_views.size()};

    (void)mr;  // PMR is used implicitly via the pmr containers passed in
    return true;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// verify_peer_trampoline — SSL_VERIFY_PEER callback (C-linkage, file-internal)
//
// Called by OpenSSL for each certificate at depth in the peer chain.
// Per D-16 + Appendix D §D.7 (data-model.md §E-14):
//   1. Extract HandshakeCtx* from SSL* ex_data (set before handshake).
//   2. At depth 0 only: parse the full chain + leaf into Certificate views.
//   3. Invoke verify_peer(augmented_cfg, peer_chain).
//   4. Store the resulting peer_identity on accept; return 1/0 to OpenSSL.
//
// At chain depths > 0 we return 1 unconditionally (defer to OpenSSL's
// built-in chain validation against the CA trust store we loaded at ctor
// time).  The authoritative policy is applied at depth 0 where the leaf
// + full chain is available.
// ─────────────────────────────────────────────────────────────────────────────
extern "C" {
// verify_peer_trampoline — NOT static: transport_factory.cpp registers it on
// the factory-built SSL_CTX via a forward declaration. The extern "C" linkage
// ensures the function pointer is compatible with the OpenSSL C-ABI.
int verify_peer_trampoline(int /*preverify_ok*/, X509_STORE_CTX* store_ctx) noexcept {
    // RC#C (P1-3): wrap entire body in try/catch(bad_alloc) so that any heap or
    // PMR exhaustion during cert parsing or peer_identity construction does NOT
    // escape the noexcept boundary (which would terminate the process via
    // std::terminate rather than surface transport_handshake_failed).
    //
    // On OOM: reject the peer (return 0) and mark hctx->accepted = false so
    // async_handshake maps to transport_handshake_failed at line 952-955.
    //
    // The noexcept declaration is load-bearing for the OpenSSL C-ABI: OpenSSL
    // calls this as a C function pointer; a throw across the extern "C"
    // boundary is undefined behaviour per [arch §5.3] / C++23 rules.
    //
    // Design anchor: spec.md FR-035; .specify/2h-transport.md §1 item 5.
    try {
        // Retrieve SSL* from the store context.
        SSL* ssl = static_cast<SSL*>(
            X509_STORE_CTX_get_ex_data(store_ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
        if (!ssl) {
            return 0;
        }

        // Retrieve the HandshakeCtx.
        HandshakeCtx* hctx =
            static_cast<HandshakeCtx*>(SSL_get_ex_data(ssl, ssl_ex_data_transport_idx()));
        if (!hctx) {
            return 0;
        }

        // Only run the full policy at depth 0 (the peer leaf).
        // At deeper depths, pass — chain validation is handled by OpenSSL.
        int depth = X509_STORE_CTX_get_error_depth(store_ctx);
        if (depth != 0) {
            return 1;
        }

        // ── Build the peer chain array ────────────────────────────────────────────
        // The verified chain from OpenSSL contains [leaf, intermediates, root].
        STACK_OF(X509)* x509_chain = X509_STORE_CTX_get0_chain(store_ctx);
        int raw_chain_len = x509_chain ? sk_X509_num(x509_chain) : 0;
        if (raw_chain_len <= 0) {
            // Fallback: just the current cert.
            X509* leaf = X509_STORE_CTX_get_current_cert(store_ctx);
            if (!leaf) {
                return 0;
            }
            raw_chain_len = 1;
            // We'll handle this inline below.
        }

        // Cap at 16 to avoid stack overflows.
        static constexpr int kMaxChainLen = 16;
        int chain_len = std::min(raw_chain_len, kMaxChainLen);

        // Backing storage for each certificate's fields.
        // Using system allocator here (not PMR) since the trampoline builds
        // temporary views — per [const §VIII.5] the HOT path is async_read_some;
        // the handshake is a cold path.
        struct CertBacking {
            std::vector<std::byte> der_bytes;
            std::string subject_buf;
            std::string issuer_buf;
            std::vector<std::string> san_dns_storage;
            std::vector<std::string_view> san_dns_views;
            std::vector<std::string> san_uri_storage;
            std::vector<std::string_view> san_uri_views;
        };

        // Use PMR-based intermediates fed by hctx->mr for the strings passed to
        // parse_x509_to_certificate (which writes into pmr containers).
        // Declare with system allocator as the out-params; they write into
        // system-heap std::strings and std::vectors.
        // NOTE: verify_peer itself will allocate peer_identity into hctx->mr;
        //       Certificate views are temporary locals here.
        std::array<CertBacking, kMaxChainLen> backings{};
        std::array<fixpp::tls::Certificate, kMaxChainLen> certs{};

        // Adapt pmr-based function to system-allocator storage.
        // We declare wrapper pmr containers backed by new_delete_resource and
        // pass them to parse_x509_to_certificate.
        auto* sys_mr = std::pmr::new_delete_resource();

        int built = 0;
        for (int i = 0; i < chain_len; ++i) {
            X509* x509 = nullptr;
            if (x509_chain && raw_chain_len > 1) {
                x509 = sk_X509_value(x509_chain, i);
            } else if (i == 0) {
                x509 = X509_STORE_CTX_get_current_cert(store_ctx);
            }
            if (!x509) {
                continue;
            }

            // Temporary pmr containers backed by sys_mr (new_delete_resource).
            std::pmr::vector<std::byte> der_pmr{sys_mr};
            std::pmr::string subject_pmr{sys_mr};
            std::pmr::string issuer_pmr{sys_mr};
            std::pmr::vector<std::string> san_dns_pmr{sys_mr};
            std::pmr::vector<std::string_view> san_dns_views_pmr{sys_mr};
            std::pmr::vector<std::string> san_uri_pmr{sys_mr};
            std::pmr::vector<std::string_view> san_uri_views_pmr{sys_mr};

            certs[i] = fixpp::tls::Certificate{};
            if (!parse_x509_to_certificate(x509, *sys_mr, certs[i], der_pmr, subject_pmr,
                                           issuer_pmr, san_dns_pmr, san_dns_views_pmr, san_uri_pmr,
                                           san_uri_views_pmr)) {
                return 0;
            }

            // Transfer backing data into our persistent CertBacking for this slot.
            // The Certificate views point into `backings[i]`, which outlives `certs`.
            backings[i].der_bytes.assign(der_pmr.begin(), der_pmr.end());
            backings[i].subject_buf.assign(subject_pmr.begin(), subject_pmr.end());
            backings[i].issuer_buf.assign(issuer_pmr.begin(), issuer_pmr.end());
            backings[i].san_dns_storage.assign(san_dns_pmr.begin(), san_dns_pmr.end());
            backings[i].san_uri_storage.assign(san_uri_pmr.begin(), san_uri_pmr.end());

            // Rebuild the Certificate views to point into backings[i].
            certs[i].raw_der_ = std::span<const std::byte>{backings[i].der_bytes.data(),
                                                           backings[i].der_bytes.size()};
            certs[i].subject_dn_ = std::string_view{backings[i].subject_buf};
            certs[i].issuer_dn_ = std::string_view{backings[i].issuer_buf};

            // Rebuild SAN views.
            backings[i].san_dns_views.clear();
            for (auto const& s : backings[i].san_dns_storage) {
                backings[i].san_dns_views.emplace_back(s.data(), s.size());
            }
            backings[i].san_uri_views.clear();
            for (auto const& s : backings[i].san_uri_storage) {
                backings[i].san_uri_views.emplace_back(s.data(), s.size());
            }
            certs[i].san_dns_names_ = std::span<const std::string_view>{
                backings[i].san_dns_views.data(), backings[i].san_dns_views.size()};
            certs[i].san_uris_ = std::span<const std::string_view>{
                backings[i].san_uri_views.data(), backings[i].san_uri_views.size()};

            ++built;
        }

        if (built == 0) {
            return 0;
        }

        std::span<const fixpp::tls::Certificate> peer_chain{certs.data(),
                                                            static_cast<std::size_t>(built)};

        // ── Invoke verify_peer against the augmented config ────────────────────────
        // augmented_cfg.pinset_snapshot was set at handshake start (captured-once).
        // verify_peer reads pinset_snapshot; it NEVER calls cfg.pinset->find/snapshot.
        auto result = fixpp::tls::verify_peer(hctx->augmented_cfg, peer_chain);
        if (!result) {
            hctx->accepted = false;
            // 013 T039: capture the precise tls_* error code + sub_reason BY COPY.
            // async_handshake reads these AFTER the handshake completes to emit
            // session_event_tls_validation_failed. The copy happens here inside the
            // trampoline (same thread); the values are then stable for the
            // async_handshake resume. [FR-027 / data-model §E-5]
            hctx->verify_error = result.error();
            // Copy last_handshake_sub_reason() into a std::string (not a view) so
            // the value survives the thread-local being reset by any subsequent call.
            // String literal storage is static, but defensive copy is required per
            // data-model §E-5 sub_reason capture semantics.
            hctx->sub_reason_copy = std::string{fixpp::tls::last_handshake_sub_reason()};
            return 0;
        }

        hctx->peer_id = std::move(*result);
        hctx->accepted = true;
        return 1;

    } catch (const std::bad_alloc&) {
        // OOM during cert parsing or peer_identity construction: reject the peer.
        // hctx may or may not be valid here (OOM could occur before hctx retrieval),
        // but the recoverable path is to return 0 (reject) so OpenSSL surfaces a
        // handshake failure. If hctx is reachable, mark it explicitly rejected.
        //
        // We cannot safely call SSL_get_ex_data again here (it might allocate
        // depending on implementation) so we use the simplest constant-time path:
        // return 0 to signal OpenSSL "chain verification failed". The async_handshake
        // wrapper will then map the resulting handshake error to transport_handshake_failed.
        return 0;
    } catch (...) {
        // Any other exception (logic_error, etc.) — same treatment: reject.
        return 0;
    }
}
}  // extern "C"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor (throwing — [arch §5.3] engine-bootstrap carve-out)
//
// Builds its own asio::ssl::context from ssl_cfg_ fields. Used when no
// pre-built context is available (tests, standalone usage, legacy callers).
// For production use via the factory, use the from_factory_tag ctor instead.
// ─────────────────────────────────────────────────────────────────────────────
asio_tls_transport::asio_tls_transport(asio::any_io_executor exec, Transport::Config cfg,
                                       fixpp::tls::SslCtxConfig ssl_cfg)
    : cfg_{cfg},
      ssl_cfg_{std::move(ssl_cfg)},
      exec_{exec},
      socket_{exec_},
      ssl_ctx_{std::make_shared<asio::ssl::context>(asio::ssl::context::tls)} {
    setup_ssl_ctx_();
    // state_ default-initialized to fresh per asio_tls_transport.hpp.
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory-path constructor (FR-026 shared-context path — initiator).
//
// Takes the pre-built shared context from asio_tls_transport_factory::make().
// Does NOT call setup_ssl_ctx_() — the context is already configured.
// ─────────────────────────────────────────────────────────────────────────────
asio_tls_transport::asio_tls_transport(from_factory_tag, asio::any_io_executor exec,
                                       Transport::Config cfg, fixpp::tls::SslCtxConfig ssl_cfg,
                                       std::shared_ptr<asio::ssl::context> shared_ctx)
    : cfg_{cfg},
      ssl_cfg_{std::move(ssl_cfg)},
      exec_{exec},
      socket_{exec_},
      ssl_ctx_{std::move(shared_ctx)} {
    // ssl_ctx_ already configured by the factory; no setup_ssl_ctx_() call.
    // state_ default-initialized to fresh; role_ default-initialized to client.
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory-path accept-adoption constructor (FR-026 + US3 combined).
//
// Adopts an already-connected TCP socket + uses the factory's shared context.
// Sets state_ = connected + role_ = server (TLS server mode).
// ─────────────────────────────────────────────────────────────────────────────
asio_tls_transport::asio_tls_transport(from_factory_tag, asio::any_io_executor exec,
                                       Transport::Config cfg, fixpp::tls::SslCtxConfig ssl_cfg,
                                       std::shared_ptr<asio::ssl::context> shared_ctx,
                                       asio::ip::tcp::socket accepted_socket)
    : cfg_{cfg},
      ssl_cfg_{std::move(ssl_cfg)},
      exec_{exec},
      socket_{std::move(accepted_socket)},
      ssl_ctx_{std::move(shared_ctx)},
      role_(role_t::server),
      state_(state_t::connected) {
    apply_socket_options_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Accept-adoption constructor (US3 — asio_listener mint path per FR-024).
//
// Identical SSL_CTX setup to the initiator-side ctor; differs only in:
//   - socket_ is move-constructed from the already-connected accepted_socket
//     (preserves the kernel-side TCP state established by accept(2)).
//   - state_ is promoted to state_t::connected — async_connect is therefore
//     spent. The FSM calls async_handshake to drive the TLS handshake on the
//     server-mode SSL context.
//
// Throws on OpenSSL SSL_CTX setup failure per the [arch §5.3] engine-bootstrap
// carve-out; callers wrap in [2a §4.2] trap_throw.
// ─────────────────────────────────────────────────────────────────────────────
asio_tls_transport::asio_tls_transport(asio::any_io_executor exec, Transport::Config cfg,
                                       fixpp::tls::SslCtxConfig ssl_cfg,
                                       asio::ip::tcp::socket accepted_socket)
    : cfg_{cfg},
      ssl_cfg_{std::move(ssl_cfg)},
      exec_{exec},
      socket_{std::move(accepted_socket)},
      ssl_ctx_{std::make_shared<asio::ssl::context>(asio::ssl::context::tls)},
      role_(role_t::server),
      state_(state_t::connected) {
    setup_ssl_ctx_();
    apply_socket_options_();
}

// ─────────────────────────────────────────────────────────────────────────────
// ~asio_tls_transport — retires every armed timer epoch (D-4.1 item 3).
//
// This is a destructor-BODY statement, not a retiring member: members
// (including timer_epochs_ itself) are destroyed AFTER this body runs, so
// the retirement is sequenced strictly before socket_/ssl_stream_'s
// destruction. A stranded connect- or handshake-timeout handler that
// observes the retired epoch is therefore guaranteed `this` is still alive
// when it decided not to touch it, and guaranteed dead when it did.
//
// Covers the destroy-with-no-drain leg (D-4.0): reconnect_fsm.cpp and
// engine.cpp's accept loop both destroy this transport synchronously on the
// failure arm, with no wait for an in-flight timer handler to run first.
// The in-function retire-before-cancel at each arm site (below) does NOT
// cover a coroutine frame destroyed mid-co_await without resuming — this
// destructor is the only thing that does.
// ─────────────────────────────────────────────────────────────────────────────
asio_tls_transport::~asio_tls_transport() {
    ++timer_epochs_->connect;
    ++timer_epochs_->handshake;
    ++timer_epochs_->close;
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_socket_options_ — shared FR-029 / FR-029a socket-option application.
// Initiator: called by async_connect after the kernel 3-way handshake
// completes. Acceptor: called by the accept-adoption ctor immediately after
// the SSL_CTX is up. Best-effort — opt_ec from setsockopt is intentionally
// dropped (mirrors the initiator-side semantics; tests assert post-conditions
// via get_option in tests/perf/test_socket_option_defaults.cpp).
// ─────────────────────────────────────────────────────────────────────────────
void asio_tls_transport::apply_socket_options_() noexcept {
    asio::error_code opt_ec;

    asio::ip::tcp::no_delay no_delay_opt{cfg_.tcp_nodelay};
    socket_.set_option(no_delay_opt, opt_ec);

    if (!cfg_.so_linger_enabled) {
        asio::socket_base::linger linger_opt{false, 0};
        socket_.set_option(linger_opt, opt_ec);
    } else {
        asio::socket_base::linger linger_opt{true, cfg_.so_linger_seconds};
        socket_.set_option(linger_opt, opt_ec);
    }

    if (cfg_.tcp_recv_buf_bytes > 0) {
        asio::socket_base::receive_buffer_size recv_buf{cfg_.tcp_recv_buf_bytes};
        socket_.set_option(recv_buf, opt_ec);
    }

    if (cfg_.tcp_send_buf_bytes > 0) {
        asio::socket_base::send_buffer_size send_buf{cfg_.tcp_send_buf_bytes};
        socket_.set_option(send_buf, opt_ec);
    }

    if (cfg_.tcp_keepalive) {
        asio::socket_base::keep_alive keepalive_opt{true};
        socket_.set_option(keepalive_opt, opt_ec);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// setup_ssl_ctx_ — shared SSL_CTX bring-up. Reads ssl_cfg_, mutates
// *ssl_ctx_. Both ctors call this after their member-init lists complete.
// Anything throwing here propagates out of the ctor and is caught by the
// caller's trap_throw envelope.
// ─────────────────────────────────────────────────────────────────────────────
void asio_tls_transport::setup_ssl_ctx_() {
    // Force registration of the ex_data index before first use.
    (void)ssl_ex_data_transport_idx();

    SSL_CTX* ctx = ssl_ctx_->native_handle();

    // ── Protocol version bounds per [const §XII.2] ───────────────────────────
    if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
        throw std::runtime_error("SSL_CTX_set_min_proto_version TLS1_2 failed");
    }
    if (SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1) {
        throw std::runtime_error("SSL_CTX_set_max_proto_version TLS1_3 failed");
    }

    // ── Options per [FIXS §3.2] + [const §XII.3] (FR-016) ───────────────────
    // Disable renegotiation, compression, and session tickets.
    // Note: SSL_OP_NO_EARLY_DATA is deprecated in OpenSSL 3.x; TLS 1.3
    // 0-RTT is disabled by not calling SSL_CTX_set_early_data_enabled (it
    // is off by default in ASIO's context).
    constexpr long opts = SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET;
    SSL_CTX_set_options(ctx, opts);

    // ── TLS 1.3 cipher suites ─────────────────────────────────────────────────
    {
        std::string tls13_list;
        bool first = true;
        for (auto sv : fixpp::tls::CipherPolicy::tls13_suites) {
            if (!first) {
                tls13_list += ':';
            }
            tls13_list.append(sv.data(), sv.size());
            first = false;
        }
        if (SSL_CTX_set_ciphersuites(ctx, tls13_list.c_str()) != 1) {
            throw std::runtime_error("SSL_CTX_set_ciphersuites failed");
        }
    }

    // ── TLS 1.2 cipher list ───────────────────────────────────────────────────
    {
        std::string tls12_list;
        bool first = true;
        for (auto sv : fixpp::tls::CipherPolicy::tls12_suites) {
            if (!first) {
                tls12_list += ':';
            }
            tls12_list.append(sv.data(), sv.size());
            first = false;
        }
        if (SSL_CTX_set_cipher_list(ctx, tls12_list.c_str()) != 1) {
            throw std::runtime_error("SSL_CTX_set_cipher_list failed");
        }
    }

    // ── Key exchange groups (curves) ──────────────────────────────────────────
    {
        std::string groups_list;
        bool first = true;
        for (auto sv : fixpp::tls::CipherPolicy::kx_groups) {
            if (!first) {
                groups_list += ':';
            }
            groups_list.append(sv.data(), sv.size());
            first = false;
        }
        if (SSL_CTX_set1_curves_list(ctx, groups_list.c_str()) != 1) {
            throw std::runtime_error("SSL_CTX_set1_curves_list failed");
        }
    }

    // ── Signature algorithms ──────────────────────────────────────────────────
    {
        std::string sigalg_list;
        bool first = true;
        for (auto sv : fixpp::tls::CipherPolicy::sig_algs) {
            if (!first) {
                sigalg_list += ':';
            }
            sigalg_list.append(sv.data(), sv.size());
            first = false;
        }
        if (SSL_CTX_set1_sigalgs_list(ctx, sigalg_list.c_str()) != 1) {
            throw std::runtime_error("SSL_CTX_set1_sigalgs_list failed");
        }
    }

    // ── Verify mode + trampoline ──────────────────────────────────────────────
    // All profiles require a peer cert (mtls_* and one_way_ca all require TLS
    // with peer verification per [2g §4.5.1]).  The trampoline dispatches into
    // verify_peer for the actual policy; OpenSSL's depth-0 callback at depth 0
    // returns the final accept/reject.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       verify_peer_trampoline);

    // ── Disable session caching (per-session ephemeral connections) ───────────
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);

    // ── Load credentials from cert_source ─────────────────────────────────────
    // The constructor may be called at session-open time inside the factory's
    // trap_throw boundary ([arch §5.3] engine-bootstrap carve-out).  We drive
    // load_credentials() synchronously via a temporary io_context.
    if (!ssl_cfg_.cs) {
        throw std::runtime_error("asio_tls_transport: null cert_source in SslCtxConfig");
    }

    std::optional<fixpp::tls::local_credentials> creds_opt;
    {
        asio::io_context tmp_io;
        std::optional<core::expected_t<fixpp::tls::local_credentials>> creds_result;

        asio::co_spawn(
            tmp_io.get_executor(),
            [&]() -> asio::awaitable<void> {
                creds_result = co_await ssl_cfg_.cs->load_credentials();
            },
            asio::detached);

        tmp_io.run();

        if (!creds_result || !creds_result->has_value()) {
            throw std::runtime_error("asio_tls_transport: load_credentials failed");
        }
        creds_opt = std::move(**creds_result);
    }

    auto& creds = *creds_opt;

    // ── Install the leaf certificate ──────────────────────────────────────────
    {
        auto leaf_der = creds.leaf.raw_der();
        if (leaf_der.empty()) {
            throw std::runtime_error("asio_tls_transport: empty leaf cert DER");
        }
        const unsigned char* p = reinterpret_cast<const unsigned char*>(leaf_der.data());
        X509Ptr leaf_x509{d2i_X509(nullptr, &p, static_cast<long>(leaf_der.size()))};
        if (!leaf_x509) {
            throw std::runtime_error("asio_tls_transport: d2i_X509 failed for leaf cert");
        }
        if (SSL_CTX_use_certificate(ctx, leaf_x509.get()) != 1) {
            throw std::runtime_error("asio_tls_transport: SSL_CTX_use_certificate failed");
        }
    }

    // ── Install the private key ───────────────────────────────────────────────
    {
        auto* sw_key = std::get_if<fixpp::tls::software_key_ref>(&creds.signer);
        if (!sw_key) {
            throw std::runtime_error(
                "asio_tls_transport: async_signer_ref (HSM) not supported in v1.0");
        }
        EVP_PKEY* pkey = static_cast<EVP_PKEY*>(sw_key->handle.ossl_pkey);
        if (!pkey) {
            throw std::runtime_error("asio_tls_transport: null EVP_PKEY in software_key_ref");
        }
        if (SSL_CTX_use_PrivateKey(ctx, pkey) != 1) {
            throw std::runtime_error("asio_tls_transport: SSL_CTX_use_PrivateKey failed");
        }
        if (SSL_CTX_check_private_key(ctx) != 1) {
            throw std::runtime_error("asio_tls_transport: SSL_CTX_check_private_key failed");
        }
    }

    // ── Install intermediate chain certs ──────────────────────────────────────
    for (auto const& chain_cert : creds.chain) {
        auto chain_der = chain_cert.raw_der();
        if (chain_der.empty()) {
            continue;
        }
        const unsigned char* p = reinterpret_cast<const unsigned char*>(chain_der.data());
        X509Ptr x509{d2i_X509(nullptr, &p, static_cast<long>(chain_der.size()))};
        if (!x509) {
            throw std::runtime_error("asio_tls_transport: d2i_X509 failed for chain cert");
        }
        // SSL_CTX_add_extra_chain_cert takes ownership — release from unique_ptr.
        if (SSL_CTX_add_extra_chain_cert(ctx, x509.get()) != 1) {
            throw std::runtime_error("asio_tls_transport: SSL_CTX_add_extra_chain_cert failed");
        }
        (void)x509.release();
    }

    // ── Install trust anchors (CA certs) ──────────────────────────────────────
    {
        auto trust_result = ssl_cfg_.cs->load_trust_anchors();
        if (trust_result) {
            X509_STORE* store = SSL_CTX_get_cert_store(ctx);
            if (store) {
                for (auto const& ca_cert : *trust_result) {
                    auto ca_der = ca_cert.raw_der();
                    if (ca_der.empty()) {
                        continue;
                    }
                    const unsigned char* p = reinterpret_cast<const unsigned char*>(ca_der.data());
                    X509Ptr x509{d2i_X509(nullptr, &p, static_cast<long>(ca_der.size()))};
                    if (x509) {
                        // X509_STORE_add_cert increments refcount; we still own x509.
                        X509_STORE_add_cert(store, x509.get());
                    }
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// async_connect
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>> asio_tls_transport::async_connect(
    Endpoint const& ep) {
    using E = core::error;

    // Enable total cancellation (co_spawn defaults to terminal-only per D-17).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // FR-006 is unconditional: after close() returns, EVERY async_* answers
    // transport_already_closed. This precedes the one-shot guard below, which
    // would otherwise collapse `closed` into transport_already_connected (#339).
    if (state_ == state_t::closed) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // One-shot guard.
    if (state_ != state_t::fresh) {
        co_return std::unexpected{E::transport_already_connected};
    }

    // #342 overlap guard: the one-shot test above is a STATE test, and state_
    // does not leave `fresh` until an attempt SUCCEEDS -- so without this a
    // second async_connect issued while the first is still in flight passed
    // straight through and really attempted. asio's composed async_connect
    // calls socket_.close(ec) before each endpoint attempt, so the two
    // attempts corrupt each other (the first can surface as operation_aborted
    // -> transport_connect_timeout, and the shared connect epoch is advanced
    // by whichever finishes first). Overlap is now REFUSED with the variant
    // every contract site already published for it.
    // WHY 97 and not a new transport_connect_in_progress sibling of the 99/100
    // pair the read/write guards use: 97 is what every published contract site
    // ALREADY named for this case, so the guard makes the contract true instead
    // of rewriting it — and a new variant could not sit in the family anyway,
    // which is pinned contiguous at 94..115 (FR-034 / T006) while error.hpp
    // already runs past 115.
    if (timer_epochs_->connect_in_flight) {
        co_return std::unexpected{E::transport_already_connected};
    }
    // Cleared on EVERY exit path, including frame destruction under
    // cancellation. A failed attempt leaves state_ == fresh AND clears this,
    // so the Transport stays retryable per FR-007.
    detail::inflight_flag_guard connect_guard{timer_epochs_,
                                             &timer_epoch_state::connect_in_flight};

    // #341: no pre-connect cancellation reap here, deliberately -- it would be
    // dead. See the CANCELLATION TIMING note on Transport in transport.hpp
    // for the mechanism and the re-derivation recipe.

    // ── Resolve ───────────────────────────────────────────────────────────────
    asio::ip::tcp::resolver resolver{exec_};
    asio::error_code resolve_ec;
    auto endpoints = co_await resolver.async_resolve(
        ep.host, std::to_string(ep.port), asio::redirect_error(asio::use_awaitable, resolve_ec));

    if (resolve_ec) {
        if (resolve_ec == asio::error::operation_aborted) {
            co_return std::unexpected{E::transport_connect_cancelled};
        }
        co_return std::unexpected{E::transport_resolve_failed};
    }

    // Post-resolve cancellation reap.
    auto cs = co_await asio::this_coro::cancellation_state;
    if (cs.cancelled() != asio::cancellation_type::none) {
        co_return std::unexpected{E::transport_connect_cancelled};
    }

    // #347: close() runs on this strand and can have executed while we were
    // suspended in async_resolve -- which the RESOLVER never observes, because
    // socket_.close() does not cancel it. FR-006 is unconditional, so re-test
    // it here instead of proceeding to open a socket the owner already closed.
    if (state_ == state_t::closed) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // ── Connect with timeout ──────────────────────────────────────────────────
    // Arm a timer that cancels the socket on expiry.
    asio::steady_timer timer{exec_};
    timer.expires_after(cfg_.connect_timeout);
    // Timer-epoch guard (D-4.1, FR-014): the handler captures a COPY of
    // timer_epochs_, never `this` — nothing here touches `this` until the
    // guard has passed, so a handler stranded by a destroy-with-no-drain
    // (D-4.0) safely no-ops instead of reading/calling through dead memory.
    const std::uint64_t connect_epoch = ++timer_epochs_->connect;
    timer.async_wait([this, epochs = timer_epochs_, connect_epoch](asio::error_code ec) {
        if (ec || connect_epoch != epochs->connect) {
            return;  // cancelled, or this attempt's epoch was retired — no-op.
        }
        // Timeout: cancel in-flight socket operations.
        asio::error_code ignored;
        socket_.cancel(ignored);
    });

    // 016 T008 — make the in-flight connect promptly abortable by Engine::stop()'s
    // cancellation_type::total (the 015 down-peer L2 carry-forward). The range
    // async_connect honors only `terminal` cancellation; stop() emits `total`,
    // which the op would otherwise ignore — so stop() blocked until connect_timeout
    // ran to completion (a mid-connect transport is not yet live_transport, so
    // stop()'s socket-close step can't reach it either). Install an OUT filter on
    // this coroutine's cancellation state that maps any accepted cancellation to
    // `terminal` for the forwarded child op, so stop()'s total promptly aborts the
    // connect. (Slot-level assignment is unsafe here — async_connect is also driven
    // from contexts whose coroutine has no connected cancellation slot.)
    // [[feedback_asio_cospawn_total_cancellation_default]];
    // [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]].
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation(), [](asio::cancellation_type ct) {
            return ct == asio::cancellation_type::none ? ct : asio::cancellation_type::terminal;
        });

    asio::error_code connect_ec;
    // Use the free function asio::async_connect via an explicit namespace
    // to avoid the ambiguity with our member method name.
    auto connected_ep = co_await asio_free::async_connect(
        socket_, endpoints, asio::redirect_error(asio::use_awaitable, connect_ec));

    // Retire this attempt's epoch BEFORE cancel() — a same-strand handler
    // still queued after cancel() sees the retirement and no-ops instead of
    // touching `this` (D-4.1).
    ++timer_epochs_->connect;
    timer.cancel();

    if (connect_ec) {
        if (connect_ec == asio::error::operation_aborted) {
            // Check whether it was a user cancel or a timeout.
            cs = co_await asio::this_coro::cancellation_state;
            if (cs.cancelled() != asio::cancellation_type::none) {
                co_return std::unexpected{E::transport_connect_cancelled};
            }
            co_return std::unexpected{E::transport_connect_timeout};
        }
        if (connect_ec == asio::error::connection_refused) {
            co_return std::unexpected{E::transport_connect_refused};
        }
        co_return std::unexpected{E::transport_connect_refused};
    }

    // ── Apply socket options post-connect (FR-029 / FR-029a) ─────────────────
    apply_socket_options_();

    // ── Populate ConnectInfo ──────────────────────────────────────────────────
    ConnectInfo info;
    {
        info.remote.host = connected_ep.address().to_string();
        info.remote.port = connected_ep.port();
        info.family = connected_ep.address().is_v4() ? AF_INET : AF_INET6;

        asio::error_code local_ec;
        auto local_ep = socket_.local_endpoint(local_ec);
        if (!local_ec) {
            info.local.host = local_ep.address().to_string();
            info.local.port = local_ep.port();
        }
    }

    // #347: last re-test before committing the state. asio's composed
    // async_connect OPENS the socket it connects, so a close() that landed
    // during the attempt has already been undone by the time we get here --
    // close the socket again rather than publishing `connected` and leaving a
    // live socket behind a close() that already returned.
    if (state_ == state_t::closed) {
        asio::error_code close_ec;
        socket_.close(close_ec);
        co_return std::unexpected{E::transport_already_closed};
    }

    state_ = state_t::connected;
    co_return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// async_handshake
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] asio::awaitable<core::expected_t<handshake_result>>
asio_tls_transport::async_handshake(fixpp::tls::SslCtxConfig const& cfg) {
    using E = core::error;

    // Enable total cancellation (D-17).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // FR-006 is unconditional: after close() returns, EVERY async_* answers
    // transport_already_closed. This precedes the one-shot guard below, which
    // would otherwise collapse `closed` into transport_already_connected (#339).
    if (state_ == state_t::closed) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // One-shot guard: only valid from the connected state.
    if (state_ != state_t::connected) {
        co_return std::unexpected{E::transport_already_connected};
    }

    // #342 overlap guard: see async_connect. state_ stays `connected` for the
    // whole duration of an in-flight handshake, so the one-shot test above
    // cannot refuse an OVERLAPPING second async_handshake; this does.
    // 97 rather than a new *_in_progress variant — see async_connect above.
    if (timer_epochs_->handshake_in_flight) {
        co_return std::unexpected{E::transport_already_connected};
    }
    // Cleared on every exit path, including frame destruction under
    // cancellation. ⚠️ That makes the Transport IDLE again; it does NOT make it
    // retryable. Only the preflight returns above leave state_ == connected --
    // every handshake that enters the OpenSSL exchange sets state_ = closed, so
    // a retry after one answers transport_already_closed, not a real attempt.
    detail::inflight_flag_guard handshake_guard{timer_epochs_,
                                               &timer_epoch_state::handshake_in_flight};

    // #341: no pre-handshake cancellation reap here, deliberately -- it would be
    // dead. See the CANCELLATION TIMING note on Transport in transport.hpp
    // for the mechanism and the re-derivation recipe.

    // Reject PSK config (FR-017).
    if (cfg.profile == fixpp::tls::SecurityProfile::unset) {
        co_return std::unexpected{E::transport_psk_unsupported};
    }

    // ── Capture the pinset snapshot ONCE (per [2g §6.5.1] BINDING CONTRACT) ──
    // BEFORE the OpenSSL handshake protocol exchange begins.
    captured_pinset_ = nullptr;
    if (cfg.pinset) {
        captured_pinset_ = cfg.pinset->snapshot();
    }

    // ── Build the augmented SslCtxConfig for the trampoline ───────────────────
    HandshakeCtx hctx;
    hctx.augmented_cfg = cfg;
    hctx.augmented_cfg.pinset_snapshot = captured_pinset_;
    hctx.mr = (cfg_.mr != nullptr) ? cfg_.mr : std::pmr::new_delete_resource();
    // 013 T039: propagate listener_events_ so async_handshake can emit after
    // the handshake completes (trampoline captures error; C++ side emits).
    hctx.listener_events = listener_events_;

    // ── Construct the ssl_stream (lazy at handshake start) ────────────────────
    ssl_stream_.emplace(socket_, *ssl_ctx_);

    // ── Register HandshakeCtx in SSL* ex_data ─────────────────────────────────
    SSL* ssl = ssl_stream_->native_handle();
    SSL_set_ex_data(ssl, ssl_ex_data_transport_idx(), &hctx);

    // ── Handshake with timeout ────────────────────────────────────────────────
    asio::steady_timer timer{exec_};
    timer.expires_after(cfg_.tls_handshake_timeout);
    // Timer-epoch guard (D-4.1, FR-014): separate `handshake` counter from
    // `connect` above — see the class-body comment for why a shared counter
    // is not used even though today's callers happen to serialize the two.
    const std::uint64_t handshake_epoch = ++timer_epochs_->handshake;
    timer.async_wait([this, epochs = timer_epochs_, handshake_epoch](asio::error_code ec) {
        if (ec || handshake_epoch != epochs->handshake) {
            return;  // cancelled, or this attempt's epoch was retired — no-op.
        }
        asio::error_code ignored;
        socket_.cancel(ignored);
    });

    asio::error_code handshake_ec;
    const auto hs_role =
        (role_ == role_t::server) ? asio::ssl::stream_base::server : asio::ssl::stream_base::client;
    co_await ssl_stream_->async_handshake(hs_role,
                                          asio::redirect_error(asio::use_awaitable, handshake_ec));

    // Retire this attempt's epoch BEFORE cancel() (D-4.1).
    ++timer_epochs_->handshake;
    timer.cancel();

    // Clear the ex_data pointer before HandshakeCtx leaves scope.
    if (ssl_stream_) {
        SSL_set_ex_data(ssl_stream_->native_handle(), ssl_ex_data_transport_idx(), nullptr);
    }

    if (handshake_ec) {
        state_ = state_t::closed;
        if (handshake_ec == asio::error::operation_aborted) {
            auto cs = co_await asio::this_coro::cancellation_state;
            if (cs.cancelled() != asio::cancellation_type::none) {
                co_return std::unexpected{E::transport_handshake_cancelled};
            }
            co_return std::unexpected{E::transport_handshake_timeout};
        }
        // 013 T039: if the trampoline captured a precise tls_* error code
        // (verify_peer rejection path), emit session_event_tls_validation_failed
        // to the listener's event ring BEFORE returning. The handshake_ec is the
        // OpenSSL-level SSL error that resulted from the trampoline returning 0.
        // We emit here (not in the !hctx.accepted block below) because
        // OpenSSL errors from a rejected peer cert reach this branch FIRST.
        // [013 FR-026 / FR-027 / FR-028 / data-model §E-5]
        if (hctx.listener_events != nullptr && hctx.verify_error.has_value()) {
            const auto code = *hctx.verify_error;

            // Read peer endpoint as "host:port". The TCP connection is still
            // live even though TLS was rejected; use best-effort read.
            std::string peer_ep_str;
            {
                asio::error_code ep_ec;
                auto remote = socket_.remote_endpoint(ep_ec);
                if (!ep_ec) {
                    peer_ep_str =
                        remote.address().to_string() + ":" + std::to_string(remote.port());
                }
            }

            // Emit with owning-string copies for the string_view fields.
            // The listener's parallel string arrays hold the copies; the
            // string_views in the event point into those arrays. [data-model §E-5]
            hctx.listener_events->emit_with_strings(
                fixpp::session::session_event_tls_validation_failed{}, code, hctx.sub_reason_copy,
                peer_ep_str, "TLS peer cert rejected: " + hctx.sub_reason_copy);
        }

        co_return std::unexpected{E::transport_handshake_failed};
    }

    // If the trampoline rejected the peer cert without causing a handshake_ec
    // (unusual case where OpenSSL did not propagate its error), surface failure.
    // No verify_error capture expected here, but guard defensively.
    if (!hctx.accepted) {
        state_ = state_t::closed;
        co_return std::unexpected{E::transport_handshake_failed};
    }

    // ── Build handshake_result with PMR allocations trapped (per /simplify
    //    audit Agent-1 P1: PMR exhaustion mid-result-build must surface
    //    transport_handshake_failed, not std::terminate via uncaught
    //    coroutine throw). State transition happens AFTER all PMR allocs
    //    succeed so a fault-injection unwind leaves state_ == closed, not
    //    the half-set "handshake-completed-but-result-broken" middle. ────
    std::pmr::memory_resource* mr =
        (cfg_.mr != nullptr) ? cfg_.mr : std::pmr::new_delete_resource();
    const char* cipher_name = SSL_get_cipher_name(ssl_stream_->native_handle());
    handshake_result result;
    try {
        peer_id_ = std::move(hctx.peer_id);
        result.peer_id = peer_id_;
        result.captured_pinset = captured_pinset_;
        result.negotiated_cipher = std::pmr::string{cipher_name ? cipher_name : "", mr};
    } catch (...) {
        state_ = state_t::closed;
        co_return std::unexpected{E::transport_handshake_failed};
    }

    state_ = state_t::handshaken;
    co_return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// async_read_some
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> asio_tls_transport::async_read_some(
    std::span<std::byte> buf) {
    using E = core::error;

    // 088 T029 (FR-018) — the SSL composed op (ssl::detail::io_op) delegates to
    // base_from_cancellation_state's no-filter overload, which is documented and
    // implemented as TERMINAL-ONLY (asio/cancellation_state.hpp:88-100). A
    // one-argument reset installs a MASK (asio::cancellation_filter is `type &
    // Mask`, not a map), so Engine::stop()'s cancellation_type::total would be
    // forwarded as `total` and then silently dropped by that inner terminal-only
    // state — the in-flight read never aborts and stop() hangs UNBOUNDED on the
    // default TLS accept path (research.md D-2a). The two-argument OUT filter
    // below maps any accepted (non-none) cancellation to `terminal` for the
    // forwarded child op, so the io_op's inner state records and forwards it.
    // Mirrors async_connect's precedent at :918-933, including its commenting
    // discipline. NOT expressible at the call site (read_first_frame_bounded):
    // reset_cancellation_state REPLACES the single bottom-frame state — last
    // reset wins — so an engine-side wrapper reset is clobbered by this one.
    // [[feedback_asio_cospawn_total_cancellation_default]];
    // [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]].
    co_await asio::this_coro::reset_cancellation_state(
        asio::enable_total_cancellation(), [](asio::cancellation_type ct) {
            return ct == asio::cancellation_type::none ? ct : asio::cancellation_type::terminal;
        });

    // state_ != handshaken covers both `closed` and pre-handshake states;
    // post-close every async_* returns transport_already_closed per FR-006.
    // ssl_stream_ has_value() iff async_handshake has emplaced it; the
    // state_ == handshaken transition strictly follows that emplace, so the
    // optional must be engaged here. The explicit check guards the invariant
    // against future state-machine drift.
    if (state_ != state_t::handshaken || !ssl_stream_) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // In-flight exclusivity guard (FR-007 — strand-confined boolean).
    if (timer_epochs_->read_in_flight) {
        co_return std::unexpected{E::transport_read_in_progress};
    }

    // #341: no pre-read cancellation reap here, deliberately -- it would be
    // dead. See the CANCELLATION TIMING note on Transport in transport.hpp
    // for the mechanism and the re-derivation recipe.

    // #346: RAII — see inflight_flag_guard.hpp for why not assignment.
    detail::inflight_flag_guard read_guard{timer_epochs_, &timer_epoch_state::read_in_flight};

    // NEVER allocate in the read-path completion-handler dispatch per [const §VIII.5].
    // asio::ssl::stream::async_read_some writes directly into the caller-owned buf.
    asio::error_code ec;
    std::size_t bytes_read = co_await ssl_stream_->async_read_some(
        asio::buffer(buf.data(), buf.size()), asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
        if (ec == asio::error::operation_aborted) {
            co_return std::unexpected{E::transport_read_cancelled};
        }
        if (ec == asio::error::eof) {
            co_return std::unexpected{E::transport_read_eof};
        }
        // RC#D (P2-1): stream_truncated (peer dropped TCP without SSL close-notify)
        // surfaces as the DISTINCT transport_read_truncated variant per SC-006.
        // FR-006 had an internal contradiction ("surfaces as truncated" vs "v1.0
        // treats as eof"); SC-006 (distinct named variant per failure mode) wins.
        if (ec == asio::ssl::error::stream_truncated) {
            co_return std::unexpected{E::transport_read_truncated};
        }
        co_return std::unexpected{E::transport_read_error};
    }

    co_return bytes_read;
}

// ─────────────────────────────────────────────────────────────────────────────
// async_write
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] asio::awaitable<core::expected_t<std::size_t>> asio_tls_transport::async_write(
    std::span<const std::byte> bytes) {
    using E = core::error;

    // Enable total cancellation (D-17).
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // state_ != handshaken covers both `closed` and pre-handshake states.
    // ssl_stream_ is engaged iff state_ == handshaken (see async_read_some
    // comment); the explicit optional check guards the invariant.
    if (state_ != state_t::handshaken || !ssl_stream_) {
        co_return std::unexpected{E::transport_already_closed};
    }

    // In-flight exclusivity guard (FR-007).
    if (timer_epochs_->write_in_flight) {
        co_return std::unexpected{E::transport_write_in_progress};
    }

    // #341: no pre-write cancellation reap here, deliberately -- it would be
    // dead. See the CANCELLATION TIMING note on Transport in transport.hpp
    // for the mechanism and the re-derivation recipe.

    // #346: RAII — see inflight_flag_guard.hpp for why not assignment.
    detail::inflight_flag_guard write_guard{timer_epochs_, &timer_epoch_state::write_in_flight};

    // Composed write (async_write — NOT async_write_some per FR-004).
    asio::error_code ec;
    std::size_t bytes_written =
        co_await asio::async_write(*ssl_stream_, asio::buffer(bytes.data(), bytes.size()),
                                   asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
        if (ec == asio::error::operation_aborted) {
            // Surface short-write if partial data was sent.
            if (bytes_written > 0 && bytes_written < bytes.size()) {
                co_return std::unexpected{E::transport_write_short};
            }
            co_return std::unexpected{E::transport_write_cancelled};
        }
        co_return std::unexpected{E::transport_write_error};
    }

    // Partial write without error is a short-write condition.
    if (bytes_written < bytes.size()) {
        co_return std::unexpected{E::transport_write_short};
    }

    co_return bytes_written;
}

// ─────────────────────────────────────────────────────────────────────────────
// cancel — synchronous, strand-confined, idempotent (FR-005; #333)
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<void> asio_tls_transport::cancel() noexcept {
    // Cancel the socket directly: triggers operation_aborted on every
    // in-flight read / write / connect / handshake bound to socket_. The
    // error_code overload prevents any throw from escaping this noexcept
    // method. ⚠️ "is documented thread-safe" was struck 2026-08-31 (#333) —
    // asio's basic_stream_socket @par Thread Safety block says "Shared
    // objects: Unsafe" and does not carve out cancel. Call on the session
    // strand. Note this method emits no cancellation_signal, contrary to the
    // three class-header comments corrected in the same pass.
    // MUST NOT close the socket (FR-005) — close() is a separate API.
    asio::error_code ec;
    socket_.cancel(ec);
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// close — synchronous on session strand (FR-006)
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] core::expected_t<void> asio_tls_transport::close() noexcept {
    // Idempotent: second close() returns {} without side-effects.
    if (state_ == state_t::closed) {
        return {};
    }

    // Transition to closed immediately.
    state_ = state_t::closed;

    // ── Best-effort TLS close-notify (graceful shutdown) ──────────────────────
    // Send the close_notify alert ONLY when no SSL operation is suspended on this
    // strand. close() is strand-confined (FR-006 — see header §"State machine"),
    // so the in-flight flags in *timer_epochs_ are authoritative reads here. If a
    // read/write is in flight, its completion handler will later run
    // map_error_code → BIO_ctrl on ssl_stream_; mutating the SSL state via
    // SSL_shutdown underneath that suspended op is unsafe, and socket_.close()
    // below already interrupts it with an error (a truncated close — acceptable
    // non-fatal per [2g §7.8]). When nothing is in flight, we send the
    // close_notify for a clean bidi shutdown the peer can observe.
    //
    // ssl_stream_ is NEVER reset here. The pending async_read_some completion (on
    // this session strand) passes through asio's SSL layer which calls
    // map_error_code → BIO_ctrl on the SSL BIO; ssl_stream_.reset() would free the
    // BIO underneath it → UAF / SEGFAULT (the prior bug). ssl_stream_ is destroyed
    // in ~asio_tls_transport, which (for engine-managed sessions) runs after the
    // role loop exits — i.e., after run_read_pump co_returns and all pending SSL
    // completions have executed. [2g §7.8]
    // ssl_op_suspended_() also covers handshake_in_flight_ (#342). Before that
    // term existed, a suspended async_handshake -- ssl_stream_ ENGAGED, state_
    // == connected, neither read nor write flag set -- let close() reach
    // SSL_shutdown with an SSL operation suspended on the stream, exactly the
    // mutation the paragraph above forbids. The predicate's declaration carries
    // the invariant that keeps the set complete.
    if (ssl_stream_ && !ssl_op_suspended_()) {
        SSL* ssl = ssl_stream_->native_handle();
        if (ssl) {
            // First SSL_shutdown sends close_notify; bounded by tls_close_timeout
            // {1s} — for a synchronous close() we send but do NOT block for the
            // peer's response. Return value ignored (best-effort).
            (void)SSL_shutdown(ssl);
        }
    }

    // ── Close the underlying TCP socket ───────────────────────────────────────
    asio::error_code ec;
    socket_.close(ec);
    // Best-effort; ignore ec.

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// close_async — graceful TLS shutdown that actually reaches the wire (#348)
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] asio::awaitable<core::expected_t<void>> asio_tls_transport::close_async() {
    // Same cancellation policy as the five sibling async methods on this class,
    // which all reset here — close_async was the one that did not, and its
    // quiesce loop below is the reason that mattered: without the reset, a
    // cancellation arriving mid-close makes the NEXT `co_await` in the loop
    // throw operation_aborted out of a teardown path whose callers document a
    // clean `expected_t<void>{}` return. `this_coro` awaiters are exempt from
    // asio's throw-on-cancelled check, so this line is reachable even when the
    // inherited state is already cancelled. ⚠️ It does NOT protect the CALLER's
    // own `co_await t->close_async()` — that throw happens one frame up, before
    // this body runs; see the CANCELLATION TIMING note in transport.hpp.
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    // Idempotent, exactly like close().
    if (state_ == state_t::closed) {
        co_return core::expected_t<void>{};
    }

    // No stream at all -- nothing to shut down gracefully. Delegate to close(),
    // which is reached with state_ still open and so does the socket teardown.
    if (!ssl_stream_) {
        co_return close();
    }

    // Set BEFORE the suspension so no new async_* can start against ssl_stream_
    // while the shutdown is in flight -- read/write/handshake all reject on
    // state_.
    //
    // ⚠️ IT ALSO MAKES A close() OR A SECOND close_async() ARRIVING DURING THE
    // SUSPENSION RETURN {} WHILE THE SOCKET IS STILL OPEN. That second caller is
    // told "closed" up to `tls_close_timeout` before the socket actually closes.
    // It is SUBSUMPTION rather than a leak only because of the guarantee below:
    // every exit from this function past this line closes the socket, so the
    // close the second caller was promised does happen, bounded by that budget.
    // Before the catch(...) existed that guarantee did not hold and this really
    // was a leak.
    //
    // ⚠️ Making close() fall THROUGH here (close the socket itself rather than
    // return early) was considered and NOT done. It would need a `closing` flag
    // in the predicate below so close() takes its no-SSL_shutdown path, and the
    // difference it buys is observable only while a close_async is slow — i.e.
    // against a WEDGED operation, which nothing in the suite can produce. It
    // would be an unwitnessable branch guarding a window no in-tree caller can
    // reach. Recorded instead: see B-348-1 (AMENDED 2) in
    // spec/behaviors-and-limitations.md. Revisit if an adopter appears that can
    // race the two entry points and act on the early {}.
    //
    // ⚠️ FROM HERE ON, DO NOT DELEGATE TO close(): it reads state_ for its
    // idempotency check and would return {} having closed nothing. Every bail-out
    // below performs the socket teardown itself.
    //
    // ⚠️ AND THAT MUST HOLD FOR THE EXCEPTIONAL EXITS TOO, WHICH IS WHAT THE
    // catch(...) BELOW IS FOR. Publishing `closed` here makes both close() and a
    // second close_async() no-ops, so an unwind between this line and the socket
    // teardown would strand a LIVE connection behind a permanently closed logical
    // state, unrecoverable through either entry point. The suspension points
    // below can throw: asio's throw-on-cancelled precheck fires at a `co_await`
    // whose state was cancelled after the reset above, and timer construction can
    // throw. The subsumption claim in the paragraph above is only true because
    // every path out of this function from here closes the socket.
    //
    // ⚠️ NOT AN RAII GUARD, deliberately. A scope guard holding `&socket_` is a
    // guard bound to a TRANSPORT MEMBER, which is the measured heap-use-after-free
    // inflight_flag_guard.hpp exists to prevent: it would also run on the
    // frame-DESTROYED path, where the Transport may already be gone. A catch runs
    // only on the frame-RESUMED path, where `this` is alive by construction.
    state_ = state_t::closed;

    // ONE budget for the whole call, not one per phase. The quiesce below and
    // the shutdown deadline further down both run against this deadline, so the
    // documented tls_close_timeout knob bounds close_async as a whole rather
    // than being spent twice over.
    const auto close_deadline = std::chrono::steady_clock::now() + cfg_.tls_close_timeout;

    try {
        // ── Quiesce a suspended SSL operation rather than giving up on it (#348) ──
        // close() bails to an abortive close whenever an op is suspended, because
        // SSL_shutdown underneath a suspended op mutates state that op's completion
        // is about to touch (map_error_code -> BIO_ctrl on the SSL BIO). Inheriting
        // that bail here would have made close_async useless at exactly the call
        // sites that need it: on an established session the read pump is ALWAYS
        // blocked in async_read_some (engine.cpp's stop() says so at its own
        // transport-close step, and closes the socket precisely to wake it).
        //
        // The hazard is the SUSPENSION, not the operation. socket_.cancel()
        // completes the pending op with operation_aborted WITHOUT touching SSL
        // state; once its frame has resumed and inflight_flag_guard has cleared the
        // flag, nothing is suspended on ssl_stream_ and the shutdown is safe.
        //
        // ⚠️ state_ is already closed above, which is what makes this terminate: a
        // woken read pump that immediately re-reads is refused, so the flag stays
        // clear once it drops.
        if (ssl_op_suspended_()) {
            asio::error_code cancel_ec;
            socket_.cancel(cancel_ec);

            // Bounded join. Same shape as the stop() join in engine.cpp: a
            // zero-length timer wait yields THROUGH THE SCHEDULER, which a bare
            // asio::post does not -- the completion being waited for is delivered
            // by the reactor, and a post-only spin can starve it.
            asio::steady_timer quiesce{exec_};
        bool first_poll = true;
            while (ssl_op_suspended_() && std::chrono::steady_clock::now() < close_deadline) {
                // One IMMEDIATE re-check — the cancelled completion is normally already
            // queued behind us — then a real backoff. Polling at zero length for the
            // whole budget would turn the fallback for a WEDGED operation into a
            // spin that saturates the strand it is waiting on, i.e. it would amplify
            // exactly the condition it exists to contain.
            quiesce.expires_after(first_poll ? std::chrono::milliseconds{0}
                                             : std::chrono::milliseconds{1});
            first_poll = false;
                asio::error_code wait_ec;
                co_await quiesce.async_wait(asio::redirect_error(asio::use_awaitable, wait_ec));
                if (wait_ec) {
                    // Our own cancellation. Break rather than spin to the deadline
                    // -- every subsequent wait would complete instantly with the
                    // same error, turning a bounded join into a hot loop.
                    break;
                }
            }

            if (ssl_op_suspended_()) {
                // Could not quiesce. Abortive close -- and deliberately NO
                // SSL_shutdown, because that is the exact mutation the suspended
                // completion forbids. The peer sees the #348 symptom; that is the
                // honest outcome, not a pretended graceful one.
                //
                // ⚠️ NO CELL DRIVES THIS BRANCH, measured: removing this
                // socket_.close() leaves every close_async cell GREEN, including
                // the cancellation cell, which reaches the catch(...) below
                // instead. Getting here needs an operation that is STILL in
                // flight after the loop gave up — a wedged op or an exhausted
                // budget — and nothing in the suite can hold a read in flight
                // against socket_.cancel(). It satisfies the same invariant as
                // the catch: no exit past the state transition leaves the socket
                // open. Do not add a cell that claims to drive it without
                // running the mutation first.
                asio::error_code ec;
                socket_.close(ec);
                co_return core::expected_t<void>{};
            }
        }

        // THE DIFFERENCE FROM close(). close() calls SSL_shutdown() on the native
        // handle, which only deposits the alert into asio's BIO; nothing drains it
        // and the socket_.close() that follows discards it. async_shutdown goes
        // through ssl::detail::io, which drains engine::get_output() to the next
        // layer -- i.e. it actually writes the alert.
        //
        // Bounded by the shared close_deadline above, so a peer that never answers
        // cannot hold the close open; the budget comes from Config rather than a
        // literal so the documented knob is the one in force, and it is the budget
        // for the whole call -- a quiesce that consumed part of it leaves the
        // shutdown that much less. We do NOT wait for the peer's
        // answering close_notify beyond that budget -- sending ours is what makes
        // the peer's read a clean EOF, which is the observable #348 is about.
        // QUICK SHUTDOWN. Without this, async_shutdown waits for the peer's
        // ANSWERING close_notify and only the deadline below releases it -- so
        // every close against a peer that does not itself close gracefully burned
        // the WHOLE tls_close_timeout budget.
        // Marking the inbound direction already-shut makes SSL_shutdown report
        // complete as soon as OUR alert is written, which is the only half #348 is
        // about; the deadline goes back to being a backstop rather than the normal
        // exit path.
        SSL_set_shutdown(ssl_stream_->native_handle(), SSL_RECEIVED_SHUTDOWN);

        // Deadline, armed with the SAME epoch discipline as the connect and
        // handshake sites above -- the handler captures a shared_ptr COPY of the
        // block and compares, never `this` alone. timer.cancel() cannot un-queue an
        // already-completed handler (D-4.1), so a deadline that fired just before
        // teardown would otherwise reach socket_.cancel() through a dangling
        // `this`. The epoch is retired below BEFORE cancel(), and again in the
        // destructor body.
        asio::steady_timer deadline{exec_};
        deadline.expires_at(close_deadline);
        const std::uint64_t close_epoch = ++timer_epochs_->close;
        deadline.async_wait([this, epochs = timer_epochs_, close_epoch](asio::error_code ec) {
            if (ec || close_epoch != epochs->close) {
                return;  // cancelled, or this attempt's epoch was retired — no-op.
            }
            asio::error_code ignored;
            socket_.cancel(ignored);
        });

        asio::error_code shutdown_ec;
        co_await ssl_stream_->async_shutdown(asio::redirect_error(asio::use_awaitable, shutdown_ec));

        // Retire BEFORE cancel (D-4.1) — see the connect/handshake sites.
        ++timer_epochs_->close;
        deadline.cancel();

        // shutdown_ec is deliberately not surfaced. A peer that closes the TCP
        // connection without answering yields eof/stream_truncated here, and that
        // is a NORMAL graceful close from our side: our alert was still written,
        // which is the whole point. close() is best-effort and so is this.
        (void)shutdown_ec;
    } catch (...) {
        // Any unwind after the state transition. The socket is still open and
        // nothing else can close it, so close it here and report the same
        // best-effort success close() reports — the connection IS down.
        //
        // WITNESSED by CloseAsyncCancelledMidCloseStillClosesTheSocket in
        // test_inflight_exclusivity.cpp, which kills the mutation that removes
        // this socket_.close(): the peer's read then stays pending forever.
        //
        // ⚠️ AN EARLIER DRAFT OF THIS COMMENT SAID "NO CELL DRIVES THIS HANDLER"
        // and explained at length why one could not — reasoning that landing a
        // cancellation inside the microsecond-wide async_shutdown suspension
        // would be racing. That reasoning described a path this handler is NOT
        // usually reached by. What the cell actually does is emit during the
        // QUIESCE; the wait_ec break then falls through with the state still
        // cancelled, and the throw happens at the async_shutdown `co_await`'s
        // precheck rather than inside the shutdown. The claim was refuted by
        // running the mutation, which is the only reason it is not still here.
        // The give-up branch above is the exit with no cell — see its comment.
        asio::error_code ec;
        socket_.close(ec);
        co_return core::expected_t<void>{};
    }

    // ssl_stream_ is NEVER reset here, for the same reason close() documents:
    // a pending completion passes through map_error_code -> BIO_ctrl on the SSL
    // BIO, and freeing it underneath that is a UAF.
    asio::error_code ec;
    socket_.close(ec);

    co_return core::expected_t<void>{};
}

}  // namespace fixpp::transport
