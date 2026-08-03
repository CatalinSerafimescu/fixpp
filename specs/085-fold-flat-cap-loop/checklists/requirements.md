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

**Note 2 — SC-005 is necessarily mechanism-referencing.**
SC-001..SC-004 and SC-006..SC-009 are outcome-stated and technology-agnostic. SC-005 ("exactly one traversal") names the mechanism because the mechanism *is* the deliverable — an outcome-only restatement ("the engine is not slower") is already SC-006 and does not discharge the issue's acceptance criterion. Kept as written, flagged here.

**Note 3 — A-001 is a load-bearing assumption, not a conclusion.**
The redundancy argument that justifies FR-001/FR-002 was derived by the orchestrator at `main` = `c1564dd2`. A-001 obliges `/speckit-plan` to re-verify all four steps independently rather than inherit them. If any step does not hold, the feature is re-scoped rather than patched. Downstream reviewers should treat the argument as *claimed and checkable*, not settled.

**Note 4 — no [NEEDS CLARIFICATION] markers were raised.**
The issue text, the L-063-4 row and 083's recorded evidence between them fixed every scope decision: leg 1 stays descoped (A-002), #180 stays closed (A-003), the dict-free path is preserved not repaired (FR-003, A-005), and no configuration surface is added (A-006). `/speckit-clarify` is still mandatory for this feature under constitution §XVI.3 (wire-layer change) and runs next.

- Items marked incomplete require spec updates before `/speckit-clarify` or `/speckit-plan`. None are incomplete; three carry documented deviations.
