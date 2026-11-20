# Research: 105 transport and liveness (B27)

**Feature**: [spec.md](spec.md) · **Plan**: [plan.md](plan.md) · **Base**: the branch's merge-base with `origin/main`
(the source was read at `52aeed4e`; the rebase onto `310ff565`, #713, changed tests only).

Phase 0 of `/speckit-plan`. Every behaviour claim below was re-read in source at the base and is cited by
symbol. The re-derive in the parent repo (`research/findings/b27-rederive-2026-11-20.md`) and the SecondBrain
pages were used as leads only. Owner rulings R-1 to R-4, clarifications Q1 and Q2, the rulings on SI-1 to SI-3, OQ-1 and L-105-b, the
Gate A round 1 decisions D-a to D-c and the Gate A round 2 decisions D-d to D-f (spec Clarifications) are final and
are not re-opened here.

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
  replay-gate wake only; `store_then_emit` does not re-test it before the transmit. (105 limits the writes of the
  ending Logouts only, D2 item 11; every other write keeps this property, L-105-f.)
- **F-15. TestReqIDs are monotonic per Session.** `run_liveness_loop` builds each one as `TR` followed by
  `std::to_chars` of `++next_test_request_id_`, a per-Session `std::uint32_t` ("Per-session TestRequest ID counter"
  in `session.hpp`); `git grep next_test_request_id_ -- src include` finds that increment and the declaration only,
  so nothing resets it, and `to_chars` writes no sign and no leading zero. Whether a TestReqID is an earlier one of
  this connection is therefore decidable from its value and two integers (D5).
- **F-16. A transport close returns a blocked write.** The plain transport has no `close_async` of its own
  (`Transport::close_async`'s default is `co_return close();`), and `asio_plain_transport::close` closes the socket,
  which completes a pending `async_write` with an error. `asio_tls_transport::close_async` sets `state_ = closed`,
  then, while `ssl_op_suspended_()` (which reads `write_in_flight` as well as `read_in_flight` and
  `handshake_in_flight`), calls `socket_.cancel()` and joins the op within `close_deadline`
  (`std::chrono::steady_clock::now() + cfg_.tls_close_timeout`), then writes close_notify by `async_shutdown` under
  the same deadline; at the deadline its timer cancels the shutdown, so the socket closes with no close_notify
  written (B-348-1's abortive close). `live_write_serialized_` maps the
  write's error to `dispatch_aborted` and its RAII guard releases `write_gate_`; a waiter at the gate then writes to a
  closed transport and gets the same result. `close()`'s graceful phase already uses this unwind
  (`close_grace`, "FQ-G: if phase 1 wedges behind write_gate_ / async_write, force-close the transport"), and
  `drain_for_reconnect_` does at its bound.

## D1 — How a `Disconnected` edge gets the transport closed (FR-001, FR-002, FR-008)

**Decision: an edge latch, consumed by the root that owns the ending.**

1. **The latch.** A private member of `Session`, `connection_end_owed_`, holding the `conn_gen_` the close is owed
   on. One setter, `owe_connection_end_()`, sets it when all of these hold: `state_ == lifecycle::open` (not inside
   `close()`), `!reconnect_drain_in_flight_` (a new flag held only around `drain_for_reconnect_`'s call in
   `drive_reconnect`, B-103-9), and no wait **armed before Active** is open (D2; Gate A round 2, root cause #1). A
   wait armed after Active does not refuse the owe: the call ends that wait and owes (data-model E-3 row 12), so any
   `Disconnected` written during it, from any root, closes the connection. It has three callers (data-model E-2): the
   edge, on a transition **into** `Disconnected` from another state only, so a later write of `Disconnected` over
   `Disconnected` cannot owe a second close; the R-2 bound's expiry for a wait armed before Active; and the response
   to such a wait. The latter two exist because before Active the session is `Disconnected` already, so no edge
   runs. `reconnect_in_flight_` cannot serve: its holder in `drive_reconnect` stays alive across
   `co_await emit_initiator_logon_()`, so it would hide every Logon-emission ending of a direct initiator.
   `install_reconnected_transport` records only `LogonSent` (and nothing on its refusal path), so the narrower
   window exposes no further write; the census re-reads it at the implementation head.
   **Placement (N-4).** `record_state_transition_` returns at `if (engine_.application == nullptr) return;` before its
   `onLogon`/`onLogout` block, and `EngineConfig::application` defaults to `nullptr`. The edge's call therefore sits
   in the existing `new_state == fsm_state::Disconnected` block, beside `wake_peer_logout_bound_()`, using the prior
   state captured at entry (the function already captures `was_active` there), so it runs before `onLogout` and for
   a session with no application alike; one cell runs with no application. Setting it is two stores: no suspension,
   no allocation, no spawn (F-1).
2. **Consumer (a), the pump.** After every `on_inbound_frame` return, the pump closes as it closes on an error:
   `if (!r || session_engine_access::take_connection_end(session)) co_await stop_pump();`. `take_connection_end`
   tests and clears the latch, and leaves it set while an ending Logout is being written by another root (data-model
   E-3, "Other roots during `writing`"), so the pump does not cut that Logout; the writer's root consumes it after
   the write. The error close (`!r`) does not defer, as on `main`. The test runs after the arm has returned, so after its reply Logout, its persist
   and its receipt reset unit (F-3, FR-005, FR-008). The close is `close(terminal)`, the path F-5 describes, so
   FR-002 holds by construction.
3. **The acceptor's first frame goes through the pump.** `run_read_pump` gains a `first_frame` parameter
   delivered before the coalesced surplus, under the same disposition as every later frame. `run_accept_loop`
   step 8 is deleted (F-9, FR-006). `run_connect_loop` passes an empty span. The first frame is delivered without
   the loop's `expired()` pre-check, as today: `read_first_frame_bounded` already bounded its read by the
   establishment time left, and a pre-check would turn a Logon read at the deadline's edge into a timeout.
4. **Consumer (b), every non-pump root, at its exit.** `Session::release_connection_end_() noexcept`: when the
   latch is set, its generation is `conn_gen_`, `state_ == lifecycle::open` and `inbound_depth_ == 0`, it spawns the
   close frame (item 5) on the session's executor, detached, and clears the latch after the spawn has returned. The
   spawn follows `on_inbound_active_too_high_logout_`'s shape, not the liveness loop's: the join is the coroutine's
   parameter (`connection_close_join`, which increments `connection_close_counter_` at construction and decrements it
   at destruction, so a frame that is never published releases it), and the `co_spawn` sits in a `try` whose
   `catch (...)` **fails closed** (Gate A round 3, root cause #1; clarification D-g): it calls the synchronous
   `Transport::close()` of the transport it took and clears the latch, so the count stays balanced and the peer still
   observes EOF with no later call (on TLS, abortive while an SSL operation is suspended, L-105-h; contract C-8.6).
   Round 2's catch left the latch for a later `close()` or `Engine::stop()`, which an Engine session ended by a
   detached root while its pump is parked in a read does not get. The precedent's `catch` restores the mark and
   rethrows (`throw;`); this one must not, `release_connection_end_` being `noexcept`. The liveness loop's spawn sites, which
   `fetch_add` and then call a bare `co_spawn`, leak the count on a throw and are not copied; they are on `main` and
   outside this feature's change. Every test is made here, synchronously, so the frame decides nothing. The spawn is
   on `exec_.underlying()`, never the wrapper executor (the TSan class `brain` records for wrapper spawns). When
   `inbound_depth_ != 0` the latch is left set: a frame is inside an arm, and the pump consumes the latch when that
   arm returns (F-7), so no store work an arm owns is cut. While an ending Logout is being written
   (`ending_wait_.phase == writing`) the latch is left set too, and the writer's root consumes it after the write.
5. **The close frame, `close_transport_(join, transport)`**, a static coroutine that receives its join and the
   transport's `shared_ptr` at the spawn, awaits `close_async()` on that transport, and touches no member
   of `Session` (data-model E-2). `close()` and `drain_for_reconnect_` join it (item 6) and `~Session` asserts its
   counter is zero, so a Session destroyed after `close()`, as B-103-13 requires, has no close frame running; the
   frame widens no lifetime precondition. It runs on its own spawn chain, so `close_async`'s cancellation clobber
   stays there (F-6).
   - **Engine session:** the pump's parked read completes with an error (`close_async` cancels it, and a
     re-read is refused once the transport is closed), the pump runs `stop_pump`, and `close(terminal)` joins.
   - **Direct Session:** the connection ends and the Session stays open in `Disconnected`, so
     `drive_reconnect` can drain and install a new transport (F-4; spec Assumptions, "a reconnect is not
     blocked").
6. **Joins.** `close()` clears the latch when it commits `closing`, and before its own `close_async()` joins
   `connection_close_counter_` by post, as it joins the liveness loop. So the two never run `close_async` on one
   transport at once (FR-002). `drain_for_reconnect_` clears the latch and joins the counter before step 3's
   quiescence test, so no close frame outlives its connection. Both join the counter **a second time, after they
   join the ending bound's counter** (Gate A round 3, N-3): the bound's write-limit expiry spawns a close frame while
   `close()` or the drain may be waiting (D2 item 11), and it spawns it before it exits, so the second join collects
   it (data-model E-2 I-4). `~Session` asserts the counter is zero,
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
before Active (data-model E-3 rows 9 and 11). Every root that can reach a caller is a consumer or an exclusion. A new
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
(W-1 to W-15); the state machine of the ending Logout (its write limit, the wait, the bound and the latch) is one
table, data-model E-3. Gate A round 2 made that machine total (N-1: absorption by cause, item 4) and added the
write limit of ruling D-e (item 11). This section is the mechanism.

**Decision.**
1. **One helper starts every R-2 ending and owns its ending write.** `Session::send_ending_logout_(text)` writes
   the Logout through the existing leaf for the state (`inbound_logout_leaf_` after Active;
   `refuse_logon_with_logout_`'s build and emit and `inbound_logon_sent_logout_leaf_` before it), then either arms
   the wait or records the ending as the site records it today. The `Disconnected` writes that today sit in each
   caller's tail move into the helper: Arm B's unconditional write, `resend_gap_deadline_`'s
   `record_disconnected_unless_already_()`, `refuse_logon_with_logout_`'s `disconnect_unless_superseded_`, and the
   SendingTime and OrigSendingTime arms' writes. Without that move an absorbing helper would change nothing.
   - **Armed only on leaving `writing`** (item 11; data-model E-3), so only when the bound was spawned, and only if the
     Logout was transmitted, the ending claimed no pending too-high mark (below), and no other root ended the session
     during the write (`fsm_state_` still the state the write started in). A clock and
     `logout_disconnect_timeout_ms > 0` are the conditions of entering `writing`. The
     leaves return success also when nothing went out: the Logout did not build (`if (lo_result)` false, the long
     CompIDs case), its emit failed (`(void)emit_r`), or `store_then_emit` returned success without transmitting
     (a superseded Logon arm; a frame held back while `wire_closed_by_reset_` is set). So `store_then_emit` gains
     a private out-parameter, set only when a transport accepted the frame: `live_write_serialized_` returned
     `written` **with a live transport present** (it also returns `written` when there is none), or the direct
     sink returned success. The leaves pass it up. Timeout 0 and no clock arm nothing (contract C-2, "No wait is
     armed"; the too-high path's `logout_disconnect_timeout_ms == 0` branch is the precedent), and spawn no frame.
   - **The order before Active** (data-model E-3 rows 1, 3, 6): enter `writing` and spawn the bound (item 11), the
     Logout's write, arm, then the refusal's own `Disconnected` write (`disconnect_unless_superseded_`, now in the
     helper). The bound is running when that edge wakes it; its wake is a re-test, and its pre-Active predicate
     (`state_ == lifecycle::open`) holds, so it waits on.
   - **The pending too-high mark (N-5).** `resend_gap_deadline_` and Arm B call `claim_peer_logout_none_()` before
     their leaf ("its own Logout is the one"). The claim moves into the helper and reports whether it found the mark
     `pending`. When it did, the peer's Logout has already been received, so the helper arms no wait: it records
     `Disconnected` after the write and the connection closes then, as R-1's does (contract C-2.8). The claimed
     mark's receipt does not reach its reset point under `reset_on_logout`, as on `main`, where both sites claim and
     end at once; without this rule 105 would add a full bound of waiting for a second Logout a real peer does not
     send. One cell: a too-high Logout while a resend is outstanding, then a non-advancing frame after the gap
     deadline; the peer reads the deadline's Logout, then EOF with the mock clock frozen.
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
4. **Absorption, by cause** (W-8; Gate A round 2, root cause #1). While an ending is in progress after Active
   (`ending_in_progress_()`: the phase `writing` or `armed` with `from_active`):
   - `send_ending_logout_` writes nothing and records nothing. Whatever the arm wrote before calling it (the
     SendingTime arm's Reject) has been written, as in Active.
   - The **peer-frame** silent endings go through one helper, `end_session_silently_()`, which records
     `Disconnected` unless an ending is in progress: the BeginString and CompID guards, MsgSeqNum 0, the TestReqID
     mismatch of C-5.4 (both sites, through `classify_test_req_id_`), and D-3 of `dispose_unparseable_`.
   - **Nothing else is absorbed.** The absorbed set is these peer-frame conditions and `send_ending_logout_`'s
     callers, never "a site that returns success". Every internal failure (a number assignment, a store, a persist,
     an emit, a `toAdmin` throw) and every other root (a send admitted before the Logout whose store or emit fails,
     `send_body_`'s handler, a liveness emit admitted before it, `run_peer_logout_bound_`'s handler) writes
     `Disconnected` through the edge, and the edge ends the wait and owes the close (data-model E-3 row 12). So
     `inbound_resend_request_leaf_`'s failed assignment, which records `Disconnected` and returns success, ends the
     wait as FR-004 requires ("not absorbed"), and the pump closes on the latch. Arm B's tail for a refusal that is
     not too-low (a drained sequence-number mutex) is an internal failure, not absorbed.
   - Census: D1's write-site grep restricted to the frame arms, each hit classed by cause, "peer-frame condition"
     (through the helper) or "internal failure" (through the edge); known positives: the BeginString guard of
     `on_inbound_active_` (peer-frame) and `inbound_resend_request_leaf_`'s assignment failure (internal).
   - `resend_gap_deadline_` does not fire (it returns `nullopt`), so its frame takes the row it would take before
     the deadline (W-7). The wait's own ending is already running.
   - The armed test is in `owe_connection_end_()` and in these two helpers, never in `record_state_transition_`'s
     write, which also serves `close()`, the drain and the error arms (data-model E-3, its invariant).
   Round 1 classed silent endings by return value ("returns success" absorbed); that absorbed an internal failure
   FR-004 says is not absorbed, and, classed the other way, left the wait armed with nothing to close (N-1).
5. **A frame above NextNumIn** with no resend outstanding (W-6) is processed as in Active (Q2; FR-004's "the sequence
   checks apply"): `on_inbound_active_too_high_` sends its ResendRequest, a reply to the peer's frame, as QuickFIX
   C++ does during its wait (`validLogonState` admits a non-Logout frame once `sentLogout` is set, and `verify`
   reaches `doTargetTooHigh`). Contract C-10's departure covers fixpp's own originations only (the liveness
   Heartbeat and TestRequest), not replies.
6. **The liveness loop** (W-11) tests one predicate, `liveness_may_originate_()` (Active and the ending phase
   `none`; data-model E-4), as its loop condition and after every suspension: after its sleep and after each emit.
   So nothing is originated from the start of an ending Logout's write, R-1's own included. The arming adds no wake:
   the predicate alone keeps the loop from originating, and `close()` and the drain still join it.
   `root_cancel_.emit` is not used as a wake: it reaches everything bound to the root slot.
7. **Admitted frames** (contract C-2.3, L-105-e). A send, Heartbeat or TestRequest numbered before the Logout and
   still in its store call can reach the wire after it (F-14). The design narrows the promise to numbering and
   discloses the wire order, instead of a barrier: the race exists on `main` for every Logout ending, on the
   SendingTime-Reject and gap-deadline routes the peer can recover the frame by ResendRequest during the wait (W-4;
   on the too-low route its ResendRequest is below NextNumIn and is absorbed), and a barrier on `replay_gate_.shared` would have to
   exclude the waiter's own hold (R-1 runs inside the liveness loop's emits) and be bounded.
8. **The bound.** `deadline = deadline_after(now, logout_disconnect_timeout_ms)` on `effective_clock_`, read once at
   the **start** of the Logout's write (item 11), never moved by a later send or by the write's completion (FR-004;
   QuickFIX/Go's rule, not C++'s and /J's sliding `lastSentTime`): one budget for the write and the wait. Before
   Active on an Engine session the bound is **capped by the establishment deadline** (Gate A round 3, root cause #2;
   Codex #3). The pump's own test cannot do it while the refusal's arm is suspended: `run_read_pump` tests
   `expired()` at its loop head and before each delivery, and races the deadline only against `async_read_some`. So
   the pump hands its `establish_deadline` to the Session once (`session_engine_access::set_establishment_deadline`,
   one store, data-model E-5), and the bound waits on both instants, each on its own clock (the bound's on
   `effective_clock_`, the establishment deadline on `engine_.clock`, which `SessionConfig::clock_override` does not
   move), so it closes at whichever passes first, during the store call and write (E-3 row 4) as during the wait
   (row 11). The two waits are independent sleeps on an Engine session; data-model E-3, "The establishment cap",
   states that condition and its re-derivation recipe (Gate A loop 2 round 1, root cause #2). The pump's `expire()` keeps the cap while it is in its read race. At either close no
   establishment-timeout event or record is written, because the session has recorded its own ending (D4). A
   Session driven without an Engine has no establishment deadline (`logon_timeout_ms` reaches `run_read_pump` only
   through the accept and connect loops), so its pre-Active wait ends at the bound (Gate A round 2, Codex #4; contract
   C-2.6). A detached `run_ending_logout_bound_(join, gen)` waits on it, in the shape of `run_peer_logout_bound_` (a
   join parameter that touches only `ending_logout_bound_counter_`, a published wake, `detail::await_deadline` raced
   with a timer the wake cancels, a generation assert, an exception handler that ends the wait closed), with two
   differences: its failure exit closes as its expiry does, never through `release_connection_end_` (data-model
   E-3, "The bound's exits", row 16; Gate A loop 2 round 1, root cause #1), and it loops, so a wake is a re-test of its live predicate, not an exit. Its spawn takes
   `on_inbound_active_too_high_logout_`'s join-as-parameter shape, with a `catch` around `co_spawn` that, unlike the
   precedent's, does not rethrow. A spawn that throws **fails closed** (Gate A round 3, root cause #1; clarification
   D-g): the phase goes back to `none` and the Logout is not written (the leaf's path for a Logout that cannot be
   built), so the site records `Disconnected` and the connection closes at once (data-model E-3 row 2a). Round 2's
   rule, "proceed as with timeout 0", started an unbounded write that ruling D-e does not allow. Its wake
   sources, its live predicate by phase, and what each exit does are data-model E-3. The predicate is not
   `run_peer_logout_bound_`'s `live()`: that one requires `fsm_state_ == Active` (false before Active) and
   `!closing_or_stopped_()` (false under a graceful `close()`, which would leave nothing to clear the Logout owner but
   the peer's Logout, and hand the close to `close()`'s wall-clock grace and its direct `Transport::close()`,
   abortive on TLS).
9. **`close()` and `Engine::stop()` during the wait** (W-13, W-14; data-model E-3 rows 13 to 15). A graceful
   `close()` after Active poll-joins the Logout owner (103 D4.6) while the bound runs on; the response or the bound
   records `Disconnected`, which clears the owner, and `close()` proceeds to its own `close_async()` (contract
   C-8.4). A terminal `close()`, an escalation, `Engine::stop()` and the drain end the wait at once; `close()` and
   the drain join the bound's counter, then the close frame's counter again (D1 item 6). During the Logout's write
   (`writing`) the closers differ by mode (item 11).
10. **`onLogout`** fires at the `Disconnected` edge, as today. After Active that edge is the wait's end (the
    response, the bound, any internal failure of E-3 row 12, a cut write, or `close()`), which is QuickFIX C++'s timing (`Session::disconnect` calls
    `onLogout`). Before Active it does not fire, as today: it fires only on an Active→!Active edge.

11. **The write limit** (ruling D-e; contract C-2.7; data-model E-3 rows 1 to 5). Every Logout fixpp writes to end
    a session enters the phase `writing` at its write point (`inbound_logout_leaf_`, where it claims the Logout
    owner, after Active; the build-and-emit of `refuse_logon_with_logout_` and `inbound_logon_sent_logout_leaf_`
    before it): R-1's (D3), every R-2 Logout (item 1), and the replies to the peer's Logout (`on_inbound_active_logout_`,
    `acknowledge_peer_logout_`), so the limit holds by construction at every ending Logout, not at a list of callers.
    The deadline is read and the bound spawned before the gate acquire, so a Logout queued at `write_gate_` behind a
    blocked application send or Heartbeat is covered too. Census of the after-Active write point:
    `grep -n 'inbound_logout_leaf_(tic' src/session/session.cpp`, each hit's enclosing function an ending (at the base:
    `resend_gap_deadline_`, `on_inbound_active_sending_time_reject_`, `acknowledge_peer_logout_`,
    `on_inbound_active_poss_dup_`, `on_inbound_active_out_of_sequence_`, `on_inbound_active_logout_`; known positive
    `resend_gap_deadline_`), and `run_logout_phase1` reaching no such call (it builds `close()`'s own Logout). Including
    the replies is this design's choice, by construction at the shared leaf; ruling D-e names R-1's and the R-2
    Logouts.
    - **The write starts at the owner claim**, so the limit can fire while the Logout is still inside its number
      assignment or its store call (`store_then_emit`'s Step 1, the store, runs before `live_write_serialized_`
      acquires `write_gate_`). The transport closes at the bound and the peer sees EOF then; `Disconnected` and
      `onLogout` follow when that store call returns (FR-008: a store call is not cut), not at the bound itself. Two
      consequences are disclosed rather than designed away (Gate A round 3, N-2; B-105-2): a persistent store slower
      than `logout_disconnect_timeout_ms` makes every ending Logout "not sent"; and a write that completes late
      leaves the peer only the rest of the budget to answer. Reading the deadline after the store instead was
      rejected: R-1's limit and the pre-Active establishment cap must also hold for a Logout held in its store.
    - **The bound cannot be spawned** (an allocation failure): no Logout is written, and the ending proceeds as
      "Logout not sent" (item 8; clarification D-g).
    - **On expiry with the write not returned**, the bound spawns the close frame (D1 item 5) on the live transport,
      testing only the generation and the transport's presence, then exits; a spawn that throws takes the
      synchronous `Transport::close()` instead (D1 item 4, clarification D-g). The close frame runs `close_async()`,
      which returns the blocked write with an error (F-16); `live_write_serialized_` returns `dispatch_aborted` and
      releases `write_gate_`; a Logout waiting at the gate finds the transport closed. **Nothing is cancelled and no
      frame is destroyed while suspended**: the leaf, the arm and the gate holder each resume and run to their end,
      so the leaf's buffer, the gate's guard and every frame keep their lifetimes. The leaf then returns as for a
      failed write (it discards the emit's result), the phase goes to `none`, and the site records `Disconnected`
      ("Logout not sent"): R-1 at once, an R-2 ending with no wait, a reply before its reset unit as today. The
      consumer that then runs (the pump's `close(terminal)`, the loop's release) finds the transport closed and is
      subsumed (C-1.a).
    - **This close runs while a frame is inside its arm** (the arm is the one suspended in the write), which is
      one of data-model E-2 I-2's exceptions and contract C-1.b's: it cuts no store work (durable-before-transmit;
      the receipt reset unit is store-only).
    - **The bound's expiry closes the same way** (Gate A round 3, root cause #2; N-1; data-model E-3 rows 10 and
      11). A wait's expiry while an arm is suspended, for example a resend answer (W-4) blocked in `async_write`
      behind a peer that stopped reading, would otherwise leave the close to `release_connection_end_`, which waits
      for `inbound_depth_ == 0`, so for an arm that may never return. The argument that lets the write limit close
      under a suspended arm does not depend on which deadline fires, so at its expiry the bound spawns the close frame
      testing the generation, a live transport and `state_ == lifecycle::open`, then clears the latch; the arm in
      flight resumes with a write error, and the pump's later `close(terminal)` is subsumed. The bound's own failure closes the same way
      (Gate A loop 2 round 1, root cause #1; Codex #2, N-1): its `catch (...)`, and a deadline arm that completed
      while its clock reads before its instant (`detail::await_deadline`'s `catch (...)` absorbs every
      non-`system_error` exception and completes the arm, which the loop would otherwise re-race at once), take the
      expiry exit of the phase they find (data-model E-3, "The bound's exits"). Routing that failure through
      `release_connection_end_` would defer it to a pump arm suspended in the Logout's write with nothing left to
      limit the write. What stays deferred is
      an ending recorded by a root that is not a deadline (E-3 row 12): while an arm is blocked in a non-ending write
      it closes when that write returns (L-105-f).
    - **No second wait.** A cut write is not transmitted, so the R-2 helper arms nothing; and since the wait would
      have shared the deadline, a write that completes just before it leaves only what remains of the budget. One
      budget from the write's start is this design's reading of D-e's "counted from the write's start" and R-2's
      "from the write of fixpp's Logout"; it moves R-2's origin from the write's end to its start, observable only
      when the write is slow.
    - **Other roots during the write** (data-model E-3 rows 5a, 5b): a `Disconnected` written by another root owes the
      close, but both latch consumers defer while `writing`, so the Logout is not cut; the writer's root consumes the
      latch after the write. An arm's error close does not defer, as on `main`.
    - **`close()`, `Engine::stop()` and the drain during the write**, by mode (Gate A round 3, N-3). A terminal
      `close()`, an escalation and `Engine::stop()` close the transport themselves (`close()`'s `close_async`, stop's
      step 2 `close_async`), which returns the write (E-3 row 5). A graceful `close()` after Active does not: its
      phase 1 poll-joins the Logout owner `logout_in_flight_`, raced only against `close_grace`, a wall-clock
      `asio::steady_timer` armed at the `close()` call (`close`, "The grace running out takes the force-close
      below"), and the blocked write holds that owner. The drain waits for quiescence against its own wall-clock
      bound and force-closes with `Transport::close()` only there. Under those two the write ends at the limit (the
      only session-clock bound on it) or at that wall-clock timer, whichever comes first (E-3 row 5c). The limit is
      therefore not conditioned on `lifecycle::open`. The bound runs on until the leaf returns; `close()` and the
      drain join it after their transport close, then join the close frame's counter again (D1 item 6), which
      collects a frame the limit spawned meanwhile.
    - **Timeout 0 and no clock**: no limit; the write is unbounded as on `main` (L-105-f). A 0 ms limit is not read as
      "close at once" (`close_grace`'s reading), because it would cut every R-1 Logout.
    - **TLS**: `close_async` cancels the write and then cannot write close_notify behind a peer that does not read;
      it closes abortively at `tls_close_timeout` (contract C-8.5, L-105-g; permitted by ruling D-e).
    - **What stays unbounded** (L-105-f, narrowed): every write that does not end the session.
    - **Alternatives rejected.** *Racing only the Logout's own await* (`write || timer`): the first write to block is
      usually an application send or a Heartbeat holding the gate, so the race would leave the holder blocked and the
      socket open, and it would cancel a frame suspended in a store or a write. *A separate watchdog coroutine*: a
      third detached frame with its own join and spawn-failure rule, where the R-2 bound already exists and needs the
      same deadline. *The synchronous `Transport::close()`* (as `close_grace` and the drain use): abortive on TLS whenever
      an SSL operation is suspended, even when the peer reads, which ruling D-e names `close_async` to avoid. It is
      kept only as the fallback when a close frame cannot be spawned (clarification D-g).

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
- At the give-up (D6), `run_liveness_loop` first writes the slot-74 record (below), **before** the Logout's write, so
  a write that never completes cannot lose it (ruling D-e). Then it takes a replay-gate shared hold as its other emits
  do (103 D5′.4), claims a pending too-high mark as the other ending Logouts do (`claim_peer_logout_none_`, 103
  D4.6), and writes a Logout through `inbound_logout_leaf_` (reached from a detached root as `run_peer_logout_bound_`
  reaches it through `acknowledge_peer_logout_`) with Text(58) `TestRequest unanswered, TestReqID <id>`, `<id>` being
  `pending_test_req_id_`. The phrase follows FIX-SL 2020 §4.5.5 ("a Logout(35=5) message with the Text(58) field
  stating that the session was terminated due to lack of a response to a TestRequest(35=1) message, followed by
  terminating the transport layer connection") and the shape of 537's "ResendRequest unanswered, expecting MsgSeqNum
  N". The text is built in the loop's frame in a bounded buffer with a `static_assert` on its size, as
  `resend_gap_deadline_`'s is.
- **The write is limited** (D2 item 11; kind `r1`): the leaf enters `writing`, so the write is raced against
  `logout_disconnect_timeout_ms` from its start, and at the deadline the transport is closed and the write returns
  as a failed one. No wait follows in any case (R-1 is FR-004a's exception).
- Then `record_disconnected_unless_already_()`. The edge sets the latch, and the loop's exit calls
  `release_connection_end_()` (D1). The connection closes as soon as the write has returned, or at the limit.
- **When the Logout cannot be written**, the leaf's own dispositions record `Disconnected` or leave it to the loop's
  `record_disconnected_unless_already_()`, and the close follows the same way. Contract C-3.2's seven classes, each a
  cell asserting the slot-74 record, the wire (no complete Logout at the peer, except the volatile-store class) and
  the peer's EOF, each with its own mutant: the buffer cannot hold the CompIDs (`if (lo_result)` false); `toAdmin`
  throws (the leaf records `Disconnected` and returns `app_callback_threw`); the number assignment fails
  (`record_disconnected_unless_already_()` and the error); the store fails (on a persistent store `store_then_emit`
  returns the error, which the leaf discards; on a volatile store the failure is logged and the write proceeds, so
  both are cells); the write fails (`live_write_serialized_` returns `dispatch_aborted`, and the leaf discards it with
  `(void)emit_r`, so the leaf returns success and the loop must still record and close); the write does not complete
  (a blocked transport write; D10's write-limit cells); the bound cannot be spawned (an allocation failure: no
  Logout is written, clarification D-g). The assignment-failure cell is R-1's, not the liveness
  emission's `LivenessAssignFails` (contract C-1 row 8).
- **Observable surface for slot 74.** One `FIXPP_SLOG` warn record, written at the give-up before the write:
  `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}`, `error` being
  `static_cast<uint64_t>(error::session_test_request_unanswered)`, registered in `src/log/format_registry.cpp` beside
  093's records. Round 1's `logout_sent={}` field is dropped: it was the leaf's transmitted bit, so it could only be
  written after the write, which is what a blocked write never reaches (Gate A round 2, Codex #5). Whether the Logout
  went out is observable on the wire. No rate bound: once per connection, as `note_establishment_timeout_`. The C ABI
  sees the wire (the Logout, then EOF), the log through its logger, and `fixpp_session_is_established` turning false.
  No call returns slot 74; that stays as today and is stated in B&L.
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

## D5 — An out-of-sequence Heartbeat with sequence validation off (FR-009, #582), and the TestReqID rule (ruling D-d)

**Decision (#582).** In `on_inbound_active_out_of_sequence_`'s Heartbeat branch, the call to
`answer_pending_test_request_(hdr.test_req_id)` runs when a TestRequest is pending and either (the frame is too
high and a resend is outstanding) or `!cfg_.validate_sequence_numbers`. The rest of the branch is unchanged: no
advance, no persist, no `fromAdmin`, no echo. With the knob on, the predicate is today's (B-537-3, 103's D6). The
routine's existing comment ("Whether a TestRequest is pending is each caller's test") stands.

**Decision (D-d, the 112 rule on every Heartbeat route).** One predicate, `classify_test_req_id_(std::string_view)`,
replaces the two comparisons that exist today, the in-sequence Heartbeat arm of `on_inbound_active_` ("We have an
outstanding TestRequest. Check echo.") and `answer_pending_test_request_` (which the too-high Heartbeat route of
`on_inbound_active_too_high_`, the Heartbeat branch of `on_inbound_active_out_of_sequence_` and #582's knob-off route
call). It returns **answers** (absent, or equal to
`pending_test_req_id_`), **earlier**, or **mismatch** (data-model E-4's table). Earlier: the 112 is `TR` followed by
the canonical decimal of a number in the modular window `(test_request_id_floor_, next_test_request_id_]`, other than
the pending one; the floor is a new per-Session `std::uint32_t` set to `next_test_request_id_` at every connection
start (`open()`, `install_reconnected_transport`, `attach_accepted_transport`), so a reconnect starts a new set. The
generator makes "earlier" decidable from the value (F-15), with bounded memory (two integers). An earlier 112 does
not clear the pending TestRequest and does not end the session; the frame has refreshed liveness as every accepted
frame does. A mismatch ends the session silently, `session_testreqid_mismatch`, through `end_session_silently_` (D2
item 4), and the pump closes it (D1). `answer_pending_test_request_` returns false only for a mismatch. Edge cases:
- No TestRequest pending: the 112 is not compared, as today (both sites test `!pending_test_req_id_.empty()` first).
- An absent 112 answers, as today.
- A 112 that is not `TR` plus canonical digits (a leading zero, a sign, letters, an overflowing number): never sent,
  so a mismatch.
- A 112 of a previous connection: outside the window, so a mismatch.
- 2³² TestRequests or more on one connection: the window no longer identifies the sent set (at exactly 2³²,
  `next == floor` and nothing reads as earlier; past it, the window is the modular remainder). B-105-5 states that
  condition; it is unreachable in practice (Gate A round 3, Codex #7). `constexpr` cells pin the predicate at
  `UINT32_MAX`, across the wrap and at one full cycle.
- It resolves the round-1 consequence of D-c (N-7): a late Heartbeat carrying TR1's 112 after TR2 was sent is now an
  earlier answer, not an ending.
Cells: each outcome on the in-sequence arm and on `answer_pending_test_request_` (a mutant per site that restores
the plain comparison), the floor reset across `drive_reconnect`, a non-canonical 112, and the late-TR1 cell of D10.

**Rationale.** It is the smallest change that makes the knob-off Heartbeat reach the TestReqID rule the knob-on
paths already share. QuickFIX/J is the only engine with the knob, and it clears the pending TestRequest before
its sequence checks (`state.clearTestRequestCounter()` on `Session.next(Message)`'s path); fixpp keeps the 112
comparison (FIX-SL §4.5.5, TC scenario 6), the split the spec's Departures table records. The owner's D-d keeps
the comparison's purpose (a 112 fixpp never sent is a broken peer) without disconnecting a peer that answers late.

**Alternatives rejected.** *Deliver the knob-off Heartbeat to the knob-off delivery arm* (`fromAdmin`): it changes
what the application sees for every out-of-sequence Heartbeat, which #582 does not ask. *A set of sent TestReqIDs*:
unbounded memory, where the generator's monotonic counter gives the same answer with two integers.

## D6 — TestRequest timing and `test_request_threshold` (FR-010 to FR-012, R-3)

**Decision.**
- **Where T is computed.** In `run_liveness_loop`, once before its loop:
  `T = cfg_.test_request_threshold.value_or(default_test_request_threshold(heartbt_int))`, where the default is
  1.2 × HeartBtInt in milliseconds. HeartBtInt is whole seconds, so 1.2 × H is exactly `H × 1200` ms with no
  rounding; the product saturates at `milliseconds::max()` (a `detail` helper beside `deadline_after`, with
  `constexpr` cells).
- **The instants.** TestRequest at `deadline_after(last_inbound_steady_, T)`; give-up at
  `deadline_after(deadline_after(last_inbound_steady_, T), T)`, two saturating steps with no product formed, the
  shape of `detail::resend_gap_deadline`; Heartbeat at `last_outbound_steady_ + H`. The outbound Heartbeat cadence
  (1 × HeartBtInt) and the gap deadline (2 × HeartBtInt, B-537-2) are unchanged.
- **One sleep per iteration** (Gate A round 2, N-2; the sleep set restated in Gate A round 3, root cause #3;
  data-model E-4). The loop sleeps until the earliest instant of its sleep set (the Heartbeat instant when eligible
  or not yet reached, the TestRequest and give-up instants when eligible), tests eligibility on waking, and acts on
  every eligible instant reached: while a TestRequest is pending with no inbound frame since it was sent, only the
  give-up is eligible; otherwise the Heartbeat and the TestRequest are. Round 2 slept to the earliest **eligible**
  instant, so right after TR1 it slept to the give-up and a frame at 13 s could not wake it: the Heartbeat due at
  22 s went at 24 s (`run_liveness_loop` suspends only in `effective_clock_->sleep_until`, and the per-frame refresh
  wakes nothing). Keeping the not-yet-eligible Heartbeat instant in the set fixes that; a Heartbeat instant reached
  while ineligible is dropped, or the loop would spin. A frame after the Heartbeat instant has the Heartbeat sent at
  the next wake, the stale give-up instant at the latest, at most max(0, T − H) late; FR-010 and C-6.3 say so, and
  QuickFIX C++ itself acts at its next one-second `Session::next()` tick. A one-shot inbound wake was not added: it
  is a new mechanism for a Heartbeat whose delay disconnects nothing. The round-1 design kept
  round 0's uninterruptible grace sleep and re-read `last_inbound_steady_` only after it, so with H = 10 s and
  T = 12 s a frame at 13 s that does not answer TR1 left fixpp outbound-silent until about 24 s, though the next
  Heartbeat was due at 22 s, against FR-010's "with inbound silent", C-6.3 and QuickFIX C++, where that frame's
  `testRequest(0)` makes `needHeartbeat` true at 22 s.
- **The give-up is measured from the last inbound frame** (clarification D-c; F-13). The give-up instant is computed
  from the `last_inbound_steady_` read at the top of each iteration, and is eligible only while no inbound frame has
  arrived since the pending TestRequest. A frame after the TestRequest therefore defers the give-up and makes the
  Heartbeat and the next TestRequest eligible; `pending_test_req_id_` stays set, so the TestReqID(112) rule still
  applies to a later Heartbeat (D5), and a further T of silence sends a new TestRequest whose TestReqID becomes the
  pending one (as the loop assigns it today). This is QuickFIX C++'s observable (`Session::verify` ends every
  accepted frame with `lastReceivedTime` and `testRequest(0)`, and `timedOut` is the time since the last receipt)
  and QuickFIX/Go's (the peer timer reset on every frame); fixpp keeps the 112 comparison (spec Departures). Cells:
  application traffic after the TestRequest and no Heartbeat answer, the session Active at 2T after the
  TestRequest's instant and given up at 2T after the last frame; and N-2's cell (H = 10 s, T = 12 s, a frame at 13 s:
  a Heartbeat at 22 s, TR2 at 25 s, no Heartbeat at 35 s, the give-up just after 37 s); a frame at 23 s, after the
  Heartbeat instant, with the Heartbeat at the stale give-up wake just after 24 s; and a no-spin cell (a silent peer
  after TR1: one sleep registered on the mock clock per wake, a bounded number of loop iterations up to the give-up).
- **The `+1ns` guard** is a third saturating step:
  `deadline_after(give_up_instant, std::chrono::nanoseconds{1})`. `deadline_after`'s template accepts any signed
  whole multiple of the time point's tick, and `fixpp::core::steady_time_point` is `std::chrono::steady_clock`'s, so
  no new helper is needed; adding 1 ns to a saturated `steady_time_point::max()` by hand would overflow a signed
  representation, which round 0's "plus the guard" did. The guard exists so that a mock clock firing at exact
  equality does not run the give-up before a test can deliver its echo (`TR_DistinctNow`'s comment); it is R-3's
  "after 2T", within one tick. Cells: exactly 2T, the echo still wins; 2T + 1 ns, the give-up; a `constexpr` cell at
  the saturated value (`steady_time_point::max()` in, `max()` out).
- **No Heartbeat while the TestRequest waits on a silent peer.** With T = 1.2 × HeartBtInt the window from the
  TestRequest to the give-up is longer than HeartBtInt, so a Heartbeat due inside it is not sent while inbound stays
  silent, which is QuickFIX C++'s rule (`needHeartbeat` requires `!testRequest()`). FR-010 and contract C-6.3 state
  it, and a cell pins it with a positive control (H = 10 s, a silent peer: a Heartbeat at 10 s, the TestRequest at
  12 s, the session Active at 22 s with no Heartbeat, the give-up just after 24 s).
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
  `reply_ceiling_log_gate_`, so the type must stay visible to it: the struct moves to a new installed detail header
  (an implementation header with the status of `detail/replay_gate.hpp`, listed in contract C-9 and B-105-7; Gate A
  round 3, Codex #6), `include/fixpp/session/detail/log_rate_gate.hpp` (namespace `fixpp::session::detail`, the same fields, so the
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
  (once per entry). Each arm has a cell: a port in use reaches the `system_error` arm; no configuration reaches the
  generic arm (`asio_listener`'s constructor and `make_bind_endpoint` throw `std::system_error` for every bind,
  listen and address failure, and leave only an allocation failure), so a listener-construction seam in
  `run_accept_loop` (the allocation-failure seam family of D10) throws `std::bad_alloc` in place of the
  construction (Gate A loop 2 round 1, Codex #3). Both formats are registered in `src/log/format_registry.cpp`.
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
  internal. A new installed implementation header, `include/fixpp/session/detail/log_rate_gate.hpp` (D8), with
  `detail/replay_gate.hpp`'s status: the install rule ships `include/` whole, and 084's package-layout contract
  ships a `session/detail/` header a public header reaches. No `SessionConfig` field changes. BREAKING C++ layout, recorded in B&L as 103's B-103-9 and 104's
  B-104-7 record theirs.

**Alternative rejected.** *No bump, on "the C ABI shape is unchanged":* X §7 is about results, not shapes.

## D10 — Test plan

- **One ctest entry `session_105_tests`** (label `105`) over new files in `tests/session/`:
  `test_105_connection_close.cpp` (D1, D4; per root and per refusal), `test_105_logout_wait.cpp` (D2),
  `test_105_liveness_timing.cpp` (D3, D6), `test_105_heartbeat_knob_off.cpp` (D5), `test_105_presession_log.cpp`
  (D8), `test_105_tls_close.cpp` (FR-002 on `loopback_tls_session_harness.hpp`), and `test_105_support.hpp`.
  Config cells extend `tests/config/test_load_negative_battery.cpp`; C-ABI cells extend
  `tests/capi/recovery_liveness_capi_witness_test.cpp`. `ReconnectFsm` removals are proven by the build.
- **RED first on the base, or a guard with its mutant** (FR-017; Gate A round 2, N-3). A behaviour cell is written,
  run on the branch's merge-base with `origin/main` and shown RED for its stated reason before the fix. A regression guard (a rule the base already
  keeps: B-093-4's establishment-deadline close, B-537-3's knob-on rule, HeartBtInt 0, the closes `close()` and the
  drain own) is GREEN on the base by design; its RED is its named mutant. Quickstart section 3 marks every cell one
  or the other. A cell whose oracle the base meets for another reason (the session already `Disconnected`) carries a
  positive control at the instant it tests: `fixpp_session_is_established` or `fsm_state() == Active` true, and for
  a timing cell the TestRequest on the wire.
- **The close observable is the peer's.** Engine cells use `engine_loopback_harness.hpp` / `plain_engine_rig.hpp`
  and assert the peer's read ends (EOF, or the TLS close_notify on the TLS arm), never a session flag.
- **Bounds from the competing timeout.** Every competing timeout (the establishment deadline, the liveness
  instants, `logout_disconnect_timeout_ms`) runs on the mock clock. A cell for an "at once" class (SC-001) holds
  the mock clock still and pumps the executor until the peer's read ends or a wall cap passes. Five timers run on
  wall time, not on the mock clock, and can also close a connection: the TLS handshake and first-frame deadlines,
  `close()`'s grace timer (`close_grace`) and `drain_for_reconnect_`'s bound, each an `asio::steady_timer`, and
  `asio_tls_transport::close_async`'s `close_deadline` (`tls_close_timeout`, after which it closes abortively). A cell
  sets each one it can reach well above its wall cap (or does not reach it), so with the clock frozen a hit is the
  mechanism's. The spurious-hit arm: the same cell
  with the latch's consumer deleted (a mutant) must go RED, and a cell with a short `logon_timeout_ms` asserts the
  EOF arrives before the deadline and with no `session_event_establishment_timeout`.
- **R-2 cells, one or more per row of contract C-2 "While the wait is armed"** (W-1 to W-15), each with the mutant
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
  - W-9, one cell per disposition of an internal failure during a wait armed after Active (Gate A round 2, root
    cause #1), each asserting peer EOF with the mock clock frozen, the bound joined, `onLogout` once and no second
    Logout: (1) an arm returns an error with `Disconnected` written (`StoreFailureDuringWait_EofAtOnce`); (2) an arm
    returns an error without it (a persist failure of an in-sequence application message); (3) an arm returns success
    with `Disconnected` written (`TooHighDuringWait_ResendRequestAssignFails_EofAtOnce`, `inbound_resend_request_leaf_`'s
    failed assignment); (4) a failure in a non-pump root (`SendHeldInStoreAcrossTheEnding_StoreFails_EofAtOnce`, the
    W-10 numbering setup with a failing store). Plus the direct-Session form of (1), closing at the next `send()`.
    Mutant: the round-1 predicate (the owe refused whenever `armed`).
  - W-10: `send()` refused, with the session shown Active (`fixpp_session_is_established` true) at the same instant,
    since on the base the session is `Disconnected` and refuses the send with the same error. Numbering (C-2.3): a send held in its store call, the ending triggered, the store
    released; the send's number is below the Logout's (the wire order is not asserted; L-105-e).
  - W-11: the wait armed one tick before the Heartbeat instant, and one tick before the give-up instant; nothing
    originated, with the session shown Active at the instant tested (on the base the loop leaves at its state test
    because the ending wrote `Disconnected`). Mutant: one post-suspension test reverted to `fsm_state_ != Active`.
  - W-12: EOF at exactly the bound, not before (advance to bound − 1 ms, assert open; advance 1 ms, assert EOF); the
    bound does not slide (a Heartbeat answering the peer's TestRequest mid-wait).
  - W-13: `close(graceful)` during the wait, on TLS, asserting close_notify and the close at the bound's instant on
    the mock clock. `close()`'s grace is the same knob (`close_grace` expires after
    `logout_disconnect_timeout_ms`), so the two are separated only through the clock split: the knob set long (well
    above the wall cap) and the mock clock advanced to the bound well inside that wall time; a cell that sees only
    EOF would pass through the grace's abortive close. Mutant: the bound's live predicate tests
    `closing_or_stopped_()`.
  - W-14: `close(terminal)`, `Engine::stop()` and the drain during the wait: one close each.
  - The state machine (data-model E-3): before Active, the bound survives the refusal's own `Disconnected` write
    (mutant: the bound exits on any wake; mutant: the arming after that write); the bound's pre-Active expiry closes
    (the second setter); timeout 0 and no clock arm nothing and close after the write. "A second write of
    `Disconnected` owes nothing" has no peer-visible observable (data-model E-2 I-5) and is a census row, not a cell.
  - The pending too-high mark (N-5; D2 item 1): a too-high Logout while a resend is outstanding, then a non-advancing
    frame after the gap deadline; EOF after the deadline's Logout with the clock frozen. Mutant: the claim's result
    ignored (the wait armed).
  - No application (N-4): one class-P cell with `EngineConfig::application` null; EOF at once. Mutant: the edge's
    owe moved below the no-application return.
  - Spawn failures (round 2 Codex #1; round 3 root cause #1): an allocation-failure seam at each new spawn, each cell
    asserting the peer's EOF with no later inbound frame and no API call, not only balanced counters. The close
    frame's spawn failing, at `release_connection_end_` (an Engine session ended by the liveness loop while its pump
    is parked in a read), at the write limit (E-3 row 4) and at the bound's expiry (rows 10, 11): EOF by the
    synchronous fallback, the counter 0 after `close()`; on TLS no close_notify (L-105-h). The bound's spawn
    failing, after Active, before Active, for R-1 and for a reply: no Logout bytes at the peer, `Disconnected` and
    `onLogout` once, EOF at once. Mutants: the fallback deleted (the latch left set, round 2's rule: EOF never comes);
    the bound's failure read as timeout 0 (a Logout written: the wire oracle).
  - The bound's failure exit (Gate A loop 2 round 1, root cause #1; data-model E-3 row 16), one cell per cause and
    phase. The `catch (...)`: `LogoutWait105.EndingBound_RaceThrows_<Writing|Armed>_EofAtOnce`, an in-body seam of
    the spawn seams' family throwing `std::bad_alloc` where the bound builds its race, before it awaits the `||`
    expression (not inside an arm: a throwing arm cancels nothing, `await_deadline`'s note says, so on a frozen clock
    the exception would never reach the `catch (...)`). `Writing`: the Logout's write held
    behind a peer that does not read, EOF at once with the clock frozen, no wait after it, `onLogout` once, plus a
    variant whose store call is still held when EOF is asserted; `Armed`: after Active and before Active, EOF at
    once. The absorbed exception: `LogoutWait105.EndingBound_DeadlineArmAbsorbsException_<Writing|Armed>_EofAtOnce`,
    the counting `Clock` decorator below set as `clock_override`, throwing `std::bad_alloc` from `sleep_until` for
    the bound's instant only, so the liveness loop's sleeps are untouched; `detail::await_deadline` absorbs it and
    completes the arm early. Each cell drives the executor by a bounded number of handler runs (`run_one` × N, or
    `run_for` under a wall cap), so its RED arm terminates. Mutants: the failure exit routed through
    `release_connection_end_` (the `Writing` cells: no EOF while the write is held); the early completion read as
    a re-test (the decorator's count of the bound's sleeps exceeds one per wake within the bounded drive).
- **The write limit** (ruling D-e; D2 item 11), on a transport whose write can be held until the transport closes
  (the scripted transport's `write_hold`, which `test_103_resend_state_reconnect.cpp` sets, or a seam in
  `test_105_support.hpp`), and the TLS arm on a peer that stops reading (a zero window). RED on the base: the held
  write never returns and nothing closes. Two shapes: the Logout is itself the blocked write, for R-1, an R-2 Logout after
  Active, an R-2 Logout before Active and a reply; and the Logout is queued at `write_gate_` behind a blocked
  application send, for R-1, an R-2 Logout after Active and a reply (`Reply_LogoutQueuedBehindBlockedSend_*`: an
  application send blocked in `async_write` holding `write_gate_`, then the peer's Logout; the receipt arm resumes,
  `onLogout` fires once and the reset unit runs as 104 orders it; Gate A round 3, Codex #5). The queued shape before
  Active is not reachable on an Engine session: the engine initiator's Logon write is awaited inside
  `drive_reconnect` (`run_connect_loop` Step 3) before `run_read_pump` starts (Step 5), and the acceptor writes
  nothing before its refusal. It exists only for a direct initiator whose own reader runs before `open()` or
  `drive_reconnect` returns, and is covered by the shared write point, so it has no cell. Each asserts: open at the bound − 1 ms, no complete Logout at the peer, EOF at the bound
  (plain), close at the bound plus `tls_close_timeout` with no close_notify (TLS), `onLogout` once, no wait after it
  (the peer's Logout sent after the bound changes nothing), and for R-1 the slot-74 record present. A control cell:
  the write released at the bound − 1 ms, the Logout complete on the wire, then the R-2 wait's remaining budget, EOF
  at the bound; the discriminating oracle is the wire (the Logout bytes and the close_notify present or absent), not
  the EOF instant. The spurious-hit arm: delete the limit (mutant) and assert the connection still open past the
  bound, which also shows that nothing else closes it (the liveness loop originates nothing during the write; before
  Active `logon_timeout_ms` is set well above the bound, so neither the pump's establishment test nor the ending
  machine's establishment cap fires first; `close_grace` and the drain's bound are not reached). Timeout 0: the write unbounded (the held
  write keeps the connection open past any instant, a guard with the mutant "0 read as close at once").
  - The establishment cap (Gate A round 3, root cause #2; Codex #3), on an Engine session with establishment time
    left below `logout_disconnect_timeout_ms`: a refusal Logout blocked in its write, and one held in its store call,
    each closed at the establishment deadline (open at deadline − 1 ms) with no `session_event_establishment_timeout`
    and no establishment-timeout record. The pre-Active wait still armed at that deadline is not a cap cell: no arm
    is in flight then, so the pump is in its read race and its own `expire()` closes at the same instant, and the
    cell cannot attribute its close to the cap. It is E-5's cell instead
    (`LogoutWait105.PreActive_WaitArmedAtEstablishmentDeadline_ClosedNoEvent`, mutant: `expire()` without its
    `ended` test; Gate A loop 2 round 1, N-3). A cell with `SessionConfig::clock_override` set to a second mock clock advances only the engine clock to
    the deadline, so the cap's arm on `engine_.clock` is the one that fires. Mutant: the cap's arm deleted (the
    close then comes at the write limit).
  - The bound's expiry while an arm is blocked (N-1): after Active on the SendingTime-Reject route, the peer's
    ResendRequest during the wait, its answer blocked in `async_write` behind a peer that stopped reading; open at
    the bound − 1 ms, EOF at the bound (`LogoutWait105.Bound_ExpiresWhileResendAnswerBlocked_EofAtTheBound`).
    Mutant: rows 10 and 11 routed through `release_connection_end_`'s `inbound_depth_` test.
  - The write completing after the bound has fired (Gate A loop 2 round 1; data-model E-3, `ending_wait_.cut`): on a
    direct Session, an R-2 Logout's write held, the clock advanced to the bound, the write released before the
    executor is drained so its completion runs ahead of the close frame's `close_async()`; no wait armed,
    `Disconnected` and `onLogout` once (`WriteLimit105.DirectSession_WriteCompletesAfterTheBoundFires_NoWaitArmed`).
    A guard (the base arms no wait); its RED is the mutant "Leaving `writing`" ignores `cut`.
  - `close(graceful)` during the write (N-3): the mock clock advanced to the bound well inside `close_grace`'s wall
    time, EOF at the bound, the close frame's counter 0 when `close()` returns. Mutant: the second join deleted
    (the Debug assert of `~Session`, or the counter read after `close()`).
- **Timing cells that discriminate.** H = 10 s on the mock clock: no TestRequest at 11 s (the base sends at 10 s),
  one at 12 s; Active at 23.9 s (the base ends near 20 s), ended at 24 s plus the guard (cell name
  `…EndedAt24sPlusGuard`). A set T (7 s): TestRequest at 7 s, give-up at 14 s plus the guard. Both roles. The guard
  cells of D6 (exactly 2T, 2T + 1 ns, the saturated `constexpr` cell); D-c's cell (traffic after the TestRequest,
  no Heartbeat); the late answer to TR1 (traffic after TR1, then T of silence draws TR2, then a Heartbeat with TR1's 112: the session
  stays Active and TR2 stays pending, ruling D-d; the give-up then follows 2T after that frame); N-2's cell (a Heartbeat
  at 22 s, TR2 at 25 s, none at 35 s, the give-up just after 37 s); the skipped Heartbeat of a silent peer, with the
  session shown Active and the TestRequest at 12 s as its positive control; the two precedence cells. The no-spin
  cell (`SilentAfterTestRequest_NoSpin`; Gate A loop 2 round 1, N-2): `mock_clock` exposes no count of its sleeps,
  so `test_105_support.hpp` provides a counting `Clock` decorator over `mock_clock` (it counts `sleep_until`
  calls and forwards each to the mock), set as `clock_override`. Under the mutant every `sleep_until` meets a
  passed deadline and `mock_clock` completes it at once, so a run-until-idle drive (`poll`, `run`) would never
  return; the cell drives a bounded number of handler runs per mock-clock step and asserts the count, so its RED
  arm terminates as a failed assertion, not as a hung binary.
- **Lifetime cells** (ASan): a Session destroyed after `close()` has returned, for each new frame (the R-2 bound
  and the close frame), including a close frame spawned by the write limit during `close()`'s wait for the bound.
  The precondition is B-103-13's (`close()` before the Session is destroyed); `~Session` asserts both counters are
  zero, and no cell destroys a Session with a frame running.
- **Both roles** for every engine cell, by `INSTANTIATE_TEST_SUITE_P` over `session_role`, as 537's cells do.
- **The cell recipe for moved timings:** `grep -rlnE 'TestRequest|35=1' tests/session tests/capi tests/conformance tests/interop`,
  read for a mock-clock advance across HeartBtInt; the whole `session` and `capi` suites are run, since a missed
  one fails there.
- **Lanes.** linux-clang-debug, -asan, -ubsan, -tsan (the latch, the spawned close and the bound are strand
  confined; TSan proves it), gcc-release of the touched targets, and the MSVC build of the feature's targets before
  the first labelled push (the project's rule for a feature's own targets). The TLS arm runs on every lane that runs
  `loopback_tls_session_harness.hpp` today.
- **Benchmarks (Article VIII §2, §3; ruling D-f).** The per-frame cost is two tests, not one: the pump's latch test
  (`run_read_pump`) and the ending-phase test in `on_inbound_active_` (the response test of D2 item 3, made on every
  frame); per write, `store_then_emit`'s transmitted out-parameter; and members and frame state in the Active arms and
  the liveness loop. Round 1's "only the pump's latch test is new" was false (Gate A round 2, Codex #2). 104's verify
  record (step 8) is the reason the runs matter: a buffer held across a `co_await` cost +22 % on
  `session_send_bench` through asio's frame recycler. Owed, paired base-vs-candidate A-B-A-B: `on_inbound_frame_bench`
  (the per-frame test and the arms' frames), `session_send_bench` (`send_body_`, `store_then_emit`) and
  `engine_send_bench`, plus **a new pump case in `engine_send_bench`'s harness**, which already holds a published
  Active session whose read pump waits on a scripted read: the case feeds inbound Heartbeat and application frames
  through `run_read_pump`, so the latch test is measured on the unarmed path. In `bench/ci-suite.txt`,
  `engine_send_bench` is a `paired` entry, `on_inbound_frame_bench` is listed unpaired (execution-checked, no timing
  limit) and `session_send_bench` is not listed, so the runs for those two are local and recorded in the verify
  evidence; the new case is part of the `engine_send_bench` binary, so its paired entry covers it. No "none drives the pump"
  statement is admissible (Article VIII §3: no perf change without a benchmark in the same PR).
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
  asserts the too-high Logout bound's counter is 0 …)"), which gain the R-2 bound and the close frame beside the too-high Logout's bound: `close()` joins all three, and
  `~Session` asserts each counter is zero (Gate A round 3, Codex #4: no close-frame exemption).
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
    driver's close-before-destroy precondition: run `close()` or `drive_reconnect` and let the R-2 bound,
    `run_ending_logout_bound_`, exit before destroying the Session, as for the too-high Logout's bound; `close()` also joins the close frame, so the precondition covers it with no
    exemption; `Lifetime105.*` are its cells).
  - *New rows:* `B-105-1` the close rule by ending class (FR-001 to FR-008); `B-105-2` R-2's wait and its
    observables, and the write limit on every ending Logout (ruling D-e: one budget from the write's start; a cut
    write closes the connection and counts as not sent; disclosed: the budget starts at the owner claim, so it covers
    the Logout's number assignment and store call, a persistent store slower than the budget makes the Logout "not
    sent", and a write that completes late leaves the peer only the rest of the budget to answer; the bound closes the
    connection at its expiry, and at its own failure, whatever frame is being processed; before Active on an Engine session the establishment
    deadline caps it); `B-105-3` R-1 and its departure from QuickFIX (the record
    written before the write); `B-105-4` the #582 rule; `B-105-5` the TestRequest timing, the threshold's
    validation and the TestReqID(112) rule of ruling D-d (an earlier 112 of this connection neither answers nor ends;
    a 112 never sent ends; the set restarts at each connection; after 2³² TestRequests on one connection the window
    no longer identifies the sent set; a Heartbeat due after a frame that follows the TestRequest goes at its instant
    or at the loop's next wake, at most max(0, T − H) late); `B-105-6` the pre-session and listener records; `B-105-7` BREAKING C-ABI 1.18,
    the C++ layout changes, and the new installed implementation header `include/fixpp/session/detail/log_rate_gate.hpp`
    (the status of `detail/replay_gate.hpp`); an `L-105-*` row for each disclosure of the contract's "Disclosed" section (L-105-b to
    L-105-h; L-105-a was withdrawn in Gate A round 1 and is not used). L-105-f is written as narrowed by ruling D-e
    (non-ending writes stay unbounded, including a liveness emit queued behind a blocked send, which never reaches
    R-1), names the follow-up fixpp issue for bounding every write (filed before Gate B, labelled from
    `gh label list`), and names the round-1 text it supersedes. L-105-g is the TLS abortive fallback of a cut write. L-105-h is the synchronous close when a close frame cannot be
    spawned (clarification D-g). L-105-b carries no allocation-failure clause.
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
- **R2. A latch left set with no consumer** (a direct Session whose application never calls anything again; L-105-b).
  A close-frame spawn that throws no longer leaves it: its `catch` closes the transport synchronously and clears the
  latch (D1 item 4, clarification D-g); the census table's "every root" column is the control, and each spawn has
  an allocation-failure cell (D10).
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

- **OQ-3 (scope; round-1 finding N-5): closed by ruling D-e (owner, 2026-11-20).** Every Logout fixpp writes to end a
  session is time-limited in 105 (D2 item 11, contract C-2.7); L-105-f narrows to non-ending writes and gets a
  follow-up issue. The round-1 interim decision (disclose the blocked-write ending with a follow-up) is superseded by
  D-e, and with it round 2's Codex #5 fix shape (FR-003 and FR-004 "except a write that never completes"): the
  exemption is no longer needed, because FR-003's "if the Logout cannot be sent, the session MUST still close" now
  holds by the limit.
