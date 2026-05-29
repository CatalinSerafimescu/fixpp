# Security & Identity-Binding Requirements Checklist: Live Transport Wiring (014-transport-active-binding)

**Purpose**: Validate that the requirements for the live handshake-identity → CompID authorization binding (FIXS §4.4 / T-041), the mTLS fail-closed semantics, and the signature-algorithm allow-list are complete, unambiguous, consistent, and measurable — BEFORE implementation. This tests the requirements writing, not the code.
**Created**: 2026-05-29
**Feature**: [spec.md](../spec.md) · Gate-A trigger: Security

## Requirement Completeness

- [x] CHK001 Is the identity **source** precedence on the live initiator path (live `handshake_result.peer_id` ahead of the retained `logon_peer_identity_override` seam ahead of the inherited 013 arms) stated in a requirement, or only in design records E-2/C2? [Completeness, Gap, Spec §FR-006/§FR-008] — SPEC-FIXED: FR-006 stated "the live identity MUST be supplied to authorize()" but did not enumerate the three-arm precedence order explicitly as a normative requirement. Added an explicit precedence sentence to FR-006 in spec.md: "(1) live `handshake_result.peer_id` on the initiator path when available; (2) `logon_peer_identity_override` seam; (3) inherited 013 arms." The design records E-2/C2 are now cross-referenced; the normative statement is in the FR.
- [x] CHK002 Are the fail-closed triggers enumerated completely — both *absent* and *off-allow-list* authenticated identity under a binding policy? [Completeness, Spec §FR-007] — PASS: FR-007 explicitly states "an absent or off-allow-list authenticated identity MUST fail closed"; the Edge Cases section further confirms "No identity under a binding policy: no authenticated identity → fail closed (the all-empty principal is unauthorized unless deliberately bound), per 013 FR-019." Both triggers covered.
- [x] CHK003 Is the requirement that NO fabricated/stand-in identity remains on the live initiator path specified separately from the requirement that the seam is *retained for tests*? [Completeness, Spec §FR-006/§FR-008] — PASS: FR-006 states "there must be no fabricated/stand-in identity on the live path"; FR-008 separately states "The residual fabricated auth payload from 013 MUST be removed from the live path. (The test-only `logon_peer_identity_override` may remain…)". The two requirements are distinct in adjacent FRs.
- [x] CHK004 Is the `sigalg_disallowed` rejection requirement specified with its allow-list authority (`[const §XII.3]`) and a fixture class (Ed25519/Ed448/unknown-`EVP_PKEY`)? [Completeness, Spec §FR-012] — PASS: FR-012 says "the `sigalg_disallowed` `sub_reason` cell, exercised with an Ed25519/Ed448 (or unknown-`EVP_PKEY`) certificate fixture, bringing the 013 US3 `sub_reason` coverage to its full set." The allow-list authority `[const §XII.3]` is cited in the Normative References section and in plan §Constraints. Allow-list authority is `[const §XII.3]` confirmed to resolve (constitution Art. XII §3 defines the signature-algorithm allow-list: ECDSA P-256/P-384 and RSA-PSS ≥2048; Ed25519/Ed448 are outside it).

## Requirement Clarity

- [x] CHK005 Is "binding policy" defined unambiguously (what distinguishes a binding allow-list policy from a genuinely permissive one)? [Clarity, Ambiguity, Spec §FR-007/§Edge Cases] — PASS: "binding policy" is defined by the inherited semantics from 013 (CompIdAuthorizationPolicy with at least one entry = binding; empty policy = permissive). FR-007 says "under a binding policy, an absent or off-allow-list authenticated identity MUST fail closed"; the Permissive edge case says "behaviour inherited unchanged from 013." The concept is well-defined by reference to the shipped CompIdAuthorizationPolicy semantics. No ambiguity gap.
- [x] CHK006 Is the "no fail-OPEN hole on the mTLS path" claim stated precisely enough that a reviewer can distinguish "making the already-fail-CLOSED gate operable" from "introducing fail-CLOSED from scratch"? [Clarity, Spec §Overview/§FR-007] — PASS: The Overview (third paragraph), US2 "Why this priority," FR-007, and the plan §Constraints all explicitly state: "013's mTLS Logon gate already fails CLOSED... 014 introduces NO fail-CLOSED from scratch and there is no fail-OPEN hole on the mTLS path — it swaps the identity *source* (test-seam/stub → live peer_id) so the gate becomes *operable* with a real identity." The distinction is clear and repeated in multiple places.
- [x] CHK007 Is the partial-closure boundary of **T-041** (what advances on the initiator live path vs what defers to 015) stated unambiguously? [Clarity, Spec §Overview/§Assumptions] — PASS: The spec Assumptions section explicitly states "T-041 advances but does not fully close in 014 — 014 wires the live identity into the decision and proves it on the live initiator path only; acceptor binding-logic is proven via the existing test seam. Full production closure for the acceptor role (live acceptor path + test-seam removal) lands with the 015 engine. (014/015 binding boundary resolved — Clarifications, Session 2026-05-29.)" The Clarifications section records Q2 resolution verbatim.

## Requirement Consistency

- [x] CHK008 Are the authorization-failure event/code shapes (`session_compid_unauthorized` + `compid_authorization_failed`) required to be identical between the inherited 013 open-Logon path and the 014 reconnect path? [Consistency, Spec §FR-007] — PASS: FR-007 states "The fail-closed/permissive semantics, canonical extraction order, and event/code *shapes* are inherited unchanged from 013 (FR-019/FR-020/FR-022)." It then explicitly notes the *FSM disposition* differs (retry-to-cap on reconnect vs terminal Disconnected on open path), while keeping the event/code shapes constant. Consistent.
- [x] CHK009 Is the "014 changes only the identity *source*, not the semantics/extraction-order/shapes" invariant stated consistently across Overview, US2, and FR-007? [Consistency, Spec §Overview/§FR-006..007] — PASS: All three locations are consistent. Overview says "014 changes only the identity *source*"; US2 AC3 says "those semantics are inherited unchanged; 014 changes only the *source* of the identity (stub → live handshake)"; FR-007 says "014 changes only the identity **source** (and the reconnect-path disposition above)." No contradictions.

## Acceptance Criteria Quality / Measurability

- [x] CHK010 Can SC-003 ("no test-seam/fabricated identity source remains on the live initiator path") be objectively verified given the seam is deliberately retained for binding-logic tests? Is the live-vs-test-path distinction measurable? [Measurability, Spec §SC-003] — PASS: SC-003 specifies both "no test-seam/fabricated identity source remains on the live initiator path" AND "verified via the binding-logic test seam." The live path test (T011 `test_live_identity_binding.cpp`) asserts the real `handshake_result.peer_id` reaches `authorize()` with no fabricated payload — this is measurable by inspecting the identity passed. The seam test (T012 `test_compid_binding_seam.cpp`) uses `logon_peer_identity_override` explicitly, which is a different test. The distinction is path-structural (live loopback fixture = live path; override present = seam path) and independently testable.
- [x] CHK011 Is the admit-on-list / fail-close-off-list-or-absent outcome expressed as an observable acceptance criterion (state not Active + event emitted), not just prose? [Measurability, Spec §FR-007/§SC-003] — PASS: FR-007 specifies "refuse Active and emit `session_compid_unauthorized` + `session_event_compid_authorization_failed`." SC-003 says "under a binding policy, the on-list identity is admitted and off-list/absent identities fail closed." US2 AC2 specifies: "the session does not reach Active and a `compid_authorization_failed` event with `session_compid_unauthorized` is emitted." Observable state (not Active) + event emitted are both specified.

## Scenario & Edge-Case Coverage

- [x] CHK012 Are requirements defined for the "TCP/handshake up, identity unauthorized" edge (transport-layer success, off-list identity under a binding policy)? [Coverage, Spec §Edge Cases] — PASS: The Edge Cases section explicitly covers "TCP up, identity unauthorized: handshake completes at the transport layer but the identity is off-list under a binding policy → fail closed; the failed attempt counts as one reconnect attempt and is retried to the cap reason-agnostically (FR-003)." FR-007 also covers it normatively.
- [x] CHK013 Are requirements defined for the all-empty / absent principal under a binding policy (unauthorized unless deliberately bound)? [Coverage, Spec §Edge Cases] — PASS: Edge Cases says "No identity under a binding policy: no authenticated identity → fail closed (the all-empty principal is unauthorized unless deliberately bound), per 013 FR-019." FR-007 covers it via "absent…authenticated identity MUST fail closed."
- [x] CHK014 Is the non-mTLS permissive skip (guard arm 3) explicitly bounded as in-scope-but-out-of-change (genuinely permissive, not a hole to close)? [Coverage, Spec §FR-007/US2 Why] — PASS: FR-007 states the semantics are "inherited unchanged from 013 (FR-019/FR-020/FR-022)" and the non-mTLS skip "is genuinely permissive and out of scope." US2 "Why this priority" confirms "the non-mTLS skip (branch 3) is genuinely permissive and out of scope." Edge Cases: "Permissive (no binding policy): behaviour inherited unchanged from 013."

## Dependencies & Assumptions

- [x] CHK015 Is the assumption that acceptor-side live binding stays via the test seam (deferred to 015, T-041 stays `implementing`) documented and bounded as a *clarified* asymmetry, not a silent half-fix? [Assumption, Spec §Assumptions/§FR-008] — PASS: Spec Assumptions §"T-041 advances but does not fully close in 014" states this explicitly. FR-008 documents "The test-only `logon_peer_identity_override` may remain as the binding-logic test seam until the 015 engine removes the dependency." Clarifications Q2 is recorded in spec §Clarifications. Plan §Constraints cites `[[feedback_half_restructure_symmetric_api]]` as the explicit documentation that this is a clarified, documented asymmetry, not a half-fix defect.
- [x] CHK016 Is the dependency on 013's inherited authorization semantics (FR-019/020/022: fail-closed/permissive, canonical CN→SAN-DNS→SAN-URI→SHA-256 extraction) explicitly cited? [Dependency, Spec §FR-007/§Dependencies] — PASS: FR-007 cites "FR-019/FR-020/FR-022" by name. The Dependencies section lists "013 reconnect FSM driver, CompIdAuthorizationPolicy, TLS-outcome SessionEvent, in-process credential-reload control plane." The Normative References section includes the 013 contract surfaces. All three FRs are verified to exist in 013 spec.

## Notes

- Check items off as completed: `[x]`; record disposition (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline for the step-9 audit.
- This checklist validates requirement *quality*; behavioural verification lives in the test plan (tasks T011/T012, FR-012 cell T020).

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 15 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **16** |

### SPEC-FIXED items
- CHK001 — FR-006 in `spec.md` §FR-006 lacked a normative statement of the three-arm identity-source precedence order on the initiator live path; added an explicit precedence sentence to FR-006 (design records E-2/C2 are cross-referenced); affected: `spec.md:§FR-006`.

### DD-DECIDED items
*(none)*

### WAIVED items
*(none)*

Anchors spot-verified:
- `[const §XII.3]` — resolves in constitution.md Article XII §3 (signature-algorithm allow-list: ECDSA P-256/P-384, RSA-PSS ≥2048; Ed25519/Ed448 outside the list).
- `[FIXS §4.4]` — cited as external FIX/S standard (not an internal anchor); not spot-verified in library (no local copy; binding via spec Normative References).
- `[FIX-SL §4.2.2]` — same as above; external standard.
- `013 FR-019/020/022` — all three resolve in `specs/013-session-reconnect-binding/spec.md` (verified above).
