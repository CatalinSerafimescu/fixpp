// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-017 + Key Entity E-11.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 (Certificate value-type) +
// §4.6 (lifetime annotation rules) + [arch §5.5] (declaration-site
// lifetime annotation).
// Spec anchors: spec.md FR-017 ([[clang::lifetimebound]] on abstract base
// declaration site per [2b §6.4] precedent), data-model.md E-11.

#pragma once

// ============================================================================
// Certificate — value-typed view over a parsed peer cert.
//
// Owns NONE of the underlying DER bytes. The cert_source impl owns the raw
// DER storage; Certificate carries spans + string_views back-pointing into
// that storage. All non-owning view accessors carry [[clang::lifetimebound]]
// at THIS declaration site (the abstract-base-equivalent surface) per the
// [2b §6.4] precedent v0.2 RC#1 close codified.
// ============================================================================
//
// namespace fixpp::tls {
//
// using pin_fingerprint = std::array<std::byte, 32>;
//
// enum class ecdsa_curve : std::uint8_t {
//     unspecified = 0,
//     p256        = 1,
//     p384        = 2,
//     // Note: p521 etc. NOT enumerated — verify_peer rejects with
//     // tls_cert_invalid (curve sub-reason) at the curve-check step.
// };
//
// class Certificate {
//  public:
//     // ── Identity ────────────────────────────────────────────────
//     [[nodiscard]] std::string_view  subject_dn() const noexcept [[clang::lifetimebound]];
//     [[nodiscard]] std::string_view  issuer_dn()  const noexcept [[clang::lifetimebound]];
//
//     // ── SAN entries (DNS names + URIs, cap-bounded by max_san_entries) ──
//     [[nodiscard]] std::span<const std::string_view> san_dns_names() const noexcept [[clang::lifetimebound]];
//     [[nodiscard]] std::span<const std::string_view> san_uris()      const noexcept [[clang::lifetimebound]];
//
//     // ── Fingerprint (SHA-256-of-raw_der; the Pinset key per Clarify Q4) ─
//     [[nodiscard]] pin_fingerprint  sha256() const noexcept;     // value-return; no lifetimebound needed
//
//     // ── Algorithm + key ─────────────────────────────────────────
//     [[nodiscard]] signature_algorithm  alg() const noexcept;
//     [[nodiscard]] std::size_t          rsa_key_bits() const noexcept;   // 0 if not RSA-PSS
//     [[nodiscard]] ecdsa_curve          curve() const noexcept;          // unspecified if not ECDSA
//
//     // ── X.509 metadata ──────────────────────────────────────────
//     [[nodiscard]] int                       x509_version() const noexcept;  // 1, 2, or 3
//     [[nodiscard]] std::chrono::system_clock::time_point  not_before() const noexcept;
//     [[nodiscard]] std::chrono::system_clock::time_point  not_after()  const noexcept;
//
//     // ── Raw DER back-pointer (debugging / pinning recompute) ────
//     [[nodiscard]] std::span<const std::byte>  raw_der() const noexcept [[clang::lifetimebound]];
//
//  private:
//     // std::span<const std::byte>                raw_der_;
//     // std::string_view                          subject_dn_;
//     // std::string_view                          issuer_dn_;
//     // std::span<const std::string_view>         san_dns_names_;
//     // std::span<const std::string_view>         san_uris_;
//     // pin_fingerprint                           sha256_;
//     // signature_algorithm                       alg_;
//     // std::size_t                               rsa_key_bits_ = 0;
//     // ecdsa_curve                               curve_         = ecdsa_curve::unspecified;
//     // int                                       x509_version_;
//     // std::chrono::system_clock::time_point     not_before_;
//     // std::chrono::system_clock::time_point     not_after_;
// };
//
// }  // namespace fixpp::tls
//
// ============================================================================
// Contract assertions (verified at /speckit-verify):
//
//   1. Every non-owning view accessor on Certificate carries
//      [[clang::lifetimebound]] at this declaration site (FR-017 / [arch §5.5]
//      / [2b §6.4]); tests/tls/test_certificate_lifetimebound.cpp drives a
//      -Wdangling smoke test.
//   2. sha256() returns the SHA-256 of raw_der() (Pinset key consistency).
//   3. Certificate owns no DER storage; raw_der().data() back-points into
//      cert_source-owned bytes.
//   4. x509_version_ ∈ {1, 2, 3} (v1 is parsed but rejected at verify_peer).
//   5. san_dns_names() + san_uris() combined cardinality ≤ max_san_entries
//      after parse (the cert_source parser enforces; verify_peer re-checks
//      with the cfg.validation_caps bounds).
