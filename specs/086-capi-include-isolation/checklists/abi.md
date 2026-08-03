# Checklist: C-ABI consumption surface — requirements quality

**Purpose**: Unit tests for the *requirements* governing the C-ABI and service consumption interfaces.
Validates whether they are complete, unambiguous, consistent and measurable — **not** whether the
implementation works (that is `/speckit-verify` and Gate B's job).
**Created**: 2026-08-04 · **Audience**: Gate B reviewer · **Depth**: formal gate
**Feature**: [spec.md](../spec.md) · **Domain**: Article IV §2 / Article X — the legal isolation boundary

## Requirement Completeness

- [ ] CHK001 - Is the complete set of headers that MUST be reachable through the C-ABI target enumerated by name, rather than described by a count? [Completeness, Spec §FR-002]
- [ ] CHK002 - Are requirements defined for **every** cell of the reachability matrix, including each ❌ cell, or does any cell rely on being implied by another? [Completeness, Contracts §1]
- [ ] CHK003 - Is the boundary of the isolation obligation — which targets it binds and which it deliberately does not — stated as a requirement rather than left to inference? [Completeness, Spec §FR-003a]
- [ ] CHK004 - Are requirements defined for what the service target MUST reach, not only for what it must not? [Completeness, Spec §FR-011a]
- [ ] CHK005 - Does the spec state a requirement for the C++ umbrella's continued reach over **both** header sets, or only over the C++ surface? [Completeness, Spec §FR-004, §FR-011c]
- [ ] CHK006 - Are requirements stated for usage requirements **other than** include directories that the narrowing mechanism also withholds? [Completeness, Spec §FR-009a]
- [ ] CHK007 - Is there a requirement covering what happens to a consumer that links both isolated and non-isolated targets? [Coverage, Edge Case, Spec §Edge Cases]

## Requirement Clarity

- [ ] CHK008 - Is "reachable" defined precisely enough to be evaluated — specifically, is it defined over the **transitive** interface rather than a single target's property? [Clarity, Spec §FR-003]
- [ ] CHK009 - Is the distinction between "by-name" and "closure-only" export members defined with a test a reader can apply to a new target, rather than by enumeration alone? [Clarity, Spec §FR-003a]
- [ ] CHK010 - Is "the isolated root" specified unambiguously enough that two implementers would produce the same installed path? [Clarity, Data-model §E1]
- [ ] CHK011 - Where a requirement names a specific source line as the thing to change, is the **claim** stated as well, so the requirement survives the line moving? [Clarity, Spec §FR-011d]
- [ ] CHK012 - Is the withheld-definition exception stated as a predicate rather than as a fixed list that can silently go stale? [Clarity, Ambiguity, Spec §FR-009a]

## Requirement Consistency

- [ ] CHK013 - Do the reachability requirements in the spec and the normative matrix in the contract agree cell-for-cell, with no cell present in one and absent from the other? [Consistency, Spec §FR-003 vs Contracts §1]
- [ ] CHK014 - Is the additivity requirement stated so it cannot be read as discharging the target-graph obligation? [Consistency, Conflict, Spec §FR-005a vs §FR-005b]
- [ ] CHK015 - Are the two isolation legs' independence claims consistent in **both** directions, given that one target links the other? [Consistency, Spec §FR-011d vs §FR-011e]
- [ ] CHK016 - Do the spec, data-model and contract describe the same set of installed roots with the same contents? [Consistency, Spec §Clarifications, Data-model §E1, Contracts §2]
- [ ] CHK017 - Does any surviving statement describe a ❌ assertion as a build target, contradicting the mechanism requirement? [Consistency, Conflict, Spec §FR-006a]

## Acceptance Criteria Quality

- [ ] CHK018 - Can the "0 C++ engine headers reachable" criterion be objectively evaluated given the evidence the design actually produces, or does it claim more than its named evidence establishes? [Measurability, Spec §SC-001]
- [ ] CHK019 - Is the demonstrated-red obligation expressed so that a reviewer can tell whether it was met, including **what** must be recorded? [Measurability, Spec §FR-007, §SC-002]
- [ ] CHK020 - Is the requirement that a positive assertion cannot establish a negative one stated as a binding rule on evidence, not merely as an explanatory note? [Measurability, Spec §FR-008a]
- [ ] CHK021 - Does the spec require the must-fail probe to fail *for the isolation reason*, with a stated way to distinguish that from any other failure? [Measurability, Spec §FR-008]
- [ ] CHK022 - Are the success criteria for the service leg as strong as those for the C-ABI leg, or weaker without a stated reason? [Consistency, Spec §SC-001a]

## Scenario & Edge Case Coverage

- [ ] CHK023 - Are requirements defined for a C consumer as distinct from a C++ consumer, given the user story promises both? [Coverage, Spec §US1]
- [ ] CHK024 - Is the case of the same header being reachable from two installed roots addressed in requirements? [Edge Case, Spec §Edge Cases]
- [ ] CHK025 - Are requirements stated for a consumer that bypasses CMake entirely (bare include-path flag)? [Coverage, Spec §FR-005a]
- [ ] CHK026 - Is there a requirement covering the case where a future dependency contributes a usage requirement the current exception predicate does not admit? [Gap, Coverage]

## Dependencies & Assumptions

- [ ] CHK027 - Is the assumption that the C-ABI headers are self-contained recorded, with a stated consequence if it becomes false? [Assumption, Spec §Assumptions]
- [ ] CHK028 - Is the assumption that the export closure does not move recorded as an assumption requiring measurement, rather than as a fact? [Assumption, Spec §FR-016]
- [ ] CHK029 - Is the dependency on the merged predecessor feature's export machinery documented? [Dependency, Spec §Dependencies]
- [ ] CHK030 - Is the C-ABI surface freeze status cited from a resolvable location, given the cited file lives in a different repository? [Traceability, Assumption]
