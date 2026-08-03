# Checklist: non-functional requirements — portability, compatibility, evidence quality

**Purpose**: Unit tests for the *requirements* covering cross-platform behaviour, consumer compatibility,
and the quality of the evidence the feature obliges itself to produce.
**Created**: 2026-08-04 · **Audience**: Gate B reviewer · **Depth**: formal gate
**Feature**: [spec.md](../spec.md)

## Portability & Platform Coverage

- [ ] CHK055 - Are requirements stated for every platform the feature ships on, or do some hold only where they were measured? [Coverage, Completeness, Plan §Technical Context]
- [ ] CHK056 - Is it specified which obligations can be discharged locally and which are CI-only, so a local green is not mistaken for full coverage? [Clarity, Coverage, Quickstart §10]
- [ ] CHK057 - Are requirements written so that no assertion needs a per-platform branch to hold? [Consistency, Spec §Edge Cases]
- [ ] CHK058 - Is the toolchain-dependent naming of installed artifacts accounted for in any requirement that names a file? [Coverage, Gap]

## Consumer Compatibility

- [ ] CHK059 - Is the requirement that no consumer-visible include spelling changes stated explicitly, rather than implied by the layout being additive? [Completeness, Spec §Assumptions]
- [ ] CHK060 - Are requirements defined for consumers who do not use the package's CMake config at all? [Coverage, Spec §FR-005a]
- [ ] CHK061 - Is the absence of any migration obligation stated as a consequence of a requirement, rather than left unmentioned? [Clarity, Spec §Assumptions]
- [ ] CHK062 - Are the requirements clear about which consumption paths carry an isolation guarantee and which explicitly do not? [Clarity, Spec §FR-003a]

## Evidence Quality (the feature's own gates)

- [ ] CHK063 - Does every gate this feature adds have a stated way to fail, or is any of them capable only of passing? [Measurability, Coverage]
- [ ] CHK064 - Is there a requirement that a gate never observed failing does not count as evidence? [Completeness, Spec §FR-007]
- [ ] CHK065 - Are requirements stated about *where* evidence is durably recorded, or is capture left implicit? [Completeness, Measurability, Quickstart §0]
- [ ] CHK066 - Is the requirement to record a selected-test count — not merely an exit code — stated, given a filter matching nothing exits successfully? [Measurability, Quickstart §0]
- [ ] CHK067 - Are the limits of each measured finding recorded alongside it, so a fixture result is not read as a product guarantee? [Clarity, Research §Limits]
- [ ] CHK068 - Is it specified which obligations remain unproven at design time and must be re-established during implementation? [Completeness, Research §"not yet proven"]

## Maintainability of the Requirements Themselves

- [ ] CHK069 - Are figures that can go stale (counts, member totals) either derivable by a stated command or marked with their derivation date? [Clarity, Maintainability]
- [ ] CHK070 - Are requirements that reference source lines scoped by claim, so they survive the line moving? [Clarity, Spec §FR-013, §FR-014]
- [ ] CHK071 - Is every cross-repository citation marked so a reader knows which repository resolves it? [Traceability]
- [ ] CHK072 - Are the deferred carry-forward items recorded with an owning stage, rather than as unattributed future work? [Traceability, Completeness]

## Process & Governance

- [ ] CHK073 - Are the mandatory controls for this class of change enumerated with their current status, rather than assumed discharged? [Completeness, Plan §Constitution Check]
- [ ] CHK074 - Is the basis for treating this feature as ABI-affecting recorded as a deliberate classification rather than presented as the article's literal trigger? [Clarity, Plan §Constitution Check]
- [ ] CHK075 - Are the constitutional obligations that are **not** waived by this feature's nature distinguished from those that are? [Clarity, Consistency, Plan §Constitution Check]
- [ ] CHK076 - Are the worktree-specific hazards that affect verification stated where a verifier will encounter them? [Completeness, Coverage, Tasks §banner]
