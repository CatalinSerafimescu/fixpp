# Interop Requirements Checklist: Persistent seqnum hydrate

**Purpose**: Validate that the live restart-resume interop requirement (the both-role cell vs a QFcpp/QFJ peer) is specified completely and measurably — scope, role coverage, recovery precondition, and the skip-without-counterparty guarantee — so the cell ships with real assertions, not a coincidental pass.
**Created**: 2026-06-09
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) §VII.6 · [quickstart.md](../quickstart.md) W10

## Scope & Role Coverage

- [ ] CHK001 Is the interop requirement specified for BOTH roles (restart a fixpp initiator AND, separately, a fixpp acceptor mid-session vs a running QFcpp/QFJ peer), not just one? [Coverage, plan §VII.6 / W10]
- [ ] CHK002 Is the required post-restart observable outcome stated as "resumes BOTH counters from the persisted store" (inbound and outbound), so a cell that only checks outbound resume is incomplete? [Completeness, W10 / SC-001 / SC-002]

## Recovery Precondition (must match the unit-level narrowing)

- [ ] CHK003 Is the interop recovery precondition consistent with SC-004 — i.e. the cell runs with `enable_next_expected_msg_seq_num=true` so behind-side tolerance / ResendRequest is the EXPECTED peer-ahead recovery, rather than ambiguously passing via a coincidental no-gap restart? [Consistency, SC-004 / recovery-interaction CHK005]
- [ ] CHK004 Is "peer-ahead recovers via ResendRequest, no fatal too-low/too-high" stated as the measurable acceptance, with the knob-state precondition attached (not claimed unconditionally)? [Measurability, plan §VII.6 / W10]

## Counterparty-Present Assertion Floor

- [ ] CHK005 Does the requirement mandate non-trivial assertions on the counterparty-PRESENT path (both counters resumed, peer-ahead reconciled, session reaches/holds Active), so the skip guard cannot make a zero-assertion cell "pass"? [Completeness, plan §VII.6 / [[feedback_fail_placeholder_red_test]]]
- [ ] CHK006 Is the skip-without-counterparty behavior scoped to counterparty-ABSENT ONLY (it must never short-circuit the present-path assertions when the peer is up)? [Clarity, plan §VII.6]

## Boundary & Harness

- [ ] CHK007 Is the harness-side counterparty config (a cross-repo `phase-9-harness/` delta) distinguished from the in-repo cell (`tests/interop/happy/hp_fix44_restart_resume_test.cpp`), so the deliverable split is unambiguous? [Clarity, plan §Project Structure / tasks T002/T014]
- [ ] CHK008 Is a golden-capture requirement for the live matrix stated (the restart-resume disposition recorded), or is its absence intentional? [Gap, W10 / [[project_release_interop_quickfix_fix8]]]

## Notes

- This single-cell feature yields a focused interop checklist; the deeper persistence and seqnum-boundary requirement quality lives in `durability.md` and `recovery-interaction.md`.
- CHK003/CHK005 are the load-bearing items — they guard against the recurring interop anti-pattern of a skip-guarded cell that asserts nothing and the over-claimed knob-off recovery.
