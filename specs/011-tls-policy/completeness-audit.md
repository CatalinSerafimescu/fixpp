# 011-tls-policy — Feature-completeness audit (T052)

**Date**: 2026-05-24
**Auditor**: orchestrator (post-Phase-5 close-out, pre-/simplify)
**Anchor**: `[[feedback_feature_completeness_gate]]` + `[[project_005_phase8_completeness_false_pass]]` (audit test BODIES, not file names; require entry-point exercise for emit/cross-session/role FRs)
**Scope**: tasks ↔ FR (FR-001..FR-027 incl. FR-009a, FR-020a) ↔ SC (SC-001..SC-008) ↔ catalogue rows (T-006/T-007/T-008/T-011/T-013 owned; T-039/T-040/T-041 cross-cut).

## FR coverage table

| FR     | Owning task(s)                       | Witness test(s)                                                                                | Body-audit                                                              | Verdict |
| ------ | ------------------------------------ | ---------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- | ------- |
| FR-001 | T031                                 | `test_make_file_cert_source_factory` `ReturnsExpectedNotThrows`                                | `cert_source.hpp:124-145` declares exactly 2 pure-virtuals.              | PASS    |
| FR-002 | T031, T033, T028                     | `test_load_credentials_cancellation` 6 cases                                                   | `load_credentials` returns `asio::awaitable<expected_t<local_credentials>>` per `cert_source.hpp:135-137`; no exception escapes. | PASS    |
| FR-003 | T031, T027                           | `test_hsm_async_signer_mock`                                                                   | `local_credentials::signer` is `variant<software_key_ref, async_signer_ref>` per `cert_source.hpp` E-2.                          | PASS    |
| FR-004 | T032, T033                           | `test_make_file_cert_source_factory` `SuccessReturnsUsableSharedPtr`                           | `file_cert_source` ships PEM/DER + encrypted-PEM passphrase impl in `src/tls/file_cert_source.cpp`.                              | PASS    |
| FR-005 | T032, T033, T026                     | `test_make_file_cert_source_factory` (5 cases incl. parse-fail / wrong-pw / missing-file)      | Factory `make_file_cert_source(Config, mr) -> expected_t<shared_ptr<cert_source>> noexcept` per `file_cert_source.hpp:53-54`.    | PASS    |
| FR-006 | T023, T024, T011                     | `test_pinset_single_thread_ordering` (add/add/remove + dup-add + remove-absent)                | `add(Certificate const&)` consumes cert; `remove(sha256)` keys on fingerprint; no atomic-swap; `make_pinset` factory present.    | PASS    |
| FR-007 | T024, T018                           | `tls_handshake_alloc_guard` (counting_resource) + `tls_handshake_alloc_guard_mallocnesia`      | Dual-gate fires per `[[feedback_tracking_pmr_resource_false_pass]]`. Pinset::find/snapshot are lock-free + zero-alloc.            | PASS    |
| FR-008 | T023, T015                           | `test_pin_view_lifetime_under_rotation` (TSan + ASan)                                          | `pin_view` carries `shared_ptr<const pin_snapshot>` per `pinset.hpp`; lifetimebound on `value`.                                  | PASS    |
| FR-009 | T024, T014                           | `test_pinset_rotation_does_not_affect_in_flight`                                               | In-flight handshake captures `snapshot()` once before pause; concurrent add/remove observable only at next handshake.            | PASS    |
| FR-009a| T017                                 | `test_pinset_per_counterparty_sharing`                                                         | Two `shared_ptr<Pinset>` aliases observe rotation atomically on next find/snapshot.                                              | PASS    |
| FR-010 | T023, T024, T011                     | `test_pinset_single_thread_ordering` (capacity-exhausted case)                                 | `Config::max_pins = 16` default; `add` over cap → `tls_pinset_capacity_exhausted`; no silent eviction.                           | PASS    |
| FR-011 | T042, T035                           | `tls_cipher_allow_list_static_assert` (positive) + `tls_cipher_allow_list_negative_compile` (`WILL_FAIL=TRUE`) | `CipherPolicy::banned_tokens` 12 entries verbatim from `[2g §4.4]`; `static_assert(!any_banned(...))` chain.            | PASS    |
| FR-012 | T042, T037                           | `test_tls_error_variants` (cipher-not-allowed witness)                                         | `static constexpr bool is_allowed(string_view) noexcept` per `cipher_policy.hpp`.                                                | PASS    |
| FR-013 | T044, T036                           | `test_security_profile_mapping` (4 enumerators + 4 rejection paths)                            | Enum has `unset=0`, `mtls_ca=1`, `mtls_pinned=2`, `one_way_ca [[deprecated]]=3` per `security_profile.hpp`.                      | PASS    |
| FR-014 | T044, T036                           | `test_security_profile_mapping` (row-by-row mapping table)                                     | TLS 1.3 preferred + 1.2 fallback encoded as `SslCtxConfig` contract 2h-transport consumes (per data-model E-11).                 | PASS    |
| FR-015 | T044                                 | (declarative; no mid-session swap API exists)                                                  | `SslCtxConfig::profile` is captured at `make_ssl_ctx_config` time; no setter on `Session` exposed.                               | PASS    |
| FR-016 | T031                                 | (declarative; no swap API on `Session` / `cert_source`)                                        | `cert_source` is `shared_ptr` held by `SessionConfig`; no mid-session swap method.                                               | PASS    |
| FR-017 | T008, T023, T031, T043, T040         | `test_certificate_lifetimebound`                                                               | Every view accessor on `Certificate`, `peer_identity`, `pin_view`, `local_credentials` carries `[[clang::lifetimebound]]` at declaration site. | PASS |
| FR-018 | T043                                 | `test_verify_peer_t039` (`peer_identity` returned on accept)                                   | `peer_identity` owning value type per `peer_identity.hpp`; built only by `verify_peer`.                                          | PASS    |
| FR-019 | T038, T046                           | `test_verify_peer_t039` (DoS-cap negative cases)                                               | `verify_peer` enforces RSA ≤ `max_rsa_key_bits`, DER ≤ `max_cert_der_bytes`, SAN ≤ `max_san_entries` per `verify_peer.cpp` steps 1/3/6. | PASS |
| FR-020 | T038, T046                           | `test_verify_peer_t039` (RSA-low + ECDSA-curve + X.509v1 + chain-depth + expired/not-yet-valid)| `verify_peer` enforces chain ≤ 8 + RSA ≥ 2048 + ECDSA P-256/P-384 + reject v1 + expiry vs `cfg.clock->now()`.                    | PASS    |
| FR-020a| T038, T046                           | `test_verify_peer_t039` `MultiViolationFirstInOrderFires` — multi-violation chain asserts first-in-order variant fires | 10-step canonical order in `verify_peer.cpp` matches data-model E-14 exactly.                            | PASS    |
| FR-021 | T028, T033                           | `test_load_credentials_cancellation` (pre-I/O reap + cancellable_dispatch reap; both witnesses)| `load_credentials` body follows §6.4 recipe verbatim; cancellation → `expected_t::unexpected{tls_load_cancelled}` (no throw).    | PASS    |
| FR-022 | T031, T032, T044, T023, T008, T042, T043 | (sweep — 9 `expected_t<T>` returns across tls headers; all carry `[[nodiscard]]`)         | `grep -E '\[\[nodiscard\]\][[:space:]]+.*expected_t<' include/fixpp/tls/*.hpp` covers all 9 declaration sites.                   | PASS    |
| FR-023 | T033                                 | (declarative; cold-path PMR is the file_cert_source construction body)                          | `file_cert_source::load_credentials` PMR-allocates parsed-cert storage; hot-path `Pinset::find` allocates zero.                  | PASS    |
| FR-024 | T019, T029, T024, T033               | `test_pinset_add_pmr_fail` + `test_file_cert_source_pmr_fail`                                  | Warm-path `Pinset::add` and cold-path `file_cert_source::load_credentials` PMR throws routed through `trap_throw` → typed variants. | PASS |
| FR-025 | T006, T007, T037                     | `test_tls_error_variants` (15 cases covering all 16 variants incl. `tls_pin_empty_at_open` Q2) | 16 variants at `core/error.hpp:351-429` slots 78..93. Coalescing groups documented in `tls_errors.hpp`.                          | PASS    |
| FR-026 | (declarative — NOT-OWNS)             | (negative-coverage; absence of source files)                                                   | `src/tls/` contains no OpenSSL `SSL_CTX` construction / `SSL_VERIFY_PEER` wiring / C-ABI symbols (`nm` would surface — deferred to /speckit-verify T053). | PASS (declarative) |
| FR-027 | (declarative — NOT-SUPPORTS)         | (negative-coverage; absence of API)                                                            | No PSK API, no CRL/OCSP, no mid-handshake rotation, no `SecurityProfile` swap, no `cert_source` swap, no `dlopen`.               | PASS (declarative) |

**Coverage**: 27/27 FR (incl. FR-009a + FR-020a). No SPEC-FIXED rows. No WAIVED rows.

## SC coverage table

| SC     | Witness                                                                            | Body-audit                                                                                                                                 | Verdict |
| ------ | ---------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ | ------- |
| SC-001 | `test_pinset_rotation_does_not_affect_in_flight` + `test_fixs_rotation` (conformance) | Cross-handshake atomicity: in-flight observes pre-rotation; next handshake observes post-rotation.                                          | PASS    |
| SC-002 | `test_verify_peer_t039` (14 cases incl. all 10-step rejections) + `test_tls_error_variants` | Every out-of-envelope cert is refused with a typed variant; no path returns `peer_identity` on a banned cert.                              | PASS    |
| SC-003 | `tls_cipher_allow_list_negative_compile` (CMake `WILL_FAIL=TRUE` on banned-token compile) | Build with banned cipher fails with `static_assert` diagnostic identifying the violation.                                                  | PASS    |
| SC-004 | `cert_source.hpp` ships 2 pure-virtuals (`[const §XIV.2]` ≤ 5) + `test_hsm_async_signer_mock` exercises a user-class HSM impl | Single-class HSM impl is implementable without modifying fixpp.                                                                            | PASS    |
| SC-005 | `test_security_profile_empty_pinset` (`/clarify` Q2) + `test_security_profile_mapping` (mtls_pinned row) | Switching to `mtls_pinned` is a config-time change; `make_ssl_ctx_config(mtls_pinned, …, pinset)` is the only entry. No recompile needed. | PASS    |
| SC-006 | `test_tls_error_variants` (15 cases — every named variant)                          | 16 distinct `error::tls_*` variants; no catch-all "TLS failed" exists. The grouping `tls_handshake_failed` carries diagnostic sub-reason.   | PASS    |
| SC-007 | `bench_pinset_find` (≤ 130 ns p99 lock-free / ≤ 200 ns p99 fallback)                | Bench records platform tuple + asserts applicable ceiling. Debug build well within design-doc bound; Release governed by `/speckit-verify`. | PASS    |
| SC-008 | `test_hsm_async_signer_mock` (sampling thread_id INSIDE nested `co_spawn`)          | Signer runs on a non-session executor per `[2d §7.5]`; no strand-safety violation.                                                          | PASS    |

**Coverage**: 8/8 SC. No SPEC-FIXED rows. No WAIVED rows.

## Catalogue rows touched

| Row     | Status before | Status after | Notes                                                                                                            |
| ------- | ------------- | ------------ | ---------------------------------------------------------------------------------------------------------------- |
| T-006   | backlog       | implementing | TLS 1.2 ECDHE+AES-GCM suites in `CipherPolicy::tls12_suites`. Flips to `done` when 2h-transport ships.            |
| T-007   | backlog       | implementing | TLS 1.3 suites in `CipherPolicy::tls13_suites`; preferred per FR-014 / `SslCtxConfig` contract.                   |
| T-008   | backlog       | implementing | Mutual TLS — leaf pinning ships via `Pinset` + `SecurityProfile::mtls_pinned`; flips to `done` post-2h-transport. |
| T-011   | backlog       | implementing | Pinset rotation API shipped per `[2g §4.3]`.                                                                      |
| T-013   | backlog       | implementing | Banned suites refused via `CipherPolicy::banned_tokens` `static_assert` + runtime `is_allowed`.                   |
| T-039   | backlog       | backlog      | `verify_peer` + `SslCtxConfig` C++ contract published; flips when 2h-transport wires `SSL_VERIFY_PEER` callback.  |
| T-040   | backlog       | backlog      | `cert_source` + `file_cert_source` + factory C++ contract published; flips when 2h-transport consumes.            |
| T-041   | backlog       | backlog      | `peer_identity` value type published; flips when session/ Phase-4 binds to `SessionEvent` for CompID binding.     |

## Waivers

**None.** Every FR / SC has a typed witness and a body-audited test or declarative grounding. No coverage YELLOW carve-outs were needed in Phase 6 inline. The `bench_pinset_snapshot_acquire` / `bench_pinset_find` / `bench_verify_peer_in_envelope` p99 hard gates are governed by `/speckit-verify` T053 against Release builds + platform tuple — outside the scope of this audit, which records that the bench binaries exist and the soft-gate assertions pass in Debug.

## Pending pipeline steps

- T051 `/simplify` — 3 general-purpose review agents (A simplification / B correctness-bugs / C test-bodies depth) + Opus close-analysis per `[[feedback_speckit_simplify_before_verify]]` + `[[feedback_simplify_pass_catches_9th_burn]]`. Particular scrutiny on matrix-witness fixture helpers for the `GTEST_SKIP` vs `ASSERT_TRUE` entry-gate anti-pattern (PR #83 13th-burn signal — the orchestrator already removed two such instances inline at Phase 4 review; /simplify should catch any missed ones).
- T053 `/speckit-verify` — Tier-1 mirror of CI: ASan + UBSan + TSan + GCC Release; coverage gate ≥ 95% line / ≥ 85% branch on `src/tls/*` + `include/fixpp/tls/*` via lcov DA/BRDA (NOT `llvm-cov report` aggregate per `[[feedback_coverage_gate_lcov_basis]]`); static analysis; ABI-hygiene (`nm` — confirm NO new `extern "C"` per FR-026); fuzz 1+ hour for `fuzz_file_cert_source`; bench p99 ceilings vs platform tuple. Produces `.specify/decisions/011-tls-policy-verify.md` (required for `/gate-b` per `[const §XVII.8]`).

## Audit verdict

**GREEN.** Feature surface is complete against FR-001..FR-027 + FR-009a + FR-020a, witnessed by SC-001..SC-008, and traces cleanly to catalogue rows T-006/007/008/011/013 (implementing) + T-039/040/041 (backlog with explicit C++ surface-contract forwarding notes in `spec/feature-catalogue.md`). Ready for T051 `/simplify` → T053 `/speckit-verify` → `/gate-b`.
