# Credential-Rotation Observability Requirements Checklist: Live Transport Wiring (014-transport-active-binding)

**Purpose**: Validate that the requirements for emitting genuine `credentials_rotated` events — timing/ordering, fingerprint source/shape, no-op semantics, and first-load handling — are complete, unambiguous, consistent, and measurable BEFORE implementation. Tests the requirements, not the emit behaviour.
**Created**: 2026-05-29
**Feature**: [spec.md](../spec.md)

## Requirement Completeness

- [x] CHK001 Is the emit timing fully specified — at the *next* `drive_reconnect_attempt` after `reload_credentials`, on the session strand, immediately BEFORE the new snapshot is passed to `make()`? [Completeness, Spec §FR-009] — PASS: FR-009 says "on the session strand at the next `drive_reconnect_attempt`, immediately BEFORE the new `cert_source_snapshot()` is passed to `make()`." All three timing constraints (next attempt, on strand, before make()) are stated.
- [x] CHK002 Is the event payload fully specified — the REAL SHA-256 end-entity (leaf) fingerprints of our OWN old and new `cert_source`, as raw 32-byte arrays? [Completeness, Spec §FR-010] — PASS: FR-010 says "the event MUST carry the REAL SHA-256 end-entity (leaf) fingerprints of the old and new `cert_source` as raw 32-byte arrays, computed inside the credential-load path." The type is also confirmed by the event definition in `session_event.hpp:102-105` (`std::array<std::byte,32>` fields `old_sha256`/`new_sha256`). Completely specified.
- [x] CHK003 Is the "first-ever credential load is NOT a rotation (no event)" behaviour stated in a requirement, or only in design record E-3? [Completeness, Gap, Spec §FR-009] — SPEC-FIXED: This behaviour was only in design record E-3 and the FSM contract C3 ("If `last_active_source_ == nullptr` → initial load, set members, **no** event"), not in a normative FR. A normative sentence has been added to FR-009 in spec.md: "The **first-ever credential load** … is NOT a rotation and MUST NOT emit a `credentials_rotated` event; the FSM silently sets its rotation-detect baseline and proceeds to `make()`." Affected: `spec.md:§FR-009`.

## Requirement Clarity

- [x] CHK004 Is "leaf fingerprint" defined unambiguously (end-entity certificate, SHA-256 over the leaf DER)? [Clarity, Spec §FR-010] — PASS: FR-010 says "SHA-256 end-entity (leaf) fingerprints…raw 32-byte arrays, computed inside the credential-load path." The 013 FR-032 (cited in spec Assumptions and FR-009) adds "SHA-256 fingerprints of the END-ENTITY (leaf) client/server cert…SHA-256 of the leaf's DER encoding." The Key Entities section for "Credential-rotation observation" says "our own `cert_source` end-entity SHA-256 (old and new)." The computation is from `local_credentials` leaf DER (R3). Unambiguous.
- [x] CHK005 Is "no-op rotation" defined precisely (new leaf fingerprint equals the current one) and distinguished from "no rotation staged"? [Clarity, Spec §FR-011/§Edge Cases] — PASS: FR-011 defines it as "`old_sha256 == new_sha256`" explicitly. The Edge Cases section says "No-op credential rotation: `credentials_rotated` still emits with `old==new`." US3 AC2 says "the new `cert_source` leaf fingerprint equals the current one." The distinction from "no rotation staged" is implied by FR-009's "after `reload_credentials` has staged a new `cert_source`" — no staging = no event. Clear.
- [x] CHK006 Does the requirement stay implementation-agnostic about WHO emits (behavioural "on the session strand") rather than over-constraining the FSM/Session split? [Clarity, Spec §FR-009] — PASS: FR-009 says "the system MUST emit…on the session strand" — it specifies the behavioural requirement (emit, strand, before make()) without mandating whether the FSM or Session physically calls the emit. The design record E-3/C3 records the implementation decision (FSM holds detect state, Session holds the strand-bound emit callback), but that is not repeated in the normative FR text. The requirement is properly implementation-agnostic.

## Requirement Consistency

- [x] CHK007 Are the `credentials_rotated` semantics consistent with the inherited 013 FR-032 (emit-before-`make()`, no-op-still-emits, not change-gated)? [Consistency, Spec §FR-009..011/§Assumptions] — PASS: FR-009 says "per 013 FR-032" and "immediately BEFORE the new `cert_source_snapshot()` is passed to `make()`." FR-011 says "The event MUST NOT be suppressed on a no-op rotation (`old_sha256 == new_sha256`), per 013 FR-032." The spec Assumptions say "`credentials_rotated` semantics are locked by merged 013 FR-032 — our own rotated `cert_source` leaf fingerprints, emitted at `drive_reconnect_attempt` before `make()`, not change-gated." 013 FR-032 verified to exist (grep confirmed). Consistent.
- [x] CHK008 Is the emit-ordering phrasing consistent across FR-009 ("before `make()`"), US3 ("before the rotated certificate is used"), and SC-005? [Consistency, Spec §FR-009/US3/§SC-005] — PASS: FR-009: "immediately BEFORE the new `cert_source_snapshot()` is passed to `make()`." US3 AC1: "before the new snapshot is passed to `make()`." SC-005: "exactly one `credentials_rotated` event at the next handshake carrying the real new leaf fingerprint." The ordering is consistently "before make()" across all three locations. (US3's "before the rotated certificate is used" is slightly looser but aligns with FR-009's more precise "before make()" phrasing.) No contradiction.

## Acceptance Criteria Quality / Measurability

- [x] CHK009 Can SC-005 ("exactly one `credentials_rotated` … carrying the real new leaf fingerprint") be objectively verified (count == 1 AND fingerprint matches the loaded leaf)? [Measurability, Spec §SC-005] — PASS: SC-005 says "exactly one `credentials_rotated` event at the next handshake carrying the real new leaf fingerprint (including no-op rotations, which emit `old==new`)." The test (T016 `test_credentials_rotated_emit.cpp`) counts events in `Session::recent_events()` ring and compares `new_sha256` against the fingerprint of the loaded leaf DER. Both the count (== 1) and the fingerprint match are independently verifiable. Measurable.
- [x] CHK010 Is the no-op case's acceptance expressed measurably (`old_sha256 == new_sha256`, event present not suppressed)? [Measurability, Spec §FR-011/§SC-005] — PASS: FR-011 says "The event MUST NOT be suppressed on a no-op rotation (`old_sha256 == new_sha256`)." SC-005 includes "no-op rotations, which emit `old==new`." US3 AC2 says "the event is still emitted with `old_sha256 == new_sha256` (not suppressed)." The observable is: event present in the ring AND `old_sha256 == new_sha256`. Measurable.

## Scenario & Edge-Case Coverage

- [x] CHK011 Are requirements defined for the no-op rotation still emitting (not suppressed)? [Coverage, Spec §FR-011/§Edge Cases] — PASS: FR-011 explicitly covers this as a normative requirement: "The event MUST NOT be suppressed on a no-op rotation." The Edge Cases section confirms it. US3 AC2 gives a scenario. Covered at all levels.
- [x] CHK012 Is the staging precondition covered — the event emits only after `reload_credentials` has staged a new `cert_source` (vs steady state with no rotation)? [Coverage, Spec §FR-009] — PASS: FR-009 opens with "After `reload_credentials` has staged a new `cert_source`" — the staging precondition is explicit. The complementary case (no staging = no event) is implied by this conditional and confirmed by the first-load rule (now in FR-009 after SPEC-FIX to CHK003). Test T016 covers both cases (staged → event, first load → no event).

## Dependencies & Assumptions

- [x] CHK013 Is the assumption that `credentials_rotated` semantics are "locked by merged 013 FR-032" documented? [Assumption, Spec §Assumptions] — PASS: The spec Assumptions section explicitly says "`credentials_rotated` semantics are locked by merged 013 FR-032 — our own rotated `cert_source` leaf fingerprints, emitted at `drive_reconnect_attempt` before `make()`, not change-gated." 013 FR-032 is verified to exist in 013 spec.
- [x] CHK014 Is the dependency on the 013 `reload_credentials` in-process control plane (which stages the new source) explicitly cited? [Dependency, Spec §FR-009/§Dependencies] — PASS: FR-009 opens with "After `reload_credentials` has staged a new `cert_source`." The Dependencies section says "013 reconnect FSM driver, CompIdAuthorizationPolicy, TLS-outcome SessionEvent, in-process credential-reload control plane." The Normative References cite "013 `include/fixpp/session/{reconnect_fsm,session_event,compid_authorization_policy}.hpp`." Explicitly cited.

## Notes

- Check items off as completed: `[x]`; record disposition (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline for the step-9 audit.
- CHK003 (first-load-no-event in a requirement vs design-only) is the main completeness probe.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 13 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **14** |

### SPEC-FIXED items
- CHK003 — FR-009 in `spec.md` §FR-009 did not state the "first-ever credential load is NOT a rotation (no event)" behaviour normatively (it was only in design record E-3 and contract C3); added a normative sentence to FR-009 bounding the no-emit case; affected: `spec.md:§FR-009`.

### DD-DECIDED items
*(none)*

### WAIVED items
*(none)*

Anchors spot-verified:
- `[const §XI.4]` — resolves in constitution.md Article XI §4 (per-session strand default).
- `013 FR-032` — resolves in `specs/013-session-reconnect-binding/spec.md:FR-032` (verified above, full text confirmed including emit-before-make(), no-op-still-emits, leaf DER SHA-256).
