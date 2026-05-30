# Security Requirements Quality Checklist: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure

**Purpose**: Validate that the security requirements (mTLS live-identity binding, fail-CLOSED authorization, the happens-before invariant, the bounded pre-session/DoS window, and test-seam removal) are complete, clear, consistent, and measurable — BEFORE implementation. "Unit tests for the requirements," not for the code.
**Created**: 2026-05-30
**Feature**: [spec.md](../spec.md)
**Domain**: Security (Gate A trigger). Audience: reviewer / `/speckit-checklist-audit` gate (step 9).

## Requirement Completeness

- [ ] CHK001 Are requirements for the live-identity SOURCE (`handshake_result.peer_id` harvested by the accept loop's own `async_handshake`) stated for the acceptor first-connection path AND distinguished from the initiator path? [Completeness, Spec §FR-005/§FR-006]
- [ ] CHK002 Is the happens-before invariant (`live_peer_id_` set on the session strand strictly-before the first `on_inbound_frame` reaches the acceptor gate) captured as a binding *requirement*, not only a design note? [Gap, Spec §Clarifications "happens-before invariant"]
- [ ] CHK003 Are the fail-CLOSED requirements specified for BOTH the "off-list" and the "absent/delayed" identity as distinct cases (not collapsed into one)? [Completeness, Spec §FR-007/§SC-002]
- [ ] CHK004 Are requirements defining the unmatched-Logon disposition (`session_unknown_acceptor_session = 121`, NO session created, connection-level close) complete and free of any fail-open path? [Completeness, Spec §FR-005/§Edge Cases]
- [ ] CHK005 Are requirements defined for every pre-session abort path (handshake stall, silent peer, partial first frame, over-budget) ensuring transport + accept slot reclamation on each? [Completeness, Spec §FR-014/§SC-011]
- [ ] CHK006 Is the test-seam removal requirement scoped to a defined "production surface" that explicitly enumerates which artifacts (config struct, public headers, AND tests) must be free of `logon_peer_identity_override`? [Completeness, Spec §FR-009]

## Requirement Clarity

- [ ] CHK007 Is the first-frame "byte budget" quantified, or its configuration source named, rather than left as an unquantified "maximum"? [Clarity, Spec §FR-014/§SC-011]
- [ ] CHK008 Is the "handshake/Logon deadline" expressed as a measurable, configurable value with a defined owner (the accept-scope cancellation domain)? [Clarity, Spec §FR-014]
- [ ] CHK009 Are "off-list" and "absent" identity given explicit, distinguishable criteria rather than being used interchangeably? [Clarity, Spec §FR-007/§SC-002]
- [ ] CHK010 Is "the authenticated peer identity (not a test override or fabricated stand-in)" precise about what disqualifies a value as the authorization source? [Clarity, Spec §FR-006]

## Requirement Consistency

- [ ] CHK011 Are the `session_event_compid_authorization_failed` / `session_compid_unauthorized` event-and-code shapes required to be inherited *unchanged* from 013/014, with a single normative source (no second definition introduced here)? [Consistency, Spec §FR-008]
- [ ] CHK012 Does the static-only "no fail-open path" requirement (FR-005) stay consistent with the deferred optional dynamic-session-provider hook description so the two never imply contradictory routing? [Consistency, Spec §FR-005/§Edge Cases/§Assumptions]
- [ ] CHK013 Is the canonical extraction order (CN→SAN-DNS→SAN-URI→SHA-256) required to be identical on the acceptor gate to the initiator path, with no new ordering for 015? [Consistency, Spec §FR-008]

## Acceptance Criteria Quality (Measurability)

- [ ] CHK014 Can SC-002's "fails CLOSED" be objectively verified by the named code + event, INCLUDING the delayed/absent-identity ordering case (gate runs before identity arrives → must still fail CLOSED)? [Measurability, Spec §SC-002/§Clarifications]
- [ ] CHK015 Can SC-006's "no test depends on it" be objectively verified by a grep gate across `src/` + `include/` + `tests/`, with a defined zero-occurrence success condition? [Measurability, Spec §SC-006]
- [ ] CHK016 Can SC-011's "other peers are unaffected" be objectively verified (isolation criterion stated), not just asserted qualitatively? [Measurability, Spec §SC-011/§FR-014]

## Edge Case & Boundary Coverage

- [ ] CHK017 Are requirements defined for a peer that completes TLS but never sends a Logon (silent peer) as distinct from one that sends a partial/over-budget first frame? [Edge Case, Spec §SC-011]
- [ ] CHK018 Is the non-mTLS permissive path explicitly excluded from the binding logic so it cannot be read as a fail-open authorization path? [Boundary, Spec §Edge Cases/§Out of Scope]
- [ ] CHK019 Is the pre-session window's independence from per-session limits stated (it precedes any `Session` object, so it cannot rely on per-session config)? [Coverage, Spec §FR-014]

## Ambiguities & Assumptions

- [ ] CHK020 Is the optional dynamic-session-provider hook's deferral unambiguous, so no acceptance criterion (SC-001..SC-011) silently requires the dynamic path to close T-041? [Ambiguity, Spec §FR-005/§Assumptions]
- [ ] CHK021 Is the assumption "no new error slots are required by the binding logic" reconciled with the one genuinely-new lifecycle slot (`session_unknown_acceptor_session = 121`), so the boundary between binding-logic and lifecycle slots is unambiguous? [Assumption, Spec §Assumptions/§Clarifications]

## Notes

- Check items off as resolved: `[x]`. Disposition during `/speckit-checklist-audit` (step 9) as PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>.
- These items test whether the SECURITY REQUIREMENTS are well-written — not whether the implementation authorizes correctly (that is `/speckit-implement` + `/speckit-verify`).
