# Contract: session wire endings changed by 104 (one condition, one observable ending)

Decisions: [research.md](../research.md) D1–D7 and D11 (the manager freeze, RR-5). Notation:
- N = NextNumIn before the frame, M = the frame's MsgSeqNum(34), N_out = fixpp's next outbound number before the
  frame.
- T = `logout_disconnect_timeout_ms`.
- "rol" = `reset_on_logout`, "rod" = `reset_on_disconnect`.
- "reset" = the 141=Y reset unit `run_reset_unit_(1, 1)` (093 contract C-6): one `MessageStore::reset_to`
  operation, after which both numbers are 1 in the manager and the store, and stay 1 in the manager until the
  connection ends: no writer but a reset writer changes them (RR-5). The teardown's reset is
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
`assign_outbound` failure, a cancelled store call), a gate refusal (arm S's mTLS CompID authorisation, O19; arms
A, L and B pass no identity gate), a too-high Logout's bound that ends
unacknowledged, or a receipt still short of its reset point when `close()` could not wait for it (RR-2 item 4). Only rod then resets, at teardown. There is no teardown backstop and no reconnect-Logon backstop
under rol.

| Arm | Reset point: reached when |
|---|---|
| A | the reply Logout has been attempted (Active only: `inbound_logout_leaf_` returned success, which includes a Logout that did not fit, whose store call failed, or whose write failed), the `sequence_numbers_reset` event, `fromAdmin` returned without a throw, a reject or a late parse failure, and `persist_inbound_advance_` succeeded |
| L | the 35=5 frame passed the arm's garble and fault guards. Its MsgSeqNum is not read, as in the base's LogoutSent "confirm" row: any 34 resets, and NextNumIn is neither advanced nor persisted before the unit (W3) |
| B | its reply has been attempted (as A's reply) and the event emitted |
| S | with rol set only: the 35=5 frame passed the arm's guards ahead of `interpret_logon` (garble, `logon_arm_superseded_`, fault, 099's establishment refusal; the validate gate exempts 35=5), and on an mTLS session the Logon path's mTLS gate passes (research D1 item 3): with `live_peer_id_` set it calls `authorize`, and with mTLS and no live identity it fails closed. With rol off the branch does not run (W11) |

**Arm S's branch and the knob (orchestrator ruling 2026-11-18, spec Clarifications "Session 2026-11-18 (checklist
audit; orchestrator ruling)").** The 35=5 branch in `on_inbound_logon_sent_` ahead of `interpret_logon`, and its mTLS
gate, run only when `reset_on_logout` is set.
- With rol off nothing changes from the base: `interpret_logon` refuses the frame as a non-Logon, the arm records
  `Disconnected` and returns success, the mTLS gate does not run (no `authorize` call is made) and no
  `session_event_compid_authorization_failed` is emitted, whatever the identity (W11).
- With rol set the branch runs, the mTLS gate first (design ruling 5, FIX-TC 1S(c)). The gate is the Logon path's,
  with both its arms:
  1. with `live_peer_id_` set, it calls `authorize`. On an initiator that identity is set only by
     `install_reconnected_transport`, after `reconnect_fsm.cpp`'s step 7 authorised it;
  2. with mTLS and no live identity, it fails closed with the same event and no `authorize` call. A direct
     initiator that reaches LogonSent through `open()` has no live identity and takes this arm; the
     direct-initiator cells drive it.

  An authorised identity reaches the reset point and resets once (RU-2, W10); an unauthorised one, or none,
  draws the gate's `session_event_compid_authorization_failed` and no reset (O19). Either way the arm ends `Disconnected`, returns
  success as the base's ending does, and sends no reply Logout (RU-2, DQ-9).

**RR-2 (one owner; RU-1's construction, as ruled 2026-11-17; ownership by construction A, owner 2026-11-18).**
Once a reset point is reached the reset is completed, and nothing that races it can lose it or run it twice. A
receipt that has not reached its reset point is never reset under rol, by anyone.

*The invariants the construction rests on.* Each is read from source at `3d04c544`, a bundle commit on this branch
whose `src`, `include` and `tests` equal the base's (check: `git diff --stat bf294f00 3d04c544 -- src include tests`
is empty; T001 checks the same for the head). Each is stated here once, and
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
  - Re-derive the claim sites with `git grep -n -e claim_peer_logout_none_ -e note_engine_stop_ -e
    'peer_logout_.phase = ' -- src/session/session.cpp include/fixpp/session/session.hpp`, and read each hit
    (`note_engine_stop_` is defined in the header); a site not in the list above is a
    change to INV-M. At the base the recipe also returns `peer_logout_.phase = none` at
    `install_reconnected_transport` (a new connection) and at the spawn branch's rollback when `co_spawn` throws
    (the OOM class); neither is a claim.
  - The leaf sets `logout_in_flight_`, and the only place that clears it is `record_state_transition_`'s
    Disconnected edge. After that edge no frame reaches the Active arm.
  - The spawn branch is reached only when the immediate-acknowledgement test of
    `on_inbound_active_too_high_logout_` is false. That test is INV-X's list (`logout_disconnect_timeout_ms == 0
    || !effective_clock_ || logout_in_flight_ || closing_or_stopped_()`, one definition); INV-M rests on its last
    two terms.
  - `install_reconnected_transport` asserts, with a plain Debug `assert`, that the bound's counter is 0 before the
    new connection starts. A Release build checks nothing there: what makes it hold is `drain_for_reconnect_`,
    which polls the bound's counter to 0 before the install.
- **INV-T (drain test to arm entry in one strand turn).** A 35=5 frame goes from `on_inbound_active_`'s `acking`
  drain test ("while the bound acknowledges a too-high Logout") to the entry of its receipt arm with no suspension.
  - The only awaits on those paths are `seqnum_mgr_.check_inbound`, whose mutex is granted inline on the strand
    (research R1), and the PossDup stage, which does not suspend on its pass path.
  - On route (b) there is also `resend_gap_deadline_`, whose path when the deadline has not expired returns with no
    suspension.
  - The `this_coro` cancellation awaits do not suspend either.
  - The `persist_inbound_advance_` awaits on the way sit on the Reject, Heartbeat and application paths, not on
    35=5.
  - One step is read and not run: asio starts an awaited child coroutine inline. It is read at the asio that
    `conanfile.py` pins: `awaitable<T>::await_suspend` pushes the child's frame onto the running thread
    (re-derive: `grep -n push_frame <asio>/awaitable.hpp <asio>/impl/awaitable.hpp`). The Debug assert below detects a
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
- **INV-F and INV-Z** (the manager frozen only while `Disconnected`; every counter write inside a `SeqnumManager`
  mutator) are RR-5's, stated there.
- **Debug asserts (plan phases 1 and 2):**
  - `peer_logout_.phase != acking` at the entry of `on_inbound_active_logout_` and of
    `on_inbound_active_too_high_logout_` (INV-T);
  - at a reset point, the value item 5 names (INV-X).

1. With rol set, only an awaited read-pump frame sets the marker to `pending`, at its receipt:
   - A at its entry (`on_inbound_active_logout_`);
   - L and S at their 35=5 branch, after the garble guard (`on_inbound_logout_sent_`'s 35=5 test, which also
     follows the fault guard; `on_inbound_logon_sent_`'s 35=5 branch ahead of `interpret_logon`). Neither arm
     suspends between that branch and its reset point (the guards and S's mTLS gate, either arm, are synchronous), so
     the marker reaches the reset point in the same strand turn. Any other frame in L or S, the Logon
     acknowledgement included, leaves the marker untouched;
   - B's immediate-acknowledgement branch of `on_inbound_active_too_high_logout_`.

   B's spawn branch and its ignored branch ("a second too-high Logout while the mark is set") write nothing to the
   marker. A detached bound's "receipt owed" state is the 103 mark (`pending` / `acking`), which 103 already owns
   alone. With rol off the marker stays `none`: no receipt is reset, waited for or refused on the marker's account.
   That scope is the marker's only: `close()`'s wait for a Logon arm's 141=Y unit in flight (item 4, RT-5) and the
   unasked reset (U1, U2) apply whatever the knobs.
2. **One owner per value.** By INV-P, at most one pump frame holds `pending` at a time, and it is that frame's own.
   By INV-X no bound runs its reset point beside it.
   - **A pump frame's `pending`.** It leaves `pending` exactly once:
     - to `reached` at the reset point, or by `close()`'s close-out (item 4);
     - or to `none` at any exit before the reset point.

     The frame clears it before any `close()` it calls itself (`close_on_late_parse_failure_`; the throw paths), so
     `close()` never waits for its own caller. Re-derive the in-arm calls by reading the four receipt arms' bodies,
     and the helpers they await, for the hits of `git grep -n -e 'close(' -e close_on_late_parse_failure_ --
     src/session/session.cpp`; a call site not covered by the clear is a defect of this item. On every other exit, return or exception, a frame-local owner sets
     `none` if the marker is still `pending`. 103 accepts a Session-touching destructor in an awaited frame. A
     `close()` close-out to `reached` is never undone by that owner.
   - **The bound** (`run_peer_logout_bound_`) writes the marker exactly once: at its reset point, in `acking`, inside
     `acknowledge_peer_logout_`. It moves `none` → `reached` and starts the reset unit in the same strand turn
     (item 3), asserting that the marker was `none`. Every other exit of the bound writes nothing to the marker:
     the claimed, stopped, superseded and drained exits, its exception handler, and a frame destroyed unresumed.
     Its receipt owed is the 103 mark, which those exits already clear or leave to a claim (O18).
3. **The reset point is one strand turn, and nothing in it throws.**
   - At the start of the turn, immediately before the marker's move, the arm resets its thread's cancellation state
     (`reset_cancellation_state`, which completes inline and does not suspend), so no suspension or callback falls
     between the reset and the marker's move (RR-3 step 3 lists it in the turn). This drops a `terminal` or `all` emission already recorded in
     the arm's thread, for example one from inside the `onLogout` that RR-3 step 2 fires. Without the reset, that
     emission makes `FIXPP_DETAIL_CO_AWAIT_LOCK`'s first part throw in `run_reset_unit_`'s first manager call.
   - The marker's move to `reached` (item 5) and the start of the unit follow with no suspension and no callback
     between them. The transition is written immediately before `run_reset_unit_`. Its manager calls grant inline (093 C-6
     step 3; research R1), and it sets `reset_unit_in_flight_` before its first suspension. With no store, the unit
     has no suspension at all.
   - Within the turn nothing can record a cancellation. So no other coroutine on the strand observes a `reached`
     receipt whose unit has not started (suspension), and no exception leaves the marker `reached` with no reset
     (throws).
   - "Nothing throws" excludes allocation failure, which is the existing OOM class and not a third ending. The
     unit's frame is allocated by the call of `run_reset_unit_`, whose ramp is `noexcept`, so a `std::bad_alloc`
     there terminates the process (`std::terminate`, as `L-048-1`'s sites do; read, not run) rather than leaving the
     marker `reached` with no reset. An allocation inside the unit, after it has started, is 093 C-6's unit, unchanged by
     104.
4. `close()` (any mode) waits after its joins and before its teardown predicate, while the marker is `pending` or
   `reset_unit_in_flight_` is set. In `close(graceful)` that is in phase 2, after phase 1 has returned. It needs a
   clock (as 093's wait does) and is bounded by `logon_timeout_ms`, measured from the wait's start on
   `effective_clock_` (`detail::deadline_after`, saturating), as 093's wait was; reusing the establishment timeout
   as this bound is 093's choice, kept. On expiry it emits `session_event_close_reset_wait_expired`.
   - **A joining `close()` runs none of this.** A `close()` that finds one already in flight (093 OD-29's
     escalation of a graceful close, `B-563-2`'s join, a repeated close) waits for that close's result by its
     result poll and returns it. The close that owns the teardown runs this wait and its close-out, once.
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
     poll then keeps one handler ready on the session's executor for up to `logon_timeout_ms`. Each pass is one
     posted handler with no delay, so the handlers of other sessions on the same io_context threads run between
     passes; what the poll takes from them is the thread time its passes use, a busy wait for the hold's duration
     (read, not measured). Why 093's rejection of a poll does not apply here: research D1 item 4.
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
       A store call that never completes hangs `stop()`: `L-093-12`'s condition, which names the reset unit's
       store operation and whose amended text names a receipt's reply store and inbound persist too. 104 widens
       nothing here: on the base a pump frame held in any store call already holds step 3; only the row's text
       widens.
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
   - The arm's other work also runs after `close()` returned: the event and `fromAdmin`. A Reject that `fromAdmin`'s
     reject would draw is refused at `assign_outbound`, before its `toAdmin`, by the manager freeze that the
     close-out armed (RR-5), or, once `close()` has drained the manager (`seqnum_mgr_.drain()`), by the drained mutex,
     which refuses every mutator alike: no Reject is numbered, stored or sent, and no `toAdmin` runs for it (cell
     R-5e, which opens the window between the close-out and the drain). That is fixpp#597 ("close() publishes closed_drained while a
     store call of another coroutine of the session is in flight", open). 104 neither widens nor closes it.
6. `install_reconnected_transport` sets `none`. A received Logout never resets a later connection.

**RR-3 (order at the reset point; RU-3).** In every receipt arm:
1. the reply Logout, if one is owed (A in Active, B); for A, then the `sequence_numbers_reset` event, `fromAdmin`
   and `persist_inbound_advance_`, in today's order, so `fromAdmin` returns before the `onLogout` that step 2
   fires; for B, then its event;
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
the close off the callback's stack (B-563-2's R-7 setup, where the callback runs the session's own executor),
104 specifies no ending for L-007-2's setup (undefined in release); no cell. On the bound's route the marker is `none`
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
    takes the number back, as D-OUT-1 does for a cancelled store call (while the manager is frozen that take-back
    is itself refused, with no observable effect: RR-5 item 7), and returns `dispatch_aborted`;
    `persist_inbound_advance_` returns success with no write. Each boundary is one byte test. It runs on every
    outbound frame that has a store call, and on every inbound persist to a persistent store; the in-memory inbound
    path returns before the test (its cost: plan "Performance Goals"). A test made before the call cannot govern a call already issued
    when the marker became `reached`; that call runs to its end (RR-2 item 5). The end state equals QuickFIX's,
    whose reset after the Logout discards every earlier increment. The receipt unit, the teardown's reset under
    rod and `close(graceful)`'s store flush (`close_flush_hook_a1_`) do not pass through either boundary, so each
    runs after `reached` as it does today; L's phase 1 wake makes no store call.
  - **What a refused call's caller observes.** Each boundary returns a result its callers already handle: a
    refused `store_then_emit` ends as a cancelled store call does (the number taken back, `dispatch_aborted`, no
    frame; D-OUT-1, O9's ending), and a refused `persist_inbound_advance_` lets its caller continue as after a
    write. The arm's return value is then the one its existing handling of that result gives.
  - **The manager half.** RR-4 governs store calls. A manager write by a writer in flight (a GapFill's
    `set_next_inbound`, a failed send's reconcile `set_next_outbound`, a late Reject's `assign_outbound`) is
    RR-5's.

**RR-5 (the manager freeze; owner ruling 2026-11-18, construction (Z); research D11).** RR-4 refuses a new store
call once the marker is `reached`. RR-5 refuses a new manager write, so that a writer already in flight at the reset
point cannot change either number after it. From the reset-point turn until the connection ends, `SeqnumManager`
accepts only the reset writers. RR-1 to RR-4, RU-1, RU-3 and construction A are unchanged, and RR-5 adds no wait.

1. **Scope: any `reached` marker (sub-ruling 1).** The manager is frozen exactly while the marker is `reached`. Three
   sites write the freeze, each in the same strand turn as the marker's write:
   - a reset point: a pump frame's `pending` → `reached`, or the bound's `none` → `reached` (RR-2 items 2, 3 and 5);
   - `close()`'s close-out of a `pending` receipt to `reached`, with no unit (RR-2 item 4);
   - the thaw, in `install_reconnected_transport`, in the block that sets the marker to `none` (RR-2 item 6), before
     its `LogonSent` transition.

   With rol off the marker never leaves `none`, so the manager is never frozen. W11, the U rows and a Logon arm's
   141=Y unit are unchanged.
2. **INV-F (frozen only while `Disconnected`).** Each turn that freezes the manager follows a `Disconnected` write:
   - RR-3 step 2 precedes the reset point;
   - `close()` records `Disconnected` ("ensure FSM is Disconnected before phase-2 fires") before its phase 2, which
     holds the close-out.

   A session leaves `Disconnected` only at `install_reconnected_transport`, after the thaw, and at `open()`, where
   the marker is `none`: `open()` runs once per `Session` (`if (state_ != lifecycle::never_opened)` returns
   `session_already_open`), and `install_reconnected_transport` refuses once `close()` or stop has begun
   (`closing_or_stopped_()`). Re-derive the exits with `git grep -n -E
   'record_state_transition_\(fsm_state::(NotConnected|LogonSent|LogonReceived|Active)\)' -- src/session/session.cpp`
   and read each hit. A frame delivered while `Disconnected` takes the Disconnected row, which drains it.
   `Session::send` refuses on `fsm_state_ != Active` at its entry and after every replay-gate wait. The liveness
   loop tests the state after each sleep and wake. So the only writers that can meet a frozen manager are those
   already in flight when it froze (research D11, "The writers").
3. **INV-Z (the choke point).** Every write of the two counters is inside a `SeqnumManager` mutator body.
   - Six are in `src/session/seqnum_manager.cpp`: `check_inbound`, `assign_outbound`, `hydrate`, `reset_to_one`,
     `set_next_inbound` and `set_next_outbound`.
   - The seventh, `take_back_outbound`, is defined inline in the installed header
     `include/fixpp/session/seqnum_manager.hpp`. The ruling's "all seven in `seqnum_manager.cpp`" holds for six.
   - Re-derive: `git grep -n -E '\bnext_(in|out)bound_\b *(=[^=]|\+\+|--)|(\+\+|--)next_(in|out)bound_\b' --
     src/session include/fixpp/session ':!include/fixpp/session/memory_store.hpp'`. Every hit is inside one of the
     seven bodies, or is a comment or the two member initialisers. `MemoryStore`'s own counters share the names and
     are excluded; the test-only `seqnum_manager_test_access::set_counters` is under `tests/`.

   A test in each refused mutator therefore governs every writer, whoever calls it and wherever it last suspended.
   No call site is read for the freeze to hold. That is why the freeze holds by construction, where a refusal per
   call site (research D11, (Y)) holds only by enumeration.
4. **The allowed writers (sub-ruling 3).** Only the two reset writers write while frozen. The manager tells them
   apart by order and by mutator, with no token:
   - **The receipt unit's manager set.** `run_reset_unit_`'s `reset_to_one`, `set_next_inbound` and
     `set_next_outbound` run before the freeze.
     - `run_reset_unit_` arms the freeze itself, when the marker is `reached`: after its manager set, whatever that
       set returned, and before its first suspension (`reset_unit_in_flight_` and the store's `reset_to`), or before
       its return when there is no store.
     - The manager calls grant inline (research R1; T008's record). So the marker's move, the manager set and the
       freeze are one strand turn, and no other coroutine runs between them (RR-2 item 3).
     - A Logon arm's 141=Y unit runs with the marker not `reached`, and arms nothing.
     - A Debug assert at `run_reset_unit_`'s entry checks that the manager is not frozen. A unit never runs on a
       frozen manager: a pump frame that finds `reached` runs none (RR-2 item 5), and the bound finds `none`.
   - **The teardown's reset.** `reset_seqnums_to_one_durable` calls `reset_to_one`, which carries no freeze test.
     Its callers are exactly the two reset writers: re-derive with `git grep -n 'reset_to_one(' -- src include` and
     read each hit. A caller that is not a reset writer is a change to RR-5. `reset_seqnums_to_one_durable` has
     other callers too, the Logon paths' `reset_on_logon` resets in `emit_initiator_logon_` and
     `on_inbound_not_connected_` (re-derive with `git grep -n 'reset_seqnums_to_one_durable(' -- src`). INV-F keeps
     both off a frozen manager. So under rod the teardown resets after a receipt as it does today (W7, R-2).
   - **The interface.** `SeqnumManager` gains one private flag, and two members that set and clear it, `freeze` and
     `thaw`. Each takes a passkey, `SeqnumManager::freeze_key`, which only `Session` can construct (its constructor
     is private, with `Session` its friend). No other caller can freeze or thaw a manager, and no other public name
     is added. The six refused mutators read the flag; nothing else does. The layout change is B-104-7's.
   - **Debug assert.** After each of the three sites in item 1, the manager is frozen if and only if the marker is
     `reached`.
5. **The refused returns (sub-ruling 2).** A refused call changes neither counter. Each return was chosen from its
   callers' existing handling, read in source (research D11, "The refused returns").

   | Mutator | Refused return | Callers that can meet a frozen manager, and what each then does |
   |---|---|---|
   | `check_inbound(seq)` | `unexpected(session_already_closed)`, the code its drained-mutex path already returns | `consume_rejected_seqnum_`, on a route-B frame in flight: a result other than `store_seqnum_overflow` is "not consumed", so it returns success with no `note_inbound_advance_` and no persist. Its caller goes on to its Reject, which `assign_outbound` refuses (next row). The other callers (`on_inbound_active_`, `on_inbound_not_connected_`, `on_inbound_logon_sent_`) call it in their frame's entry turn, so none meets a frozen manager (INV-F) |
   | `assign_outbound()` | `unexpected(session_already_closed)` | Every caller's failure branch. No frame is built into the store or onto the wire. Where `assign_outbound` precedes `fire_to_admin_` (the Rejects of both `emit_session_reject_` overloads, `inbound_reject_leaf_` and `apply_inbound_sequence_reset`), no `toAdmin` runs. `inbound_reject_best_effort_leaf_` skips and returns success. Every other caller records `Disconnected`, only when the state is not that already (item 6), and returns the error, which ends the frame (the pump's `stop_pump` on a frame's error). The emitters whose callback (`fire_to_admin_`, or `toApp` for the BusinessMessageReject and `Session::send`) precedes `assign_outbound` do not meet a frozen manager (item 6) |
   | `take_back_outbound(k)` | no-op | `store_then_emit`'s cancelled-call block (D-OUT-1) and RR-4's refusal: the number is not given back. No input gives this an observable effect (item 7) |
   | `hydrate(in, out)` | `unexpected(session_already_closed)` | None. `ensure_hydrated_` runs in `emit_initiator_logon_` (at `open()`, marker `none`; on a reconnect, after `install_reconnected_transport`'s thaw, its `force` refresh included) and in the acceptor's NotConnected arm, which a frozen session never reaches (INV-F). Were it reached, `ensure_hydrated_`'s failure write is `disconnect_unless_superseded_`, which writes no second `Disconnected` entry, and the error is returned |
   | `reset_to_one()` | never refused: a reset writer's mutator (item 4) | the two reset writers |
   | `set_next_inbound(n)` | success, with no write | `apply_inbound_sequence_reset`, on a route-B GapFill or Reset-mode SequenceReset in flight. It continues as after a write: its awaiting-resend exit and `note_inbound_advance_`, neither a counter write. It returns success, and its failure branch, which records `Disconnected` unconditionally, is not reached. Nothing is emitted. The unit's own call precedes the freeze (item 4) |
   | `set_next_outbound(n)` | success, with no write | `store_then_emit`'s reconcile after a persistent-store failure, which discards the result (`(void)co_await`). `store_then_emit` returns its captured store error as today, and the frame is not transmitted (fail closed). The manager keeps the number it had. The unit's own call precedes the freeze |

6. **What a refused call's caller observes (sub-ruling 2's rule).** The caller emits nothing for a refused number,
   calls no `toAdmin` for it, and adds no second `Disconnected` entry to the state history.
   - **No frame and no `toAdmin`.** Where `assign_outbound` precedes `fire_to_admin_`, the refusal ends the emitter
     before its `toAdmin`. Where `fire_to_admin_` precedes it, the emitter cannot meet a frozen manager: no
     suspension falls between its last state test, or its frame's entry turn, and its `assign_outbound`. So both
     come before the freeze, or after a state test that fails while `Disconnected` (INV-F).
     - Re-derive the callback-first emitters: for each hit of `git grep -n 'seqnum_mgr_.assign_outbound()' --
       src/session/session.cpp`, read whether a `fire_to_admin_(`, or a `toApp` dispatch, precedes it in its
       function. `inbound_business_reject_leaf_` ("route 35=j through toApp before assign_outbound") and
       `send_impl` are the `toApp` ones; the rule is read for them as for `toAdmin`.
     - Read per site (research D11, "No `toAdmin` for a refused number"):
       - `inbound_logout_leaf_`: its `fsm_state_ != Active` test;
       - `run_liveness_loop`: its state tests after each sleep and wake;
       - `run_logout_phase1`: O22;
       - `inbound_resend_request_leaf_`, `inbound_heartbeat_reply_leaf_` and `inbound_business_reject_leaf_`:
         reached in their frame's entry turn;
       - `send_impl`: `Session::send` refuses on the state at its entry and after every replay-gate wait (INV-F);
       - `refuse_logon_with_logout_` and `honor_peer_next_expected_`: the Logon arms only.
     - A callback-first emitter reachable while frozen is a defect of this item, and goes to the orchestrator.
   - **No second `Disconnected` entry.** `record_state_transition_` appends to the state history on every call.
     - Every `Disconnected` write on a path that a refused `assign_outbound` returns into is made only when the
       state is not `Disconnected` already. The manager cannot meet this rule alone: no refused return of
       `assign_outbound` avoids its callers' unconditional writes, so the rule needs these caller-side edits. They
       also apply to any `assign_outbound` failure while the session is already `Disconnected`, a drained manager
       after `close()` included, and not to the freeze alone. That is the rule `disconnect_unless_superseded_` and
       `run_logout_phase1`'s exits already follow (`.specify/536-537-538-recovery-liveness.md`, D-OUT-2, from the
       merge of B41).
     - It covers each `assign_outbound` call's failure branch (the recipe above, each failure branch read), and the
       writes the frame makes on its way out to the pump.
     - `set_next_*` refuse with success, so `apply_inbound_sequence_reset`'s unconditional write is not reached.
     - Cells R-5a and R-5c assert exactly one `Disconnected` entry in `Session::fsm_visit_history()`.
7. **Unchanged.**
   - RR-4's two boundaries are unchanged.
   - While frozen, `store_then_emit`'s take-back of a refused or cancelled call is itself refused. That has no
     observable effect:
     - after a unit the next outbound number is 1, which `take_back_outbound` lowers to nothing;
     - after a close-out, no frame reaches a store call with a number taken in an earlier turn. An emitter's
       `assign_outbound`, its `toAdmin` and `store_then_emit`'s boundary run in one strand turn, and a call already
       issued is not refused (research D11; T013 reads it per emitter).
   - D-OUT-10 is unchanged.
   - The store half still rests on RR-4 and on "a call already issued runs to its end" (RR-2 item 5). RR-5 closes
     the manager half.
8. **Cost.** One byte test in each refused mutator, before its write. `check_inbound` runs once per inbound frame
   and `assign_outbound` once per outbound frame, so the test is on both hot paths. It allocates nothing (plan
   "Performance Goals"; `seqnum_bench` and `on_inbound_frame_bench` run paired, T093).


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
The "QuickFIX" column is that reading, subordinate to the spec's "Departures from QuickFIX" (FR-018), which wins on
a difference; it is not a second list of departures.
A cell id is given where the row can be driven; its mutant is in research D10.

| # | Condition | A (Active / LogonReceived) | L (LogoutSent) | B (too-high ack) | S (LogonSent) | QuickFIX | Cell |
|---|---|---|---|---|---|---|---|
| O1 | normal | reply, persist, Disconnected, reset at the arm; wire: the reply only | Disconnected, reset at the arm, then phase 1 wakes; wire: nothing | reply, Disconnected, reset at the arm | Disconnected, reset at the arm; the arm returns success; wire: nothing (QuickFIX replies) | reset at receipt, all three | R-1c, R-1d, R-1e, R-1o |
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
| O13-r | `close()` during, before the reset point, wait expired or no clock, the arm held in its reply's store call | no reset under rol (the receipt did not reach its reset point); the marker is closed out to `reached`. The held call completes after `close()` returned (RR-2 item 5): the reply is stored. On a live transport it is not transmitted, because `close()` has run `close_async()` and `write_gate_.cancel_and_drain()`, so the write fails with `dispatch_aborted`; a config-time `transport_send` sink transmits it (as `B-563-5`). The persist is refused (RR-4), so the store's inbound stays N while the manager's is N+1 (`check_inbound`, before the arm). No unit. The event and `fromAdmin` run after `close()` returned (fixpp#597); a Reject `fromAdmin` draws is refused at `assign_outbound`, before its `toAdmin` (RR-5; the close-out froze the manager) | no input (as O12) | the bound: no input, because `close()`'s bound join precedes the wait, so it never closes out a bound's receipt (RR-2 item 4). Immediate acknowledgement (no clock is one of its conditions): as A, without the persist and `fromAdmin` (B has neither) | no input (as O12) | n/a | R-1l (`HeldInReplyStore`), R-5e |
| O13-p | as O13-r, the arm held in its inbound persist | no reset under rol; the marker is closed out to `reached`; the held persist completes after `close()` returned and lands (store inbound N+1); no unit (fixpp#597, as O13-r) | no input (as O12) | n/a (B has no persist) | no input (as O12) | n/a | R-1l (`HeldInPersist`) |
| O13a | `reached`, unit not started, at the teardown | the teardown resets once; no input by construction (RR-2 item 3) | as A | as A | as A | n/a | R-1u (its mutant opens the window) |
| O14 | `close()` during the unit | `close()` waits for the unit (clock); without a clock it returns while the unit's store operation completes on the strand (093's no-clock case, inside fixpp#597) | as A | immediate acknowledgement: as A. The bound: `close()`'s bound join waits for it, so for its unit, with or without a clock | as A | n/a | R-1j |
| O15 | `Engine::stop()` during | one reset or none, decided by the arm; no close-out. Stop's role-loop join (its step 3, before its step 4 `close()`; RR-2 item 4, `L-563-4`) waits for the arm, past `logon_timeout_ms` (the session always has a clock under the Engine). The arm reaches its reset point and resets once, or exits before it (O2–O11) and does not. A held reply is stored and not transmitted, because step 2 closed the transport (O10). A unit in flight is waited for (`L-093-12`). No `close_reset_wait_expired`. Stop returns after the arm (`L-093-12`, `L-563-4`). Its emission is total, which the arm's filter refuses after its first lock, and no Engine caller emits `terminal` or `all` on the arm's slot. A direct caller's `terminal` emission takes O9 at a store call and RR-2 item 3 at the reset point | L does not suspend before its reset point (research R1); its unit runs inside the pump frame, so the join waits for it (`L-093-12`): one reset | immediate acknowledgement: as A. The bound: as O12 and O18 through step 4's `close()` (a `pending` bound claimed by `note_engine_stop_`, no reset; an `acking` one joined, one reset) | a frame that arrives after stop's step 1 meets `logon_arm_superseded_` (the engine-stop flag) and acts on nothing, no reset; a unit already started runs inside the pump frame, and the join waits for it | n/a | R-1t, R-1x |
| O16 | store reset fails | logged; Disconnected; no retry (RT-4) | as A | as A | as A | QFGo: logged, then `latentState` | R-1i |
| O17 | grace timer of `close(graceful)` expires during L's unit | n/a | phase 1 returns `session_logout_timeout`; the timer branch also force-closes the transport (`live->close()`) before phase 2; the wire is the same (nothing more is sent); `close()` waits for the unit | n/a | n/a | n/a | R-1j |
| O18 | bound ends unacknowledged (claim by `close()`, stop, another ending's Disconnected, reconnect drain, a Logout in flight) | n/a | n/a | the marker untouched (`none`): the bound wrote nothing, and 103 clears or claims its mark; no reset | n/a | QFC, QFJ: reset (they process a too-high Logout at receipt, `verify(logout, false, false)`) | R-1f |
| O18a | a second too-high Logout while the bound is `pending` (the ignored branch, either route) | n/a | n/a | the frame is ignored and writes nothing to the marker. The first bound's acknowledgement resets once (O1) | n/a | n/a (QuickFIX processed the first at receipt) | R-1y |
| O19 | mTLS identity not authorised, or none (the gate's arm (2), the case of a direct initiator started by `open()`) | n/a | n/a | n/a | rol set: the gate's `session_event_compid_authorization_failed`; no reset; Disconnected; the arm returns success. With rol off the branch does not run (W11): no event | n/a | R-1o |
| O20 | Logout in NotConnected | — | — | — | — | no reset (QFC `validLogonState` false) | R-1o |
| O21 | a writer in flight at the reset point writes the manager after it (RR-5): a route-B pump frame suspended in a store call, or `Session::send`, the liveness loop or phase 1 suspended in `store_then_emit`'s failure reconcile, on any route | an application send or liveness frame in flight: its reconcile's `set_next_outbound` refused; the send returns the store's error, nothing transmitted; manager `(1, 1)` | as A, phase 1's own reconcile included | the pump frame in flight: its GapFill's `set_next_inbound` refused (the frame returns success, nothing emitted); its Reject's `assign_outbound` refused before `toAdmin` (nothing numbered, stored or sent); its reconcile refused. Manager and store `(1, 1)`; one `Disconnected` entry | no pump writer: S is the only pump frame and no bound exists before Active. A direct caller's `open()` Logon still in its store call: its reconcile refused | n/a: QuickFIX processes each frame and each send to its end, and a too-high Logout at its receipt (DQ-12), so no writer of its own is in flight at its reset, which discards every earlier increment. No departure: a frame that 103's bound lets in after a too-high Logout is never processed by QuickFIX, so nothing of it is sent there either (research D11) | R-5a, R-5b, R-5c, R-5d |
| O22 | `close(graceful)` started after the reset point (sub-ruling 4) | phase 1 does not run: `close()`'s phase-1 test needs Active or LogonReceived, and the state is `Disconnected` from RR-3 step 2. No Logout, no `toAdmin`, no `assign_outbound`. A `close(graceful)` that passed that test before the receipt: phase 1 waiting at the replay gate is woken by the Disconnected edge and takes its "not sent" exit before its stamp, its `toAdmin` and its number; otherwise phase 1 runs from the test to its `assign_outbound` in one strand turn, before the receipt (research D11, "Phase 1"). Either way it never meets a frozen manager | phase 1 already sent its Logout (that is what LogoutSent means) | as A (`close()` claims the bound at its first commit, before phase 1) | as A (phase 1 needs Active or LogonReceived) | n/a | read from source, no cell. R-3, when T011 reads it drivable, runs this input: `close(graceful)` held in its flush while arm A in LogonReceived reaches its reset point |

## Part 2 — endings per arm and input

| # | Arm / condition | Input | Wire from fixpp | Numbers after | Store | State |
|---|---|---|---|---|---|---|
| W1 | LogoutSent (fixpp's graceful Logout sent), rol set | no peer Logout; T expires | nothing more | as after the Logout's emit | unchanged by any reset | Disconnected; `close()` ok |
| W2 | same | transport drops (peer EOF) during the wait | nothing more | as W1 | as W1 | Disconnected; `close()` ok |
| W3 | same | peer Logout, before T | nothing more | `(1, 1)` before phase 1 wakes | one `reset_to(1, 1)`, state Disconnected, the transport open, before `close()`'s phase 2 | Disconnected |
| W4 | Active, rol set | peer Logout request, M = N | Logout reply at 34 = N_out | pre-reset at `onLogout`; `(1, 1)` after the arm | inbound persist, then `reset_to(1, 1)` with the state Disconnected and the transport open | Disconnected |
| W5 | Active, rol set, T > 0, a clock | peer Logout with M > N | ResendRequest (as B-103-7); when N passes M or at T, the acknowledgement Logout | as W4, after the acknowledgement | `reset_to(1, 1)` after the acknowledgement, state Disconnected | Disconnected |
| W6 | W5 pending, then `close()`, stop, the peer's EOF or a reconnect drain | — | as B-103-7 for that route | not reset by rol (O18); rod resets at teardown | no `reset_to` | as that route |
| W6a | W5 with the bound already `acking`, then `close()`, stop, the peer's EOF or a reconnect drain | — | the acknowledgement Logout attempted; on the wire only if its write completes before the transport closes (as O10, O13-r, O15): `Session::close` runs `close_async()` before 103's bound join, `Engine::stop()` closes the transport at its step 2, and on the EOF the peer has gone | `(1, 1)` | one `reset_to(1, 1)`: the bound runs to its end, because each route joins it (`close()`'s bound join, which the pump's close after the EOF and stop's step 4 reach; `drain_for_reconnect_`'s poll of the bound's counter, before `install_reconnected_transport`) | Disconnected |
| W7 | W3 or W4 with rol **and** rod | — | as W3 / W4 | `(1, 1)` | two resets: the arm's `reset_to`, then the teardown's `reset` | Disconnected |
| W8 | W3 or W4, receipt reset fails | — | as W3 / W4 | manager `(1, 1)`; store pre-reset when the store refused | logged | Disconnected; arm's return unchanged |
| W9 | standalone initiator: W3/W4 reset at receipt, then `drive_reconnect`, numbers advance, then `close()` with rol only | — | the new Logon at the numbers as they stand | not reset by the new connection's teardown | no further reset | closed |
| W10 | LogonSent, rol set; on an mTLS session the identity is authorised (an unauthorised one: O19) | peer Logout | nothing (QuickFIX replies with a Logout) | `(1, 1)` | `reset_to(1, 1)` | Disconnected; the arm returns success |
| W11 | LogonSent, rol off, whatever the identity (arm S's branch and its mTLS gate do not run: `interpret_logon` refuses the frame as a non-Logon, as the base; no `session_event_compid_authorization_failed`); or NotConnected with any knob | peer Logout | nothing | unchanged | no reset | Disconnected; in LogonSent the arm returns success, as the base |
| U1 | LogonSent, lenient or unilateral, fixpp's Logon without 141=Y, N ≥ 1 | ack 141=Y, M = 1 | no second Logon; the next frame fixpp sends is 34=1 | inbound next 2, outbound next 1 | `reset_to(2, 1)`; event `by_peer_request = true` | Active |
| U2 | same | ack 141=Y, M > 1 | no second Logon; on the peer's next frame (34 > 1), ResendRequest 7=1, 16=0 | inbound next 1, outbound next 1 | `reset_to(1, 1)` | Active |
| U3 | U1 or U2 with 789 enabled and present, 789 = X | X > 1 | Logout "NextExpectedMsgSeqNum too high, expecting 1 but received X" | as after the reset, plus the Logout | reset, then the Logout stored | Disconnected |
| U4 | `bilateral_strict` or a knob-asked reset (fixpp's Logon carried 141=Y) | any ack | unchanged (today's `check_inbound` against N = 1) | unchanged | unchanged | unchanged |
| U4m | U1 or U2 on a direct mTLS initiator started by `open()`, which fails the Logon path's mTLS gate: it has no live identity, so the gate fails closed in its arm (2), with no `authorize` call (an initiator whose live identity is not authorised fails it in arm (1)) | — | nothing | unchanged (no unit) | no `reset_to` | Disconnected |
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
| T8 | T1/T2 on an mTLS session whose live identity is not authorised for the CompID, or that has none (the case of a direct initiator started by `open()`: the gate's arm (2), with no `authorize` call); on the acceptor also a Logon whose credentials `authorize_logon` refuses | — | nothing (FIX-TC 1S(c): "Disconnect without sending a message") | as T1 | nothing | Disconnected, with the gate's own event |

**Precedence on one Logon frame** is each arm's code order (research D5), with one change (N-8): a too-low frame
runs the identity gates before its Logout. An earlier guard (validate gate, `interpret_logon`, posture,
SendingTime, `seq == 0`, and in NotConnected the strict refusal and the `reset_on_logon` reset) takes its own
ending. For a too-low frame the arm then runs the mTLS CompID-authorisation gate (both arms) and, on the acceptor,
`authorize_logon`, through the helpers that make today's checks, so the event, the one-shot `live_peer_id_`
consumption and the silent Disconnected are identical. Only then does it send the too-low Logout. The later
guards (1137, strict mismatch, the unit, 789) are not reached by a too-low frame. The frame is a Logon (35=A):
`interpret_logon` refuses any other first frame before the sequence check. A Logout (35=5) in LogonSent never
reaches the sequence check, whatever its 34: with rol set arm S's branch ahead of `interpret_logon` takes it (RR-1
row S, W10, O19), and with rol off `interpret_logon`'s non-Logon refusal does (W11), so it never draws the too-low
Logout.

**Unchanged and disclosed:**
- The too-low Logout leaves the transport open until EOF, `close()` or stop (B-103-10, L-103-5, whose text names
  no arm). In a Logon arm an engine-managed session's establishment deadline (`logon_timeout_ms`, B-093-4) also
  ends it, because the session never reached Active (`run_read_pump`'s `establishing` test in
  `src/session/engine.cpp`); a direct session waits for EOF, `close()` or stop.
- The Logon arms' sequence check ignores `validate_sequence_numbers`.
- A strict acceptor's 141=Y Logon at 34 ≠ 1 (L-103-4).
- The acceptor twin of U1 is unreachable (research D7).
- The posture, strict and SendingTime refusals and the 1137 Reject still answer before the identity gates on an
  mTLS acceptor, and the posture and SendingTime Logouts before the gate on a direct mTLS initiator
  (`on_inbound_logon_sent_` runs both ahead of its mTLS gate): a pre-existing departure from FIX-TC 1S(c)'s
  principle that 104 does not widen (L-104-3).

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
