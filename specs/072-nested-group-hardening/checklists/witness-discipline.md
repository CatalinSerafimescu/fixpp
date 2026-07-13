# Checklist: Witness Discipline — RED-First, Non-Vacuity, Mutation-Proof, No-Golden Reconfigure (Requirements Quality)

**Purpose**: Validate that the TDD-witness requirements (census trip-proof + non-vacuity, load-reject RED-before-guard, depth-3 discrimination mutation-proven with the wire-`MsgType` registration constraint, the named validator witness, and the no-golden clean-reconfigure verification) are specified completely, unambiguously, and with measurable acceptance — so the shipped behavior is PROVEN, not inferred. (Unit tests for the requirements.)
**Created**: 2026-07-12
**Feature**: [spec.md](../spec.md) · **Focus**: RED-first / mutation-proven witnesses, non-vacuous census, no-golden reconfigure, sanitizer discipline

## Census witnesses — RED-first & non-vacuity

- [x] CHK017 - Are the FR-001/FR-002 census assertions required to be TRIP-PROVEN (RED on an injected/mutated bad dict — a nested==parent delimiter for FR-001; a shared parent/child scalar member for FR-002) before being asserted GREEN over the 9 shipped dicts, so a never-red census proves nothing? [Measurability, Spec §FR-001/FR-002, Quickstart §Part-A-1, Constitution Art VII §3] — PASS: tasks.md T002/T003 state "Prove RED first" explicitly for both census assertions (injected nested==parent delimiter for FR-001; injected shared scalar member for FR-002) before asserting GREEN over the 9 shipped dicts. Consistent with plan.md's Constitution-Check row VII §3 (TDD red-green mandatory).
- [x] CHK018 - Is the non-vacuity assertion (> 0 group-declaration sites / member-sets observed per dict claimed covered) specified as a HARD in-test invariant (mirroring the existing `EXPECT_TRUE(saw_fix44_295_collision)`), so FIX40/41/42 cannot pass vacuously? [Measurability, Spec §FR-001/FR-002/SC-001] — PASS: FR-001/FR-002 both state "The assertion MUST prove non-vacuous coverage (assert it observed > 0 group-declaration sites for each dictionary it claims to cover)", explicitly mirroring the existing hard invariant. Verified on-disk: `EXPECT_TRUE(saw_fix44_295_collision)` exists exactly at `tests/dictionary/reused_tag_census_test.cpp:281`.
- [x] CHK019 - Is the FR-002 FIX40/41/42 case required to be recorded as an EXPLICIT unpinned residual (NOT counted as covered) when per-group member sets are not structurally recoverable, rather than allowed to pass as if covered? [Completeness, Spec §FR-002/FR-013d] — PASS: FR-002 states "those three MUST be recorded as an explicit unpinned residual (FR-013 / catalogue) rather than allowed to pass vacuously as if covered"; FR-013(d) restates it as a catalogue residual. tasks.md T003 mirrors the same requirement. No new value-typed entity involved — realizability N/A.

## Load-reject witness — RED-before-guard & asymmetry

- [x] CHK020 - Is the load-reject witness required to be RED before the guard exists — a colliding inline XML loaded via `XmlLoader::load_from_string` throws `group_delimiter_collision_error` (catchable as `xml_parse_error`), produces NO view, does not crash — with a conforming variant loading fine? [Measurability, Spec §SC-002, Quickstart §Part-A-2, Task T004] — PASS: tasks.md T004(a)/(b) state this exactly: colliding dialect MUST throw and is "RED until the guard (T006) exists"; a conforming variant "loads fine". SC-002 states the measurable 100%-of-cases outcome.
- [x] CHK021 - Is the FR-004 asymmetry given a DIRECT negative witness (a scalar-member-collision-only dialect with disjoint delimiters MUST load successfully), rather than left true only "by construction"? [Coverage, Spec §FR-004, /analyze F2, Task T004(c)] — PASS: tasks.md T004(c) requires exactly this — "a dialect that shares a scalar member tag between a parent group and its nested child but keeps disjoint delimiters MUST load successfully (no throw), pinning that the load guard enforces the delimiter convention ONLY". This is a direct negative witness, not a by-construction inference. Note: I could not locate a persistent `/speckit-analyze` report artifact for 072 to confirm the "F2" provenance tag itself (see Escalations in final report) — the disposition rests on the verified tasks.md/spec.md content, not on that citation.

## Depth-3 discrimination witness — mutation-proven & codegen-bound

- [x] CHK022 - Is the depth-3 witness required to be MUTATION-PROVEN RED on the pre-fix (unpushed) emitter output (resolves the WRONG bare-fallback member), not a coverage-only pass? [Measurability, Spec §FR-011/SC-003, Contract typed-context §Witness] — PASS: FR-011 states "MUST assert the correct depth-3 member post-fix and MUST be mutation-proven RED on the pre-fix emitter output"; contract typed-context.md §Witness and tasks.md T008 restate it. Acceptance Scenario 2 (US2) makes it a scenario-level requirement, not merely a task note.
- [x] CHK023 - Is the wire-`MsgType` registration constraint stated as a correctness PRECONDITION (register the context store under the wire value `"i"`, NOT the name "MassQuote", else the runtime read misses the store and the witness false-greens), and is the real-generated-type + hand-built-`table_view` vehicle required (the typed layer is codegen-bound to shipped dicts, all inert)? [Clarity, Spec §FR-011/Edge-Cases, Research §D-B6] — PASS: FR-011 states the precondition verbatim, including the false-green failure mode if registered under "MassQuote" instead of "i". Verified on-disk: `dictionaries/FIX44.xml:943` declares `<message name='MassQuote' msgtype='i' msgcat='app'>` — the wire value really is `"i"`, confirming the precondition is not a hypothetical. Edge Cases section requires the real-generated-type + hand-built-`table_view` vehicle explicitly (codegen wall documented).
- [x] CHK024 - Is the C-ABI depth-3 parity assertion (SC-004) scoped honestly as SHOULD / best-effort (only if a C-ABI depth-3 read is constructible on the same hand-built dict), NOT a required dual witness, so Part B's proof rests on the typed discrimination witness? [Clarity, Spec §FR-011/SC-004/Edge-Cases] — PASS: FR-011 states "SHOULD assert typed≡C-ABI agreement at depth-3... if a C-ABI depth-3 read is constructible... a production dispatch-path loopback witness is NOT required"; Edge Cases explicitly says the C-ABI cross-check is "a best-effort parity assertion, not a required dual witness". Consistent, not overclaimed.

## Validator witness — named, mutation-proven, split-aware

- [x] CHK025 - Is the validator witness NAMED (`ValidatorNestedMembership_Depth2ContextMissUnderFlatWalk`) and required mutation-proven RED on the flat walk / GREEN after the rewrite, with its lifecycle tied to the SPLIT-TRIGGER (it moves with FR-010 to the follow-up if the split fires)? [Completeness, Spec §FR-010b, Quickstart §Part-B-2, Task T011] — PASS: FR-010(b) names the witness exactly and requires mutation-proof (RED on flat walk, GREEN after rewrite); tasks.md T011 states "If T012 is split out, this witness moves with it," tying its lifecycle to the SPLIT-TRIGGER as required. Realizability N/A (named test, no value-typed entity).

## No-golden reconfigure verification — Measurability

- [x] CHK026 - Is SC-005 verification specified as a CLEAN reconfigure (delete `_codegen/`) across ≥ debug + sanitizer + coverage configs — explicitly NOT a golden-file diff (no checked-in flyweights) — with the stale-`_codegen`-header false-green/red hazard called out? [Measurability, Spec §FR-009/SC-005, Research §D-B5] — PASS: FR-009/SC-005 state clean reconfigure (delete `_codegen/`) across debug/sanitizer/coverage, explicitly "no golden-file diff exists — flyweights are build-tree only"; research D-B5 documents the stale-header false-green/red hazard by name (`project_codegen_emitter_staleness`). tasks.md T010 operationalizes it (`rm -rf <build>/_codegen`).
- [x] CHK027 - Is the SC-005 outcome measurable — shipped-dict typed-read + strict-validation RUNTIME results BYTE-IDENTICAL to pre-fix (the defect is inert on every shipped dict, only generated source content changes at descent sites)? [Measurability, Spec §SC-005] — PASS: SC-005 states "produce byte-identical runtime results versus pre-fix, verified across at least the debug, sanitizer, and coverage build configurations"; FR-009's Acceptance Scenario 4 (US2) restates it with the same wording. Objective and measurable.

## Sanitizer discipline

- [x] CHK028 - Is any sanitizer finding on the touched loader / emitter / validator paths required to be treated as a REAL defect (validated under ASan/UBSan/TSan, not asserted benign)? [Consistency, Plan §Constitution-Check IX §2] — PASS: plan.md Constitution-Check table row "IX §2 | all | ASan/UBSan/TSan Tier 1 pass" applies project-wide (Art. IX §2), and this is reinforced by the project-level default-real sanitizer policy (CLAUDE.md "Testing & Verification": treat every sanitizer finding as a real defect until disproven). Consistent — no feature-local carve-out weakens it.

## Notes

- Requirements-quality checklist (probes the SPEC's witness obligations, not the implementation). Dispositioned by the pipeline step-9 checklist audit before `/speckit-implement`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 12 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 12 |

### SPEC-FIXED items
None.

### DD-DECIDED items
None. All witness-discipline requirements are stated directly in spec.md (FR-001/002/004/009/010b/011, SC-002/003/004/005) and cross-confirmed in tasks.md/quickstart.md/research.md — dispositioned PASS as written, not deferred to a design doc.

### WAIVED items
None.

Anchors spot-verified (all resolve against on-disk source at branch `072-nested-group-hardening`): `tests/dictionary/reused_tag_census_test.cpp:281`; `dictionaries/FIX44.xml:943` (`msgtype='i'` for MassQuote); `src/dictionary/dictionary.cpp:335,368`; `dictionaries/FIX40.xml:701,823,848`. No `.specify/2x-*.md` design-doc anchor exists for this feature; authoritative anchors are research.md D-A/D-B decisions + plan.md `## Gate A`, both verified in the sibling `correctness-and-abi.md` audit and reused here.

**Note on CHK021's "/analyze F2" citation**: no persistent `/speckit-analyze` report artifact was found anywhere in the repo for `072-nested-group-hardening` (checked `research/reviews/`, `.specify/decisions/`, git log/status). The CHK021 requirement itself is real and verified directly against tasks.md T004(c) and spec.md FR-004, so the disposition (PASS) does not depend on confirming the "/analyze F2" provenance — but the orchestrator should confirm `/speckit-analyze` (pipeline step 6) actually ran for this feature before `/speckit-implement`, since `plan.md:34,99` and `quickstart.md:34` still describe it as a pending "MANDATORY before /implement" item rather than a completed step. See Escalations in the final report.
