# Data Model: 103 resend and Logon-reset fidelity

Session state added or changed. All of it is private to `Session` (or internal under `src/session`) and lives on
the session strand; no public type changes. One installed header is added, include/fixpp/session/detail/replay_gate.hpp:
an internal detail header with no public symbol, installed only because `Session` holds the gate by value in
include/fixpp/session/session.hpp (precedent: include/fixpp/session/detail/has_flush_for_session_close.hpp). "No public
symbol" means what it means for that precedent: the header declares only names in a `detail` namespace, which the
documentation does not describe and the supported API does not include; the install-surface and layering checks
(`tools/check_layers.py`, the packaging consumer tests) must accept it as they accept the precedent, and tasks T061
runs them. The gate's `asio::steady_timer` adds no include requirement for a consumer of the installed
session.hpp: session.hpp already reaches `<asio/steady_timer.hpp>` through `fixpp/session/reconnect_fsm.hpp`, whose
members are `std::optional<asio::steady_timer>` (re-derive: `command grep -n steady_timer
include/fixpp/session/reconnect_fsm.hpp`); replay_gate.hpp includes it itself. Decisions are in [research.md](research.md).

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

`std::uint32_t`, incremented in `install_reconnected_transport` before its `LogonSent` transition (D1), after the
install's commit-point test: a refused install installs nothing, clears nothing and leaves `conn_gen_` unchanged. In
the install the order is the commit-point test, then the clear (resend state, mark) and the increment, then the
`LogonSent` transition, then `drive_reconnect`'s `emit_initiator_logon_`.
Connection-scoped work captures it; the reconnect drain makes older-generation work unreachable after an install, so
the bound asserts it (`FIXPP_ASSERT`), never branches on it: only builds with `FIXPP_ASSERT` enabled witness the
guarantee (the Debug cells G2 and G8), and a release build has no generation check, which the drain makes unnecessary
(research Risk R24 (f)). Wrap-around at `UINT32_MAX` is harmless: the drain leaves no work of an older generation
alive at any install, so the equality assertion never compares against a stale generation that the wrap could
match. The acceptor needs none: `attach_accepted_transport` runs once per Session.

## Reconnect drain — `drive_reconnect` (changed) and `inbound_depth_` (new)

- `inbound_depth_`: `std::uint32_t`, raised by an RAII holder declared as `on_inbound_active_`'s first statement,
  lowered at its every exit, an exception and frame destruction included (not in `on_inbound_frame`, a non-coroutine
  forwarder); reaching 0 calls `replay_gate_.wake_all()` (on a resumed exit; a frame destroyed unresumed falls under
  research D1's lifetime rule, after which nothing runs). Precondition: the Session outlives the inbound frame (the
  holder's destructor touches `inbound_depth_` and the gate), as for the chain's existing lock guards.
- Reader fence, tested after the closing/closed/stopped test (`session_already_closed`):
  `cfg_.role == session_role::acceptor || (cfg_.engine_managed && live_transport_shared_() != nullptr)` →
  `session_already_open`, nothing changes (the Engine's read pump owns that transport; the acceptor disjunct is an
  erratum the owner confirmed 2026-11-15). Disclosed false positive (B-103-9): a standalone initiator that sets
  `engine_managed` to defer its connect is treated as Engine-owned, so its second call with a transport installed is
  refused. Standalone initiator precondition: the caller's reader has left `live_transport()` before the call and
  starts on the new one after it (the same precondition keeps the old connection's frames from being fed during the
  call, on which `emit_initiator_logon_`'s exclusion rests, research D5′.4).
- `reconnect_in_flight_`: `bool`, set at `drive_reconnect`'s entry, cleared by RAII; a second call returns
  `session_already_open`.
- Quiescent ⇔ `replay_gate_.shared == 0 && inbound_depth_ == 0` (no emitter outside the inbound chain between its
  number peek and the return of its `store_then_emit`, and no inbound chain running), then, after the drain's
  `root_cancel_.emit(total)`, `bound_counter == 0 && liveness_counter_ == 0` (polled by post).
- `drive_reconnect`: single flight and the callback post; refuse if closing, closed or stopping (`session_already_closed`); then the reader fence above (`session_already_open`); from Active or LogonReceived
  record `Disconnected`; wait until quiescent on the gate's wake (bounded by `logout_disconnect_timeout_ms`, then the
  old transport is force-closed and the wait continues); re-test the refusal after every wake; then connect and
  install. The install is the commit point: with no suspension before it, it refuses on closing, closed or stop,
  closes the new transport and installs nothing (`session_already_closed`). It asserts quiescence. From a
  callback: one post first. Cancellation reaching backoff, connect or handshake: fixpp#585, unchanged. Caller cancellation → `dispatch_aborted`, nothing installed, state
  `Disconnected`, cancellation left pending.
- Endings, in the order the operation tests them (research D1):

  | Ending | Result | State it leaves |
  |---|---|---|
  | a second call while one is in flight (single flight) | `session_already_open` | unchanged; nothing connected |
  | closing, closed or stopping (step 1) | `session_already_closed` | unchanged; nothing installed |
  | the reader fence (step 1a) | `session_already_open` | unchanged; the installed transport untouched |
  | a `close()` or stop during the drain (re-test after a wake) | `session_already_closed` | `Disconnected` (if the drain started from Active or LogonReceived); nothing installed |
  | the caller's own cancellation of the drain's wait | `dispatch_aborted` | `Disconnected`; nothing installed; the cancellation left pending |
  | a cancellation reaching backoff, connect or handshake | fixpp#585's result, unchanged (`transport_connect_cancelled`) | as today |
  | a refused install at the commit point | `session_already_closed` | `Disconnected`; the new transport closed; nothing installed, nothing cleared |
  | installed | success | `LogonSent`, then the new connection's Logon exchange |

- Invariant: no frame numbered or claimed on one connection is written on a later one. "Claimed" is the bound's
  acknowledgement after its `pending → acking` claim; every other Logout that holds `logout_in_flight_` is numbered
  inside a shared hold (`run_logout_phase1`) or inside the inbound chain (`inbound_depth_`), so the quiescence
  condition waits for every holder.

## Pending-Logout mark — `peer_logout_` (new)

Fields: `seq` (the too-high Logout's MsgSeqNum S), `at` (arrival time), `phase ∈ {none, pending, acking}`. The bound
coroutine owns its captured `gen`; the session owns `peer_logout_wake_` (published by the bound, cleared in the
bound's body when its T race ends; no exit guard clears it, D4.4) and the join counter (a shared counter, incremented before `co_spawn`, decremented by
the bound's exit guard, which touches only that counter; `close()` and the reconnect drain poll it by post).

## Logout owner — `logout_in_flight_` (new, Gate A round 2; held at the leaf before loop 2)

`bool`, tested and set synchronously, before any stamp, number peek or suspension, by the two Logout leaves of an
established session: `inbound_logout_leaf_` (every Active inbound-chain Logout and the bound's acknowledgement:
in-sequence acknowledgement, gap deadline, SendingTime-fatal, PossDup Arm D, Arm B, T = 0 acknowledgement, the
bound) and `run_logout_phase1`; cleared only on the edge into `Disconnected` (`record_state_transition_` when it records
`Disconnected`: research D4.6 "The clear" shows the clear is sound whether that clears on a transition only or on
every `Disconnected` record; the implementer picks one and tasks T100 records which), so it spans
the whole ending, the in-sequence acknowledgement's `fromAdmin`, best-effort Reject and inbound persist included;
`install_reconnected_transport` asserts it clear. `inbound_logout_leaf_` also skips when the session is no longer
Active (tested at entry), so the leaf sets the owner only in Active (`run_logout_phase1` may also set it in
LogonReceived). Found set (or not Active): `inbound_logout_leaf_`
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
  `store_then_emit` returns. Fast path: no await, no allocation. The set is the one research D5′.4's census recipe
  derives (re-run at tasks T014 (c) and T166); where this list and the census differ, the census governs and the
  difference stops for a decision.
- Executor precondition: every acquirer's coroutine runs on the session's executor (`send()`'s documented
  "session-strand" contract in session.hpp; the session's own coroutines are spawned there), so the lazy timer the
  first waiter builds on `co_await this_coro::executor` binds to the session's executor, and each waiter resumes on
  its own coroutine's executor. The Debug strand assertion at every acquire enforces it where the executor is a
  strand; under `direct_executor` the attestation is the caller's, as for every strand-confined member.
- Exclusive hold: `replay_outbound_range_` from entry (before the store tail read) to every ending (RAII); waits
  for `shared == 0` first.
- Wake sources (the one normative list; research D5′ and the amendment's census point here): exclusive release,
  the last shared release whenever a waiter exists (the walk, or the reconnect drain), `inbound_depth_` reaching 0,
  the `Disconnected` edge, engine stop (`note_engine_stop_`), and `close()`, immediately after its first commit of
  `state_ = lifecycle::closing` (after the R-4 post when taken; no suspension between), not again when joining.
  There is no install wake.
- A contended wait: the pre-check sits in the waiting frame, or the wait is awaited through the split idiom (D5′.2).
  A wait its caller's cancellation ends returns `dispatch_aborted`, state unchanged, no number consumed, the
  caller's cancellation left pending.
- Guarantee: no outbound number is assigned during an answer; frames numbered before it are written before its
  first frame.

## Frame header — `detail::FrameHeader::begin_seqno_present` (new internal field)

Set when tag 7 is present; distinguishes an absent BeginSeqNo (Reject 373=1, 371=7) from 7=0 (answered as 7=1).

## C-ABI version — `FIXPP_C_ABI_VERSION_MINOR` 16 → 17 (BREAKING, Article X §7)

No symbol, type or error code changes; the documented default result changes.
