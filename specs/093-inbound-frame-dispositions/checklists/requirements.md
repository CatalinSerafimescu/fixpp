# Specification Quality Checklist: Inbound frames 092 leaves out

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-10-02
**Reconciled**: 2026-10-03, after Gate A round 3's close-out edit
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs). House exception, wider than at specify
  time: like 092, the spec names the code it changes (`SessionConfig`, `MessageStore`, the C-ABI
  setter), because the owner rulings are stated in those terms. Since the Gate A rewrites, the spec also
  fixes mechanisms where a requirement is only testable in their terms: the resync start rule and its
  header caps, the two-phase establishment deadline and its loop-head check, the reset unit's
  cancellation shield and the engine-stop flag, and `unknown_fields()`'s catch. Their detail lives in
  the contract and the data model, and the spec cites them.
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders (FIX-session operators and integrators are the audience)
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain. FR-004's ruling was conditional on Research R-1, and R-1
  confirmed it (Clarifications, Session 2026-10-02 (specify)). FR-015 is conditional on fixpp#540's
  reproduction, and SC-008 states both outcomes.
- [x] Requirements are testable and unambiguous. Each FR and SC maps to a contract clause and named
  cells in quickstart §4; FR-050, FR-051 and FR-053 map to named gates instead (plan.md `## Gate A`,
  round 2 disagreements).
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details), except where a FIX test-case ID,
  a build lane or a named mechanism names the measurement
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded (see "Out of scope")
- [x] Dependencies and assumptions identified (B25 first; B15 rule at Gate A; #540 reproduced first)

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification beyond the house exception above

## Notes

- `/speckit-clarify` ran on 2026-10-02 and asked 4 questions: pre-Logon disregard, the timeout's default
  and zero, the C-ABI counter, and the 256 KiB cap. All are integrated into spec.md. The two items it
  deferred to the plan are settled there: the acceptor's clock start against the first-frame deadline
  (contract C-4 phase (a)), and #534, which FR-006 does not wait for (it closes only its own expiry's
  transport).
- Gate A rounds 1 and 2 added code-fact clarifications, not owner rulings (spec.md Clarifications), and
  orchestrator decisions OD-1 to OD-18 (plan.md). Round 3's close-out narrowed FR-011, FR-041, SC-006
  and contract C-1, C-3, C-6 to their stated conditions, and added OD-19 (fixpp#541 out of scope). No
  checklist item changed state.
