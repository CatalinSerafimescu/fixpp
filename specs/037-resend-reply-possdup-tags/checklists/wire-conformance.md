# Checklist: Resend-reply wire-conformance requirements quality

**Purpose**: Unit-tests-for-English over the 037 requirements — validate the GapFill/replay PossDup requirements are complete, unambiguous, consistent, and measurable before `/speckit-implement`.
**Created**: 2026-06-14
**Domain**: FIX wire conformance (resend-reply emitters)
**Audience/timing**: Gate B reviewer / pre-implementation
**Feature**: [spec.md](../spec.md)

## Requirement Completeness

- [ ] CHK001 Are requirements defined for BOTH resend-reply frame kinds — the GapFill (`35=4`) and the replayed application frame? [Completeness, Spec §FR-001..005]
- [ ] CHK002 Is the source of the GapFill's `OrigSendingTime(122)` value explicitly specified (the frame's own `52`, not a stored original)? [Completeness, Spec §FR-002, Assumptions]
- [ ] CHK003 Is the source of the replayed app frame's `122` explicitly specified (the stored `52`, not a caller-supplied `122`)? [Completeness, Spec §FR-005]
- [ ] CHK004 Are the default-path vs non-default (`allow_pos_dup=true`) behaviors each separately specified? [Completeness, Spec §FR-006, US2]
- [ ] CHK005 Is the full set of GapFill fields that MUST remain unchanged enumerated (`8/35/34/49/52/56/36/123`)? [Completeness, Spec §FR-003]
- [ ] CHK006 Are the verification surfaces (unit cell, golden re-bake, live re-run) each tied to a named success criterion? [Completeness, Spec §SC-001..004]

## Requirement Clarity

- [ ] CHK007 Is "exactly one `43`/`122`" stated as an exact-count, not a presence check (so a duplicate-tag frame fails)? [Clarity, Spec §FR-004, Contract C-2]
- [ ] CHK008 Is the `122 == 52` relationship stated as byte-equality (not merely "a timestamp")? [Clarity, Spec §FR-002, data-model INV-2]
- [ ] CHK009 Is "the unmarked GapFill" disambiguated so it cannot be read as the corrected (and false) `122`-reject rationale? [Ambiguity, Spec §US1 Why-this-priority + Assumptions]
- [ ] CHK010 Is the field-placement decision (append after `123`) stated with its interop justification and its limitation (header-after-body, not strict-order canonical)? [Clarity, research §D-3]

## Requirement Consistency

- [ ] CHK011 Does the `122 = own 52` (GapFill) vs `122 = stored 52` (replay) distinction read consistently across spec / data-model / contract / research? [Consistency, INV-2 / D-1 / D-4]
- [ ] CHK012 Is the `122`-emit rationale (emit-parity + FIX grammar, NOT inbound-reject avoidance) stated consistently in ALL artifacts, with no surviving "strict peer rejects missing 122" claim? [Consistency, research §D-2]
- [ ] CHK013 Are the unchanged-field enumerations identical across FR-003, AS-2, quickstart Cell 1, and T004 (incl. `52`)? [Consistency, Spec §FR-003 / quickstart / tasks]
- [ ] CHK014 Do the catalogue citations (S-005/S-006/S-010/S-033, `[FIX-SL §4.8.x]`) match between the Normative References section and the plan? [Consistency, Spec §Normative References]

## Acceptance Criteria Quality (Measurability)

- [ ] CHK015 Can each SC be objectively verified by a named witness (unit assert / golden diff / live cell), with no subjective term? [Measurability, Spec §SC-001..004]
- [ ] CHK016 Is the default-path byte-identity criterion (FR-006/SC-003) expressed as a byte-for-byte oracle comparison, not "looks the same"? [Measurability, Spec §SC-003, quickstart Cell 3]
- [ ] CHK017 Is the no-new-surface guarantee (FR-007) given a concrete, checkable acceptance form (e.g., header diff is doc-comment-only)? [Measurability, Spec §FR-007, tasks T022]

## Scenario & Edge-Case Coverage

- [ ] CHK018 Is the "stored frame already carries `43`/`122`" case (the DEFECT-2 trigger) covered as a requirement, including the honesty obligation to first prove the stored frame had them? [Coverage, Spec §US2, Contract C-4]
- [ ] CHK019 Is the "GapFill spans a range → no single original `52`" edge case addressed in requirements? [Edge Case, Spec §Edge Cases, research §D-1]
- [ ] CHK020 Is the retained-`122` ≠ stored-`52` case specified (the engine value wins)? [Edge Case, Spec §Edge Cases, §FR-005]
- [ ] CHK021 Are the cross-frame requirements (a resend reply emitting BOTH a GapFill and a replayed frame) captured? [Coverage, Contract C-3]

## Dependencies, Assumptions & Boundaries

- [ ] CHK022 Is the reference-engine ground truth (both engines stamp `122=52`; QFJ `35=4` exemption) recorded as a validated assumption with resolvable citations? [Assumption, research §D-1/D-2, Assumptions]
- [ ] CHK023 Is the SC-004 QFcpp-live limitation documented as an explicit waiver (L-021-3), not silently omitted or over-claimed? [Assumption/Boundary, Spec §SC-004, §FR-008]
- [ ] CHK024 Are the scope exclusions (C-103 chunked-resend deferred; inbound validation untouched; the GapFill builder is the sole SequenceReset emitter) explicitly bounded? [Boundary, research §D-5]
