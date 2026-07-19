# API / Acquisition-Surface Requirements Checklist: Orchestra runtime dictionary load

**Purpose**: Gate-B requirements-quality review of the widened C-API / TOML acquisition surfaces and the shared `dict::load_any` dispatch contract. Each item tests whether the REQUIREMENTS are complete, clear, consistent, and measurable — not whether code works.
**Created**: 2026-07-19
**Feature**: [spec.md](../spec.md) · [contracts/load_any.md](../contracts/load_any.md) · [contracts/surfaces.md](../contracts/surfaces.md)
**Audience / Depth**: Gate B reviewer · formal release gate

## Requirement Completeness

- [x] CHK001 Are the two acquisition surfaces whose behavior widens explicitly enumerated (C-API `fixpp_dict_load_from_xml` and TOML `dictionary.path`)? [Completeness, Spec §FR-001/FR-002] — PASS: spec.md FR-001 names `fixpp_dict_load_from_xml`, FR-002 names TOML `dictionary.path`; no third surface implied anywhere in the bundle.
- [x] CHK002 Is the dispatch discriminant stated as a single named property (the document root element) with the exact accepted values (`fix`, `fixr:repository`) documented? [Completeness, Spec §FR-003] — PASS: spec.md FR-003 names the root element as sole discriminant with both exact values; contracts/load_any.md dispatch-rule table repeats identically.
- [x] CHK003 Is the requirement that the sniff/dispatch logic live in ONE shared helper (not duplicated per call site) stated, and is the helper named? [Completeness, Spec §FR-004] — PASS: FR-004 ("single shared dictionary-layer helper") + data-model.md E1 names it `dict::load_any`; contracts/load_any.md G1 restates single-definition guarantee.
- [x] CHK004 Are the disposition requirements for every non-accepted input class specified (unrecognized root, malformed XML, empty file, unreadable path)? [Completeness, Spec §Edge Cases / §FR-003] — PASS: spec.md Edge Cases (lines 64-67) enumerates all four classes explicitly with each disposition.
- [x] CHK005 Is the requirement that a classic `<fix>` load remains behaviorally unchanged through BOTH surfaces documented (not just one)? [Completeness, Spec §FR-006] — PASS: FR-006 states "through either entry point MUST remain behaviorally unchanged"; contracts/surfaces.md S1/S2 tables both show the classic row unchanged.
- [x] CHK006 Is the single-dictionary FIX-Latest-only configuration success requirement (FR-008) present and distinct from the general TOML load requirement? [Completeness, Spec §FR-008] — PASS: FR-008 is a standalone requirement text, distinct from FR-002's general TOML acceptance requirement; quickstart Scenario 3 step 2 pins it separately.

## Requirement Clarity

- [x] CHK007 Is "equivalent to `OrchestraLoader::load`" defined by an observable, checkable outcome (identical parse/validate/read results) rather than an unmeasurable "same"? [Clarity, Spec §FR-001/SC-001] — PASS: SC-001 defines "identical parse/validate/read outcomes across the FIX Latest message set used in the test corpus (100% agreement, 0 divergences)".
- [x] CHK008 Is the root-element read pinned to a specific accessor semantics (first *element* child, skipping comments/PI/XML-declaration) so "root element" is unambiguous? [Clarity, Spec §FR-003 / contracts/load_any.md] — PASS: contracts/load_any.md pins `document_element()` (not `first_child()`) explicitly, with the skip-non-element rationale; research.md D-2 records the same as an N-2 hardening decision.
- [x] CHK009 Is "behaviorally unchanged / byte-identical" for the classic path expressed as a measurable property (dictionary identity + parse/validate/read parity vs pre-080)? [Clarity, Spec §FR-006/SC-003] — PASS: FR-006 ("byte-identical dictionary, identical parse/validate/read results") + SC-003 ("zero result changes") are both measurable pass bars, not prose.
- [x] CHK010 Does the spec state which error/diagnostic each failure surfaces on (C-API `CONFIG_INVALID` via `catch(...)`; TOML diagnostic via `trap_throw_to_expected`) rather than a vague "returns an error"? [Clarity, Spec §Edge Cases / contracts/surfaces.md] — PASS: the exact disposition (C-API `catch(...)` → `FIXPP_ERR_CAPI_CONFIG_INVALID`; TOML `trap_throw_to_expected` → `LoadDiagnostic`) is stated in research.md D-2's "Malformed input" paragraph (the checklist's contracts/surfaces.md pointer is imprecise — the detail lives in research.md, not surfaces.md — but the requirement itself is present in the audited bundle, so this is not a completeness gap).

## Requirement Consistency

- [x] CHK011 Do the C-API and TOML surface requirements describe the SAME dispatch rule (both delegate to the one shared helper), with no surface-specific divergence in accepted roots? [Consistency, Spec §FR-003/FR-004] — PASS: FR-003/FR-004 apply uniformly to both; contracts/surfaces.md S1/S2 tables show identical root→loader mapping for both surfaces, no divergence.
- [x] CHK012 Is the "no new loader logic beyond sniff-and-dispatch" assumption consistent with FR-009 (loaders unchanged) and the reuse-`OrchestraLoader`-as-is assumption? [Consistency, Spec §Assumptions / §FR-009] — PASS: spec.md Assumptions (line 108), FR-009, and research.md D-5 all state the loaders are unmodified and reused as-is — no contradiction across the three.
- [x] CHK013 Are the contract deltas in contracts/surfaces.md (before/after tables) consistent with the FR statements in spec.md (no input class flips disposition in one doc but not the other)? [Consistency, contracts/surfaces.md ↔ Spec §FR] — PASS: cross-checked contracts/surfaces.md S1/S2 before/after rows against spec.md FR-001/002/003/006 and Edge Cases; every input class's disposition matches across both documents.

## Acceptance Criteria Quality (Measurability)

- [x] CHK014 Is SC-001 stated with an objective pass bar (100% agreement / 0 divergences over a named corpus) rather than "matches"? [Measurability, Spec §SC-001] — PASS: SC-001 literal text is "100% agreement, 0 divergences" over "the FIX Latest message set used in the test corpus".
- [x] CHK015 Is SC-002's 0→working transition objectively checkable (Orchestra `dictionary.path` loads AND validates a FIX-Latest message)? [Measurability, Spec §SC-002] — PASS: SC-002 requires both "loads and validates FIX Latest messages successfully"; quickstart Scenario 3 operationalizes it as two checkable steps.
- [x] CHK016 Is the FR-004 "defined once, not duplicated" requirement given a checkable acceptance form (a source-inspection gate that no call site inlines its own sniff), not left as prose? [Measurability, Spec §FR-004 / quickstart Scenario 6] — PASS: quickstart Scenario 6 + tasks.md T015 define a concrete source-inspection assertion (both call sites call `dict::load_any`, no inlined sniff, exactly one implementation), not prose.

## Scenario & Edge-Case Coverage

- [x] CHK017 Are requirements defined for a well-formed `<fixr:repository>` that FAILS Orchestra-loader validation (surfaces the loader's fail-closed parse error, not success/abort)? [Coverage, Spec §Edge Cases] — PASS: spec.md Edge Cases line 65 covers this exactly, naming the disposition as the Orchestra loader's existing fail-closed parse error via the entry point's error channel.
- [x] CHK018 Is the 074 T022h invariant (XmlLoader's own unit still rejects a `<fixr:repository>` root; only the dispatch layer becomes Orchestra-aware) preserved as an explicit requirement? [Coverage, Spec §FR-009] — PASS: FR-009 states this explicitly; verified against live code — `xml_loader.cpp:742-745` (`parse_document`'s `doc.child("fix")`-missing guard) still fires first for a non-`<fix>` root, unmodified.
- [x] CHK019 Are requirements defined so that root-sniffing does NOT broaden acceptance to arbitrary XML (a third/other root still yields the existing config-invalid error)? [Coverage / Boundary, Spec §FR-003 / §Edge Cases] — PASS: FR-003 last sentence + Edge Cases line 64 both state this boundary explicitly.
- [x] CHK020 Is the fail-closed guarantee that `load_any` throws ONLY `dict::*_error` types (never a bare `std::runtime_error`, never a wrong `Dictionary`) stated as a requirement? [Coverage / Exception Flow, contracts/load_any.md G2] — PASS: contracts/load_any.md G2 states this exactly; data-model.md E1 "Throws" enumerates the concrete `dict::*_error` set. Realizability-checked: `dict::xml_parse_error`, `orchestra_parse_error`, `unknown_version_error`, `group_delimiter_collision_error`, `xml_oom_error` are all complete types in `include/fixpp/dict/error.hpp` (074-shipped, not deferred).

## Dependencies & Assumptions

- [x] CHK021 Is the dependency on 074's shipped `OrchestraLoader` + its fail-closed parse-error behavior documented as reused-unchanged (no new loader logic)? [Assumption, Spec §Assumptions] — PASS: spec.md Assumptions line 108 states this exactly.
- [x] CHK022 Is the assumption that FIX-Latest equivalence reuses existing 074/076 fixtures (no new golden artifacts introduced) documented? [Assumption, Spec §Assumptions] — PASS: spec.md Assumptions line 110 states this exactly; tasks.md Scope reminders repeats it ("No new goldens").
- [x] CHK023 Is the `mr != nullptr` precondition on `load_any` documented (mirroring the existing loaders)? [Assumption, contracts/load_any.md G4 / data-model E1] — PASS: contracts/load_any.md G4 and data-model.md E1 Precondition both state it; tasks.md T008 implements it as an assert.

## Notes

- Check items off as `[x]` with a disposition tag at step-9 audit: `SPEC-FIXED` / `DD-DECIDED §X` / `WAIVED:<reason>`. Completeness/Clarity/Consistency gaps MUST NOT be waived.
- FR-007 is intentionally absent (collision leg descoped by Gate A round 1); this checklist raises no item requiring it.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 23 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 23 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None.

### WAIVED items

None.

Anchors spot-verified: `xml_loader.cpp:742-745` (parse_document doc.child("fix") guard, matches D-5/FR-009 citation), `xml_loader.cpp:347-349` (parse_version guard, matches D-5 for-this-path-dead claim), `dictionary.hpp:90` (`which_session_version`), `tools/codegen/fixpp-codegen/ir.cpp:547-577` (076 precedent sniff, matches D-1 citation) — all resolve against the current tree. Signed-off revision: plan.md `## Gate A` (2 rounds converged 2026-07-19); research.md D-1/D-2/D-5 (live), D-3/D-4 (SUPERSEDED-retained-for-audit).
