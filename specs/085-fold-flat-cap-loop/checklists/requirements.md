# Specification Quality Checklist: Fold the Flat Per-Instance Cap Loop into the Nesting-Aware Traversal

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-08-03
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — **DEVIATION, deliberate. See Note 1.**
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders — **DEVIATION, deliberate. See Note 1.**
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details) — **partial, see Note 2**
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification — **DEVIATION, deliberate. See Note 1.**

## Notes

**Note 1 — source-anchored specification is this project's established form for an internal-defect feature.**
Three Content Quality / Feature Readiness items are ticked as *deliberate deviations*, not as passes on the template's literal wording. This feature's entire subject is the relationship between two functions in one translation unit: the deliverable is "remove a walk that provably cannot fire". Stated without naming the sites, FR-001 would be untestable and FR-002's behaviour-preservation claim would be unfalsifiable. The precedent is `specs/083-group-delimiter-resolution/spec.md`, whose Context section names files, line numbers and functions throughout and which passed Gate A on that form. Recorded here rather than silently ticked so a reviewer can overrule it.

**Note 2 — SC-005 is necessarily mechanism-referencing, and was rewritten once to stay falsifiable.**
SC-001..SC-004 and SC-006..SC-009 are outcome-stated and technology-agnostic. SC-005 ("exactly one traversal") names the mechanism because the mechanism *is* the deliverable — an outcome-only restatement ("the engine is not slower") is already SC-006 and does not discharge the issue's acceptance criterion. Its first draft demanded a "strict reduction in entries visited per call", which **nothing in the engine can measure**: there is no entries-visited counter and adding one to a hot path to satisfy a criterion would be scope creep. Restated as discharged by source inspection, with SC-006's benchmark carrying the performance claim.

**Note 2a — three named artifacts were verified to exist rather than inherited.**
SC-001's two test names came from the issue body, not from a source read. Both were checked at `main` = `c1564dd2` before this checklist was signed: `DelimiterCensus.RedCountsReconcileWithSpecBaseline` at `tests/dictionary/delimiter_census_test.cpp:476`, and seven `TypedReadSplitAgreement.*` tests in `tests/wire/typed_read_split_agreement_test.cpp`. The cap's fixture-reachability (A-006a) was verified for the same reason. This matters because a *dangling* success criterion is indistinguishable from a satisfied one until someone tries to run it.

**Note 3 — A-001 is a load-bearing assumption, not a conclusion.**
The redundancy argument that justifies FR-001/FR-002 was derived by the orchestrator at `main` = `c1564dd2`. A-001 obliges `/speckit-plan` to re-verify all four steps independently rather than inherit them. If any step does not hold, the feature is re-scoped rather than patched. Downstream reviewers should treat the argument as *claimed and checkable*, not settled.

**Note 4 — no [NEEDS CLARIFICATION] markers were raised, and `/speckit-clarify` still changed the spec materially.**
The issue text, the L-063-4 row and 083's recorded evidence between them fixed every *scope* decision up front: leg 1 stays descoped (A-002), #180 stays closed (A-003), the dict-free path is preserved not repaired (FR-003, A-005), and no configuration surface is added (A-006). That is why the draft carried no markers. `/speckit-clarify` — mandatory here under constitution §XVI.3 as a wire-layer change — nonetheless ran a full 5/5 session on 2026-08-03 and produced seven new requirements (FR-001a, FR-003a, FR-005a, FR-007b, SC-004a, SC-010, A-006a) plus a rewritten SC-006. The lesson worth recording: *no unresolved markers* is not the same as *no unresolved decisions*. The five that mattered were all questions the draft had silently answered by picking a default — what "fold" concretely means, whether a discovered dict-free false positive gets repaired here, what evidence discharges "proven RED", which benchmark, and how to anchor references that go stale on merge. None would have surfaced as a marker, because each already had a plausible answer written into the spec.

**Note 5 — one clarification opened new work rather than closing it.**
Q2 surfaced a defect the feature was not looking for: on the dict-free path `group_end = entries_.size()`, so the flat cap's final segment counts trailing top-level fields toward one instance and can return `err_group_too_large` for a group far below the cap. It is deliberately **not** repaired here (FR-003a) — doing so would break SC-002's "zero outcomes change" on the one path this feature was told to leave alone. It is instead recorded and given its own tracking issue, gated by SC-010. Reviewers should read FR-003a as a scope *decision*, not an oversight.

- Items marked incomplete require spec updates before `/speckit-clarify` or `/speckit-plan`. None are incomplete; three carry documented deviations.
