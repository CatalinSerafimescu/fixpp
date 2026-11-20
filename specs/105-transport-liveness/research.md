# Research: 105 transport and liveness (B27)

**Feature**: [spec.md](spec.md) · **Plan**: [plan.md](plan.md) · **Base**: `52aeed4e` (fixpp `main`).

Phase 0 of `/speckit-plan`. Every behaviour claim below was re-read in source at the base and is cited by
symbol. The re-derive in the parent repo (`research/findings/b27-rederive-2026-11-20.md`) and the SecondBrain
pages were used as leads only. Owner rulings R-1 to R-4 and clarifications Q1 and Q2 are final and are not
re-opened here.

Abbreviations: S = `src/session/session.cpp`, E = `src/session/engine.cpp`. "The edge" is the
`Disconnected` write in `Session::record_state_transition_`. "The pump" is `run_read_pump` in E.

## Findings that shape every decision

- **F-1. The edge is synchronous and `noexcept`.** `record_state_transition_` writes the state history, clears
  `logout_in_flight_`, wakes the replay gate and the too-high Logout's bound, and fires `onLogout` on the
  Active→!Active edge. Nothing it does can suspend, and a `co_spawn` there could throw `bad_alloc` into a
  `noexcept` function.
- **F-2. `close()` cannot be awaited by a detached root.** `Session::close` polls `liveness_counter_` and
  `peer_logout_bound_counter_` to zero before its teardown. `run_liveness_loop` and `run_peer_logout_bound_`
  hold those counters until they exit, so an inline `co_await close()` from either never completes.
- **F-3. `close()` closes the transport before its 104 receipt wait.** `close()` runs `root_cancel_.emit` and
  `close_async()` and only then waits for `logout_seen_ == receipt_marker::pending` or
  `reset_unit_in_flight_`. `acknowledge_peer_logout_` and `on_inbound_active_logout_` record `Disconnected`
  before their reset point (104 RR-1, "Disconnected is recorded first, so onLogout sees the pre-reset numbers").
  A close started at the edge would therefore close the transport before the reset unit runs, which FR-005
  orders after it.
- **F-4. A terminal `close()` is permanent.** `close()` moves `state_` to `lifecycle::closing` and then
  `closed_drained`; `Session::drive_reconnect` refuses a closing or closed Session (`closing_or_stopped_`).
  A direct initiator that reconnects through `drive_reconnect` must therefore get its *transport* closed, not
  its Session.
- **F-5. The pump already closes through `close(terminal)`.** `stop_pump` awaits
  `session.close(close_mode::terminal)` on EOF, a read error, an over-limit frame, an error from
  `on_inbound_frame` and the establishment deadline. `close()` reaches the transport only through
  `close_async()` (B-348-1, amended), and `asio_tls_transport::close_async` cancels a suspended SSL operation,
  waits for it to unwind, then writes close_notify; a second `close()` or `close_async()` arriving meanwhile is
  subsumed ("Idempotent, exactly like close()").
- **F-6. `close_async` clobbers its chain's cancellation state.** `asio_tls_transport::close_async` resets the
  whole co_spawn chain to `disable_cancellation` and leaves it so. It must not run inside the liveness loop's
  chain, which relies on total cancellation at its sleeps (D-536-2).
- **F-7. The inbound depth exists.** `on_inbound_active_` holds an `inbound_depth_holder` for the whole
  processing of a frame; `inbound_depth_` is non-zero exactly while a frame is inside an arm.
- **F-8. A Logout owner exists.** `inbound_logout_leaf_` sets `logout_in_flight_` before its first suspension and
  writes nothing when it is already set; the only clear is the edge. `send_body_` refuses a send while it is set
  (103 FR-021). `close()`'s graceful phase 1 poll-joins it (103 D4.6). The too-high Logout path acknowledges at
  once when it is set (`on_inbound_active_too_high_logout_`).
- **F-9. The acceptor's first frame bypasses the pump.** `run_accept_loop` step 8 awaits
  `session->on_inbound_frame(first_frame)` and discards the result (`(void)deliver_r;`).
- **F-10. Pre-session rejects are silent.** Each reject branch of `run_accept_loop` before `open()` is
  `(void)transport->close(); continue;`, and a failed `asio_listener` construction is `catch (...) { co_return; }`.
  `log_role_loop_open_failure` already logs through the entry's logger with no Session.

## D1 — How a `Disconnected` edge gets the transport closed (FR-001, FR-002, FR-008)

**Decision: an edge latch, consumed by the root that owns the ending.**

1. **The latch.** A private `bool` member of `Session`, `connection_end_owed_`. The edge sets it, and only the
   edge, when all of these hold: `state_ == lifecycle::open` (not inside `close()`), `!reconnect_drain_in_flight_`
   (a new flag held only around `drain_for_reconnect_`'s call in `drive_reconnect`, B-103-9), and no R-2 wait is
   `armed` (D2). `reconnect_in_flight_` cannot serve: its holder in `drive_reconnect` stays alive across
   `co_await emit_initiator_logon_()`, so it would hide every Logon-emission ending of a direct initiator.
   `install_reconnected_transport` records only `LogonSent` (and nothing on its refusal path), so the narrower
   window exposes no further write; the census re-reads it at the implementation head. It is set before `onLogout` is
   fired. Setting it is one store: no suspension, no allocation, no spawn (F-1).
2. **Consumer (a), the pump.** After every `on_inbound_frame` return, the pump closes as it closes on an error:
   `if (!r || session_engine_access::take_connection_end(session)) co_await stop_pump();`. `take_connection_end`
   tests and clears the latch. The test runs after the arm has returned, so after its reply Logout, its persist
   and its receipt reset unit (F-3, FR-005, FR-008). The close is `close(terminal)`, the path F-5 describes, so
   FR-002 holds by construction.
3. **The acceptor's first frame goes through the pump.** `run_read_pump` gains a `first_frame` parameter
   delivered before the coalesced surplus, under the same disposition as every later frame. `run_accept_loop`
   step 8 is deleted (F-9, FR-006). `run_connect_loop` passes an empty span. The first frame is delivered without
   the loop's `expired()` pre-check, as today: `read_first_frame_bounded` already bounded its read by the
   establishment time left, and a pre-check would turn a Logon read at the deadline's edge into a timeout.
4. **Consumer (b), every non-pump root, at its exit.** `Session::release_connection_end_()`: when the latch is
   set and `inbound_depth_ == 0`, clear it and spawn `close_connection_(conn_gen_)` on the session's executor,
   detached, holding a new join counter `connection_close_counter_` (incremented before the spawn, as the
   liveness counter is, FQ-C). The spawn is on `exec_.underlying()`, never the wrapper executor (the TSan class
   `brain` records for wrapper spawns). When `inbound_depth_ != 0` the latch is left set: a frame is inside an arm, and the
   pump consumes the latch when that arm returns (F-7), so no store work an arm owns is cut. A spawn that throws
   leaves the latch set; the next consumer, `close()` or `Engine::stop()` closes the connection.
5. **`close_connection_(gen)`** closes the transport and nothing else: when `state_ == lifecycle::open` and
   `conn_gen_ == gen`, `co_await live_transport_shared_()->close_async()`. It runs on its own spawn chain, so
   `close_async`'s cancellation clobber stays there (F-6).
   - **Engine session:** the pump's parked read completes with an error (`close_async` cancels it, and a
     re-read is refused once the transport is closed), the pump runs `stop_pump`, and `close(terminal)` joins.
   - **Direct Session:** the connection ends and the Session stays open in `Disconnected`, so
     `drive_reconnect` can drain and install a new transport (F-4; spec Assumptions, "a reconnect is not
     blocked").
6. **Joins.** `close()` clears the latch when it commits `closing`, and before its own `close_async()` joins
   `connection_close_counter_` by post, as it joins the liveness loop. So the two never run `close_async` on one
   transport at once (FR-002). `drain_for_reconnect_` clears the latch and joins the counter before step 3's
   quiescence test, so no `close_connection_` outlives its connection. `~Session` asserts the counter is zero,
   as it asserts the bound's.
7. **The root set** (consumer (b)), derived below: `run_liveness_loop`, `run_peer_logout_bound_`, the R-2
   bound `run_ending_logout_bound_` (D2), `Session::send` and `Engine::send`'s session body (through
   `send_body_` and `send_deferred_`), `Session::drive_reconnect`, and `Session::open` where it emits a
   direct initiator's Logon (`emit_initiator_logon_`). `Session::close` is excluded by the latch's condition.

**The census, by execution context.** A `Disconnected` write is classed by the root it runs under, not by the
function that holds it: `store_then_emit`, `emit_session_reject_`, `inbound_logout_leaf_`,
`record_disconnected_unless_already_`, `disconnect_unless_superseded_` and `refuse_logon_with_logout_` are
each reached from several roots.
- *The write sites.* `grep -nE 'record_state_transition_\(fsm_state::Disconnected\)|record_disconnected_unless_already_\(\)|disconnect_unless_superseded_\(' src/session/session.cpp`.
  Known positive: `resend_gap_deadline_`.
- *The roots.* Every `asio::co_spawn` in S and E (`grep -n 'co_spawn' src/session/session.cpp src/session/engine.cpp`)
  and every public `asio::awaitable` member of `Session` in `include/fixpp/session/session.hpp`.
- *The walk.* For each write site, `codegraph_callers` transitively up to a root (projectPath of the tree being
  edited). Each root's class is then fixed: the pump (consumer (a)), a member of the root set (consumer (b)), or
  excluded (`close`, `run_logout_phase1` under it, `drain_for_reconnect_`).
- *Known positives the walk must reproduce.* The unanswered-TestRequest write maps to `run_liveness_loop`; Arm B's
  too-low write in `on_inbound_active_out_of_sequence_` maps to the pump; `send_body_`'s failure write maps to
  the send root. A walk that misses one of these is wrong.
- *The result is a table in tasks.md* (one row per site, its root(s), its class and its cell), regenerated at the
  implementation head. FR-001's "zero sites without a cell or a named exemption" (SC-001) is read from it.

**Why it holds for the whole class.** Every write goes through the edge, which is the only setter. Every root
that can reach the edge is a consumer or an exclusion. A new write site needs no change; a new root does, and
the census recipe finds it.

**Alternatives rejected.**
- *Post `close(terminal)` from the edge.* It spawns inside a `noexcept` function (F-1). It would run at the
  arm's next suspension, so it could cut the reply Logout's write or close before the reset unit (F-3). It makes
  a direct initiator's Session permanently closed (F-4).
- *A pump-side state check only, with an active wake of the parked read.* Waking a post-Active read needs either a
  race on every read (`async_read_some || waker`: a coroutine frame per read on the hot path, Article VIII §5 and
  XV §1) or `Transport::cancel()` from the detached root, which on TLS is the cancel without the join that
  `close_async` adds. It also leaves an engine-less Session's transport open.
- *A latch consumed only by the pump.* Nothing would close for the liveness loop, the bounds and a send while the
  pump is parked in a read.
- *`co_await close()` at the end of each detached root.* It deadlocks on the root's own join counter (F-2).

## D2 — The R-2 wait after a Logout fixpp sends to end the session (FR-004, FR-004a, Q2)

**Decision.**
1. **One helper starts every R-2 ending.** `Session::send_ending_logout_(text)` writes the Logout through the
   existing leaf for the state (`inbound_logout_leaf_` after Active; `refuse_logon_with_logout_`'s body and
   `inbound_logon_sent_logout_leaf_` before it), then arms the wait **only if the Logout was transmitted**. The
   leaves return success also when nothing went out: the Logout did not build (`if (lo_result)` false, the long
   CompIDs case), its emit failed (`(void)emit_r`), or `store_then_emit` returned success without transmitting
   (a superseded Logon arm; a frame held back while `wire_closed_by_reset_` is set; both stated in its
   declaration comment in `session.hpp`). So `store_then_emit` gains a private out-parameter set only after its
   transmit step succeeds, the leaves pass it up, and the helper arms on it. A Logout not transmitted arms
   nothing: the caller records `Disconnected` as today, the edge sets the latch, and the connection closes at once
   (contract C-1 rows 1 and 11; `Capi537Witness.Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilently` flips to
   "ends silently and closes at once"). The callers are the fixpp-initiated ending
   Logouts: `resend_gap_deadline_`, `on_inbound_active_sending_time_reject_`, the OrigSendingTime branch of
   `on_inbound_active_poss_dup_`, Arm B of `on_inbound_active_out_of_sequence_`, and every caller of
   `refuse_logon_with_logout_`, `refuse_too_low_logon_` and `inbound_logon_sent_logout_leaf_`. The replies are not
   callers: `on_inbound_active_logout_` and `acknowledge_peer_logout_` stay under FR-005. Census:
   `grep -nE 'inbound_logout_leaf_\(|refuse_logon_with_logout_\(|refuse_too_low_logon_\(|inbound_logon_sent_logout_leaf_\(' src/session/session.cpp`,
   each hit classed "initiates" or "replies"; known positive: `resend_gap_deadline_` initiates.
2. **The wait's state.** `ending_wait_`: `none`, `armed` (with `deadline`) or `resolved`.
   - **After Active the FSM stays `Active`.** The callers above no longer record `Disconnected` after the
     leaf. `logout_in_flight_`, set by the leaf, is the existing marker: `send_body_` refuses an application
     send (F-8, 103 FR-021), the leaf writes no second Logout, and a graceful `close()` poll-joins it. Every
     other frame runs the Active arms, as Q2 rules: `fromApp` and NextNumIn advance, a TestRequest is answered,
     a ResendRequest is answered, the sequence checks apply.
   - **Before Active the FSM records `Disconnected`** as each refusal does today, with the wait armed, so the
     edge sets no latch. "As in Active" cannot apply to a session that never logged on; every frame is drained by
     the `Disconnected` row as today, except the peer's Logout (item 4).
3. **The bound.** `deadline = deadline_after(now, logout_disconnect_timeout_ms)` on `effective_clock_`, read once
   when the leaf returns (the Logout's write), never moved by a later send (FR-004; QuickFIX/Go's rule, not
   C++'s and /J's sliding `lastSentTime`). Before Active on an Engine session the pump's establishment deadline
   still runs (it is a pump local, keyed on `!has_reached_active`), so the connection closes at the earlier of the
   bound and that deadline, and a refused, unauthenticated peer is never held longer than `logon_timeout_ms` allows
   today. At that deadline the pump closes *without* the establishment-timeout event and record, because the
   session has recorded its own ending (D4). A detached `run_ending_logout_bound_(gen)` waits on it, in the shape of
   `run_peer_logout_bound_` (a join counter `ending_logout_bound_counter_` that `close()` and
   `drain_for_reconnect_` join, a published wake, `detail::await_deadline` raced with a timer the wake cancels,
   a generation assert, an exception handler that ends the wait closed). At expiry it sets `resolved`, records
   `Disconnected` when the state is not that already (the edge then sets the latch), or sets the latch directly
   when it is, and calls `release_connection_end_()` on exit (D1 item 4). The edge wakes it.
4. **The peer's Logout resolves the wait.**
   - After Active it reaches `on_inbound_active_logout_` (in sequence) or the too-high path, which acknowledges at
     once because `logout_in_flight_` is set (F-8). Its leaf writes nothing (the owner is set), so it is a
     response, as FR-004 requires. Each receipt arm sets `resolved` at its entry when the wait is `armed`, so its
     own `Disconnected` edge sets the latch; its reset point runs as in Active (104 RU-1 to RU-3, B-104-1), and
     the pump closes after the arm returns (D1 item 2). Receipt arms: `grep -n 'pending_receipt_owner const' src/session/session.cpp`
     plus `acknowledge_peer_logout_`; known positive `on_inbound_active_logout_`.
   - In `Disconnected` with the wait `armed` (before Active, or after a silent ending during the wait, item 6),
     the `Disconnected` row (a new arm that `on_inbound_cold_` reaches only while a wait is armed) scans the frame's
     header; a frame with no fault, MsgType(35) third and `35=5` sets
     `resolved` and the latch. No reset runs there (OQ-1). Every other frame is drained as today. The scan runs
     only while a wait is armed, so the `Disconnected` row costs nothing otherwise.
5. **A second ending condition during the wait is absorbed.** `send_ending_logout_` with the wait `armed`
   writes nothing and records nothing (FR-004: no second wait, no second Logout). The liveness loop stops
   originating anything while the wait is armed: its loop condition becomes
   `fsm_state_ == Active && ending_wait_.phase != armed`, so no Heartbeat, TestRequest or give-up is sent during
   the wait (the bound, default 2000 ms, is shorter than any default liveness interval). It is keyed on the wait,
   not on `logout_in_flight_`, because `run_logout_phase1` sets that flag before it records `LogoutSent`, and the
   loop's behaviour in a graceful close's phase 1 is not this feature's to change.
6. **Silent endings during the wait** (a TestReqID mismatch through `answer_pending_test_request_`, the BeginString
   and CompID guard, MsgSeqNum 0) record `Disconnected` with the wait `armed`, so no latch is set and the first
   bound holds, read literally from FR-004. The peer's later Logout resolves the wait in the `Disconnected` row
   (item 4). An arm that returns an *error* (a store failure, an overflow) is closed by the pump at once, as
   today: a failed store is not waited on.
7. **Timeout 0 and no clock.** `logout_disconnect_timeout_ms == 0`: the deadline is the write's instant, so the
   bound expires at its first test and the connection closes after the write with no wait. No clock
   (`effective_clock_` null, a direct Session only): no bound can run, so the wait is not armed and the ending
   records `Disconnected` after the write, closing at once. Both are cells.
8. **`close()` and `Engine::stop()` during the wait.** A graceful `close()` finds `logout_in_flight_` and
   poll-joins it against its own grace timer (103 D4.6); the peer's Logout or the bound ends the wait and
   `close()` proceeds. A terminal `close()` or `Engine::stop()` records `Disconnected` through `close()`, whose
   edge wakes the bound; `close()` joins the bound's counter. One ending, one close: the latch is not set inside
   `close()`.
9. **`onLogout`** fires at the `Disconnected` edge, as today. After Active that is at the wait's end (the peer's
   Logout, the bound, or `close()`), which is QuickFIX C++'s timing (`Session::disconnect` calls `onLogout`).
   Before Active it does not fire, as today: it fires only on an Active→!Active edge.

**Rationale.** Staying Active with the existing Logout owner reuses every rule 103 built for "a Logout of the
session is in flight" (send refusal, single Logout, close's poll-join, the too-high path) instead of adding a
state. The `Disconnected` row already drains everything before Active, which is all a refused peer needs.

**Alternatives rejected.**
- *`LogoutSent` as the wait's state.* Q2 rules it out: `on_inbound_logout_sent_` drains every frame but the
  Logout, and it is `close()`'s drain.
- *A new `fsm_state` value.* `fsm_state` is public (`fsm_visit_history()`); a new value is a C++ API change and
  touches every `switch` on it, for no observable gain over the owner flag.
- *Reusing `run_peer_logout_bound_` itself.* Its body waits for a resend recovery and then acknowledges; only its
  shape (join, wake, deadline race, generation) fits.
- *Reusing the liveness loop as the timer.* It does not run with HeartBtInt 0, and its sleeps are keyed to
  liveness, not to a Logout.
- *A sliding bound from the last send* (QuickFIX C++/J). FR-004 fixes the Logout's write as the origin.

## D3 — The unanswered TestRequest (FR-003, R-1)

**Decision.**
- At the give-up (D6), `run_liveness_loop` takes a replay-gate shared hold as its other emits do (103 D5′.4),
  claims a pending too-high mark as the other ending Logouts do (`claim_peer_logout_none_`, 103 D4.6), and writes
  a Logout through `inbound_logout_leaf_` (reached from a detached root as `run_peer_logout_bound_` reaches it
  through `acknowledge_peer_logout_`) with Text(58) `TestRequest unanswered, TestReqID <id>`,
  `<id>` being `pending_test_req_id_`. The phrase follows FIX-SL 2020 §4.5.5 ("a Logout(35=5) message with the
  Text(58) field stating that the session was terminated due to lack of a response to a TestRequest(35=1)
  message, followed by terminating the transport layer connection") and the shape of 537's "ResendRequest
  unanswered, expecting MsgSeqNum N". The text is built in the loop's frame in a bounded buffer with a
  `static_assert` on its size, as `resend_gap_deadline_`'s is.
- Then `record_disconnected_unless_already_()`. The wait is not armed (R-1 is FR-004a's exception), so the edge
  sets the latch, and the loop's exit calls `release_connection_end_()` (D1). The connection closes as soon as
  the write has returned.
- **When the Logout cannot be written** (the leaf's buffer cannot hold the CompIDs, `toAdmin` throws, the number
  assignment fails, or `store_then_emit` fails), the leaf's own dispositions record `Disconnected` or leave it to
  the loop, and the close follows the same way. Each is a cell.
- **Observable surface for slot 74.** One `FIXPP_SLOG` warn record, written at the give-up whether or not the
  Logout was written:
  `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} logout_sent={} error={}`, `logout_sent`
  being the transmitted bit of D2 item 1, `error` being
  `static_cast<uint64_t>(error::session_test_request_unanswered)`, registered in `src/log/format_registry.cpp`
  beside 093's records. No rate bound: once per connection, as `note_establishment_timeout_`. The C ABI sees the
  wire (the Logout, then EOF), the log through its logger, and `fixpp_session_is_established` turning false. No
  call returns slot 74; that stays as today and is stated in B&L.
- **`unanswered_tr_` is deleted** (it has no reader; its writes in `answer_pending_test_request_`, the in-sequence
  Heartbeat arm of `on_inbound_active_` and the loop go with it), with its declaration comment in `session.hpp`.
  The "(Phase 6 US4 wires the Logout emission before Disconnected.)" comment is deleted (FR-018).
- The `error.hpp` comment on slot 74 is rewritten to the new rule, without the arithmetic: "inbound silence of
  2 × the TestRequest threshold with no answering Heartbeat; the session sends a Logout with Text and closes".

**Alternatives rejected.**
- *A new `SessionEvent` alternative.* FR-003 asks for an existing ending surface; the variant has no ending event,
  and adding one is a public C++ type change for an observable the log already carries.
- *Keeping `unanswered_tr_` and adding a reader.* Nothing needs the flag once the log record is written at the
  give-up.
- *No Logout (QuickFIX C++, /J, /Go).* Ruled out by R-1.

## D4 — Silent pre-Active refusals and the acceptor's first frame (FR-006, FR-007)

**Decision.**
- A refusal before Active that sends nothing records `Disconnected` with no wait armed, so the edge sets the latch
  and the pump closes at once after the frame (D1). This covers 092's D-1, D-2 and D-3 in `dispose_unparseable_`,
  the NotConnected and LogonSent refusals of `on_inbound_not_connected_` and `on_inbound_logon_sent_`, the
  BeginString and CompID guard, and MsgSeqNum 0. D-1/D-2/D-3 still refuse exactly what they refuse today (FR-007).
- **The label.** The pump's `expire()` writes `note_establishment_timeout` only when the session has not recorded
  its own ending (`!session_engine_access::ended(session)`); otherwise it closes with no event and no record.
  `establishing()` is unchanged, so the deadline still bounds a pre-Active R-2 wait (D2 item 3) and nothing is
  held longer than today. `session_event_establishment_timeout` is then written only for a peer that did not log
  on in time (B-093-4 keeps its event). A silent refusal never reaches the deadline: the latch closes it first.
- **The first frame** is delivered by the pump (D1 item 3). An error result closes, a recorded `Disconnected`
  closes, and a refusal with a Logout waits by R-2, exactly as for a later frame.
- **Cells that flip** (FR-019 list; the census recipe in D11 finds the rest):
  `EstablishmentTimeoutQ16.AcceptorAfterRefusedLogon_*` (`engine_establishment_timeout_test.cpp`),
  `ResendAnswerStoreFailureEngine.Acceptor789Path_SlotReadFails_ConnectionStaysOpenUntilTheEstablishmentDeadline`
  (`test_538_resend_answer_store_failure_engine.cpp`), `InboundFrameDispositionsQ10.WithinCapMismatchInActive_DisconnectsWithoutALogout`
  (`inbound_frame_dispositions_test.cpp`, its open-transport half), and the #534-commented cells of
  `inbound_frame_dispositions_test.cpp` and `test_cancelled_outbound_store_engine.cpp`. 092's contract C-2 O-1 and
  093's FR-006 phase (b) and contract C-4/C-7 are superseded for the close timing only, by name (D11).

**Alternatives rejected.** *Keeping the deadline as the close and relabelling its event* (a new "refused" event):
it holds a refused connection for up to `logon_timeout_ms`, which Q1 rules out.

## D5 — An out-of-sequence Heartbeat with sequence validation off (FR-009, #582)

**Decision.** In `on_inbound_active_out_of_sequence_`'s Heartbeat branch, the call to
`answer_pending_test_request_(hdr.test_req_id)` runs when a TestRequest is pending and either (the frame is too
high and a resend is outstanding) or `!cfg_.validate_sequence_numbers`. The rest of the branch is unchanged: no
advance, no persist, no `fromAdmin`, no echo. A matching or absent 112 answers; another 112 ends the session
silently, as in sequence, and the pump closes it (D1). With the knob on, the predicate is today's (B-537-3,
103's D6). The routine's existing comment ("Whether a TestRequest is pending is each caller's test") stands.

**Rationale.** It is the smallest change that makes the knob-off Heartbeat reach the TestReqID rule the knob-on
paths already share. QuickFIX/J is the only engine with the knob, and it clears the pending TestRequest before
its sequence checks (`state.clearTestRequestCounter()` on `Session.next(Message)`'s path); fixpp keeps the 112 comparison (FIX-SL §4.5.5, TC scenario 6),
the split the spec's Departures table records.

**Alternatives rejected.** *Deliver the knob-off Heartbeat to the knob-off delivery arm* (`fromAdmin`): it changes
what the application sees for every out-of-sequence Heartbeat, which #582 does not ask.

## D6 — TestRequest timing and `test_request_threshold` (FR-010 to FR-012, R-3)

**Decision.**
- **Where T is computed.** In `run_liveness_loop`, once before its loop:
  `T = cfg_.test_request_threshold.value_or(default_test_request_threshold(heartbt_int))`, where the default is
  1.2 × HeartBtInt in milliseconds. HeartBtInt is whole seconds, so 1.2 × H is exactly `H × 1200` ms with no
  rounding; the product saturates at `milliseconds::max()` (a `detail` helper beside `deadline_after`, with
  `constexpr` cells).
- **The two instants.** TestRequest at `deadline_after(last_inbound_steady_, T)`; give-up at
  `deadline_after(deadline_after(last_inbound_steady_, T), T)`, two saturating steps with no product formed, the
  shape of `detail::resend_gap_deadline`. The outbound Heartbeat cadence (1 × HeartBtInt) and the gap deadline
  (2 × HeartBtInt, B-537-2) are unchanged. The main sleep is the earlier of the Heartbeat instant and the
  TestRequest instant; the "inbound arrived" test reads the TestRequest instant.
- **The `+1ns` guard.** It exists so that a mock clock firing at exact equality does not run the give-up before a
  test can deliver its echo (`TR_DistinctNow`'s comment). It is kept on the give-up instant and re-derived against
  the new arithmetic: a cell advances to exactly 2T and asserts the echo still wins, and a cell at 2T + 1 ns
  asserts the give-up.
- **Validation (FR-011).** A set threshold of zero or below is refused: `Session::open()` and
  `Engine::register_session` return `invalid_session_config`, as for `logon_timeout_ms == 0` (093); the TOML
  mapper records an `out_of_range` diagnostic at `session[i].test_request_threshold`. Whether
  `parse_duration_to_ms` can produce a negative value is read in the implementation and pinned by a cell either
  way. There is no upper bound beyond the parser's own overflow guard: `"1h"` (`pos_scalars_extra.toml`, asserted
  by `test_load_happy_path.cpp`) stays accepted. A T above the gap deadline is accepted, and 2T overflowing the
  clock saturates (the give-up never fires before the clock's end).
- **Precedence** (FR-010). With the default T and a gap open, the gap deadline (2H) precedes the give-up (2.4H), so
  a silent peer with a gap open always ends with the deadline's Logout and R-2; with a configured T below H the
  give-up can come first. Both orders are cells.
- **Surfaces.** No C-ABI setter and no Python surface (FR-011): `grep -rn test_request_threshold bindings include/fix`
  finds none. The QuickFIX parity table row in `tests/config/test_quickfix_parity_table.cpp` and its TOML
  documentation state the rule; `SessionConfig::test_request_threshold`'s comment in `session_config.hpp` gains it.
- **Cells that move:** T018 `CellB_*` and `CellC_*_UnansweredTestRequestDisconnects`
  (`heartbeat_testrequest_test.cpp`, `test_heartbeat_cadence_8cell.cpp`), `ci/red-arms/batch18-lost-advance.sh`
  which names one of them, and any cell that advances a mock clock across HeartBtInt expecting a TestRequest
  (recipe in D10).

**Alternatives rejected.** *Two knobs (QuickFIX/J's multipliers):* R-3 rules one. *Rounding T to whole seconds:* no
rounding is needed for the default, and a configured T is already milliseconds. *Refusing T > 2H:* it breaks a
shipped fixture and nothing requires it.

## D7 — Dead state (FR-013, #559 sites 2 and 3)

**Decision.**
- Remove from `ReconnectFsm` (`include/fixpp/session/reconnect_fsm.hpp`, `src/session/reconnect_fsm.cpp`):
  `attempt_index_`, `heartbeat_interval_`, `logout_disconnect_timeout_`, `heartbeat_timer_`, `test_request_timer_`,
  `logout_timer_`, `last_outbound_testreqid_`, `drive_logout` and the header comment that claims it uses the
  stored timeout. The constructor loses its two now-unused parameters; `Session::Session` and every test that
  constructs a `ReconnectFsm` are updated (recipe: `grep -rlnE 'ReconnectFsm[ ]+[a-z_]+[{(]|ReconnectFsm[{(]' tests src`;
  known positive `tests/session/test_logout_timeout.cpp`).
- Remove `parse_cn_from_dn_local` from S (no caller; `parse_cn_from_dn` in `compid_authorization_policy.cpp` is
  the one that runs).
- **The `drive_logout` probe.** `DriveLogoutStub_*` and `Initiator_DriveLogout_*` in `test_logout_timeout.cpp` are
  retired with a note naming FR-013; `ci/red-arms/batch11-labels.txt`'s entry for them is removed with them. The
  fuzz harness comment in `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` ("triggers drive_logout path") is
  corrected.
- **Size pins.** None found at the base by
  `grep -rnE 'sizeof\((fixpp::session::)?(Session|ReconnectFsm)\b' src include tests ci tools bench bindings`.
  The recipe's instrument is proven on a known positive by widening the class list:
  `grep -rnE 'sizeof\([A-Za-z_:]+\) *(==|<=|<)' tests` reports `tests/dictionary/ref_shape_test.cpp`'s
  `FieldRef` pin. Re-run at the implementation head; the full `-k 0` build is the second instrument (FR-013).
- **The other stubs** of `ReconnectFsm` (`run_heartbeat_cadence`, `run_inbound_liveness_watch`,
  `validate_inbound_heartbeat_testreqid`, `process_inbound_sequence_reset`, `reply_to_inbound_resend_request`)
  are not in FR-013's list: SI-1.

## D8 — Pre-session rejects and the listener (FR-014, FR-015, #687)

**Decision.**
- **The rate gate is hoisted, not copied.** `session.hpp` is installed and declares `garble_log_gate_` and
  `reply_ceiling_log_gate_`, so the type must stay visible to it: the struct moves to an installed detail header,
  `include/fixpp/session/detail/log_rate_gate.hpp` (namespace `fixpp::session::detail`, the same fields, so the
  layout does not change), `Session` keeps the name by a member alias, and the admit logic becomes an inline
  `admit(gate, now, window, occurrences)` there. `Session::admit_log_record_` forwards to it with its own clock and window (`max(HeartBtInt, 1 s)`), so the
  garble and reply-ceiling records keep their behaviour. The accept loop holds one gate as a local of
  `run_accept_loop`, which runs for the entry's accept lifetime; its clock is `engine_cfg.clock` and its window
  is `max(entry.config.heartbeat_interval or the default, 1 s)`. No member is added to `SessionEntry`
  (`engine.hpp` is installed).
- **One record per reject**, through a new anonymous-namespace `log_pre_session_reject` in E beside
  `log_role_loop_open_failure` (same logger resolution, same trace context), warn level, `cat::session`:
  `acceptor connection refused before a session: reason={} begin_string={} sender_comp_id={} target_comp_id={} error={} suppressed_since_last={}`.
  `reason` is a `pre_session_reject` value (data-model); the CompIDs are those of the first frame when read
  (empty otherwise), copied by `ArgValue::from_inline`, whose 15-byte copy bounds what an unauthenticated peer can
  put in the log; `error` is the handshake's or the first-frame read's error code when there is one.
- **The listener.** `catch (...)` gives no error value. The catch becomes `catch (std::system_error const&)`
  recording its `code().value()`, then `catch (...)` recording 0, each writing one record
  `acceptor listener could not bind: begin_string={} sender_comp_id={} target_comp_id={} error={}` with no rate gate
  (once per entry). Both formats are registered in `src/log/format_registry.cpp`.
- **No counters** (deferred to the OTel plan, spec Assumptions).

**Alternatives rejected.** *A gate per reason:* a flood can rotate reasons; one gate per entry bounds the total.
*Logging CompID lengths only (101's choice for MsgType):* FR-014 asks for the CompIDs; the 15-byte bound and the
file sink's escaping limit what they can carry.

## D9 — C ABI and C++ surface

**Decision: C-ABI 1.18, BREAKING, with no symbol, type or error-code change.** `include/fix/c_api/session.h`'s
1.11, 1.12 and 1.17 clauses state results this feature changes: an establishment refusal's connection "stayed
open" to the deadline; endings "with no Logout"; a session "ends" when a TestRequest goes unanswered at today's
timing; a too-low frame's Logout followed by the session's end. Article X §7 counts a changed documented result
as breaking, and 103 bumped to 1.17 for the same kind of change. The observers (each gets a 1.18 clause, and the
older sentences are amended in place): `fixpp_session_is_established` (true until the R-2 wait ends; stays true
for a knob-off peer that answers; the TestRequest give-up at 2T), `fixpp_session_close`, `fixpp_session_send`
(refused during the wait), `fixpp_session_register_callback` and `fixpp_session_register_send_callback`. What no
declaration carries, the counterparty's side, goes in `version.h`'s 1.18 history: the connection closes at every
ending, the unanswered TestRequest's Logout, the TestRequest at 1.2 × HeartBtInt.
- In-repo consumers updated in the same PR (Article X §7): the Python binding's version expectations and every
  test pinned to the minor (`git grep -nE 'MINOR 17|1\.17' -- include tests bindings tools ci`).
- **C++.** `sizeof(Session)` changes (members added in D1, D2; `unanswered_tr_` and `ReconnectFsm`'s members
  removed). `ReconnectFsm`'s constructor signature and layout change (an installed header). `run_read_pump` is
  internal. No `SessionConfig` field changes. BREAKING C++ layout, recorded in B&L as 103's B-103-9 and 104's
  B-104-7 record theirs.

**Alternative rejected.** *No bump, on "the C ABI shape is unchanged":* X §7 is about results, not shapes.

## D10 — Test plan

- **One ctest entry `session_105_tests`** (label `105`) over new files in `tests/session/`:
  `test_105_connection_close.cpp` (D1, D4; per root and per refusal), `test_105_logout_wait.cpp` (D2),
  `test_105_liveness_timing.cpp` (D3, D6), `test_105_heartbeat_knob_off.cpp` (D5), `test_105_presession_log.cpp`
  (D8), `test_105_tls_close.cpp` (FR-002 on `loopback_tls_session_harness.hpp`), and `test_105_support.hpp`.
  Config cells extend `tests/config/test_load_negative_battery.cpp`; C-ABI cells extend
  `tests/capi/recovery_liveness_capi_witness_test.cpp`. `ReconnectFsm` removals are proven by the build.
- **RED first on the base** for every behaviour cell (FR-017): the cell is written, run on `52aeed4e` and shown RED
  for its stated reason before the fix.
- **The close observable is the peer's.** Engine cells use `engine_loopback_harness.hpp` / `plain_engine_rig.hpp`
  and assert the peer's read ends (EOF, or the TLS close_notify on the TLS arm), never a session flag.
- **Bounds from the competing timeout.** Every competing timeout (the establishment deadline, the liveness
  instants, `logout_disconnect_timeout_ms`) runs on the mock clock. A cell for an "at once" class (SC-001) holds
  the mock clock still and pumps the executor until the peer's read ends or a wall cap passes; with the clock
  frozen nothing else can close the connection, so a hit is the mechanism's. The spurious-hit arm: the same cell
  with the latch's consumer deleted (a mutant) must go RED, and a cell with a short `logon_timeout_ms` asserts the
  EOF arrives before the deadline and with no `session_event_establishment_timeout`.
- **R-2 cells:** the peer's Logout before the bound (EOF follows its Logout, with the reset under
  `reset_on_logout`); the bound with no Logout (EOF at exactly the bound, not before: advance to bound − 1 ms,
  assert open; advance 1 ms, assert EOF); the bound does not slide (a Heartbeat sent during the wait); an
  application message, a TestRequest, a ResendRequest, a too-low and a too-high frame during the wait; a second
  ending condition (no second Logout); a silent ending during the wait (the bound holds); `send()` refused;
  timeout 0; no clock; `close()` graceful and terminal and `Engine::stop()` during the wait (one close each); the
  pre-Active wait bounded by the establishment deadline, with no timeout event.
- **Timing cells that discriminate.** H = 10 s on the mock clock: no TestRequest at 11 s (the base sends at 10 s),
  one at 12 s; Active at 23.9 s (the base ends near 20 s), ended at 24 s plus the guard. A set T (7 s): TestRequest
  at 7 s, give-up at 14 s. Both roles. The `+1ns` cells of D6.
- **Both roles** for every engine cell, by `INSTANTIATE_TEST_SUITE_P` over `session_role`, as 537's cells do.
- **The cell recipe for moved timings:** `grep -rlnE 'TestRequest|35=1' tests/session tests/capi tests/conformance tests/interop`,
  read for a mock-clock advance across HeartBtInt; the whole `session` and `capi` suites are run, since a missed
  one fails there.
- **Lanes.** linux-clang-debug, -asan, -ubsan, -tsan (the latch, the spawned close and the bound are strand
  confined; TSan proves it), gcc-release of the touched targets, and the MSVC build of the feature's targets before
  the first labelled push (the project's rule for a feature's own targets). The TLS arm runs on every lane that runs
  `loopback_tls_session_harness.hpp` today.
- **Benchmarks (Article VIII §3).** Per message, only the pump's latch test is new. `on_inbound_frame_bench` calls
  `on_inbound_frame` directly and bypasses the pump; the plan runs paired base-vs-candidate any bench that drives
  `run_read_pump` (`grep -rln 'Engine\b\|run_read_pump' bench`; `bench/threading/bench_threading.cpp` and
  `bench/session/engine_send_bench.cpp` are the candidates to read) and states in the evidence file if none does,
  as 104 did for `engine_send_bench`.
- **Supersession pins that flip** (FR-019): `GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen`
  and its helper `expect_logout_and_open_transport` (its `state_after_the_bound_frame == Disconnected` also flips:
  the state is Active during the wait), `Capi537Witness.Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilently`,
  `InboundFrameDispositionsQ10.WithinCapMismatchInActive_DisconnectsWithoutALogout`, the #534-commented cells of
  `SecondFrameInFlight.*` and `ReconcileAfterTheTakeBack.*`, `EstablishmentTimeoutQ16.AcceptorAfterRefusedLogon_*`,
  `ResendAnswerStoreFailureEngine.Acceptor789Path_SlotReadFails_ConnectionStaysOpenUntilTheEstablishmentDeadline`,
  T018 `CellC_*_UnansweredTestRequestDisconnects`, the 537 note's control cell "D-537-6, N", and `DriveLogoutStub_*` /
  `Initiator_DriveLogout_*`. Each is renamed where its name states the old rule.

## D11 — Supersession (FR-016, FR-018, FR-019)

- **Census recipe** over `specs/`, `.specify/`, `brain/`, `spec/`, `tests/`, `include/`, `src/`, `ci/`:
  `git grep -nE '#534|fixpp#534|stays open|transport stays open|1 ?× ?HeartBtInt|1×HB|test_request_threshold|Phase 6 US4|not a close|drive_logout|unanswered_tr_|parse_cn_from_dn_local|L-103-5|R-6|establishment (deadline|timeout)'`.
  Known positives it must report: `tests/session/test_537_gap_deadline.cpp` ("fixpp#534"), `include/fixpp/core/error.hpp`
  ("1×HeartBtInt"), `src/session/session.cpp` ("Phase 6 US4"). Each hit is classed: superseded by name (a merged
  bundle, edited nowhere: `specs/005-*`, `specs/013-*`, `specs/092-*`, `specs/093-*`, `specs/103-*`, `specs/104-*`), a
  living surface rewritten, or not this rule.
- **Rulings superseded by name in this bundle** (the spec's FR-019 list): 536-537-538 note R-6's "A Logout on the
  dead-peer ending. Not taken" by R-1; D-537-5's C-3 disclosure ("the transport stays open") by FR-004; 103 Arm B's
  disclosure and 104's DQ-8 (L-103-5) by FR-004; 093 FR-006 phase (b) and contract C-4/C-7 by FR-007; 092 contract
  C-2 O-1's close timing by FR-007; 005 research D-8 (threshold 1 × HeartBtInt) by R-3. The living
  `.specify/536-537-538-recovery-liveness.md` gets an erratum at its end naming 105 (that note is a design doc,
  not a merged bundle).
- **Header comments naming the new rule** (issue #310): `record_state_transition_` ("105 D1: the edge sets the
  connection-end latch; its consumers"), `run_read_pump`'s disposition list, `run_liveness_loop`'s algorithm block
  (its "1 × HeartBtInt" steps) and its declaration comment in `session.hpp` ("after heartbt_int of no inbound data",
  "grace window (1×heartbt_int)"), `pending_test_req_id_`'s comment, `resend_gap_deadline_` and Arm B (R-2), `dispose_unparseable_` (092's "D-1 and D-2
  are a state transition, not a close" and its D-3 twin, FR-018), `error.hpp` slot 74.
- **B&L plan** (live file only):
  - *Retire* L-103-5 to the closed file; its QuickFIX statement is not carried forward as written.
  - *Amend in place, dated, naming 105:* B-537-2 (the "transport stays open" clause; its silent grace-check ending,
    whose reach the default T changes), B-537-3 (#582 fixed), L-093-1 and L-092-1 ("closes nothing (fixpp#534)"),
    L-093-6 (the rejects are now logged; garbles of a connection that yields no Session are still not counted),
    B-093-4 (the deadline no longer closes a refused connection), B-092-2 and B-092-5 (the grace window is 2T),
    B-103-10 ("then `Disconnected`" becomes "then the R-2 wait, and `Disconnected` at its end") and B-104-6 (still
    `Disconnected` at once, and the connection closes at the peer's Logout or the bound).
  - *New rows:* `B-105-1` the close rule by ending class (FR-001 to FR-008); `B-105-2` R-2's wait and its
    observables; `B-105-3` R-1 and its departure from QuickFIX; `B-105-4` the #582 rule and its 112 split;
    `B-105-5` the TestRequest timing and the threshold's validation; `B-105-6` the pre-session and listener records;
    `B-105-7` BREAKING C-ABI 1.18 and the C++ layout changes; an `L-105-*` row for each disclosure of the
    contract's "Disclosed" section.
- **Brain pages:** `brain/components/session.md` (R-6, and its deferral of the grace-check reading to B27),
  `brain/components/session-liveness-and-reconnect.md` (the `ReconnectFsm` correction block, and the liveness
  table's two thresholds as a pointer to source), `brain/components/engine-accept-path.md` (the first frame through
  the pump, the reject records), `brain/components/graceful-logout.md` if its census hit is a rule this changes.
  Each is a lead to verify against source and cites the source.

## D12 — The owed docs task (the `<windows.h>` anti-pattern, brain half)

Included in this feature as a docs task (plan.md). Source: the parent's `.claude/agents/phase-implementer.md`,
section "Cross-platform — <windows.h> macros rewrite common identifiers on MSVC". The brain half is:
- a `brain/failure-classes.md` entry in that page's form, trigger and procedure only, no instance and no PR: the
  trigger is a new identifier in code an MSVC lane compiles whose spelling a platform header defines as a macro;
  the procedure is to build the touched targets on MSVC before the first labelled push, and to treat a Linux-only
  green as no evidence for that class;
- one `brain/log.md` line dated at the PR, naming the class.
It is docs only; it touches no code and no test.

## Risks

- **R1. Two closes on one TLS transport** (`close_connection_` and `Engine::stop()`'s step 2). `close_async`
  subsumes the second caller (F-5), and stop's step 4 `close()` joins the counter. A TSan cell runs the race.
- **R2. A latch left set with no consumer** (a spawn that threw; a direct Session whose application never calls
  anything again). The connection then ends at `close()` or `Engine::stop()` as today; a Debug assert in
  `close()` cannot help, so the census table's "every root" column is the control.
- **R3. Timing cells on real clocks.** Every competing timeout is on the mock clock (D10); the wall cap exists only to
  stop a broken cell.
- **R4. Interop peers.** QuickFIX peers in `tests/interop` now see a Logout before EOF on the unanswered TestRequest
  and an R-2 wait; their cells are run.

## Spec issues for the orchestrator

- **SI-1 (FR-013 vs R-4 (b)).** R-4 folds "the whole `ReconnectFsm` dead set" into #559, but FR-013 lists only
  `drive_logout` among the stubs. `run_heartbeat_cadence`, `run_inbound_liveness_watch`,
  `validate_inbound_heartbeat_testreqid`, `process_inbound_sequence_reset` and `reply_to_inbound_resend_request` are
  no-op bodies with no caller in `src/` (`grep -rnE '\.(run_heartbeat_cadence|run_inbound_liveness_watch|validate_inbound_heartbeat_testreqid|process_inbound_sequence_reset|reply_to_inbound_resend_request)\(' src tests`
  finds none; the same recipe finds `drive_logout`'s test call, its known positive). `run_inbound_liveness_watch`'s
  header comment states a TestRequest timing ("1.2×HeartBtInt ... 2×HeartBtInt total, Logout(5)") that FR-012
  requires restated. **Recommendation:** extend FR-013 to remove these stubs and their comments (test comments that
  name them as RED witnesses are corrected); if the owner keeps them, FR-012 still requires the comment fixed.
- **SI-2 (FR-010 "HeartBtInt ≤ 0 disables both, as today").** Today `run_liveness_loop` returns only at exactly 0
  (`heartbt_int.count() == 0`), while `detail::resend_gap_deadline` is off at ≤ 0. A negative interval is
  reachable only through C++ (`SessionConfig::heartbeat_interval`; the C setter takes `uint32_t`, the TOML
  duration parser yields no negative). **Recommendation:** make the loop's test `<= 0`, matching FR-010's wording
  and the deadline, with a cell; or amend FR-010 to "= 0".
- **SI-3 (FR-004, the peer's Logout after a pre-Active refusal).** FR-004 says the peer's Logout during the wait
  "resets at its reset point, as a response in LogoutSent does", but 104's RU-2 list has no reset point for a
  session that never reached Active other than arm S (a Logout refusing fixpp's Logon in LogonSent). Taken to the
  owner as OQ-1.

## Open questions for the owner

- **OQ-1. With `reset_on_logout`, does the peer's Logout that answers a Logout fixpp sent to refuse a Logon (before
  Active) reset the numbers?**
  - FIX-SL 2020 §4.6 describes the Logout exchange for an established session; it says nothing on a refused Logon.
  - QuickFIX C++, /J and /Go close at once after a too-low Logon's Logout (`Session::doTargetTooLow` in C++;
    `Session.doTargetTooLow` in QuickFIX/J; the `targetTooLow` case of QuickFIX/Go's `logonState`), so none of them
    ever receives this Logout.
  - fixpp's 104 has no reset point for it (RU-2), and the session's numbers were never agreed with this peer.
  - **Recommendation: no reset.** The peer's Logout ends the wait and the connection closes; `reset_on_logout`
    applies to sessions that reached Active and to arm S, as 104 ruled. One cell pins it.
