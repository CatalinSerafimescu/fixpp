# Contract: session wire endings changed by 104 (one condition, one observable ending)

Decisions: [research.md](../research.md) D1–D7. Notation:
- N = NextNumIn before the frame, M = the frame's MsgSeqNum(34), N_out = fixpp's next outbound number before the
  frame.
- T = `logout_disconnect_timeout_ms`.
- "rol" = `reset_on_logout`, "rod" = `reset_on_disconnect`.
- "reset" = the 141=Y reset unit `run_reset_unit_(1, 1)` (093 contract C-6): one `MessageStore::reset_to`
  operation, after which both numbers are 1 in the manager and the store. The teardown's reset is
  `reset_seqnums_to_one_durable` (`MessageStore::reset`); a cell tells the two apart by which store call it sees.
- "the marker" = the private member `logout_seen_`, whose name is kept and whose type and contract 104 change
  (data-model "`logout_seen_`"): `none`, `pending` (an awaited read-pump frame of a receipt arm is between its
  receipt and its reset point), `reached` (this connection's receipt reached its reset point).
- "the 103 mark" = `peer_logout_.phase` (`none` / `pending` / `acking`), 103's state of a too-high Logout's bound.

## Part 1 — the reset under `reset_on_logout` (supersedes 024 C3.1 by name)

**RT-1 (the trigger; supersedes 024 `contracts/reset-knobs.md` C3.1, "either direction").**
- With rol set, the sequence numbers are reset to 1 when, and only when, a Logout **received** from the peer is
  processed as far as its reset point (RR-1). The receipt arms are:
  - **A** — `on_inbound_active_logout_`: a request in Active, or a Logout in LogonReceived;
  - **L** — `on_inbound_logout_sent_`: the response to fixpp's own Logout, in LogoutSent;
  - **B** — `acknowledge_peer_logout_`: a too-high Logout (B-103-7), at its immediate acknowledgement or the
    bound's (`run_peer_logout_bound_`);
  - **S** — `on_inbound_logon_sent_`: a Logout received in LogonSent, the peer refusing fixpp's Logon (RU-2, new).
- A Logout received in NotConnected (acceptor, before any Logon) is not a receipt arm and never resets (RU-2;
  QuickFIX C++ `validLogonState` is false there).
- A Logout fixpp **sends** never triggers it, whether answered or not: `close(graceful)`'s phase 1, every
  error-path Logout and 104's too-low Logout.

**RR-1 (the reset point; RU-1).** Each receipt arm has one reset point. The reset is made if and only if the arm
reaches it. Processing that ends before it does not reset under rol: a validation failure, a `fromAdmin` throw or
reject, a late parse failure, a failed inbound persist, a refused reply (its `toAdmin` throw, its
`assign_outbound` failure, a cancelled store call), a gate refusal, a too-high Logout's bound that ends
unacknowledged, or a receipt still short of its reset point when `close()` could not wait for it (RR-2 item 4). Only rod then resets, at teardown. There is no teardown backstop and no reconnect-Logon backstop
under rol.

| Arm | Reset point: reached when |
|---|---|
| A | the reply Logout has been attempted (Active only: `inbound_logout_leaf_` returned success, which includes a Logout that did not fit, whose store call failed, or whose write failed), the `sequence_numbers_reset` event, `fromAdmin` returned without a throw, a reject or a late parse failure, and `persist_inbound_advance_` succeeded |
| L | the 35=5 frame passed the arm's garble and fault guards |
| B | its reply has been attempted (as A's reply) and the event emitted |
| S | the 35=5 frame passed the arm's guards ahead of `interpret_logon` (garble, `logon_arm_superseded_`, fault, 099's establishment refusal; the validate gate exempts 35=5), and on an mTLS session the live identity is authorised for the CompID (research D1.2) |

**RR-2 (one owner; RU-1's construction, as ruled 2026-11-17; ownership by construction A, owner 2026-11-18).**
Once a reset point is reached the reset is completed, and nothing that races it can lose it or run it twice. A
receipt that has not reached its reset point is never reset under rol, by anyone.

*The invariants the construction rests on.* Each is read from source at `3d04c544`. Each is stated here once, and
the other documents name it. INV-T and INV-X are also asserted in Debug builds. Plan phase 1 opens by running
those asserts (the settling experiment), and phase 2 re-runs it once the marker's assert exists.
- **INV-P (one awaited receipt frame at a time).** The read pump awaits each frame to its end before it delivers the
  next. The anchors are the `co_await session.on_inbound_frame` sites in `src/session/engine.cpp`
  (`run_read_pump`'s delivery loops, and the acceptor's first-frame delivery, awaited before `run_read_pump`
  starts).
  Re-derive them with `git grep -n "on_inbound_frame(" -- src/session/engine.cpp`. The `Session` contract states the
  same rule (`FIXPP_ASSERT_INBOUND_REPLY_SLOT_FREE`, "overlapping on_inbound_frame() calls on one Session").
- **INV-M (one bound per connection).** Once the 103 mark leaves `pending`, the spawn branch of
  `on_inbound_active_too_high_logout_` cannot be reached for the rest of that connection.
  - The mark leaves `pending` through a claim: `close()` (`claim_peer_logout_none_` at its first commit),
    `note_engine_stop_`, or a pump frame that calls `inbound_logout_leaf_` in the same turn (the in-sequence Logout
    in `on_inbound_active_logout_`, `resend_gap_deadline_`, and Arm B's too-low branch in
    `on_inbound_active_out_of_sequence_`). It also leaves through the bound itself: its claim refusal (with
    `logout_in_flight_` set), its `acking` (which ends in Disconnected), or its exception handler (which records
    Disconnected).
  - The leaf sets `logout_in_flight_`, and the only place that clears it is `record_state_transition_`'s
    Disconnected edge. After that edge no frame reaches the Active arm.
  - The spawn branch is guarded by `logout_in_flight_ || closing_or_stopped_()`.
  - `install_reconnected_transport` asserts that the bound's counter is 0 before the new connection starts.
- **INV-T (drain test to arm entry in one strand turn).** A 35=5 frame goes from `on_inbound_active_`'s `acking`
  drain test ("while the bound acknowledges a too-high Logout") to the entry of its receipt arm with no suspension.
  - The only awaits on those paths are `seqnum_mgr_.check_inbound`, whose mutex is granted inline on the strand
    (research R1), and the PossDup stage, which does not suspend on its pass path.
  - On route (b) there is also `resend_gap_deadline_`, whose path when the deadline has not expired returns with no
    suspension.
  - The `this_coro` cancellation awaits do not suspend either.
  - The `persist_inbound_advance_` awaits on the way sit on the Reject, Heartbeat and application paths, not on
    35=5.
  - One step is read and not run: asio starts an awaited child coroutine inline. The Debug assert below detects a
    suspension in the window only if it lasts long enough for the bound's wake to finish (at least two posted
    turns; plan phase 1 derives the count). A shorter suspension leaves the 103 mark `pending` at the assert, so it
    is covered by this reading only.
- **INV-X (an `acking` bound excludes a pump receipt arm between its receipt and its reset point).** The two never
  overlap.
  - A pump 35=5 frame that meets an `acking` bound is drained before any arm (INV-T).
  - A bound cannot enter `acking` while a pump receipt arm is between its receipt and its reset point:
    - A in Active claims the 103 mark at its entry and sets `logout_in_flight_` in the same turn (INV-M).
    - A in LogonReceived, L (LogoutSent) and S (LogonSent) run in no state from which the bound can claim. The bound's
      "live" test requires Active, and no bound is spawned before Active (research D1, "Read from source: a
      too-high Logout in LogonReceived").
    - B's immediate acknowledgement is taken only when no bound can claim: T = 0, no clock, a Logout in flight, or
      closing or stopped.
- **Debug asserts (plan phases 1 and 2):**
  - `peer_logout_.phase != acking` at the entry of `on_inbound_active_logout_` and of
    `on_inbound_active_too_high_logout_` (INV-T);
  - at a reset point, the value item 5 names (INV-X).

1. With rol set, only an awaited read-pump frame sets the marker to `pending`, at its receipt:
   - A at its entry (`on_inbound_active_logout_`);
   - L and S at their 35=5 branch, after the garble guard (`on_inbound_logout_sent_`'s 35=5 test, which also
     follows the fault guard; `on_inbound_logon_sent_`'s 35=5 branch ahead of `interpret_logon`). Neither arm
     suspends between that branch and its reset point (the guards and S's `authorize` call are synchronous), so
     the marker reaches the reset point in the same strand turn. Any other frame in L or S, the Logon
     acknowledgement included, leaves the marker untouched;
   - B's immediate-acknowledgement branch of `on_inbound_active_too_high_logout_`.

   B's spawn branch and its ignored branch ("a second too-high Logout while the mark is set") write nothing to the
   marker. A detached bound's "receipt owed" state is the 103 mark (`pending` / `acking`), which 103 already owns
   alone. With rol off the marker stays `none`: nothing is reset, waited for or refused.
2. **One owner per value.** By INV-P, at most one pump frame holds `pending` at a time, and it is that frame's own.
   By INV-X no bound runs its reset point beside it.
   - **A pump frame's `pending`.** It leaves `pending` exactly once:
     - to `reached` at the reset point, or by `close()`'s close-out (item 4);
     - or to `none` at any exit before the reset point.

     The frame clears it before any `close()` it calls itself (`close_on_late_parse_failure_`; the throw paths), so
     `close()` never waits for its own caller. On every other exit, return or exception, a frame-local owner sets
     `none` if the marker is still `pending`. 103 accepts a Session-touching destructor in an awaited frame. A
     `close()` close-out to `reached` is never undone by that owner.
   - **The bound** (`run_peer_logout_bound_`) writes the marker exactly once: at its reset point, in `acking`, inside
     `acknowledge_peer_logout_`. It moves `none` → `reached` and starts the reset unit in the same strand turn
     (item 3), asserting that the marker was `none`. Every other exit of the bound writes nothing to the marker:
     the claimed, stopped, superseded and drained exits, its exception handler, and a frame destroyed unresumed.
     Its receipt owed is the 103 mark, which those exits already clear or leave to a claim (O18).
3. **The reset point is one strand turn, and nothing in it throws.**
   - Immediately before the turn, the arm resets its thread's cancellation state (`reset_cancellation_state`,
     which completes inline and does not suspend). This drops a `terminal` or `all` emission already recorded in
     the arm's thread, for example one from inside the `onLogout` that RR-3 step 2 fires. Without the reset, that
     emission makes `FIXPP_DETAIL_CO_AWAIT_LOCK`'s first part throw in `run_reset_unit_`'s first manager call.
   - The marker's move to `reached` (item 5) and the start of the unit follow with no suspension and no callback
     between them. The transition is written immediately before `run_reset_unit_`. Its manager calls grant inline (093 C-6
     step 3; research R1), and it sets `reset_unit_in_flight_` before its first suspension. With no store, the unit
     has no suspension at all.
   - Within the turn nothing can record a cancellation. So no other coroutine on the strand observes a `reached`
     receipt whose unit has not started (suspension), and no exception leaves the marker `reached` with no reset
     (throws).
4. `close()` (any mode) waits after its joins and before its teardown predicate, while the marker is `pending` or
   `reset_unit_in_flight_` is set. It needs a clock (as 093's wait does) and is bounded by `logon_timeout_ms`. On
   expiry it emits `session_event_close_reset_wait_expired`.
   - **Route B needs nothing new here.** `close()` claims the 103 mark at its first commit
     (`claim_peer_logout_none_`). Its joins include 103's bound join, an unbounded poll on the bound's counter. So a
     bound that was not yet `acking` exits unacknowledged (O18), and an `acking` one runs to its end before the wait
     begins, its unit included, because the bound awaits it. The wait therefore sees only a pump frame's `pending`, or
     a unit in flight: a pump arm's, or a Logon arm's. `close()` never closes out a bound's receipt.
   - **The wait is a poll by post**, like the liveness and bound joins just above it. Each posted turn re-reads
     both terms and compares `effective_clock_->steady_now()` with the bound, and the condition is tested before the
     first post, so a `close()` with nothing outstanding makes no pass.
   - The poll needs no waker, so it cannot miss an exit from the condition: `pending` → `none` at an exit before
     the reset point, `run_reset_unit_`'s early returns (no store, a manager failure), and the unit's completion.
     So `close()` returns as soon as the condition is false, and never emits the expiry event for a receipt that
     ended. 093's event wait, whose one waker was the unit's completion, is replaced, and `reset_unit_wake_` is
     deleted (data-model; why: research D1 item 4).
   - Cost: one posted turn per pass while a receipt or unit is outstanding, in `close()` only, bounded by
     `logon_timeout_ms`. The worst case is a receipt held in a store call, for example a `FileStore` write. The
     poll then keeps one handler ready on the session's executor for up to `logon_timeout_ms`. Why 093's rejection
     of a poll does not apply here: research D1 item 4.
   - **A receipt still `pending` after the wait** (it expired, or there was no clock to wait with) never reached
     its reset point. It is processing that ended before the reset point (RR-1): **no reset under rol**. The
     close-out sets the marker to `reached`, so that when the arm resumes it runs no unit and issues no further
     store call (item 5). Under rod the teardown resets as it always does.
   - **A receipt that is `reached` but whose unit has not started** is the one case the teardown would reset
     itself, once. By item 3 it has no input. So the teardown predicate,
     `!teardown_reset_done_ && (rod || (rol && <reached, unit not started>))`, is coded as its first term only:
     item 3 implements the second. A change that splits the turn must add that term, and a way to observe "unit not
     started", which the three-valued marker does not have. Cell R-1u's mutant opens the window (a suspension
     between `reached` and the unit) and goes RED, which is the evidence that item 3 is load-bearing.
   - **Under `Engine::stop()`, the wait is the role-loop join, not this poll.** Re-derive the order from
     `Engine::stop`'s step comments in `src/session/engine.cpp`; the live row `L-563-4` states it ("`stop()`
     reaches `Session::close` only after it has joined the role loops").
     - Stop calls `close(terminal)` only at its step 4, after its step 3 join of the role loops
       (`outstanding_counter_`). The role loops await the read pump inline, the pump awaits each frame to its end
       (INV-P), and the frame awaits its unit. So step 3 waits for a pump frame's receipt arm. By step 4 no pump
       frame's receipt is `pending` and no unit is in flight: this poll makes no pass under stop and never closes
       a receipt out. The pump's own close after the arm, when its next read meets the transport stop's step 2
       closed, runs after the frame too and sees the same state.
     - Stop's `total` emission does not release a held arm: after `check_inbound`'s lock the arm's filter is
       terminal-only (research R1), and a `FileStore` call that holds its lock runs its job to its end
       (`B-598-1`).
     - So the arm owns its ending under stop through the join: it reaches its reset point and resets once, or exits
       before it and does not. There is no close-out and no `session_event_close_reset_wait_expired`.
     - Stop's wait has no bound and no clock condition (an Engine session always has a clock:
       `validate_engine_config` at `Engine::start`, and `Session::open`'s fallback from a null `clock_override`
       to `EngineConfig::clock`). It ends when the held store call returns (`B-598-1`; `L-563-7` condition (2)).
       A store call that never completes hangs `stop()`: `L-093-12`'s condition, now also met by a receipt's reply
       store and inbound persist. 104 widens nothing here: on the base a pump frame held in any store call already
       holds step 3.
     - A bound is outside the join: it is spawned detached with no cancellation slot. A `pending` bound is claimed
       by `note_engine_stop_` and exits unacknowledged (O18); an `acking` bound is joined by step 4's `close()`,
       through 103's bound join.
5. At its reset point an arm finds one of the values below. Any other value fails a Debug assert. It is not a
   third case.
   - **A pump frame**, which is A, L, S, or B's immediate acknowledgement:
     - it finds its own `pending`, sets `reached` and runs the unit (item 3);
     - or it finds `reached`, because `close()` closed the receipt out (item 4), and runs no unit.
   - **The bound** finds `none`, sets `reached` and runs the unit (item 2).
   - B's two callers share the reset point in `acknowledge_peer_logout_`. They are told apart by the 103 mark: it is
     `acking` only for the bound (INV-X).
   - **No store call is issued after the close-out.** RR-4's boundaries refuse each store call issued while the
     marker is `reached`.
   - **A store call already issued runs to its end.** `close()` cancels nothing on the arm's path (`root_cancel_`
     reaches only what is bound to it), and a `FileStore` call that holds its lock runs its write to its end
     whatever arrives (`B-598-1`). So what that call writes lands after `close()` returned, with its own ending:
     Outcomes O13-r and O13-p.
   - The arm's other work also runs after `close()` returned: the event, `fromAdmin`, and the attempt at a Reject
     that RR-4 then refuses. That is fixpp#597 ("close() publishes closed_drained while a
     store call of another coroutine of the session is in flight", open). 104 neither widens nor closes it.
6. `install_reconnected_transport` sets `none`. A received Logout never resets a later connection.

**RR-3 (order at the reset point; RU-3).** In every receipt arm:
1. the reply Logout, if one is owed (A in Active, B);
2. record `Disconnected` (where the arm has not), which stops every emitter from numbering in the reset space
   (RR-4) and drains further inbound frames;
3. the reset point, in one strand turn (RR-2 item 3): the cancellation state reset, the marker to `reached` (RR-2
   item 5) and the unit; then
   `reset_cancellation_state(enable_total_cancellation{})` as `on_inbound_logon_sent_` does after its unit; a
   manager or store error is RT-4's;
4. L only: `logout_confirmed_` and phase 1's wake;
5. the transport closes as today (the pump's EOF, `close()`, or `Engine::stop()`).

A `close()` started inside the `onLogout` that step 2 fires finds a pump frame's marker `pending`. It waits one
post first (B-563-2), so the arm reaches step 3 before it, and the wait then covers the unit. Where that post does not move
the close off the callback's stack (B-563-2's R-7 setup), the close's own wait still covers the receipt, and with
no clock the receipt is not reset (item 4, a receipt still `pending`). On the bound's route the marker is `none`
until step 3, and `close()`'s bound join covers the receipt and its unit (RR-2 item 4).

`onLogout` fires on the edge into Disconnected at step 2, so it observes the pre-reset numbers (A, B; L's
`onLogout` fired earlier, on phase 1's Active → LogoutSent edge). This is a listed departure (spec "Departures
from QuickFIX"): QuickFIX C++ and QuickFIX/J fire `onLogout` in `disconnect()`, after `m_state.reset`. Nothing on
the wire differs, because QuickFIX resets and then disconnects synchronously and fixpp sends nothing after the
reply. This closes the window in which the numbers were (1, 1) while the session was still Active; it does not
widen 103's FR-021.

**RR-4 (what stops at step 2, read from source).**
- **The liveness loop** (`run_liveness_loop`) tests `fsm_state_ != Active` after each sleep, after the Heartbeat's
  emit and before and after the TestRequest's grace sleep. Between the post-sleep test and `assign_outbound` it
  suspends only on the replay gate's wait, after which it re-tests the state. So it numbers nothing after step 2.
- **`Session::send`** refuses on `fsm_state_ != Active`, at entry and after every replay-gate wait.
- **The resend answer** (`replay_outbound_range_`) returns `dispatch_aborted` on `Disconnected` after its wait for
  the shared holds. The bound does not claim while a walk holds the gate exclusively.
- **The inbound pump** delivers a frame after step 2 to the Disconnected row, which drains it.
- **Not stopped by the state:** an emitter already past its state test holds a pre-reset number. Two cases:
  - its store call was issued in the same strand turn as its `assign_outbound`, before the unit's `reset_to`, and
    D-OUT-10 in `store_then_emit` refuses its transmit once the manager is lowered (research R1 states the
    store-order condition that makes this hold, and its phase-0 check);
  - on route B, a pump-delivered Active-arm frame whose processing was already in flight when the detached bound
    recorded `Disconnected`, and whose later leaves or inbound persist are issued after the unit; and on any
    route, the store calls the receipt arm issues after `close()` closed the receipt out (RR-2 item 4).
    **By construction:** `store_then_emit` (before its store call) and `persist_inbound_advance_` (after its
    `store_is_persistent_` return, before its store call) refuse while the marker is `reached`. `store_then_emit`
    takes the number back, as D-OUT-1 does for a cancelled store call, and returns `dispatch_aborted`;
    `persist_inbound_advance_` returns success with no write. Each boundary is one byte test. It runs on every
    outbound frame that has a store call, and on every inbound persist to a persistent store; the in-memory inbound
    path returns before the test (its cost: plan "Performance Goals"). A test made before the call cannot govern a call already issued
    when the marker became `reached`; that call runs to its end (RR-2 item 5). The end state equals QuickFIX's,
    whose reset after the Logout discards every earlier increment. The receipt unit and the teardown's reset do not
    pass through either boundary.

**RT-4 (store failure at the reset).** A manager or store failure of the receipt unit is logged. The arm's ending
is unchanged (Disconnected, and the return value it has without the failure). There is no retry under rol; under
rod the teardown reset still runs. On a refusing persistent store the stored numbers stay pre-reset (L-104-2).

**RT-5 (`close()`).** RR-2 item 4's wait replaces the wait inside the predicate block, whatever the knobs, so a
`close()` that finds a Logon-arm 141=Y unit in flight now waits for it even with no knob set (B-104-2). 024 C5.1
is kept as written: at most one store reset per teardown (`teardown_reset_done_`). With rol and rod both set, a
received Logout makes two resets, one at the reset point and one at teardown; both numbers end at 1 (D2).

**RT-6 (unchanged).** rod (024 C4.1, C4.2); the not-sent and cut Logouts (no reset).

### Outcomes (RU-1): every receipt arm × every way its processing can end

"no reset" means under rol alone; rod still resets at teardown. Engine columns are read from source (research D1).
A cell id is given where the row can be driven; its mutant is in research D10.

| # | Condition | A (Active / LogonReceived) | L (LogoutSent) | B (too-high ack) | S (LogonSent) | QuickFIX | Cell |
|---|---|---|---|---|---|---|---|
| O1 | normal | reply, persist, Disconnected, reset at the arm; wire: the reply only | Disconnected, reset at the arm, then phase 1 wakes; wire: nothing | reply, Disconnected, reset at the arm | Disconnected, reset at the arm; wire: nothing (QuickFIX replies) | reset at receipt, all three | R-1c, R-1d, R-1e, R-1o |
| O2 | `fromAdmin` throws | reply already sent; Disconnected; no reset | n/a (no `fromAdmin`) | n/a | n/a | no reset (QFC, QFJ: `verify` → `fromCallback` throws before the reply) | R-1g |
| O3 | `fromAdmin` rejects | reply, Reject, Disconnected; no reset | n/a | n/a | n/a | no reset (QFC, QFJ: the reject exception leaves `nextLogout`; QFGo: `processReject`) | R-1g |
| O4 | late parse failure | marker cleared, then `close(terminal)`; no reset | n/a | n/a | n/a | no reset (not a processed Logout) | R-1g |
| O5 | reply does not fit 256 bytes | no reply; reset point reached; reset | n/a | as A | n/a | n/a (no fixed buffer) | R-1n |
| O6 | reply `toAdmin` throws | leaf records Disconnected, `app_callback_threw`; no reset | n/a | as A | n/a | QFC: no reset (propagates out of `sendRaw`); QFJ: resets (`sendRaw` catches `Throwable` from `toAdmin`, then sends); QFGo: no throw path | R-1n |
| O7 | reply `assign_outbound` fails | leaf records Disconnected, the error; no reset | n/a | as A | n/a | n/a | R-1n |
| O8 | reply store fails (persistent) | not transmitted; reset point reached; reset | n/a | as A | n/a | reset (QFC, QFJ catch the store's `IOException` in `sendRaw` and continue) | R-1n |
| O9 | reply store call cancelled: `store_cancelled` returned, or `operation_aborted` thrown (D-OUT-2 gives both timings the one block after the `try`) | the number taken back (D-OUT-1); Disconnected; no frame; `operation_aborted` unwinds the arm and its owner clears the marker (RR-2 item 2); no reset; `close()` is not held | n/a | immediate acknowledgement: as A. The bound: 103's handler ends it closed; the marker untouched (`none`); no reset | n/a | n/a | R-1v (the returned timing, which reaches the shared block) |
| O10 | reply write fails | stored, not on the wire; reset | n/a | as A | n/a | reset (QFC `send` returns false; `nextLogout` continues) | R-1n |
| O11 | inbound persist fails | Disconnected, the error; no reset | n/a | n/a | n/a | no reset (QFC, QFJ: `incrNextTargetMsgSeqNum` throws `IOException`) | R-1n |
| O12 | `close()` during, before the reset point, with a clock | `close()` waits; the arm reaches its reset point and resets once, or exits and does not (with no store, the unit sets no in-flight flag); `close()` returns as soon as the marker has left `pending` with no unit in flight, not at `logon_timeout_ms`, and emits no `close_reset_wait_expired` | no input: L does not suspend before its reset point (research R1) | immediate acknowledgement: as A. The bound, not yet `acking`: claimed at `close()`'s first commit, no reset (O18). Already `acking`: `close()`'s bound join waits for it, so it reaches its reset point and its unit ends before the wait begins (RR-2 item 4), one reset | no input: S does not suspend before its reset point; a frame that arrives after `close()` began meets `logon_arm_superseded_` and acts on nothing, no reset | n/a (QuickFIX serialises receipt and disconnect) | R-1k, R-1z (A beside a claimed bound), R-1f, R-1w |
| O13-r | `close()` during, before the reset point, wait expired or no clock, the arm held in its reply's store call | no reset under rol (the receipt did not reach its reset point); the marker is closed out to `reached`. The held call completes after `close()` returned (RR-2 item 5): the reply is stored. On a live transport it is not transmitted, because `close()` has run `close_async()` and `write_gate_.cancel_and_drain()`, so the write fails with `dispatch_aborted`; a config-time `transport_send` sink transmits it (as `B-563-5`). The persist is refused (RR-4), so the store's inbound stays N while the manager's is N+1 (`check_inbound`, before the arm). No unit. The event and `fromAdmin` run after `close()` returned (fixpp#597) | no input (as O12) | the bound: no input, because `close()`'s bound join precedes the wait, so it never closes out a bound's receipt (RR-2 item 4). Immediate acknowledgement (no clock is one of its conditions): as A, without the persist and `fromAdmin` (B has neither) | no input (as O12) | n/a | R-1l (`HeldInReplyStore`) |
| O13-p | as O13-r, the arm held in its inbound persist | no reset under rol; the marker is closed out to `reached`; the held persist completes after `close()` returned and lands (store inbound N+1); no unit (fixpp#597, as O13-r) | no input (as O12) | n/a (B has no persist) | no input (as O12) | n/a | R-1l (`HeldInPersist`) |
| O13a | `reached`, unit not started, at the teardown | the teardown resets once; no input by construction (RR-2 item 3) | as A | as A | as A | n/a | R-1u (its mutant opens the window) |
| O14 | `close()` during the unit | `close()` waits for the unit (clock); without a clock it returns while the unit's store operation completes on the strand (093's no-clock case, inside fixpp#597) | as A | immediate acknowledgement: as A. The bound: `close()`'s bound join waits for it, so for its unit, with or without a clock | as A | n/a | R-1j |
| O15 | `Engine::stop()` during | one reset or none, decided by the arm; no close-out. Stop's role-loop join (its step 3, before its step 4 `close()`; RR-2 item 4, `L-563-4`) waits for the arm, past `logon_timeout_ms` (the session always has a clock under the Engine). The arm reaches its reset point and resets once, or exits before it (O2–O11) and does not. A held reply is stored and not transmitted, because step 2 closed the transport (O10). A unit in flight is waited for (`L-093-12`). No `close_reset_wait_expired`. Stop returns after the arm (`L-093-12`, `L-563-4`). Its emission is total, which the arm's filter refuses after its first lock, and no Engine caller emits `terminal` or `all` on the arm's slot. A direct caller's `terminal` emission takes O9 at a store call and RR-2 item 3 at the reset point | L does not suspend before its reset point (research R1); its unit runs inside the pump frame, so the join waits for it (`L-093-12`): one reset | immediate acknowledgement: as A. The bound: as O12 and O18 through step 4's `close()` (a `pending` bound claimed by `note_engine_stop_`, no reset; an `acking` one joined, one reset) | a frame that arrives after stop's step 1 meets `logon_arm_superseded_` (the engine-stop flag) and acts on nothing, no reset; a unit already started runs inside the pump frame, and the join waits for it | n/a | R-1t, R-1x |
| O16 | store reset fails | logged; Disconnected; no retry (RT-4) | as A | as A | as A | QFGo: logged, then `latentState` | R-1i |
| O17 | grace timer of `close(graceful)` expires during L's unit | n/a | phase 1 returns `session_logout_timeout`; the timer branch also force-closes the transport (`live->close()`) before phase 2; the wire is the same (nothing more is sent); `close()` waits for the unit | n/a | n/a | n/a | R-1j |
| O18 | bound ends unacknowledged (claim by `close()`, stop, another ending's Disconnected, reconnect drain, a Logout in flight) | n/a | n/a | the marker untouched (`none`): the bound wrote nothing, and 103 clears or claims its mark; no reset | n/a | QFC, QFJ: reset (they process a too-high Logout at receipt, `verify(logout, false, false)`) | R-1f |
| O18a | a second too-high Logout while the bound is `pending` (the ignored branch, either route) | n/a | n/a | the frame is ignored and writes nothing to the marker. The first bound's acknowledgement resets once (O1) | n/a | n/a (QuickFIX processed the first at receipt) | R-1y |
| O19 | mTLS identity not authorised | n/a | n/a | n/a | no reset; Disconnected | n/a | R-1o |
| O20 | Logout in NotConnected | — | — | — | — | no reset (QFC `validLogonState` false) | R-1o |

## Part 2 — endings per arm and input

| # | Arm / condition | Input | Wire from fixpp | Numbers after | Store | State |
|---|---|---|---|---|---|---|
| W1 | LogoutSent (fixpp's graceful Logout sent), rol set | no peer Logout; T expires | nothing more | as after the Logout's emit | unchanged by any reset | Disconnected; `close()` ok |
| W2 | same | transport drops (peer EOF) during the wait | nothing more | as W1 | as W1 | Disconnected; `close()` ok |
| W3 | same | peer Logout, before T | nothing more | `(1, 1)` before phase 1 wakes | one `reset_to(1, 1)`, state Disconnected, the transport open, before `close()`'s phase 2 | Disconnected |
| W4 | Active, rol set | peer Logout request, M = N | Logout reply at 34 = N_out | pre-reset at `onLogout`; `(1, 1)` after the arm | inbound persist, then `reset_to(1, 1)` with the state Disconnected and the transport open | Disconnected |
| W5 | Active, rol set, T > 0, a clock | peer Logout with M > N | ResendRequest (as B-103-7); when N passes M or at T, the acknowledgement Logout | as W4, after the acknowledgement | `reset_to(1, 1)` after the acknowledgement, state Disconnected | Disconnected |
| W6 | W5 pending, then `close()`, stop, the peer's EOF or a reconnect drain | — | as B-103-7 for that route | not reset by rol (O18); rod resets at teardown | no `reset_to` | as that route |
| W7 | W3 or W4 with rol **and** rod | — | as W3 / W4 | `(1, 1)` | two resets: the arm's `reset_to`, then the teardown's `reset` | Disconnected |
| W8 | W3 or W4, receipt reset fails | — | as W3 / W4 | manager `(1, 1)`; store pre-reset when the store refused | logged | Disconnected; arm's return unchanged |
| W9 | standalone initiator: W3/W4 reset at receipt, then `drive_reconnect`, numbers advance, then `close()` with rol only | — | the new Logon at the numbers as they stand | not reset by the new connection's teardown | no further reset | closed |
| W10 | LogonSent, rol set | peer Logout | nothing (QuickFIX replies with a Logout) | `(1, 1)` | `reset_to(1, 1)` | Disconnected |
| W11 | W10, rol off; or NotConnected with any knob | peer Logout | nothing | unchanged | no reset | Disconnected |
| U1 | LogonSent, lenient or unilateral, fixpp's Logon without 141=Y, N ≥ 1 | ack 141=Y, M = 1 | no second Logon; the next frame fixpp sends is 34=1 | inbound next 2, outbound next 1 | `reset_to(2, 1)`; event `by_peer_request = true` | Active |
| U2 | same | ack 141=Y, M > 1 | no second Logon; on the peer's next frame (34 > 1), ResendRequest 7=1, 16=0 | inbound next 1, outbound next 1 | `reset_to(1, 1)` | Active |
| U3 | U1 or U2 with 789 enabled and present, 789 = X | X > 1 | Logout "NextExpectedMsgSeqNum too high, expecting 1 but received X" | as after the reset, plus the Logout | reset, then the Logout stored | Disconnected |
| U4 | `bilateral_strict` or a knob-asked reset (fixpp's Logon carried 141=Y) | any ack | unchanged (today's `check_inbound` against N = 1) | unchanged | unchanged | unchanged |
| U4m | U1 or U2 on a direct mTLS initiator whose live identity is not authorised for the CompID | — | nothing | unchanged (no unit) | no `reset_to` | Disconnected |
| U5 | after U1 (outbound next 1, nothing sent) | ResendRequest at 34=2, 7=1, 16=0 | one GapFill 34=1, 43=Y, 122=52, 123=Y, 36=1 | inbound next 3 | inbound persist | Active |
| U6 | Active, any history | ResendRequest whose EndSeqNo ≥ N_out (999999 on FIX.4.0/4.1 included) | as 16=0: the walk to N_out − 1, and an empty-range GapFill's 36 = N_out (never EndSeqNo + 1). With BeginSeqNo B > N_out the GapFill is (34=B, 36=N_out), its NewSeqNo below its MsgSeqNum, QFC's identical frame (`nextResendRequest`'s clamp, then `generateRetransmits`' tail `generateSequenceReset(B, next)`) | unchanged | as today | Active |
| U7 | U1 or U2 on a persistent store that refuses the unit's `reset_to` | — | nothing after fixpp's Logon | the manager at the unit's targets (`run_reset_unit_` sets it before its store call) | pre-reset (the store refused) | Disconnected; the arm returns the store's error (030's persistent-fatal disposition, unchanged) |
| T1 | LogonSent, ack without 141=Y, M < N (43 any) | — | one Logout, 58 = "MsgSeqNum too low, expecting N but received M", 34 = N_out, no 1409, no 789; `toAdmin` called once before its number is taken; on an mTLS session only after the identity is authorised (T8) | N unchanged | the Logout stored as outbound; nothing inbound persisted | Disconnected |
| T2 | NotConnected (acceptor), Logon without 141=Y, M < N, not strict, `reset_on_logon` off (43 any) | — | as T1, 34 = the hydrated next outbound; only after both identity gates pass (T8) | as T1 | as T1 | Disconnected |
| T3 | T1/T2, Logout over 256 bytes (long CompIDs) | — | none (the near-bound CompIDs still emit T1's Logout) | as T1 | nothing | Disconnected; success |
| T4 | T1/T2, `toAdmin` throws | — | none | as T1 | nothing | Disconnected; `app_callback_threw` |
| T5 | T1/T2, `assign_outbound` fails | — | none | as T1 | nothing | Disconnected; that error |
| T6 | T1/T2, the Logout's store or write fails | — | persistent store failure: none; volatile store failure (024 I-07, logged then sent): T1's Logout; write failure: none on the wire, the Logout stored | as T1 | as the store left it | Disconnected; success |
| T7 | T1/T2 with rol set | — | as T1 | not reset by this Logout | no reset | Disconnected |
| T8 | T1/T2 on an mTLS session whose live identity is not authorised for the CompID; on the acceptor also a Logon whose credentials `authorize_logon` refuses | — | nothing (FIX-TC 1S(c): "Disconnect without sending a message") | as T1 | nothing | Disconnected, with the gate's own event |

**Precedence on one Logon frame** is each arm's code order (research D5), with one change (N-8): a too-low frame
runs the identity gates before its Logout. An earlier guard (validate gate, `interpret_logon`, posture,
SendingTime, `seq == 0`, and in NotConnected the strict refusal and the `reset_on_logon` reset) takes its own
ending. For a too-low frame the arm then runs the mTLS CompID-authorisation gate (both arms) and, on the acceptor,
`authorize_logon`, through the helpers that make today's checks, so the event, the one-shot `live_peer_id_`
consumption and the silent Disconnected are identical. Only then does it send the too-low Logout. The later
guards (1137, strict mismatch, the unit, 789) are not reached by a too-low frame.

**Unchanged and disclosed:**
- The too-low Logout leaves the transport open until EOF, `close()` or stop (B-103-10, L-103-5).
- The Logon arms' sequence check ignores `validate_sequence_numbers`.
- A strict acceptor's 141=Y Logon at 34 ≠ 1 (L-103-4).
- The acceptor twin of U1 is unreachable (research D7).
- The posture, strict and SendingTime refusals and the 1137 Reject still answer before the identity gates: a
  pre-existing departure from FIX-TC 1S(c) that 104 does not widen (L-104-3).

## Superseded by name (header-comment rule, issue #310)

- 024 `contracts/reset-knobs.md` **C3.1** ("either direction"): by RT-1 and RR-1. 024 **C5.1** is kept; its
  "single store reset" reads per teardown (erratum E-4).
- 027 `contracts/next-expected-msgseqnum.md` **C3**, the two-value end model's explicit-end case, and its required
  test "request `[10,20]`, store through 5": by U6 (B-104-5). The cell that pins `NewSeqNo=21`
  (`WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore`) is re-pinned to `NewSeqNo=6`, QuickFIX C++'s identical
  GapFill(34=10, 36=6).
- 093 `contracts/inbound-frame-dispositions.md` **C-6**'s `close()` bullet ("`close()` waits only when it is about
  to issue its teardown reset"), and the event-driven shape of that wait, whose one waker was the unit's
  completion: by RR-2 item 4 and RT-5 (B-104-2). B-093-11 gains the same pointer.
