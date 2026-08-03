# Checklist: installed layout, package contents & export set — requirements quality

**Purpose**: Unit tests for the *requirements* governing what the package ships, where, and what the export
declares. Validates requirement quality, not package correctness.
**Created**: 2026-08-04 · **Audience**: Gate B reviewer · **Depth**: formal gate
**Feature**: [spec.md](../spec.md) · **Inherits**: `specs/084-packaging-cpack-export/contracts/package-layout.md`

## Requirement Completeness

- [ ] CHK031 - Is every installed root this feature adds specified with its exact destination and contents? [Completeness, Data-model §E1]
- [ ] CHK032 - Are requirements defined for what the isolated roots must **not** contain, or only for what they must? [Completeness, Spec §FR-010a]
- [ ] CHK033 - Is there a requirement that the package-contents gate assert the C-ABI headers positively, given nothing asserted them before this feature? [Completeness, Spec §FR-010]
- [ ] CHK034 - Are requirements stated for the export set's membership and its shipped object files, or is their stability assumed? [Completeness, Spec §FR-016]
- [ ] CHK035 - Is there a requirement covering the configure-time existence check that makes a missing shipped object file fatal for every consumer? [Coverage, Spec §Edge Cases]

## Requirement Clarity

- [ ] CHK036 - Is "purely additive" defined in terms of an observable comparison, and is the thing compared specified (produced manifests vs. install rules)? [Clarity, Spec §FR-005a, §SC-003a]
- [ ] CHK037 - Is the scope of additivity — file layout only, explicitly not the target graph — stated unambiguously? [Clarity, Spec §FR-005b]
- [ ] CHK038 - Where the requirements reference the existing header install rule, is it clear whether it may acquire *new* exclusions versus already carrying some? [Clarity, Contracts §2a]
- [ ] CHK039 - Is the baseline that "additive" is measured against pinned to a named commit rather than left as an ambient artifact? [Clarity, Measurability, Quickstart §2]

## Requirement Consistency

- [ ] CHK040 - Do the installed-root descriptions agree across spec, data-model, contract and quickstart? [Consistency]
- [ ] CHK041 - Is the export-member count stated consistently everywhere it appears, and is each occurrence either a measurement or explicitly marked a prediction? [Consistency, Spec §FR-016]
- [ ] CHK042 - Does any requirement describe the export membership basis in a way that conflicts with how membership is actually determined? [Consistency, Conflict, Spec §FR-003a]
- [ ] CHK043 - Are the packaging requirements consistent with the inherited D1 decision, or is the divergence recorded where they differ? [Consistency, Dependency]

## Acceptance Criteria Quality

- [ ] CHK044 - Can the additivity criterion fail? Is the comparison specified so that a removed path produces a non-zero result rather than a printed line nobody checks? [Measurability, Spec §SC-003a]
- [ ] CHK045 - Is the content assertion required to be demonstrated failing, or only to exist? [Measurability, Spec §SC-005]
- [ ] CHK046 - Is the export re-measurement required from a real generate run, with reading the build files explicitly excluded as a method? [Measurability, Spec §FR-016]
- [ ] CHK047 - Is the criterion for "no production source edited" expressed so it can fail, given that the natural command reports success either way? [Measurability, Spec §SC-007]

## Scenario & Edge Case Coverage

- [ ] CHK048 - Are requirements stated for the platform asymmetry in the installed prefix, so a content assertion cannot pass vacuously on one platform? [Edge Case, Coverage, Spec §Edge Cases]
- [ ] CHK049 - Are requirements defined for the archive/object naming differences across toolchains, where content assertions must match? [Coverage, Gap]
- [ ] CHK050 - Is the consequence of shipping the same header at two paths addressed for any assertion that counts or exact-matches installed paths? [Edge Case, Spec §Edge Cases]
- [ ] CHK051 - Are requirements stated for a stale staging prefix, given that installing does not remove files left by a previous install? [Edge Case, Gap]

## Dependencies & Assumptions

- [ ] CHK052 - Is the dependency on the predecessor feature's packaging machinery documented, including which parts must not change? [Dependency, Spec §Dependencies]
- [ ] CHK053 - Is the assumption that no part of the inherited packaging arrangement needs modification stated and traced to evidence? [Assumption, Research §R2]
- [ ] CHK054 - Are the citation corrections owed to the inherited contract scoped by claim, with the non-uniform nature of the drift recorded? [Traceability, Spec §FR-015]
