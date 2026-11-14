# Data Model: 103 resend and Logon-reset fidelity

Session state added or changed. All of it is private to `Session` (or internal under `src/session`) and lives on
the session strand; no public type changes. Decisions are in [research.md](research.md).

## Reset policy — `SessionConfig::reset_seqnum_policy_field` (changed default)

| Value | Initiator sends 141=Y when | Initiator accepts an ack without 141=Y | Acceptor, Logon without 141=Y | Acceptor, 141=Y Logon |
|---|---|---|---|---|
| `bilateral_lenient` (**new default**) | a reset setting is on and both numbers are 1 | yes | accepted | mirrored |
| `bilateral_strict` | always, after a durable reset of both numbers to 1 (34=1) | no (`session_seqnum_reset_mismatch`) | refused with Logout + Text, before the sequence check | mirrored |
| `unilateral` | as lenient | yes | accepted | not mirrored |

Invariant (FR-002): any Logon fixpp sends with 141=Y carries 34=1 and both stored numbers are 1 when it is built.

## Resend state — `ResendState` / `ReconnectFsm` (lifetime changed)

Fields: awaiting-resend flag, outstanding range [begin, end], `started_at` (deadline start), `inbound_filled_through`.
- Set: the first too-high frame in Active (`enter_awaiting_resend`).
- Cleared: the gap closes (NextNumIn passes `end`), a SequenceReset moves NextNumIn past it, **and (103) when a new
  transport is installed** (`install_reconnected_transport`, D1), whether or not the session passed through
  `Disconnected` first. A reconnected session therefore starts with no resend outstanding. Not cleared on the
  `Disconnected` edge: nothing reads it outside Active (D1; re-derived by the phase-1 census).

## Connection generation — `conn_gen_` (new)

`std::uint32_t`, incremented in `install_reconnected_transport` before its `LogonSent` transition (D1).
Connection-scoped work captures it and acts only while it is current. The acceptor needs none:
`attach_accepted_transport` runs once per Session.

## Pending-Logout mark — `peer_logout_` (new)

Fields: `seq` (the too-high Logout's MsgSeqNum S), `at` (arrival time), `phase ∈ {none, pending, acking}`. The bound
coroutine owns its captured `gen`; the session owns `peer_logout_wake_` (published by the bound, cleared when its T
race ends and on every exit) and the join counter (a shared counter, incremented before `co_spawn`, decremented by
the bound's exit guard, which also cancels any `bound_done` timer `close()` races on).

| From | Event | To | Effect |
|---|---|---|---|
| none | too-high Logout in Active, T > 0 and a clock | pending | ResendRequest if none outstanding; bound spawned, captures `gen = conn_gen_`, publishes its wake, then re-tests "live" |
| none | too-high Logout in Active, T = 0 or no clock | none | acknowledgement at once, no ResendRequest |
| pending | NextNumIn > S (`note_inbound_advance_`) or T elapsed, and "live" still true after the gate wait | acking | bound sends the acknowledgement; then, if `conn_gen_ == gen`, the event and `Disconnected` |
| pending | gap deadline, in-sequence Logout, `close(graceful)`, Arm B too-low, `close(terminal)`, stop | none | that ending's own Logout, if any, is the one Logout (claim table D4.6); bound woken |
| acking | `close(graceful)` | acking | `close()` joins the bound inside its grace, before `Disconnected` and before closing the transport |
| any | new transport installed | none | `conn_gen_` moves; bound and gate woken; bound exits without effect |
| any | edge into `Disconnected` | unchanged | bound and gate woken; the bound's "live" test fails (not Active), so it exits without effect |

"Live" = `conn_gen_ == gen`, phase `pending`, `fsm_state_ == Active`, not `closing`/`closed_drained`, no engine stop.
The bound tests it after publishing its wake, after its T race, after every gate wake, and before its claim; after
the claim it re-tests only `conn_gen_ == gen`, after every suspension, before any state write or event (D4.4).
Invariant: no bound acts on a connection other than the one its mark arrived on.

While `phase != none`, `Session::send` returns `session_invalid_state_for_send`. While `phase == pending`,
administrative traffic is answered; while `acking`, inbound frames are drained.

## Replay gate — `detail::replay_gate` (new)

Fields: `shared` (open shared holds), `exclusive` (a walk is waiting or running), `waiters`, lazy `wake` timer.
- Shared hold: every emitter outside the inbound chain (`send_body_`/`send_impl`, liveness Heartbeat/TestRequest,
  `run_logout_phase1`, the bound's acknowledgement), from before its SendingTime stamp and number peek until its
  `store_then_emit` returns. Fast path: no await, no allocation.
- Exclusive hold: `replay_outbound_range_` from entry (before the store tail read) to every ending (RAII); waits
  for `shared == 0` first.
- Wake sources: exclusive release, last shared release while exclusive, the `Disconnected` edge, engine stop, a new
  transport installed.
- A contended wait: the pre-check sits in the waiting frame, or the wait is awaited through the split idiom (D5′.2).
  A wait its caller's cancellation ends returns `dispatch_aborted`, state unchanged, no number consumed, the
  caller's cancellation left pending.
- Guarantee: no outbound number is assigned during an answer; frames numbered before it are written before its
  first frame.

## Frame header — `detail::FrameHeader::begin_seqno_present` (new internal field)

Set when tag 7 is present; distinguishes an absent BeginSeqNo (Reject 373=1, 371=7) from 7=0 (answered as 7=1).

## C-ABI version — `FIXPP_C_ABI_VERSION_MINOR` 16 → 17 (BREAKING, Article X §7)

No symbol, type or error code changes; the documented default result changes.
