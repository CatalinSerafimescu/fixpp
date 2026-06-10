# Requirements-Quality Checklist: Conformance & Durability — 030 received-reset inbound advance

**Purpose**: Unit-test the *requirements writing* of the received-141 inbound-advance correction before `/speckit-implement` — completeness, clarity, consistency, measurability, coverage of the FRs/SCs (NOT implementation verification).
**Created**: 2026-06-10
**Focus**: conformance-correctness + durability/contract-amendment. Depth: standard. Audience: reviewer (pre-implement gate).
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [tasks.md](../tasks.md)

## Requirement Completeness

- [ ] CHK001 Are the corrected post-reset counter values specified for BOTH the acceptor and the initiator arms (not just the live-found acceptor case)? [Completeness, Spec §FR-001/§FR-009]
- [ ] CHK002 Is the durable-store behavior of the correction specified for both a persistent and a non-persistent store? [Completeness, Spec §FR-005/§FR-010, Edge Cases]
- [ ] CHK003 Are requirements present for the failure path (durable reset fails on a persistent store), not only the success path? [Completeness, Spec §FR-010, US1 sc3 / US2 sc2]
- [ ] CHK004 Is the guard condition (restore fires only when an in-sequence reset Logon was actually consumed) stated as a requirement, with the no-consume case explicitly addressed? [Completeness, Spec §FR-007]
- [ ] CHK005 Does the spec enumerate the full blast radius (every pre-existing test whose pinned value/behavior changes), and is each tied to a specific FR? [Completeness, Spec §Known Blast Radius / §SC-003]
- [ ] CHK006 Are the §VI catalogue / B&L / coverage-index deltas (which rows, which new B-/L- entries) specified rather than left "TBD at polish"? [Completeness, Spec §Normative References, Tasks T023-T025]

## Requirement Clarity

- [ ] CHK007 Is "persist-to-2" stated unambiguously as restoring BOTH the in-memory manager AND the durable store to the reset base + 1 (not just the in-memory counter)? [Clarity, Spec §FR-005]
- [ ] CHK008 Is the FR-003/FR-004 pair unambiguous that the reply Logon `MsgSeqNum` stays byte-identical (seq 1) while ONLY the `789` content corrects 1→2 — i.e., no flat "outbound byte-identical" claim survives? [Clarity, Spec §FR-003/§FR-004]
- [ ] CHK009 Is the scope of "fatal-when-persistent" precisely bounded to the persistent store, with the non-persistent path explicitly stated to retain logged/stay-Active? [Clarity, Spec §FR-010, Edge Cases]
- [ ] CHK010 Is the distinction between this case and the 029 over-persist class stated with a decidable criterion ("is there a surviving net-advance?"), rather than asserted? [Clarity, Spec §FR-005, Edge Cases]
- [ ] CHK011 Is the restore *placement* requirement (must land where the 789 read sees the corrected counter) stated clearly enough that an implementer cannot satisfy the counter while leaving 789 wrong? [Clarity, Spec §Discriminating Witness, Plan Summary]

## Requirement Consistency

- [ ] CHK012 Is the pin count internally consistent across spec / plan / tasks (7 = 6 value-pins + 1 contract-witness split), with no residual "5 pins"/"6 pins" current-state claim? [Consistency, Spec §SC-003 / Plan Project-Structure / Tasks Phase 6]
- [ ] CHK013 Do the spec, plan, and research agree on the fix shape (persist-to-2, both arms, fatal-when-persistent) with no surviving manager-only / unconditional-INV-H1 wording? [Consistency, Spec §FR-005]
- [ ] CHK014 Is terminology consistent (e.g., `store_is_persistent_`, `peer_ack_sent_reset_flag`, "received-141", "reset base + 1") across all artifacts? [Consistency]
- [ ] CHK015 Does the 024 I-07 contract-amendment description agree between the spec (Edge Cases / §FR-010), the blast-radius pin #5 (witness split), and SC-003? [Consistency, Spec §FR-010 / §SC-003]

## Acceptance-Criteria Quality (Measurability)

- [ ] CHK016 Is the acceptor discriminating witness a falsifiable triple (next_inbound==2 AND reply.MsgSeqNum==1 AND reply.789==2) such that no weaker proxy passes? [Measurability, Spec §Discriminating Witness]
- [ ] CHK017 Is the initiator witness objectively stated (next_inbound==2 + harm-repro, no 789 clause) and is the *reason* the 789 clause is excluded documented? [Measurability, Spec §FR-009 / §Discriminating Witness]
- [ ] CHK018 Is the INV-H1 acceptance criterion expressed as a direct assertion on the durable store (`store.durable_inbound == 2`, `store == manager`), not a manager proxy? [Measurability, Spec §Discriminating Witness — the 029 W9b proxy-gap lesson]
- [ ] CHK019 Is the FR-010 fault-injection witness specified so it actually falsifies the soundness claim (persistent reset-fail → Disconnect AND persist-to-2 not reached AND no `store > manager` observable)? [Measurability, Spec §Discriminating Witness / §SC-002]
- [ ] CHK020 Is SC-001 (the live close-out) measurable (session reaches Active, zero ResendRequest, seq-2 accepted, vs BOTH QFcpp and QFJ)? [Measurability, Spec §SC-001]

## Scenario & Edge-Case Coverage

- [ ] CHK021 Are requirements defined across the full reset-policy matrix (bilateral-strict / bilateral-lenient / unilateral) on BOTH roles? [Coverage, Spec §FR-008, Edge Cases]
- [ ] CHK022 Is the `reset_on_logon=true` knob path explicitly required to be unchanged (a non-regression requirement with a named witness), not merely implied? [Coverage, Spec §FR-006 / §SC-004]
- [ ] CHK023 Is the no-consumed-reset-Logon path (guard false) covered by a requirement and a witness, not only the happy path? [Coverage, Spec §FR-007, Edge Cases]
- [ ] CHK024 Is the non-persistent store covered as its own scenario (write-through no-op, no disconnect-on-reset, stay-Active retained)? [Coverage, Spec §FR-010, Edge Cases]
- [ ] CHK025 Is the restart-after-received-141 hazard (the reason manager-only is rejected) captured as the rationale for persist-to-2, so the requirement cannot regress to a half-fix? [Coverage, Spec §FR-005, Edge Cases]

## Dependencies & Assumptions

- [ ] CHK026 Is the conformance oracle (QuickFIX-cpp/J reset-then-increment → net 2; 789=nextTarget+1) documented and grounded in source, not asserted? [Assumption, Spec §Clarifications / §Assumptions]
- [ ] CHK027 Is the assumption "a `141=Y` Logon carries `MsgSeqNum=1`" validated (reference-engine-confirmed) rather than presumed? [Assumption, Spec §Assumptions / §Clarifications]
- [ ] CHK028 Are the dependencies on the 029 persistence spine (INV-H1, `next_seqnum(inbound,increment)`, `store_is_persistent_`) and the 013/024 received-141 machinery documented as in-scope context? [Dependency, Spec §Assumptions]
- [ ] CHK029 Is it stated that NO new config knob / wire field / store interface is introduced (so the fix stays within the existing surface)? [Assumption, Plan Constitution-Check §X/§XIV]

## Ambiguities & Conflicts

- [ ] CHK030 Does any requirement still conflate "outbound reply = seq 1" with "inbound next-expected = 1" (the original root-cause conflation)? [Conflict, Spec §Context / §FR-003]
- [ ] CHK031 Is there any unresolved tension between the 024 I-07 stay-Active contract and the new fatal-when-persistent requirement, or is the amendment explicitly scoped (persistent only) to resolve it? [Conflict, Spec §FR-010]
- [ ] CHK032 Are there any remaining placeholders, "to-be-confirmed" assumptions, or stale cites (e.g., `§4.6` vs `§4.4.2`, `test_next_expected.cpp` vs `_msgseqnum.cpp`, `:1947` vs `:1942`) in the requirements? [Ambiguity, Spec §Normative References / Tasks T002]

## Notes

- This checklist tests the *requirements*, not the code; it is the `/speckit-checklist` artifact that `/speckit-checklist-audit` (mandatory, pipeline step 9) dispositions before `/speckit-implement` is unblocked.
- The companion `requirements.md` is the `/specify` spec-quality checklist; this `conformance.md` adds the domain-specific (session-seqnum-conformance + durability) dimension.
