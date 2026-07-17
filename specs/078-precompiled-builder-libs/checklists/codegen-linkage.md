# Checklist: Codegen / Linkage Requirements Quality — 078-precompiled-builder-libs

**Purpose**: "Unit tests for the requirements" — validate that the split-object / linkage / determinism / #197-removal requirements are complete, clear, consistent, and measurable BEFORE `/implement`.
**Created**: 2026-07-17
**Feature**: [spec.md](../spec.md) · **Focus**: codegen-layout correctness + build/CI-removal · **Audience**: PR reviewer · **Depth**: standard

## Requirement Completeness

- [x] CHK001 Are all emitted per-version artifacts (`groups.hpp`, `validators/traits.hpp`, slim `messages/<Msg>.hpp`, `.builder.inl`/`.validator.inl`, disjoint `.builder.cpp`/`.validator.cpp`, `all.hpp`) each enumerated with a defined content contract? [Completeness, data-model Entities 1–5, contracts/include-layout] — PASS: data-model.md Entities 1/1b/2/3/4/5 each carry Fields/Invariants/Relationships, mirrored by contracts/include-layout.md's file-set tree with inline content comments; the two are consistent (verified by cross-read).
- [x] CHK002 Is the "always emitted + always compiled" requirement stated for BOTH libraries per builder-bearing version, with the vt11 (admin-only → none) and v42 (deferred to #196) exclusions explicit? [Completeness, FR-002/FR-003/FR-004, Assumption A7] — PASS: FR-002–FR-004 state "always built"/"always compile" for both libs; Assumption A7 + spec.md Edge Cases ("A version with no typed groups / admin-only version") state vt11→none and v42→#196 explicitly; tasks T014 dispositions the no-emit witness.
- [x] CHK003 Is the complete set of non-`#include` `Builders.hpp` gates (markers, existence assertions, text-parse gates, `-D` header defs) enumerated with a per-gate re-point disposition? [Completeness, plan §Gate A round-2 census] — PASS: reproduced the census independently (`grep -rn 'Builders\.hpp' tests/ cmake/ bench/`); every non-`#include` hit (cmake/Codegen.cmake markers :267,273,285,623; codegen_build_graph_test.cmake :119-125; codegen_source_staleness_test.cmake :96-108; builder_completeness_common.hpp parse helpers; builder_completeness_mutation_witness_test.cpp :146-147; determinism_test.cpp :357-879; test_077_builder_no_emit.cpp :51-72; vlatest_compile_smoke_test.cpp :82-91; test_077_builder_dedup_count.cpp; test_077_v42_vt11_completeness_and_c4.cpp; tests/codegen/CMakeLists.txt `-D..._BUILDERS_HPP` defs) appears in plan.md's New-C table with a per-file disposition and a T009–T018 task. No un-tabled hit found.
- [x] CHK004 Are requirements defined for where `builder_registry` lives after the split, and which artifact each migrated gate re-points to (`all.hpp` vs `groups.hpp`)? [Completeness, data-model Entity 5/8] — PASS: Entity 5 pins `builder_registry` to `all.hpp`; Entity 8 explicitly calls out the two distinct re-point targets (`builder_registry`→`all.hpp`, `G_<no_tag>Args`→`groups.hpp`), consistent with the T016/T017 dispositions.
- [x] CHK005 Is the golden artifact requirement redefined as a file *set* (not a single header) with an explicit inventory? [Completeness, FR-010, research R6] — PASS: FR-010, research R6, data-model Entity 7, and completeness-and-golden.md "Golden set" section all enumerate the same explicit file inventory (`groups.hpp`+`validators/traits.hpp`+per-message 5-file set+`all.hpp`).

## Requirement Clarity

- [x] CHK006 Is "link-time opt-in" defined precisely (link the lib / don't) rather than left as a vague "opt-in"? [Clarity, FR-004] — PASS: FR-004 states explicitly "link the builder library, the validator library, both, or neither"; cmake-targets.md + include-layout.md consumer-intent tables map each choice to a concrete link line.
- [x] CHK007 Is the header-only inline-mode selection mechanism (macro name, per-message vs whole-TU override) unambiguously specified? [Clarity, FR-006, research R4] — PASS: the mechanism (independent per-side whole-TU macros `FIXPP_BUILDERS_HEADER_ONLY`/`FIXPP_VALIDATORS_HEADER_ONLY`, plus a per-message override) is fixed and unambiguous; the per-message override's exact spelling is explicitly and consistently deferred to `/implement` (research R4, include-layout.md "exact spelling decided at `/implement`; e.g. …") while every downstream artifact (tasks T026/T027, quickstart Scenario 4, completeness-and-golden.md) consistently uses the same illustrative `FIXPP_BUILDERS_HEADER_ONLY_<Msg>`/`FIXPP_VALIDATORS_HEADER_ONLY_<Msg>` spelling — a legitimate implementation-detail deferral, not a testability gap (FR-006/FR-007 are mechanism-level, not spelling-level).
- [x] CHK008 Is the "slim declaration header" defined by what it EXCLUDES (function bodies) rather than by a subjective adjective? [Clarity, data-model Entity 2] — PASS: FR-001 ("does not require parsing the builder function bodies"), Entity 2 ("non-inline `extern` declarations"), R1's cost-class column ("cheap (decls)") all define it by exclusion of bodies.
- [x] CHK009 Is the install/export scope unambiguous — build-tree + in-tree consumers only, with slim headers NOT installed for external linking while targets are unexported? [Clarity, Clarifications §Gate A round 1] — PASS: cmake-targets.md "Install / export scope" section + spec.md Clarifications (Gate A round 1) + research R3 state this identically, including the coherence rule (no header install without an exported target).

## Requirement Consistency

- [x] CHK010 Is the builder⟂validator zero-validator-code invariant labelled consistently as SC-003/FR-005 (not SC-005) across spec/research/data-model/contracts/tasks after the analyze remediation? [Consistency, SC-003/FR-005] — SPEC-FIXED: found a residual mislabel the F1 remediation missed — tasks.md T002's file name was `tests/codegen/test_078_odr_sc005_probe.cpp` for a probe whose content is entirely the SC-003 builder⟂validator/ODR invariant (no SC-005 content at all). Renamed to `test_078_odr_sc003_probe.cpp`. Case-insensitive token sweep (`grep -rniE 'sc[-_]?005|sc[-_]?003'`) over the whole bundle confirms this was the only residual; every other `SC-005` hit is genuinely the `all.hpp` aggregator (US4) and every other `SC-003` hit is genuinely zero-validator-code.
- [x] CHK011 Is the SC-004 distinction — builder output "byte-identical wire bytes" vs validator "result-identical (same success/error + offending tag)" — applied consistently at every locus? [Consistency, SC-004, FR-009] — PASS: verified spec FR-009/SC-004, data-model Entity 3, completeness-and-golden.md "Equivalence in both modes", quickstart.md Scenario 4a/4c, and tasks T021/T026 (builder, byte-identical) vs T027 (validator, result-identical) — no locus conflates the two.
- [x] CHK012 Are the library target names (`fixpp_builders_<ver>`/`fixpp_validators_<ver>` + `fixpp::builders::<ver>`/`fixpp::validators::<ver>` aliases) identical across data-model, cmake-targets, and tasks? [Consistency, contracts/cmake-targets] — PASS: Entity 8, cmake-targets.md target table, and tasks T006/T019 use identical target/alias spelling throughout.
- [x] CHK013 Is the validator-traits home stated consistently as `validators/traits.hpp` (with `groups.hpp` DATA-ONLY) everywhere, with no residual "traits in groups.hpp" or "traits TU" wording? [Consistency, data-model Entity 1/1b] — PASS: grepped spec/research/data-model/contracts/tasks/quickstart for any positive "traits in groups.hpp"/"traits TU" claim — none found; cmake-targets.md's only "not a separately compiled 'traits TU'" hit is a negation consistent with the `inline`-header design.

## Acceptance-Criteria Quality (Measurability)

- [x] CHK014 Is SC-001's "order of magnitude below" anchored to the measured monolith baseline (~3.6 GiB RSS) and routed to a concrete witness (compile-bench record), not a hard ctest gate? [Measurability, SC-001, research R9] — PASS: SC-001 cites the baseline inline; R9's SC→witness table routes it to the 003/T046 compile-bench decision-record convention (explicitly NOT Article VIII); T023 implements it.
- [x] CHK015 Is SC-002 ("link only used messages") objectively verifiable via `nm` on a subset-linked binary given per-message `.o` archive granularity? [Measurability, SC-002] — PASS: R9, completeness-and-golden.md `nm` witnesses section, T028 all specify the exact `nm --defined-only` check.
- [x] CHK016 Is SC-003 ("zero validator machine code") objectively verifiable via `nm` on a builder-only binary? [Measurability, SC-003] — PASS: R9, completeness-and-golden.md, T024, R2a leg (ii) all specify the same `nm` check.
- [x] CHK017 Is SC-006 tied to the sanitizer-instrumented CI legs' peak RSS under the 16 GB runner limit (the binding constraint), not the uninstrumented build? [Measurability, SC-006, Clarifications §Gate A round 1 — main-CI OOM] — PASS: SC-006 text, R8, and cmake-targets.md all state the sanitizer legs (not the uninstrumented build) are binding, backed by the cited main-CI OOM run IDs; spot-verified the python-bindings legs' `python_touched` path-gating (`tier1.yml:131,141,628-631`) and the reused `linux-clang-debug` preset (`tier1.yml:746-747`) — claims hold.
- [x] CHK018 Is SC-007 expressed as a checkable property (byte-stable content + stable file NAME-set + COUNT), not just "deterministic"? [Measurability, SC-007, research R6] — PASS: SC-007, R6, Entity 7, and completeness-and-golden.md's three-assertion determinism-test spec all state the same checkable triad.

## Scenario & Edge-Case Coverage

- [x] CHK019 Are ODR requirements for the mix case (force-inline one message + link the rest, sharing a group-plan) specified: no duplicate symbol, single trait definition? [Coverage, FR-007, research R2a] — PASS: FR-007, R2a leg (i), T027, quickstart Scenario 4a.
- [x] CHK020 Is the both-libs-linked case (builder + validator, the common consumer) covered by a no-duplicate-symbol requirement? [Coverage, research R2a leg iii] — PASS: R2a leg (iii), Entity cross-invariant 2, completeness-and-golden.md "Both-libs linked", quickstart Scenario 4c.
- [x] CHK021 Is the "shared `groups.hpp` included from many per-message headers" case covered by an include-once requirement? [Edge Case, FR-012] — PASS: FR-012, Entity 1 invariants (`#pragma once`), T029 explicitly tests the FR-012 include-once property via `all.hpp`.
- [x] CHK022 Is the completeness-proof semantic shift (address-of-declaration proves LINK; compile-proof at the lib target) stated so coverage is not read as weakened? [Coverage, research R7] — PASS: R7, Entity 6, completeness-and-golden.md "Coverage is not weakened", plan.md Constitution Check "VI PASS+" row all state this explicitly.
- [x] CHK023 Are requirements defined so an admin-only / no-builder version (vt11) yields a coherent (possibly empty) target rather than a broken build? [Edge Case, spec §Edge Cases, A7] — PASS: spec Edge Cases, A7, T014.

## Dependencies & Assumptions

- [x] CHK024 Is the built-from-source client-toolchain assumption (A3) documented, with its consequence (shared `Args` ABI soundness) and the cross-toolchain out-of-scope alternative (C-ABI runtime builder) named? [Assumption, A3] — PASS: A3, spec Edge Cases "Cross-toolchain / prebuilt-binary consumers", R10 all state this identically.
- [x] CHK025 Is the #197-removal's dependency on a CI evidence run over the python-bindings sanitizer legs (not local `/speckit-verify`) documented, with the split-follow-up-PR fallback? [Dependency, research R8] — PASS: A4, R8 step 2, cmake-targets.md, quickstart Scenario 7, T033/T034 all state the workflow_dispatch-or-follow-up-PR evidence path identically; spot-verified against `tier1.yml` directly (see CHK017).
- [x] CHK026 Is the zero-core-change assumption (A8/FR-011) stated with a verifiable check (grep of `src/`/`capi/`/`bindings/`/`python`)? [Assumption, FR-011] — PASS: A8 states the grep verification inline; FR-011; T001 records the scope invariant; T040 re-verifies by grep at close-out.

## Ambiguities & Conflicts

- [x] CHK027 Is the constitution AMEND obligation (Article I §1 / XVIII §7, v0.9→v0.10 at merge) unambiguously assigned to a task and gated so it is not dropped or deferred past merge? [Conflict, plan §Constitution Check, tasks T035] — PASS: plan.md Constitution Check table + Gate A round 1 disposition section name the exact loci (`constitution.md:88`,`:375`) and the v0.9→v0.10 bump; T035 is the concrete task; spot-verified both loci resolve in the current `.specify/constitution.md` (I §1 "single-TU header" text at the FIX-Latest builder-tier paragraph; XVIII §7 "single version-agnostic structural-plan Args-dedup emitter" text) — both anchors present and matching the described stale wording.
- [x] CHK028 Is the single-body-source variant (research R1) reconciled with the golden name/COUNT determinism contract so a chosen emission form cannot silently break FR-010? [Ambiguity, research R1/R6] — DD-DECIDED §Gate A round 2 (New-B): R1's "Optional structural variant" section and completeness-and-golden.md's "Drift-watch (Gate A round 2)" note both state the reconciliation explicitly — if the single-body variant is chosen at `/implement`, Entity 7's golden enumeration and the R6 name-set/count assertion regenerate together in lockstep, and R6 stays authoritative over whichever emission form is picked. This is a Gate-A-round-2-settled design decision (traceability ref), not a residual spec ambiguity.

## Notes

- These items test whether the REQUIREMENTS are well-written — not whether the implementation works. Dispositions belong to the checklist-audit (pipeline step 9): PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 26 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | 28 |

### SPEC-FIXED items

- CHK010 — renamed `tests/codegen/test_078_odr_sc005_probe.cpp` → `test_078_odr_sc003_probe.cpp` in `tasks.md` T002 (the probe's content is entirely the SC-003 builder⟂validator/ODR invariant; the filename was a residual SC-005/SC-003 mislabel the prior `/speckit-analyze` F1 remediation missed because it swept prose, not the T002 filename token); affected: `tasks.md:T002`.
- (Bonus finding, not CHK-mapped, fixed in the same pass) — `plan.md:157` and `tasks.md:T019` claimed a "069×5" `#include`-er test-file count for the migration-relink surface; the actual on-disk `tests/session/test_069_*.cpp` set has 4 files (`all_families_roundtrip`, `family_exemplar_golden`, `mode_count`, `us1_smoke`), not 5. Corrected both loci to "069×4". No file was missing from the named glob — only the numeral was wrong — so this does not change T019's scope, only its stated count.

### DD-DECIDED items

- CHK028 — anchor "Gate A round 2 (New-B)" in `research.md` R1 + `contracts/completeness-and-golden.md` "Drift-watch"; rationale: the single-body-source emission variant's interaction with the golden name/COUNT contract was explicitly reconciled at Gate A round 2 — R6 stays authoritative over whichever emission form is picked at `/implement`, and both loci restate the same reconciliation, so this is a settled design decision recorded as a traceability ref, not a residual spec ambiguity.

### WAIVED items

None.

Anchors spot-verified: `.specify/constitution.md:88` (Article I §1, FIX-Latest builder-tier "single-TU header" paragraph) and `.specify/constitution.md:375` (Article XVIII §7, "single version-agnostic structural-plan Args-dedup emitter" paragraph) — both resolve and match the stale wording plan.md's Constitution Check table describes; `.github/workflows/tier1.yml:131,141,628-631,746-753` (python-bindings sanitizer legs' `python_touched` path-gating + reused `linux-clang-debug` preset) — resolves and matches R8/A4's description; `CMakePresets.json:26,53,198` and `CMakeLists.txt:208-210,347-380` (the #197 stopgap: option + 3 preset overrides + Ninja job pool) — all resolve exactly as cited. All spot-verified against the signed-off Gate A round 3 revision of `plan.md`/`research.md` (converged 2026-07-17, Codex P1=0 P2=0 P3=1 final round).
