# Research: 105 transport and liveness (B27)

**Feature**: [spec.md](spec.md) · **Plan**: [plan.md](plan.md) · **Base**: `52aeed4e` (fixpp `main`).

Phase 0 of `/speckit-plan`. Every behaviour claim below was re-read in source at the base and is cited by
symbol. The re-derive in the parent repo (`research/findings/b27-rederive-2026-11-20.md`) and the SecondBrain
pages were used as leads only. Owner rulings R-1 to R-4, clarifications Q1 and Q2, the rulings on SI-1 to SI-3, OQ-1 and L-105-b, and the
Gate A round 1 decisions D-a to D-c (spec Clarifications) are final and are not re-opened here.

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
  processing of a frame; `inbound_depth_` is non-zero exactly while a frame is inside an arm. Every frame enters
  `on_inbound_active_` in every state (`on_inbound_frame` forwards to it, and it calls `on_inbound_cold_` for the
  states other than LogonReceived and Active), so the count covers the `Disconnected` row too.
- **F-8. A Logout owner exists.** `inbound_logout_leaf_` sets `logout_in_flight_` before its first suspension and
  writes nothing when it is already set; the only clear is the edge. `send_body_` refuses a send while it is set
  (103 FR-021). `close()`'s graceful phase 1 poll-joins it (103 D4.6). The too-high Logout path acknowledges at
  once when it is set (`on_inbound_active_too_high_logout_`).
- **F-9. The acceptor's first frame bypasses the pump.** `run_accept_loop` step 8 awaits
  `session->on_inbound_frame(first_frame)` and discards the result (`(void)deliver_r;`).
- **F-10. Pre-session rejects are silent.** Each reject branch of `run_accept_loop` before `open()` is
  `(void)transport->close(); continue;`, and a failed `asio_listener` construction is `catch (...) { co_return; }`.
  `log_role_loop_open_failure` already logs through the entry's logger with no Session.
- **F-11. The dispatch order in Active** (`on_inbound_active_`, read at the base): garble test, header-fault
  disposition, the per-frame liveness refresh, the validation gate, the BeginString and CompID guards, the SendingTime
  guard (Logout exempt), the Reset-mode SequenceReset bypass, MsgSeqNum 0, the too-high branch (no resend
  outstanding; it leaves for `on_inbound_active_too_high_`, which sends a ResendRequest), the PossDup stage-1 arm,
  `check_inbound`, and only then the `35=5` branch to `on_inbound_active_logout_`. A Logout off NextNumIn leaves at
  `check_inbound` for `on_inbound_active_out_of_sequence_`, whose first act is `resend_gap_deadline_`, then the
  too-high-Logout branch (only while a resend is outstanding), then Arm B. So the peer's reply to a too-low or
  gap-deadline Logout never reaches the in-sequence receipt arm.
- **F-12. The gap deadline is frame-driven.** `resend_gap_deadline_` is called only from the inbound arms (its own
  comment: "Called where a frame can leave the Active arm"); no timer fires it. A silent peer never meets it.
- **F-13. The give-up is read once.** `run_liveness_loop` computes the grace instant from `inbound_deadline`,
  captured at the top of the iteration before the TestRequest, and after the grace sleep tests only
  `!pending_test_req_id_.empty()`. `pending_test_req_id_` is cleared only by a Heartbeat with a matching or absent
  112 (the in-sequence Heartbeat arm, `answer_pending_test_request_`). Its state tests after each suspension read
  only `fsm_state_ != fsm_state::Active`.
- **F-14. A write has no timeout.** `live_write_serialized_` holds `write_gate_` across `async_write`'s completion,
  and neither transport's `async_write` is bounded. `send_body_` tests `logout_in_flight_` at entry and after a
  replay-gate wake only; `store_then_emit` does not re-test it before the transmit.

## D1 — How a `Disconnected` edge gets the transport closed (FR-001, FR-002, FR-008)

**Decision: an edge latch, consumed by the root that owns the ending.**

1. **The latch.** A private member of `Session`, `connection_end_owed_`, holding the `conn_gen_` the close is owed
   on. One setter, `owe_connection_end_()`, sets it when all of these hold: `state_ == lifecycle::open` (not inside
   `close()`), `!reconnect_drain_in_flight_` (a new flag held only around `drain_for_reconnect_`'s call in
   `drive_reconnect`, B-103-9), and no R-2 wait is `armed` (D2). It has three callers (data-model E-2): the edge, on
   a transition **into** `Disconnected` from another state only, so a later write of `Disconnected` over
   `Disconnected` cannot owe a second close; the R-2 bound's expiry for a wait armed before Active; and the response
   to such a wait. The latter two exist because before Active the session is `Disconnected` already, so no edge
   runs. `reconnect_in_flight_` cannot serve: its holder in `drive_reconnect` stays alive across
   `co_await emit_initiator_logon_()`, so it would hide every Logon-emission ending of a direct initiator.
   `install_reconnected_transport` records only `LogonSent` (and nothing on its refusal path), so the narrower
   window exposes no further write; the census re-reads it at the implementation head. The edge's call is made before
   `onLogout` is fired. Setting it is two stores: no suspension, no allocation, no spawn (F-1).
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
   set, its generation is `conn_gen_`, `state_ == lifecycle::open` and `inbound_depth_ == 0`, clear it and spawn
   the close frame (item 5) on the session's executor, detached, holding a new join counter
   `connection_close_counter_` (incremented before the spawn, as the liveness counter is, FQ-C). Every test is made
   here, synchronously, so the frame decides nothing. The spawn is on `exec_.underlying()`, never the wrapper executor (the TSan class
   `brain` records for wrapper spawns). When `inbound_depth_ != 0` the latch is left set: a frame is inside an arm, and the
   pump consumes the latch when that arm returns (F-7), so no store work an arm owns is cut. A spawn that throws
   leaves the latch set; the next consumer, `close()` or `Engine::stop()` closes the connection.
5. **The close frame, `close_transport_(transport, counter)`**, a static coroutine that receives the transport's
   `shared_ptr` and the counter's copy at the spawn, awaits `close_async()` on that transport, and touches no member
   of `Session` (data-model E-2). So it may outlive the Session, as the bound's join parameter may (B-103-13), and
   it widens no lifetime precondition. It runs on its own spawn chain, so `close_async`'s cancellation clobber
   stays there (F-6).
   - **Engine session:** the pump's parked read completes with an error (`close_async` cancels it, and a
     re-read is refused once the transport is closed), the pump runs `stop_pump`, and `close(terminal)` joins.
   - **Direct Session:** the connection ends and the Session stays open in `Disconnected`, so
     `drive_reconnect` can drain and install a new transport (F-4; spec Assumptions, "a reconnect is not
     blocked").
6. **Joins.** `close()` clears the latch when it commits `closing`, and before its own `close_async()` joins
   `connection_close_counter_` by post, as it joins the liveness loop. So the two never run `close_async` on one
   transport at once (FR-002). `drain_for_reconnect_` clears the latch and joins the counter before step 3's
   quiescence test, so no close frame outlives its connection. `~Session` asserts the counter is zero,
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

**Why it holds for the whole class.** Every `Disconnected` write goes through the edge, and the edge's call is
`owe_connection_end_()`'s, whose predicate is the only one; its other two callers are the R-2 wait's own endings
before Active (data-model E-3 rows 7 and 10). Every root that can reach a caller is a consumer or an exclusion. A new
write site needs no change; a new root does, and the census recipe finds it. The setter census is
`grep -n 'owe_connection_end_(' src/session/session.cpp` (known positive: the edge), each call site with a mutant
that deletes it.

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

## D2 — The R-2 wait as a dispatch overlay (FR-004, FR-004a, Q2, D-a, D-b)

Gate A round 1 rewrote this decision: the round-0 design left the frames of the wait to the existing arms, so the
checks that had started the wait ran again ahead of the peer's Logout (F-11), and the wait's setters and wake
sources were spread over several sites. The observable rules are one table, contract C-2 "While the wait is armed"
(W-1 to W-14); the state machine of the wait, the bound and the latch is one table, data-model E-3. This section is
the mechanism.

**Decision.**
1. **One helper starts every R-2 ending and owns its ending write.** `Session::send_ending_logout_(text)` writes
   the Logout through the existing leaf for the state (`inbound_logout_leaf_` after Active;
   `refuse_logon_with_logout_`'s build and emit and `inbound_logon_sent_logout_leaf_` before it), then either arms
   the wait or records the ending as the site records it today. The `Disconnected` writes that today sit in each
   caller's tail move into the helper: Arm B's unconditional write, `resend_gap_deadline_`'s
   `record_disconnected_unless_already_()`, `refuse_logon_with_logout_`'s `disconnect_unless_superseded_`, and the
   SendingTime and OrigSendingTime arms' writes. Without that move an absorbing helper would change nothing.
   - **Armed only if the Logout was transmitted**, a clock is present and `logout_disconnect_timeout_ms > 0`. The
     leaves return success also when nothing went out: the Logout did not build (`if (lo_result)` false, the long
     CompIDs case), its emit failed (`(void)emit_r`), or `store_then_emit` returned success without transmitting
     (a superseded Logon arm; a frame held back while `wire_closed_by_reset_` is set). So `store_then_emit` gains
     a private out-parameter, set only when a transport accepted the frame: `live_write_serialized_` returned
     `written` **with a live transport present** (it also returns `written` when there is none), or the direct
     sink returned success. The leaves pass it up. Timeout 0 and no clock arm nothing (contract C-2, "No wait is
     armed"; the too-high path's `logout_disconnect_timeout_ms == 0` branch is the precedent), and spawn no frame.
   - **The order before Active** (data-model E-3 row 2): transmit, arm, the refusal's own `Disconnected` write
     (`disconnect_unless_superseded_`, now in the helper), then the bound's spawn.
   - **The callers** are the fixpp-initiated ending Logouts: `resend_gap_deadline_`,
     `on_inbound_active_sending_time_reject_`, the OrigSendingTime branch of `on_inbound_active_poss_dup_`, Arm B
     of `on_inbound_active_out_of_sequence_`, and every caller of `refuse_logon_with_logout_`,
     `refuse_too_low_logon_` and `inbound_logon_sent_logout_leaf_`. The replies are not callers:
     `on_inbound_active_logout_` and `acknowledge_peer_logout_` stay under FR-005. Census:
     `grep -nE 'inbound_logout_leaf_\(|refuse_logon_with_logout_\(|refuse_too_low_logon_\(|inbound_logon_sent_logout_leaf_\(' src/session/session.cpp`,
     each hit classed "initiates" or "replies"; known positive: `resend_gap_deadline_` initiates.
2. **After Active the FSM stays `Active`** during the wait, under the existing Logout owner `logout_in_flight_`,
   set by the leaf: `send_body_` refuses an application send (F-8, 103 FR-021), the leaf writes no second Logout,
   and a graceful `close()` poll-joins it. **Before Active the FSM is `Disconnected`**, as each refusal records it
   today, with the wait armed, so the edge owes no close; every frame but the peer's Logout is drained by the
   `Disconnected` row (clarification D-b).
3. **The response test** (W-1, clarification D-a). While a wait armed after Active is open, `on_inbound_active_`
   tests the frame for the response after the identity checks and ahead of every sequence-number test: after the
   garble test, the header-fault disposition, the liveness refresh, the validation gate and the BeginString and
   CompID guards; before the Reset-mode SequenceReset bypass, MsgSeqNum 0, the too-high branch, the PossDup arm and
   `check_inbound` (F-11). A frame with `35=5` there is the response, whatever its MsgSeqNum. Being ahead of the
   PossDup stage-1 arm, an in-sequence response carrying 43=Y is not Rejected by that arm's Arm C or Arm D; it goes
   straight to `check_inbound` and the in-sequence route below.
   - At NextNumIn it takes today's in-sequence route (`check_inbound` advances it, `on_inbound_active_logout_`
     delivers `fromAdmin` and persists), with `resolved` set at that arm's entry. Its leaf writes nothing (the
     owner is set), so it is a response.
   - Off NextNumIn it goes to a response arm in the shape of `acknowledge_peer_logout_`: `resolved`, a
     `pending_receipt_owner` (as the too-high path's immediate acknowledgement holds one), the leaf (which writes
     nothing), the event, `Disconnected`, and under `reset_on_logout` the reset point. It does not consume the
     number, does not call `persist_inbound_advance_` (which would persist an advance that never happened), and
     does not deliver `fromAdmin`. QuickFIX/J advances NextNumIn for a Logout only when its MsgSeqNum equals the
     expected one (`Session.nextLogout`, QFJ-750) before `resetState()`; QuickFIX C++ advances it by one in any case
     (contract C-10).
   - Before Active the `Disconnected` row (a new arm that `on_inbound_cold_` reaches only while a wait is armed)
     scans the frame's header; a frame with no fault, MsgType(35) third, the right BeginString and CompIDs and `35=5`
     sets `resolved` and calls `owe_connection_end_()`. No reset runs (owner ruling on OQ-1). The scan runs only
     while a wait is armed, so the row costs nothing otherwise.
4. **Absorption** (W-8). While a wait armed after Active is open:
   - `send_ending_logout_` writes nothing and records nothing. Whatever the arm wrote before calling it (the
     SendingTime arm's Reject) has been written, as in Active.
   - The silent ending sites that return success go through one helper, `end_session_silently_()`, which records
     `Disconnected` unless such a wait is armed: the BeginString and CompID guards, MsgSeqNum 0, the two TestReqID
     mismatch sites (the in-sequence Heartbeat arm and `answer_pending_test_request_`), and D-3 of
     `dispose_unparseable_`. Arm B's tail for a refusal that is not too-low (a drained sequence-number mutex) is
     classed by the census like every other hit, with its own cell. Census: D1's write-site grep restricted to the frame arms, each hit
     classed "returns success" (through the helper) or "returns an error" (W-9, not absorbed); known positive: the
     BeginString guard of `on_inbound_active_`.
   - `resend_gap_deadline_` does not fire (it returns `nullopt`), so its frame takes the row it would take before
     the deadline (W-7). The wait's own bound is the ending already running.
   - The armed test is in these helpers, never in `record_state_transition_`, whose `Disconnected` write also
     serves `close()`, the drain and the error arms (data-model E-3, its invariant).
5. **A frame above NextNumIn** with no resend outstanding (W-6) is processed as in Active (Q2; FR-004's "the sequence
   checks apply"): `on_inbound_active_too_high_` sends its ResendRequest, a reply to the peer's frame, as QuickFIX
   C++ does during its wait (`validLogonState` admits a non-Logout frame once `sentLogout` is set, and `verify`
   reaches `doTargetTooHigh`). Contract C-10's departure covers fixpp's own originations only (the liveness
   Heartbeat and TestRequest), not replies.
6. **The liveness loop** (W-11) tests one predicate, `liveness_may_originate_()`, as its loop condition and after
   every suspension: after the main sleep, after the Heartbeat's emit, and before and after the grace sleep
   (data-model E-4). Round 0 changed only the loop condition, so a sleep that began before the wait and ended inside
   it went on to a Heartbeat, a TestRequest or R-1 (F-13). The arming adds no wake: the predicate alone keeps the
   loop from originating, and `close()` and the drain still join it. `root_cancel_.emit` is not used as a wake: it
   reaches everything bound to the root slot.
7. **Admitted frames** (contract C-2.3, L-105-e). A send, Heartbeat or TestRequest numbered before the Logout and
   still in its store call can reach the wire after it (F-14). The design narrows the promise to numbering and
   discloses the wire order, instead of a barrier: the race exists on `main` for every Logout ending, on the
   SendingTime-Reject and gap-deadline routes the peer can recover the frame by ResendRequest during the wait (W-4;
   on the too-low route its ResendRequest is below NextNumIn and is absorbed), and a barrier on `replay_gate_.shared` would have to
   exclude the waiter's own hold (R-1 runs inside the liveness loop's emits) and be bounded.
8. **The bound.** `deadline = deadline_after(now, logout_disconnect_timeout_ms)` on `effective_clock_`, read once at
   the arming (the Logout's write), never moved by a later send (FR-004; QuickFIX/Go's rule, not C++'s and /J's
   sliding `lastSentTime`). Before Active on an Engine session the pump's establishment deadline still runs (a pump
   local, keyed on `!has_reached_active`), so the connection closes at the earlier of the two, and at that deadline
   the pump closes without the establishment-timeout event and record, because the session has recorded its own
   ending (D4). A detached `run_ending_logout_bound_(join, gen)` waits on it, in the shape of
   `run_peer_logout_bound_` (a join parameter that touches only `ending_logout_bound_counter_`, a published wake,
   `detail::await_deadline` raced with a timer the wake cancels, a generation assert, an exception handler that ends
   the wait closed). Its wake sources, its live predicate in full, and what each exit does are data-model E-3. The
   predicate is not `run_peer_logout_bound_`'s `live()`: that one requires `fsm_state_ == Active` (false before
   Active) and `!closing_or_stopped_()` (false under a graceful `close()`, which would leave nothing to clear the
   Logout owner but the peer's Logout, and hand the close to `close()`'s wall-clock grace and its direct
   `Transport::close()`, abortive on TLS).
9. **`close()` and `Engine::stop()` during the wait** (W-13, W-14; data-model E-3 rows 6, 9, 12, 13). A graceful
   `close()` after Active poll-joins the Logout owner (103 D4.6) while the bound runs on; the response or the bound
   records `Disconnected`, which clears the owner, and `close()` proceeds to its own `close_async()` (contract
   C-8.4). A terminal `close()`, an escalation, `Engine::stop()` and the drain end the wait at once; `close()` and
   the drain join the bound's counter.
10. **`onLogout`** fires at the `Disconnected` edge, as today. After Active that edge is the wait's end (the
    response, the bound, an error arm, or `close()`), which is QuickFIX C++'s timing (`Session::disconnect` calls
    `onLogout`). Before Active it does not fire, as today: it fires only on an Active→!Active edge.

**Rationale.** Staying Active with the existing Logout owner reuses every rule 103 built for "a Logout of the
session is in flight" (send refusal, single Logout, close's poll-join, the too-high path) instead of adding a
state. The response test ahead of the sequence checks is the one place where every engine read agrees (spec
clarification D-a). Absorption keeps the first ending the only one: no second state change, no early `onLogout`, no
loss of the response's reset. The `Disconnected` row already drains everything before Active, which is all a
refused peer needs (D-b).

**Alternatives rejected.**
- *`LogoutSent` as the wait's state after Active.* Q2 rules it out: `on_inbound_logout_sent_` drains every frame but
  the Logout, and it is `close()`'s drain.
- *A new `fsm_state` value.* `fsm_state` is public (`fsm_visit_history()`); a new value is a C++ API change and
  touches every `switch` on it, for no observable gain over the owner flag.
- *Site-by-site patches* (Arm B, `resend_gap_deadline_` and the liveness loop each taught about the wait). That was
  round 0's shape and the source of the round-1 P1: one site missed is a swallowed response.
- *A silent ending during the wait moves the session to `Disconnected`* (round 0). It fired `onLogout` early, sent
  later frames to the drain, and routed the peer's Logout to a row that resets nothing.
- *Processing frames "as in Active" before Active.* That delivers `fromApp` for a peer whose Logon was refused
  (clarification D-b).
- *A barrier for admitted sends.* Item 7.
- *Reusing `run_peer_logout_bound_` itself.* Its body waits for a resend recovery and then acknowledges; only its
  shape fits.
- *Reusing the liveness loop as the timer.* It does not run with HeartBtInt ≤ 0, and its sleeps are keyed to
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
- **When the Logout cannot be written**, the leaf's own dispositions record `Disconnected` or leave it to the loop's
  `record_disconnected_unless_already_()`, and the close follows the same way. Contract C-3.2's five classes, each
  a cell asserting the slot-74 record's `logout_sent=0` and the peer's EOF, each with its own mutant: the buffer
  cannot hold the CompIDs (`if (lo_result)` false); `toAdmin` throws (the leaf records `Disconnected` and returns
  `app_callback_threw`); the number assignment fails (`record_disconnected_unless_already_()` and the error); the
  store fails (on a persistent store `store_then_emit` returns the error, which the leaf discards; on a volatile
  store the failure is logged and the write proceeds, so both are cells); the write fails (`live_write_serialized_`
  returns `dispatch_aborted`, and the leaf discards it with `(void)emit_r`, so the leaf returns success and the
  loop must still record and close). The assignment-failure cell is R-1's, not the liveness emission's
  `LivenessAssignFails` (contract C-1 row 8).
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
  `establishing()` is unchanged, so the deadline still bounds a pre-Active R-2 wait (D2 item 8) and nothing is
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
  TestRequest instant; the "inbound arrived" test at the loop's top reads the TestRequest instant, not
  `heartbt_int` as today.
- **The give-up is measured from the last inbound frame** (clarification D-c; F-13). After the grace sleep the loop
  re-reads `last_inbound_steady_` and gives up only when a TestRequest is pending and the clock is at or past the
  give-up instant computed from that read. Otherwise it goes back to its top, which recomputes both instants, so
  outbound Heartbeats resume; `pending_test_req_id_` stays set, so the TestReqID(112) rule still applies to a later
  Heartbeat, and a further T of silence sends a new TestRequest whose TestReqID becomes the pending one (as the loop
  assigns it today). Re-sleeping in the grace instead would leave fixpp outbound-silent for as long as a peer kept
  sending without answering. This is QuickFIX C++'s observable (`Session::verify` ends every accepted frame with
  `lastReceivedTime` and `testRequest(0)`, and `timedOut` is the time since the last receipt) and QuickFIX/Go's
  (the peer timer reset on every frame); fixpp keeps the 112 comparison (spec Departures). Cell: application traffic
  after the TestRequest and no Heartbeat; the session is Active at 2T after the TestRequest's instant and gives up at
  2T after the last frame.
- **The `+1ns` guard** is a third saturating step:
  `deadline_after(give_up_instant, std::chrono::nanoseconds{1})`. `deadline_after`'s template accepts any signed
  whole multiple of the time point's tick, and `fixpp::core::steady_time_point` is `std::chrono::steady_clock`'s, so
  no new helper is needed; adding 1 ns to a saturated `steady_time_point::max()` by hand would overflow a signed
  representation, which round 0's "plus the guard" did. The guard exists so that a mock clock firing at exact
  equality does not run the give-up before a test can deliver its echo (`TR_DistinctNow`'s comment); it is R-3's
  "after 2T", within one tick. Cells: exactly 2T, the echo still wins; 2T + 1 ns, the give-up; a `constexpr` cell at
  the saturated value (`steady_time_point::max()` in, `max()` out).
- **The Heartbeat during the grace window.** The loop sleeps through the grace with inbound silent and sends no
  Heartbeat there, as today. With T = 1.2 × HeartBtInt that window is longer than HeartBtInt, so a Heartbeat due
  inside it is skipped, which is QuickFIX C++'s rule (`needHeartbeat` requires `!testRequest()`). FR-010 and contract
  C-6.3 state it, and a cell pins it (H = 10 s, a silent peer: a Heartbeat at 10 s, the TestRequest at 12 s, no
  Heartbeat at 22 s, the give-up just after 24 s).
- **The liveness predicate** `liveness_may_originate_()` is tested after every suspension (D2 item 6).
- **Validation (FR-011).** A set threshold of zero or below is refused: `Session::open()` and
  `Engine::register_session` return `invalid_session_config`, as for `logon_timeout_ms == 0` (093); the TOML
  mapper records an `out_of_range` diagnostic at `session[i].test_request_threshold` for a zero duration.
  `parse_duration_to_ms` cannot produce a negative value: it scans leading digits and refuses a token that does not
  start with one as `malformed_value` ("duration must start with a numeric value"), so a negative TOML token keeps
  that diagnostic (contract C-6.6). Both are cells; extending signed duration parsing is out of scope. There is no upper bound beyond the parser's own overflow guard: `"1h"` (`pos_scalars_extra.toml`, asserted
  by `test_load_happy_path.cpp`) stays accepted. A T above the gap deadline is accepted, and 2T overflowing the
  clock saturates (the give-up never fires before the clock's end).
- **Precedence** (FR-010; F-12). The gap deadline is tested only when a frame arrives, and every frame that reaches
  it has refreshed liveness (the per-frame writer of `on_inbound_active_` runs before every sequence test). So a peer
  that keeps sending frames that do not advance NextNumIn ends at the gap deadline with its Logout and R-2, whatever
  T, and a silent peer with a gap open ends by R-1 at 2T. Round 0's "the gap deadline precedes the give-up" came from
  the re-derive file and holds for neither. Cells: gap open, the peer sends a non-advancing frame after 2H, the
  deadline's Logout; gap open, the peer silent, R-1's Logout at 2T.
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
- **The other stubs** of `ReconnectFsm` with no caller (`run_heartbeat_cadence`, `run_inbound_liveness_watch`,
  `validate_inbound_heartbeat_testreqid`, `process_inbound_sequence_reset`, `reply_to_inbound_resend_request`) are
  removed too, with their comments (owner ruling on SI-1, 2026-11-20; FR-013). Test comments that name them as RED
  witnesses are corrected. Caller census:
  `grep -rnE '\.(run_heartbeat_cadence|run_inbound_liveness_watch|validate_inbound_heartbeat_testreqid|process_inbound_sequence_reset|reply_to_inbound_resend_request|drive_logout)\(' src tests`,
  known positive `drive_logout`'s test call; re-run at the implementation head.

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

**Decision: C-ABI 1.18, BREAKING, with no symbol, type or error-code change.** Article X §7's action depends on its
predicate, "before the first public release" (the first GitHub Release, as the constitution's v-history note for
§7 defines it): a MINOR bump only while no Release exists.
`gh release list --exclude-drafts` on the fixpp repo returned nothing when Gate A round 1 read it; the predicate is
re-read at the PR head as a precondition of the bump and its result goes in the evidence file, and a non-empty
listing switches the action to the constitution's post-release rule. `include/fix/c_api/session.h`'s
1.11, 1.12 and 1.17 clauses state results this feature changes: an establishment refusal's connection "stayed
open" to the deadline; endings "with no Logout"; a session "ends" when a TestRequest goes unanswered at today's
timing; a too-low frame's Logout followed by the session's end. Article X §7 counts a changed documented result
as breaking, and 103 bumped to 1.17 for the same kind of change. The observers (each gets a 1.18 clause, and the
older sentences are amended in place): `fixpp_session_is_established` (true until the R-2 wait ends; stays true
for a knob-off peer that answers; the TestRequest give-up at 2T), `fixpp_session_close`, `fixpp_session_send`
(refused during the wait), `fixpp_session_register_callback` and `fixpp_session_register_send_callback`. What no
declaration carries, the counterparty's side, goes in `version.h`'s 1.18 history: the connection closes at every
ending, the unanswered TestRequest's Logout, the TestRequest at 1.2 × HeartBtInt.
- Each affected declaration gets a C-driven witness in `tests/capi/recovery_liveness_capi_witness_test.cpp`,
  `fixpp_session_close` (graceful and terminal) during the wait and callback delivery during the wait included.
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
  the mock clock still and pumps the executor until the peer's read ends or a wall cap passes. Four timers run on
  wall time, not on the mock clock, and can also close a connection: the TLS handshake and first-frame deadlines,
  `close()`'s grace timer (`close_grace`) and `drain_for_reconnect_`'s bound, each an `asio::steady_timer`. A cell
  sets each one it can reach well above its wall cap (or does not reach it), so with the clock frozen a hit is the
  mechanism's. The spurious-hit arm: the same cell
  with the latch's consumer deleted (a mutant) must go RED, and a cell with a short `logon_timeout_ms` asserts the
  EOF arrives before the deadline and with no `session_event_establishment_timeout`.
- **R-2 cells, one or more per row of contract C-2 "While the wait is armed"** (W-1 to W-14), each with the mutant
  that deletes its mechanism:
  - W-1: the peer's Logout at the number a real peer uses (C-2.2): below NextNumIn after the too-low Logout, above it
    after the gap-deadline Logout, at NextNumIn after the SendingTime Reject's Logout; EOF follows it, with the reset
    under `reset_on_logout` and NextNumIn advanced only at NextNumIn. A cell that sends an in-sequence Logout after
    the too-low Logout would pass with the defect live, so the off-NextNumIn cells are the discriminating ones.
    Mutants: the response test deleted; the response test moved below the too-high branch. Before Active: the
    peer's Logout ends the wait and resets nothing (owner ruling on OQ-1).
  - W-2 to W-5: an application message (reaches `fromApp`), a TestRequest (answered), a ResendRequest (answered),
    a Heartbeat answering the pending TestRequest, each during the wait after Active, on the SendingTime-Reject route
    (the one route where the peer's frames are in sequence; contract C-2.2); on the too-low route the same frames,
    below NextNumIn, are W-8 cells (absorbed). The same frames drained before Active. A W-2 cell built on the
    too-low route with an in-sequence frame would be a spurious hit.
  - W-6: a frame above NextNumIn during the wait: not delivered, a ResendRequest on the wire, as in Active.
  - W-7: a gap open with its deadline passed: a frame during the wait does not draw a second Logout.
  - W-8: each absorbed condition (a too-low frame, a second SendingTime Reject, a TestReqID mismatch, a CompID
    mismatch, MsgSeqNum 0), each followed by an application message (reaches `fromApp`) and the peer's Logout
    (resets under `reset_on_logout`), with `onLogout` counted once at the wait's end. Mutant: the silent-ending
    helper records `Disconnected` while armed.
  - W-9: a store failure during the wait closes at once.
  - W-10: `send()` refused. Numbering (C-2.3): a send held in its store call, the ending triggered, the store
    released; the send's number is below the Logout's (the wire order is not asserted; L-105-e).
  - W-11: the wait armed one tick before the Heartbeat instant, and one tick before the give-up instant; nothing
    originated. Mutant: one post-suspension test reverted to `fsm_state_ != Active`.
  - W-12: EOF at exactly the bound, not before (advance to bound − 1 ms, assert open; advance 1 ms, assert EOF); the
    bound does not slide (a Heartbeat answering the peer's TestRequest mid-wait).
  - W-13: `close(graceful)` during the wait, on TLS, asserting close_notify and the close at the bound's instant on
    the mock clock, with `close()`'s grace (wall time) set long; a cell that sees only EOF would pass through the
    grace's abortive close. Mutant: the bound's live predicate tests `closing_or_stopped_()`.
  - W-14: `close(terminal)`, `Engine::stop()` and the drain during the wait: one close each.
  - The state machine (data-model E-3): before Active, the bound survives the refusal's own `Disconnected` write
    (mutant: the bound spawned before that write; mutant: the arming after it); the bound's pre-Active expiry closes
    (the second setter); timeout 0 and no clock arm nothing and close after the write; a second write of
    `Disconnected` owes nothing (mutant: the edge's transition test deleted).
  - The pre-Active wait bounded by the establishment deadline, with no timeout event.
- **Timing cells that discriminate.** H = 10 s on the mock clock: no TestRequest at 11 s (the base sends at 10 s),
  one at 12 s; Active at 23.9 s (the base ends near 20 s), ended at 24 s plus the guard (cell name
  `…EndedAt24sPlusGuard`). A set T (7 s): TestRequest at 7 s, give-up at 14 s plus the guard. Both roles. The guard
  cells of D6 (exactly 2T, 2T + 1 ns, the saturated `constexpr` cell); D-c's cell (traffic after the TestRequest,
  no Heartbeat); D-c's consequence (traffic after TR1, then T of silence draws TR2, then a late Heartbeat with TR1's
  112 ends the session, newly reachable and within the ruling); the skipped Heartbeat of the grace window; the two
  precedence cells.
- **Lifetime cells** (ASan): a closed Session destroyed after each new frame has exited, the R-2 bound and the
  close frame; the close frame outliving its Session is legal (it holds only the transport and the counter).
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
  are a state transition, not a close" and its D-3 twin, FR-018), `error.hpp` slot 74, and the caller preconditions
  on `drive_reconnect`'s declaration in `session.hpp` ("close() runs before the Session is destroyed (~Session
  asserts the too-high Logout bound's counter is 0 …)"), which gain the R-2 bound beside the too-high Logout's bound
  and state that the close frame holds only the transport and its counter, so it may outlive the Session.
- **B&L plan** (live file only):
  - *Retire* L-103-5 to the closed file; its QuickFIX statement is not carried forward as written.
  - *Amend in place, dated, naming 105:* B-537-2 (the "transport stays open" clause; its silent grace-check ending,
    whose reach the default T changes), B-537-3 (#582 fixed), L-093-1 and L-092-1 ("closes nothing (fixpp#534)"; L-092-1's "a silent `Disconnected` at the TestRequest's grace
    check" becomes "R-1's Logout and the close", not "the gap deadline", since the gap deadline never fires for a
    silent peer; and its "or leaves a TestRequest unanswered" goes, since a peer that keeps sending is not silent and
    is not given up, clarification D-c),
    L-093-6 (the rejects are now logged; garbles of a connection that yields no Session are still not counted),
    B-093-4 (the deadline no longer closes a refused connection), B-092-2 and B-092-5 (the grace window is 2T),
    B-103-10 ("then `Disconnected`" becomes "then the R-2 wait, and `Disconnected` at its end") and B-104-6 (still
    `Disconnected` at once, and the connection closes at the peer's Logout or the bound), and B-103-13 (the direct
    driver's close-before-destroy precondition also covers the R-2 bound; the close frame is exempt, holding only
    the transport and its counter).
  - *New rows:* `B-105-1` the close rule by ending class (FR-001 to FR-008); `B-105-2` R-2's wait and its
    observables; `B-105-3` R-1 and its departure from QuickFIX; `B-105-4` the #582 rule and its 112 split;
    `B-105-5` the TestRequest timing and the threshold's validation; `B-105-6` the pre-session and listener records;
    `B-105-7` BREAKING C-ABI 1.18 and the C++ layout changes; an `L-105-*` row for each disclosure of the
    contract's "Disclosed" section (L-105-b to L-105-f; L-105-a was withdrawn in Gate A round 1 and is not used).
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

- **R1. Two closes on one TLS transport** (`close_transport_` and `Engine::stop()`'s step 2). `close_async`
  subsumes the second caller (F-5), and stop's step 4 `close()` joins the counter. A TSan cell runs the race.
- **R2. A latch left set with no consumer** (a spawn that threw; a direct Session whose application never calls
  anything again). The connection then ends at `close()` or `Engine::stop()` as today; a Debug assert in
  `close()` cannot help, so the census table's "every root" column is the control.
- **R3. Timing cells on real clocks.** Every competing timeout is on the mock clock (D10); the wall cap exists only to
  stop a broken cell.
- **R4. Interop peers.** QuickFIX peers in `tests/interop` now see a Logout before EOF on the unanswered TestRequest
  and an R-2 wait; their cells are run.

## Spec issues for the orchestrator (all ruled 2026-11-20)

- **SI-1 (FR-013 vs R-4 (b)): ruled, remove every caller-less stub.** R-4 folds "the whole `ReconnectFsm` dead set"
  into #559, and FR-013 now names every `ReconnectFsm` stub with no caller (D7, data-model E-9).
  `run_inbound_liveness_watch`'s header comment, which states a TestRequest timing, goes with it.
- **SI-2 (FR-010, HeartBtInt ≤ 0 vs = 0): ruled, ≤ 0 disables.** Today `run_liveness_loop` returns only at exactly 0
  (`heartbt_int.count() == 0`), while `detail::resend_gap_deadline` is off at ≤ 0. A negative interval is reachable
  only through C++ (`SessionConfig::heartbeat_interval`; the C setter takes `uint32_t`, the TOML duration parser
  yields no negative). The loop's test becomes `<= 0`, with a cell.
- **SI-3 = OQ-1 (FR-004, the peer's Logout after a pre-Active refusal): ruled, no reset.** FIX-SL 2020 §4.6 describes
  the Logout exchange for an established session and says nothing on a refused Logon; QuickFIX C++, /J and /Go
  close at once after a too-low Logon's Logout (`Session::doTargetTooLow` in C++; `Session.doTargetTooLow` in
  QuickFIX/J; the `targetTooLow` case of QuickFIX/Go's `logonState`), so none of them receives this Logout; and
  104's RU-2 list has no reset point for a session that never reached Active other than arm S. The peer's Logout
  ends the wait and the connection closes (contract C-2 W-1, C-2.6); one cell pins it.
- **L-105-b (direct Session after an inbound-frame ending): accepted**, FR-001 scoped to Engine-managed sessions.

The spec is amended for each (Clarifications, FR-001, FR-004, FR-010, FR-013).

## Owner questions (Gate A round 1)

- **OQ-3 (scope; review finding N-5): bound R-1's Logout write?** A write that never completes blocks every
  Logout-first ending behind `write_gate_` (F-14), so such an ending closes only at `close()` or `Engine::stop()`.
  This round discloses it (contract C-11 L-105-f) with a follow-up issue, which is in scope. The alternative is a
  design addition: race R-1's leaf with `logout_disconnect_timeout_ms`, then `close_async`, which falls back to the
  abortive close (B-348-1). It would also be the first write in the session with a bound, so it is the owner's call
  whether it belongs to 105 or to the follow-up.
