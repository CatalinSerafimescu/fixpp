# Reconnect / Session-FSM Lifecycle Requirements Checklist: Live Transport Wiring (014-transport-active-binding)

**Purpose**: Validate that the requirements for the realized initiator reconnect loop — triggering conditions, backoff/cap, reason-agnostic retry-to-cap, cancellation/release, success handoff, and terminal disposition — are complete, unambiguous, consistent, and measurable BEFORE implementation. Tests the requirements, not the FSM behaviour.
**Created**: 2026-05-29
**Feature**: [spec.md](../spec.md) · Gate-A trigger: Session FSM

## Requirement Completeness

- [ ] CHK001 Are the conditions that trigger a reconnect attempt enumerated completely (connection drop, handshake failure, transport read/write error)? [Completeness, Spec §FR-001]
- [ ] CHK002 Does FR-003's "every failed reconnect attempt" explicitly include a **factory `make()` failure** as a counted attempt, or only connect/handshake/authorization? [Completeness, Gap, Spec §FR-003]
- [ ] CHK003 Is the requirement that NO production code path may mint a transport and discard it (full stub removal) stated as a verifiable invariant? [Completeness, Spec §FR-005]
- [ ] CHK004 Is the meaning of "session resumes" at the 014 boundary specified (produce authorized live transport + re-drive Logon to Active), with the continuous read-pump explicitly carved to 015? [Completeness, Spec §FR-001/§Assumptions/§Out of Scope]

## Requirement Clarity

- [ ] CHK005 Is "one attempt" defined unambiguously so that every failure cause (make/connect/handshake/authorize) counts identically (reason-agnostic)? [Clarity, Spec §FR-003]
- [ ] CHK006 Is the backoff schedule + attempt cap clearly delegated to 012's `ReconnectPolicy` (consumed, not redefined) rather than re-specified? [Clarity, Spec §FR-002]
- [ ] CHK007 Is the terminal outcome at cap exhaustion named precisely (the FSM's terminal disconnected outcome / no infinite retry)? [Clarity, Spec §FR-003]

## Requirement Consistency

- [ ] CHK008 Is the reconnect-path authorize-failure disposition (count one attempt + retry-to-cap, NOT terminal Disconnected) stated as an *intentional* divergence from the inherited 013 open-Logon terminal disposition? [Consistency, Spec §FR-007/§Clarifications Q1]
- [ ] CHK009 Are cancellation requirements consistent across both `co_await` regions — the in-flight handshake AND the inter-attempt backoff sleep? [Consistency, Spec §FR-004/§Edge Cases]

## Acceptance Criteria Quality / Measurability

- [ ] CHK010 Is the consecutive-attempt count **N** for the no-leak success criterion quantified (SC-004 currently says "across N … attempts" without a minimum)? [Measurability, Gap, Spec §SC-004]
- [ ] CHK011 Is "100% of reachable cases" in SC-001 bounded — is "reachable" defined so the criterion is objectively testable? [Measurability, Ambiguity, Spec §SC-001]
- [ ] CHK012 Is SC-002 ("consumes exactly one attempt; terminates at the cap") expressed against an observable counter / terminal state? [Measurability, Spec §SC-002]

## Scenario & Edge-Case Coverage

- [ ] CHK013 Are requirements defined for cancellation mid-handshake — prompt abort + release of the partially-constructed transport, no leak / no orphaned socket? [Coverage, Spec §FR-004/§Edge Cases]
- [ ] CHK014 Is reconnect exclusivity (no new attempt starts while a prior attempt's transport is still in flight) specified as a requirement? [Coverage, Spec §Edge Cases]
- [ ] CHK015 Are requirements defined for the persistently-failing-peer flow terminating at the cap (no infinite retry)? [Coverage, Spec §FR-003/US1 AC2]

## Dependencies & Assumptions

- [ ] CHK016 Is the assumption "reconnect is initiator-side; acceptor sessions re-accept (acceptor live path → 015)" documented with its 014 boundary clear? [Assumption, Spec §Assumptions]
- [ ] CHK017 Is the dependency on 012's `ReconnectPolicy` (schedule/jitter/`max_attempts`/`delay_for_attempt(n)`) and on ASIO native total-cancellation explicitly cited? [Dependency, Spec §FR-002/§FR-004/§Dependencies]

## Notes

- Check items off as completed: `[x]`; record disposition (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline for the step-9 audit.
- CHK002 and CHK010 are the highest-value gaps (factory-make counting; SC-004 N) — resolve in spec or disposition explicitly.
