// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-001..005 + FR-021..024.
//
// This is a CONTRACT SHAPE ORACLE, not an implementation header. The actual
// declarations land in include/fixpp/tls/cert_source.hpp +
// include/fixpp/tls/file_cert_source.hpp during /speckit-implement; this file
// documents the binding contract that implementation must satisfy.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.1 (cert_source) + §4.2
// (file_cert_source).
// Spec anchors: spec.md FR-001 (≤2 pure-virtuals), FR-002 (awaitable +
// expected_t), FR-003 (signer variant), FR-004 (file_cert_source default),
// FR-005 (make_file_cert_source factory), FR-021 (ASIO native cancellation),
// FR-022 ([[nodiscard]] on expected_t), FR-023 (cold-path PMR), FR-024
// (trap_throw routing).

#pragma once

// ============================================================================
// cert_source — pluggable interface (≤2 pure-virtuals; [const §XIV.2] cap=5)
// ============================================================================
//
// namespace fixpp::tls {
//
// struct local_credentials;            // E-2 (data-model.md)
// struct software_key_ref;             // E-3
// struct async_signer_ref;             // E-4
// class  Certificate;                  // E-11
//
// class cert_source {
//  public:
//     struct Config {
//         std::filesystem::path                        leaf_path;
//         std::filesystem::path                        chain_path;
//         std::filesystem::path                        ca_bundle_path;
//         std::function<expected_t<std::string>()>     password_callback;   // optional
//         std::size_t                                  max_rsa_key_bits   = 8192;
//         std::size_t                                  max_cert_der_bytes = 16 * 1024;
//         std::size_t                                  max_san_entries    = 64;
//         std::size_t                                  max_chain_depth    = 8;
//     };
//
//     cert_source() = default;
//     virtual ~cert_source() = default;
//     cert_source(cert_source const&)            = delete;
//     cert_source& operator=(cert_source const&) = delete;
//     cert_source(cert_source&&)                 = delete;
//     cert_source& operator=(cert_source&&)      = delete;
//
//     // Cold-path. Awaitable. May allocate via PMR. Honours ASIO native
//     // cancellation slots per [2d §6.5] cancellable_dispatch recipe.
//     // Cancellation completes with unexpected{tls_load_cancelled}.
//     [[nodiscard]] virtual
//     asio::awaitable<expected_t<local_credentials>>
//     load_credentials() = 0;
//
//     // Hot-path-adjacent (called at SSL_CTX build time; not steady-state).
//     // Returns a non-owning view; the underlying Certificate storage is
//     // owned by the cert_source impl. [[clang::lifetimebound]] at the
//     // declaration site per [arch §5.5] / [2b §6.4].
//     [[nodiscard]] virtual
//     std::span<const Certificate>
//     load_trust_anchors() const noexcept [[clang::lifetimebound]] = 0;
// };
//
// }  // namespace fixpp::tls
//
// ============================================================================
// local_credentials — value type returned by load_credentials
// ============================================================================
//
// namespace fixpp::tls {
//
// enum class signature_algorithm : std::uint8_t {
//     ecdsa_p256,
//     ecdsa_p384,
//     rsa_pss_sha256,
//     rsa_pss_sha384,
//     rsa_pss_sha512,
// };
//
// struct software_key_ref {
//     std::shared_ptr<void>  key_handle;   // EVP_PKEY* under file_cert_source default
//     signature_algorithm    alg;
// };
//
// class async_signer;  // user-side; see contracts/cert_source.hpp commentary
//
// struct async_signer_ref {
//     std::shared_ptr<async_signer>  impl;
//     asio::any_io_executor          executor;   // [2d §7.5] strand-safety boundary
//     signature_algorithm            alg;
// };
//
// struct local_credentials {
//     Certificate                                     leaf;
//     std::pmr::vector<Certificate>                   chain;     // cold-path PMR
//     std::variant<software_key_ref, async_signer_ref> signer;   // FR-003
// };
//
// }  // namespace fixpp::tls
//
// ============================================================================
// file_cert_source — default impl + factory
// ============================================================================
//
// namespace fixpp::tls {
//
// class file_cert_source final : public cert_source {
//     // Implementation detail — load_credentials reads PEM/DER from
//     // cfg_.leaf_path + cfg_.chain_path + cfg_.ca_bundle_path. Password
//     // callback used iff the PEM is encrypted. PMR-allocated parse output.
//     // load_trust_anchors returns the CA bundle parsed at construction.
//
//  public:
//     // Construction-time errors (bad path / malformed PEM at engine
//     // bootstrap before any session is open) MAY throw per [arch §5.3]
//     // carve-out. The factory below wraps that boundary in expected_t<...>.
//     explicit file_cert_source(Config cfg, std::pmr::memory_resource* mr);
//
//     [[nodiscard]] asio::awaitable<expected_t<local_credentials>>
//         load_credentials() override;
//     [[nodiscard]] std::span<const Certificate>
//         load_trust_anchors() const noexcept [[clang::lifetimebound]] override;
// };
//
// // FR-005 — the [arch §6] rule-4 factory entry point.
// // Returns unexpected{tls_load_failed} for any cold-path file / parse error.
// // PMR allocator is caller-owned; MUST outlive the returned cert_source.
// [[nodiscard]]
// expected_t<std::shared_ptr<cert_source>>
// make_file_cert_source(cert_source::Config cfg, std::pmr::memory_resource* mr) noexcept;
//
// }  // namespace fixpp::tls
//
// ============================================================================
// Contract assertions (verified at /speckit-verify against the actual code):
// ============================================================================
//
//   1. cert_source has EXACTLY 2 pure-virtual methods (FR-001).
//   2. load_credentials returns asio::awaitable<expected_t<local_credentials>>
//      (FR-002).
//   3. local_credentials::signer is variant<software_key_ref, async_signer_ref>
//      (FR-003).
//   4. async_signer_ref::executor is asio::any_io_executor (FR-003 / [2d §7.5]).
//   5. file_cert_source ships in v1.0 (FR-004).
//   6. make_file_cert_source returns expected_t<shared_ptr<cert_source>>
//      (FR-005); the function is noexcept.
//   7. All expected_t<T>-returning methods are [[nodiscard]] (FR-022).
//   8. load_credentials honours ASIO native cancellation (FR-021); test seam
//      tests/tls/test_load_credentials_cancellation.cpp witnesses this.
//   9. PMR throws inside load_credentials surface as tls_load_failed via
//      [2a §4.2] trap_throw (FR-024). No exception escapes the public surface.
//  10. load_trust_anchors return carries [[clang::lifetimebound]] at the
//      ABSTRACT BASE declaration site (FR-017 / [arch §5.5] / [2b §6.4]).
