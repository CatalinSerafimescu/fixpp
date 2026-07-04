# Tasks: Exact wide-integer cross-exponent decimal compare (C1 / int128)

**Input**: Design documents from `specs/060-int128-decimal-compare/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/compare-contract.md, quickstart.md

**Tests**: REQUIRED. This feature's entire value ("same results, faster") is proven by a differential
oracle + directed witness matrix + differential fuzzer (FR-010 / FR-010a / FR-011; SC-001 / SC-002 /
SC-006). Because the swap is semantics-preserving, the tests are **characterization-first**: the
retained digit-string reference + corpus are written to compile/pass against the *current* comparator,
become *discriminating* only after the US1 swap, and are **mutation-proven** in US2 (introduce a mutant →
a witness fails). "Red-first" here = the mutation step, not an initially-failing assertion.

**Organization**: one code change, layered by verification dimension. Foundational stands up the
differential-oracle harness (shared); US1 = the swap; US2 = soundness; US3 = cross-toolchain.

**Scope guard (do NOT exceed)**: `src/core/decimal.cpp` compare-body only. `include/fixpp/core/decimal.hpp`
UNCHANGED (verify at T018). Do NOT touch `from_chars`/`to_chars`/arithmetic. Zero public/C-ABI/wire/
error/layout change.

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: different file, no dependency on an incomplete task → parallelizable.
- Build infra per `[feedback_conan_preset_build_infra_gotchas]` (`conan install . -of build/<P> --build=missing`); `-j2` max per `[feedback_build_resource_cap_oom]`.

---

## Phase 1: Setup

**Purpose**: baseline capture + build wiring the later phases depend on.

- [X] T001 Capture the **pre-change** deterministic instruction-count baseline: single-filter callgrind Ir for `BM_decimal_compare`, `BM_decimal_compare_diff_exp`, `BM_decimal_compare_diff_bucket` (from `bench/core/decimal_bench.cpp`), and record the numbers in `specs/060-int128-decimal-compare/research.md` (R3) as the §VIII.2 regression basis. (WSL2 ns are shape-only; Ir is the gate.)
- [X] T002 [P] Add a CMake option `FIXPP_DECIMAL_FORCE_PORTABLE_MUL` (default OFF) that, when ON, forces `mul_u64_wide`'s `#if` selection to the portable 32-bit-limb `#else` branch, so Linux/Clang can compile+cover it. Wire it into the `fixpp_core` build and a dedicated forced-portable test build in the relevant `CMakeLists.txt`. (Used by T012.)

---

## Phase 2: Foundational (BLOCKS US1–US3)

**Purpose**: the differential-oracle harness — a verbatim copy of TODAY's comparator as the reference —
must exist BEFORE `decimal.cpp` is changed (so the reference is the un-swapped algorithm).

**⚠️ CRITICAL**: T003 must be written against the current `compare` before T005 alters it.

- [X] T003 Create `tests/core/decimal_compare_diff_oracle_test.cpp` (GoogleTest) + register it in `tests/core/CMakeLists.txt`: (a) a **verbatim test-local copy** of today's digit-string `decimal_traits<pod_decimal>::compare` body as `reference_compare` (the differential oracle's source of truth, never changes with the impl); (b) a **fixed-seed deterministic** boundary-biased corpus generator — mantissa ∈ [INT64_MIN+1, INT64_MAX] biased toward digit-count boundaries, exponent ∈ [−38, 0] **plus** out-of-domain `int8` values; (c) assert `decimal_traits<pod_decimal>::compare(a,b) == reference_compare(a,b)` for every pair. Document the seed. (Compiles + passes on current code — trivially, reference == production; becomes discriminating after T005.)

**Checkpoint**: harness green on current tree → the swap can proceed with a live differential net.

---

## Phase 3: User Story 1 — Faster cross-exponent ordering, identical results (Priority: P1) 🎯 MVP

**Goal**: replace the different-exponent slow path with the wide-integer compare; the differential oracle
(deterministic corpus + Python oracle) + existing unit tests stay green (SC-001).

**Independent Test**: `ctest -R 'decimal_compare_diff_oracle|decimal_compare_test|decimal_compare_oracle'`
all green → new comparator == reference for the full corpus + the Python-`Decimal` oracle.

- [X] T004 [US1] In `src/core/decimal.cpp`, add the TU-local `static inline std::uint64_t mul_u64_wide(std::uint64_t a, std::uint64_t b, std::uint64_t* hi) noexcept` (3-way `#if`: `__SIZEOF_INT128__` → `unsigned __int128`; `_MSC_VER && _M_X64` → `_umul128`; `_MSC_VER && _M_ARM64` → `__umulh`+`a*b`; `#else` → portable 32-bit-limb schoolbook; honor `FIXPP_DECIMAL_FORCE_PORTABLE_MUL`) + `static constexpr std::uint64_t kPow10[19] = {1,…,10^18}`, above `compare`. No caller yet → dead-but-compiles. (data-model.md `mul_u64_wide` / `kPow10` contracts.)
- [X] T005 [US1] In `src/core/decimal.cpp` `compare`, **replace** the different-exponent slow path (current strip_zeros + digit_count/bucket + digit-extraction + lexicographic walk) with: raw-mantissa zero-filter → `int k = |int{ae}−int{be}|` → `k ≥ 19` magnitude dominance (no multiply) → else `mul_u64_wide(mag_scaled, kPow10[k], &hi)` + two-limb `(hi,lo)` compare → sign flip. **Preserve verbatim** the sentinel filter, sign filter, and the R3 same-exponent hoist. Zero loops, zero divisions, `noexcept`. Update the contract comment (drop "No `__int128`", cite amended `2a §6.3` — the §6.3 edit lands in T015).
- [X] T006 [US1] Confirm T003's deterministic differential oracle + `tests/core/decimal_compare_test.cpp` (AC-C1..C5) are green post-swap; **extend** `tests/oracle/decimal_compare_oracle_test.py` (seam-#8 Python-`Decimal`, seed=42) with **cross-exponent** pairs (augment `_canonical_pairs`, do not rebuild) and confirm green. (FR-001, SC-001.)

**Checkpoint**: MVP — the swap is in and provably result-identical over the corpus + Python oracle.

---

## Phase 4: User Story 2 — No overflow / narrowing / UB on any input (Priority: P1)

**Goal**: prove soundness — the directed witness matrix (mutation-tested), the primitive's `hi` limb,
UBSan-clean arithmetic, and a continuous differential fuzzer (SC-002, SC-006).

**Independent Test**: witnesses pass AND each is demonstrated to fail under its targeted mutant; UBSan
clean; the fuzzer traps on a deliberate compare mutant.

- [X] T007 [P] [US2] Add the **directed witness matrix** (research.md R5 / data-model.md — the corrected 7 rows) to `tests/core/decimal_compare_diff_oracle_test.cpp`: (1) canonicalization-equality; (2) **hi-limb crosses 2^64** `{99,0}` vs `{9223372036854775807,−18}` (product `9.9e19 ≥ 2^64`, `hi=5`), both orders/signs; (3) k-boundary k=18/k=20-near-INT64_MAX/k=38, both directions/signs; (4) zero-filter ordering/sign; (5) extremes ±INT64_MAX / ±(INT64_MIN+1) at exp 0 and −38; (6) sentinel pairs; (7) out-of-domain int8 exponents. Plus **antisymmetry** `cmp(a,b)==invert(cmp(b,a))` and **transitivity** on sampled triples. **Do NOT** use the superseded `−17`/`2^63` witness.
- [X] T008 [P] [US2] Add a direct `mul_u64_wide` unit test (assert `(hi,lo)` vs a reference wide product over random + boundary inputs incl. products ≥ 2^64), run on BOTH the default and `FIXPP_DECIMAL_FORCE_PORTABLE_MUL=ON` builds, in `tests/core/decimal_compare_diff_oracle_test.cpp` (or a sibling).
- [X] T009 [US2] **Mutation-test** each soundness-critical witness: introduce the targeted mutant one at a time — drop the `hi != 0` term; guard `≥19`→`≥21`/`≥40`; `a_scales` direction swap; `kPow10` off-by-one; end sign-flip omission — and prove ≥1 witness FAILS for each. `19→20`: **measured to be result-equivalent but killed via the ASan lane** (`kPow10[19]` OOB — guard + table size jointly load-bearing for memory safety), superseding the ex-ante "accepted semantically-equivalent no-kill" prediction; recorded as such in the kill table + a directed k=19 correctness cell added to the witness matrix. Document the runs (kill table) as a note in the tracked test file (`.specify/decisions/` is gitignored). **Measured-supersedes-predicted correction propagated to spec.md FR-011/SC-002, research.md R1/R5, gate.md CHK011.**
- [X] T010 [US2] Create the **differential libFuzzer** target `tests/fuzz/fuzz_decimal_compare.cpp` (decode two `pod_decimal`s from input; assert `compare(a,b) == reference_compare(a,b)`; mismatch → `__builtin_trap`) + register in `tests/fuzz/CMakeLists.txt` with `-fsanitize=fuzzer,address,undefined` + commit a seed corpus under `tests/fuzz/corpus/decimal_compare/`. Demonstrate it **traps** on a deliberate compare mutant (SC-006). (FR-010a.)
- [X] T011 [US2] Run the ASan / UBSan / TSan Tier-1 matrix on the decimal_compare tests; confirm **UBSan clean** on the widening multiply + shifts (the primary soundness guard, `[const §IX.2]`).

**Checkpoint**: soundness proven — witnesses bite (mutation-tested), UBSan clean, fuzzer asserts.

---

## Phase 5: User Story 3 — Identical across all toolchains incl. MSVC (Priority: P2)

**Goal**: the one 128-bit primitive compiles + yields identical ordering on GCC/Clang/libc++/clang-cl,
MSVC x64/ARM64, and the portable fallback (SC-004; L-049-3 guardrail).

**Independent Test**: forced-portable oracle green on Linux; Tier-2 `windows-msvc-*` oracle green.

- [X] T012 [US3] Confirm the MSVC `_umul128` (x64) and `__umulh` (ARM64) **signatures + arch availability** against current Microsoft `<intrin.h>` documentation; keep the `#elif` conditions conservative (a wrong guess must fall to `#else` = a perf bug, not a correctness bug). Adjust `src/core/decimal.cpp` if the from-memory design-note signatures are off. (research.md R2 obligation.)
- [X] T013 [US3] Build with `FIXPP_DECIMAL_FORCE_PORTABLE_MUL=ON` on Linux/Clang and run the differential oracle + witness matrix against the portable `#else` limb path — proves it equivalent AND covers it for lcov `[const §IX.1]`.
- [~] T014 [US3] **DEFERRED-TO-PR (PR-time CI trigger — cannot run during local `/speckit-implement`)**: Trigger the Tier-2 `run-tier2` label → `tier2.yml` `windows-msvc-{debug,release,asan}` on `windows-2022`; confirm the differential oracle + witness matrix pass on MSVC x64 (and ARM64 if the runner provides it). This is the CI leg of the Gate-A "confirm-at-implement Tier-2 run" obligation (its local leg = T012, DONE). Fire it once the PR is open, before Gate-B sign-off; the MSVC `#elif` branch has no Linux lane so this Windows run is the only place `_umul128`/`__umulh` actually compile+execute. Tracked in the `/speckit-verify` note + Gate-B preconditions.

**Checkpoint**: portable + MSVC parity established; no Linux-invisible `#elif` breakage.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: the mandatory contract amendment, the perf gate, surface-invariance, and close-out.

- [X] T015 [P] **Amendment 1–2**: `.specify/2a-decimal.md §6.3` — replace the "not implemented by scaling to a common exponent … the previous `__int128` fallback was unsound" text with the **guarded** algorithm (k≥19 dominance + product `< 2^123` proof), leading with the bound proof; retire "No multiplication, no wide-int dependency"; **keep** "no MSVC-vs-Clang **algorithm** split" (primitive-level `#if` only). And `specs/001-core-decimal/research.md D-5` — supersession note (v0.1 rejected the *unguarded* scale; record the bound proof + per-compiler-primitive decision). (FR-009 sites 1–2.)
- [~] T016 [P] **DEFERRED-TO-POST-MERGE (parent-repo, outside this submodule's git tree — CANNOT land in the library PR)** — **Amendment 3 (parent-repo, post-merge)**: `research/G19-fix-fpml-iso20022/phases/phase-9/perf-investigation/02-lowlatency-recommendations.md` — outside this submodule's git tree, so it CANNOT land in the library PR; land as a **separate parent-repo commit immediately after the library PR merges** (same convention as T017/059). Dated (2026-07-04) supersession notes reclassifying C1 as a **default-path** swap at all THREE framings: the C1 Tier-C entry, the Tier-C preamble caveat (`:354-362` — carve C1 out of "None of them is a default-path change"), and the "Considered and rejected: `__int128`" bullet. **P3 fix (anchor confirmed by /speckit-analyze)**: the rejected bullet is at `:600-611` (section start `:558`; C1 entry's own correction `:367`), NOT `:357-359` — that cite was a false match inside the preamble caveat. Confirm the exact line at edit time, then cite correctly. (FR-009 site 4 + the Gate-A P3.)
- [~] T017 [P] **DEFERRED-TO-POST-MERGE (parent-repo, outside this submodule's git tree — CANNOT land in the library PR)** — **Amendment 4 (parent-repo, post-merge)**: `research/G19-fix-fpml-iso20022/remaining-work/perf-and-hardening-findings.md` — outside this submodule's git tree, so it CANNOT land in the library PR; land as a separate parent-repo commit alongside T016, immediately after the library PR merges (precedent: 059's `790e6b1 docs(remaining-work): 059 close-out`, landed after PR #163's merge bump `f0d4876`). Reclassify C1 default-path: rewrite the Cluster-2 residual line (`:62`), **rewrite** the C1 table row (`:72`, do not merely flip to DONE), and remove C1 from the "Low-latency MODE" list (`:141`). Update the parent `[[project_decimal_cluster2_fixes]]` memory pointer. (FR-009 site 5.)
- [X] T018 **Perf gate**: update `bench/baselines/decimal_baseline.json` in this PR with a rationale line; re-run single-filter callgrind and confirm `BM_decimal_compare_diff_exp` **Ir decreases** vs the T001 baseline, with **no regression (±5%)** on `BM_decimal_compare` / `BM_decimal_compare_diff_bucket`. (`[const §VIII.2/3]`; NFR-001, SC-003.)
- [X] T019 [P] **Surface-invariance**: confirm `git diff include/fixpp/core/decimal.hpp` is empty and the C-ABI `abidiff` (abi-golden) shows the decimal PoD byte-identical — no public / C-ABI / wire / error / layout change. (FR-007, SC-004.)
- [X] T020 Run `specs/060-int128-decimal-compare/quickstart.md` end-to-end (oracle, forced-portable, fuzz, bench, surface checks) and confirm each done-criterion.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T021 [P] **Catalogue close-out**: 060 is an internal perf/hardening feature (no OFFICIAL FIX-spec row) — add a design/evidence row for it in `spec/feature-catalogue.md` (per the 046/048/058/059 non-OFFICIAL precedent) with the PR / evidence ref, add the matching `spec/coverage-index.md` entry, and record any operator-facing limitation in `spec/behaviors-and-limitations.md` (e.g. an L-060-x for "MSVC `#elif` branch is Tier-2-only — no Linux lane compiles it"). If a later audit finds 060 *does* own an OFFICIAL row, flip it to `done` instead. (T057 analog.)
- [X] T022 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver; (ii) every spec FR-001..012 / NFR-001 and SC-001..006 maps to a landed test AND landed implementation; (iii) the 060 catalogue row is `done` with a matching `coverage-index.md` entry; (iv) the **in-submodule** amendment sites 1–3 (T015 + the T005 comment) are fully landed in-PR and SC-005 grep-clean for those 3 sites; the **parent-repo** sites 4–5 (T016/T017) are recorded here as a tracked, not-yet-landed post-merge obligation (per the 059 precedent, this does NOT block Gate-B — SC-005's own "after merge" wording covers the full 5-site grep once the parent-repo commit lands). Record the verdict (100% in-submodule, parent-repo close-out tracked) in `.specify/decisions/060-int128-decimal-compare-verify.md` (`## Completeness`). Hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d). (T058 analog.)

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)**: T001, T002 — no deps; start immediately.
- **Foundational (P2)**: T003 (the retained reference) MUST land before T005 alters `compare`. Blocks US1–US3.
- **US1 (P3)**: T004 → T005 → T006. Depends on T003.
- **US2 (P4)**: T007/T008 [P] after T005; T009 after T007; T010 after T003+T005; T011 after T005. Depends on US1.
- **US3 (P5)**: T012 (source, may precede/parallel T005's helper); T013 after T002+T005; T014 after T005.
- **Polish (P6)**: T015–T017 [P] (independent docs; T016 folds the P3 anchor fix); T018 after T005 (+ T001 baseline); T019 after T005; T020 after US1–US3; T021 then T022 (final) after everything.

### Story dependencies

- US1 (P1) = the code change — the MVP; everything else verifies it.
- US2 (P1) depends on US1 (witnesses/fuzz test the swapped compare).
- US3 (P2) depends on US1 (the primitive branches exist after T004/T005).

### Parallel opportunities

- Setup: T002 ∥ T001.
- US2: T007 ∥ T008 (both add to the oracle test file — sequence the two edits, but independent of other phases).
- Polish amendments T015 ∥ T016 ∥ T017 ∥ T019 (different files).

---

## Implementation Strategy

**MVP = Setup + Foundational + US1** (T001–T006): the swap lands, provably result-identical over the
deterministic corpus + Python oracle. **STOP and VALIDATE** here before soundness/portability.

**Then incrementally**: US2 (soundness — the gate that makes the Gate-A reversal defensible) → US3
(cross-toolchain) → Polish (amendment + perf gate + close-out). T015 (in-submodule amendment sites 1–3)
and the bench-baseline (T018) MUST land **in the same PR** as the code — hard gates, not follow-ups.
T016/T017 (parent-repo amendment sites 4–5, outside this submodule's git tree) CANNOT physically be
part of the library PR; they land as a **separate post-merge parent-repo commit**, per the 059
`remaining-work` close-out precedent — this does not block Gate-B (see T022).

### The 3 confirm-at-implement obligations (from Gate A) — mapped

- MSVC signatures vs MS docs + Tier-2 run → **T012 + T014**.
- Forced-portable `#else` coverage → **T002 + T013**.
- Bench-baseline update in-PR → **T018**.

## Notes

- The differential reference (T003) is a *frozen copy* of the pre-swap comparator — never re-sync it to the new impl (that would make the oracle vacuous).
- Per `[feedback_phase_implementer_sonnet_runaway_scope]`: one task per implementer invocation, cap LoC, no scope creep beyond `decimal.cpp` + the named test/doc files.
- Re-index CodeGraph (`codegraph sync`) after T005 (the only code-structural change).
