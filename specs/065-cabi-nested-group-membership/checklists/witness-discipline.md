# Checklist: Witness Discipline — RED-First, Dual FR-011 Equivalence, Degradation Pin (Requirements Quality)

**Purpose**: Validate that the TDD witness requirements (mutation-proven RED before GREEN, the two mandatory FR-011 C-ABI≡C++ witnesses, and the FR-008 dict-free degradation pin) are specified completely, unambiguously, and with measurable acceptance — so the shipped behavior is proven, not inferred. (Unit tests for the requirements.)
**Created**: 2026-07-10
**Feature**: [spec.md](../spec.md) · **Focus**: RED-first TDD, dual equivalence witnesses, degradation safety

## RED-first discipline — Measurability

- [x] CHK014 - Is the pre-existing `GTEST_SKIP`'d witness required to be MUTATION-PROVEN RED on the pre-fix code (un-skip only, no source fix → the trailing-tag-on-last-instance assertion must FAIL returning `OK`/value) before the fix lands, with an objective pass/fail signal? [Measurability, Spec §FR-010/SC-001, Constitution Art VII §3, Task T001] — **PASS:** FR-010 ("It MUST be mutation-proven RED on the pre-fix code"); T001 operationalizes it exactly (temporarily delete only `GTEST_SKIP()`, confirm the `999`-on-`nested[last]` assertion FAILS, capture evidence, restore skip). Verified live: the skipped test + its exact discriminator exist at `tests/capi/message_read_test.cpp:1101`.
- [x] CHK015 - Is the un-skip lifecycle specified to KEEP all of the witness's positive assertions (nested_count, per-instance member values, trailing member reachable at the outer index), not merely flip the one discriminator? [Completeness, Spec §FR-010, Task T007] — **PASS:** FR-010 states this verbatim; T007 restates it ("keep ALL positive assertions").
- [x] CHK016 - Is the required witness message shape concrete enough that the witness cannot degenerate into a weaker proxy (a multi-entry nested group followed by a declared trailing outer member — e.g. FIX44 `NoLegs(555)` → `NoLegSecurityAltID(604)`×2 → trailing `LegQty(687)`)? [Clarity, Spec §FR-011, Task T008] — **PASS:** FR-011 pins the exact concrete tag shape; T008 repeats it identically for witness (a).

## Dual FR-011 equivalence witnesses — Coverage & Consistency

- [x] CHK017 - Is it a stated requirement that BOTH FR-011 witnesses are mandatory — (a) a direct `as_table_view()` extent-arithmetic witness AND (b) an engine-loopback dispatch-path witness — with the contract (C7) requiring both and NOT narrowable to one? [Coverage, Spec §FR-011/SC-005, Contract §C7, Clarification 2026-07-09] — **PASS:** the 2026-07-09 Clarification records "A: Both"; FR-011 states "two real-dictionary witnesses"; Contract §C7 states "C7 requires both — the loopback witness pins the exact parked-P1 regression seam and must not be narrowed away."
- [x] CHK018 - Is witness (a)'s build home specified precisely and self-consistently (inline-XML `load_from_string`→`as_table_view()` in `message_read_test.cpp`, mallocnesia-safe, NO `FIXPP_DICT_DATA_DIR`, per the 296 precedent), with no residual mandate to use the shipped `FIX44.xml`? [Consistency, Spec §FR-011(a), Plan §Project-Structure, Quickstart §6(a)] — **PASS:** FR-011(a), Plan §Project-Structure (tests/capi/message_read_test.cpp section), and Quickstart §6(a) all state this identically. Verified live: the cited precedent `TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent` exists at `message_read_test.cpp:1735` (plan.md's `:1778-1791` cite is the documented pre-066-restructure drift, tasks.md re-anchors it to `:1735` — not a CHK-failing defect per the tasks.md-is-authoritative rule).
- [x] CHK019 - Is witness (b)'s build home specified precisely (a dict066-style engine-loopback target carrying `FIXPP_DICT_DATA_DIR`+`FIXPP_CAPI_FEATURE_B_INCLUDES` via `capi_dict066_loopback_support.hpp`; CANNOT live in `message_read_test.cpp`), and its distinct value (production msg_type/parent-path threading — the 063 Gate-B RC#1 empty-`msg_type` class) stated? [Clarity, Spec §FR-011(b), Contract §C7(b), Quickstart §6(b)] — **PASS:** FR-011(b), Contract §C7, and Quickstart §6(b) all state this precisely and consistently; T009 operationalizes it. Verified live: the dict066 loopback red target + its `FIXPP_DICT_DATA_DIR`/`FIXPP_CAPI_FEATURE_B_INCLUDES` compile-defs exist at `tests/capi/CMakeLists.txt:418-432`.
- [x] CHK020 - Is the typed-path writability caveat specified as a requirement — equivalence is asserted via genuine member values/extent agreement + the trailing tag's ABSENCE from the typed nested entry's corrected extent (`field_value()` escape hatch), NOT by asking the typed nested accessor for the non-member trailing tag directly? [Consistency, Spec §FR-011/SC-005, Quickstart §6 writability-caveat] — **PASS:** FR-011's "Writability caveat" paragraph and Quickstart §6's identically-worded caveat both state this exactly, including the `field_value()` escape-hatch mechanism and the explicit "do NOT" prohibition.
- [x] CHK021 - Is SC-005 equivalence explicitly scoped to the DEPTH-1 layout (depth-≥2 equivalence excluded and cross-referenced to the scope boundary)? [Clarity, Spec §SC-005, Contract §C5a] — **PASS:** SC-005 states "For the represented depth-1 layout... (Depth-≥2 equivalence is out of scope — see Edge Cases.)"; Contract §C5a cross-references SC-005/C7 to the same depth-1 scope.

## FR-008 degradation pin & no-regression — Coverage

- [x] CHK022 - Is the dict-free (membership-unavailable) degradation requirement stated as SAFE (no crash/UB) AND no-regression-vs-today's-positional-behavior, with a named acceptance path (null-predicate `build_nested_subview`/`group()` fallback)? [Completeness, Spec §FR-008, Research §Decision-3] — **PASS:** FR-008 states the safe-degradation requirement exactly; Research Decision-3 names the precise mechanism (`build_nested_subview` builds with the null predicate; `group()` dict-free fallback at `offset_table.cpp:545-547`) and cites the confirming loopback test.
- [x] CHK023 - Is FR-008 given a DIRECT verification obligation (a dict-free degradation witness, or an explicit source-verified-unreachability waiver) rather than left asserted-by-construction with no coverage? [Coverage, Task T011/T016, /analyze E-1] — **PASS:** T011 adds the direct dict-free degradation witness (constructs a genuinely dict-free view, descends via C-ABI, asserts byte-identical-to-today behavior under ASan/UBSan), with an explicit fallback to a source-verified-unreachability waiver recorded in T016 if the harness genuinely cannot mint a dict-free cursor. tasks.md's "Requirement coverage" section confirms this closes `/analyze` E-1 (the one prior MEDIUM finding). This is exactly the closure the task brief anticipated.
- [x] CHK024 - Is the SC-002 no-regression requirement measurable — a pre-un-skip grep for any existing `NestedGroup*` case pinning the OLD positional `nc` on a non-terminal zero/short count that the membership path could flip, so no silent regression slips in? [Measurability, Spec §SC-002, Plan §Phase-2-note, Research §New-3] — **PASS:** the requirement itself is measurable and self-contained in T010 ("grep the existing `NestedGroup*` suite... confirm NONE regress"), independent of the internal cross-reference. Non-blocking note: `plan.md:113` / `tasks.md:57` both cite "research §New-3" as this finding's origin, but `research.md` has no item literally labeled "New #3" (a dangling internal backref, same category as CHK010's loose section pointer) — the requirement's substance is unaffected; flagged for the orchestrator to clean up the citation if desired, not dispositioned as a requirements-quality gap.
- [x] CHK025 - Is any sanitizer finding on the reworked nested path (sub-table lifetime + slice extents) required to be treated as a real defect (validated under ASan/UBSan/TSan, not asserted)? [Consistency, Plan §Sanitizers, Constitution Art IX, Task T013] — **PASS:** Plan "Sanitizers as real defects" line + Constitution-Check gate reference Art IX; T013 operationalizes it as a required verification task, not an assertion.

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
None.

### WAIVED items
None.

Anchors spot-verified: skipped witness = `tests/capi/message_read_test.cpp:1101` (live); 296 precedent `TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent` = `message_read_test.cpp:1735` (live; plan.md's `:1778-1791` is documented pre-066 drift, not a defect per the tasks.md-authoritative rule); dict066 loopback red target + compile-defs = `tests/capi/CMakeLists.txt:418-432` (live); `stored_group_context()` private / `gen_` `#ifndef NDEBUG` = `offset_table.hpp:257` / confirmed live — all resolve against the current bundle + source tree (2026-07-10).

**Non-blocking observation (not a disposition-affecting defect)**: `plan.md:113` / `tasks.md:57` cite "research §New-3" for the SC-002 pre-un-skip-grep finding (CHK024), but `research.md` has no item literally labeled "New #3" — a dangling internal cross-reference. The requirement (T010's grep-then-confirm-none-regress acceptance criterion) is self-contained and measurable without it. Left for the orchestrator to clean up if desired; not fixed by this audit (fixing it would mean either fabricating new research content to match the label, or editing plan.md/tasks.md outside a CHK-driven disposition — both out of this audit's scope).
