# Quickstart: Persistent seqnum hydrate — RED witnesses

TDD order: each witness is RED before the corresponding wiring lands. Tests in
`tests/session/test_persistent_seqnum_hydrate.cpp` unless noted.

## W1 — Initiator restart resumes outbound (SC-001)
Seed a persistent store to `next_outbound = 42`. Construct a fresh initiator over it, open,
drive the Logon. **Assert** the Logon carries `34=42` (not `34=1`).

## W2 — Inbound durable-tracking + restart resume (SC-002 / FR-001 / INV-H2 / RC-2)
Drive 5 accepted in-seq inbound messages — the stream **must include admin frames (heartbeats)**
plus app messages so the persist disposition matrix is exercised (a heartbeat-only stream must
still durably track; a single tail-only persist would silently miss the heartbeats). **Assert** the
store's `next_inbound == 6`. Then construct a fresh session over the same store, open, and
**assert** the manager resumes `next_inbound == 6` (not 1).

## W3 — Deliver-then-persist ordering (INV-H2)
Use an `Application` (or store) that records the durable `next_inbound` value observed *inside*
`fromApp`. **Assert** that during the callback for message `S` the durable counter is still `S`
(not yet `S+1`) — i.e. the persist happens strictly after the callback returns.

## W4 — Acceptor cold resume (FR-009, both roles)
Seed a store `{next_inbound=37, next_outbound=42}`. Construct an acceptor over it; deliver a peer
Logon with `34=37`. **Assert** it is treated as in-sequence (reaches Active, **no** too-high
ResendRequest / too-low fatal), and the acceptor reply Logon samples `34=42`.

## W5 — Post-GapFill lower bound + recovery precondition (SC-004 / INV-H1 / L-029-1 / RC-1 / New-1)
Drive an inbound `SequenceReset`-GapFill that jumps the manager `N→M`. **Assert**
`store.next_inbound ≤ manager.next_inbound` (the jump is not persisted). Then restart over the
store with the peer's first post-restart Logon carrying a seq **higher** than the hydrated stale
lower bound. **Assert the actual precondition, not an over-claim:**
- with `enable_next_expected_msg_seq_num` **ON** (or a peer **reset** Logon `141=Y`): the peer
  Logon is admitted (behind-side tolerance / in-seq), the session resumes ≤ true, recovers via the
  proactive resend / ResendRequest, **no fatal** — this is the recovered path;
- with the knob **OFF** and the peer Logon too-high: the session **fatals on the Logon gate**
  (`:1615` / `:2856`) — assert the Disconnected transition as its own documented L-029-1 case.

Do **not** name the knob-off path "recovers via ResendRequest" — the Logon gate has no
ResendRequest arm ([[feedback_witness_asserts_named_postcondition_not_proxy]]).

## W6 — Inbound persist failure is fatal (SC-006 / D-3)
Inject a `persist_inbound_advance_()` (`next_seqnum(inbound,true)`) failure via a fault-injecting
test store on an in-seq message. **Assert** the session transitions to Disconnected (fatal) and the
**durable** store counter remains the last successfully-persisted value (the failed write did not
advance it — a safe lower bound, INV-H1). Do **NOT** assert "manager unchanged": the in-memory
`check_inbound` advanced before delivery, so on reconnect the in-flight message may be replayed
(at-least-once, INV-H2), never skipped. (Split: failure on the first persist vs a later one.)

## W14 — Hydrate read-failure is fatal, no partial seed (SC-005 / FR-006 / C2.3)
Inject a `next_seqnum(dir,false)` **read** failure on the hydrate path via the fault-injecting test
store. **Assert** the session transitions to Disconnected (fatal), the manager is **not** partially
seeded, and `hydrated_` stays `false` so the next reconnect retries (D-9). **Split** to prove no
partial seed: (a) fail the **first** read (`inbound,false`) → no mutation at all; (b) fail the
**second** read (`outbound,false`) after the first succeeded → still no mutation (the manager is
written only after both values are in hand). Distinct from W6 (the inbound **write**/persist
failure).

## W7 — Non-persistent store no-op (SC-003 / INV-H4 / D-10)
Construct a session with a null store AND one with a memory store (both ⇒ `store_is_persistent_ ==
false`). **Assert** counters start 1, `ensure_hydrated_` performs **no store read** on either
(the memory store is discriminated non-persistent at `open()`, not read), frames are byte-identical
to the pre-feature baseline, and the full existing session/seqnum/store regression stays green.

## W8 — One-shot fires exactly once + happens-before (INV-H3 / New-4 / [[feedback_half_restructure_symmetric_api]])
Instrument `ensure_hydrated_` (or `hydrate`) with a call counter. **Assert** it mutates the
manager exactly once for an initiator and exactly once for an acceptor, that a **reconnect**
(second `emit_initiator_logon_` via `drive_reconnect`) does **not** re-hydrate, and — on both
roles — that **hydrate completes-before the first `check_inbound`** (a happens-before assertion,
e.g. an ordering token / sequence log, not just a call count).

## W9 — Precedence vs reset/received-141 (INV-H5 / RC-1)
(a) `reset_on_logon=true` + persisted `{37,42}` → after the initiator Logon the outbound is `1`
(reset wins over hydrate). (b) Acceptor cold-hydrated, peer Logon `34=1,141=Y`,
`reset_on_logon=false` → the inbound seed is **withheld** (corrected ordering) so `check_inbound(1)`
is in-sequence; the received-141 reset wins and the manager ends at `next_inbound` per existing 013
policy. **Assert the session does NOT fatal as too-low at `:1615`** — this is the witness the
pre-fix design could not pass.

## W11 — Hydrated initiator 789 advertisement (New-3 / 027 interaction)
Seed `{next_inbound=37, next_outbound=42}` and set `enable_next_expected_msg_seq_num=true`.
Construct a fresh initiator, open, drive the Logon. **Assert** the Logon carries `789=37` (the
hydrated `next_inbound`, not `1`) and does **not** carry `141=Y` (`seqnums_at_one` is false on a
resumed session, so no spurious reset advertisement).

## W12 — Validate-off `35=4` persist split (RC-B / New-B)
On a session with `validate_sequence_numbers=false`, drive an **exact-match** SequenceReset-GapFill
(`35=4`) whose `MsgSeqNum` equals the expected inbound — `check_inbound` advances `+1` at `:2253`,
the validate-off branch (`:2339`) dispatches `fromAdmin` and early-`co_return`s at `:2359` without
`apply_inbound_sequence_reset`. **Assert** the durable inbound counter advanced `+1` (PERSIST after
`fromAdmin` returns). Sibling: drive a **Reset-mode** `35=4` validate-off (S6, `:1968-2026`, runs
before `check_inbound`, no advance) and **assert** the durable inbound counter is unchanged
(NO-PERSIST). This is a 028-interop-cell configuration (a live path), so the corrected row ships
tested, not by coincidence.

## W13 — Custom-store discriminator (RC-A / New-B)
Construct a session over a **custom persistent** factory (`yields_persistent_store()==true`, or use
the FileStore path) and **assert** hydrate runs (the reads fire / counters resume). Construct one
over a **custom non-persistent** factory (`yields_persistent_store()==false`, like
`MemoryStoreFactory`) and **assert** hydrate is skipped (`store_is_persistent_==false`, no read).
This tests the discriminator beyond the two built-ins by coincidence — a missed override would
otherwise resume (safe default), and the flush-hook tag is NOT consulted.

## W10 — Live interop (parent harness, skip-without-counterparty)
Restart a fixpp initiator (and separately an acceptor) mid-session against a running QFcpp/QFJ
peer. **Assert** both counters resume from the persisted store and the peer-ahead inbound recovers
via ResendRequest — no fatal too-low/too-high. Golden captured for the live matrix.
