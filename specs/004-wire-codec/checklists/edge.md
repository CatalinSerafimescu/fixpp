# Adversarial / Edge-Coverage Requirements Quality Checklist: Wire Codec

**Purpose**: Formal release-gate validation that edge-case and hostile-input requirements are complete, consistent, and measurable. Tests whether the spec *defines* the edge behaviour — not whether the parser handles it.
**Created**: 2026-05-16
**Feature**: [spec.md](../spec.md) · **Plan**: [plan.md](../plan.md)
**Audience**: Reviewer at Gate A/B

## Partial-Read Coverage

- [ ] CHK001 - Is the set of split boundaries (mid-tag, mid-`=`, mid-value, mid-SOH, mid-`BodyLength`, between messages, one byte at a time) enumerated exhaustively rather than "etc."? [Completeness, Spec Edge Cases]
- [ ] CHK002 - Is the carry-over requirement (partial trailing bytes survive to the next feed) stated with a defined storage owner and lifetime? [Clarity, Spec FR-009 / Key Entities]
- [ ] CHK003 - Is "reassembles correctly" expressed as a measurable outcome (exactly the original frame sequence, in order, no loss/dup)? [Measurability, Spec SC-004]

## Repeating Groups

- [ ] CHK004 - Are nested-group requirements (groups within groups, W-007) specified distinctly from flat-group requirements? [Completeness, Spec FR-004/Edge Cases]
- [ ] CHK005 - Is the per-group-instance cap explicitly per-instance, not aggregate, with the exceed error code named? [Ambiguity, Spec Edge Cases/FR-015]
- [ ] CHK006 - Is the `iter()` vs `operator[]` equivalence stated as a requirement (identical entries/order), making it verifiable? [Measurability, Spec FR-004 / Plan seam #8]

## Length+Data Fields

- [ ] CHK007 - Is the W-008 requirement that `Data` is read by length (never misparsed on `=`/SOH) stated unambiguously? [Clarity, Spec FR-005]
- [ ] CHK008 - Is the Index-mode vs Iter-mode scope boundary for dialect-introduced BLOB pairs explicitly bounded (Iter = static FIX-5.0-SP2 table only; new dialect pairs out of v1.0 Iter scope)? [Coverage, Spec FR-005 / research D-11]
- [ ] CHK009 - Is the deferred case (dialect-introduced new BLOB pair in Iter mode) documented as an intentional exclusion, not an undefined gap? [Gap, Spec FR-005 / research D-11]

## Unknown / Custom Fields

- [ ] CHK010 - Is the split between dictionary-missing (→ `unknown_fields()`) and dictionary-known-but-invalid-for-MsgType (→ `wire_unexpected_tag`) specified unambiguously? [Conflict, Spec Edge Cases / data-model E9]
- [ ] CHK011 - Is opaque byte-exact round-trip preservation of unknown fields stated as a measurable requirement (original byte order, no vector materialization)? [Measurability, Spec FR-008/Edge Cases]

## Corruption / Hostile Input

- [ ] CHK012 - Is mandatory CheckSum/BodyLength rejection stated with an explicit "no production bypass" requirement (tests-only hook permitted)? [Clarity, Spec FR-017]
- [ ] CHK013 - Is each hostile case (oversized frame, offset overflow, out-of-range tag, oversized group) bound to one defined error code with bounded memory? [Completeness, Spec Edge Cases/SC-003]
- [ ] CHK014 - Is the over-4096 corpus distinction specified clearly — SC-003 measures the reject path at the default cap vs SC-008 measures footprint with the cap raised — so the two are not contradictory? [Consistency, Spec SC-003/SC-008 / research D-7]
- [ ] CHK015 - Is "clean under sanitizers and fuzzing" specified as a measurable acceptance condition (zero crash / zero OOB / zero unbounded alloc)? [Measurability, Spec SC-003]

## Structural Edge Cases

- [ ] CHK016 - Are empty/zero-length values, missing trailer, fields after `CheckSum(10)`, duplicated standard header fields, and out-of-order mandatory header fields each given a defined expected outcome? [Coverage, Spec Edge Cases]
- [ ] CHK017 - Is the digit-only BodyLength requirement (space-padded `9=   123|` rejected) stated explicitly so the conformance boundary is unambiguous? [Clarity, Spec / research D-5 / Plan §4.5]

## Traceability

- [ ] CHK018 - Does every edge case in the spec's Edge Cases section map to at least one FR- or SC-, with no enumerated edge case lacking a defined requirement? [Traceability, Spec Edge Cases ↔ FR/SC]
- [ ] CHK019 - Are intentionally-excluded scenarios (FIXP/SOFH/SBE, session FSM semantics) stated as explicit exclusions rather than silent omissions? [Gap, Spec Assumptions]
