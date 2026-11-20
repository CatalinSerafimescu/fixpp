# Specification Quality Checklist: Transport and liveness (105)

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-11-20 · **Refreshed**: 2026-11-20, after the Gate A loop 2 round 1 pass (ruling D-g of the round 3 targeted edit; the ending bound's failure exit)
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — house style: a library spec names the public symbols, configuration knobs (`logout_disconnect_timeout_ms`, `tls_close_timeout`) and B&L rows it changes; mechanisms (how the close is scheduled, the ending state machine, the TestReqID classification) are the plan's (Assumptions; data-model E-2 to E-4)
- [x] Focused on user value and business needs — the counterparty's and the operator's observables
- [x] Written for non-technical stakeholders — as far as a FIX-engine contract allows
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — owner rulings R-1…R-4, the Gate A round 1 decisions D-a…D-c, the round 2 decisions D-d…D-f, the round 3 decision D-g (spawn failure fails closed) and the loop 2 round 1 scope ruling settle the open protocol and scope questions; OQ-3 is closed by D-e
- [ ] Requirements are testable and unambiguous — every FR has a planned cell marked RED-on-base or guard-with-mutant (FR-017; quickstart section 3), including D-g's spawn-failure seams and the bound's failure exit (FR-004b), but tasks.md is non-operative until it is regenerated after Gate A converges; re-check against the regenerated tasks
- [x] Success criteria are measurable — SC-001's "at once" is the contract's single definition, with the TLS close's `tls_close_timeout` bound
- [x] Success criteria are technology-agnostic (no implementation details) — stated as peer-observed EOF, timings in HeartBtInt multiples and configuration knobs, record counts
- [x] All acceptance scenarios are defined — Story 1 scenario 9 (the write limit) and Story 2 scenario 2 (the TestReqID rule) added in round 2
- [x] Edge cases are identified — the blocked Logout write, the TestReqID forms (earlier, previous connection, non-canonical) added in round 2; an allocation failure at a spawn (D-g) and inside the running bound (FR-004b) added since
- [x] Scope is clearly bounded — Out of Scope section; L-105-f names what stays unbounded; its follow-up issue and two pre-existing ones (the direct driver's shared timer slot, the pump's establishment-race spin) are listed in plan "Follow-ups to file before Gate B"
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria — FR-004b and FR-009's D-d clause included
- [x] User scenarios cover primary flows
- [ ] Feature meets measurable outcomes defined in Success Criteria — not yet demonstrable: it depends on the cells and the benchmark runs at the implementation head (`/speckit-verify`)
- [x] No implementation details leak into specification — see the Content Quality note

## Notes

- Decided in the plan, not the spec: the close mechanism (FR-001/FR-002; research D1), the ending state machine and the write limit's mechanism (FR-004, FR-004b; data-model E-3), the ending surface for slot 74 (FR-003; a log record written before the write), threshold validation of edge values (FR-011), the TestReqID classification (FR-009; data-model E-4), the rate bound of FR-014, the C-ABI minor (D9).
- Unchecked items are re-checked when tasks.md is regenerated and again at `/speckit-verify`.
