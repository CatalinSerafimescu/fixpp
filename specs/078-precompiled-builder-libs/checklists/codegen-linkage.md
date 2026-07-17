# Checklist: Codegen / Linkage Requirements Quality — 078-precompiled-builder-libs

**Purpose**: "Unit tests for the requirements" — validate that the split-object / linkage / determinism / #197-removal requirements are complete, clear, consistent, and measurable BEFORE `/implement`.
**Created**: 2026-07-17
**Feature**: [spec.md](../spec.md) · **Focus**: codegen-layout correctness + build/CI-removal · **Audience**: PR reviewer · **Depth**: standard

## Requirement Completeness

- [ ] CHK001 Are all emitted per-version artifacts (`groups.hpp`, `validators/traits.hpp`, slim `messages/<Msg>.hpp`, `.builder.inl`/`.validator.inl`, disjoint `.builder.cpp`/`.validator.cpp`, `all.hpp`) each enumerated with a defined content contract? [Completeness, data-model Entities 1–5, contracts/include-layout]
- [ ] CHK002 Is the "always emitted + always compiled" requirement stated for BOTH libraries per builder-bearing version, with the vt11 (admin-only → none) and v42 (deferred to #196) exclusions explicit? [Completeness, FR-002/FR-003/FR-004, Assumption A7]
- [ ] CHK003 Is the complete set of non-`#include` `Builders.hpp` gates (markers, existence assertions, text-parse gates, `-D` header defs) enumerated with a per-gate re-point disposition? [Completeness, plan §Gate A round-2 census]
- [ ] CHK004 Are requirements defined for where `builder_registry` lives after the split, and which artifact each migrated gate re-points to (`all.hpp` vs `groups.hpp`)? [Completeness, data-model Entity 5/8]
- [ ] CHK005 Is the golden artifact requirement redefined as a file *set* (not a single header) with an explicit inventory? [Completeness, FR-010, research R6]

## Requirement Clarity

- [ ] CHK006 Is "link-time opt-in" defined precisely (link the lib / don't) rather than left as a vague "opt-in"? [Clarity, FR-004]
- [ ] CHK007 Is the header-only inline-mode selection mechanism (macro name, per-message vs whole-TU override) unambiguously specified? [Clarity, FR-006, research R4]
- [ ] CHK008 Is the "slim declaration header" defined by what it EXCLUDES (function bodies) rather than by a subjective adjective? [Clarity, data-model Entity 2]
- [ ] CHK009 Is the install/export scope unambiguous — build-tree + in-tree consumers only, with slim headers NOT installed for external linking while targets are unexported? [Clarity, Clarifications §Gate A round 1]

## Requirement Consistency

- [ ] CHK010 Is the builder⟂validator zero-validator-code invariant labelled consistently as SC-003/FR-005 (not SC-005) across spec/research/data-model/contracts/tasks after the analyze remediation? [Consistency, SC-003/FR-005]
- [ ] CHK011 Is the SC-004 distinction — builder output "byte-identical wire bytes" vs validator "result-identical (same success/error + offending tag)" — applied consistently at every locus? [Consistency, SC-004, FR-009]
- [ ] CHK012 Are the library target names (`fixpp_builders_<ver>`/`fixpp_validators_<ver>` + `fixpp::builders::<ver>`/`fixpp::validators::<ver>` aliases) identical across data-model, cmake-targets, and tasks? [Consistency, contracts/cmake-targets]
- [ ] CHK013 Is the validator-traits home stated consistently as `validators/traits.hpp` (with `groups.hpp` DATA-ONLY) everywhere, with no residual "traits in groups.hpp" or "traits TU" wording? [Consistency, data-model Entity 1/1b]

## Acceptance-Criteria Quality (Measurability)

- [ ] CHK014 Is SC-001's "order of magnitude below" anchored to the measured monolith baseline (~3.6 GiB RSS) and routed to a concrete witness (compile-bench record), not a hard ctest gate? [Measurability, SC-001, research R9]
- [ ] CHK015 Is SC-002 ("link only used messages") objectively verifiable via `nm` on a subset-linked binary given per-message `.o` archive granularity? [Measurability, SC-002]
- [ ] CHK016 Is SC-003 ("zero validator machine code") objectively verifiable via `nm` on a builder-only binary? [Measurability, SC-003]
- [ ] CHK017 Is SC-006 tied to the sanitizer-instrumented CI legs' peak RSS under the 16 GB runner limit (the binding constraint), not the uninstrumented build? [Measurability, SC-006, Clarifications §Gate A round 1 — main-CI OOM]
- [ ] CHK018 Is SC-007 expressed as a checkable property (byte-stable content + stable file NAME-set + COUNT), not just "deterministic"? [Measurability, SC-007, research R6]

## Scenario & Edge-Case Coverage

- [ ] CHK019 Are ODR requirements for the mix case (force-inline one message + link the rest, sharing a group-plan) specified: no duplicate symbol, single trait definition? [Coverage, FR-007, research R2a]
- [ ] CHK020 Is the both-libs-linked case (builder + validator, the common consumer) covered by a no-duplicate-symbol requirement? [Coverage, research R2a leg iii]
- [ ] CHK021 Is the "shared `groups.hpp` included from many per-message headers" case covered by an include-once requirement? [Edge Case, FR-012]
- [ ] CHK022 Is the completeness-proof semantic shift (address-of-declaration proves LINK; compile-proof at the lib target) stated so coverage is not read as weakened? [Coverage, research R7]
- [ ] CHK023 Are requirements defined so an admin-only / no-builder version (vt11) yields a coherent (possibly empty) target rather than a broken build? [Edge Case, spec §Edge Cases, A7]

## Dependencies & Assumptions

- [ ] CHK024 Is the built-from-source client-toolchain assumption (A3) documented, with its consequence (shared `Args` ABI soundness) and the cross-toolchain out-of-scope alternative (C-ABI runtime builder) named? [Assumption, A3]
- [ ] CHK025 Is the #197-removal's dependency on a CI evidence run over the python-bindings sanitizer legs (not local `/speckit-verify`) documented, with the split-follow-up-PR fallback? [Dependency, research R8]
- [ ] CHK026 Is the zero-core-change assumption (A8/FR-011) stated with a verifiable check (grep of `src/`/`capi/`/`bindings/`/`python`)? [Assumption, FR-011]

## Ambiguities & Conflicts

- [ ] CHK027 Is the constitution AMEND obligation (Article I §1 / XVIII §7, v0.9→v0.10 at merge) unambiguously assigned to a task and gated so it is not dropped or deferred past merge? [Conflict, plan §Constitution Check, tasks T035]
- [ ] CHK028 Is the single-body-source variant (research R1) reconciled with the golden name/COUNT determinism contract so a chosen emission form cannot silently break FR-010? [Ambiguity, research R1/R6]

## Notes

- These items test whether the REQUIREMENTS are well-written — not whether the implementation works. Dispositions belong to the checklist-audit (pipeline step 9): PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>.
