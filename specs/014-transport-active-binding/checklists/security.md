# Security & Identity-Binding Requirements Checklist: Live Transport Wiring (014-transport-active-binding)

**Purpose**: Validate that the requirements for the live handshake-identity → CompID authorization binding (FIXS §4.4 / T-041), the mTLS fail-closed semantics, and the signature-algorithm allow-list are complete, unambiguous, consistent, and measurable — BEFORE implementation. This tests the requirements writing, not the code.
**Created**: 2026-05-29
**Feature**: [spec.md](../spec.md) · Gate-A trigger: Security

## Requirement Completeness

- [ ] CHK001 Is the identity **source** precedence on the live initiator path (live `handshake_result.peer_id` ahead of the retained `logon_peer_identity_override` seam ahead of the inherited 013 arms) stated in a requirement, or only in design records E-2/C2? [Completeness, Gap, Spec §FR-006/§FR-008]
- [ ] CHK002 Are the fail-closed triggers enumerated completely — both *absent* and *off-allow-list* authenticated identity under a binding policy? [Completeness, Spec §FR-007]
- [ ] CHK003 Is the requirement that NO fabricated/stand-in identity remains on the live initiator path specified separately from the requirement that the seam is *retained for tests*? [Completeness, Spec §FR-006/§FR-008]
- [ ] CHK004 Is the `sigalg_disallowed` rejection requirement specified with its allow-list authority (`[const §XII.3]`) and a fixture class (Ed25519/Ed448/unknown-`EVP_PKEY`)? [Completeness, Spec §FR-012]

## Requirement Clarity

- [ ] CHK005 Is "binding policy" defined unambiguously (what distinguishes a binding allow-list policy from a genuinely permissive one)? [Clarity, Ambiguity, Spec §FR-007/§Edge Cases]
- [ ] CHK006 Is the "no fail-OPEN hole on the mTLS path" claim stated precisely enough that a reviewer can distinguish "making the already-fail-CLOSED gate operable" from "introducing fail-CLOSED from scratch"? [Clarity, Spec §Overview/§FR-007]
- [ ] CHK007 Is the partial-closure boundary of **T-041** (what advances on the initiator live path vs what defers to 015) stated unambiguously? [Clarity, Spec §Overview/§Assumptions]

## Requirement Consistency

- [ ] CHK008 Are the authorization-failure event/code shapes (`session_compid_unauthorized` + `compid_authorization_failed`) required to be identical between the inherited 013 open-Logon path and the 014 reconnect path? [Consistency, Spec §FR-007]
- [ ] CHK009 Is the "014 changes only the identity *source*, not the semantics/extraction-order/shapes" invariant stated consistently across Overview, US2, and FR-007? [Consistency, Spec §Overview/§FR-006..007]

## Acceptance Criteria Quality / Measurability

- [ ] CHK010 Can SC-003 ("no test-seam/fabricated identity source remains on the live initiator path") be objectively verified given the seam is deliberately retained for binding-logic tests? Is the live-vs-test-path distinction measurable? [Measurability, Spec §SC-003]
- [ ] CHK011 Is the admit-on-list / fail-close-off-list-or-absent outcome expressed as an observable acceptance criterion (state not Active + event emitted), not just prose? [Measurability, Spec §FR-007/§SC-003]

## Scenario & Edge-Case Coverage

- [ ] CHK012 Are requirements defined for the "TCP/handshake up, identity unauthorized" edge (transport-layer success, off-list identity under a binding policy)? [Coverage, Spec §Edge Cases]
- [ ] CHK013 Are requirements defined for the all-empty / absent principal under a binding policy (unauthorized unless deliberately bound)? [Coverage, Spec §Edge Cases]
- [ ] CHK014 Is the non-mTLS permissive skip (guard arm 3) explicitly bounded as in-scope-but-out-of-change (genuinely permissive, not a hole to close)? [Coverage, Spec §FR-007/US2 Why]

## Dependencies & Assumptions

- [ ] CHK015 Is the assumption that acceptor-side live binding stays via the test seam (deferred to 015, T-041 stays `implementing`) documented and bounded as a *clarified* asymmetry, not a silent half-fix? [Assumption, Spec §Assumptions/§FR-008]
- [ ] CHK016 Is the dependency on 013's inherited authorization semantics (FR-019/020/022: fail-closed/permissive, canonical CN→SAN-DNS→SAN-URI→SHA-256 extraction) explicitly cited? [Dependency, Spec §FR-007/§Dependencies]

## Notes

- Check items off as completed: `[x]`; record disposition (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline for the step-9 audit.
- This checklist validates requirement *quality*; behavioural verification lives in the test plan (tasks T011/T012, FR-012 cell T020).
