// src/tls/verify_peer.cpp
// Implementation of verify_peer + last_handshake_sub_reason.
//
// Design anchor: .specify/2g-tls.md v0.4 §6.5.1 BINDING CONTRACT (10-step
// short-circuit order) + contracts/security_profile.hpp assertions 6-9.
// Spec anchors: FR-019 (DoS bounds), FR-020 (full enforcement),
//               FR-020a (short-circuit canonical order per Clarify Q3).
//
// 10-step order (FR-020a BINDING CONTRACT — short-circuit on first violation):
//   1. Per-cert DER size        ≤ caps.max_cert_der_bytes  → tls_cert_der_too_large
//   2. RSA key lower bound      ≥ 2048                     → tls_handshake_failed "rsa_under_min"
//   3. RSA key upper bound      ≤ caps.max_rsa_key_bits    → tls_rsa_key_too_large
//   4. ECDSA curve              ∈ {P-256, P-384}           → tls_handshake_failed "ecdsa_curve"
//   5. Chain depth              ≤ caps.max_chain_depth     → tls_handshake_failed "chain_too_deep"
//   6. SAN cardinality          ≤ caps.max_san_entries     → tls_san_entries_exceeded
//   7. X.509 version            ∈ {v2, v3}                 → tls_handshake_failed "x509_v1"
//   8. Expiration               leaf.not_before ≤ now ≤ leaf.not_after → tls_handshake_failed "expired"/"not_yet_valid"
//   9. Pinning (mtls_pinned)    scan cfg.pinset_snapshot for leaf.sha256_ → tls_pin_mismatch
//  10. Cipher (TODO 2h)         CipherPolicy::is_allowed(negotiated)  → tls_cipher_not_allowed
//
// CRITICAL per [2g §6.5.1]: verify_peer NEVER calls cfg.pinset->find /
// contains / snapshot. It reads cfg.pinset_snapshot (captured at make_ssl_ctx_config).
//
// Sub-reason carrier: thread-local per pragmatic v0.1 design (T037 brief).
// The thread-local is set only when verify_peer returns tls_handshake_failed.

#include <fixpp/tls/security_profile.hpp>

#include <fixpp/core/error.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string_view>

namespace fixpp::tls {

// ── Thread-local sub-reason carrier ──────────────────────────────────────────
// Points to a string literal (static storage) so the value is safe across
// call boundaries within the same thread.
static thread_local const char* tls_last_sub_reason = "";

std::string_view last_handshake_sub_reason() noexcept {
    return std::string_view{tls_last_sub_reason};
}

namespace {

// Helper: return tls_handshake_failed with a sub-reason set.
core::expected_t<peer_identity>
handshake_failed(const char* sub_reason) noexcept {
    tls_last_sub_reason = sub_reason;
    return std::unexpected{core::error::tls_handshake_failed};
}

}  // namespace

core::expected_t<peer_identity>
verify_peer(SslCtxConfig const&          cfg,
            std::span<const Certificate> peer_chain) noexcept
{
    // Empty chain is a degenerate case; treat as handshake failure.
    if (peer_chain.empty())
        return handshake_failed("empty_chain");

    const Certificate& leaf = peer_chain[0];
    const auto& caps = cfg.caps;

    // ── Step 1: Per-cert DER size ──────────────────────────────────────────────
    if (leaf.raw_der().size() > caps.max_cert_der_bytes)
        return std::unexpected{core::error::tls_cert_der_too_large};

    // ── Step 2: RSA key lower bound (≥ 2048 bits) ─────────────────────────────
    if (leaf.alg() == signature_algorithm::rsa_pss && leaf.rsa_key_bits() < 2048)
        return handshake_failed("rsa_under_min");

    // ── Step 3: RSA key upper bound (≤ max_rsa_key_bits) ──────────────────────
    if (leaf.alg() == signature_algorithm::rsa_pss && leaf.rsa_key_bits() > caps.max_rsa_key_bits)
        return std::unexpected{core::error::tls_rsa_key_too_large};

    // ── Step 4: ECDSA curve ∈ {P-256, P-384} ──────────────────────────────────
    if (leaf.alg() == signature_algorithm::ecdsa) {
        if (leaf.curve() != ecdsa_curve::p256 && leaf.curve() != ecdsa_curve::p384)
            return handshake_failed("ecdsa_curve");
    }

    // ── Step 5: Chain depth ≤ max_chain_depth ─────────────────────────────────
    if (peer_chain.size() > caps.max_chain_depth)
        return handshake_failed("chain_too_deep");

    // ── Step 6: SAN cardinality ≤ max_san_entries ─────────────────────────────
    const std::size_t san_total =
        leaf.san_dns_names().size() + leaf.san_uris().size();
    if (san_total > caps.max_san_entries)
        return std::unexpected{core::error::tls_san_entries_exceeded};

    // ── Step 7: X.509 version ∈ {v2, v3} (v1 rejected) ───────────────────────
    if (leaf.x509_version() == 1)
        return handshake_failed("x509_v1");

    // ── Step 8: Expiration ────────────────────────────────────────────────────
    if (cfg.clock) {
        const auto now = cfg.clock->now();
        if (now > leaf.not_after())
            return handshake_failed("expired");
        if (now < leaf.not_before())
            return handshake_failed("not_yet_valid");
    }

    // ── Step 9: Pinning (mtls_pinned only) ────────────────────────────────────
    // Consumes cfg.pinset_snapshot — NEVER calls cfg.pinset->find/contains/snapshot.
    if (cfg.profile == SecurityProfile::mtls_pinned && cfg.pinset_snapshot) {
        const auto& fp = leaf.sha256();
        bool found = false;
        for (const auto& pin_entry : *cfg.pinset_snapshot) {
            if (pin_entry.sha256 == fp) {
                found = true;
                break;
            }
        }
        if (!found)
            return std::unexpected{core::error::tls_pin_mismatch};
    }

    // ── Step 10: Cipher (TODO: 2h delegates negotiated cipher string) ──────────
    // The negotiated cipher is not available in Phase-5 scope (2h hasn't shipped).
    // Structure the check site so 2h can inject the cipher string later.
    // TODO(2h): if (!CipherPolicy::is_allowed(negotiated_cipher))
    //     return std::unexpected{core::error::tls_cipher_not_allowed};

    // ── Accept: build peer_identity ───────────────────────────────────────────
    // PMR resource: cfg.mr or std::pmr::new_delete_resource().
    auto* mr = cfg.mr ? cfg.mr : std::pmr::new_delete_resource();

    peer_identity id{mr};
    id.subject_dn.assign(leaf.subject_dn());

    for (const std::string_view dns : leaf.san_dns_names()) {
        std::pmr::string s{mr};
        s.assign(dns);
        id.san_dns_names_owned.push_back(std::move(s));
    }

    for (const std::string_view uri : leaf.san_uris()) {
        std::pmr::string s{mr};
        s.assign(uri);
        id.san_uris_owned.push_back(std::move(s));
    }

    id.leaf_fingerprint = leaf.sha256();
    id.not_after        = leaf.not_after();

    return id;
}

}  // namespace fixpp::tls
