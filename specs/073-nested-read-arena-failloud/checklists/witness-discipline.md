# Checklist: Witness discipline — requirements quality (073-nested-read-arena-failloud)

**Purpose**: Unit-test the WRITTEN test/acceptance requirements for faithfulness, discrimination, and coverage — are the witness obligations specified clearly and completely? Not implementation verification.
**Created**: 2026-07-13
**Audience**: Gate-B reviewer / implementer.

## Fault-injection faithfulness

- [x] CHK017 - Is the exhaustion-injection mechanism specified concretely (tiny-cap `monotonic_buffer_resource` over `null_memory_resource()` as the parse arena) rather than left to the implementer? [Clarity, research §D6 / quickstart] — PASS: research.md §D6 opening paragraph + quickstart.md "Faithful exhaustion harness" section give the concrete mechanism with a code sketch (`std::pmr::monotonic_buffer_resource` over `std::pmr::null_memory_resource()`).
- [x] CHK018 - Is it explicitly required that the witness NOT use a post-hoc "alloc failed" flag and NOT a hand-built 16 KiB-exhausting message (unfaithful injection excluded)? [Completeness, research §D6 / Spec §Assumptions] — PASS: Spec §Assumptions bullet 1 explicitly excludes both ("not by fabricating a 16 KiB-exhausting real message and not by flipping a post-hoc 'allocation failed' flag"); research.md §D6 restates verbatim, citing [[feedback_fault_injection_posthoc_flag_unfaithful]].
- [x] CHK019 - Is the requirement that the injected tiny-cap arena reproduce the SAME failure the real fixed 16 KiB null-upstream arena exhibits at its cap stated? [Clarity, Spec §Assumptions] — PASS: Spec §Assumptions bullet 1 "The injected under-capacity arena reproduces the exact failure the fixed arena would exhibit at its cap"; quickstart.md restates in the harness section.

## Discrimination / mutation-proof

- [x] CHK020 - Is each witness required to be authored RED-first and mutation-proven, with the specific mutants named, AND is mode discrimination pinned platform-robustly? [Measurability, research §D2/§D6 / tasks §T001] — SPEC-FIXED (implement-time): originally named a **two-mutant** matrix (null-only → mode (b) RED; final-exit-only → read-2 RED). With mode (c) added (see CHK001 / correctness-and-abi), tasks T001 is updated to a **three-mutant** matrix: M1 null-only → modes (b) AND (c) RED; M2 drop the `|| build_status()==out_of_memory` term (the old 2-mode formula) → mode (c) RED (the mode-c discriminator); M3 final-exit-only → read-2 RED. **Also fixed a platform-fragility gap the original overlooked**: a cap tuned to hit mode (c) via `sizeof(OffsetTable)` (clang-debug-specific, 280 B) can silently land in mode (a) or succeed on gcc-release / MSVC, making M2 vacuous across the 3-tier CI ([[feedback_local_verify_clang_only_misses_gcc_release_ci_job]]). T001 now pins each mode by **introspection on the sub-table** (`table==nullptr` / `build_status()`-ok+throw / `build_status()==out_of_memory`) so the discriminator fails loud instead of false-passing when a platform's cap lands elsewhere; T006/T008 (which cannot introspect) use wide-margin fixtures. Each mutant's RED must be SEEN.
- [x] CHK021 - Is the mode-(b) "second-loss" witness required to read the group TWICE (to pin the cache-hit exit), with the reason (2nd read served from cached non-null row re-throws) recorded? [Completeness, research §D6 / tasks §T001/T008] — PASS: research.md §D6 item 5 spells out the read-twice rationale in detail; tasks T001/T008 both require the repeated-read assertion; quickstart.md Scenario 5 "Why read twice" paragraph gives the same rationale for an implementer.
- [x] CHK022 - Is the requirement to assert the distinct signal DIRECTLY (C-ABI `WIRE_LIMIT_EXCEEDED`; typed `group_view.alloc_failed()`) — NOT via an `nc==0` / empty-span proxy — stated? [Clarity, Spec §SC-001/§SC-002 / tasks §T006/T008] — PASS: Spec §SC-001/§SC-002 state the code/witness must be proven RED/GREEN directly; tasks T006 ("Assert the code DIRECTLY (not an nc==0 proxy)") and T008 ("Assert alloc_failed() DIRECTLY") state it explicitly.

## Coverage & symmetry

- [x] CHK023 - Are non-failure CONTROL witnesses required on BOTH paths (absent → C-ABI `TAG_NOT_FOUND` / typed `alloc_failed()==false`; count-0 → `OK`/nc=0 / `alloc_failed()==false`), so a false positive is caught? [Coverage, Spec §SC-003 / tasks §T006/T008] — PASS: Spec §SC-003; tasks T006 ("controls (SC-003) → absent nested group returns FIXPP_ERR_TAG_NOT_FOUND, genuine count-0 returns FIXPP_ERR_OK/nc=0") and T008 (mirrors on the typed path) name both controls explicitly.
- [x] CHK024 - Is the parity requirement (BOTH the C-ABI and typed nested reads fail loud symmetrically — FR-005 one-pass, no half-restructure) expressed as a testable checkpoint? [Consistency, Spec §FR-005/§SC-005] — PASS: Spec §FR-005/§SC-005; tasks Phase 3/Phase 4 Checkpoints + "Implementation Strategy (MVP first)" paragraph states explicitly "do NOT ship US1 without US2, per the half-restructure rule, delivered in the same PR"; grep-verified only two production consumers exist (message_read.cpp, emit_messages.cpp), so the parity claim is exhaustive, not partial.
- [x] CHK025 - Is the typed witness required to assert the process does NOT terminate (value/status delivery, not a throw across the `noexcept` boundary)? [Coverage, Spec §FR-004 / §US2] — PASS: Spec §FR-004 + US2 Acceptance Scenario 1 state "the process does not terminate"; tasks T008 requires the same assertion explicitly; verified both `nested_group_slices` overloads are declared/defined `noexcept` in live source (`offset_table.hpp:228,243`; `.cpp:721,778`).
- [x] CHK026 - Is a wire-level primitive witness (exercising `nested_group_slices` directly under the tiny arena) specified in addition to the two consumer-level witnesses, so the shared status logic is pinned independently of both consumers? [Completeness, tasks §T001] — PASS: tasks T001 (Phase 2 Foundational) is a dedicated wire-level primitive witness, distinct from T006 (C-ABI, Phase 3) and T008 (typed, Phase 4).

## Regression / CI faithfulness

- [x] CHK027 - Is it required that the no-regression gate run the FULL ctest (not narrow targets) specifically because the `codegen_determinism_test` can hang CI on a stale golden? [Clarity, tasks §T013 / research §D7] — PASS: tasks T013 states "FULL ctest (NOT narrow targets — the determinism golden hangs CI if stale)" explicitly; research.md §D7 cites the prior incident ([[feedback_codegen_golden_exists_narrow_verify_misses_it]]) as the rationale.
- [x] CHK028 - Is the sanitizer scope for the diff justified (ASan/UBSan for the `bad_alloc`/arena path; TSan N/A — no concurrency surface) rather than left ambiguous? [Consistency, plan Article IX/XI / tasks §T013] — PASS: plan.md Constitution Check rows for Article IX ("bad_alloc/arena path is ASan/UBSan-relevant") and Article XI ("no concurrency surface") give the justification; tasks T013 restates "the sanitizer presets the diff touches (ASan/UBSan for the bad_alloc/arena path)", consistent with the plan.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 11 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 12 |

### SPEC-FIXED items
- CHK020 — **SPEC-FIXED at `/speckit-implement`**: the mutant matrix went from two mutants to three (added M2, the mode-(c) discriminator) after mode (c) surfaced, AND T001 mode discrimination was moved from cap-band tuning to sub-table introspection to stay valid across the 3-tier CI (clang-debug / gcc-release / MSVC). Affected: `tasks.md#T001`, `tasks.md#T008`, research §D2/§D6.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified (all resolve in the signed-off revisions):
- research.md §D6 (all 5 numbered witness scenarios) — resolves against quickstart.md §Scenario 1-5, verbatim correspondence.
- tasks.md T001/T006/T008 (Foundational/US1/US2 witnesses) — resolve, distinct files/phases confirmed.
- `.specify/constitution.md` Article VII §3/§7 (TDD mandatory, fuzzing) — resolves.
- `offset_table.hpp:228,243` + `.cpp:721,778` (`noexcept` on both `nested_group_slices` overloads) — resolve, exact match.
- `[[feedback_codegen_golden_exists_narrow_verify_misses_it]]` / `[[feedback_fault_injection_posthoc_flag_unfaithful]]` memory citations — both are real, previously-recorded project lessons directly on-point for D6/D7.
