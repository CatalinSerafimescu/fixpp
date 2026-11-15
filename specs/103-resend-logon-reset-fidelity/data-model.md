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
Connection-scoped work captures it; the reconnect drain makes older-generation work unreachable after an install, so
the bound asserts it. The acceptor needs none: `attach_accepted_transport` runs once per Session.

## Reconnect drain — `drive_reconnect` (changed) and `inbound_depth_` (new)

- `inbound_depth_`: `std::uint32_t`, raised by an RAII holder declared as `on_inbound_active_`'s first statement,
  lowered at its every exit, an exception and frame destruction included (not in `on_inbound_frame`, a non-coroutine
  forwarder); reaching 0 calls `replay_gate_.wake_all()`. Precondition: the Session outlives the inbound frame (the
  holder's destructor touches `inbound_depth_` and the gate), as for the chain's existing lock guards.
- Reader fence: `drive_reconnect` on an Engine-managed Session (`cfg_.engine_managed`) with an installed transport
  returns `session_already_open` and changes nothing (the Engine's read pump owns that transport). Standalone
  precondition: the caller's reader has left `live_transport()` before the call and starts on the new one after it.
- `reconnect_in_flight_`: `bool`, set at `drive_reconnect`'s entry, cleared by RAII; a second call returns
  `session_already_open`.
- Quiescent ⇔ `replay_gate_.shared == 0 && inbound_depth_ == 0` (no emitter outside the inbound chain between its
  number peek and the return of its `store_then_emit`, and no inbound chain running), then, after the drain's
  `root_cancel_.emit(total)`, `bound_counter == 0 && liveness_counter_ == 0` (polled by post).
- `drive_reconnect`: refuse if closing, closed or stopping (`session_already_closed`); from Active or LogonReceived
  record `Disconnected`; wait until quiescent on the gate's wake (bounded by `logout_disconnect_timeout_ms`, then the
  old transport is force-closed and the wait continues); re-test the refusal after every wake; then connect and
  install. The install is the commit point: with no suspension before it, it refuses on closing, closed or stop,
  closes the new transport and installs nothing (`session_already_closed`). It asserts quiescence. From a
  callback: one post first. Cancellation reaching backoff, connect or handshake: fixpp#585, unchanged. Caller cancellation → `dispatch_aborted`, nothing installed, state
  `Disconnected`, cancellation left pending.
- Invariant: no frame numbered or claimed on one connection is written on a later one.

## Pending-Logout mark — `peer_logout_` (new)

Fields: `seq` (the too-high Logout's MsgSeqNum S), `at` (arrival time), `phase ∈ {none, pending, acking}`. The bound
coroutine owns its captured `gen`; the session owns `peer_logout_wake_` (published by the bound, cleared in the
bound's body when its T race ends; no exit guard clears it, D4.4) and the join counter (a shared counter, incremented before `co_spawn`, decremented by
the bound's exit guard, which touches only that counter; `close()` and the reconnect drain poll it by post).

## Logout owner — `logout_in_flight_` (new, Gate A round 2; held at the leaf before loop 2)

`bool`, tested and set synchronously, before any stamp, number peek or suspension, by the two Logout leaves of an
established session: `inbound_logout_leaf_` (every Active inbound-chain Logout and the bound's acknowledgement:
in-sequence acknowledgement, gap deadline, SendingTime-fatal, PossDup Arm D, Arm B, T = 0 acknowledgement, the
bound) and `run_logout_phase1`; cleared when that leaf returns, on every path. Found set: `inbound_logout_leaf_`
returns a "skipped" success and its caller keeps the rest of its ending; `close(graceful)` poll-joins it inside its
grace and sends none; the bound tests it at its claim, sets the phase `none` and exits. LS-1 pins that no other
Active Logout builder exists. Invariant: at most one Logout of an established session is in flight.

| From | Event | To | Effect |
|---|---|---|---|
| none | too-high Logout in Active, T > 0 and a clock | pending | ResendRequest if none outstanding; bound spawned, captures `gen = conn_gen_`, publishes its wake, then re-tests "live" |
| none | too-high Logout in Active, T = 0 or no clock | none | acknowledgement at once, no ResendRequest |
| pending | NextNumIn > S (`note_inbound_advance_`) or T elapsed, "live" still true after the gate wait, and `logout_in_flight_` clear | acking | bound sends the acknowledgement; then the event and `Disconnected` (guarded by `fsm_state_ != Disconnected`; `conn_gen_ == gen` asserted, D4.4) |
| pending | gap deadline, in-sequence Logout, `close(graceful)`, Arm B too-low, `close(terminal)`, stop | none | that ending's own Logout, if any, is the one Logout (claim table D4.6); bound woken |
| pending | SendingTime-fatal or PossDup Arm-D Logout (existing endings) | pending | the leaf owns their Logout; the bound exits at its owner test or at their `Disconnected` edge |
| acking | `close(graceful)` | acking | `logout_in_flight_` is set: `close()` poll-joins it inside its grace, before `Disconnected` and before closing the transport |
| any | new transport installed (always after the drain, which waited for the bound's counter to reach 0) | none | `conn_gen_` moves; no bound of the old connection exists (D1 step 3) |
| any | edge into `Disconnected` | unchanged | bound and gate woken; the bound's "live" test fails (not Active), so it exits without effect |

"Live" = phase `pending`, `fsm_state_ == Active`, not `closing`/`closed_drained`, no engine stop.
The bound tests it after publishing its wake, after its T race, after every gate wake, and before its claim, and
asserts `conn_gen_ == gen` at each test and after its emit (D4.4). Invariant: no bound acts on a connection other
than the one its mark arrived on (the drain guarantees it).

While `phase != none`, `Session::send` returns `session_invalid_state_for_send`. While `phase == pending`,
administrative traffic is answered; while `acking`, inbound frames are drained.

## Replay gate — `detail::replay_gate` (new)

Fields: `shared` (open shared holds), `exclusive` (a walk is waiting or running), `waiters`, lazy `wake` timer.
- Shared hold: every emitter outside the inbound chain (`send_body_`/`send_impl`, liveness Heartbeat/TestRequest,
  `run_logout_phase1`, the bound's acknowledgement), from before its SendingTime stamp and number peek until its
  `store_then_emit` returns. Fast path: no await, no allocation.
- Exclusive hold: `replay_outbound_range_` from entry (before the store tail read) to every ending (RAII); waits
  for `shared == 0` first.
- Wake sources: exclusive release, the last shared release whenever a waiter exists (the walk, or the reconnect
  drain), `inbound_depth_` reaching 0, the `Disconnected` edge, engine stop, `close()`.
- A contended wait: the pre-check sits in the waiting frame, or the wait is awaited through the split idiom (D5′.2).
  A wait its caller's cancellation ends returns `dispatch_aborted`, state unchanged, no number consumed, the
  caller's cancellation left pending.
- Guarantee: no outbound number is assigned during an answer; frames numbered before it are written before its
  first frame.

## Frame header — `detail::FrameHeader::begin_seqno_present` (new internal field)

Set when tag 7 is present; distinguishes an absent BeginSeqNo (Reject 373=1, 371=7) from 7=0 (answered as 7=1).

## C-ABI version — `FIXPP_C_ABI_VERSION_MINOR` 16 → 17 (BREAKING, Article X §7)

No symbol, type or error code changes; the documented default result changes.
