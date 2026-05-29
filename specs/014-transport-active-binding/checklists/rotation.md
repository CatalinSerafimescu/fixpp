# Credential-Rotation Observability Requirements Checklist: Live Transport Wiring (014-transport-active-binding)

**Purpose**: Validate that the requirements for emitting genuine `credentials_rotated` events — timing/ordering, fingerprint source/shape, no-op semantics, and first-load handling — are complete, unambiguous, consistent, and measurable BEFORE implementation. Tests the requirements, not the emit behaviour.
**Created**: 2026-05-29
**Feature**: [spec.md](../spec.md)

## Requirement Completeness

- [ ] CHK001 Is the emit timing fully specified — at the *next* `drive_reconnect_attempt` after `reload_credentials`, on the session strand, immediately BEFORE the new snapshot is passed to `make()`? [Completeness, Spec §FR-009]
- [ ] CHK002 Is the event payload fully specified — the REAL SHA-256 end-entity (leaf) fingerprints of our OWN old and new `cert_source`, as raw 32-byte arrays? [Completeness, Spec §FR-010]
- [ ] CHK003 Is the "first-ever credential load is NOT a rotation (no event)" behaviour stated in a requirement, or only in design record E-3? [Completeness, Gap, Spec §FR-009]

## Requirement Clarity

- [ ] CHK004 Is "leaf fingerprint" defined unambiguously (end-entity certificate, SHA-256 over the leaf DER)? [Clarity, Spec §FR-010]
- [ ] CHK005 Is "no-op rotation" defined precisely (new leaf fingerprint equals the current one) and distinguished from "no rotation staged"? [Clarity, Spec §FR-011/§Edge Cases]
- [ ] CHK006 Does the requirement stay implementation-agnostic about WHO emits (behavioural "on the session strand") rather than over-constraining the FSM/Session split? [Clarity, Spec §FR-009]

## Requirement Consistency

- [ ] CHK007 Are the `credentials_rotated` semantics consistent with the inherited 013 FR-032 (emit-before-`make()`, no-op-still-emits, not change-gated)? [Consistency, Spec §FR-009..011/§Assumptions]
- [ ] CHK008 Is the emit-ordering phrasing consistent across FR-009 ("before `make()`"), US3 ("before the rotated certificate is used"), and SC-005? [Consistency, Spec §FR-009/US3/§SC-005]

## Acceptance Criteria Quality / Measurability

- [ ] CHK009 Can SC-005 ("exactly one `credentials_rotated` … carrying the real new leaf fingerprint") be objectively verified (count == 1 AND fingerprint matches the loaded leaf)? [Measurability, Spec §SC-005]
- [ ] CHK010 Is the no-op case's acceptance expressed measurably (`old_sha256 == new_sha256`, event present not suppressed)? [Measurability, Spec §FR-011/§SC-005]

## Scenario & Edge-Case Coverage

- [ ] CHK011 Are requirements defined for the no-op rotation still emitting (not suppressed)? [Coverage, Spec §FR-011/§Edge Cases]
- [ ] CHK012 Is the staging precondition covered — the event emits only after `reload_credentials` has staged a new `cert_source` (vs steady state with no rotation)? [Coverage, Spec §FR-009]

## Dependencies & Assumptions

- [ ] CHK013 Is the assumption that `credentials_rotated` semantics are "locked by merged 013 FR-032" documented? [Assumption, Spec §Assumptions]
- [ ] CHK014 Is the dependency on the 013 `reload_credentials` in-process control plane (which stages the new source) explicitly cited? [Dependency, Spec §FR-009/§Dependencies]

## Notes

- Check items off as completed: `[x]`; record disposition (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline for the step-9 audit.
- CHK003 (first-load-no-event in a requirement vs design-only) is the main completeness probe.
