# Validator Semantics Requirements Quality Checklist: Wire Codec

**Purpose**: Formal release-gate validation that the `Validator` requirements (depth, plugin shape, scratch bound, error re-map, classification) are complete, clear, consistent, and measurable. Tests the requirements, not the validator.
**Created**: 2026-05-16
**Feature**: [spec.md](../spec.md) · **Plan**: [plan.md](../plan.md) · **Data model**: [data-model.md](../data-model.md) (E7)
**Audience**: Reviewer at Gate A/B

## Default-Validator Depth

- [ ] CHK001 - Is "full per-version default" defined with the exact rule set (required-field presence, type conformance, enum membership, repeating-group structure) rather than a vague "complete validator"? [Clarity, Spec FR-010]
- [ ] CHK002 - Are all four supported versions (v42/v44/v50sp2/vt11) named explicitly so per-version coverage is unambiguous? [Completeness, Spec FR-010/SC-005]
- [ ] CHK003 - Is "unconditional over every dictionary-known field present" specified explicitly, contrasted against a per-accessor model, so the validation trigger is unambiguous? [Ambiguity, data-model E7 / research D-4]
- [ ] CHK004 - Is a structural-only or interface-only stand-in explicitly excluded as non-conformant to the requirement? [Conflict, Spec Clarifications Q2 / research D-4]

## Plugin Shape & Cap

- [ ] CHK005 - Is the pure-virtual count stated as **exactly 5** (not "≤5") consistently across spec, plan, data-model, and the contract oracle? [Consistency, Spec FR-011 / Plan §XIV.2 / contracts/validator.hpp]
- [ ] CHK006 - Are the five pure-virtual methods each named, so "exactly 5" is auditable rather than asserted? [Measurability, data-model E7 / contracts/validator.hpp]
- [ ] CHK007 - Is "holds `dict::table_view` by value (no virtual `wire/`→`dict/` runtime edge)" stated as a verifiable layering requirement with a named check? [Measurability, Spec FR-011/SC-007]
- [ ] CHK008 - Is the runtime-virtual-plugin requirement reconciled with the constitutional ≤5 cap so no justification-paragraph ambiguity remains? [Consistency, Plan Constitution Check §XIV.2]

## Scratch-Arena Bound

- [ ] CHK009 - Is the whole-message entry point specified to take an explicit caller-supplied scratch arena (`validate(msg, scratch_mr)`)? [Clarity, Spec FR-011 / data-model E7]
- [ ] CHK010 - Is the working-set bound (~600 B: `seen[]` bitmap + `required_remaining`) specified with its composition so it is auditable? [Measurability, Spec FR-011 / data-model E7]
- [ ] CHK011 - Is `validate_field` specified as allocation-free distinctly from the arena-bound whole-message path? [Completeness, data-model E7]
- [ ] CHK012 - Is the validator's zero-`new`/`delete` claim traced onto FR-012/SC-002 so the allocation guarantee covers the validator path? [Traceability, Spec FR-011/FR-012]

## Error Re-Map (slot 41)

- [ ] CHK013 - Is `wire_field_value_truncated` (slot 41) specified unambiguously as a **re-map** of 2a/001 `decimal_precision_loss` — explicitly NOT a verbatim propagation and NOT a slot deletion? [Ambiguity, Spec Clarifications / data-model Error mapping]
- [ ] CHK014 - Is the re-map call site named precisely (the validator's `[2b §6.5 rule 3]` type-check) so "surfaced unchanged" cannot be misread as propagation? [Clarity, data-model Error mapping]
- [ ] CHK015 - Is the design-doc-wins decision (distinct slot retained, only the call site specified) recorded so the slot is not later "optimized away"? [Consistency, Spec Clarifications Q (Gate A r1)]

## Classification Correctness

- [ ] CHK016 - Is "zero false accept of a non-conforming message" stated as an absolute, measurable acceptance criterion (not "high accuracy")? [Measurability, Spec SC-005]
- [ ] CHK017 - Are the four non-conforming classes (missing required, type violation, enum violation, malformed group count) each given a defined expected error? [Completeness, Spec FR-010/SC-005]
- [ ] CHK018 - Is the `wire_unexpected_tag` (dict-known invalid for MsgType) path specified distinctly from the unknown-fields (dict-missing) path so the two are not conflated? [Conflict, data-model E9 / Error mapping]

## Traceability

- [ ] CHK019 - Does every validator requirement trace to FR-010/FR-011 and SC-005/SC-007, with no validator obligation lacking a measurable outcome? [Traceability, Spec FR-010/FR-011/SC-005/SC-007]
