# Tasks: 011 — TLS Policy Core

**Input**: Design documents from `specs/011-tls-policy/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/, quickstart.md
**Design anchor**: `.specify/2g-tls.md` v0.4 (Phase-2 Gate A r3 converged 2026-05-09); Phase-4 Gate A converged round 4 (user-signed-off 2026-05-24) — record at `library/.specify/decisions/011-tls-policy-gatea.md`.

**Tests**: TDD is mandatory per `[const §VII.1]` / `[const §VII.3]`. Test seams are re-emitted by NAME from `[2g §9]`'s 18 binding seams (not re-derived from FRs) per plan.md NEW-P1-3 close. Plus 2 PMR-failure seams (Gate A round-3 carry-forward P3 — procedurally deferred to /speckit-tasks scope-expansion per phase-4.md Track Log) + 1 /clarify-Q2 seam + 2 FR-aligned contract-binding witnesses.

**Organization**: 3 user stories from spec.md — US1 (P1, MVP: Pinset rotation), US2 (P2: cert_source plugin), US3 (P3: hardened-by-default trust mode). Foundational types `Certificate` + `error::tls_*` enum extensions block all stories.

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Maps task to a user story (US1, US2, US3)
- File paths are relative to the **library submodule root** (`research/G19-fix-fpml-iso20022/library/`)

## Path Conventions

`include/fixpp/tls/`, `src/tls/`, `tests/tls/`, `tests/conformance/`, `tests/fuzz/`, `bench/tls/`, `spec/`, `tools/`, `docs/src/`. NEW module — no prior code in `include/fixpp/tls/` or `src/tls/`; this feature establishes it from scratch (`plan.md` Structure Decision).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Wire the new `tls/` module into the build / layer system BEFORE any code lands. Per `[[feedback_gate_b_check_layers_post_fixer]]` — `tools/check_layers.py` registration MUST precede `/speckit-implement`.

- [X] T001 Confirm `tls/` module registration is already present in `tools/check_layers.py` (line 24: `"tls": {"core"}`; line 25 `transport` and line 30 `capi` already list `tls` as a dependency, per `[arch §4.6]`); confirm `python tools/check_layers.py` runs clean against the empty `include/fixpp/tls/` tree; update the registration ONLY if 011's actual consumed-by relationships diverge from the pre-existing `core/`-only `requires` set
- [X] T002 [P] Add `openssl/3.6.2` Conan row to `conanfile.txt` / equivalent build dependency manifest (version already pinned in `conanfile.py:23` Phase-4 comment — "4.x is breaking, not yet on Conan"); verify CMake `find_package(OpenSSL 3.0 REQUIRED)` succeeds; record vetting decision in `research.md` Outstanding section if any Conan-row change requires escalation
- [X] T003 [P] Create new-module directory tree: `include/fixpp/tls/`, `src/tls/`, `tests/tls/`, `tests/tls/fixtures/`, `tests/conformance/`, `tests/fuzz/`, `bench/tls/` (empty `.gitkeep` only — no source files yet); verify each directory exists via `test -d <path>` before marking T003 complete (load-bearing for T011..T020a + T021/T022 + T026..T030 + T035..T041 which write files into these trees)
- [X] T004 Append `add_subdirectory(src/tls)`, `add_subdirectory(tests/tls)`, `add_subdirectory(tests/conformance)`, `add_subdirectory(tests/fuzz)`, `add_subdirectory(bench/tls)` to top-level `CMakeLists.txt`; create minimal `CMakeLists.txt` stubs in each new directory (no targets yet)
- [X] T005 [P] Author `tests/tls/fixtures/Makefile` driving `openssl` CLI to regenerate every PEM/DER fixture enumerated in `plan.md` Scale/Scope (CA + leaf RSA-2048 + leaf RSA-1024 negative + leaf RSA-16384 negative + leaf ECDSA P-256 + leaf ECDSA P-521 negative + leaf X.509 v1 negative + leaf SAN-64 + leaf SAN-65 negative + leaf DER-16KiB negative + chain depth-8 + chain depth-9 negative + leaf expired against `mock_clock` baseline 2026-01-01 + encrypted-PEM passphrase `"test"`); generate fixtures once and check binaries into the repo

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Ship the shared types every user story consumes — the `error::tls_*` enum extensions + `Certificate` value-type view. Per data-model E-12: `Certificate` is the input to both `Pinset::add(Certificate const&)` (US1) and `verify_peer(SslCtxConfig const&, std::span<const Certificate> peer_chain)` (US3).

**⚠️ CRITICAL**: No user-story work can begin until this phase completes.

- [X] T006 Append 16 `error::tls_*` variants to `include/fixpp/core/error.hpp` at slots 78..93 (next contiguous range after slot 77 per `[[project_2e_design_doc_only_seqnum_handoff]]` + plan.md project-structure note); names re-emitted verbatim from `[2g §6.6]` per data-model E-15: `tls_cert_load_failed`, `tls_cert_parse_failed`, `tls_cipher_not_allowed`, `tls_invalid_security_profile`, `tls_sign_callback_unavailable`, `tls_pin_empty_at_open`, `tls_pin_not_found`, `tls_pin_already_present`, `tls_pinset_capacity_exhausted`, `tls_pinset_alloc_failed`, `tls_handshake_failed`, `tls_rsa_key_too_large`, `tls_cert_der_too_large`, `tls_san_entries_exceeded`, `tls_pin_mismatch`, `tls_load_cancelled`
- [X] T007 [P] Create `include/fixpp/tls/tls_errors.hpp` re-exporting the 16 variants under `fixpp::tls::errors::` aliases for ergonomic at-site use; document the per-doc-prefix `FIXPP_ERR_TLS_*` C-ABI coalescing groups (CONFIG / HANDSHAKE / PINSET / RUNTIME / CANCELLED) per data-model E-15 + `[2g §6.6]` lines 1006-1013
- [X] T008 [P] Create `include/fixpp/tls/certificate.hpp` declaring the `Certificate` value type per data-model E-12 (fields `raw_der_`, `subject_dn_`, `issuer_dn_`, `san_dns_names_`, `san_uris_`, `sha256_`, `x509_version_`, `not_before_`, `not_after_`, `alg_`, `rsa_key_bits_`, `curve_`); every non-owning view accessor carries `[[clang::lifetimebound]]` at the declaration site per `[arch §5.5]` + `[2b §6.4]` precedent (D-11)
- [X] T009 [P] Create `src/tls/certificate.cpp` implementing the DER parse helpers — subject DN extraction, issuer DN, SAN list (DNS + URI), SHA-256-of-raw-DER, X.509 version, signature algorithm + RSA key bits / ECDSA curve discovery; route OpenSSL ASN.1 errors through `[2a §4.2]` `trap_throw` and surface as `error::tls_cert_parse_failed`
- [X] T010 Wire `src/tls/certificate.cpp` + `include/fixpp/tls/*.hpp` into the `src/tls` `CMakeLists.txt` library target; run `python tools/check_layers.py` and confirm PASS for the new module; run `cmake --build` and confirm clean compile under Clang Debug + GCC Release sanity

**Checkpoint**: Foundation ready — user stories can now begin in parallel (US1 / US2 / US3 each consume `Certificate` + `error::tls_*`).

---

## Phase 3: User Story 1 — Counterparty cert rotation without disconnects (Priority: P1) 🎯 MVP

**Goal**: Ship the FIXS-RC1 §5 add-then-remove Pinset rotation surface — mid-session-mutable per `[arch §5.6]`, lock-free reader path on the handshake hot path per `[const §VIII.5]`, per-counterparty granularity recommended per FR-009a / Q5. `Pinset::add(Certificate const&)` + `Pinset::remove(std::array<std::byte, 32> const&)` + `Pinset::find(...) -> pin_view` + `Pinset::snapshot()` per data-model E-5 / E-6 / E-7 / E-8.

**Independent Test** (per spec.md): with two `SessionConfig`s targeting the same counterparty sharing one `Pinset` instance, in `mtls_pinned` mode, run a continuous handshake loop against a peer presenting the *old* cert, then `add(new)`, then `remove(old)`, then run the same loop against a peer presenting the *new* cert. Both sessions transition atomically; an in-flight handshake captured mid-rotation completes against whichever set it started with.

### Tests for User Story 1 (write FIRST — must FAIL before implementation)

- [X] T011 [P] [US1] Author `tests/tls/test_pinset_single_thread_ordering.cpp` (`[2g §9 seam #9]`) — Pinset single-thread `add`/`add`/`remove` + duplicate-`add` (→ `tls_pin_already_present`) + remove-absent (→ `tls_pin_not_found`) + `pin_view::found()` true/false correctness
- [X] T012 [P] [US1] Author `tests/tls/test_pinset_add_then_remove_deterministic.cpp` (`[2g §9 seam #1]`) — hook-driven non-flaky deterministic add-then-remove ordering witness against a deterministic harness (no thread scheduling dependency)
- [X] T013 [P] [US1] Author `tests/tls/test_pinset_add_then_remove_stress.cpp` (`[2g §9 seam #2]`) — TSan stress: thread A runs `add`/`remove` loop; thread B runs `find` loop; verify no UAF / data race under TSan + ASan
- [X] T014 [P] [US1] Author `tests/tls/test_pinset_rotation_does_not_affect_in_flight.cpp` (`[2g §9 seam #15]`) — mid-session rotation does NOT affect in-flight handshake per `[2g §6.5.1]` BINDING CONTRACT; the handshake captures `Pinset::snapshot()` ONCE before pause; concurrent `add`/`remove` on a separate thread; in-flight handshake observes pre-rotation snapshot; next handshake observes post-rotation snapshot
- [X] T015 [P] [US1] Author `tests/tls/test_pin_view_lifetime_under_rotation.cpp` (`[2g §9 seam #16]`) — `pin_view` lifetime under concurrent rotation; thread A holds `pin_view` from `find()`; thread B issues `add+remove` repeatedly; assert under ASan + TSan that `pin_view::value` dereference remains valid for the view's lifetime regardless of subsequent removes
- [X] T016 [P] [US1] Author `tests/tls/test_pinset_snapshot_outlives_pinset.cpp` (`[2g §9 seam #18]`, round-3 P1-1 close) — post-`~Pinset()` snapshot lifetime under the `Pinset::Config::mr` outlives-snapshot contract per data-model E-5 / `[2g §4.6]`; construct `Pinset` against scope-outer `monotonic_buffer_resource`; add 2 pins, snapshot once, drop `Pinset`; exercise read-only access on `pin::subject_dn` / `san_dns`; companion negative test where MR scope is INSIDE `Pinset` scope MUST surface ASan UAF
- [X] T017 [P] [US1] Author `tests/tls/test_pinset_per_counterparty_sharing.cpp` (FR-009a / Q5) — two `SessionConfig`s sharing one `shared_ptr<Pinset>` see rotation atomically across both on the next handshake; no per-session torn read
- [X] T018 [P] [US1] Author `tests/tls/test_tls_handshake_alloc_guard.cpp` (`[2g §9 seam #7]`) — zero-alloc on the handshake-hot path under the dual gate: (a) `counting_resource` (PMR routing) AND (b) mallocnesia LD_PRELOAD (global-malloc interception per `[[reference_mallocnesia_path]]` + `[[feedback_tracking_pmr_resource_false_pass]]` — counting_resource alone misses non-PMR `std::vector` escapes); covers `Pinset::snapshot` + `Pinset::find` + `verify_peer` paths
- [X] T019 [P] [US1] Author `tests/tls/test_pinset_add_pmr_fail.cpp` (Gate A round-3 carry-forward P3 — warm-path PMR-failure witness procedurally deferred from Gate A round 2/3 to /speckit-tasks per phase-4.md) — `Pinset::add` snapshot-clone PMR allocation throw routes through `[2a §4.2]` `trap_throw` and surfaces as `error::tls_pinset_alloc_failed`; complements seam #12 synthesise-side coverage with a dedicated trap_throw-boundary witness
- [X] T020a [P] [US1] Author `tests/conformance/mock_transport.hpp` (+ `mock_transport.cpp` if non-trivial) declaring `MockTransport` — a minimal canned-byte replay harness for T020. Surface: constructor takes an ordered sequence of `(direction, bytes)` pairs (`send` / `recv`) recorded from a real OpenSSL handshake for both old and new certs; runtime asserts each call matches the expected direction + payload and returns the recorded peer bytes. Pure in-process, no sockets, no executor. Cap ≤200 LoC per `[[feedback_phase_implementer_sonnet_runaway_scope]]`; consumed ONLY by T020 — do NOT generalise to a multi-test fixture. Companion fixture file: `tests/conformance/fixtures/rotation_handshake.bytes` (or per-cert sub-files) generated by an `openssl s_client` capture script committed alongside.
- [X] T020 [US1] Author `tests/conformance/test_fixs_rotation.cpp` (`[2g §9 seam #3]`, sequenced after T020a) — FIXS-RC1 §5 recorded rotation scenario driven end-to-end against the `MockTransport` replay harness from T020a, replaying canned handshake bytes for both old and new certs; verifies the cross-handshake atomicity contract
- [X] T021 [P] [US1] Author `bench/tls/bench_pinset_snapshot_acquire.cpp` (`[2g §9 seam #4]`) — platform-conditional latency ceiling on `Pinset::snapshot()`: ≤ 30 ns p99 on the libstdc++ ≥ 16 / libc++ lock-free floor; ≤ 100 ns p99 on the libstdc++ ≤ 15 internal-mutex fallback per `[2g §6.3]`; record platform tuple (compiler / stdlib / stdlib-version / cpu-supports-cmpxchg16b) and select ceiling at runtime
- [X] T022 [P] [US1] Author `bench/tls/bench_pinset_find.cpp` (`[2g §9 seam #5]`) — latency regression on `Pinset::find(sha256)` at `max_pins = 16`: ≤ 130 ns p99 on lock-free floor; ≤ 200 ns p99 on libstdc++ ≤ 15 fallback; SC-007's "≤ 130 ns p99 full find" is gated here

### Implementation for User Story 1

- [X] T023 [US1] Create `include/fixpp/tls/pinset.hpp` declaring `Pinset` + `Pinset::Config` + `pin` + `using pin_snapshot = std::pmr::vector<pin>;` + `pin_view`; API re-emitted verbatim from `[2g §4.3]` lines 437-518 — `add(Certificate const&) -> expected_t<void>`, `remove(std::array<std::byte, 32> const&) -> expected_t<void>`, `find(std::array<std::byte, 32> const&) const noexcept -> pin_view`, `contains(...) const noexcept -> bool`, `snapshot() const noexcept -> shared_ptr<const pin_snapshot>`; `Config::max_pins {16}` + `Config::mr {nullptr}` with the lifetime contract pinned per data-model E-5. ALSO declare the free factory `[[nodiscard]] expected_t<shared_ptr<Pinset>> make_pinset(Pinset::Config cfg, std::pmr::memory_resource* mr) noexcept;` per `contracts/pinset.hpp:119` — mirrors the `make_file_cert_source` factory pattern (T032) so 2i C-ABI callers + future hot-reload paths never see a throwing constructor
- [X] T024 [US1] Create `src/tls/pinset.cpp` implementing `Pinset` against the design-doc invariants: `std::shared_mutex` writer + `atomic<std::shared_ptr<const pin_snapshot>>` reader publication (single acquire-load on `find` / `snapshot` per `[2g §6.5]`); `add` PMR-copies the `Certificate`'s SHA-256 + subject_dn + SAN list at `add()` time per `[2g §4.3]` lines 482-487; writer holds the mutex ONLY during publication of the new snapshot, readers NEVER block; `remove` does NOT need diagnostic context (keys on the fingerprint only); rationale for `std::shared_mutex` choice is the CONSOLIDATED inheritance pin at `[2g §6.5.2]` — cite-and-stop per NEW-P2-6 close (do NOT re-derive at impl site). ALSO implement the `make_pinset(Config, mr*)` factory body: wrap construction in `[2a §4.2]` `trap_throw` so any PMR-allocation throw surfaces as `expected_t::unexpected{tls_pinset_alloc_failed}` (NEVER thrown across the factory return), success returns `expected_t{shared_ptr<Pinset>{...}}` per the T023 declaration
- [X] T025 [US1] Wire `pinset.cpp` into `src/tls` `CMakeLists.txt`; register every US1 test/bench target in `tests/tls/CMakeLists.txt` + `bench/tls/CMakeLists.txt` + `tests/conformance/CMakeLists.txt` (depends on T003 having created `bench/tls/` + `tests/conformance/`); run the full US1 test list (T011..T020 + T020a helper) GREEN under ASan + UBSan + TSan + GCC Release sanity; run the US1 bench list (T021, T022) and confirm the platform-conditional p99 ceiling per `[const §VIII.2]`

**Checkpoint**: US1 fully functional and independently testable. The FIXS-RC1 §5 rotation surface is shippable as an MVP slice if /implement chooses to stop here.

---

## Phase 4: User Story 2 — Plug a custom credential source without recompiling (Priority: P2)

**Goal**: Ship the `cert_source` pluggable interface (2 pure-virtuals, well under `[const §XIV.2]` ≤ 5 cap) + the `file_cert_source` v1.0 reference impl + the `make_file_cert_source(Config, mr) -> expected_t<shared_ptr<cert_source>>` factory that wraps the `[arch §5.3]` construction-time-throw carve-out. HSM/KMS/vault impls are user-side per `[const §XII.8]`; this feature publishes the contract per data-model E-1 / E-1a / E-2 / E-3 / E-4.

**Independent Test** (per spec.md): provide a stub `cert_source` impl whose `load_credentials()` returns an `async_signer_ref` pointing at an isolated executor. Open a TLS session; observe that signing-callback work runs on the operator's executor (not the session strand), the session opens successfully, and the file-default impl is never instantiated.

### Tests for User Story 2 (write FIRST — must FAIL before implementation)

- [X] T026 [P] [US2] Author `tests/tls/test_make_file_cert_source_factory.cpp` (`[2g §9 seam #17]`, RC#2 close) — factory parity: (a) `make_file_cert_source` returns `expected_t<shared_ptr<cert_source>>` with NO exception thrown across the session-handling boundary; (b) parse-failure surfaces `tls_cert_parse_failed`; (c) load-failure (missing file / unreadable / wrong password) surfaces `tls_cert_load_failed`; (d) success returns a `shared_ptr` whose `load_credentials()` / `load_trust_anchors()` mirror the throwing-constructor `file_cert_source` directly; companion positive-compile witness that 2i's C-ABI bridge stub calls `make_file_cert_source` (not the throwing constructor) through the factory's `expected_t<...>` shape
- [X] T027 [P] [US2] Author `tests/tls/test_hsm_async_signer_mock.cpp` (`[2g §9 seam #8]`) — HSM-style `async_signer_ref` mock; verify signing routes off the session strand via `cancellable_dispatch` per the `[2g §6.4]` recipe + `[2d §6.5]` precedent; sample `std::this_thread::get_id()` INSIDE the dispatched coroutine (per `[[feedback_asio_post_resume_bounces_to_spawn_executor]]` — `co_await asio::post` does NOT pin the resume to the other executor; use nested `asio::co_spawn(other_exec, fn, use_awaitable)` and put the sampling inside `fn`)
- [X] T028 [P] [US2] Author `tests/tls/test_load_credentials_cancellation.cpp` (`[2g §9 seam #13]`) — FR-021 ASIO cancellation seam; recipe per `contracts/cert_source.hpp §6.4 Cancellation contract` (published verbatim from `[2g §6.4]` per T031 / NEW-P2-5 close) under both `per_session_strand` and `direct_executor` modes per `[2d §4.8]`; injects probe at recipe step 4 (cancellable_dispatch) and asserts the dispatch is observed and reaped; cancellation completes with `expected_t::unexpected{tls_load_cancelled}` (NOT a thrown exception); pre-I/O cancellation reap (recipe step 3 — the "between-call-and-first-suspension" reap) is the load-bearing case
- [X] T029 [P] [US2] Author `tests/tls/test_file_cert_source_pmr_fail.cpp` (Gate A round-3 carry-forward P3 — cold-path PMR-failure witness procedurally deferred from Gate A round 2/3 to /speckit-tasks per phase-4.md) — `file_cert_source::load_credentials` PMR allocation throw on file-I/O + parse + chain-build routes through `[2a §4.2]` `trap_throw` and surfaces as `error::tls_cert_load_failed`; complements seam #12 synthesise-side coverage with a dedicated cold-path trap_throw-boundary witness
- [X] T030 [P] [US2] Author `tests/fuzz/fuzz_file_cert_source.cpp` (`[2g §9 seam #6]`, `[const §IX.4]`) — libFuzzer-driven random PEM/DER inputs feeding `file_cert_source` construction; ASan + UBSan invariants; verify no crash / UAF / UB on adversarial inputs; corpus seeded from `tests/tls/fixtures/`

### Implementation for User Story 2

- [X] T031 [P] [US2] Create `include/fixpp/tls/cert_source.hpp` declaring the abstract `cert_source` interface per data-model E-1 (2 pure-virtuals re-emitted verbatim from `[2g §4.1]` lines 277-290: `[[nodiscard]] virtual asio::awaitable<core::expected_t<local_credentials>> load_credentials() = 0;` AND `[[nodiscard]] virtual core::expected_t<std::span<const Certificate>> load_trust_anchors() [[clang::lifetimebound]] = 0;`); declare `local_credentials` (E-2 — `leaf [[clang::lifetimebound]]`, `chain [[clang::lifetimebound]]` VIEW NOT owning vector, `signer` variant), `software_key_ref` (E-3), `async_signer_ref` (E-4), `sign_request` + `sign_response`; publish the `§6.4 Cancellation contract — load_credentials recipe` section verbatim per D-13 / NEW-P2-5 close (steps 1-4 from `[2g §6.4]` lines 903-944)
- [X] T032 [P] [US2] Create `include/fixpp/tls/file_cert_source.hpp` declaring `file_cert_source` (concrete impl of `cert_source`) + `file_cert_source::Config` per data-model E-1a (verbatim from `[2g §4.2]` lines 316-327: `leaf_path` / `chain_path` / `private_key_path` / `ca_bundle_path` / `password_cb` / `mr` / `max_chain_depth {8}` / `max_rsa_key_bits {8192}` / `max_cert_der_bytes {16 * 1024}` / `max_san_entries {64}`); declare the factory `[[nodiscard]] expected_t<shared_ptr<cert_source>> make_file_cert_source(Config, std::pmr::memory_resource*);`
- [X] T033 [US2] Create `src/tls/file_cert_source.cpp` implementing `file_cert_source` (PEM/DER auto-detect on `leaf_path` + `chain_path` + `private_key_path` + `ca_bundle_path`; chain dedupe on load; encrypted-PEM password callback invoked once at construction-time per `[arch §5.3]` carve-out; chain build) AND the `make_file_cert_source` factory body that wraps the construction-time throw in `expected_t<...>` so non-construction-time callers (2i C ABI, future hot-reload) NEVER see a thrown exception; cancellation per the `[2g §6.4]` recipe inside `load_credentials`
- [X] T034 [US2] Wire `file_cert_source.cpp` into `src/tls` `CMakeLists.txt`; register every US2 test target in `tests/tls/CMakeLists.txt` + the fuzz target in `tests/fuzz/CMakeLists.txt`; run the full US2 test list (T026..T030) GREEN; run the fuzz harness for 1+ hour confirming no new crashes per `[const §IX.4]`

**Checkpoint**: US2 functional. HSM/KMS/vault integrations are now implementable as a single user-supplied class without modifying fixpp (SC-004).

---

## Phase 5: User Story 3 — Hardened-by-default trust mode (Priority: P3)

**Goal**: Ship the `CipherPolicy` compile-time allow-list + the `SecurityProfile` enum + the `SslCtxConfig` value type + the `make_ssl_ctx_config` factory + the `verify_peer` validation predicate + the `peer_identity` value (T-041 handoff to session/). Compile-time refusal of banned ciphers/TLS versions makes the runtime contract cheap (the hot path never re-checks what `static_assert` proved). `verify_peer` short-circuits on the first violation hit in the canonical 10-step order per FR-020a / `/clarify` Q3.

**Independent Test** (per spec.md): (a) attempt to build the engine with a non-allowed cipher constant wired into `CipherPolicy`; verify the build fails with a diagnostic identifying the violation. (b) Open a session against a peer presenting an RSA-1024 cert; verify handshake refuses with `error::tls_handshake_failed` carrying diagnostic sub-reason `"rsa_under_min"`. (c) Open a session against a peer presenting a cert with > `max_san_entries` SAN entries; verify refusal with `error::tls_san_entries_exceeded`.

### Tests for User Story 3 (write FIRST — must FAIL before implementation)

- [X] T035 [P] [US3] Author `tests/tls/test_cipher_allow_list_static_assert.cpp` + companion CMake `try_compile` harness in `tests/tls/CMakeLists.txt` (`[2g §9 seam #10]`, Codex P2-2 + NEW-P1-3 close) — NEGATIVE-COMPILE test that attempts to declare a `CipherPolicy` variant including `"ECDHE-RSA-AES128-CBC-SHA256"` (CBC banned per `[const §XII.3]`); verify the build FAILS with the expected `static_assert` message. Companion positive `try_compile` confirms the published allow-list arrays compile clean
- [X] T036 [P] [US3] Author `tests/tls/test_security_profile_mapping.cpp` (`[2g §9 seam #11]`, NEW-P1-3 sub-bullet close + NEW-P2-2 close) — `SecurityProfile`-to-OpenSSL-mode row-by-row mapping per `[2g §4.5.1]` (each row of the 4-row table verified incl. the acceptor/initiator role split for `mtls_pinned` and `mtls_ca` rows + the four rejection paths: `unset` / `mtls_pinned`-with-null-`Pinset` / `one_way_ca`-with-non-null-`Pinset` / null-clock). Additionally verifies the `SecurityProfile::one_way_ca [[deprecated]]` diagnostic emission via a `try_compile` witness with `-Wdeprecated-declarations -Werror`
- [X] T037 [P] [US3] Author `tests/tls/test_tls_error_variants.cpp` (`[2g §9 seam #12]`) — exercise every `error::tls_*` variant from data-model E-15 (16 variants total — 15 from `[2g §6.6]` + the `tls_pin_empty_at_open` Q2 amendment) via synthesised failing inputs; assert returned `expected_t::unexpected` carries the expected variant; for `tls_handshake_failed` (the grouping variant) assert the diagnostic field carries the expected sub-reason (`"expired"`, `"not_yet_valid"`, `"rsa_under_min"`, `"sigalg_disallowed"`, `"ecdsa_curve"`, `"chain_too_deep"`, `"x509_v1"`)
- [X] T038 [P] [US3] Author `tests/tls/test_verify_peer_t039.cpp` (`[2g §9 seam #14]`; also satisfies `contracts/security_profile.hpp` assertion 8 — expiration witness — superseding that oracle's `test_verify_peer_expiration.cpp` filename reference, content fully covered here) — T-039 cert-parameter validation: each rejection criterion (RSA < 2048, RSA > `max_rsa_key_bits`, ECDSA non-P-256/P-384, X.509 v1, expired vs `cfg.clock`, not-yet-valid, SHA-1 sig, banned sig_alg, DER > 16 KiB, SAN > 64, chain depth > 8) returns `expected_t::unexpected` of the right variant. Includes the FR-020a short-circuit-first-violation-hit witness per `/clarify` Q3 (canonical 10-step order: DER size → RSA-low → RSA-high → ECDSA-curve → chain-depth → SAN-card → X.509-version → expiration → pinning → cipher); construct multi-violation chains and assert ONLY the FIRST-in-order variant fires
- [X] T039 [P] [US3] Author `tests/tls/test_security_profile_empty_pinset.cpp` (`/clarify` Q2) — `make_ssl_ctx_config(SecurityProfile::mtls_pinned, cs, clock, empty_pinset)` returns `unexpected{tls_pin_empty_at_open}`; the session never opens; distinguishes operator-config error from peer-cert error
- [X] T040 [P] [US3] Author `tests/tls/test_certificate_lifetimebound.cpp` (FR-017) — `-Wdangling` smoke test against the abstract-base declaration site for every `[[clang::lifetimebound]]`-annotated accessor on `Certificate`, `local_credentials`, `pin_view`, `peer_identity`; per the `[arch §5.5]` / `[2b §6.4]` precedent the override at the concrete impl does NOT re-declare the attribute
- [X] T041 [P] [US3] Author `bench/tls/bench_verify_peer_in_envelope.cpp` — `verify_peer` p99 ceilings per `[2g §6.3]`: ≤ 1 ms p99 for an in-envelope chain (depth ≤ 4, RSA-2048 or ECDSA-P-256, ≤ 8 SAN entries); DoS-reject paths (RSA-16384 / DER-too-large / SAN-too-many) MUST short-circuit ≤ 50 µs p99 — the bound is the whole point of the cap

### Implementation for User Story 3

- [X] T042 [P] [US3] Create `include/fixpp/tls/cipher_policy.hpp` declaring `CipherPolicy` per data-model E-9 verbatim from `[2g §4.4]` lines 553-604: `tls13_suites` (3 entries — `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256`), `tls12_suites` (6 entries — ECDHE-(RSA|ECDSA) × (AES128-GCM | AES256-GCM | CHACHA20-POLY1305)), `kx_groups` (3 — `X25519`, `secp256r1`, `secp384r1`), `sig_algs` (4 — `ECDSA+SHA256`, `ECDSA+SHA384`, `RSA-PSS+SHA256`, `RSA-PSS+SHA384`), `banned_tokens` (12 — `RC4`, `DES`, `3DES`, `MD5`, `DH_anon`, `NULL`, `EXPORT`, `TLS_RSA`, `CBC`, `SHA1`, `TLS_AES_128_CCM`, `0RTT`); `static_assert(!any_banned(...))` over every allow-list; `[[nodiscard]] static constexpr bool is_allowed(std::string_view) noexcept`
- [X] T043 [P] [US3] Create `include/fixpp/tls/peer_identity.hpp` declaring `peer_identity` per data-model E-13 verbatim from `[2g §4.5]` lines 706-724 — owning value type with PMR-allocated `subject_dn`, `san_dns_names_owned`, `san_uris_owned`, `leaf_fingerprint`, `not_after`; accessors carry `[[clang::lifetimebound]]` bound to `*this`
- [X] T044 [P] [US3] Create `include/fixpp/tls/security_profile.hpp` declaring `SecurityProfile` enum (4 enumerators per E-10 — `unset = 0`, `mtls_ca = 1`, `mtls_pinned = 2`, `one_way_ca [[deprecated(...)]] = 3` with the attribute on the enumerator declaration itself, NOT in a comment) + `SslCtxConfig` (E-11: `profile`, `cs`, `pinset`, `pinset_snapshot` per NEW-P1-1, `clock`, `ciphers`, `mr`) + the `make_ssl_ctx_config` factory signature verbatim from `[2g §4.5]` lines 681-686 (NEW-P1-2 close — parameter order `(profile, cs, clock, pinset = nullptr, mr = nullptr)`, NO `validation_caps` parameter — DoS caps reach `verify_peer` via `cs->config()`) + the `verify_peer` free-function signature `[[nodiscard]] core::expected_t<peer_identity> verify_peer(SslCtxConfig const& cfg, std::span<const Certificate> peer_chain) noexcept;` per E-14
- [X] T045 [US3] Create `src/tls/security_profile.cpp` implementing `make_ssl_ctx_config`: table validation per `[2g §4.5.1]` 4 rows; reject `SecurityProfile::unset` → `tls_invalid_security_profile`; reject null `cs` → `tls_invalid_security_profile`; reject null `clock` → `tls_invalid_security_profile`; reject `mtls_pinned` + null `pinset` → `tls_invalid_security_profile`; reject `mtls_pinned` + non-null EMPTY `pinset` → `tls_pin_empty_at_open` (`/clarify` Q2); reject `one_way_ca` + non-null `pinset` → `tls_invalid_security_profile` (`[2g §4.5.1]` row 3); TLS-version posture per FR-014 (`SSL_CTX_set_min_proto_version = TLS1_2_VERSION`, `SSL_CTX_set_max_proto_version = TLS1_3_VERSION`) is encoded as part of the `SslCtxConfig` contract the 2h-transport adapter consumes
- [X] T046 [US3] Create `src/tls/verify_peer.cpp` implementing the 10-step short-circuit per FR-020a + data-model E-14 + `[2g §6.5.1]` BINDING CONTRACT: (1) per-cert DER → `tls_cert_der_too_large`; (2) RSA-low → `tls_handshake_failed` sub-reason `"rsa_under_min"`; (3) RSA-high → `tls_rsa_key_too_large`; (4) ECDSA curve → `tls_handshake_failed` sub-reason `"ecdsa_curve"`; (5) chain depth → `tls_handshake_failed` sub-reason `"chain_too_deep"`; (6) SAN cardinality → `tls_san_entries_exceeded`; (7) X.509 version → `tls_handshake_failed` sub-reason `"x509_v1"`; (8) expiration vs `cfg.clock->now()` → `tls_handshake_failed` sub-reason `"expired"` / `"not_yet_valid"`; (9) pinning under `mtls_pinned`: scan `cfg.pinset_snapshot` for `peer_chain[0].sha256()` per `[2g §6.5.1]` — NEVER call `cfg.pinset->find/contains/snapshot` mid-verification; violation → `tls_pin_mismatch`; (10) `CipherPolicy::is_allowed` → `tls_cipher_not_allowed`. On accept: build `peer_identity` from `peer_chain[0]` PMR-copying the subject DN + SANs into owning storage (PMR resource = `cfg.mr` or engine default)
- [X] T047 [US3] Wire `security_profile.cpp` + `verify_peer.cpp` + every US3 header into `src/tls` `CMakeLists.txt`; register every US3 test target in `tests/tls/CMakeLists.txt` + the bench target in `bench/tls/CMakeLists.txt`; run the full US3 test list (T035..T041) GREEN under ASan + UBSan + TSan + Coverage + GCC Release sanity; run T041 bench and confirm `verify_peer` p99 ceiling per `[const §VIII.2]`

**Checkpoint**: All three user stories independently functional. The `tls/` module surface is complete; 2h-transport can now consume `SslCtxConfig` + `verify_peer`; session-FSM Phase-4 can now consume `peer_identity` for the T-041 binding.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Catalogue + coverage-index updates per `[[feedback_feature_completeness_gate]]`; operator docs; `/simplify` pre-`/speckit-verify` per `[[feedback_speckit_simplify_before_verify]]` + `[[feedback_simplify_pass_catches_9th_burn]]`; completeness audit; `/speckit-verify` execution.

- [X] T048 [P] Append `011-tls-policy` row + flip T-006 / T-007 / T-008 / T-011 / T-013 to `implementing` in `spec/feature-catalogue.md`. Leave T-039 / T-040 as `backlog` with a forwarding note: "C++ surface contract published by 011-tls-policy (`SslCtxConfig` + `verify_peer` + `peer_identity`); rows will flip when 2h-transport consumes them at handshake-wiring time." Leave T-041 as `backlog` with a forwarding note: "C++ surface contract published by 011-tls-policy (`peer_identity` value type); row will flip when session/ Phase-4 binds `peer_identity` to `SessionEvent`."
- [X] T049 [P] Append the 011 ledger + rotate the Active feature pointer in `spec/coverage-index.md`
- [X] T050 [P] Author `docs/src/tls-quickstart.md` mirroring `specs/011-tls-policy/quickstart.md` (Scenarios A/B/C/D/E) for mdBook publication; reference path corrected to `../../.specify/architecture.md` per Codex round-1 P3-2 close
- [X] T051 Run `/simplify` across the implementation diff per `[[feedback_speckit_simplify_before_verify]]` — 3 general-purpose review agents with non-overlapping foci (A simplification / B correctness-bugs / C test-bodies depth) + Opus close-analysis; never trust agent severities raw per `[[feedback_simplify_pass_catches_9th_burn]]` (4 burns in 4 features confirms load-bearing); particularly scrutinise matrix-witness fixture helpers for the `GTEST_SKIP` vs `ASSERT_TRUE` entry-gate anti-pattern (PR #83 13th-burn signal)
- [X] T052 Run feature-completeness audit per `[[feedback_feature_completeness_gate]]` — tasks ↔ FR (FR-001..FR-027) ↔ SC (SC-001..SC-008) ↔ catalogue rows (T-006/T-007/T-008/T-011/T-013 owned + T-039/T-040 cross-cut with 2h-transport + T-041 cross-cut into session/); document any waivers in `plan.md ## Gate B` with the `[const §VI]` rationale; audit test BODIES not file names per `[[project_005_phase8_completeness_false_pass]]`; pay special attention to FRs that involve cross-session or role assertions. INCLUDE an explicit FR-022 `[[nodiscard]]` sweep: grep every `expected_t<` return type in `include/fixpp/tls/*.hpp` and assert each carries `[[nodiscard]]` at the declaration site (factory + interface + concrete impls); record any miss as a SPEC-FIXED row in the audit table — **GREEN** per `specs/011-tls-policy/completeness-audit.md` (27/27 FR + 8/8 SC; no waivers; FR-022 sweep clean across all 9 `expected_t<T>` declaration sites in `include/fixpp/tls/*.hpp`)
- [X] T053 Run `/speckit-verify` — execute the Tier-1 mirror of CI: sanitizer matrix (ASan + UBSan + TSan + GCC Release sanity) per `[const §IX.2]`; coverage gate ≥95% line / ≥85% branch on `src/tls/*` + `include/fixpp/tls/*` per `[const §IX.1]` using lcov DA/BRDA not the llvm-cov-report aggregate per `[[feedback_coverage_gate_lcov_basis]]` (record any YELLOW carve-out per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` precedent — `file_cert_source.cpp` I/O fault paths anticipated as candidates); static-analysis pass (clang-tidy + check_layers.py PASS for `tls/`); ABI-hygiene: `nm` confirms no NEW `extern "C"` symbols per `[const §X.2]` (011 emits no C ABI; 2i bridges); alloc-discipline: T018 mallocnesia + counting_resource dual-gate GREEN; fuzz: T030 runs 1+ hour clean per `[const §IX.4]`; bench: T021 + T022 + T041 p99 ceilings green per platform tuple per `[const §VIII.2]`; abidiff N/A (no C-ABI surface added); produce `.specify/decisions/011-tls-policy-verify.md` (Tier-1 mirror evidence required for `/gate-b` per `[const §XVII.8]` paired-evidence rule)

**Checkpoint**: Feature ready for `/gate-b` PR review. Per phase-4.md Next gate row: `/gate-b` (Codex hostile PR review) → merge → step 19 close-out.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: T001 must complete before T010 (check_layers.py registration required by the Foundational compile-gate); T002 must complete before T031..T033 (OpenSSL link required); T003 + T004 enable every subsequent task that creates files in those directories; T005 (fixtures) must complete before T011..T020 (US1 tests), T026..T030 (US2 tests), T035..T041 (US3 tests).
- **Phase 2 (Foundational)**: T006 + T007 + T008 + T009 → T010 (compile-gate); BLOCKS all user-story work.
- **Phase 3 (US1)**: depends on Phase 2 (Foundational `Certificate` + `error::tls_*` variants).
- **Phase 4 (US2)**: depends on Phase 2; INDEPENDENT of US1 (parallel-staffable).
- **Phase 5 (US3)**: depends on Phase 2 + Phase 4 (US3 tests / impl reference `cert_source` via `SslCtxConfig::cs`). US3 can be authored against a stub `cert_source` mock if US2 not yet shipped, but practical TDD ordering favours US2-before-US3.
- **Phase 6 (Polish)**: depends on US1 + US2 + US3 all shippable; `/speckit-verify` (T053) is the Tier-1 gate before `/gate-b`.

### User Story Dependencies

- **US1 (P1)**: foundationally independent — needs only `Certificate` + `error::tls_*` from Phase 2. MVP-shippable on its own (Pinset rotation is a usable surface even without `verify_peer` wiring).
- **US2 (P2)**: foundationally independent of US1 — needs only `Certificate` + `error::tls_*`.
- **US3 (P3)**: references `cert_source` via `SslCtxConfig::cs`. Practical TDD ordering: US2 first so US3 tests can construct real `cert_source` instances rather than mocks (the AC scenarios all assume real `cert_source` wiring).

### Within Each User Story

- **TDD ordering** (mandatory per `[const §VII.1]` / `[const §VII.3]`): every test task in the "Tests for User Story X" section MUST be authored and FAIL before any implementation task in the same phase. Per `[[feedback_subagent_phase_verification_two_traps]]` — guard against SUCCEED-placeholder tests and FSM-transition-skipped-but-end-state-matches.
- Within US1: T020a (`MockTransport` replay-harness infrastructure) must land before T020 can compile (T020a is a helper, NOT a contract-binding test — TDD-FAIL gate does not apply); T011..T020 must compile + FAIL before T023 + T024 land; T021 + T022 (benches) author before T024.
- Within US2: T026..T030 must compile + FAIL before T031..T033 land.
- Within US3: T035..T041 must compile + FAIL before T042..T046 land.

### Parallel Opportunities

- **Phase 1**: T002 / T003 / T004 / T005 parallelisable after T001 lands.
- **Phase 2**: T006 / T007 / T008 parallelisable; T009 depends on T008 (`Certificate` header); T010 is the compile-gate sequenced last.
- **Phase 3 (US1)**: T011..T019, T020a, T021, T022 are in different files, all parallelisable; T020 sequences AFTER T020a (depends on the `MockTransport` helper); T023 + T024 sequential (impl); T025 sequential (wire-up + run).
- **Phase 4 (US2)**: every test task T026..T030 parallelisable; T031 + T032 parallelisable (different headers); T033 depends on both; T034 sequential.
- **Phase 5 (US3)**: every test task T035..T041 parallelisable; T042 + T043 + T044 parallelisable (different headers); T045 + T046 parallelisable (different `.cpp` files); T047 sequential.
- **Phase 6 (Polish)**: T048 / T049 / T050 parallelisable; T051 / T052 / T053 sequential (each depends on the previous step's output landing).

### Across-Story Parallelism

If team capacity allows: once Phase 2 closes, Developers can take US1 / US2 / US3 in parallel; each is independently testable per `spec.md` Independent Test sections. US3 should drag US2 by a beat for the practical-test-fixture reason above.

---

## Parallel Example: User Story 1 tests

```bash
# Author every US1 test/bench file together (all different files, no deps);
# T020 is the lone exception — it sequences AFTER T020a.
Task: "tests/tls/test_pinset_single_thread_ordering.cpp"           # T011
Task: "tests/tls/test_pinset_add_then_remove_deterministic.cpp"    # T012
Task: "tests/tls/test_pinset_add_then_remove_stress.cpp"           # T013
Task: "tests/tls/test_pinset_rotation_does_not_affect_in_flight.cpp"  # T014
Task: "tests/tls/test_pin_view_lifetime_under_rotation.cpp"        # T015
Task: "tests/tls/test_pinset_snapshot_outlives_pinset.cpp"         # T016
Task: "tests/tls/test_pinset_per_counterparty_sharing.cpp"         # T017
Task: "tests/tls/test_tls_handshake_alloc_guard.cpp"               # T018
Task: "tests/tls/test_pinset_add_pmr_fail.cpp"                     # T019
Task: "tests/conformance/mock_transport.hpp"                       # T020a (helper)
Task: "bench/tls/bench_pinset_snapshot_acquire.cpp"                # T021
Task: "bench/tls/bench_pinset_find.cpp"                            # T022

# Then, once T020a lands:
Task: "tests/conformance/test_fixs_rotation.cpp"                   # T020
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phase 1 Setup — register `tls/` in check_layers + add OpenSSL Conan row + scaffold dirs + generate PEM/DER fixtures.
2. Phase 2 Foundational — `error::tls_*` enum + `Certificate` value-type + foundational compile-gate.
3. Phase 3 US1 — Pinset rotation with full FIXS-RC1 §5 add-then-remove semantics, mid-session-mutable, lock-free reader path, per-counterparty shared `shared_ptr<Pinset>`.
4. **STOP and VALIDATE**: run T011..T020 GREEN under ASan + UBSan + TSan; run T021 + T022 bench at p99 ceiling. US1 is the MVP — usable for FIXS RC1 §5 rotation drills against a counterparty.
5. Deploy/demo if ready.

### Incremental Delivery

1. Phase 1 + Phase 2 → Foundation ready.
2. Phase 3 (US1) → Pinset rotation MVP → Test → Deploy/Demo.
3. Phase 4 (US2) → cert_source plugin surface + file default → Test → Deploy/Demo (HSM/KMS now possible).
4. Phase 5 (US3) → Hardened-by-default trust mode → Test → Deploy/Demo (FIXS-RC1 wire-policy fully enforceable).
5. Phase 6 Polish → catalogue/coverage updates + `/simplify` + completeness audit + `/speckit-verify` → ready for `/gate-b`.

### Parallel Team Strategy

With multiple developers post-Phase-2:

- Developer A: US1 (Pinset rotation + benches).
- Developer B: US2 (cert_source + file_cert_source + factory + fuzz harness).
- Developer C: US3 — start with the tests authored against mocks, switch to real `cert_source` instances as Developer B's US2 ships.

---

## Notes

- `[P]` tasks = different files, no dependencies.
- `[Story]` label maps task to a specific user story for traceability.
- Each user story is independently completable + testable per spec.md Independent Test sections.
- Verify tests FAIL before implementing (TDD red-green-refactor per `[const §VII.1]`).
- Commit after each task or logical group.
- Stop at any checkpoint to validate the story independently.
- **PMR-failure seam carry-forward** (Gate A round-3 carry-forward P3, procedurally deferred): T019 (warm-path `Pinset::add` PMR throw) + T029 (cold-path `file_cert_source` PMR throw) are the named seams the Gate A close-out flagged as the natural first /speckit-tasks scope-expansion candidate per `phases/phase-4.md` Track Log. Both authored as part of their owning user-story phase (US1 + US2 respectively); not split out into a polish-phase add-on.
- **Subagent phasing** per `[[feedback_speckit_subagent_phasing]]`: `/speckit-implement` Phases 3 / 4 / 5 each route through `subagent_type: phase-implementer-sonnet` with the persona pre-loaded; parent passes only the per-call delta and independently re-verifies the gates (`[[feedback_subagent_phase_verification_two_traps]]`).
- **Authority for next-step ordering** is `.specify/pipeline.md`, not plan.md's compressed summary per `[[feedback_follow_pipeline_md_not_plan_summary]]`.
- **Avoid runaway-scope on multi-task briefs**: per `[[feedback_phase_implementer_sonnet_runaway_scope]]` — cap LoC, one task per invocation for polish phases; never trust a "ctest hung" claim without inspection.
