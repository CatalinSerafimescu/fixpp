// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-013..016 + FR-019..020a +
// FR-025 (incl. tls_pin_empty_at_open from Clarify Q2) + FR-014 (TLS-version
// posture from Clarify Q1).
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 (SecurityProfile enum + adapter
// + verify_peer + peer_identity) + §6.6 (error envelope).
// Spec anchors: spec.md FR-013 (enum + [[deprecated]] enumerator), FR-014
// (SSL_CTX mapping table; TLS 1.3 preferred + 1.2 fallback per Clarify Q1),
// FR-015/016 (frozen at session open), FR-019 (DoS bounds at verify_peer
// entry), FR-020 (full verify_peer enforcement), FR-020a (short-circuit
// evaluation order per Clarify Q3), FR-025 (named error variants incl.
// tls_pin_empty_at_open per Clarify Q2).

#pragma once

// ============================================================================
// SecurityProfile — explicit-choice enum per [const §XII.5].
// ============================================================================
//
// namespace fixpp::tls {
//
// enum class SecurityProfile : std::uint8_t {
//     unset       = 0,    // sentinel — make_ssl_ctx_config rejects with tls_invalid_security_profile
//     mtls_ca     = 1,    // recommended starting profile for v1.0 deployments
//     mtls_pinned = 2,    // FIXS-RC1 strict profile (required for FIXS-conformant deployments)
//     one_way_ca  [[deprecated("one_way_ca is legacy interop; prefer mtls_pinned or mtls_ca")]] = 3,
// };
//
// }  // namespace fixpp::tls
//
// ============================================================================
// SslCtxConfig — value-type description of the SSL_CTX configuration the
// 2h-transport feature must apply. 2g publishes this shape; 2h consumes.
// ============================================================================
//
// namespace fixpp::tls {
//
// struct SslCtxConfig {
//     SecurityProfile                                profile;
//
//     // FR-014 / Clarify Q1: TLS 1.3 preferred + TLS 1.2 fallback.
//     int                                            min_proto_version = TLS1_2_VERSION;
//     int                                            max_proto_version = TLS1_3_VERSION;
//
//     // Allow-list views; pointers into CipherPolicy::tls13_suites /
//     // CipherPolicy::tls12_suites. Non-owning; the policy is static-storage.
//     std::span<const std::string_view>              tls13_ciphersuites;
//     std::span<const std::string_view>              tls12_cipher_list;
//
//     std::shared_ptr<cert_source>                   source;
//     std::shared_ptr<Pinset>                        pinset;            // null permitted only under mtls_ca + one_way_ca
//     std::shared_ptr<Clock>                         clock;             // effective_clock per [2d §7.9]
//     cert_source::Config                            validation_caps;   // DoS bounds carried into verify_peer
//
//     int                                            ssl_verify_flags;  // e.g. SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
// };
//
// // FR-014 — adapter factory. Reject combinations:
// //   - mtls_pinned + null pinset            → tls_invalid_security_profile
// //   - mtls_pinned + non-null empty pinset  → tls_pin_empty_at_open  (Clarify Q2)
// //   - one_way_ca  + non-null pinset        → tls_invalid_security_profile (deprecated+pinned contradictory)
// //   - unset                                → tls_invalid_security_profile
// //   - null clock                           → tls_invalid_security_profile
// [[nodiscard]]
// expected_t<SslCtxConfig>
// make_ssl_ctx_config(SecurityProfile                profile,
//                     std::shared_ptr<cert_source>   source,
//                     std::shared_ptr<Pinset>        pinset,
//                     std::shared_ptr<Clock>         clock,
//                     cert_source::Config            validation_caps) noexcept;
//
// }  // namespace fixpp::tls
//
// ============================================================================
// SSL_CTX-config mapping table (FR-014) — normative; 2h-transport applies.
// ============================================================================
//
//   | profile       | SSL_VERIFY flags                                   | client cert | trust anchors | pin lookup    |
//   |---------------|----------------------------------------------------|-------------|---------------|---------------|
//   | mtls_ca       | SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT  | required    | REQUIRED      | optional      |
//   | mtls_pinned   | SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT  | required    | optional      | MANDATORY     |
//   | one_way_ca    | SSL_VERIFY_PEER (server view of server cert)       | not req'd   | REQUIRED      | forbidden     |
//   | unset         | n/a                                                | n/a         | n/a           | n/a           |
//
// For all profiles: min_proto_version = TLS1_2_VERSION; max_proto_version =
// TLS1_3_VERSION (Clarify Q1). 0-RTT/early-data is rejected at compile time.
//
// ============================================================================
// verify_peer — the validation predicate 2h's SSL_VERIFY_PEER callback calls.
// ============================================================================
//
// namespace fixpp::tls {
//
// class Certificate;     // E-11
// struct peer_identity;  // E-12
//
// // FR-020a / Clarify Q3 — SHORT-CIRCUIT first violation hit in this exact
// // order. Returns ONE error variant per failed handshake (no aggregate).
// //
// //   1. Per-cert DER size        ≤ caps.max_cert_der_bytes  → tls_cert_der_too_large
// //   2. RSA key lower bound      ≥ 2048                     → tls_rsa_key_too_small
// //   3. RSA key upper bound      ≤ caps.max_rsa_key_bits    → tls_rsa_key_too_large
// //   4. ECDSA curve              ∈ {P-256, P-384}           → tls_cert_invalid (curve)
// //   5. Chain depth              ≤ caps.max_chain_depth     → tls_cert_chain_too_deep
// //   6. SAN cardinality          ≤ caps.max_san_entries     → tls_san_entries_exceeded
// //   7. X.509 version            ∈ {v2, v3}                 → tls_cert_invalid (x509_v1)
// //   8. Expiration               leaf.not_before ≤ cfg.clock->now() ≤ leaf.not_after  → tls_cert_expired
// //   9. Pinning (mtls_pinned)    cfg.pinset->contains(leaf.sha256_)  → tls_cert_pin_mismatch
// //  10. Cipher (if 2h delegates) CipherPolicy::is_allowed(negotiated)  → tls_cipher_not_allowed
// //
// // Cert expiration evaluated against cfg.clock->now() which is the
// // session-scoped effective_clock per [2d §7.9] (D-9).
// [[nodiscard]]
// expected_t<peer_identity>
// verify_peer(SslCtxConfig const&             cfg,
//             Certificate const&              leaf,
//             std::span<const Certificate>    chain) noexcept;
//
// }  // namespace fixpp::tls
//
// ============================================================================
// Contract assertions (verified at /speckit-verify):
//
//   1. SecurityProfile has exactly 4 enumerators incl. `unset` sentinel and
//      `one_way_ca` carrying [[deprecated]] on the ENUMERATOR DECLARATION
//      (FR-013 / [const §XII.5]).
//   2. SslCtxConfig::min_proto_version = TLS1_2_VERSION and
//      max_proto_version = TLS1_3_VERSION by construction (Clarify Q1 /
//      FR-014); tests/tls/test_security_profile_adapter.cpp row-by-row
//      witness.
//   3. make_ssl_ctx_config(mtls_pinned, ..., empty_pinset, ...) returns
//      unexpected{tls_pin_empty_at_open} (Clarify Q2 / FR-025;
//      tests/tls/test_security_profile_empty_pinset.cpp).
//   4. make_ssl_ctx_config is noexcept; all expected_t<T>-returning factories
//      are [[nodiscard]] (FR-022).
//   5. verify_peer short-circuits on the FIRST violation hit in the
//      documented order (FR-020a / Clarify Q3); test seam
//      tests/tls/test_verify_peer_dos_bounds.cpp asserts row-by-row.
//   6. verify_peer uses cfg.clock for expiration (D-9 / [2d §7.9]);
//      tests/tls/test_verify_peer_expiration.cpp drives via mock_clock.
//   7. verify_peer returns owning peer_identity on accept (E-12).
//   8. Frozen-at-session-open enforced: there is no
//      Session::swap_security_profile(...) or Session::swap_cert_source(...)
//      API in v1.0 (FR-015 / FR-016).
