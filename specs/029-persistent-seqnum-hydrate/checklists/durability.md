# Durability Requirements Checklist: Persistent seqnum hydrate

**Purpose**: Validate that the durability/persistence requirements (the durable inbound write, the lower-bound invariant, the crash-ordering semantic, the failure disposition, and the non-persistent no-op floor) are complete, clear, consistent, and measurable — BEFORE a high-risk store↔session boundary slice (008 drew 5 P1s).
**Created**: 2026-06-09
**Feature**: [spec.md](../spec.md) · [data-model.md](../data-model.md) · [contracts/seqnum-hydrate.md](../contracts/seqnum-hydrate.md)

## Persistence Semantics (lower bound & ordering)

- [ ] CHK001 Is the INV-H1 lower-bound invariant (`persisted_next_inbound ≤ manager.next_inbound`, always) stated as an always-holds requirement, with every place it could be violated enumerated (GapFill jump, fatal-after-advance, persist failure)? [Completeness, INV-H1 / data-model §Invariants]
- [ ] CHK002 Is "deliver-then-persist" specified precisely enough to distinguish the in-memory `check_inbound` advance (BEFORE delivery, the gate, unchanged) from the durable write (AFTER the callback)? [Clarity, FR-002 / INV-H2 / C4]
- [ ] CHK003 Can the D-2 ordering ("after the application callback returns") be objectively verified, and is the observation point (durable value sampled *inside* `fromApp`/`fromAdmin` for msg S equals S, not S+1) specified rather than left to the implementer? [Measurability, W3 / INV-H2]
- [ ] CHK004 Is the at-least-once consequence (crash mid-delivery ⇒ re-deliver on restart, deduped by PossDup/ResendRequest; never silently skipped) stated as a requirement, not only as rationale? [Completeness, FR-002 / spec Edge Cases]
- [ ] CHK005 Is the counter-only nature of the inbound persist (no inbound frame body retained — fixpp never resends inbound) explicitly required and distinguished from the outbound store-with-body? [Clarity, plan §Summary / C3.0]
- [ ] CHK006 Is the decision NOT to persist a `SequenceReset`-GapFill absolute jump (the store has no absolute set; the jump is `set_next_inbound`, not `+1`) stated as a requirement with its INV-H1 justification, rather than an incidental implementation choice? [Completeness, D-5 / INV-H1 / C3.4]

## Persist Disposition Matrix (coverage & consistency)

- [ ] CHK007 Does the spec/data-model enumerate the COMPLETE set of `check_inbound`-success PERSIST sites (acceptor Logon, initiator Logon-ack, Heartbeat, TestRequest, ResendRequest, Logout, Reject, in-seq app, resend-fill) as an exact set, so a tail-only persist that silently drops admin frames is caught? [Coverage, RC-2 / data-model §Persist disposition matrix]
- [ ] CHK008 Is each NO-PERSIST arm (too-low/PossDup redelivery, validate-off deliver-without-advance, Reset-mode `35=4`, absolute jump) specified with the reason it did NOT advance the manager, so the persist gate is keyed to "did `check_inbound` advance?" not to message type? [Clarity, RC-2 / data-model matrix]
- [ ] CHK009 Is the `35=4` three-way split (validate-off exact-match GapFill → PERSIST; Reset-mode `35=4` → NO-PERSIST; absolute `NewSeqNo` jump → NO-PERSIST) stated unambiguously, with the distinguishing control-flow (advanced via `:2253` vs runs-before-`check_inbound` vs `set_next_inbound`)? [Consistency, RC-B / C3.4 / W12]
- [ ] CHK010 Is the resend-fill-vs-GapFill-jump distinction (an in-sequence PossDup *fill* PERSISTs; only the `apply_inbound_sequence_reset` absolute jump is excluded) specified so the jump exclusion cannot accidentally suppress fill persistence? [Conflict, data-model §"Resend-fill vs GapFill-jump (New-2)"]
- [ ] CHK011 For the terminal PERSIST sites (Logout/Reject), is the persist ordering relative to `record_state_transition_(Disconnected)` specified (after `fromAdmin`, before the transition, `store_` still live), rather than the ambiguous "after handling"? [Clarity, C3.1 / data-model matrix]

## Hydrate-on-Open Lifecycle

- [ ] CHK012 Is "hydrate-on-open" bounded to **cold open, one-shot** (mutates the manager at most once per session lifetime; reconnect never re-hydrates) as an explicit requirement, with the regression it prevents (re-hydrate regressing a live manager to the store lower bound) named? [Completeness, INV-H3 / D-6]
- [ ] CHK013 Is the latch-after-success semantic (`hydrated_` set ONLY after both reads + `hydrate()` succeed; a transient read failure stays retryable on the next reconnect) specified, distinct from a pre-latched flag that would make a transient failure sticky? [Clarity, INV-H6 / D-9 / C2.1]
- [ ] CHK014 Is the hydrate-before-first-use ordering requirement (outbound counter loaded before any outbound seqnum is sampled; inbound seed before the inbound validation gate) stated with both directions, and is it testable as a happens-before, not just a call-count? [Measurability, FR-004 / W8 / data-model W8]
- [ ] CHK015 Is the empty/never-written persistent store case specified as a no-op hydrate to `1` (NOT an error), with the WIRE-level byte-identity caveat (one open-time read accepted) called out? [Edge Case, spec Edge Cases / FR-005]

## Failure Disposition

- [ ] CHK016 Is the inbound persist-write failure disposition (D-3 fatal → Disconnected, reusing the existing store-failure disposition, no new error slot) stated as a requirement, with the reconnect-rehydrates-last-durable-value recovery path named? [Completeness, FR-007 / D-3]
- [ ] CHK017 Is the hydrate READ-failure disposition (fatal → Disconnected, NO partial seed — manager mutated only after BOTH reads succeed) specified and distinguished from the persist-WRITE failure? [Clarity, FR-006 / SC-005 / C2.3]
- [ ] CHK018 For the persist-failure witness, does the spec require asserting the durable counter is the last-persisted lower bound (NOT "manager unchanged", since `check_inbound` advanced before delivery ⇒ at-least-once replay)? [Measurability, SC-006 / W6 / [[feedback_witness_asserts_named_postcondition_not_proxy]]]

## Non-Persistent No-Op (byte-identity floor)

- [ ] CHK019 Is the non-persistent discriminator requirement keyed to a captured-at-open `store_is_persistent_` bool (from a factory accessor) rather than `store_ == nullptr`, so a configured MEMORY store (non-null) still skips the read? Is the reason (a memory store's counter read posts/locks/allocates) stated? [Clarity, FR-005 / D-10 / INV-H4]
- [ ] CHK020 Is the byte-identity guarantee for the non-persistent path quantified as "zero added store read AND zero added allocation on the open path", and is the binding measurement basis (mallocnesia LD_PRELOAD, not a PMR counter alone) named? [Measurability, SC-003 / INV-H4]

## Ambiguities, Assumptions & Boundaries

- [ ] CHK021 Is the outbound asymmetry (inbound persist failure = fatal; outbound store write stays I-07 logged-then-proceed) documented as a deliberate, in-scope boundary (L-029-2) rather than an inconsistency, with "outbound→fatal is out of scope" stated? [Assumption, plan §Complexity / L-029-2]
- [ ] CHK022 Is the assumption that the existing `MessageStore`/`FileStore` already persists both counters in one record (only the inbound counter was never advanced by the session) validated against 008, so "no store schema change" holds? [Assumption, plan §Storage / spec Assumptions]
- [ ] CHK023 Is the I-3 comment reconciliation (the unwired `session.cpp:1517` "store-before-deliver" prose corrected to deliver-then-persist) captured as a required deliverable, so shipped code and comments agree? [Gap, D-8 / spec Assumptions]

## Notes

- This checklist tests whether the durability REQUIREMENTS are well-written — not whether the persistence works. Pair with `recovery-interaction.md` (Logon-gate / 789 / 141) and `interop.md` (live restart-resume cell).
- Highest-risk items: CHK001/006 (INV-H1 lower bound), CHK007–CHK011 (persist matrix exactness), CHK012–CHK013 (one-shot / latch-after-success). These are the Gate-A-adjudicated pins.
