// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-013..016 + FR-019..020a +
// FR-025 (incl. tls_pin_empty_at_open from Clarify Q2) + FR-014 (TLS-version
// posture from Clarify Q1).
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 (SecurityProfile enum + adapter
// + verify_peer + peer_identity) + §4.5.1 (normative SecurityProfile-to-
// OpenSSL-mode mapping table) + §6.5.1 (verify_peer-time pinset access
// BINDING CONTRACT — snapshot-once-at-handshake-start) + §6.6 (error envelope).
// Shapes below are re-emitted VERBATIM from 2g §4.5.
// Spec anchors: spec.md FR-013 (enum + [[deprecated]] enumerator; FR-013 is
// REWRITTEN to four enumerators per Codex P1-4 sharpening + 2g §4.5 line
// 650-656), FR-014 (SSL_CTX mapping table; TLS 1.3 preferred + 1.2 fallback
// per Clarify Q1), FR-015/016 (frozen at session open), FR-019 (DoS bounds
// at verify_peer entry), FR-020 (full verify_peer enforcement), FR-020a
// (short-circuit evaluation order per Clarify Q3), FR-025 (named error
// variants incl. tls_pin_empty_at_open per Clarify Q2).

#pragma once

// ============================================================================
// SecurityProfile — explicit-choice enum per [const §XII.5].
//
// Re-emitted VERBATIM from 2g §4.5 lines 650-656. FOUR enumerators including
// the `unset = 0` sentinel that make_ssl_ctx_config rejects with
// tls_invalid_security_profile (Session::open also rejects with
// invalid_session_config per [2d §4.5] / §7.2 hand-off). The user-visible
// values are mtls_ca / mtls_pinned / one_way_ca; `unset` is the sentinel
// default-construction value per [const §XII.5]'s no-implicit-default rule.
//
// FR-013 sharpening (Codex P1-4 close): the FR's prose is rewritten to "four
// enumerators including the unset sentinel rejected at make_ssl_ctx_config /
// Session::open"; the [[deprecated]] attribute placement on the enumerator
// declaration (NOT in a comment) is the original FR-013 clause that the
// contract honours.
// ============================================================================
//
// namespace fixpp::tls {
//
// enum class SecurityProfile : std::uint8_t {
//     unset       = 0,   // sentinel — not a valid choice; rejected at Session::open and make_ssl_ctx_config per [const §XII.5].
//     mtls_ca     = 1,
//     mtls_pinned = 2,
//     one_way_ca  [[deprecated("one_way_ca is legacy interop; prefer mtls_pinned or mtls_ca")]]
//                 = 3,
// };
//
// }  // namespace fixpp::tls

// ============================================================================
// SslCtxConfig — value-type description of the SSL_CTX configuration the
// 2h-transport feature must apply. Re-emitted VERBATIM from 2g §4.5 lines
// 670-677.
//
// CRITICAL (NEW-P1-1 close): SslCtxConfig carries `pinset_snapshot` (a
// shared_ptr<const pin_snapshot> captured ONCE at handshake start by 2h's
// wiring) so verify_peer can scan the captured snapshot directly without
// reaching `cfg.pinset->find/contains/snapshot` itself. This is the §6.5.1
// BINDING CONTRACT operationalised. The `pinset` shared_ptr is retained for
// diagnostic / observability purposes (logging, metrics) but verification
// MUST use `pinset_snapshot`.
// ============================================================================
//
// namespace fixpp::tls {
//
// struct SslCtxConfig {
//     SecurityProfile                                  profile;
//     std::shared_ptr<cert_source>                     cs;
//     std::shared_ptr<Pinset>                          pinset;           // null permitted only under SecurityProfile::mtls_ca + one_way_ca. Kept for diagnostic reachability only; verification consumes pinset_snapshot.
//     std::shared_ptr<const pin_snapshot>              pinset_snapshot;  // NEW-P1-1 close: 2h captures Pinset::snapshot() ONCE at handshake start per §6.5.1 BINDING CONTRACT; verify_peer scans this snapshot directly (NEVER calls cfg.pinset->find/contains). null permitted under mtls_ca + one_way_ca.
//     std::shared_ptr<fixpp::core::Clock>              clock;            // [2d §7.9] effective_clock; verify_peer uses clock->now().
//     CipherPolicy                                     ciphers {};       // value-typed; constexpr-only members.
//     std::pmr::memory_resource*                       mr {nullptr};     // for peer_identity's owning SAN-string allocation; null → engine default.
// };
//
// // make_ssl_ctx_config — re-emitted VERBATIM from 2g §4.5 lines 681-686.
// //
// // Parameter order is: (profile, cs, clock, pinset = nullptr, mr = nullptr).
// // `pinset` and `mr` have defaults; `cs` and `clock` are required.
// //
// // Rejections per 2g §4.5 + §4.5.1 + /clarify Q2:
// //   - SecurityProfile::unset                       → tls_invalid_security_profile
// //   - null clock                                   → tls_invalid_security_profile
// //   - mtls_pinned + null pinset                    → tls_invalid_security_profile
// //   - mtls_pinned + non-null EMPTY pinset          → tls_pin_empty_at_open (/clarify Q2)
// //   - one_way_ca  + non-null pinset                → tls_invalid_security_profile (deprecated profile + pin is contradictory per §4.5.1 row 3)
// //
// // The cert_source's DoS-cap fields (max_rsa_key_bits, max_cert_der_bytes,
// // max_san_entries, max_chain_depth) are reachable via cs->config() (or via
// // a concrete-impl accessor); verify_peer consumes them through cs, NOT
// // through a duplicated SslCtxConfig field per 2g §4.2 line 320-326.
// [[nodiscard]] core::expected_t<SslCtxConfig>
//     make_ssl_ctx_config(SecurityProfile                       profile,
//                         std::shared_ptr<cert_source>          cs,
//                         std::shared_ptr<fixpp::core::Clock>   clock,
//                         std::shared_ptr<Pinset>               pinset = nullptr,
//                         std::pmr::memory_resource*            mr     = nullptr);
//
// }  // namespace fixpp::tls

// ============================================================================
// §4.5.1 SecurityProfile-to-OpenSSL-mode NORMATIVE mapping table.
//
// Re-emitted VERBATIM from 2g §4.5.1. The table is the contract 2h consumes;
// 2h MUST configure the OpenSSL SSL_CTX exactly as specified.
//
// | Profile       | OpenSSL `verify_mode`                                          | Local cert required          | CA anchors      | Pinset required | Pin check policy                                                                                                                                                                                                                          |
// |---------------|----------------------------------------------------------------|------------------------------|-----------------|-----------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
// | `mtls_pinned` | `SSL_VERIFY_PEER \| SSL_VERIFY_FAIL_IF_NO_PEER_CERT` (acceptor) / equivalent client-side cert presentation (initiator) | Yes (both ends)              | Optional (peer chain may be self-signed under FIXS RC1 strict pinning) | **Yes**         | **Mandatory** — peer cert SHA-256 MUST appear in the captured `pinset_snapshot` or `verify_peer` returns `tls_pin_mismatch`.                                                                                                                |
// | `mtls_ca`     | `SSL_VERIFY_PEER \| SSL_VERIFY_FAIL_IF_NO_PEER_CERT` (acceptor) / equivalent client-side (initiator)              | Yes (both ends)              | **Required**    | Optional        | If Pinset is provided AND non-empty, peer cert SHA-256 MUST appear in the snapshot OR be a CA-trusted leaf signed by an in-anchor CA; if Pinset is null or empty, CA-trust validation is the sole mechanism.                                |
// | `one_way_ca` (deprecated) | `SSL_VERIFY_PEER` for the acceptor's view of the server cert; initiator-side cert presentation is implementation-policy (FIXS legacy interop) | Initiator: no; acceptor: server cert required | **Required**    | **Forbidden** (null Pinset only) — `make_ssl_ctx_config` rejects a non-null Pinset under `one_way_ca` with `tls_invalid_security_profile` | n/a (no pinning)                                                                                                                                                                                                                            |
// | `unset`       | n/a — `make_ssl_ctx_config` rejects with `tls_invalid_security_profile` per [const §XII.5]'s no-implicit-default rule | n/a                          | n/a             | n/a             | n/a                                                                                                                                                                                                                                         |
//
// Note: the acceptor / initiator role split in column 2 is intentional per
// 2g §4.5.1 — `mtls_pinned` and `mtls_ca` rows BOTH carry the
// `(acceptor) / equivalent client-side (initiator)` qualification (the
// initiator does not consume `SSL_VERIFY_FAIL_IF_NO_PEER_CERT` because it
// presents — rather than verifies — its own cert; 2h's wiring distinguishes
// the role and applies the equivalent client-side cert presentation).
// `[2g §9 seam #11]` verifies row-by-row including this role split.
//
// For ALL profiles (except `unset`): min_proto_version = TLS1_2_VERSION;
// max_proto_version = TLS1_3_VERSION (Clarify Q1 / FR-014). 0-RTT/early-data
// is refused at compile time via CipherPolicy::banned_tokens (`0RTT` token).
// ============================================================================

// ============================================================================
// verify_peer — the validation predicate 2h's SSL_VERIFY_PEER callback calls.
// Re-emitted VERBATIM from 2g §4.5 lines 700-701.
//
// CRITICAL signature change vs the bundle's earlier shape (NEW-P1-1 +
// NEW-P1-2 + Codex P1-3 close):
//
//   1. ONE `peer_chain` parameter (not a leaf + chain pair) per 2g §4.5
//      line 700. The leaf is `peer_chain[0]`; the OpenSSL SSL_VERIFY_PEER
//      callback delivers ONE chain, not (leaf, chain) separately.
//
//   2. Pinning consumes `cfg.pinset_snapshot` (the captured snapshot 2h's
//      handshake wiring acquired ONCE at handshake start per §6.5.1
//      BINDING CONTRACT). verify_peer NEVER calls cfg.pinset->find /
//      contains / snapshot mid-verification.
// ============================================================================
//
// namespace fixpp::tls {
//
// class Certificate;     // defined in certificate.hpp
// struct peer_identity;  // defined in peer_identity.hpp
//
// // FR-020a / Clarify Q3 — SHORT-CIRCUIT first violation hit in this exact
// // order. Returns ONE error variant per failed handshake (no aggregate).
// // Below, `caps.X` is shorthand for the cert_source's Config::X field
// // reachable through `cfg.cs->config()` — there is NO `cfg.validation_caps`
// // field per NEW-P1-2 + [2g §4.2] line 320-326.
// //
// //   1. Per-cert DER size        ≤ caps.max_cert_der_bytes  → tls_cert_der_too_large
// //   2. RSA key lower bound      ≥ 2048                     → tls_handshake_failed (sub-reason "rsa_under_min" per 2g §6.6)
// //   3. RSA key upper bound      ≤ caps.max_rsa_key_bits    → tls_rsa_key_too_large
// //   4. ECDSA curve              ∈ {P-256, P-384}           → tls_handshake_failed (sub-reason "ecdsa_curve")
// //   5. Chain depth              ≤ caps.max_chain_depth     → tls_handshake_failed (sub-reason "chain_too_deep")
// //   6. SAN cardinality          ≤ caps.max_san_entries     → tls_san_entries_exceeded
// //   7. X.509 version            ∈ {v2, v3}                 → tls_handshake_failed (sub-reason "x509_v1")
// //   8. Expiration               leaf.not_before ≤ cfg.clock->now() ≤ leaf.not_after  → tls_handshake_failed (sub-reason "expired" or "not_yet_valid")
// //   9. Pinning (mtls_pinned)    scan cfg.pinset_snapshot for leaf.sha256_  → tls_pin_mismatch
// //  10. Cipher (if 2h delegates) CipherPolicy::is_allowed(negotiated)  → tls_cipher_not_allowed
// //
// // Per 2g §6.6: most rejection causes route through the `tls_handshake_failed`
// // GROUPING variant with a diagnostic-field sub-reason; only DoS-cap and
// // pinning rejections use dedicated variants
// // (tls_cert_der_too_large / tls_rsa_key_too_large /
// // tls_san_entries_exceeded / tls_pin_mismatch). spec.md FR-025 enumerates
// // the full variant list.
// //
// // Cert expiration evaluated against cfg.clock->now() which is the session-
// // scoped effective_clock per [2d §7.9] (D-9). The leaf's not_before /
// // not_after are X.509-envelope absolute wall-clock times (X.509 envelope
// // is the source of truth; cfg.clock evaluates the comparison) — see
// // NEW-P2-4 explanation in data-model.md.
// //
// // Pinning consumes cfg.pinset_snapshot per §6.5.1 BINDING CONTRACT — a
// // direct linear scan / hash on the captured snapshot, NEVER a call into
// // the live Pinset (NEW-P1-1 close).
// [[nodiscard]] core::expected_t<peer_identity>
//     verify_peer(SslCtxConfig const&           cfg,
//                 std::span<const Certificate>  peer_chain) noexcept;
//
// }  // namespace fixpp::tls

// ============================================================================
// Contract assertions (verified at /speckit-verify):
//
//   1. SecurityProfile has exactly 4 enumerators incl. `unset = 0` sentinel
//      and `one_way_ca` carrying [[deprecated]] on the ENUMERATOR DECLARATION
//      (FR-013 / [const §XII.5] / 2g §4.5 lines 650-656). NEW-P2-2 close: the
//      [[deprecated]] attribute placement is verified by [2g §9 seam #11]'s
//      try_compile witness with -Wdeprecated-declarations -Werror confirming
//      diagnostic emission when user code references SecurityProfile::one_way_ca.
//
//   2. make_ssl_ctx_config parameter order is (profile, cs, clock, pinset =
//      nullptr, mr = nullptr) per 2g §4.5 lines 681-686 (NEW-P1-2 close —
//      NOT the bundle's earlier (profile, source, pinset, clock,
//      validation_caps) shape).
//
//   3. SslCtxConfig carries `pinset_snapshot` (shared_ptr<const pin_snapshot>)
//      per NEW-P1-1 close. verify_peer NEVER calls cfg.pinset->find /
//      contains / snapshot during the verification window.
//
//   4. make_ssl_ctx_config(mtls_pinned, ..., empty_pinset, ...) returns
//      unexpect{tls_pin_empty_at_open} (Clarify Q2 / FR-025;
//      tests/tls/test_security_profile_empty_pinset.cpp).
//
//   5. make_ssl_ctx_config is a non-noexcept factory (may allocate the
//      SslCtxConfig copy); all expected_t<T>-returning factories are
//      [[nodiscard]] (FR-022).
//
//   6. verify_peer takes (SslCtxConfig const&, std::span<const Certificate>
//      peer_chain) per 2g §4.5 line 700-701 (Codex P1-3 close; NOT the
//      bundle's earlier (cfg, leaf, chain) split).
//
//   7. verify_peer short-circuits on the FIRST violation hit in the documented
//      order (FR-020a / Clarify Q3); test seam
//      tests/tls/test_verify_peer_t039.cpp ([2g §9 seam #14]) asserts
//      row-by-row.
//
//   8. verify_peer uses cfg.clock for expiration (D-9 / [2d §7.9]);
//      tests/tls/test_verify_peer_expiration.cpp drives via mock_clock.
//
//   9. verify_peer returns owning peer_identity on accept (peer_identity.hpp);
//      SAN strings + subject DN PMR-copied into peer_identity's owning storage
//      per 2g §4.5 lines 694-699.
//
//  10. Frozen-at-session-open enforced: there is no
//      Session::swap_security_profile(...) or Session::swap_cert_source(...)
//      API in v1.0 (FR-015 / FR-016).
//
//  11. The §4.5.1 row-by-row mapping (incl. acceptor / initiator role split
//      per the `mtls_pinned` and `mtls_ca` rows) is verified by [2g §9 seam #11]
//      tests/tls/test_security_profile_mapping.cpp.
