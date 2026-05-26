#pragma once
// include/fixpp/tls/cipher_policy.hpp
// fixpp::tls::CipherPolicy — compile-time allow-list + runtime predicate.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.4 lines 553-604 (verbatim).
// Spec anchors: FR-011 (compile-time allow-list, static_assert refusal),
//               FR-012 (runtime is_allowed constexpr noexcept predicate).
// Constitution: [const §XII.3] (allowed cipher suites),
//               [const §XII.4] (banned cryptography),
//               [const §XV.11] (banned TLS-1.0/1.1/SSL/RC4/etc).

#include <array>
#include <string_view>

namespace fixpp::tls {

namespace detail {

// Compile-time helper: does any entry in `list` contain a banned token from
// `banned` as a substring? Used by CipherPolicy::static_assert chain (FR-011).
template <std::size_t N, std::size_t M>
[[nodiscard]] constexpr bool contains_banned(
    std::array<std::string_view, N> const& list,
    std::array<std::string_view, M> const& banned) noexcept {
    for (std::string_view entry : list) {
        for (std::string_view ban : banned) {
            if (entry.find(ban) != std::string_view::npos) return true;
        }
    }
    return false;
}

}  // namespace detail

struct CipherPolicy {
    // ── TLS 1.3 suites (RFC 8446 §9.1 mandatory + recommended) ──
    static constexpr std::array<std::string_view, 3> tls13_suites{{
        "TLS_AES_128_GCM_SHA256",
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
    }};

    // ── TLS 1.2 suites (ECDHE + AEAD only; SHA-256 / SHA-384 PRF) ──
    static constexpr std::array<std::string_view, 6> tls12_suites{{
        "ECDHE-RSA-AES128-GCM-SHA256",
        "ECDHE-RSA-AES256-GCM-SHA384",
        "ECDHE-ECDSA-AES128-GCM-SHA256",
        "ECDHE-ECDSA-AES256-GCM-SHA384",
        "ECDHE-RSA-CHACHA20-POLY1305",
        "ECDHE-ECDSA-CHACHA20-POLY1305",
    }};

    // ── Key exchange groups ──
    static constexpr std::array<std::string_view, 3> kx_groups{{
        "X25519",
        "secp256r1",
        "secp384r1",
    }};

    // ── Signature algorithms ──
    // RSA-PSS key-size constraint (≥ 2048 bits) enforced at verify_peer per [FIXS §3.4].
    static constexpr std::array<std::string_view, 4> sig_algs{{
        "ECDSA+SHA256",
        "ECDSA+SHA384",
        "RSA-PSS+SHA256",
        "RSA-PSS+SHA384",
    }};

    // ── Banned tokens — substring-matched against any allow-list entry ──
    // Note: banned_tokens covers two axes:
    //   (1) tokens [const §XII.4] / [const §XV.11] explicitly bans;
    //   (2) tokens not on [const §XII.3] allow-list (TLS_AES_128_CCM belt-and-braces guard).
    static constexpr std::array<std::string_view, 12> banned_tokens{{
        "RC4",
        "DES",
        "3DES",
        "MD5",
        "DH_anon",
        "NULL",
        "EXPORT",
        "TLS_RSA",
        "CBC",
        "SHA1",
        "TLS_AES_128_CCM",  // not on [const §XII.3] allow-list — intersection guard.
        "0RTT",             // banned per [const §XII.3].
    }};

    // ── Compile-time static_assert chain (FR-011) ──
    // defined AFTER all array declarations so the values are available.
    // Fires a diagnostic at build time if any allow-list entry contains a banned token.
    static_assert(!detail::contains_banned(tls13_suites, banned_tokens),
                  "TLS 1.3 suite list contains a banned token "
                  "(per [const §XII.3] / [const §XII.4] / [const §XV.11]).");
    static_assert(!detail::contains_banned(tls12_suites, banned_tokens),
                  "TLS 1.2 suite list contains a banned token "
                  "(per [const §XII.3] / [const §XII.4] / [const §XV.11]).");
    static_assert(!detail::contains_banned(kx_groups, banned_tokens),
                  "Key-exchange group list contains a banned token.");
    static_assert(!detail::contains_banned(sig_algs, banned_tokens),
                  "Signature-algorithm list contains a banned token.");

    // ── Runtime predicate (FR-012) ──
    // Returns true iff tok ∈ (tls13_suites ∪ tls12_suites) AND
    // no banned_token is a substring of tok.
    // Substring matching: "ECDHE-RSA-AES128-CBC-SHA1" → fails on "CBC" + "SHA1".
    [[nodiscard]]
    static constexpr bool is_allowed(std::string_view tok) noexcept {
        // First: check no banned token is a substring.
        for (std::string_view ban : banned_tokens) {
            if (tok.find(ban) != std::string_view::npos) return false;
        }
        // Then: tok must be on the explicit allow-list (tls13 OR tls12).
        for (std::string_view s : tls13_suites) {
            if (s == tok) return true;
        }
        for (std::string_view s : tls12_suites) {
            if (s == tok) return true;
        }
        return false;
    }
};

}  // namespace fixpp::tls
