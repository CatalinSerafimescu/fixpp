# Interop Requirements Checklist: Persistent seqnum hydrate

**Purpose**: Validate that the live restart-resume interop requirement (the both-role cell vs a QFcpp/QFJ peer) is specified completely and measurably — scope, role coverage, recovery precondition, and the skip-without-counterparty guarantee — so the cell ships with real assertions, not a coincidental pass.
**Created**: 2026-06-09
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) §VII.6 · [quickstart.md](../quickstart.md) W10

## Scope & Role Coverage

- [x] CHK001 Is the interop requirement specified for BOTH roles (restart a fixpp initiator AND, separately, a fixpp acceptor mid-session vs a running QFcpp/QFJ peer), not just one? [Coverage, plan §VII.6 / W10] — PASS: plan §VII.6 states "live both-role cell: restart a fixpp initiator/acceptor mid-session vs a QFcpp/QFJ peer"; tasks.md T014 specifies "restart a fixpp initiator (and separately an acceptor) mid-session vs a running QFcpp/QFJ peer"; W10 in data-model.md states "restart a fixpp side mid-session vs QFcpp/QFJ → resumes both counters, peer-ahead recovers". Both roles are specified.
- [x] CHK002 Is the required post-restart observable outcome stated as "resumes BOTH counters from the persisted store" (inbound and outbound), so a cell that only checks outbound resume is incomplete? [Completeness, W10 / SC-001 / SC-002] — PASS: W10 in data-model.md states "resumes both counters, peer-ahead recovers"; tasks.md T014 states "assert both counters resume from the persisted store after the fixpp-side restart"; T014 also states "mirror the in-cell assertion pattern of tests/interop/happy/hp_fix44_next_expected_test.cpp". SC-001 (outbound) and SC-002 (inbound) each establish per-direction acceptance criteria, and W10 requires both. Both-counter requirement is explicit.

## Recovery Precondition (must match the unit-level narrowing)

- [x] CHK003 Is the interop recovery precondition consistent with SC-004 — i.e. the cell runs with `enable_next_expected_msg_seq_num=true` so behind-side tolerance / ResendRequest is the EXPECTED peer-ahead recovery, rather than ambiguously passing via a coincidental no-gap restart? [Consistency, SC-004 / recovery-interaction CHK005] — PASS: tasks.md T014 explicitly states "Knob state: run the cell with enable_next_expected_msg_seq_num = true on the fixpp side so behind-side tolerance / ResendRequest is the expected recovery for a peer-ahead inbound (per SC-004 / W5: the knob-off Logon gate has no ResendRequest arm and would fatal+reconnect). State this explicitly so the cell is not ambiguously passing via a coincidental no-gap restart." Consistent with SC-004 and recovery-interaction CHK005's narrowed precondition.
- [x] CHK004 Is "peer-ahead recovers via ResendRequest, no fatal too-low/too-high" stated as the measurable acceptance, with the knob-state precondition attached (not claimed unconditionally)? [Measurability, plan §VII.6 / W10] — PASS: tasks.md T014 states the acceptance criteria: "assert the peer-ahead inbound is reconciled (ResendRequest observed / no fatal), and assert the session reaches/holds Active"; the knob-state precondition is explicitly attached: "run the cell with enable_next_expected_msg_seq_num = true on the fixpp side". W10 in data-model.md states "restart a fixpp side mid-session vs QFcpp/QFJ → resumes both counters, peer-ahead recovers". The measurable outcome (ResendRequest observed, no fatal, session Active) is stated with the precondition attached.

## Counterparty-Present Assertion Floor

- [x] CHK005 Does the requirement mandate non-trivial assertions on the counterparty-PRESENT path (both counters resumed, peer-ahead reconciled, session reaches/holds Active), so the skip guard cannot make a zero-assertion cell "pass"? [Completeness, plan §VII.6 / [[feedback_fail_placeholder_red_test]]] — PASS: tasks.md T014 states "Acceptance (counterparty-PRESENT path — the skip guard must not make a zero-assertion cell 'pass'): assert both counters resume from the persisted store after the fixpp-side restart, assert the peer-ahead inbound is reconciled (ResendRequest observed / no fatal), and assert the session reaches/holds Active — mirror the in-cell assertion pattern of tests/interop/happy/hp_fix44_next_expected_test.cpp." Three specific assertions are mandated for the present-path, with the explicit prohibition on the skip guard making a zero-assertion cell pass. [[feedback_fail_placeholder_red_test]] is cited.
- [x] CHK006 Is the skip-without-counterparty behavior scoped to counterparty-ABSENT ONLY (it must never short-circuit the present-path assertions when the peer is up)? [Clarity, plan §VII.6] — PASS: tasks.md T014 states "The skip guard covers only counterparty-ABSENT; it must never short-circuit these assertions when the peer is up." The separation between absent-path (skip) and present-path (assert) is explicit in the task wording. plan §VII.6 also specifies the skip-without-counterparty pattern per the 016/018/027/028 harness pattern.

## Boundary & Harness

- [x] CHK007 Is the harness-side counterparty config (a cross-repo `phase-9-harness/` delta) distinguished from the in-repo cell (`tests/interop/happy/hp_fix44_restart_resume_test.cpp`), so the deliverable split is unambiguous? [Clarity, plan §Project Structure / tasks T002/T014] — PASS: plan §Project Structure shows both files: in-repo cell `tests/interop/happy/hp_fix44_restart_resume_test.cpp` (in-repo, fixpp source change) and `research/G19-fix-fpml-iso20022/phase-9-harness/` (cross-repo, not a fixpp source change); T002 creates the in-repo skeleton; T014 references the parent harness delta as a cross-repo config delta (explicitly not a fixpp source change). The split is unambiguous with different ownership paths.
- [x] CHK008 Is a golden-capture requirement for the live matrix stated (the restart-resume disposition recorded), or is its absence intentional? [Gap, W10 / [[project_release_interop_quickfix_fix8]]] — PASS: quickstart.md W10 states "Golden captured for the live matrix" at the end of the W10 description; the interop cell assertion pattern mirrors hp_fix44_next_expected_test.cpp (cited in T014). The golden-capture requirement is stated in quickstart.md W10, and the project topic file [[project_release_interop_quickfix_fix8]] governs the live matrix recording convention. No gap — the capture requirement is stated, albeit in quickstart rather than in the interop checklist itself. This is acceptable given the quickstart is the authoritative witness spec and W10 covers it.

## Notes

- This single-cell feature yields a focused interop checklist; the deeper persistence and seqnum-boundary requirement quality lives in `durability.md` and `recovery-interaction.md`.
- CHK003/CHK005 are the load-bearing items — they guard against the recurring interop anti-pattern of a skip-guarded cell that asserts nothing and the over-claimed knob-off recovery.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 8 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **8** |

### SPEC-FIXED items
_(none)_

### DD-DECIDED items
_(none)_

### WAIVED items
_(none)_

Anchors spot-verified: W10 (quickstart.md and data-model.md — both resolve), SC-001/SC-002 (spec.md §Success Criteria — both resolve), SC-004 (spec.md — resolves), plan §VII.6 (plan.md — resolves), tasks T002/T014 (tasks.md — both resolve), [[project_release_interop_quickfix_fix8]] (referenced for golden-capture convention — project memory topic). No dangling refs found.
