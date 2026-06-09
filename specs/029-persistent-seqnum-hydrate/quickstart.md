# Quickstart: Persistent seqnum hydrate — RED witnesses

TDD order: each witness is RED before the corresponding wiring lands. Tests in
`tests/session/test_persistent_seqnum_hydrate.cpp` unless noted.

## W1 — Initiator restart resumes outbound (SC-001)
Seed a persistent store to `next_outbound = 42`. Construct a fresh initiator over it, open,
drive the Logon. **Assert** the Logon carries `34=42` (not `34=1`).

## W2 — Inbound durable-tracking + restart resume (SC-002 / FR-001 / INV-H2)
Drive 5 in-seq inbound messages through an established session over a persistent store.
**Assert** the store's `next_inbound == 6`. Then construct a fresh session over the same store,
open, and **assert** the manager resumes `next_inbound == 6` (not 1).

## W3 — Deliver-then-persist ordering (INV-H2)
Use an `Application` (or store) that records the durable `next_inbound` value observed *inside*
`fromApp`. **Assert** that during the callback for message `S` the durable counter is still `S`
(not yet `S+1`) — i.e. the persist happens strictly after the callback returns.

## W4 — Acceptor cold resume (FR-009, both roles)
Seed a store `{next_inbound=37, next_outbound=42}`. Construct an acceptor over it; deliver a peer
Logon with `34=37`. **Assert** it is treated as in-sequence (reaches Active, **no** too-high
ResendRequest / too-low fatal), and the acceptor reply Logon samples `34=42`.

## W5 — Post-GapFill lower bound + recovery (SC-004 / INV-H1 / L-029-1)
Drive an inbound `SequenceReset`-GapFill that jumps the manager `N→M`. **Assert**
`store.next_inbound ≤ manager.next_inbound` (the jump is not persisted). Restart over the store
and **assert** the session resumes ≤ true, issues a ResendRequest, and recovers (no fatal, no skip).

## W6 — Inbound persist failure is fatal (SC-006 / D-3)
Inject a `next_seqnum(inbound,true)` failure via a fault-injecting test store on an in-seq
message. **Assert** the session transitions to Disconnected, the manager is unchanged, and there
is no partial state. (Split: failure on the first persist vs a later one.)

## W7 — Memory/null store no-op (SC-003 / INV-H4)
Construct a session with no `store_` (and one with a memory store). **Assert** counters start 1,
`ensure_hydrated_` performs no store read on the no-store path, frames are byte-identical to the
pre-feature baseline, and the full existing session/seqnum/store regression stays green.

## W8 — One-shot fires exactly once (INV-H3 / [[feedback_half_restructure_symmetric_api]])
Instrument `ensure_hydrated_` (or `hydrate`) with a call counter. **Assert** it mutates the
manager exactly once for an initiator and exactly once for an acceptor, and that a **reconnect**
(second `emit_initiator_logon_` via `drive_reconnect`) does **not** re-hydrate.

## W9 — Precedence vs reset/received-141 (INV-H5)
(a) `reset_on_logon=true` + persisted `{37,42}` → after the initiator Logon the outbound is `1`
(reset wins over hydrate). (b) Acceptor cold-hydrated to `next_inbound=37`, peer Logon `34=1,141=Y`
→ received-141 reset wins, manager ends at `next_inbound` per existing 013 policy.

## W10 — Live interop (parent harness, skip-without-counterparty)
Restart a fixpp initiator (and separately an acceptor) mid-session against a running QFcpp/QFJ
peer. **Assert** both counters resume from the persisted store and the peer-ahead inbound recovers
via ResendRequest — no fatal too-low/too-high. Golden captured for the live matrix.
