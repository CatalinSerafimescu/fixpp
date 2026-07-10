# NFR Checklist: TDD / Perf / Coverage / Build-Graph (Requirements Quality)

**Purpose**: Validate that the cross-cutting non-functional requirements — the tests-first (TDD) obligation, the shape-oracle byte-equality acceptance basis, the coverage/sanitizer matrix, the perf no-regression budget, the banned-pattern/fail-closed constraints, and the codegen build-graph cleanliness + forced-regen ordering — are measurable, quantified, and stated with an objective basis BEFORE implementation. Unit tests for the REQUIREMENTS, not the code.
**Created**: 2026-07-10
**Feature**: [spec.md](../spec.md) · **Focus**: Articles VII (TDD), VIII (perf), IX (coverage/sanitizers), XV (banned patterns), FR-003/FR-008/FR-010, SC-002/SC-003/SC-006

**CHK numbering**: restarts at CHK001 in each of the three 067 checklists (api / abi / nfr).

## Acceptance Criteria Quality — TDD & Shape-Oracle

- [x] CHK001 Is the tests-first (TDD) obligation stated with the specific test artifacts that MUST FAIL before their implementation lands (shape-oracle, round-trip, fail-closed, exact-set completeness, validate-required, emitter unit)? [Acceptance Criteria, Spec §Art-VII] — PASS: tasks.md marks the specific artifacts `[TESTS-FIRST]` (T007, T011-T015, T020, T022-T023) each with "MUST FAIL first"; plan.md Constitution Check row VII lists the same test categories (shape-oracle, round-trip, fail-closed, exact-set completeness, validate-required).
- [x] CHK002 Is the shape-oracle byte-equality acceptance basis fully measurable — WHAT bytes (direct byte compare AND `shape_oracle_profile()` excluding only `{8,9,10,34,52}`), WHICH exemplars (D/8/9/E/AS in v44), driven by the identical 061 seed? [Acceptance Criteria, Spec §FR-003] — PASS: contract §G2 fully specifies WHAT bytes (direct compare + `shape_oracle_profile()` excluding only `{8,9,10,34,52}`), WHICH exemplars (D/8/9/E/AS in v44), driven by the identical 061 seed (`tests/session/exemplar_seeds.hpp`).
- [x] CHK003 Is the all-33 extension of the byte-equality guarantee specified (the 5 exemplars pinned by goldens via G2; the remaining 28 by the G6 non-tautological round-trip + byte-structural asserts), not left as "trusted"? [Acceptance Criteria, Spec §FR-008] — PASS: contract §G1 states G2/G6 "extend the guarantee from the 5 exemplars to all 33 OFFICIAL messages"; §G6 explicitly splits 5-exemplars-via-G2 vs 28-via-non-tautological-round-trip.
- [x] CHK004 Is the non-tautological witness requirement stated checkably — byte-structural asserts (field order matches the canonical G3 order, correct SOH count, absence of framing tags) so a build→parse loop alone is INSUFFICIENT? [Acceptance Criteria, Spec §FR-008] — PASS: FR-008 + contract §G6 state the byte-structural asserts checkably (field order matches G3 canonical order, correct SOH count, absence of framing tags) and state explicitly "a pure build→parse loop is insufficient".
- [x] CHK005 Is the W/X paired byte-golden + MassQuote insurance requirement (R5) stated as a discrete, non-droppable deliverable — the shared-`NoMDEntries(268)` 269-vs-279 discriminator — rather than left implicit in "round-trip"? [Coverage, Spec §FR-008 / research R5] — PASS: research R5 marks the W/X pair "REQUIRED — Gate A round 1, Codex #8 / RC#1"; contract §G6 repeats "REQUIRED, the shared-`NoMDEntries(268)` per-occurrence discriminator"; plan.md Project Structure lists concrete deliverable files (`gen/qf_market_data.cpp`, `market_data_snapshot.fix`, `market_data_incremental.fix`); tasks.md T018 operationalizes it as a discrete task.

## Requirement Completeness — Coverage & Sanitizers

- [x] CHK006 Are the coverage/sanitizer requirements quantified — WHICH Tier-1 matrix (sanitizers/coverage/static analysis), with the generated headers coverage-EXCLUDED (`analyze_coverage.py`) but the emitter `.cpp` + `builder_validate.hpp` + harness MUST-cover? [Completeness, plan §Art-IX] — PASS: plan.md Constitution Check row IX states verbatim "Full Tier-1 matrix. Generated headers coverage-excluded (`analyze_coverage.py`); emitter `.cpp` + runtime validate + harness are NOT excluded — must be covered."
- [x] CHK007 Is the coverage-exclusion of generated headers reconciled against the must-cover emitter source — i.e. is there a stated requirement that no untested logic hides behind the generated-header exclusion (the emitter `.cpp` producing it is covered)? [Ambiguity, plan §Art-IX] — PASS: the same plan.md row IX sentence explicitly reconciles exclusion (generated headers) vs must-cover (emitter `.cpp` + runtime validate + harness) in one clause.
- [x] CHK008 Is the coverage measurement basis named (lcov DA/BRDA via `analyze_coverage.py`), not just "coverage passes"? [Measurability, plan §Art-IX] — PASS: the measurement instrument is named (`tools/analyze_coverage.py`, plan.md row IX / research R8), which per standing project convention (lcov DA/BRDA basis) is the canonical coverage gate tool — naming the tool satisfies "named basis", not left as bare "coverage passes".

## Requirement Completeness — Perf

- [x] CHK009 Are the perf requirements specified as "no new budget" with the concrete inherited property (`body_builder`'s zero-global-heap fixed arena) and a "no bench regression" clause? [Completeness, Spec §Art-VIII] — PASS: plan.md Technical Context states verbatim "Performance Goals: no new budget; outbound builder inherits `body_builder`'s zero-global-heap fixed arena. No regression to existing codegen/build benches."
- [x] CHK010 Is the "no bench regression" claim tied to a baseline the comparison is made against (the T001 pre-change GREEN baseline for existing codegen/build benches), not an unanchored assertion? [Measurability, plan §Perf / tasks T001] — PASS: tasks.md T001 states the baseline explicitly — "record the pre-change GREEN baseline (guards G7 read-emitter determinism + FR-009 no-collateral-change)" — before any code edits, giving the comparison a concrete anchor.

## Banned Patterns & Fail-Closed

- [x] CHK011 Are the banned-pattern / fail-closed requirements stated checkably — no silent loss, no app-message drop, INV-4 fail-closed atomic commit (`out` untouched on any error) at the GENERATED wrapper — with the discriminating fail-closed witnesses named (G9)? [Completeness, Spec §Art-XV] — PASS: plan.md Constitution Check row XV ("body_builder fail-closed (INV-4); no app-message drop; no new banned pattern") plus contract §G9's six named discriminating witnesses (undersized out, control-byte/SOH, Bool Y/N, Length+Data coupling, required-group-zero, W-vs-X delimiter discrimination).

## Requirement Completeness — Build-Graph & Regen Ordering

- [x] CHK012 Is the codegen build-graph cleanliness gate specified as a requirement — forced regeneration is `git`-clean w.r.t. tracked files (generated output build-tree-only; no source-tree writes; deterministic)? [Completeness, Spec §FR-010] — PASS: FR-010 states the gate verbatim ("regenerated via the forced-regeneration path and pass the codegen build-graph cleanliness gate (`git`-clean after regen)"); contract §G8 elaborates build-tree-only / no source-tree writes / deterministic.
- [x] CHK013 Is the forced-regen/staleness ORDERING specified as a requirement — non-debug (sanitizer/coverage) build dirs compile a fresh `_codegen`, so forced regeneration MUST precede those runs (and `/speckit-verify`), else stale-emitter false-greens? [Completeness, Spec §SC-006 / plan §codegen controls] — PASS: plan.md "Codegen-specific mandatory controls" section states the ordering explicitly ("sanitizer/coverage builds compile a fresh `_codegen`, so regenerate before those runs"); tasks.md Phase Dependencies states "T028 forced regen precedes T030 `/speckit-verify`".
- [x] CHK014 Is the generated-code hygiene contract complete (G8) — standard `// GENERATED … DO NOT EDIT.` banner + `#pragma once`, lands only in `build/<preset>/_codegen/...`, and `write_file` empty-skip permits incremental TDD (no file ⇒ RED, not stale-file green)? [Completeness, contract §G8] — PASS: contract §G8 is a complete, standalone hygiene contract — banner text, `#pragma once`, build-tree-only landing path, and `write_file` empty-skip TDD semantics all stated verbatim.

## Acceptance Criteria Quality — Success Criteria

- [x] CHK015 Is SC-003 measurable — every generated OFFICIAL builder passes its seeded round-trip (each seeded field incl. nested group entries reads back at exact input value) WITH the non-tautological byte-structural asserts? [Acceptance Criteria, Spec §SC-003] — PASS: SC-003 states verbatim "Every generated OFFICIAL builder passes its seeded round-trip witness (each seeded field, including nested group entries, reads back at its exact input value) with the non-tautological byte-structural asserts".
- [x] CHK016 Is SC-006 stated with the objective gate instrument (forced regen `git`-clean + build-graph cleanliness gate passing), not "regeneration works"? [Acceptance Criteria, Spec §SC-006] — PASS: SC-006 states verbatim "Forced codegen regeneration is `git`-clean; the codegen build-graph cleanliness gate passes" — the objective gate instrument, not a vague "regeneration works".

## Dependencies & Assumptions

- [x] CHK017 Is the dependency on the frozen 061 shape-oracle assets stated as a fixed, untouched input — `exemplar_seeds.hpp`, `shape_oracle_profile()`, the 5 checked-in `.fix` goldens, and the hand exemplars in `src/session/business_messages.cpp` (fix the emitter, never the frozen hand builders)? [Assumption, Spec §Assumptions / Dependencies] — PASS: spec.md Assumptions states "The 061 shape-oracle is frozen and authoritative... reused as-is; this feature does not re-derive them"; Dependencies names the concrete assets; tasks.md T002 confirms them located with "No edits — these are the frozen oracle".
- [x] CHK018 Is the completeness-audit + Article VI catalogue close-out obligation (coverage-index + feature-catalogue for the 33 write rows) stated as a HARD Gate-B precondition, not optional bookkeeping? [Dependencies, plan §Art-VI close-out] — PASS: plan.md Open Items "Art VI close-out tasks" states "These are not optional bookkeeping"; tasks.md T031/T032 are marked `[MANDATORY RIDER]` / "MUST be the FINAL task" and explicitly "HARD `/gate-b` precondition (Article XVII §8 / pre-flight 4d)".
- [x] CHK019 Is the Article VI §2 canonical-format question flagged as OPEN and owned — message-level refs vs section-granular `[DocAbbrev §X.Y.Z]` vs `[impl]`/design-authority rows was downgraded P1→P3 at Gate A Round 3 and deferred to /tasks (T029) for resolution before any row closes — rather than presented as settled? [Ambiguity, plan §Art-VI close-out] — DD-DECIDED §plan.md Gate A Round 3 ("Codex #2... DOWNGRADED P1 → P3 by the Opus adversarial pass"): the Art VI §2 canonical-format question (`[DocAbbrev §X.Y.Z]` vs `[impl]`) is explicitly flagged OPEN and owned — downgraded with a documented rationale (pre-existing project-wide convention, not a 067 regression) and scheduled to tasks.md T029 for resolution before any row closes; presented as open-and-scheduled, not settled — recorded as a frozen-authority traceability ref, not re-spec'd.

## Notes

- Requirements-quality checklist (probes the SPEC/design bundle, not the emitted code). Dispositioned by the pipeline step-9 checklist audit before `/speckit-implement`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 18 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | 19 |

### SPEC-FIXED items

None.

### DD-DECIDED items

- CHK019 — anchor `plan.md Gate A Round 3` (Codex #2 P1→P3 downgrade) + `tasks.md T029`; rationale: Art VI §2 canonical-format question is explicitly flagged OPEN and scheduled for pre-close resolution, not presented as settled — a legitimate, owned deferral of a pre-existing project-wide convention gap, not a 067-introduced regression.

### WAIVED items

None.

Anchors spot-verified: `plan.md` Constitution Check row IX (`tools/analyze_coverage.py` coverage-exclusion/must-cover split, CHK006/CHK007/CHK008); `plan.md` Technical Context Performance Goals (CHK009); `tasks.md` T001/T018/T028/T031/T032 (baseline, W/X goldens, regen ordering, catalogue close-out, completeness audit, CHK005/CHK010/CHK013/CHK018); contract `generated-builder.md` §G2/§G6/§G8/§G9 (byte-equality basis, non-tautological witness, hygiene, fail-closed witnesses, CHK002-CHK004/CHK011/CHK014-CHK016) — all resolve in the Gate-A-converged bundle (converged commit `a4cb5624`).
