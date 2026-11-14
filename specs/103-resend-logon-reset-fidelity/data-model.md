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
- Cleared: the gap closes (NextNumIn passes `end`), a SequenceReset moves NextNumIn past it, **and (103) every edge
  into `Disconnected`** (D1). A reconnected session therefore starts with no resend outstanding.

## Pending-Logout mark — `peer_logout_` (new)

Fields: `seq` (the too-high Logout's MsgSeqNum S), `at` (arrival time), `phase ∈ {none, pending, acking}`.

| From | Event | To | Effect |
|---|---|---|---|
| none | too-high Logout in Active, T > 0 and a clock | pending | ResendRequest if none outstanding; bound coroutine spawned |
| none | too-high Logout, T = 0 or no clock | none | acknowledgement at once, no ResendRequest |
| pending | NextNumIn > S (`note_inbound_advance_`) or T elapsed | acking | bound sends the acknowledgement (after its gate wait), then `Disconnected` |
| pending | gap deadline, in-sequence Logout, `close(graceful)`, Arm B too-low | none | that ending's own Logout is the one Logout (claim table D4.6) |
| any | edge into `Disconnected` | none | bound woken; exits without effect |

While `phase != none`, `Session::send` returns `session_invalid_state_for_send`.

## Replay gate — `detail::replay_gate` (new)

Fields: `shared` (open shared holds), `exclusive` (a walk is waiting or running), `waiters`, lazy `wake` timer.
- Shared hold: every emitter outside the inbound chain (`send_body_`/`send_impl`, liveness Heartbeat/TestRequest,
  `run_logout_phase1`, the bound's acknowledgement), from before its SendingTime stamp and number peek until its
  `store_then_emit` returns. Fast path: no await, no allocation.
- Exclusive hold: `replay_outbound_range_` from entry (before the store tail read) to every ending (RAII); waits
  for `shared == 0` first.
- Wake sources: exclusive release, last shared release while exclusive, the `Disconnected` edge, engine stop.
- Guarantee: no outbound number is assigned during an answer; frames numbered before it are written before its
  first frame.

## Frame header — `detail::FrameHeader::begin_seqno_present` (new internal field)

Set when tag 7 is present; distinguishes an absent BeginSeqNo (Reject 373=1, 371=7) from 7=0 (answered as 7=1).

## C-ABI version — `FIXPP_C_ABI_VERSION_MINOR` 16 → 17 (BREAKING, Article X §7)

No symbol, type or error code changes; the documented default result changes.
