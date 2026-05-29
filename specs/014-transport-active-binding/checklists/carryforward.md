# Error-Taxonomy & Carry-Forward Requirements Checklist: Live Transport Wiring (014-transport-active-binding)

**Purpose**: Validate that the requirements for the error-slot append / slot-74 cleanup (FR-016) and the five 013/012 Gate-B carry-forward witnesses (FR-012..016) are complete, unambiguous, consistent, and measurable BEFORE implementation. Tests the requirements, not the witnesses.
**Created**: 2026-05-29
**Feature**: [spec.md](../spec.md) · Gate-A trigger: Error semantics

## Requirement Completeness

- [ ] CHK001 Are all five carry-forward obligations enumerated with their source waiver (013 item 2/3, 012 RC#C/RC#G/RC#I, 013 slot-74)? [Completeness, Spec §FR-012..016]
- [ ] CHK002 Is the slot-74 cleanup decomposed into all its sub-deliverables — new code, the seqnum-manager comment, the return value, the test assertion, and the contract note? [Completeness, Spec §FR-016]
- [ ] CHK003 Are the fixture requirements specified for FR-012 (Ed25519/Ed448 / unknown-`EVP_PKEY` leaf) and FR-014 (multi-SAN leaf) with WHAT path each must exercise? [Completeness, Spec §FR-012/§FR-014]

## Requirement Clarity

- [ ] CHK004 Is the new error slot specified with an exact value and boundary (`session_seqnum_too_high = 120`, after `session_invalid_argument = 119`) and the append-only / no-renumber constraint? [Clarity, Spec §FR-016/§Assumptions]
- [ ] CHK005 Is "slot 74 keeps its real meaning; only the too-high MISUSE is removed" stated unambiguously (vs retiring slot 74 entirely)? [Clarity, Spec §FR-016]
- [ ] CHK006 Is FR-015 scoped clearly as a catalogue/scope **re-label only** — no harness body change, no new parser-touching code? [Clarity, Spec §FR-015]
- [ ] CHK007 Is FR-013b's handshake bench stated as establishing a NEW baseline (not a ±5% regression gate against a prior number this PR)? [Clarity, Spec §FR-013]

## Requirement Consistency

- [ ] CHK008 Is the error-slot envelope consistent across spec, `contracts/error_slots.hpp`, and plan (012 → 94..115, 013 → 116..119, next free = 120; slot 70 permanent hole; slot 74 meaning preserved)? [Consistency, Spec §Assumptions/§FR-016]
- [ ] CHK009 Is FR-016's "zero behavioural change" claim consistent with the stated fact that all 3 `check_inbound` callers discard the returned code? [Consistency, Spec §FR-016/§Edge Cases]

## Acceptance Criteria Quality / Measurability

- [ ] CHK010 Can SC-006 be objectively verified per-item — each of the five witnesses passes AND the fuzz-scope entry is re-labelled? [Measurability, Spec §SC-006]
- [ ] CHK011 Is FR-014's requirement to exercise the MID and TAIL allocation sites (not only the boundary) expressed measurably (multi-SAN forces the deeper sites)? [Measurability, Spec §FR-014]
- [ ] CHK012 Is FR-013a's once-per-handshake invariant (`load_credentials()` == 1) expressed as an observable counter assertion? [Measurability, Spec §FR-013]

## Scenario & Edge-Case Coverage

- [ ] CHK013 Is the seqnum-too-high branch's reachability scoped (LogonSent/LogonReceived handshake states; Active intercepted earlier) so the FR-016 witness targets the right path? [Coverage, Spec §Edge Cases/§FR-016]
- [ ] CHK014 Are the carry-forward witnesses' previously-blocked status documented (each was waived precisely because it needed the live TLS handshake this feature provides)? [Coverage, Spec §FR-013/US4]

## Dependencies & Assumptions

- [ ] CHK015 Is the dependency of FR-013a/b on the live loopback fixture (vs the prior infeasibility under `mock_transport`) documented? [Dependency, Spec §FR-013/US4 Independent Test]
- [ ] CHK016 Is the assumption that the new slot triggers no abidiff (C++ enum value, not a `fixpp_error_t` C-ABI symbol) documented? [Assumption, Spec §FR-016/plan §Target Platform]

## Notes

- Check items off as completed: `[x]`; record disposition (PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) inline for the step-9 audit.
- FR-015 is doc-only; ensure the audit does not demand a code witness for it (CHK006).
