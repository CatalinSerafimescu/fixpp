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
| S | with rol set only: the 35=5 frame passed the arm's guards ahead of `interpret_logon` (garble, `logon_arm_superseded_`, fault, 099's establishment refusal; the validate gate exempts 35=5), and on an mTLS session the Logon path's mTLS gate passes (research D1 item 3): with `live_peer_id_` set it calls `authorize`, and with mTLS and no live identity it fails closed. The gate is the Logon path's own function (`logon_identity_admitted_`), so it has the same side effects there: an admitted live identity draws `session_event_peer_identity_bound` and is consumed, as a refused one is (orchestrator ruling 2 on the fourteenth run). With rol off the branch does not run (W11) |

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
the wire differs. QuickFIX replies, resets and disconnects in one call (QFC and QFJ `nextLogout`, QFGo
`handleLogout`), and fixpp transmits nothing after its reset point, which follows the reply (RR-6, ruling W), apart
from its own crossing Logout (RR-6 item 4), which QuickFIX has already written.
- **One window remains.** After its reply, arm A suspends in step 1: in its `persist_inbound_advance_`, or in its
  best-effort Reject's store call when `fromAdmin` rejects. `fromAdmin` itself is synchronous (`parse_and_dispatch_`).
  A concurrent emitter's write granted in that window precedes the reset point and goes out.
  - The persist suspends only on a persistent store: `persist_inbound_advance_` returns at
    `if (!store_is_persistent_)` before any store call.
  - So, with a persistent store, a liveness frame queued behind the reply is written during the persist whenever it
    is queued there (the gate's grant is posted). That is not a race.
  - With a volatile store and no best-effort Reject, arm A does not suspend after its reply, and the frame is held
    back.
- **The window is within QuickFIX/J's envelope, per frame kind** (research D11, "Frames in flight across
  `reached`"):
  - application sends: QuickFIX C++ and QuickFIX/J user-thread sends can race between the reply and `disconnect()`;
    QuickFIX/Go never sends one there;
  - liveness frames: QuickFIX/J's `SessionTimerTask` can race there; QuickFIX C++ and QuickFIX/Go run the timer on
    the frame thread and never send one there. This closes the window in which the numbers were (1, 1) while the session was still Active; it does not
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
    store-order condition that makes this hold, and its phase-0 check). Its one exemption is RR-6 item 4;
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
   'record_state_transition_\(fsm_state::(NotConnected|LogonSent|LogonReceived|Active|LogoutSent)\)' --
   src/session/session.cpp` and read each hit. The `LogoutSent` write in `run_logout_phase1` is preceded by its
   "already Disconnected → no LogoutSent" test; read that it still is. A frame delivered while `Disconnected` takes the
   Disconnected row, which drains it. `Session::send` refuses on `fsm_state_ != Active` at its entry and after
   every replay-gate wait. The liveness
   loop tests the state after each sleep and wake. So the only writers that can meet a frozen manager are those
   already in flight when it froze (research D11, "The writers").
3. **INV-Z (the choke point).** Every write of the two counters is inside a `SeqnumManager` mutator body.
   - Six are in `src/session/seqnum_manager.cpp`: `check_inbound`, `assign_outbound`, `hydrate`, `reset_to_one`,
     `set_next_inbound` and `set_next_outbound`.
   - The seventh, `take_back_outbound`, is defined inline in the installed header
     `include/fixpp/session/seqnum_manager.hpp`. The ruling's "all seven in `seqnum_manager.cpp`" holds for six.
   - Re-derive: `git grep -n -E '\bnext_(in|out)bound_\b *([-+*/%&|^]|<<|>>)?=[^=]|(\+\+|--) *next_(in|out)bound_\b|\bnext_(in|out)bound_\b *(\+\+|--)|&next_(in|out)bound_\b|exchange\(next_(in|out)bound_' --
     src/session include/fixpp/session ':!include/fixpp/session/memory_store.hpp'`. Its known positive: the input
     `next_inbound_ += 1;`, `std::exchange(next_outbound_, 3);`, `next_inbound_ = 2;` and `if (next_inbound_ == 2)`,
     one line each, gives 3 hits (the `+=`, `exchange` and `=` lines; `==` excluded). Every hit is inside one of the
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
     - Debug asserts in `run_reset_unit_` check that the manager is not frozen: one at its entry, and one after
       its manager set, immediately before its freeze (the post-set assert). The entry assert pins that a unit never
       runs on a frozen manager: a pump frame that finds `reached` runs none (RR-2 item 5), and the bound finds
       `none`. The post-set assert pins the order, so a freeze moved ahead of any of the three manager calls fires
       it. With receipt targets of `(1, 1)` no other assertion can see that reorder (research D11, "Not relied on").
   - **The teardown's reset.** `reset_seqnums_to_one_durable` calls `reset_to_one`, which carries no freeze test.
     Its callers are exactly the two reset writers: re-derive with `git grep -n 'reset_to_one(' -- src include` and
     read each hit. A caller that is not a reset writer is a change to RR-5. `reset_seqnums_to_one_durable` has
     other callers too, the Logon paths' `reset_on_logon` resets in `emit_initiator_logon_` and
     `on_inbound_not_connected_` (re-derive with `git grep -n 'reset_seqnums_to_one_durable(' -- src`). Neither meets
     a frozen manager: the acceptor's arm by INV-F and INV-P, and `emit_initiator_logon_` because its only
     suspension before the reset is `ensure_hydrated_`'s store reads, after which `hydrate` refuses on a frozen
     manager and the emit returns. So under rod the teardown resets after a receipt as it does today (W7, R-2).
   - **The interface.** `SeqnumManager` gains one private flag; two members that set and clear it, `freeze` and
     `thaw`; and one that reads it, `frozen`, for this item's Debug asserts. Each takes a passkey,
     `SeqnumManager::freeze_key`, which only `Session` can construct (its constructor is private, with `Session` its
     friend; its copy constructor is user-provided and private, so the key is not trivially copyable and
     `std::bit_cast` cannot make one). The three, and the key type `freeze_key` they take, are public names. A C++
     consumer can name them but cannot call the three, because it cannot construct the key. The `static_assert`s at
     the top of `tests/session/test_104_reset_on_logout.cpp` re-derive the key's properties. No other public name is
     added. The six refused mutators
     read the flag to refuse. `frozen` reads it only for the Debug asserts, and no production branch tests it: a
     branch on it would be a refusal per call site (research D11, (Y)). The layout change is B-104-7's. `frozen` is
     an inline member function and adds no data member.
   - **Debug assert.** After each of the three sites in item 1, the manager is frozen if and only if the marker is
     `reached`.
5. **The refused returns (sub-ruling 2).** A refused call changes neither counter. Each return was chosen from its
   callers' existing handling, read in source (research D11, "The refused returns").

   | Mutator | Refused return | Callers that can meet a frozen manager, and what each then does |
   |---|---|---|
   | `check_inbound(seq)` | `unexpected(session_already_closed)`, the code its drained-mutex path already returns | `consume_rejected_seqnum_`, on a route-B frame in flight: a result other than `store_seqnum_overflow` is "not consumed", so it returns success with no `note_inbound_advance_` and no persist. Its caller goes on to its Reject, which `assign_outbound` refuses (next row). The other callers (`on_inbound_active_`, `on_inbound_not_connected_`, `on_inbound_logon_sent_`) call it in their frame's entry turn (`on_inbound_active_`) or in a Logon arm, which is the one pump frame (INV-P) with no bound before Active, so none meets a frozen manager (INV-F) |
   | `assign_outbound()` | `unexpected(session_already_closed)` | Every caller's failure branch. No frame is built into the store or onto the wire. Where `assign_outbound` precedes `fire_to_admin_` (the Rejects of both `emit_session_reject_` overloads, `inbound_reject_leaf_` and `apply_inbound_sequence_reset`), no `toAdmin` runs. `inbound_reject_best_effort_leaf_` skips and returns success. Every other caller records `Disconnected`, only when the state is not that already (item 6), and returns the error, which ends the frame (the pump's `stop_pump` on a frame's error). The emitters whose callback (`fire_to_admin_`, or `toApp` for the BusinessMessageReject and `Session::send`) precedes `assign_outbound` do not meet a frozen manager (item 6) |
   | `take_back_outbound(k)` | no-op | `store_then_emit`'s cancelled-call block (D-OUT-1) and RR-4's refusal: the number is not given back. No input gives this an observable effect (item 7) |
   | `hydrate(in, out)` | `unexpected(session_already_closed)` | A direct initiator's `open()` emit, when a concurrent `on_inbound_frame` reaches arm S during `ensure_hydrated_`'s store reads: `disconnect_unless_superseded_` writes no second `Disconnected` entry, and `open()` returns the error. Otherwise none: `ensure_hydrated_` runs in `emit_initiator_logon_` (at `open()`, marker `none`; on a reconnect, after `install_reconnected_transport`'s thaw, its `force` refresh included) and in the acceptor's NotConnected arm, which a frozen session never reaches (INV-F). Were it reached, `ensure_hydrated_`'s failure write is `disconnect_unless_superseded_`, which writes no second `Disconnected` entry, and the error is returned |
   | `reset_to_one()` | never refused: a reset writer's mutator (item 4) | the two reset writers |
   | `set_next_inbound(n)` | success, with no write | `apply_inbound_sequence_reset`, on a route-B GapFill or Reset-mode SequenceReset in flight. It continues as after a write: its awaiting-resend exit and `note_inbound_advance_`, neither a counter write. It returns success, and its failure branch is not reached. On the base that branch's `Disconnected` write is unconditional, so an error return would add a second entry; that is why the refusal is success with no write. After 104, T050 makes that write conditional (item 6, N-2), so the branch's post-drain path adds no entry. Nothing is emitted. The unit's own call precedes the freeze (item 4) |
   | `set_next_outbound(n)` | success, with no write | `store_then_emit`'s reconcile after a persistent-store failure, which discards the result (`(void)co_await`). `store_then_emit` returns its captured store error as today, and the frame is not transmitted (fail closed). The manager keeps the number it had. The unit's own call precedes the freeze |

6. **What a refused call's caller observes (sub-ruling 2's rule).** The caller emits nothing for a refused number,
   calls no `toAdmin` for it, and adds no second `Disconnected` entry to the state history.
   - **No frame and no `toAdmin`.** Where `assign_outbound` precedes `fire_to_admin_`, the refusal ends the emitter
     before its `toAdmin`. Where `fire_to_admin_` precedes it, the emitter cannot meet a frozen manager: a test that
     fails while frozen comes before its callback with no suspension from that test to its `assign_outbound`, or
     no suspension falls between its frame's start (`on_inbound_active_`'s entry state test) and its
     `assign_outbound`. So both come before the freeze, or after a test that fails while frozen.
     - Re-derive the callback-first emitters: for each hit of `git grep -n 'seqnum_mgr_.assign_outbound()' --
       src/session/session.cpp`, read whether a `fire_to_admin_(`, or a `toApp` dispatch, precedes it in its
       function. `inbound_business_reject_leaf_` ("route 35=j through toApp before assign_outbound") and
       `send_impl` are the `toApp` ones; the rule is read for them as for `toAdmin`.
     - Read per site (research D11, "No `toAdmin` for a refused number"):
       - `inbound_logout_leaf_`: its `fsm_state_ != Active` test;
       - `run_liveness_loop`: its state tests after each sleep and wake;
       - `run_logout_phase1`: O22;
       - `inbound_resend_request_leaf_`: its own entry test (the next bullet);
       - `inbound_heartbeat_reply_leaf_` and `inbound_business_reject_leaf_`: no suspension from their frame's
         start to their number (the first is `on_inbound_active_test_request_`'s first await; the second follows
         `fromApp`, which is synchronous, in `on_inbound_active_from_app_failed_`);
       - `send_impl`: `Session::send` refuses on the state at its entry and after every replay-gate wait (INV-F);
       - `refuse_logon_with_logout_` and `honor_peer_next_expected_`: the Logon arms only.
     - **The leaf entry test (orchestrator ruling 2026-11-18, the T014 STOP; research D11).** A pump-frame leaf that
       calls `toAdmin` before its number, and that its frame can reach after a suspension, holds the rule by
       construction, not by an ordering argument. It is given an entry test in `inbound_logout_leaf_`'s pattern
       (B41, D-OUT-2):
       - **The test.** Immediately after the leaf's split entry, before its number peek and its build: when the
         receipt marker (`logout_seen_`) is `reached`, the leaf returns success and writes nothing (no `toAdmin`,
         no number, no store call, no state write). From the test to its `assign_outbound` nothing suspends: the
         build and `fire_to_admin_` are synchronous, and the grant is inline (research R1). So a leaf past its
         test meets an unfrozen manager (RO-4: frozen exactly while `reached`), whatever its frame did before it.
       - **Why the marker, and not the state.** It is the narrowest test that refuses after the reset point:
         - `fsm_state_ == Disconnected` would also refuse with rol off, where nothing is frozen, and where the base
           calls `toAdmin` and sends the request after 103's bound recorded `Disconnected` (RO-1, W11: with rol off
           104 adds nothing);
         - `fsm_state_ != Active` would also refuse in LogonReceived, which `on_inbound_active_` admits;
         - `peer_logout_.phase` is `pending` exactly while 103's bounded recovery sends its own ResendRequest
           through this leaf;
         - `== reached`, not `!= none`, so a `pending` marker passes.

         The leaf reads the marker, as RR-4's two boundaries do. It does not read `frozen(freeze_key)`, which no
         production branch tests (item 4).
       - **The leaves.** `inbound_resend_request_leaf_`. Its one caller, `on_inbound_active_too_high_`, reaches it
         after `answer_resend_request_(…, /*in_sequence=*/false)` on a too-high ResendRequest, and that answer
         suspends: the walk (`replay_outbound_range_`), or, with BeginSeqNo(7) absent, the Reject's store call and
         write. A route-B reset point can fall inside it, so without the test the leaf calls `toAdmin` for a
         ResendRequest whose `assign_outbound` the freeze then refuses (research D11, T014's finding).
       - **What its caller does.** The refused leaf returns success. `on_inbound_active_too_high_` then returns
         success (its `if (!r) co_return r;` is not taken), and the frame writes no `Disconnected`: the one entry is
         the bound's, before its reset point. `reconnect_fsm_.enter_awaiting_resend` has already run before the
         leaf. It writes no counter, and `install_reconnected_transport`'s `exit_awaiting_resend` clears it.
       - **103's recovery is unchanged.** A too-high Logout's spawn branch leaves the marker `none` (RR-2 item 1).
         So that frame's own ResendRequest passes the test while `peer_logout_.phase` is `pending`, with the state
         Active. The immediate-acknowledgement branch returns before the leaf.
       - **The enumeration (a recipe, re-run by T013 and on any change to a leaf's callers).** For each
         callback-first emitter (recipe above) that a pump frame calls, list its callers (`git grep -n '<leaf>(' --
         src/session/session.cpp`). Walk each caller back to its frame's start, `on_inbound_active_`'s entry state
         test, and not to `check_inbound`: a too-high frame meets none. Read every `co_await` and
         `FIXPP_INBOUND_SPLIT_AWAIT` on the way for whether it can suspend; `FIXPP_INBOUND_SPLIT_ENTRY`'s
         `throw_if_cancelled` and an `awaitable` whose body has no `co_await` do not. A leaf that has a suspending
         await on any path, and no test of its own that fails while frozen, takes the entry test and is listed
         here.
         - Out of the class: `toAdmin` sites that take no number (`emit_gapfill_async`, and the GapFill in
           `replay_outbound_range_`), and emitters that no pump frame runs (`send_impl`, `run_liveness_loop`,
           `run_logout_phase1`, read above by their own tests).
         - `inbound_logout_leaf_` is in the class's shape and already has its test: frozen implies `Disconnected`
           (INV-F), so `fsm_state_ != Active` refuses.
     - A callback-first emitter reachable while frozen and not listed above is a defect of this item, and goes to
       the orchestrator.
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
     - `set_next_*` refuse with success while frozen, so the freeze does not reach `apply_inbound_sequence_reset`'s
       failure write. After `close()`'s `seqnum_mgr_.drain()` the drained mutex returns `session_already_closed`
       before the freeze test, so that write is reached; it is made only when the state is not `Disconnected`
       already, by the same rule.
     - **A frame in flight across a route-B reset point, with no refused call (orchestrator ruling 2026-11-18,
       the T014 STOP).** An arm whose step after `inbound_logout_leaf_` is an unconditional `Disconnected` write
       adds a second entry when its frame suspended before the leaf and a reset point fell in that suspension:
       the Logout leaf returns success on `Disconnected`, and the write follows. Those writes are made only when
       the state is not `Disconnected` already, by the same rule:
       - `on_inbound_active_sending_time_reject_`'s step 3 (its `consume_rejected_seqnum_` persist and its
         Reject's store call precede the leaf);
       - `on_inbound_active_poss_dup_`'s Arm D step 3, the same shape, found by the recipe below;
       - `resend_gap_deadline_`'s final write and arm A's tail in `on_inbound_active_logout_`, already in this
         item's scope.

       Recipe: for each `inbound_logout_leaf_(` caller, read the write that follows its success return, and
       whether its frame can suspend before the leaf. Cell R-5i covers the two writes above (research D10).
     - Cells R-5a, R-5c and R-5f assert exactly one `Disconnected` entry in `Session::fsm_visit_history()`.
     - Not covered: a caller's write for `store_then_emit`'s store error after a refused reconcile
       (`Session::send`'s `disconnect_required` write, `run_liveness_loop`'s emit-failure writes). The store error,
       not the refusal, causes that write, and the base makes it too (B-104-8).
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


**RR-6 (ruling W: nothing is transmitted after a reset point; orchestrator ruling 2026-11-18, the T014 STOP;
research D11, "Frames in flight across `reached`").** This is the third STOP of one class, **work in flight across
the write of `reached`**. T011 found the manager writers, T014 the callback-first leaf, and this one the wire
writes.
- Before W, D-OUT-10 kept a frame still inside its store call off the wire (column C1 of research D11's table), but
  a frame whose store call had returned before the reset point was still written after it, after the reply.
- QuickFIX never sends such a frame: it handles a received Logout in one call, and a frame after a too-high Logout
  is never processed.
- Gate A loop 5 (round 1, N-1) added that every frame the rule holds back has **one ending**, whichever state it was
  in (item 3).

1. **The rule.** Once a reset point has run, fixpp transmits nothing more on that connection. A reset point is a
   writer that runs the receipt unit: arm A, L or S at its reset point, or the bound at its reset point.
2. **The discriminator, `wire_closed_by_reset_`.**
   - A new private member of `Session`, declared `bool wire_closed_by_reset_ = false;` in the members' existing
     style. A `Session` is constructed with it clear.
   - It is set in the reset point's own strand turn, in the same statement block as the marker's `reached` (RR-2
     item 3), through one helper that both writes are made in: the pump frame's `pending` → `reached` and the
     bound's `none` → `reached`. Arm A sets it in a crossing too (item 4).
   - `install_reconnected_transport` clears it, in the block that sets the marker to `none` and thaws the manager
     (RR-2 item 6, RR-5 item 1).
   - `close()`'s close-out never writes it (item 5). That is why it is not the marker: the close-out also writes
     `reached`.
   - It is not `frozen(freeze_key)`, which no production branch tests (RR-5 item 4).
   - It is a layout change (B-104-7).
3. **The test, its sites, and the one ending.**
   - **The test reads the member at each place a frame is handed to the transport.**
     - In `live_write_serialized_`, after the `write_gate_` grant (`guard = std::move(*lock_r);`) and before
       `live->async_write(frame)`. This covers every frame granted the gate after the reset point, including one
       that waited on it (column C2). A write already submitted to the transport before the reset point (column C3)
       was handed over before it, and completes.
     - In `store_then_emit`, immediately before the config-time sink's `transport_send_(frame)` (column C4).
     - In `replay_outbound_range_`'s `transmit_async`, before its `transport_send_(f)`. Its live branch is covered
       by `live_write_serialized_`.

     The two sink sites have no input today. On the sink nothing suspends from the store's return to the send, and
     a frame still in its store call ends at D-OUT-10 (below). They make "nothing more" hold by construction, and
     no cell claims them.
   - **D-OUT-10 while the member is set.** A frame whose store call returns after the unit's manager set meets
     D-OUT-10 (`if (stamped_seq >= seqnum_mgr_.peek_outbound())`).
     - While `wire_closed_by_reset_` is set and the call carries no exemption, D-OUT-10 returns the same ending as
       the test (orchestrator ruling on loop 5's N-1).
     - While the member is clear, D-OUT-10 keeps its `dispatch_aborted` ending. That covers the two lowerings that
       need no reset point (item 4), ruling R-19 unchanged.
     - The member is set only after a unit has lowered the manager, and the manager is frozen from then on, so no
       later lowering can be taken for it.
   - **The one ending (C1, C2 and C4 alike).** Success with no transmit, the shape of the `transmit=false` exit:
     - `store_then_emit` returns success before it updates `last_outbound_steady_`. `live_write_serialized_` has a
       distinct return for a hit, so `store_then_emit` can tell a held-back write from a written one and skip that
       update. `transmit_async` treats a hit as success.
     - The number stays consumed. The frame's store write precedes the unit's `reset_to`, which clears it (research
       R1's store order).
     - No caller sees an error, so none writes a `Disconnected`.
     - QuickFIX C++ gives a message persisted and not sent the same shape. In `Session::sendRaw`'s application
       branch, `persist(message, messageString)` is followed by `if (isLoggedOn()) { send(messageString); }`, and
       the function then returns `true`. QuickFIX/Go's `queueForSend` persists and queues the message and returns
       `nil`. QuickFIX/J's `sendRaw` returns its `result`, which stays `false` when `isLoggedOn()` is false: the J
       engine reports it as not sent. That difference is spec DQ-18.
   - **The callers' endings, for a frame held back by the rule** (re-derive: every `store_then_emit` caller, each
     read for the write on its non-success return):

     | Caller | Ending under the rule | Before the N-1 ruling, for a C1 frame |
     |---|---|---|
     | `send_body_` (`Session::send`) | `send` returns success; nothing transmitted; no `Disconnected` write | `send_body_`'s `disconnect_required \|\| … dispatch_aborted` write (a second entry), and `send` returned `dispatch_aborted` |
     | `run_liveness_loop`, Heartbeat | the loop goes on; its next test of `fsm_state_ != Active` returns | `if (!emit_r) { record_state_transition_(fsm_state::Disconnected); co_return; }` (a second entry) |
     | `run_liveness_loop`, TestRequest | the TestRequest is not written; the test before the grace sleep (`if (fsm_state_ != fsm_state::Active) { co_return; }`) returns, so no unanswered-TestRequest write | the same unconditional write |
     | `inbound_heartbeat_reply_leaf_`, `inbound_business_reject_leaf_` | the leaf returns success; the caller's persist is refused by RR-4 (success); the frame returns success | unconditional write on `!emit_r`, and the error |
     | `emit_session_reject_`, two-argument overload | returns success to `on_inbound_active_from_admin_failed_` / `on_inbound_active_unsupported_` | unconditional write on `!emit_r`, and the error |
     | `emit_session_reject_`, six-argument overload | returns success; its `disconnect_unless_superseded_(arm)` is not reached | `disconnect_unless_superseded_` (already conditional), and the error |
     | `inbound_resend_request_leaf_` | returns success | unconditional write on `!emit_r`, and the error |
     | `inbound_reject_leaf_`, `inbound_reject_best_effort_leaf_`, `inbound_logout_leaf_`, `apply_inbound_sequence_reset`'s Reject | unchanged: each discards the emit result (`(void)emit_r;` or `(void)co_await`) | the same |
     | `run_logout_phase1` | carries the exemption: transmitted (item 4) | — |
     | `emit_initiator_logon_`, arm S against a direct caller's overlap with its emit (item 6). Its callers are `open()` and `drive_reconnect()` (`co_return co_await emit_initiator_logon_();`) | `open()`: returns success, with the state `Disconnected` (arm S recorded it) and nothing transmitted. `open()` then goes on as after any successful Logon emit: it seeds `last_inbound_steady_` and `last_outbound_steady_`, and fires `onCreate` once, with the state already `Disconnected`. A throwing `onCreate` gets `close(terminal)` and `app_callback_threw`. No `onLogout` fires, because the state was never Active. A direct, non-engine-managed initiator meets this through `open()`; under the Engine there is no input, because an engine-managed initiator defers its Logon. `drive_reconnect()`: no conforming input. Its standalone-initiator precondition (`session.hpp`, `drive_reconnect`'s doc comment: "the caller's own reader has left live_transport() before the call, and starts on the new live_transport() after it returns") and, under the Engine, the pump starting after the return deliver no frame to arm S during its emit (research D11, `hydrate`; T013) | `open()`: the unconditional write in `if (!emit_r)` (a second entry), and `open()` returned `dispatch_aborted`; `onCreate` did not fire |
     | the Logon arms' emitters (`refuse_logon_with_logout_`, `inbound_logon_sent_logout_leaf_`, `inbound_acceptor_reply_logon_leaf_`, `honor_peer_next_expected_`) | no input: a Logon arm is the one pump frame (INV-P), and no bound exists before Active | — |

   - **Owner-visible (Gate B sign-off).** Two public returns change in the window from a frame's store call to a
     reset point:
     - `Session::send` returns success for a send that is not transmitted, where the design before the ruling
       returned `dispatch_aborted` and the base transmitted it;
     - a direct initiator's `open()`, in the arm S overlap, returns success with the session `Disconnected`, and
       fires `onCreate` once on that path.

     B-104-8 states both. The `send` result departs from QuickFIX/J (spec DQ-18). `open()`'s needs no row: QuickFIX
     cannot produce the overlap (item 6).
   - **The receipt's own reply is complete before the member is written.**
     - Route A: `on_inbound_active_logout_` awaits `inbound_logout_leaf_`, and with it the whole `store_then_emit`,
       transmit included, before its `Disconnected` write (RR-3 step 1 before step 2).
     - Route B: `acknowledge_peer_logout_` awaits the same leaf before `record_state_transition_(fsm_state::
       Disconnected)`.
     - Arms L and S owe no reply (RR-3 step 1).
4. **The one exemption: fixpp's own Logout in a crossing (sub-ruling of W).**
   - **The crossing.** Phase 1 of `close(graceful)` has numbered our Logout and holds `logout_in_flight_`. A peer
     Logout then reaches arm A, and arm A reaches its reset point without a reply. Two states:
     - in Active, its reply leaf sends nothing (`if (logout_in_flight_ || fsm_state_ != fsm_state::Active)`);
     - in LogonReceived, the arm sends no reply in any case.

     A too-high peer Logout delivered then crosses ours the same way through arm B's immediate acknowledgement
     (orchestrator ruling 1 on the sixteenth run): `on_inbound_active_too_high_logout_` takes its immediate branch (`logout_in_flight_ || closing_or_stopped_()`),
     `acknowledge_peer_logout_`'s reply leaf sends nothing on `logout_in_flight_`, and B records `Disconnected` and
     reaches its reset point, which sets the member. The exemption is not keyed on the arm, so the ending is the
     same.
   - **Without the exemption**, our Logout is held back by D-OUT-10 or by item 3, and no Logout reaches the wire.
     QuickFIX writes ours before it handles the peer's: QFC and QFGo generate it on the session's own processing
     thread. So QuickFIX's ending is exactly one Logout, ours.
   - **LogonReceived, classified from source.**
     - `run_logout_phase1` runs when `close()`'s test `!close_escalated_ && (fsm_state_ == fsm_state::Active ||
       fsm_state_ == fsm_state::LogonReceived)` holds.
     - Arm A meets LogonReceived only after the acceptor's Logon arm has left with the state still LogonReceived:
       its exit superseded by a `close()` (`logon_arm_superseded_(fsm_state::LogonReceived)`). While the arm runs it
       is the one pump frame (INV-P). That `close()` can be a `close(graceful)` whose phase 1 then numbers its
       Logout. An in-sequence peer Logout delivered while that Logout is in its store call reaches arm A in
       LogonReceived: on_inbound_active_'s entry admits LogonReceived, and nothing there refuses on `closing`.
     - So the crossing has an input in LogonReceived.
     - The exemption gives it the base's ending: the base runs no unit at the receipt, so our Logout is transmitted.
       Here it is transmitted at its pre-reset number. The superseded arm's reply Logon was stored and not
       transmitted, as on the base.
     - QuickFIX has no counterpart: it writes its Logon reply inside `nextLogon`, so it has no LogonReceived window.
     - The exemption handles this route as it handles Active.
   - **The carrier.** A private scoped enumeration, `wire_reset_exemption` with values `none` and
     `crossing_logout`, as one explicit parameter of `store_then_emit` and of `live_write_serialized_`, defaulted to
     `none`.
     - **The census, by category** (re-derive with `git grep -n 'crossing_logout' -- src include`):
       - the declaration;
       - **one origin**, `run_logout_phase1`'s call;
       - the pass-through from `store_then_emit` to `live_write_serialized_`;
       - the comparisons: in `store_then_emit`, D-OUT-10's lift and the sink-site test; in `live_write_serialized_`,
         the test after the grant.
     - A second origin is a STOP finding. `transmit_async` takes no exemption, and phase 1's Logout never passes
       through it.
     - Known positive: the recipe finds `run_logout_phase1`'s call.
   - **What it exempts.**
     - Item 3's test, at the two sites the carrier reaches: `live_write_serialized_` and `store_then_emit`'s sink.
       `transmit_async` carries none.
     - D-OUT-10's ending, but only while `wire_closed_by_reset_` is set. The reason is not that a set member proves
       the lowering came from the unit: a take-back before the unit is overwritten by it. The reason:
       - once the member is set, the manager is frozen and item 3 holds back every other frame, so no frame of the
         old numbering can follow ours;
       - a frame that took our number before the unit (by a take-back) has left the next outbound number above ours.
         D-OUT-10 would then not have refused ours either.
     - Without the member, D-OUT-10 refuses ours as before (`dispatch_aborted`).
     - Nothing else: not RR-4's store-call boundary, which the Logout passed before `reached`, and not RR-5.
   - **What else can lower the outbound manager while phase 1's Logout is in its store call.** Re-derive with
     `git grep -n -E 'seqnum_mgr_\.(take_back_outbound|hydrate|reset_to_one|set_next_outbound)\(|reset_seqnums_to_one_durable\(|run_reset_unit_\(' -- src/session/session.cpp`,
     and read each hit against "phase 1 has numbered its Logout, and the state is Active or LogonReceived".
     - Two can:
       - another frame's persistent-store reconcile (`(void)co_await seqnum_mgr_.set_next_outbound(*dk)` in
         `store_then_emit`), on any concurrent emitter;
       - another frame's cancelled store call (`seqnum_mgr_.take_back_outbound(stamped_seq)`), which lowers to that
         frame's number when ours is above it.

       When no crossing unit follows, neither sets `wire_closed_by_reset_`, so D-OUT-10 refuses our Logout as
       before.
     - The others cannot meet it:
       - the reset-before-Logon in `emit_initiator_logon_`, and the 141=Y unit in `on_inbound_logon_sent_`, run in
         LogonSent, where phase 1 does not run;
       - `on_inbound_not_connected_`'s `reset_on_logon` reset and its `ensure_hydrated_` run before its
         `record_state_transition_(fsm_state::LogonReceived)`;
       - its 141=Y unit follows that write with no suspension between, and the unit's manager set is inline
         (research R1), so the lowering precedes any phase-1 number;
       - `close()`'s teardown reset runs after `run_logout_phase1() || …` has completed. At the pinned asio,
         `operator||` awaits `make_parallel_group(…)`, whose completion carries "all operations' completion handler
         arguments";
       - the bound cannot acknowledge once `close()` has claimed it at its first commit, which precedes phase 1
         (O22);
       - arm L follows phase 1's `LogoutSent`, which is written after its emit returns;
       - arm S runs in LogonSent;
       - the close-out runs no unit.
     - Arm B's immediate acknowledgement can: a too-high peer Logout in Active while our Logout is in its store
       call takes the immediate branch (a Logout in flight; `close()` has begun), and its unit is a reset point that
       sets `wire_closed_by_reset_`, so D-OUT-10's lift is unchanged (orchestrator ruling 1 on the sixteenth run).
     - So the reset points that can meet our Logout are arm A's crossing unit, in Active or LogonReceived, and arm
       B's immediate acknowledgement, in Active.
   - **The store side is unchanged.**
     - Our Logout's number is assigned in phase 1's turn and kept. Its store call was issued before `reached`, and
       it runs to its end (RR-2 item 5).
     - Against the unit's `reset_to`, it is ordered as any frame in its store call at a unit: the store applies it
       before the later `reset_to` (research R1's store-order condition, T009). The unit therefore clears it, and
       the store ends `(1, 1)`.
     - Only the transmit changes: our Logout goes out at its pre-reset number.
   - **Phase 1 afterwards.** `store_then_emit` returns success, and phase 1's existing test returns ("an inbound
     ending recorded Disconnected while this Logout was in its store call, so it was that ending's reply: no
     LogoutSent and no wait for one"). The state history keeps one `Disconnected` entry, arm A's.
   - **Wire ending.** Exactly one 35=5, ours, then nothing more on that connection. Any other frame in flight (a
     Heartbeat, an application send) is held back by item 3, because arm A set the member.
5. **The close-out is exempt (W-2).** It runs no unit and does not write the member, so O13-r's endings are
   unchanged.
   - The sink's transmit (B-563-5) holds without `reset_on_disconnect`, that is, when no teardown reset has lowered
     the manager before the held call returns. Under rod, the teardown's `reset_to_one` precedes its `store_->reset()`,
     so D-OUT-10 refuses the reply with the member clear (`dispatch_aborted`).
   - On a live transport, `close()`'s own `close_async()` and `write_gate_.cancel_and_drain()` still decide.
6. **Classified from source.** Arm S against a direct caller's overlap with `open()`'s Logon suppresses nothing that
   is owed:
   - if the peer's Logout arrives during `ensure_hydrated_`'s reads, `hydrate` is refused and no Logon is built;
   - a Logon still in its store call ends at D-OUT-10 with the member set: item 3's ending, so `open()` returns
     success with the session `Disconnected` and fires `onCreate` (item 3's table; owner-visible). `drive_reconnect()`
     has no conforming input here (item 3's table);
   - no second writer holds the gate before Active, so there is no C2;
   - a Logon whose write is already submitted is past item 3's test and completes.

   The peer's Logout arriving before our Logon is on the wire is an order QuickFIX cannot produce: it writes its
   Logon before it reads.
7. **Cost.** One byte test per write, at the transmit, and one at D-OUT-10's refusal branch. The exemption is a
   parameter compare on the same path.
8. **Cells.**
   - R-5f: a pump frame's Reject queued behind the bound's reply.
   - R-5g: the crossing.
   - R-5h: a liveness Heartbeat, in C2 and C1, and C1 at arm L, with one ending.
   - R-5j: arm S against a direct caller's `open()` overlap, and `open()`'s ending.
   - R-5k: our Logout with the member clear, behind a lower frame whose store call was cancelled, on a volatile
     store: D-OUT-10's `dispatch_aborted` ("What it exempts").
   - R-1l `HeldInReplyStore`'s sink twin: the close-out exemption.
   - R-5i: the two conditional step-3 writes (RR-5 item 6).

   All are in research D10.

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
| O5 | reply does not fit 256 bytes | no reply; reset point reached; reset | n/a | as A | n/a | n/a (no fixed buffer) | no conforming input in arm A's in-sequence reply (recipe in research D10's R-1n row); T-4a's Text-carrying Logout is the overflow witness for the Logon arms |
| O6 | reply `toAdmin` throws | leaf records Disconnected, `app_callback_threw`; no reset | n/a | as A | n/a | QFC: no reset (propagates out of `sendRaw`); QFJ: resets (`sendRaw` catches `Throwable` from `toAdmin`, then sends); QFGo: no throw path | R-1n |
| O7 | reply `assign_outbound` fails | leaf records Disconnected, the error; no reset | n/a | as A | n/a | n/a | R-1n |
| O8 | reply store fails (persistent) | not transmitted; reset point reached; reset | n/a | as A | n/a | reset (QFC, QFJ catch the store's `IOException` in `sendRaw` and continue) | R-1n |
| O9 | reply store call cancelled: `store_cancelled` returned, or `operation_aborted` thrown (D-OUT-2 gives both timings the one block after the `try`) | the number taken back (D-OUT-1); Disconnected; no frame; `operation_aborted` unwinds the arm and its owner clears the marker (RR-2 item 2); no reset; `close()` is not held | n/a | immediate acknowledgement: as A. The bound: 103's handler ends it closed; the marker untouched (`none`); no reset | n/a | n/a | R-1v (the returned timing, which reaches the shared block) |
| O10 | reply write fails | stored, not on the wire; reset | n/a | as A | n/a | reset (QFC `send` returns false; `nextLogout` continues) | R-1n |
| O11 | inbound persist fails | Disconnected, the error; no reset | n/a | n/a | n/a | no reset (QFC, QFJ: `incrNextTargetMsgSeqNum` throws `IOException`) | R-1n |
| O12 | `close()` during, before the reset point, with a clock | `close()` waits; the arm reaches its reset point and resets once, or exits and does not (with no store, the unit sets no in-flight flag); `close()` returns as soon as the marker has left `pending` with no unit in flight, not at `logon_timeout_ms`, and emits no `close_reset_wait_expired` | no input: L does not suspend before its reset point (research R1) | immediate acknowledgement: as A. The bound, not yet `acking`: claimed at `close()`'s first commit, no reset (O18). Already `acking`: `close()`'s bound join waits for it, so it reaches its reset point and its unit ends before the wait begins (RR-2 item 4), one reset | no input: S does not suspend before its reset point; a frame that arrives after `close()` began meets `logon_arm_superseded_` and acts on nothing, no reset | n/a (QuickFIX serialises receipt and disconnect) | R-1k, R-1z (A beside a claimed bound), R-1f, R-1w |
| O13-r | `close()` during, before the reset point, wait expired or no clock, the arm held in its reply's store call | no reset under rol (the receipt did not reach its reset point); the marker is closed out to `reached`. The held call completes after `close()` returned (RR-2 item 5): the reply is stored. On a live transport it is not transmitted, because `close()` has run `close_async()` and `write_gate_.cancel_and_drain()`, so the write fails with `dispatch_aborted`; without `reset_on_disconnect` a config-time `transport_send` sink transmits it (as `B-563-5`): no teardown reset has lowered the manager before the held call returns, and the close-out does not set `wire_closed_by_reset_` (RR-6 item 5). Under rod the teardown's `reset_to_one` precedes its `store_->reset()`, so D-OUT-10 refuses it. The persist is refused (RR-4), so the store's inbound stays N while the manager's is N+1 (`check_inbound`, before the arm). No unit. The event and `fromAdmin` run after `close()` returned (fixpp#597); a Reject `fromAdmin` draws is refused at `assign_outbound`, before its `toAdmin` (RR-5; the close-out froze the manager) | no input (as O12) | the bound: no input, because `close()`'s bound join precedes the wait, so it never closes out a bound's receipt (RR-2 item 4). Immediate acknowledgement (no clock is one of its conditions): as A, without the persist and `fromAdmin` (B has neither) | no input (as O12) | n/a | R-1l (`HeldInReplyStore`), R-5e |
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
| O21 | a writer in flight at the reset point writes the manager after it (RR-5): a route-B pump frame suspended in a store call, or `Session::send`, the liveness loop or phase 1 suspended in `store_then_emit`'s failure reconcile, on any route | an application send or liveness frame in flight: its reconcile's `set_next_outbound` refused; the send returns the store's error, nothing transmitted; manager `(1, 1)` | as A, phase 1's own reconcile included | the pump frame in flight: its GapFill's `set_next_inbound` refused (the frame returns success, nothing emitted); its Reject's `assign_outbound` refused before `toAdmin` (nothing numbered, stored or sent); its reconcile refused; a too-high ResendRequest's own ResendRequest refused at `inbound_resend_request_leaf_`'s entry test, before its `toAdmin` and number (RR-5 item 6). Manager and store `(1, 1)`; one `Disconnected` entry | no pump writer: S is the only pump frame and no bound exists before Active. A direct caller's `open()` Logon still in its store call: its reconcile refused | n/a: QuickFIX handles each frame to its end, a too-high Logout included at its receipt (DQ-12; QFC and QFJ `nextLogout` `verify(logout, false, false)`, QFGo `handleLogout` `verifySelect(msg, false, false, true)`), and its reset discards every earlier increment. On its processing thread no writer of its own is in flight at the reset. QFC and QFJ user-thread sends, and QFJ's timer thread, are not serialised with `nextLogout` and can land between the reply and `disconnect()`. fixpp's suppression (RR-6) therefore falls within QuickFIX's envelope. No departure on the wire: a frame that 103's bound lets in after a too-high Logout is never processed by QuickFIX, and under RR-6 nothing of it reaches fixpp's wire after the reset point (research D11). A `Session::send` held back there returns success, which departs from QuickFIX/J's `send` result (spec DQ-18) | R-5a, R-5b, R-5c, R-5d, R-5f, R-5h |
| O22 | `close(graceful)` started after the reset point (sub-ruling 4) | phase 1 does not run: `close()`'s phase-1 test needs Active or LogonReceived, and the state is `Disconnected` from RR-3 step 2. No Logout, no `toAdmin`, no `assign_outbound`. A `close(graceful)` that passed that test before the receipt: phase 1 waiting at the replay gate is woken by the Disconnected edge and takes its "not sent" exit before its stamp, its `toAdmin` and its number; otherwise phase 1 runs from the test to its `assign_outbound` in one strand turn, before the receipt (research D11, "Phase 1"). Either way it never meets a frozen manager | phase 1 already sent its Logout (that is what LogoutSent means) | as A (`close()` claims the bound at its first commit, before phase 1) | as A (phase 1 needs Active or LogonReceived) | n/a | read from source, no cell. R-3, when T011 reads it drivable, runs this input: `close(graceful)` held in its flush while arm A in LogonReceived reaches its reset point |
| O23 | crossing: phase 1 of `close(graceful)` has numbered its Logout and holds `logout_in_flight_` when a peer Logout reaches arm A, in Active or in LogonReceived (after a Logon arm superseded by that `close()`), rol set (RR-6 item 4) | A sends no reply (in Active its reply leaf refuses on `logout_in_flight_`; in LogonReceived no reply is owed); A records `Disconnected` and resets at its reset point, which also sets `wire_closed_by_reset_`. Our Logout, exempt from D-OUT-10 and from RR-6's test, is transmitted at its pre-reset number. Wire: exactly one 35=5, ours, and nothing else after the reset point. Store `(1, 1)`; one `reset_to(1, 1)`; one `Disconnected` entry; phase 1 returns by its "no LogoutSent" test. Rol off: our Logout as on the base | no input (phase 1 writes LogoutSent after its emit) | the bound: no input (`close()` claims it before phase 1). The immediate acknowledgement of a too-high peer Logout delivered while our Logout is in its store call: as A in Active (no reply on `logout_in_flight_`; Disconnected; its reset point sets `wire_closed_by_reset_`; our Logout transmitted at its pre-reset number; exactly one 35=5, ours, then nothing) (orchestrator ruling 1 on the sixteenth run) | no input (phase 1 needs Active or LogonReceived) | one Logout, ours: QFC and QFGo generate it on the session's own processing thread, before the peer's is handled, which `nextLogout` then takes as "Received logout response" | R-5g |

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
  (`WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore`) is re-pinned to `NewSeqNo=6`, its fixture's manager seeded to
  the store's next outbound number: NewSeqNo is the session's next outbound number (`peek_outbound()`), QuickFIX
  C++'s value whenever store and manager agree (orchestrator ruling on the T073 STOP).
- 099's ending for a Logon acknowledgement its validating-initiator test (data-model E-7's S3) refuses, NextNumIn
  consumed (pinned "c" by `W5PerShape.Initiator141YWith1137_4_NoReset`, `W5LogonSent.Ending`'s
  `LS_e_*_Logon_141_Y_1137_4` rows and `W25Initiator.Reply141YWithBad1130_NoReset`), for an unasked 141=Y
  acknowledgement only: by U1 and U2's "no sequence check" (T069). Such an acknowledgement is refused with NextNumIn
  unchanged ("k"), as QuickFIX's `generateReject` leaves it for a Logon; no reset, and the Reject's outbound number,
  are unchanged. The ordinary acknowledgement keeps "c" (L-104-8).
- 093 `contracts/inbound-frame-dispositions.md` **C-6**'s `close()` bullet ("`close()` waits only when it is about
  to issue its teardown reset"), and the event-driven shape of that wait, whose one waker was the unit's
  completion: by RR-2 item 4 and RT-5 (B-104-2). B-093-11 gains the same pointer.
