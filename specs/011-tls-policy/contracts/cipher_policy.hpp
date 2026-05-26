// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-011 + FR-012.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.4 (CipherPolicy compile-time
// allow-list) + §6.1 (banned-cipher refusal at compile time). Shapes below
// re-emitted VERBATIM from 2g §4.4 lines 549-616 (round-2 P1-4 close — the
// round-1 rewriter silently drifted kx_groups / sig_algs / banned_tokens
// from the design-doc verbatim while closing NEW-P3-1; round 2 restores
// the byte-faithful transcription).
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
//
//     // ── TLS 1.3 suites (RFC 8446 §9.1 mandatory + recommended) ──
//     static constexpr std::array<std::string_view, 3> tls13_suites {{
//         "TLS_AES_128_GCM_SHA256",
//         "TLS_AES_256_GCM_SHA384",
//         "TLS_CHACHA20_POLY1305_SHA256",
//     }};
//
//     // ── TLS 1.2 suites (ECDHE + AEAD only; SHA-256 / SHA-384 PRF) ──
//     static constexpr std::array<std::string_view, 6> tls12_suites {{
//         "ECDHE-RSA-AES128-GCM-SHA256",
//         "ECDHE-RSA-AES256-GCM-SHA384",
//         "ECDHE-ECDSA-AES128-GCM-SHA256",
//         "ECDHE-ECDSA-AES256-GCM-SHA384",
//         "ECDHE-RSA-CHACHA20-POLY1305",
//         "ECDHE-ECDSA-CHACHA20-POLY1305",
//     }};
//
//     // ── Key exchange groups ──
//     static constexpr std::array<std::string_view, 3> kx_groups {{
//         "X25519",
//         "secp256r1",
//         "secp384r1",
//     }};
//
//     // ── Signature algorithms ──
//     // RSA-PSS is a signature padding scheme; the key-size constraint
//     // ("RSA keys ≥ 2048 bits") is enforced separately at verify_peer
//     // (§4.5) per [FIXS §3.4]. The sig_algs entries name the OpenSSL
//     // sig_alg tokens; the key-size check is independent.
//     static constexpr std::array<std::string_view, 4> sig_algs {{
//         "ECDSA+SHA256",   // P-256
//         "ECDSA+SHA384",   // P-384
//         "RSA-PSS+SHA256", // PSS over RSA keys ≥ 2048 bits (key size enforced at verify_peer).
//         "RSA-PSS+SHA384",
//     }};
//
//     // Compile-time refusal of the banned set. Any attempt to add a string
//     // containing a banned token to any list above fires a static_assert.
//     // Note: banned_tokens covers two refusal axes — (1) tokens [const §XII.4]
//     // / [const §XV.11] explicitly bans (RC4, DES, 3DES, MD5, DH_anon, NULL,
//     // EXPORT, TLS_RSA static-key-exchange, CBC, SHA1, 0RTT); (2) tokens
//     // simply not on the [const §XII.3] allow-list (TLS_AES_128_CCM is
//     // RFC 8446 §B.4 OPTIONAL and not on the constitutional allow-list,
//     // so adding it to any allow-list array would silently slip past
//     // [const §XII.3]; the banned_tokens entry is a belt-and-braces
//     // intersection guard, NOT a constitutional ban).
//     static constexpr std::array<std::string_view, 12> banned_tokens {{
//         "RC4", "DES", "3DES", "MD5", "DH_anon", "NULL",
//         "EXPORT", "TLS_RSA", "CBC", "SHA1",
//         "TLS_AES_128_CCM",        // refused (not on [const §XII.3] allow-list — belt-and-braces intersection guard).
//         "0RTT",                   // banned per [const §XII.3].
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
//     // intentional — "AES128-CBC-SHA1" should fail because of the "CBC" and
//     // "SHA1" substrings.
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
//      suites; no CBC, no SHA1, no 1024-bit RSA.
//   3. A build that adds a banned token to any allow-list FAILS at compile
//      time with a static_assert diagnostic identifying the violation
//      (tests/tls/test_cipher_policy_compile_time_refusal.cpp witnesses via
//      a positive `static_assert(!is_allowed(banned), ...)`-style test).
//   4. is_allowed(...) is constexpr + noexcept (FR-012).
//   5. is_allowed("RC4-MD5") returns false (substring matches "RC4" + "MD5").
//   6. is_allowed("ECDHE-RSA-AES128-CBC-SHA1") returns false (substring "CBC" + "SHA1").
//   7. is_allowed("ECDHE-ECDSA-AES128-GCM-SHA256") returns true.
//   8. is_allowed("TLS_AES_128_GCM_SHA256") returns true.
