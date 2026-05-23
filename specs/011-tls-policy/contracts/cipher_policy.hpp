// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-011 + FR-012.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.4 (CipherPolicy compile-time
// allow-list) + §6.1 (banned-cipher refusal at compile time).
// Spec anchors: spec.md FR-011 (compile-time allow-list, static_assert
// refusal of banned tokens), FR-012 (runtime is_allowed constexpr noexcept
// predicate for the 2i C-ABI bridge).
// Constitution: [const §XII.3] (allowed cipher suites), [const §XII.4]
// (banned cryptography), [const §XV.11] (banned TLS-1.0/1.1/SSL/RC4/etc).

#pragma once

// ============================================================================
// CipherPolicy — compile-time allow-list + runtime predicate.
// ============================================================================
//
// namespace fixpp::tls {
//
// struct CipherPolicy {
//     // ── TLS 1.3 suites (RFC 8446 §9.1 mandatory + recommended set; [const §XII.3]) ──
//     static constexpr std::array<std::string_view, 3> tls13_suites = {{
//         "TLS_AES_128_GCM_SHA256",
//         "TLS_AES_256_GCM_SHA384",
//         "TLS_CHACHA20_POLY1305_SHA256",
//     }};
//
//     // ── TLS 1.2 suites (ECDHE-(RSA|ECDSA) + AEAD only; SHA-256/SHA-384 PRF; [const §XII.3]) ──
//     static constexpr std::array<std::string_view, 6> tls12_suites = {{
//         "ECDHE-ECDSA-AES128-GCM-SHA256",
//         "ECDHE-ECDSA-AES256-GCM-SHA384",
//         "ECDHE-ECDSA-CHACHA20-POLY1305",
//         "ECDHE-RSA-AES128-GCM-SHA256",
//         "ECDHE-RSA-AES256-GCM-SHA384",
//         "ECDHE-RSA-CHACHA20-POLY1305",
//     }};
//
//     // ── Key exchange groups ([const §XII.3]) ──
//     static constexpr std::array<std::string_view, 3> kx_groups = {{
//         "X25519", "P-256", "P-384",
//     }};
//
//     // ── Signature algorithms ([const §XII.3]) ──
//     static constexpr std::array<std::string_view, 5> sig_algs = {{
//         "ecdsa_secp256r1_sha256",
//         "ecdsa_secp384r1_sha384",
//         "rsa_pss_rsae_sha256",
//         "rsa_pss_rsae_sha384",
//         "rsa_pss_rsae_sha512",
//     }};
//
//     // ── Banned tokens ([const §XII.4] + [const §XV.11]) ──
//     static constexpr std::array<std::string_view, 13> banned_tokens = {{
//         "RC4", "DES", "3DES", "MD5", "DH_anon", "NULL", "EXPORT",
//         "CBC", "SHA-1", "RSA-1024", "TLS-1.0", "TLS-1.1", "SSL",
//     }};
//
//     // Compile-time validation (FR-011): no banned token in any allow-list.
//     // (Implementation note: the actual impl uses a constexpr helper
//     // contains_any() that walks both lists and asserts disjoint.)
//     //
//     //   static_assert(!contains_any(tls13_suites, banned_tokens),
//     //                 "TLS 1.3 suite list contains a banned token (per [const §XII.3] / [const §XV.11]).");
//     //   static_assert(!contains_any(tls12_suites, banned_tokens),
//     //                 "TLS 1.2 suite list contains a banned token.");
//
//     // Runtime predicate (FR-012): the 2i C-ABI bridge invokes this when an
//     // opaque profile-or-cipher string crosses the language boundary.
//     // Returns true iff `tok` is on (tls13_suites ∪ tls12_suites) AND NOT
//     // on banned_tokens. Substring matching against banned_tokens is
//     // intentional — "AES128-CBC-SHA" should fail because of the "CBC" and
//     // "SHA-1" substrings.
//     [[nodiscard]]
//     static constexpr bool is_allowed(std::string_view tok) noexcept;
// };
//
// }  // namespace fixpp::tls
//
// ============================================================================
// Contract assertions (verified at /speckit-verify):
//
//   1. CipherPolicy::tls13_suites contains exactly the 3 RFC 8446 §9.1
//      mandatory/recommended suites (FR-011).
//   2. CipherPolicy::tls12_suites contains exactly 6 ECDHE-(RSA|ECDSA) AEAD
//      suites; no CBC, no SHA-1, no 1024-bit RSA.
//   3. A build that adds a banned token to any allow-list FAILS at compile
//      time with a static_assert diagnostic identifying the violation
//      (tests/tls/test_cipher_policy_compile_time_refusal.cpp witnesses via
//      a positive `static_assert(!is_allowed(banned), ...)`-style test).
//   4. is_allowed(...) is constexpr + noexcept (FR-012).
//   5. is_allowed("RC4-MD5") returns false (substring matches "RC4" + "MD5").
//   6. is_allowed("ECDHE-RSA-AES128-CBC-SHA") returns false (substring "CBC" + "SHA-1").
//   7. is_allowed("ECDHE-ECDSA-AES128-GCM-SHA256") returns true.
//   8. is_allowed("TLS_AES_128_GCM_SHA256") returns true.
