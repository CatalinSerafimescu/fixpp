# Specification Quality Checklist: Resend and Logon-reset fidelity

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-11-14
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *house style: fixpp specs name the public surfaces
  and FIX fields a counterparty or operator observes (`SessionConfig`, `Session::send`, tags). Exceptions since
  Gate A (2026-11-15), each recording an owner ruling or a Gate A finding whose subject is a mechanism: FR-006 and
  the loop-1-exhausted Clarifications name the reconnect fence's predicate (`cfg_.role`, `cfg_.engine_managed`,
  `live_transport()`), FR-010 names the session-wide Logout owner, and SC-006 names `inbound_depth_`'s holder as
  what the inbound bench row covers; the mechanisms themselves are in research D1, D4.6 and D5′*
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders — *audience is FIX operators and counterparty engineers, as every fixpp spec*
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — every protocol point carries an owner ruling (2026-11-14, and the
  Gate A rulings of 2026-11-14 and 2026-11-15, the acceptor-fence erratum confirmed)
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details) — *SC-005/SC-006 name the sanitizer and
  bench lanes as the measuring instrument, not the mechanism*
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded (Out of Scope section)
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification — *the named exceptions under Content Quality only*

## Notes

- `/speckit-clarify` ran 2026-11-14 (4 questions, recommendations owner-pre-approved): C-ABI default change is
  BREAKING (MINOR bump, Appendix A); the pending-Logout bound is `logout_disconnect_timeout_ms`; sends while a peer
  Logout is pending return `session_invalid_state_for_send`; #581's deadline never cuts off a frame that advances
  NextNumIn. Re-validated: 16/16.
- Re-validated 2026-11-15 after Gate A converged (two loops; owner sign-off 2026-11-15): every item still holds.
  Changed since: SC-002 re-scoped to the live Logon cells plus an in-process FileStore restart (owner ruling, the
  live restart stays fixpp#503); SC-005's repeat count derived from a measured miss rate; SC-006 gains the paired
  inbound-frame row; FR-006/FR-010/FR-018 carry the Gate A carve-outs and rulings.
