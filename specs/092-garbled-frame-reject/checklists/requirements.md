# Specification Quality Checklist: The session never acts on a frame it could not parse

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-09-27
**Updated**: 2026-09-27 (post-round-3 clarify/plan refresh; Gate A loop 2, rounds 1 and 2)
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — see Notes
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders — within a FIX-protocol audience
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous — the items once deferred are resolved in spec.md Clarifications: 373=5 for shape (A) (FR-007), Reject-only with no identity disconnect (FR-002), and the pre-Active disposition (FR-009, FR-015). Gate A loop 2 restated SC-002/SC-003 over contract C-2's columns (state, field 3 is 35, 34 read, MsgType), so no SC implies a Reject that C-2 does not send
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details) — except SC-009 (the C-ABI version pin), which is the owner-ruled declaration itself
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded — fixpp#514 and fixpp#515 are named out of scope; the inbound seqnum_max bound (FR-019) is named in scope by the loop-2 round-2 owner ruling (spec.md Assumptions)
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification — see Notes

## Notes

- **Named library symbols.** The spec names a small number of existing library symbols:
  - the validator, the iterator and the error values in FR-012;
  - the reason mapping in FR-007;
  - the C-ABI header text and the `admin_messages.hpp` sentence in FR-014;
  - the version macro in FR-017.

  This follows house style (the 090 and 091 specs do the same): they identify WHAT changes, not
  how. The algorithmic choices are in plan.md and research.md (R-1 to R-14). FR-019 names
  `seqnum_max`, the existing bound the requirement is about.
- **`/speckit-clarify`** is mandatory for this feature (Art. XVI §3: session FSM, error semantics,
  wire parser). It is done. Session 2026-09-27 answered the reason code (FR-007), the wrong-CompID
  case (FR-002) and the pre-Active disposition (FR-009, FR-015), and nothing from it remains open.
  Gate A round 1 added two owner rulings (spec.md "Session 2026-09-27 (Gate A round 1)"): C-ABI 1.10
  BREAKING (FR-017), and FR-012 kept and fully specified. Gate A round 2 added two more (spec.md
  "Session 2026-09-27 (Gate A round 2)"): O-1, a Framer failure stays session-fatal and its
  disregard is fixpp#514 (FR-008, FR-009); O-2, a late parse failure is fail-closed and the resource
  case is fixpp#515 (FR-016). Session "2026-09-27 (after Gate A round 3)" answered R3-001..R3-005;
  the plan refresh applied them plus R3-006 (plan.md §Gate A). Session "2026-09-27 (Gate A loop 2,
  round 2)" folded the inbound seqnum_max bound into 092 (FR-019, SC-010).
- **Gate A** is required (Art. XVII §1: parser, session FSM, public C++ API and C ABI). Rounds 1
  and 2 are applied, round 3 was exhausted, and loop 2 rounds 1 and 2 are applied. Its status and the recorded disagreements are in plan.md §Gate A. The former open item
  A-4 (whether a C-ABI version note is needed) is resolved by the owner ruling.
