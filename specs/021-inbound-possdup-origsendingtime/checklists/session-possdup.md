# Session PossDup / OrigSendingTime — Requirements Quality Checklist

**Purpose**: Unit-tests-for-English over the inbound PossDup/OrigSendingTime requirements — validate completeness, clarity, consistency, measurability, and coverage of the spec/plan/contracts BEFORE implementation. Not implementation tests.
**Created**: 2026-06-04
**Feature**: [spec.md](../spec.md) (session-protocol + interop domain)

## Requirement Completeness

- [ ] CHK001 Are requirements defined for every inbound PossDup disposition arm (admin-ignore, app-drop, app-redeliver, Arm-B fatal, Arm-C required-tag, Arm-D accuracy, Arm-E reset-exempt)? [Completeness, Spec §FR-001..FR-006/FR-010, data-model §1]
- [ ] CHK002 Is the admin-vs-application message distinction defined for the tolerated arm (admin always ignored; app governed by the knob)? [Completeness, Spec §FR-010]
- [ ] CHK003 Is the expected-inbound-sequence-number behavior specified for EVERY surviving disposition (tolerated arms AND the Arm-C reject), not just the disconnect arms? [Completeness, Spec §FR-002/FR-004, contract §C1]
- [ ] CHK004 Are requirements stated for the `OrigSendingTime(122)` validation scope (which message types/seqnum positions it applies to)? [Completeness, Spec §FR-004/FR-005]
- [ ] CHK005 Is the `redeliver_poss_dup` configuration knob's default and both settings' behavior fully specified? [Completeness, Spec §FR-010, contract §C2]
- [ ] CHK006 Does the spec state what observable surface (or intentional silence) each disposition produces? [Completeness, Spec §FR-009]

## Requirement Clarity

- [ ] CHK007 Are the exact `Reject(35=3)` field tags specified unambiguously (`371=RefTagID`, `373=SessionRejectReason`) with the concrete reason values (`1` RequiredTagMissing, `10` SendingTimeAccuracyProblem)? [Clarity, Spec §FR-004/FR-005, contract §C1]
- [ ] CHK008 Is the `OrigSendingTime > SendingTime` comparison defined as strict (`>`), with the equality (`122 == 52`) case explicitly stated as accepted? [Clarity, Spec §Edge Cases, §FR-005]
- [ ] CHK009 Is "possible duplicate" defined by an objective condition (`MsgSeqNum < expected` AND `PossDupFlag(43)=Y`) rather than a vague description? [Clarity, Spec §FR-001]
- [ ] CHK010 Is the Arm-B behavior stated precisely as `→Disconnected` with **no Logout wire frame** (not the looser "Logout + disconnect")? [Clarity, Spec §FR-003, contract §C3]
- [ ] CHK011 Is "flagged as a possible duplicate" for the redeliver path defined in terms of an observable (the raw `43`/`122` fields visible to `fromApp`) rather than an unspecified flag parameter? [Ambiguity, Spec §FR-010]

## Requirement Consistency

- [ ] CHK012 Do the reject field tags agree across spec, contract, data-model, and quickstart (no surviving `380` or `373=122`)? [Consistency, Spec §FR-004/005, contract §C1, data-model §1]
- [ ] CHK013 Is the two-stage ordering (validation Arms C/D before too-low tolerance Arms A/B) consistent between data-model §1, the contract, and the FR text? [Consistency, data-model §1, Spec §Clarifications]
- [ ] CHK014 Is the at-expected validation behavior (validation fires at `34==N`) consistent between FR-004/005, the contract table, and US2 acceptance scenarios AS4/AS5? [Consistency, Spec §FR-004/005, §US2]
- [ ] CHK015 Is the no-seqnum-advance rule consistent between the too-low tolerated arms (INV-1) and the Arm-C reject (newly added)? [Consistency, Spec §FR-002/FR-004, data-model INV-1]

## Acceptance Criteria Quality & Measurability

- [ ] CHK016 Are the success criteria measurable without implementation detail (e.g., "session remains established in 100% of replay scenarios")? [Measurability, Spec §SC-001..SC-004]
- [ ] CHK017 Is SC-002's "matching QuickFIX-cpp/QuickFIX-J disposition" anchored to a verifiable reference (named engine versions + the specific reject)? [Measurability, Spec §SC-002]
- [ ] CHK018 Is SC-003's "zero regression" criterion tied to an objectively checkable set (the existing too-low tests + the Arm-B pin)? [Measurability, Spec §SC-003]
- [ ] CHK019 Does each US acceptance scenario state a Given/When/Then that can be objectively evaluated? [Acceptance Criteria, Spec §US1/US2]

## Scenario & Edge-Case Coverage

- [ ] CHK020 Are requirements defined for the `SequenceReset(35=4)` + PossDup exemption (Arm E does not reject)? [Coverage, Spec §FR-006, §US2 AS3]
- [ ] CHK021 Are requirements defined for a too-low `43=Y` **Heartbeat(0)** (handled by the existing silent-ignore exception, not Arm A)? [Edge Case, tasks §T005, Spec §Edge Cases]
- [ ] CHK022 Is the `MsgSeqNum > expected` + `43=Y` (possdup during an open resend window) case addressed (deferred to the existing too-high path)? [Coverage, Spec §Edge Cases]
- [ ] CHK023 Is role-symmetry (initiator AND acceptor) stated as a requirement, not assumed? [Coverage, Spec §FR-007, §Edge Cases]
- [ ] CHK024 Is the idempotent-ignore requirement for an already-applied admin duplicate specified (no double side-effect)? [Coverage, Spec §US1 AS2/FR-002]
- [ ] CHK025 Are the at-expected `43=Y` malformed cases (missing `122`, late `122`) covered as explicit acceptance scenarios? [Coverage, Spec §US2 AS4/AS5]

## Interop Requirements Quality

- [ ] CHK026 Do the interop requirements specify both counterparty engines AND both roles, with the skip-without-counterparty contract? [Completeness, Spec §FR-007/SC-004, quickstart §2]
- [ ] CHK027 Is the QuickFIX-J `RequiresOrigSendingTime` configuration dependency documented so the malformed-dup cell is reproducible? [Dependency, research §D4, tasks §T009]
- [ ] CHK028 Are the live-cell non-deterministic-field normalization expectations (timestamps, seqnums) specified for golden comparison? [Clarity, quickstart §2, Gap]

## Dependencies & Assumptions

- [ ] CHK029 Is the `PossResend(97)` out-of-scope boundary stated explicitly so S-010 is not mis-closed? [Assumption, Spec §Assumptions, plan §VI delta]
- [ ] CHK030 Is the FR-008 / AllowPossDup send-knob deferral documented with rationale and no in-scope reference? [Assumption, Spec §FR-008/§Deferred Follow-up]
- [ ] CHK031 Is the "no new persistent dedup store / no new error slot / reuse existing machinery" assumption stated and bounded? [Assumption, Spec §Assumptions, plan Constitution Check §X]
- [ ] CHK032 Is the three-divergence reference-engine grounding (app-redeliver, at-expected validation, AllowPossDup default) recorded as the basis for fixpp's choices? [Traceability, research §D2/D2b/D7, Spec §Assumptions]

## Ambiguities & Conflicts

- [ ] CHK033 Does any requirement still imply Arm B emits a Logout, conflicting with FR-003's no-Logout statement? [Conflict, Spec §FR-003]
- [ ] CHK034 Is there any residual claim that the engines are "identical" on Arm-D's RefTagID (they differ; fixpp chooses `371=122`)? [Conflict, research §D5, plan Summary]
- [ ] CHK035 Are all `[FIX-SL §X]` / `[const §X]` references exact (not vague), per `[const §VI.5]`? [Traceability, Spec §Normative References]
