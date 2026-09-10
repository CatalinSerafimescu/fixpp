# Specification Quality Checklist: Live QuickFIX interop fidelity — peer-side typed readback + dictionary-backed conversation

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-09-10
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [ ] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [ ] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

Two items are left **unchecked** — deliberate, reviewed deviations, not oversights. They were briefly
marked `[~]`; that marker is not a recognised checkbox state, so any tool scanning this file would have
counted 14/14 and reported a full pass. Left unchecked, they register as what they are.

**"Written for non-technical stakeholders"** and **"Success criteria are technology-agnostic"**.
This is a systems-library feature whose entire subject matter is protocol-level fidelity between two
FIX engines. Its stakeholders are the maintainer and the downstream library consumer; there is no
non-technical audience for "the peer's dictionary-backed parse must report the decoded value of
tag 40". The repository's house style, visible in every prior spec (e.g. `088-firstframe-budget-timer-lifetime`),
anchors claims to source with `file:line` citations, and `CLAUDE.md` requires that any description of
current behaviour be verifiable against source rather than asserted. Writing this spec to the generic
template's non-technical register would have made it unverifiable, which is the failure mode the
repository's own guidance is written against.

The substantive half of both items **is** honoured: the spec states **what must be true**, never
**how to build it**.

- FR-003/FR-004 require a "structured readback record" in a format identical across both
  counterparties — they do not choose JSON vs YAML vs a delimited line, a file vs a socket, or a
  schema.
- FR-013 lists the evidence a row must carry — it does not specify the field names, the file, or the
  serialization.
- FR-005 requires a message identity sufficient for pairing — it does not pick the identifier.
- No class names, function signatures, or file layouts are invented anywhere in the spec.

The `file:line` citations that do appear are all references to **existing** code, cited as evidence
for claims about the current state of the tree (the Context table), never as prescriptions for new
code. That distinction is what keeps the "no implementation details leak" item a genuine pass.

**Validation performed**: every factual claim in the Context section was verified against source on
`main` @ `e798a0f9` before the spec was written — the four capability gaps, the one-directional
assertion asymmetry, the sentinel dictionary, the absent evidence field, and the absent full-matrix
workflow were each confirmed independently rather than taken from the tracker or from a summary. The
row-4d detail file's "not the plumbing" premise was found to be **false** and is corrected in the
spec rather than inherited.


## Re-validation — 2026-09-10, after `/speckit-clarify`

Re-evaluated every item against the updated spec. **14/16 → 14/16.** No item changed state: nothing
newly passes, nothing regressed.

Five clarifications were integrated (exact-set field comparison; `MsgSeqNum(34)`+direction as the
correlation key; digest pin + capability handshake; two-level cell/witness result shape with its own
completeness gate; all three sanitizer configs). Each made an existing requirement *more* testable
rather than adding a new open question — FR count 20 → 28, SC count 8 → 12, and the circular
"field of interest" wording in FR-003/FR-006 that the first clarification targeted is gone.

The two unchecked items are unchanged in character: the clarifications added FIX-protocol vocabulary
(`MsgSeqNum(34)`, `PossDupFlag(43)`) and existing build-config identifiers (`normal`, `asan-ubsan`,
`tsan`), which are domain terms already used by the artifacts this feature edits — not newly-introduced
implementation prescriptions. No class name, function signature, file format, or transport is chosen
anywhere in the spec.

⚠️ One clarification resolved *against* the stated recommendation: the sanitizer axis was decided as
**all three configs**, not `normal` + `asan-ubsan`. That is recorded in Assumptions with its cost
stated plainly, because it pulls a first-ever TSan bring-up on the paired live matrix into scope.
