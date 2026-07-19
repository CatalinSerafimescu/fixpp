# Scope & Correctness Requirements Checklist: Runtime validator required-presence scoping

**Purpose**: Requirements-quality gate ("unit tests for the English") on the CORE presence-scope fix — message-level vs group-scope derivation, the dynamic-width per-instance check, the carve-outs (header/trailer, FIX42, component/codegen-vacuous), and the ABI / read-golden / performance NFRs. Audience: Gate B reviewer.
**Created**: 2026-07-18
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [data-model.md](../data-model.md)

**Note**: This checklist validates whether the REQUIREMENTS are complete, clear, consistent, and measurable — NOT whether the implementation works.

## Requirement Completeness

- [x] CHK001 Is the post-fix message-level required-set rule fully specified (a tag is included iff `required='Y'` AND not enclosed by any group)? [Completeness, Spec §FR-001, data-model §Message-level required set] — PASS: FR-001 states the rule verbatim; data-model §Message-level required set restates "a tag is in the set iff `required='Y'` AND it is not enclosed by any group."
- [x] CHK002 Are the per-group required-member store's two forms (bare `group_required_members(no_tag)` and context `group_required_members(msg_type, parent_path, no_tag)`) and their query order (context first, bare only on miss) documented as requirements? [Completeness, Spec §FR-002, data-model §Per-group required-member set] — PASS: FR-002 states the generic queryable-form requirement; the two concrete accessor forms + query order ("context first, bare fallback") are pinned in data-model §Per-group required-member set and re-asserted in FR-009a/Contract 1a.
- [x] CHK003 Is the dynamic-width per-instance requirement stated for EVERY per-group required-member count (no ≤64 bound), and is the removal of the candidate's fail-open skip an explicit requirement? [Completeness, Spec §FR-004/§SC-002, data-model §Group-instance membership check state] — PASS: FR-004 "Enforcement MUST be universal... never silently skipping the check for large groups... that mask MUST be widened to a dynamic width at /implement, replacing the candidate's ≤64 fail-open skip." Verified as still-open in source (`include/fixpp/wire/validator.hpp:307-312` still has `size() <= 64` — correctly a T004 /implement item, not a spec gap).
- [x] CHK004 Is the StandardHeader/StandardTrailer preservation requirement (tags 8/9/34/35/49/52/56/10 never dropped, even under an optional header/trailer componentRef) explicitly stated? [Completeness, Spec §FR-005/§Edge Cases] — PASS: FR-005/Edge Cases state the general preservation rule ("header/trailer required fields MUST NOT be dropped"; "MUST stay required even where the Orchestra dict references the header/trailer componentRef with default (optional) presence"); the concrete 8-tag enumeration lives at Clarifications/§US4/FR-009 in the same spec.md — present, cross-referenced, not missing.
- [x] CHK005 Is the FIX42 carve-out (no typed builder/validator tier → runtime-only + census-vs-IR-structure coverage) specified everywhere it is relied on (US3, FR-007, SC-004, SC-008)? [Completeness, Spec §FR-007] — PASS: verified verbatim at US3, FR-007, SC-004, and SC-008, each citing L-077-1/#196/`main.cpp:132`.
- [x] CHK006 Is the "no codegen change" position specified as a *verification* requirement (with the localize-if-it-fails path), rather than left implicit? [Completeness, Spec §FR-007/§SC-008] — PASS: FR-007 "this is a *verification* requirement, not a change... a two-tier agreement test MUST confirm"; SC-008 "If this test unexpectedly fails, it localizes a codegen leg that Phase 0 missed."
- [x] CHK007 Is the read/reify golden byte-identity requirement enumerated for ALL five affected versions (v44 / v42 / vt11 / v50sp2 / vlatest) with an explicit no-delta-allowance? [Completeness, Spec §FR-008/§SC-006] — PASS: FR-008/SC-006 both enumerate all five versions with "no census-justified golden-delta allowance."
- [x] CHK008 Is the C-ABI freeze (1.5.0, no C surface touched) stated as a constraint of this feature? [Completeness, Spec §FR-006, plan.md Article X] — PASS: FR-006 "no C-ABI change (frozen 1.5.0)"; plan.md Article X "PASS — no C-ABI surface touched (frozen 1.5.0)."
- [x] CHK009 Is the hot-path performance-bench requirement present and unconditional (ships in this PR; before/after rows for no-group / shallow-group / nested-or-multi-instance)? [Completeness, plan.md §Performance Goals / Article VIII] — PASS: plan.md §Performance Goals states the bench ships in this PR unconditionally with the three named rows, per Article VIII §3 absolute.
- [x] CHK010 Is the control requirement (a message genuinely missing a top-level required field is still rejected — no over-correction) captured as a first-class requirement? [Completeness, Spec §SC-007/§US1 AS-3] — PASS: US1 Acceptance Scenario 3 and SC-007 both state the control case explicitly.

## Requirement Clarity

- [x] CHK011 Is "top-level" defined unambiguously (a field not enclosed by any group, per the loader rule) so "message-level required set" cannot be read to include group members? [Clarity, Spec §FR-001] — PASS: FR-001 defines it by exclusion ("solely inside a repeating group MUST NOT appear"); data-model restates as "not enclosed by any group."
- [x] CHK012 Is "dynamic-width" defined concretely (widen the fixed mask, keep the linear `req_bit` scan, fail-closed) rather than as a vague adjective? [Clarity, Spec §FR-004, data-model §Group-instance membership check state] — PASS: FR-004 gives the concrete mechanism ("the validator already runs a linear `req_bit` scan; only the fixed-width mask is 64-bounded — that mask MUST be widened"); data-model repeats it.
- [x] CHK013 Is the phrase "component-usage handling unchanged" clear given the Phase-0 vacuous finding — i.e. does the spec make plain the loader threads NO component-AND while the census oracle deliberately does? [Clarity, Spec §FR-005, data-model §Message-level required set] — PASS: data-model §Message-level required set states this explicitly: "the loader threads no component-AND... The census oracle re-derives it independently with a *stronger* full-ancestor-chain component-AND... so an optional-component over-require would surface as census RED even though the loader itself threads no component-AND." **⚠️ SUPERSEDED (Gate B r1/r2):** the quoted "loader threads no component-AND" premise was falsified — T020+F1 thread component-AND into both loaders + the per-group store; the oracle matches (independence, not strength). See data-model §Census entities.
- [x] CHK014 Is the offending-tag surfacing on a malformed instance specified as reusing the existing `wire_required_field_missing` disposition (fail-closed mirror), not a new error path? [Clarity, Spec §FR-004, plan.md Article XV] — PASS: FR-004 states fail-closed + offending-tag surfacing, citing Article XV; plan.md Article XV names the disposition explicitly: "mirroring existing `wire_required_field_missing` disposition"; data-model §Group-instance membership check state also names it directly.
- [x] CHK015 Are the affected messages/versions named concretely (PositionReport AP / NoUnderlyings, TradeCaptureReport AE / NoSides, one-per-version) rather than left as "the affected set"? [Clarity, Spec §SC-001/§SC-002] — PASS: US1/SC-001 name PositionReport(AP)/NoUnderlyings and TradeCaptureReport(AE)/NoSides concretely plus the one-per-version regime (Clarifications).

## Requirement Consistency

- [x] CHK016 Do FR-001/FR-005 (loader component-AND treatment) and the census oracle's full-ancestor-chain component-AND (US4/FR-009) stay non-contradictory? [Consistency, Spec §FR-005 vs §FR-009] — PASS (updated Gate B r2): the original "loader threads no component-AND / oracle deliberately stronger" premise was falsified — T020 threads message-level component-AND into BOTH loaders and F1 (gate-b/r1) threads the group-relative component-AND into the per-group store; the oracle matches. The oracle's discriminating power is now **independence** (a structurally distinct raw-XML walk), NOT asymmetric strength. Consistent: loader and oracle agree on component-AND; the census catches divergence via independence + the synthetic RED witness.
- [x] CHK017 Is the no-golden-delta stance consistent across FR-008, SC-006, and plan.md Article X (no "census-justified delta" allowance anywhere)? [Consistency, Spec §FR-008/§SC-006] — PASS: all three state byte-identical / no delta allowance with identical rationale (loader touches only `required_out`, not `FieldRef.rule` or IR inputs).
- [x] CHK018 Is the FIX42 exclusion consistent across US3, FR-007, SC-004, SC-008, and the two-tier contract (Contract 3) — never accidentally re-included in a typed-tier assertion? [Consistency, Spec §US3/§SC-004, contracts Contract 3] — PASS: verified verbatim exclusion at all five locations, all citing the same L-077-1/#196/`main.cpp:132` anchor.
- [x] CHK019 Do spec, plan, and data-model agree that the ONLY candidate-code change is the ≤64→dynamic-width mask (loader scoping + store already candidate baseline)? [Consistency, plan.md §Phase-0 scope narrowing vs data-model §Group-instance membership check state] — PASS: plan.md §Summary/§Phase-0 scope narrowing and data-model §Group-instance membership check state both single out the mask widening as the one identified code delta; consistent with spec.md's Assumptions hypothesis-framing ("may require changing the candidate" — i.e. other legs are verify-then-fix-if-needed, not asserted-clean), not contradictory.

## Acceptance Criteria Quality (Measurability)

- [x] CHK020 Are the US1/US2 outcomes stated as objective validator verdicts (accept / reject-with-tag) rather than subjective "handles correctly"? [Measurability, Spec §SC-001/§SC-002] — PASS: US1/US2 acceptance scenarios state "accepted"/"rejected... with the offending tag surfaced," objectively checkable.
- [x] CHK021 Is the read-golden invariant measurable as byte-identity (a diff), not "goldens look unchanged"? [Measurability, Spec §SC-006] — PASS: SC-006/FR-008 use "byte-identical" (diffable).
- [x] CHK022 Is the performance requirement measurable (a reported before/after bench), with the explicit allowance that the outcome MAY be "within noise" while the bench MUST still exist? [Measurability, plan.md §Performance Goals] — PASS: plan.md §Performance Goals states exactly this ("The outcome may be 'within noise', but the bench must exist and be reported").

## Edge Case Coverage

- [x] CHK023 Is the "field required BOTH at top level AND inside an optional group" case specified to remain top-level-required? [Edge Case, Spec §Edge Cases] — PASS: Edge Cases states this case verbatim.
- [x] CHK024 Are nested groups addressed (per-instance required-ness at the correct nesting level, reusing 072's `consume_group`)? [Edge Case, Spec §Edge Cases/§Assumptions] — PASS: Edge Cases + Assumptions both state nested-group handling reuses 072's nesting-aware `consume_group`.
- [x] CHK025 Is "empty group (count present, zero instances) vs absent group" specified as both being legitimate group omissions? [Edge Case, Spec §Edge Cases] — PASS: Edge Cases states this verbatim.
- [x] CHK026 Is the max-per-group-required-member census (guarding the "0–3 required members" assumption from silent rot) specified as a durable requirement, not a one-off? [Edge Case, Spec §FR-004/§SC-002, contracts Contract 1a] — PASS: FR-004/SC-002/Contract 1a all require the max-count census; tasks.md T017 registers it inside the durable checked-in census test.

## Dependencies & Assumptions

- [x] CHK027 Is the candidate on `177a0535` explicitly framed as a HYPOTHESIS (verified independently, may be changed) rather than a ratified diff? [Assumption, Spec §Assumptions, plan.md §Summary] — PASS: spec.md Context & provenance + Assumptions both state this explicitly ("a hypothesis to be independently verified, not a diff to ratify"); plan.md §Summary repeats it.
- [x] CHK028 Is the Phase-0 "0 optional-component over-require sites" finding recorded with its evidence (static raw-XML enumeration across 10 dicts) so the group-only scope narrowing is traceable? [Assumption, Spec §Clarifications, plan.md §Phase-0] — PASS: Clarifications states the finding + method; research.md R3 gives the full evidence table (9 QuickFIX dicts: 0 sites; vlatest: 11 apparent hits all StandardHeader/Trailer, classified).
- [x] CHK029 Is the opt-in/default-off status of `validate_inbound_messages` documented as unchanged (this feature alters only what the strict path accepts/rejects)? [Assumption, Spec §Assumptions] — PASS: Assumptions states this verbatim.

## Notes

- Check items off as dispositioned at /speckit-checklist-audit: `[x]` + one of SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>.
- Completeness / Clarity / Consistency gaps MUST be SPEC-FIXED or DD-DECIDED — never WAIVED (pipeline.md step 9).

## Audit Result

Audited 2026-07-18 against spec.md, plan.md, data-model.md, research.md, contracts/census-and-agreement.md, quickstart.md, tasks.md, and the live source (`table_view.hpp`, `validator.hpp`, `xml_loader.cpp`, `orchestra_loader.cpp`, `ir.cpp`). This bundle converged through two adversarial Gate-A rounds (round 2 fixed the two census-contract-prose P1s) before reaching this audit — no further Completeness/Clarity/Consistency defects were found on re-derivation from source.

| Disposition | Count |
|---|---|
| PASS | 29 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 29 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None — the four feature-specific settled decisions (candidate-as-hypothesis, component/codegen-vacuous, FIX42 carve-out, census actual-side pin) are all re-spec'd directly into spec.md (Assumptions/Clarifications/US3/FR-007/FR-009), not merely cited by anchor, so they disposition as PASS with the anchor as supporting reference, not DD-DECIDED.

### WAIVED items

None.

Anchors spot-verified: `spec/coverage-index.md:189` (W-014 row, exact line match) · `spec/coverage-index.md:184` (§3.2 repeating-groups row, W-006/W-007/D-010, exact line match) · `spec/behaviors-and-limitations.md` L-067-1 (line 1759) · `spec/behaviors-and-limitations.md` L-077-1 (line 1817) · `spec/feature-catalogue.md` rows W-006 (line 103), W-007 (line 103/111 area), W-014 (line 111), D-010 (line 129) — all resolve in the current checked-in revision.

### Realizability sub-check

No new value-typed entity is introduced. `table_view` (extended with additive accessors `group_required_members(no_tag)` / `group_required_members(msg_type, parent_path, no_tag)`, verified present at `include/fixpp/dict/table_view.hpp:310-317` and `:383-394`) is already a complete type. `FieldRef` (`include/fixpp/dict/field_ref.hpp:83-91`) is an unchanged POD (`tag`, `type`, `rule`, `condition_index`, `group_no_tag`, `component_index`, `enum_table_index`, `length_pair_data_tag`, `_reserved`) — no pointer/owning member to an incomplete type. Verdict: **clean**, confirmed by direct source read (not assumed from forward-declaration).
