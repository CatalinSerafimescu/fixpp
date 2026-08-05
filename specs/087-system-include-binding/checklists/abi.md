# ABI-Boundary Requirements Checklist: 087 system include binding

**Purpose**: Unit tests for the *requirements writing* around the C-ABI consumption boundary this feature
binds — the fourth usage requirement `$<LINK_ONLY:>` withholds, and the 086 scope limit it closes.
**Created**: 2026-08-05
**Feature**: [spec.md](../spec.md) · [contracts/system-include-interface.md](../contracts/system-include-interface.md)
**Audience**: Gate B reviewer. Depth: formal release gate.

> These items test whether the **requirements are written well** — complete, unambiguous, consistent,
> measurable — not whether the implementation works. Implementation behaviour is asserted by contract §5's
> demonstrations and `tasks.md`, not here.

## Requirement Completeness

- [ ] CHK001 - Are the expected include sets for **both** bound targets enumerated member-by-member with a stated rationale per member, rather than given only as a count? [Completeness, Contract §1, data-model E3]
- [ ] CHK002 - Are requirements stated for the second leg (`fixpp::service`) as an independently declared obligation, with the reason it is **not** implied by the C-ABI leg? [Completeness, Spec §FR-001a, §Clarifications]
- [ ] CHK003 - Is the treatment of compiler- and SDK-supplied include roots specified as a **measured** fact with its measurement cited, rather than as an assumption about what the instrument reports? [Completeness, Contract §1a, research R2/R6]
- [ ] CHK004 - Is the prefix-relative comparison rule specified **together with** what happens to an observed entry that falls outside the prefix? [Completeness, Contract C-3, data-model I1]
- [ ] CHK005 - Are requirements stated for the disposition on a platform where the instrument is unavailable, **decided in advance** rather than left to implementation time? [Completeness, Spec §FR-010, §FR-010a]
- [ ] CHK006 - Does the amendment set distinguish artifacts to be **corrected** from historical records to be **appended to**, so provenance is preserved rather than rewritten? [Completeness, Contract §4a row 5]
- [ ] CHK007 - Are requirements explicit that no C-ABI header, symbol set or version-script change occurs — i.e. that this feature *asserts* an existing boundary rather than moving it? [Completeness, Spec §Out of Scope, plan Articles IX §5 / X §1]

## Requirement Clarity

- [ ] CHK008 - Is the comparison form (exact set equality vs containment vs deny-list) stated **once** with the rationale for rejecting each alternative, rather than restated divergently across artifacts? [Clarity, Spec §FR-003a, §Clarifications]
- [ ] CHK009 - Is it unambiguous that `isSystem` classification is **part of the compared value** and never discarded, including which side of the comparison it is matched within? [Clarity, Spec §FR-003a, data-model I2, Contract C-1]
- [ ] CHK010 - Does the spec make clear **which install prefix** the comparison uses at gate time, distinctly from any prefix used for a manual reproduction of the measurement? [Clarity, Contract C-3; closed by `tasks.md` T005a]
- [ ] CHK011 - Is the directional hazard — reverting the capi leg reds **both** legs — written as an inherited **measurement** rather than as a hypothesis? [Clarity, Spec §FR-007a, 086 FR-011e]
- [ ] CHK012 - Are the requirements for amending 086's C-3 bounded, stating exactly which legs become bound and by what instrument, and explicitly **not** generalising 086's reachability matrix? [Clarity, Contract §4, §4a "must not claim"]
- [ ] CHK013 - Is the constitutional basis for treating this feature as ABI-adjacent stated with its citation, rather than inherited silently from 086's disposition? [Clarity, Traceability, plan Article X §6, Spec §Normative References]

## Requirement Consistency

- [ ] CHK014 - Is the FR-011 amendment set defined in **exactly one** place, with every other artifact citing that definition rather than restating the list? [Consistency, Spec §FR-011, Contract §4a]
- [ ] CHK015 - Are the requirements consistent that entry **ordering is not asserted**, and is that recorded as a limitation rather than silently ignored? [Consistency, Contract §1a, data-model I2, Spec §Assumptions]
- [ ] CHK016 - Is the toolchain-scope argument stated **per toolchain**, each carried by the workflow and step that actually decides it — rather than by one cite that reaches only some of them? [Consistency, Coverage, Contract §1 table]
- [ ] CHK017 - Is the claim "unmeasured toolchains are covered because CI executes the same gate" made explicitly **conditional** on the thing that makes it true, rather than asserted unconditionally? [Consistency, Contract §1, §6]
- [ ] CHK018 - Do the spec's Key Entities and `data-model.md`'s entities describe the same set of things under the same names? [Consistency, Spec §Key Entities, data-model E1–E4]

## Acceptance Criteria Quality

- [ ] CHK019 - Can the bound property's headline success criterion be **objectively measured** — zero unexpected entries, zero missing entries, zero classification mismatches — without interpretation? [Measurability, Spec §SC-001]
- [ ] CHK020 - Is SC-007's universal wording ("no document still describes the property as an open scope limit") reconciled against an **enumerated** set, with the basis for exhaustiveness recorded as the command's own output rather than as an assertion about it? [Measurability, Traceability, Spec §SC-007, Contract §4a]

## Notes

- Check items off as dispositioned: `[x]` with an inline tag — `PASS` / `SPEC-FIXED` / `DD-DECIDED §X` / `WAIVED: <reason>`.
- Genuine Completeness / Clarity / Consistency gaps MUST be SPEC-FIXED or DD-DECIDED — never WAIVED.
- If **any** item is SPEC-FIXED, `/speckit-analyze` (pipeline step 6) is re-run before `/speckit-implement`.
