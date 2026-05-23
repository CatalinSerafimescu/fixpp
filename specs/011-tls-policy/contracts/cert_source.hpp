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
// (file_cert_source). Shapes below are re-emitted VERBATIM from 2g §4.1 / §4.2.
// Spec anchors: spec.md FR-001 (≤2 pure-virtuals), FR-002 (awaitable +
// expected_t), FR-003 (signer variant), FR-004 (file_cert_source default),
// FR-005 (make_file_cert_source factory), FR-021 (ASIO native cancellation),
// FR-022 ([[nodiscard]] on expected_t), FR-023 (cold-path PMR), FR-024
// (trap_throw routing).

#pragma once

// ============================================================================
// software_key_ref / async_signer_ref — re-emitted VERBATIM from 2g §4.1.
// ============================================================================
//
// namespace fixpp::tls {
//
// // software_key_ref — engine-internal handle to a parsed private key the
// // implementation owns. The engine never sees raw key bytes; 2h's OpenSSL
// // wiring binds the impl-owned EVP_PKEY* directly through this handle for
// // the EVP_DigestSign*/EVP_DigestVerify* fast path. Lifetime is bounded by
// // the holding cert_source instance.
// struct software_key_ref {
//     detail::private_key_handle handle;       // [[clang::lifetimebound]] on accessor; *this-bounded.
//     int                        ossl_pkey_id; // OpenSSL EVP_PKEY_id (e.g., EVP_PKEY_RSA, EVP_PKEY_EC).
// };
//
// // async_signer_ref — HSM-style awaitable signing oracle. The signer is
// // invoked on the session strand by 2h's wiring; the implementation MUST
// // suspend off-strand for any blocking work (network round-trip to HSM /
// // vault / KMS) by posting via [2d §6.5] cancellable_dispatch on a
// // non-session executor; the signing call resumes on the session strand.
// struct sign_request {
//     std::span<const std::byte> tbs [[clang::lifetimebound]];  // bytes-to-be-signed (TLS handshake digest); caller-owned.
//     int                        sig_alg;                       // OpenSSL NID for the signature algorithm.
// };
//
// struct sign_response {
//     std::pmr::vector<std::byte> signature;   // owning; PMR-allocated by the impl from the per-handshake arena.
// };
//
// struct async_signer_ref {
//     using sign_fn = std::function<
//         asio::awaitable<core::expected_t<sign_response>>(
//             sign_request const& req)>;
//     sign_fn sign;                            // never invoked synchronously on the session strand without a cancellable_dispatch hop.
// };
//
// // local_credentials — one value carrying everything OpenSSL's SSL_CTX
// // configuration needs at handshake time. Returned by load_credentials();
// // lifetime is bounded by the cert_source instance that produced it (the
// // chain's Certificate views, the leaf's view, and software_key_ref's
// // handle alias *this-owned storage).
// //
// // CRITICAL: per 2g §4.1 lines 252-254, `leaf` is a VALUE-typed Certificate
// // with [[clang::lifetimebound]] (the value's view fields alias the
// // cert_source storage) and `chain` is a std::span<const Certificate>
// // VIEW with [[clang::lifetimebound]] — NOT a std::pmr::vector<Certificate>
// // owning vector. This preserves the 2g §4.6 ownership rule: the chain's
// // Certificate views are *this-bounded by the cert_source instance.
// struct local_credentials {
//     Certificate                                     leaf [[clang::lifetimebound]];
//     std::span<const Certificate>                    chain [[clang::lifetimebound]];   // root last; view into cert_source-owned storage.
//     std::variant<software_key_ref, async_signer_ref> signer;                          // software handle OR awaitable HSM oracle.
// };
//
// }  // namespace fixpp::tls

// ============================================================================
// cert_source — pluggable interface (EXACTLY 2 pure-virtuals; [const §XIV.2]
// cap = 5). Re-emitted VERBATIM from 2g §4.1 lines 259-290.
// ============================================================================
//
// namespace fixpp::tls {
//
// class cert_source {
//  public:
//     virtual ~cert_source() = default;
//
//     // (1) Load the local credentials bundle (leaf cert + chain + signer).
//     //     Awaitable so that fetch-from-vault / HSM-blocking / file-on-
//     //     network-fs impls can suspend without blocking the session strand.
//     //     Cancellation follows ASIO's native slot model per [const §XI.2] /
//     //     [SYN §3.2 Q6a]; on cancellation_type::total the awaitable
//     //     completes with expected_t::unexpected{error::tls_load_cancelled}
//     //     (§6.4 publishes the cancellable_dispatch recipe verbatim — this
//     //     is the second pluggable awaitable after [2d §4.1.1] Clock::sleep_until
//     //     and inherits the same recipe-publication obligation).
//     //     The returned local_credentials' view fields are *this-bounded;
//     //     the [[clang::lifetimebound]] annotations on local_credentials::
//     //     leaf / chain / signer's software_key_ref::handle are
//     //     declaration-site facts at the abstract base per [arch §5.5] /
//     //     [2b §6.4] precedent.
//     [[nodiscard]] virtual
//     asio::awaitable<core::expected_t<local_credentials>>
//     load_credentials() = 0;
//
//     // (2) Trust anchors (CA certs) used for SecurityProfile::mtls_ca and
//     //     SecurityProfile::one_way_ca. The default file_cert_source returns
//     //     a chain loaded from Config::ca_bundle_path. Implementations that
//     //     do not provide CA trust (FIXS RC1 strict pinned-only deployments
//     //     under SecurityProfile::mtls_pinned) MAY return an empty span and
//     //     the SecurityProfile adapter (§4.5) verifies that mtls_pinned is
//     //     in effect before accepting empty trust anchors.
//     //     Lifetimebound at the abstract base per [arch §5.5] / [2b §6.4].
//     //
//     //     RETURN TYPE IS expected_t<std::span<const Certificate>>, NOT
//     //     std::span<const Certificate> — per 2g §4.1 line 288 (round-3 P1-1
//     //     close codified the expected_t return so the FIXS RC1 strict
//     //     pinned-only error case can be surfaced without a separate failure
//     //     channel).
//     [[nodiscard]] virtual
//     core::expected_t<std::span<const Certificate>>
//     load_trust_anchors() [[clang::lifetimebound]] = 0;
// };
//
// }  // namespace fixpp::tls

// ============================================================================
// file_cert_source — default file-path impl. Re-emitted VERBATIM from 2g §4.2
// lines 314-369.
// ============================================================================
//
// namespace fixpp::tls {
//
// class file_cert_source final : public cert_source {
//  public:
//     struct Config {
//         std::string                    leaf_path;       // PEM or DER (auto-detect by file extension + magic byte).
//         std::string                    chain_path;      // PEM bundle; intermediates only or leaf+intermediates (deduped on load).
//         std::string                    private_key_path;// PEM or DER; PKCS#8 or RFC1421-style.    ← restored per Codex P1-1 (sub-finding 2) + 2g §4.2 line 319.
//         std::string                    ca_bundle_path;  // PEM bundle for CA trust anchors. May be empty for SecurityProfile::mtls_pinned.
//         std::function<std::string()>   password_cb;     // Optional; called once at construction-time per [arch §5.3] carve-out only. May be empty.
//         std::pmr::memory_resource*     mr {nullptr};    // PMR resource for parsed-cert storage. nullptr → engine default; make_file_cert_source's mr parameter takes precedence over the field.
//         std::size_t                    max_chain_depth{8};            // §1.1 cap (chain depth).
//         std::size_t                    max_rsa_key_bits{8192};        // §1.1 DoS cap — RSA upper bound (lower bound 2048 from [FIXS §3.4]).
//         std::size_t                    max_cert_der_bytes{16 * 1024}; // §1.1 DoS cap — per-cert ASN.1 envelope size.
//         std::size_t                    max_san_entries{64};           // §1.1 DoS cap — SAN list cardinality.
//     };
//
//     // [arch §6] rule-4 factory entry point. Returns expected_t<...> for
//     // non-construction-time callers (2i C ABI, future hot-reload). The
//     // factory accepts the PMR resource as an explicit parameter (rule 4
//     // requires it) and routes Config::mr through the parameter; either the
//     // field or the parameter may be null but not both (otherwise
//     // expected_t::unexpected{error::tls_cert_load_failed}). Parse failures
//     // surface as expected_t::unexpected{error::tls_cert_parse_failed} or
//     // tls_cert_load_failed; no exception is thrown by the factory.
//     [[nodiscard]] static core::expected_t<std::shared_ptr<cert_source>>
//         make_file_cert_source(Config cfg, std::pmr::memory_resource* mr) noexcept;
//
//     // Direct-construction path for in-process C++ callers that prefer
//     // throw-on-failure (per [arch §5.3]'s construction-time carve-out).
//     // Construction loads and parses every file once. Once construction
//     // returns, every cert_source method is exception-free per [arch §5.3]
//     // hot-path discipline.
//     explicit file_cert_source(Config cfg);
//
//     ~file_cert_source() override;
//
//     // Overrides — view-returning methods carry [[clang::lifetimebound]]
//     // mirroring the abstract-base declaration site annotations (RC#1).
//     [[nodiscard]] asio::awaitable<core::expected_t<local_credentials>>
//         load_credentials() override;
//
//     [[nodiscard]] core::expected_t<std::span<const Certificate>>
//         load_trust_anchors() [[clang::lifetimebound]] override;
//
//  private:
//     // Config                                  cfg_;
//     // std::pmr::memory_resource*              mr_;             // Resolved at construction (Config::mr ?: engine default).
//     // std::pmr::vector<Certificate>           chain_;          // PMR-allocated; lifetime = *this.
//     // std::pmr::vector<Certificate>           trust_anchors_;  // PMR-allocated; lifetime = *this.
//     // Certificate                             leaf_;           // Aliases chain_.front() or carries its own storage.
//     // detail::private_key_handle              key_;            // OpenSSL EVP_PKEY*; impl-owned RAII; software_key_ref aliases this.
// };
//
// }  // namespace fixpp::tls

// ============================================================================
// §6.4 Cancellation contract — `load_credentials` recipe.
//
// Re-emitted verbatim from 2g §6.4 lines 903-944 (the "Recipe — every
// implementation of `load_credentials` (and `async_signer_ref::sign`) MUST
// follow this body shape" prose). Per NEW-P2-5 in the Opus adversarial
// review: 2g §6.4 publishes the recipe verbatim and binds every
// load_credentials implementer to follow it; an implementer reading only
// the bundle needs this recipe present here, NOT just a cite. The /speckit-
// implement phase-implementer-sonnet agent works from this bundle, not from
// 2g; without the recipe in the bundle, a half-correct cancellation impl
// (that passes the obvious test but fails the §6.4 "cancellation arrives
// BETWEEN call and first suspension" reaping case) is the likely failure
// mode.
//
// Recipe (every implementation of cert_source::load_credentials and
// async_signer_ref::sign MUST follow this body shape):
//
//   asio::awaitable<core::expected_t<local_credentials>>
//   file_cert_source::load_credentials() {
//       // 1. Read the awaiter's bound executor and recover the project-owned
//       //    fixpp::core::session_executor wrapper per [2d §4.8]. For non-
//       //    session contexts (e.g., engine bootstrap), the wrapper is the
//       //    engine's default-cert-source executor; for in-session contexts,
//       //    it is the session_executor for the session that initiated the
//       //    load.
//       auto exec = co_await asio::this_coro::executor;
//
//       // 2. Read the awaiter's cancellation_state per [SYN §3.2 Q6a] /
//       //    [const §XI.2].
//       auto cs = co_await asio::this_coro::cancellation_state;
//
//       // 3. Reap pre-I/O cancellation: if the slot already shows total,
//       //    complete immediately with tls_load_cancelled. THIS IS THE
//       //    "BETWEEN-CALL-AND-FIRST-SUSPENSION" REAP — load-bearing for the
//       //    §6.4 binding contract.
//       if (cs.cancelled() != asio::cancellation_type::none)
//           co_return core::expected_t<local_credentials>{
//               unexpect, error::tls_load_cancelled };
//
//       // 4. For any disk/network/HSM work, post the handoff via [2d §6.5]
//       //    cancellable_dispatch on a non-session executor (or, for already-
//       //    cached state, just return the prepared local_credentials
//       //    directly). cancellable_dispatch returns awaitable<expected_t<void>>;
//       //    expected_t::unexpected{dispatch_aborted} from the dispatch maps
//       //    to tls_load_cancelled at this boundary.
//       auto dispatched = co_await fixpp::core::cancellable_dispatch(
//           exec, cs.slot(),
//           [this]() { /* parse cached bytes; build local_credentials */ });
//
//       if (!dispatched)
//           co_return core::expected_t<local_credentials>{
//               unexpect, error::tls_load_cancelled };
//
//       co_return core::expected_t<local_credentials>{ /* the built value */ };
//   }
//
// The recipe is enforced by `[2g §9 seam #13]`
// (`tests/tls/test_load_credentials_cancellation.cpp`) under both
// `per_session_strand` and `direct_executor` modes per `[2d §4.8]`.
// ============================================================================

// ============================================================================
// Contract assertions (verified at /speckit-verify against the actual code):
// ============================================================================
//
//   1. cert_source has EXACTLY 2 pure-virtual methods (FR-001 / 2g §4.1).
//   2. load_credentials returns asio::awaitable<core::expected_t<local_credentials>>
//      (FR-002 / 2g §4.1).
//   3. load_trust_anchors returns core::expected_t<std::span<const Certificate>>
//      (NOT bare std::span) per 2g §4.1 line 288 (Codex P1-1 sub-finding).
//   4. file_cert_source::Config carries `private_key_path` per 2g §4.2 line 319
//      (Codex P1-1 sub-finding 2).
//   5. local_credentials::leaf is `Certificate [[clang::lifetimebound]]` (value
//      type with declaration-site annotation); local_credentials::chain is
//      `std::span<const Certificate> [[clang::lifetimebound]]` (VIEW, not
//      owning vector) per 2g §4.1 lines 252-254 (Codex P1-1 sub-finding 3).
//   6. local_credentials::signer is variant<software_key_ref, async_signer_ref>
//      (FR-003 / 2g §4.1).
//   7. async_signer_ref carries the sign_fn awaitable callable per 2g §4.1
//      (NOT a bare asio::any_io_executor + signature_algorithm field shape).
//   8. file_cert_source ships in v1.0 (FR-004 / 2g §4.2).
//   9. make_file_cert_source returns expected_t<shared_ptr<cert_source>>
//      noexcept (FR-005 / 2g §4.2 line 337-338).
//  10. All expected_t<T>-returning methods are [[nodiscard]] (FR-022 / 2g §4.1
//      footer "Every expected_t<T>-returning method declared in §§4.1–4.5
//      carries [[nodiscard]]").
//  11. load_credentials honours ASIO native cancellation per the §6.4 recipe
//      VERBATIM (FR-021 / 2g §6.4). [2g §9 seam #13] witnesses.
//  12. PMR throws inside load_credentials surface as tls_pinset_alloc_failed
//      / tls_cert_load_failed via [2a §4.2] trap_throw (FR-024 / 2g §6.1
//      paragraph at line 862). No exception escapes the public surface.
//  13. load_trust_anchors return carries [[clang::lifetimebound]] at the
//      ABSTRACT BASE declaration site (FR-017 / [arch §5.5] / [2b §6.4]).
