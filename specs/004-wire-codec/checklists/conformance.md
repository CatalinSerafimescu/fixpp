# FIX-Protocol Conformance Requirements Quality Checklist: Wire Codec

**Purpose**: Formal release-gate validation that the FIX-spec-fidelity requirements (W-001..W-014, OSS-006/008) are defined with an unambiguous oracle, a per-version corpus, and objectively measurable acceptance — *before* implementation. Tests whether conformance is **specified**, not whether the parser conforms.
**Created**: 2026-05-16
**Feature**: [spec.md](../spec.md) · **Plan**: [plan.md](../plan.md) · **Research**: [research.md](../research.md)
**Audience**: Reviewer at Gate A/B

## Conformance Oracle & Corpus

- [ ] CHK001 - Is the conformance oracle named and version-pinned (`[FIX50SP2 §3]`) so "conformant" has a single authoritative reference, not an implicit one? [Clarity, Plan Test-seam #2]
- [ ] CHK002 - Is the conformance corpus's representativeness defined (which message types / venues / sizes), so "a representative corpus" is not an unbounded term? [Ambiguity, Spec SC-001]
- [ ] CHK003 - Is each W-001..W-014 row bound to a named keyed corpus file so per-row coverage is auditable rather than aggregate? [Measurability, Plan Test-seam #2]
- [ ] CHK004 - Is the parse→serialize round-trip success criterion expressed as objectively measurable (byte-identical, including unknown/custom fields)? [Measurability, Spec SC-001/FR-008]
- [ ] CHK005 - Are the four codegen versions (v42/v44/v50sp2/vt11) named as the conformance scope, with runtime-XML-only versions explicitly out of this feature's typed scope? [Completeness, Spec FR-001/Assumptions]

## Encoding & Framing Fidelity

- [ ] CHK006 - Are the Tag=Value/SOH encoding rules (W-001) specified precisely enough that ASCII-digit tag, `=` separator, and SOH delimiter are unambiguous? [Clarity, Spec FR-001]
- [ ] CHK007 - Are the standard header (W-002) and trailer (W-003) mandatory fields and their ordering requirements stated, including the out-of-order/duplicated-header outcomes? [Completeness, Spec FR-001/Edge Cases]
- [ ] CHK008 - Is `BodyLength(9)` computation specified by exact byte range (field after BodyLength through byte before CheckSum) and rendering (digit-only, no space padding)? [Clarity, Spec FR-007 / research D-5]
- [ ] CHK009 - Is `CheckSum(10)` specified as unsigned byte-sum mod 256, exactly 3 zero-padded ASCII digits, with XOR explicitly excluded? [Clarity, Spec FR-007 / research D-5]
- [ ] CHK010 - Is pipelined multi-message framing (W-010) specified with the partial-carry and pre-parser verification obligations, not just "frame a stream"? [Completeness, Spec FR-009]

## Structural Fidelity

- [ ] CHK011 - Are repeating-group (W-006) and nested-group (W-007) requirements specified distinctly, including delimiter/first-field rules driven by the dictionary? [Completeness, Spec FR-004]
- [ ] CHK012 - Is the `Length`+`Data` (W-008) requirement specified with the Index-vs-Iter scope boundary (runtime dict vs static FIX-5.0-SP2 table), not left as "handle Data fields"? [Coverage, Spec FR-005 / research D-11]
- [ ] CHK013 - Is the W-009 field-data-type set enumerated (the spec lists the full type set) so "all field data types" is bounded, with decode/encode delegated to 2a/2c traits (no wire-side decoding)? [Clarity, Spec FR-006]
- [ ] CHK014 - Is the zero-copy requirement (W-011) stated as a non-owning, alias-the-buffer obligation with a measurable acceptance (zero copies / zero alloc)? [Measurability, Spec FR-001/SC-002]
- [ ] CHK015 - Is the offset-table O(1)-by-tag requirement (W-012) specified with occurrence-level addressability (not distinct-tag), consistent with FR-002? [Consistency, Spec FR-002]
- [ ] CHK016 - Are the serializer (W-013) and validator (W-014) conformance obligations each traced to a distinct FR and SC, not folded into a generic statement? [Traceability, Spec FR-007/FR-010]

## Inherited OSS Rows

- [ ] CHK017 - Are the inherited OSS-006 (header-only zero-copy, single TU, no hot-path heap) and OSS-008 (writer over caller-supplied buffer) obligations stated as testable requirements rather than only catalogue references? [Completeness, Plan §VI.4 / spec FR-007]
- [ ] CHK018 - Is it explicit that no NEW catalogue row is introduced (OSS rows inherited per `[2b §11]`), so coverage scope is bounded? [Clarity, Plan Constitution Check §VI.4]
- [ ] CHK019 - Is OSS-013 (SBE flyweight pattern) explicitly scoped as post-1.0 v1.2 (pattern applied, row not closed by 004), preventing an over-claim? [Conflict, tasks.md T057 / `[const §XVIII.2]`]

## Traceability & Normative References

- [ ] CHK020 - Does the spec include a Normative References section listing exact `[FIX50SP2 §X.Y]` entries per `[const §VI.5]`, so conformance claims are anchored? [Traceability, `[const §VI.5]`]
- [ ] CHK021 - Is bidirectional spec-section ↔ catalogue-row traceability required before any owned row is declared done (`[const §VI.4]` / `spec/coverage-index.md`)? [Traceability, Plan Constitution Check §VI.4]
- [ ] CHK022 - Is the QuickFIX interop scope explicitly deferred to the session layer (wire supplies the substrate only), so the conformance boundary is not over-claimed? [Clarity, Plan §VII.6 / spec Assumptions]
- [ ] CHK023 - Is every W-001..W-014 row traceable to at least one FR- and one SC-, with no owned catalogue row lacking a measurable outcome? [Traceability, Spec FR/SC ↔ feature-catalogue]
