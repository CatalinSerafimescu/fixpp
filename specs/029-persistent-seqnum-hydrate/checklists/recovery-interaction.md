# Recovery-Interaction Requirements Checklist: Persistent seqnum hydrate

**Purpose**: Validate that the requirements governing how the resumed seqnum state interacts with the Logon-path seqnum gate, the received-reset (`141=Y`) / `reset_on_logon` arms, the `789` next-expected advertisement, and the post-GapFill recovery precondition are complete, clear, and non-contradictory. This is the RC-1 surface the round-1 design could not satisfy.
**Created**: 2026-06-09
**Feature**: [spec.md](../spec.md) · [contracts/seqnum-hydrate.md](../contracts/seqnum-hydrate.md) · [research.md](../research.md)

## Logon-Gate vs Steady-State Gate (the RC-1 model)

- [ ] CHK001 Does the spec distinguish the **Logon-path** seqnum gate (acceptor `NotConnected` `:1596` / initiator `LogonSent` `:2841`, which fatals on too-high with NO ResendRequest arm unless 789) from the steady-state Active gate (which enters AwaitingResend)? Is this distinction stated as a requirement constraint, not buried in rationale? [Clarity, FR-009 / research §RC-1 / spec Edge Cases]
- [ ] CHK002 Is the requirement that a resumed (hydrated) `next_inbound` MUST NOT pre-empt a peer reset Logon into a too-low fatal at `:1615` stated explicitly, with the failing-without-it scenario named? [Completeness, FR-010 / INV-H5 / W9b]
- [ ] CHK003 Is the inbound-seed **withhold-on-reset-Logon** condition specified precisely — withhold when the header pre-scan shows `141=Y` OR `reset_on_logon`, apply otherwise — and is the placement requirement (after the pre-scan `:1585-1587`, before `check_inbound` `:1596`) stated consistently across spec/plan/contract? [Consistency, C2.4 / C2.5 / research D-6]
- [ ] CHK004 Is the asymmetry of the hydrate split stated (outbound seed applied UNCONDITIONALLY since it only feeds `34=`/never a gate; inbound seed CONDITIONAL on the non-reset branch)? [Clarity, C2.4 / INV-H5]

## Post-GapFill / Peer-Ahead Recovery Precondition

- [ ] CHK005 Is the lower-bound recovery precondition stated WITHOUT over-claim — i.e. a too-high peer Logon recovers non-fatally ONLY when `enable_next_expected_msg_seq_num` (789) is on OR the peer Logon announces a reset; otherwise it fatals on the Logon gate and recovers by reconnect (L-029-1)? [Clarity, SC-004 / FR-009 / [[feedback_witness_asserts_named_postcondition_not_proxy]]]
- [ ] CHK006 Does the acceptance criterion forbid naming the knob-OFF path "recovers via ResendRequest" (the Logon gate has no ResendRequest arm), requiring the knob-off case to assert its own Disconnected-then-reconnect disposition? [Measurability, SC-004 / W5 / spec US2 AS3]
- [ ] CHK007 Is L-029-1 (post-GapFill restart ⇒ bounded redundant ResendRequest when 789/reset available, else Logon fatal + reconnect) specified as a documented, recovery-correct limitation — not a silent skip? [Completeness, L-029-1 / data-model W5]
- [ ] CHK008 Is the clean-restart (no-gap) case — resumed at N+1, peer continues from N+1, peer Logon in-sequence — covered as a requirement distinct from the gap case, so W4 is not the only inbound-resume scenario? [Coverage, spec US2 AS / W4]

## 789 (Next-Expected) Interaction

- [ ] CHK009 Is the requirement that a hydrated initiator with the 789 knob on advertises `789 = <hydrated next_inbound>` (the true resumed position, not 1) stated, with the source of the value (the hydrated manager) identified? [Completeness, C2.7 / spec Assumptions §027/789 / W11]
- [ ] CHK010 Is the requirement that a resumed session does NOT emit a spurious `141=Y` (because `seqnums_at_one` is false once hydrated) stated and tied to the `seqnums_at_one` condition? [Clarity, C2.7 / W11]
- [ ] CHK011 Is the ordering between hydrate and the 789 sampling specified (hydrate completes before the initiator `789`/acceptor reply `789` is sampled) so the advertisement reflects the resumed value? [Consistency, FR-004 / C2.7]

## Received-Reset / reset_on_logon Precedence (non-regression)

- [ ] CHK012 Is the requirement that the existing `141`/024 cause-dependent reset split is PRESERVED (hydrate must not mask, reorder, or suppress the received-reset path) stated as a hard non-regression requirement? [Completeness, FR-010 / INV-H5]
- [ ] CHK013 Is the outbound precedence specified (outbound hydrate runs BEFORE the `024 reset_on_logon` block, so a configured reset still wins and resets outbound to 1)? [Consistency, INV-H5 / W9a]
- [ ] CHK014 For the acceptor reset-Logon case, is the post-state of `next_inbound` after withhold specified ("per existing 013 policy") rather than left undefined, so the witness has a definite expected value? [Ambiguity, W9b / C2.6]

## Reconnect & One-Shot Boundary

- [ ] CHK015 Is the requirement that reconnect does NOT re-hydrate (one-shot) stated alongside the recovery interaction, so a reconnect after a transient gap relies on the 013 sub-protocol (not a re-read that would regress the manager)? [Consistency, INV-H3 / D-6 / C4]

## Notes

- This checklist tests the requirements for the seqnum-boundary INTERACTION; the durable write/lower-bound mechanics live in `durability.md`.
- The pivotal RC-1 items are CHK002/CHK003 (withhold-on-reset-Logon) and CHK005/CHK006 (narrowed recovery precondition) — these are the round-2 Gate-A corrections that the requirements MUST encode unambiguously, or the witnesses regress to over-claims.
