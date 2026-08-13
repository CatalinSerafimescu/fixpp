# Codegen Requirements Quality Checklist: Structural Repeating-Group Detection

**Purpose**: Validate that this feature's **requirements** about the codegen tier — the re-pointed
emitters, the regenerated `v42` artifacts, the re-instated builder tier, and the count discipline that
makes the deltas falsifiable — are complete, unambiguous, consistent, and measurable.
Audience: **Gate B reviewer** (formal pre-merge gate).
**Created**: 2026-07-30
**Feature**: [spec.md](../spec.md) · [research.md](../research.md) D-3/D-7/D-9a/D-10 · contract
[group-detection.md](../contracts/group-detection.md) C1.2/K5/K8

**Scope note.** This is the feature's densest count-discipline area, and the one where a plausible
number that was never derived would survive review. Several items below interrogate *how* a count is
established, not whether it is stated.

## Requirement Completeness

- [x] CHK001 Are all emitter detection sites enumerated as an exhaustive census rather than as a representative list? [Completeness, Research §D-7] — PASS: D-7's site inventory is explicitly headed "the full census — no site may be missed", and includes a second table for the sites carrying no `NumInGroup` token that still needed a stated disposition.
- [x] CHK002 Is the census's counting unit (line-sites vs disposition rows) stated so the site totals are reconcilable? [Clarity, Plan §Scale/Scope, Research §D-7] — PASS: D-7 opens "Counting unit: one row = one *line*-site", and `plan.md` records the correction from an earlier revision that mixed units ("nine sites" counted disposition rows against a line-site total).
- [x] CHK003 Are the emitter occurrences that must **not** change dispositioned explicitly, rather than omitted? [Completeness, Research §D-7] — PASS: D-7 tables list explicit NO CHANGE / FOLD dispositions for `emit_manifest.cpp:73`, `gen_util.hpp:162`, both loader type-name tables, `field_type.hpp:70`, `message_write.cpp:134-137`, the four capi write sites, and `dictionary.cpp:402-405`/`:463`.
- [x] CHK004 Is the full set of emitted `v42` artifacts enumerated, including which of them are *not* emitted at all? [Completeness, Spec §FR-016] — PASS: FR-016 lists all five (`Fields.hpp`, `Messages.hpp`, `Validator.hpp`, `Reify.hpp`, `NormativeReferences.md`) and states explicitly `Manifest.txt` is not one, with the `emit_manifest.cpp:154-166`/`main.cpp:29` mechanism cited.
- [x] CHK005 Are the artifacts lacking a checked-in golden identified, with their alternative pin named? [Completeness, Spec §FR-016, §FR-021] — PASS: data-model I-10 states only one of five is golden-pinned; FR-021 names the class-side⟷raw-XML gate for the group axis, FR-016 the regeneration-diff record for the rest.
- [x] CHK006 Is the 078 split builder/validator file set enumerated by role rather than referred to collectively? [Completeness, Spec §FR-007] — PASS: FR-007 enumerates "per-message declaration headers, the shared per-plan groups region plus its umbrella, the validator traits header, and the `all.hpp` umbrella".
- [x] CHK007 Are requirements defined for both `--families` coverage modes, including which one has no golden by convention? [Completeness, Spec §FR-009, Contract §K8] — PASS: FR-009 splits (a) the `all`-mode golden and (b) the `official`-mode structural witness, explicitly citing 078's retirement of the official-mode golden convention (`determinism_test.cpp:898-909`).
- [x] CHK008 Is the driver's version-exclusion removal specified as taking **no** replacement predicate? [Completeness, Spec §FR-010] — PASS: FR-010's second sentence states this directly; verified at the source — `main.cpp`'s `if (ir.ns != "v42")` guard is a single name-test, deleted with nothing replacing it (D-8/T035).
- [x] CHK009 Are requirements stated for how a version with zero application messages continues to self-skip? [Coverage, Spec §FR-010] — PASS: FR-010 — `vt11` self-skips via its "empty application-message registry", not a version predicate.

## Requirement Clarity

- [x] CHK010 Is the codegen-tier predicate named as a specific data member rather than described as "structural"? [Clarity, Spec §FR-006, Research §D-3] — PASS: FR-006 names `VersionIR::group_tags` explicitly as the codegen tier's predicate.
- [x] CHK011 Is the claim that the IR's group ordering is *already* correct stated with its source, so it reads as verifiable rather than assumed? [Clarity, Research §D-3] — PASS: D-3 cites `ir.cpp:80` `walk_level`, keyed on the element name and appending `GroupOrderEntry` unconditionally; verified at the source — confirmed `walk_level` pushes `out_groups.push_back(std::move(entry))` unconditionally on `tag_name == "group"`, only guarding `delimiter_tag` assignment on `members.empty()`.
- [x] CHK012 Is the distinction between group **tags** and emitted **plan headers** made explicit, so neither is used as a file count? [Ambiguity, Plan §Scale/Scope, Research §D-9a] — PASS: data-model I-6a states "18 ≠ 28 ≠ 17 is by construction, not a discrepancy" and D-9a has a dedicated "Why '18 group tags' is not a file count" subsection.
- [x] CHK013 Is the reason the read-tier tag count exceeds the builder-tier tag count stated, rather than left as an unexplained discrepancy? [Clarity, Research §D-9a] — PASS: D-9a states `384 NoMsgTypes`'s only host is the admin message `Logon`, excluded by `emit_builders`' `is_application` gate.
- [x] CHK014 Is "byte-identical" scoped to named artifacts per version rather than asserted globally? [Clarity, Spec §FR-015, Contract §K5] — PASS: FR-015/FR-016a name specific artifacts (`Fields.hpp`, `Validator.hpp`) per version rather than a blanket claim.

## Requirement Consistency

- [x] CHK015 Do the plan-count figures agree across the spec, plan, research, data-model, and contract? [Consistency, Spec §FR-009, Research §D-9a, Contract §K8] — PASS: cross-checked directly — 28 plans/17 tags/226 files (`all`) and 19/11/147 (`official`), registry 39/25, are identical in `spec.md` SC-006, `plan.md` § Scale/Scope, `research.md` D-9a, `data-model.md` Entity 4/6, and `contracts/group-detection.md` K8.
- [x] CHK016 Do the message/application-message counts for the regenerated version agree across every locus that states them? [Consistency, Research §D-9] — PASS: 46 total / 39 app is identical in `spec.md` (FR-016, US2), `research.md` D-9, `data-model.md`, and `plan.md` § Scale/Scope.
- [x] CHK017 Is the official-mode obligation stated consistently as a structural witness in every locus, with no residual reference to a pinned golden? [Consistency, Spec §FR-009, Research §D-8] — PASS: FR-009, data-model Entity 6's golden-corpus row, contract K8, and research D-10 all state "structural witness, not a golden" identically.
- [x] CHK018 Are the artifact-classification requirements consistent with the emitted-artifact enumeration, with no artifact counted that is not emitted? [Consistency, Spec §FR-016, §SC-004] — PASS: FR-016/SC-004 both explicitly exclude `Manifest.txt`; this correction is applied identically in `data-model.md` I-10 and `research.md` D-10.
- [x] CHK019 Does the inherited structural-key guarantee from the predecessor feature remain consistently cited where newly-visible groups enter the builder tier? [Consistency, Spec §Normative References] — PASS: spec § Normative References' B-077-1 row and `tasks.md` T039 both cite the same guarantee for the same event (newly-visible groups entering the builder tier as new ordinaled plans).

## Acceptance Criteria Quality

- [x] CHK020 Is each emitted-count delta required to carry a **by-construction** reconciliation rather than a regeneration transcript? [Measurability, Spec §FR-016, §SC-004] — PASS: FR-016's closing sentence and FR-021 both require this explicitly.
- [x] CHK021 Is the expected builder-plan set required to be **derived** from the emitter's own interning rule rather than transcribed from a first run? [Measurability, Spec §FR-016b, Contract §K8] — PASS: FR-016b's "Implementation obligation" and `tasks.md` T031/T033 state this explicitly.
- [x] CHK022 Is the derivation instrument for those counts named and reproducible? [Measurability, Research §D-9a] — PASS: `contracts/builder_plan_census.py`, self-validating against three shipped tiers on every run (D-9a).
- [x] CHK023 Are the completeness gates specified as **exact-set** equality rather than subset containment? [Measurability, Spec §FR-016b, §US2 AC4] — PASS: FR-016b names it "an exact-**set** completeness gate"; US2 AC4 states "every application message and every declared group plan is accounted for, with **no** silently-skipped message or group."
- [x] CHK024 Is the class-side ⟷ structural consistency gate specified with its extraction rule, so two independent derivations are genuinely composed? [Measurability, Spec §FR-021] — PASS: FR-021 cites the version-agnostic extraction rule at `vlatest_manifest_class_consistency_test.cpp:33-63` for the class side, and FR-018's oracle for the structural side.
- [x] CHK025 Is the reason a stronger two-leg consistency pair cannot be instantiated for this version recorded, rather than the weaker single leg being presented as equivalent? [Clarity, Spec §FR-021] — PASS: FR-021 states directly "the 076 V-1/V-1b *manifest*↔class pair cannot be instantiated for `v42` as-is: V-1b keys on `Manifest.txt`, which `v42` does not emit."
- [x] CHK026 Are the two inverted descope tests required to be **inverted** rather than deleted? [Measurability, Spec §FR-016b] — PASS: FR-016b — "MUST be inverted to assert the delivered `v42` builder tier, not deleted."

## Scenario Coverage

- [x] CHK027 Are requirements defined for the version whose artifacts change, and separately for the versions whose artifacts must not? [Coverage, Spec §FR-015, §FR-016] — PASS: FR-015 (unaffected versions) and FR-016 (`v42`) are separate requirements with separate acceptance instruments.
- [x] CHK028 Are requirements defined for nested group emission at the depths the target dictionary actually contains? [Coverage, Spec §Edge Cases] — PASS: § Edge Cases' "Nested group depth" bullet names FIX42's 5 nested occurrences explicitly; US1 AC4 and US4 both exercise the `296→295` case.
- [x] CHK029 Are requirements stated for deterministic group-emission ordering under newly-visible groups? [Coverage, Spec §Edge Cases] — PASS: same § Edge Cases bullet requires "the emitter's existing depth bound and its deterministic group-emission ordering must hold"; contract P3 states determinism as a required property.
- [x] CHK030 Are requirements defined for the required-group omission case at every declared pair, including one whose construction differs from the rest? [Coverage, Spec §FR-008, Contract §K9] — PASS: FR-008 covers all 14 pairs, explicitly separating the 13 top-level cases from the nested `MassQuote`/295-in-296 case.
- [x] CHK031 Is the differing construction for that nested pair described specifically enough to be built correctly? [Clarity, Spec §FR-008] — PASS: FR-008 states the exact mechanism — checked via `gc.validate_entry(args, i)`, omission built as "an entry of 296 carrying an empty 295 span".

## Edge Case Coverage

- [x] CHK032 Are requirements stated for the emitter-staleness trap that can silently compile a stale generated header? [Edge Case, Plan §Implementation Sequencing] — PASS: plan.md step 5 states this explicitly ("Force a clean codegen rebuild first — the known emitter-staleness trap silently compiles a stale `Reify.hpp`"), carried into `tasks.md` T001/T026 and `quickstart.md` Prerequisites.
- [x] CHK033 Is the compile-cost consequence of adding typed group classes to the regenerated headers carried as a requirement or an explicit risk? [Coverage, Spec §FR-022, Research §D-11] — PASS: the read-tier compile ceiling is FR-022 leg (c), a hard requirement; the separate builder-tier compile cost is explicitly carried as D-11's risk row, not promoted to an FR-022 obligation — the two are deliberately distinguished, not conflated.
- [x] CHK034 Is the one acknowledged measurement gap in the compile-cost story stated rather than omitted? [Gap, Research §D-11] — PASS: D-11's risk row states directly "No bench profile covers this... Measure, don't assume."
- [x] CHK035 Are requirements defined for a group tag whose member signature diverges across contexts, given ordinaled plan naming? [Edge Case, Research §D-9a] — PASS: D-9a enumerates the 7 ordinaled tags (`146`→4, `73`→3, `295`→3, `78`/`268`/`296`/`420`→2 each) and FR-016b states plan names are mode-dependent and must not be cross-compared.

## Dependencies & Assumptions

- [x] CHK036 Is the set of code-generated versions stated with its source, so the blast radius is bounded? [Assumption, Spec §Assumptions] — PASS: § Assumptions — "Only FIX42, FIX44, FIX50SP2, and FIXT11 are code-generated (`cmake/Codegen.cmake`)."
- [x] CHK037 Is the exclusion of additional versions from the generated set stated explicitly as out of scope? [Boundary, Spec §Assumptions] — PASS: § Assumptions — "Adding FIX40/FIX41 (or FIX43/FIX50/FIX50SP1) to the code-generated version set is explicitly out of scope."
- [x] CHK038 Is the dependency on the predecessor split-layout feature documented where the new version enters that layout? [Dependency, Spec §FR-007] — PASS: FR-007 states `v42` emits "the full 078 split builder/validator layout... on the same terms as `v44`/`v50sp2`/`vlatest`."
- [x] CHK039 Is the label-filtered-test-selection hazard recorded as a verification requirement for the touched subsystem? [Dependency, Plan §Implementation Sequencing] — PASS: plan.md — "Because `tools/codegen/**` is touched, `ctest -L codegen` is mandatory locally — a label-filtered run that omits it has previously missed a subsystem's COUNT pin."

## Ambiguities & Conflicts

- [x] CHK040 Does any requirement leave ambiguous whether the datatype field itself is modified? [Ambiguity, Spec §Assumptions, Research §D-4] — PASS: § Assumptions and D-4 both state directly "`FieldRef::type` is **not** modified" — no ambiguity.
- [x] CHK041 Is the falsifiability consequence of leaving that field unmodified stated, rather than the byte-identity prediction being left tautological? [Clarity, Spec §FR-016a] — PASS: FR-016a — "This expectation MUST be **verified by regeneration diff**, and any artifact that moves against it is a finding to be explained, not absorbed"; research D-4 states this is "a real prediction the regeneration diff can refute."
- [x] CHK042 Is there any conflict between the requirement that no site retain the datatype gate and the sites legitimately retaining a datatype token? [Conflict, Spec §FR-001, Research §D-7] — PASS: FR-001 explicitly reconciles this — "The absolute is scoped to *detection*: `field_data_type::NumInGroup` remains the correct source wherever the question genuinely *is* the datatype, and those occurrences MUST NOT be changed" — with the six sites named.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 42 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 42 |

### SPEC-FIXED items
None.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: `tools/codegen/fixpp-codegen/ir.cpp:80-100` (`walk_level`, unconditional `GroupOrderEntry` push), `tools/codegen/fixpp-codegen/main.cpp:25-32,68-70,128-135` (`if (ir.ns != "v42")` driver exclusion, `CoverageMode::All` default, `write_file`'s empty-content skip) — all resolve as cited. Count consistency (28/17/226, 19/11/147, registry 39/25, 46/39 messages) cross-checked directly across `spec.md`, `plan.md`, `research.md` D-9/D-9a, `data-model.md`, and `contracts/group-detection.md` K8 — identical everywhere.
