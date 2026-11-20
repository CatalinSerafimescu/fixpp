# Data model: 105 transport and liveness

**Feature**: [spec.md](spec.md) · **Decisions**: [research.md](research.md) · **Contract**:
[contracts/transport-liveness-105.md](contracts/transport-liveness-105.md)

Phase 1. State and entities this feature adds, changes or removes. Names are proposals for the implementer; the
observables are fixed by the contract, the names are not. No member is described by its size or offset: re-derive
the layout from the header.

## E-1. Ending class

A `Disconnected` write is classed by the **root** it runs under (research D1, "The census, by execution
context"). The class fixes who closes the connection and when.

| Class | Roots | Who closes | When |
|---|---|---|---|
| **P — pump frame** | `run_read_pump` (every frame, the acceptor's first frame included) | the pump: `stop_pump` → `close(terminal)` | after the frame's arm returns: after its reply, persist and receipt reset unit |
| **D — detached root** | `run_liveness_loop`, `run_peer_logout_bound_`, `run_ending_logout_bound_` (E-3) | `close_transport_`, spawned at the root's exit; on an Engine session the pump then runs `close(terminal)` | at the root's exit when no frame is inside an arm; otherwise the pump, after that arm |
| **A — application call** | `Session::send` and `Engine::send`'s session body (`send_body_`, `send_deferred_`), `Session::drive_reconnect`, `Session::open` (a direct initiator's Logon) | as class D | as class D |
| **W — ending Logout** | any of the above, where the ending writes a Logout (R-1, every R-2 Logout, the replies to the peer's Logout; E-3) | as E-3's "Who closes" column: the pump after the response, the bound's own release at its expiry, the bound's write-limit close (E-3 row 4), the closer for `close()`, `Engine::stop()` and the drain | at the peer's Logout, at the bound, or at the write limit |
| **X — excluded** | `Session::close` and `run_logout_phase1` under it; `drain_for_reconnect_` under `drive_reconnect` | the close or drain that owns it, as today | as today (B-103-9's bounded waits kept) |

A write reached from several roots takes the class of the root it runs under at that moment. The census table in
tasks.md lists each write site with every root that reaches it.

## E-2. The connection-end latch (`Session`, private)

| Field | Type | Written by | Read by |
|---|---|---|---|
| `reconnect_drain_in_flight_` | `bool` | `drive_reconnect`, around its `drain_for_reconnect_` call only | `owe_connection_end_()`, the bound's live predicate (E-3) |
| `connection_end_owed_` | `std::optional<std::uint32_t>`: the `conn_gen_` the close is owed on | set: `owe_connection_end_()` only; cleared: `take_connection_end` (pump), `release_connection_end_` after a successful spawn, `close()` at its commit of `closing`, `drain_for_reconnect_` | the pump after each frame; each class D and A root at its exit |
| `connection_close_counter_` | `std::shared_ptr<std::atomic<int>>` | the close frame's join parameter (+1 at its construction, −1 at its destruction) | `close()` and `drain_for_reconnect_` (join by post); `~Session` (assert 0) |

**The one setter**, `owe_connection_end_() noexcept`: sets `connection_end_owed_ = conn_gen_` when
`state_ == lifecycle::open && !reconnect_drain_in_flight_` and **no wait armed before Active is open**
(`!(ending_wait_.phase == armed && !ending_wait_.from_active)`), else does nothing. A wait armed after Active does
not refuse it: the call ends that wait (`phase = resolved`) and owes (Gate A round 2, root cause #1; E-3). Its
callers (a census row and a mutant each):
1. `record_state_transition_`, on a transition **into** `Disconnected` from another state only, using the prior
   state the function captures at its entry. The call sits in the existing `new_state == fsm_state::Disconnected`
   block beside `wake_peer_logout_bound_()`, **above** the `engine_.application == nullptr` return, so a session
   with no application owes its close too (093 placed `reached_active_` above that return for the same reason). A
   write of `Disconnected` over `Disconnected` owes nothing.
2. The bound at its expiry for a wait armed before Active (the session is `Disconnected` already; E-3 row 11).
3. The pre-Active response, the peer's Logout during a wait armed before Active (E-3 row 9).

**The close frame.** `release_connection_end_() noexcept` decides synchronously, on the strand: the latch is set,
its generation is `conn_gen_`, `state_ == lifecycle::open`, `inbound_depth_ == 0`, and no ending Logout is being
written (`ending_wait_.phase != writing`; E-3, "Other roots during `writing`"). It then constructs the
frame's join (`connection_close_join`, the shape of `peer_logout_bound_join`: it increments the counter at
construction and decrements it at destruction, so a frame that is never published releases it), takes
`live_transport_shared_()`, and spawns the static coroutine `close_transport_(join, std::shared_ptr<Transport>)` on
`exec_.underlying()` inside a `try`. The latch is cleared **only after `co_spawn` returns**; a spawn that throws
is caught there (the function is `noexcept`), leaves the latch set and the counter balanced, and the next consumer,
`close()` or `Engine::stop()`, closes the connection (L-105-b). The precedent is
`on_inbound_active_too_high_logout_`'s spawn (join as the parameter, a `catch` that restores the state), not the
liveness loop's spawn sites, whose `fetch_add` before a bare `co_spawn` leaks the count on a throw. The frame awaits
`close_async()` on the transport it holds and touches no member of `Session`, so it may outlive the Session.

The same frame is spawned, with the same join, by the bound's write-limit expiry (E-3 row 4). That spawn tests only
the generation and a live transport: it runs while a frame is inside an arm, because the arm is the one suspended in
the write (I-2's exception).

**Invariants.**
- I-1. At most one consumer acts per set: every consumer tests and clears on the session strand.
- I-2. A class D or A consumer acts only when `inbound_depth_ == 0`, tested at the spawn, not when the frame runs.
  That is enough because a frame that enters after the edge finds the session `Disconnected` with no wait armed and
  is drained by the `Disconnected` row, with no store work. **One exception, the write limit** (E-3 row 4): it
  closes the transport while an arm is suspended in an ending Logout's write or behind it at `write_gate_`. No
  store work is cut by it: a store call completes before its frame's transmit (durable-before-transmit, `store_then_emit`'s I-3),
  and the receipt reset unit is store-only; the transport close only makes the pending writes complete with an
  error.
- I-3. The close frame's only effect is `close_async()` on the transport taken at the spawn; it never reads `this`.
- I-4. `close()` never runs its own `close_async()` while a close frame is in flight (the join).
- I-5. **One effective close per connection.** The latch is owed at most once per connection: the edge owes only on
  a transition into `Disconnected` (once per connection), and the wait's two direct callers fire only for a wait
  armed before Active, where no such transition follows. A later `close_async()` on the same transport (the pump's
  `close(terminal)` after a write-limit close, `Engine::stop()`'s step 2) is subsumed, as F-5 describes. This is
  a census row, not a cell: every `Disconnected`-over-`Disconnected` write runs in the synchronous chain of the
  first transition, before any consumer, so no peer-visible observable distinguishes it (Gate A round 2, N-3).

**Session-engine access.** `session_engine_access::take_connection_end(Session&) noexcept -> bool` (test and clear;
it returns false and leaves the latch set while an ending Logout is being written), for the pump; `session_engine_access::ended(Session const&) noexcept -> bool` (`state() == Disconnected`), for the
pump's `expire()` (E-5).

## E-3. The ending Logout: one state machine for the write limit, the R-2 wait, the bound and the latch

Gate A round 2 (root causes #1 and #2 of the round-2 review; ruling D-e). Every Logout fixpp writes to end a session
goes through this machine: R-1's (`run_liveness_loop`), every R-2 Logout (`send_ending_logout_`, before and after
Active), and the replies to the peer's Logout (`on_inbound_active_logout_`, `acknowledge_peer_logout_`). The entry
point is the write point each Logout leaf already has: `inbound_logout_leaf_` after Active, at the place where it
claims the Logout owner `logout_in_flight_` (before its first suspension; a leaf that finds the owner set writes
nothing and enters nothing here), and before Active the build-and-emit of `refuse_logon_with_logout_` and
`inbound_logon_sent_logout_leaf_`. `close()`'s own Logout (`run_logout_phase1`) is class X and keeps
`close_grace` (B-348-1).

| Field | Type | Meaning |
|---|---|---|
| `ending_wait_.phase` | enum `{ none, writing, armed, resolved }` (one byte) | `writing`: an ending Logout's write is in flight and limited; `armed`: the R-2 wait |
| `ending_wait_.kind` | enum `{ r1, r2, reply }` (one byte) | which ending entered `writing`; only `r2` can reach `armed` |
| `ending_wait_.entered_from` | `fsm_state` (one byte) | the FSM state when the write started; leaving `writing` arms a wait only if the session is still in it |
| `ending_wait_.from_active` | `bool` | entered after Active (the session stays Active through an `r2` wait) or before it (the refusal records `Disconnected`) |
| `ending_wait_.deadline` | `fixpp::core::steady_time_point` | `deadline_after(now, logout_disconnect_timeout_ms)` on `effective_clock_`, read **once, at the start of the write** (before the gate acquire); it bounds the write and then the wait: one budget, as `close_async`'s one `close_deadline` bounds its whole call |
| `ending_logout_wake_` | the shape of `peer_logout_wake_` | published by the bound while it waits |
| `ending_logout_bound_counter_` | `std::shared_ptr<std::atomic<int>>` | held by the bound's join parameter (`ending_logout_bound_join`, the shape of `peer_logout_bound_join`); joined by `close()` and `drain_for_reconnect_` after their transport close; asserted 0 by `~Session` |

**Entering `writing`** (all three kinds): only when a clock is present and `logout_disconnect_timeout_ms > 0`; then
`phase = writing`, the deadline is read, and the bound `run_ending_logout_bound_(join, gen)` is spawned **before the
write**, in `on_inbound_active_too_high_logout_`'s shape: the join is the coroutine's parameter, and a `catch (...)`
around `co_spawn` sets `phase = none` and does not rethrow, so the ending proceeds exactly as with timeout 0 (row 2).
With timeout 0 or no clock nothing is entered: the write is unbounded, as before 105, and no wait follows (C-2 "No
wait is armed", C-11 L-105-f). A 0 ms limit is not used as "close at once", unlike `close()`'s `close_grace`,
because it would cut every R-1 Logout.

**Leaving `writing`**, when the leaf returns (on the strand, with no suspension before the next step). A wait is armed
only here, so only when the bound exists:
- `r2`, the Logout transmitted (D2 item 1's out-parameter), no pending too-high mark claimed by this ending (N-5;
  D2 item 1), and `fsm_state_ == entered_from` (no other root ended the session during the write): `phase = armed`. After Active the session stays Active; before Active the refusal's own
  `Disconnected` write follows (row 6).
- every other case (`r1`, `reply`, an `r2` that was not transmitted, cut by the write limit, that claimed a pending
  mark, or whose session another root ended during the write): `phase = none`, the bound is woken, and the site
  records `Disconnected` as it does today (over `Disconnected`, it owes nothing), so the edge owes the close (E-2).

**Other roots during `writing`: the latch defers to the writer.** While `phase == writing`, the two latch consumers
leave the latch set: the pump's after-frame test (`take_connection_end` is not called) and `release_connection_end_`
(it returns without spawning). So a `Disconnected` another root writes during the write (the peer's in-sequence
Logout answered while R-1's Logout is in flight, whose leaf writes nothing because the owner is set; an arm's internal
failure during the too-high bound's acknowledgement) owes the close but does not cut the Logout. The writer's root
consumes it when the write has returned: every root that writes an ending Logout is a consumer (the pump for a frame
arm, the liveness loop for R-1, `run_peer_logout_bound_` for its acknowledgement, E-1). The deferral is bounded by the
write limit. With timeout 0 or no clock nothing enters `writing`, so nothing defers, as on `main`. **The pump's error
close does not defer**: an arm that returns an error runs `stop_pump` → `close(terminal)` at once, as on `main`, and
that close can cut a Logout in flight from another root (disclosed in L-105-f's row).

**The bound's loop.** It publishes its wake, then loops: test the live predicate; if live and the clock is before
the deadline, race the wake against `detail::await_deadline(*effective_clock_, deadline)`; on either completion
test again. A wake is a re-test, never an exit by itself. The live predicate, by phase:
- `writing`: `phase == writing` and `conn_gen_ == gen` (asserted). Nothing else ends the limit early: `close()`,
  `Engine::stop()` and the drain each close the transport themselves (`close_async`, stop's step 2, the drain's
  forced close), which returns the write and so leaves `writing`.
- `armed` after Active: `phase == armed`, `conn_gen_ == gen`, `!engine_stop_requested_`, `!reconnect_drain_in_flight_`,
  `fsm_state_ == Active`. A graceful `close()` leaves the session Active while it poll-joins the Logout owner, so the
  bound runs on (rows 13, 14). Unlike `run_peer_logout_bound_`'s `live()` it does not test `closing_or_stopped_()`.
- `armed` before Active: `phase == armed`, `conn_gen_ == gen`, `state_ == lifecycle::open`,
  `!reconnect_drain_in_flight_`. Any `close()` there is terminal in effect, since `close()`'s Logout phase runs only
  from Active or LogonReceived. The refusal's own `Disconnected` edge wakes the bound and the predicate holds, so it
  waits on (row 6).

Every exit of the bound clears its wake and calls `release_connection_end_()`; an exit that finds `phase == armed`
sets `resolved` first. Its `catch (...)` sets `phase = resolved` (or `none` from `writing`) before it records
`Disconnected` over a live session, so that edge owes (row 16).

**The bound's wake sources:** the `Disconnected` edge; the leaf's return from `writing`; `close()` at its first
commit of `closing` (beside its `claim_peer_logout_none_()`); `drain_for_reconnect_` at its entry; the response
(rows 8, 9).

**Transitions.** "Owes" means `owe_connection_end_()` sets the latch (E-2); "no owe" means its predicate refuses.

| # | FSM, lifecycle, phase | Event | Phase after | Latch | Bound | Who closes |
|---|---|---|---|---|---|---|
| 1 | any, open, `none` | an ending Logout's write starts, clock present, timeout > 0 | `writing` (kind, `from_active`, deadline) | none | spawned before the write | row 3, 4 or 5 |
| 2 | any, open, `none` | an ending Logout's write starts with timeout 0, no clock, or a failed spawn | `none` | the site's `Disconnected` after the write: owes | none | the root's consumer after the write (E-1); the write is unbounded (L-105-f) |
| 3 | any, any, `writing` | the write returns before the deadline | `armed` (an `r2` transmitted, no mark claimed) or `none` (every other case) | `none`: the site's `Disconnected` owes; `armed` after Active: none; `armed` before Active: row 6 | `armed`: runs on; `none`: woken, exits | `armed`: rows 8 to 15; `none`: the root's consumer (R-1 at the loop's exit, a reply after its reset unit) |
| 4 | any, any, `writing` | the deadline passes, the write not returned (the Logout blocked in `async_write` or queued at `write_gate_`) | `writing` until the leaf returns, then `none` (row 3, "cut by the write limit") | the site's `Disconnected` after the leaf returns: owes; its consumer finds the transport closed | spawns the close frame (E-2) on the transport, then exits | the bound's close frame: `close_async()`; on TLS a close_notify that cannot be written falls back to the abortive close after `tls_close_timeout` (C-8.5, L-105-g) |
| 5 | any, any, `writing` | `close()` (either mode), `Engine::stop()`, the drain | `writing` until the leaf returns | as row 3 or 4, whichever ends first | runs on until the leaf returns | the closer's own transport close returns the write; the closer joins the bound |
| 5a | any, open, `writing` | another root writes `Disconnected` (the peer's Logout answered while the owner is set; an internal failure) | `writing` until the leaf returns, then `none` (`fsm_state_` is no longer `entered_from`) | that transition owes; both consumers defer while `writing` | runs on until the leaf returns | the writer's root, after the write: the Logout is complete on the wire, then EOF |
| 5b | any, open, `writing` | an arm returns an error (the pump's `!r`) | — | — | — | the pump's `close(terminal)` at once, as on `main`; it can cut the Logout in flight |
| 6 | NotConnected or LogonSent, open, `armed` | the refusal's own `Disconnected` write | `armed` | no owe (a wait armed before Active) | woken; its predicate holds; waits on | rows 9, 11, 15 |
| 7 | Active, open, `armed` | a second ending from a peer frame (C-2 W-8): `send_ending_logout_` or `end_session_silently_` | `armed` | none (nothing recorded) | runs on | — |
| 8 | Active, open, `armed` | the peer's Logout (W-1) | `resolved`, at the response arm's entry | the arm's `Disconnected` owes | woken, exits | the pump, after the arm returns (reset unit first) |
| 9 | Disconnected, open, `armed` (before Active) | the peer's Logout (W-1) | `resolved` | `owe_connection_end_()` called directly | woken, exits | the pump, after the frame |
| 10 | Active, open, `armed` | the bound expires | `resolved`, then `Disconnected` | that transition owes | exits | the bound's own release |
| 11 | Disconnected, open, `armed` (before Active) | the bound expires | `resolved` | `owe_connection_end_()` called directly | exits | the bound's own release |
| 12 | Active, open, `armed` | **any other `Disconnected` write**, from any root (an arm's internal failure, W-9; a send admitted before the Logout whose store or emit fails; a liveness emit admitted before it that fails; the too-high Logout bound's handler) | `resolved`, by the edge's call | owes | woken, exits | the pump on the error or on the latch, whichever it tests first; on a non-pump root, its own release at its exit; on a direct Session, L-105-b |
| 13 | Active, `closing` (graceful), `armed` | the peer's Logout | `resolved` | no owe (`closing`) | woken, exits | `close()`, released from its poll-join |
| 14 | Active, `closing` (graceful), `armed` | the bound expires | `resolved`, then `Disconnected` | no owe | exits | `close()`, released from its poll-join |
| 15 | any, `closing` (terminal or escalated) or the stop flag or the drain, `armed` | `close(terminal)`, `Engine::stop()`, an escalation, `drive_reconnect`'s drain | `resolved` at the bound's exit | none (`closing` or the drain flag) | woken, exits; the closer joins it | the closer, as today |
| 16 | any, open, any | the bound's own `catch (...)` | `resolved` / `none` | its `Disconnected` owes | exits | its own release |
| 17 | any, any, `resolved` or `none` | the next connection (`install_reconnected_transport`, `attach_accepted_transport`) | `none` (asserted `resolved` or `none` there) | — | — | — |

**The order before Active** (rows 1, 3, 6): enter `writing` and spawn the bound; the Logout's write; `armed`; the
refusal's own `Disconnected` write. Arming after that write would let the edge owe the close and end the wait before
it began (a mutant). The bound is already running when the edge fires, which is safe because its wake is a re-test.

**The invariant that makes the latch total by construction.** The absorbed set is defined by **cause**, never by
return value: only a peer-frame condition W-8 names (through `end_session_silently_`) and a further ending Logout
(through `send_ending_logout_`) are absorbed, and only while an ending is in progress after Active
(`ending_in_progress_()` = `phase == writing` with `from_active`, or `phase == armed` with `from_active`). Every
other `Disconnected` write reaches the edge, and the edge, during a wait armed after Active, ends the wait and owes
(row 12). The one write the owe refuses is the refusal's own write during a wait armed before Active (row 6), which
is the write the suppression exists for. The armed test lives in `owe_connection_end_()` and in the two absorbing
helpers; `record_state_transition_`'s `Disconnected` write also serves `close()` and the drain, which the latch's
`lifecycle` and drain-flag terms exclude.

**During `armed`:** the rows of contract C-2 "While the wait is armed" (W-1 to W-15).

## E-4. Liveness timing (`run_liveness_loop`, locals) and the TestReqID rule

| Quantity | Value | Arithmetic |
|---|---|---|
| H | `cfg_.heartbeat_interval.value_or(kDefaultHeartBtInt)` (whole seconds) | as today |
| T | `cfg_.test_request_threshold` when set, else `default_test_request_threshold(H)` = H × 1200 ms | saturating at `milliseconds::max()` |
| `tr_sent_at` | the clock's reading when the pending TestRequest was emitted (a loop local) | — |
| "inbound since the TestRequest" | `last_inbound_steady_ > tr_sent_at` | — |
| Heartbeat instant | `last_outbound_steady_ + H`, **eligible** when no TestRequest is pending, or one is pending and an inbound frame arrived since it | as today |
| TestRequest instant | `deadline_after(last_inbound_steady_, T)`, eligible under the same condition | saturating |
| Give-up instant | `deadline_after(deadline_after(deadline_after(last_inbound_steady_, T), T), std::chrono::nanoseconds{1})`, **eligible** when a TestRequest is pending and no inbound frame arrived since it | three saturating steps, no product; the guard is the third (`deadline_after`'s template takes nanoseconds, since `steady_time_point` is `std::chrono::steady_clock`'s and its static_asserts require a whole multiple of its tick) |
| Gap deadline (unchanged, B-537-2) | `detail::resend_gap_deadline(started_at, H)` | as today |

**One sleep per iteration** (Gate A round 2, N-2; the round-1 design's separate grace sleep is gone). At its top the
loop re-reads `last_inbound_steady_` and `last_outbound_steady_` and sleeps until the earliest **eligible** instant.
On waking it acts on every eligible instant the clock has reached: the give-up (C-3), else the TestRequest (its
TestReqID becomes the pending one; `tr_sent_at` is the clock's reading), else the Heartbeat. So a frame that does not
answer the TestRequest makes the Heartbeat eligible again at once (QuickFIX C++: `Session::verify` runs
`testRequest(0)` on every accepted frame, and `needHeartbeat` is `(now - lastSentTime()) >= heartBtInt() &&
!testRequest()`), and a further T of silence sends the next TestRequest. With inbound silent since the TestRequest,
nothing but the give-up is eligible, so no Heartbeat is sent (FR-010, C-6.3).

**One predicate**, `liveness_may_originate_()` = `fsm_state_ == fsm_state::Active && ending_wait_.phase == none`:
the loop condition and every test after a suspension (after the sleep, after each emit). It is keyed on the
ending's state, not on `logout_in_flight_`, because `run_logout_phase1` sets that flag before it records
`LogoutSent`. The arming adds no wake: the predicate is what keeps the loop from originating, and `close()` and the
drain join the loop as today (research D2).

The loop is off when H ≤ 0 (owner ruling on SI-2, 2026-11-20).

**The TestReqID rule** (ruling D-d; research D5, F-15). One predicate, `classify_test_req_id_(id)`, used by both
comparison sites (the in-sequence Heartbeat arm of `on_inbound_active_` and `answer_pending_test_request_`, which the
too-high Heartbeat route and #582's knob-off route call), and only while a TestRequest is pending, as today:

| 112 on the Heartbeat | Outcome |
|---|---|
| absent, or equal to `pending_test_req_id_` | **answers**: the pending TestRequest is cleared |
| `TR` followed by the canonical decimal (no sign, no leading zero, fits `std::uint32_t`) of an n with `(n − floor − 1) mod 2³² < (next − floor) mod 2³²`, other than the pending one | **earlier**: a TestRequest fixpp sent on this connection; neither answers nor ends; the frame refreshes liveness as every accepted frame does |
| anything else | **mismatch**: ends the session silently, `session_testreqid_mismatch`, as today |

`next` is `next_test_request_id_` (the per-Session counter the loop increments for each TestReqID, never reset in
`src/`); `floor` is a new `std::uint32_t test_request_id_floor_`, set to `next_test_request_id_` at every connection
start (`open()`, `install_reconnected_transport`, `attach_accepted_transport`). Memory is those two integers. The
window `(floor, next]` is modular, so it is wrap-safe; once more than 2³² − 1 TestRequests have been sent on one
connection every canonical `TR<n>` reads as sent (disclosed in B-105-5). `answer_pending_test_request_` returns
false only for **mismatch**.

**Validation of a set T** (`Session::open`, `Engine::register_session`, the TOML mapper): `T > 0`. No other bound.

**Removed:** `Session::unanswered_tr_` and its writes.

## E-5. The pump's establishment test

`establishing()` is unchanged. `expire()` calls `note_establishment_timeout` only when `!ended(session)`, and
then `stop_pump()` either way: a session that has recorded its own ending (a refusal, or a pre-Active R-2 wait
still open at the deadline) is closed at the deadline with no event and no record.

`run_read_pump` gains `std::span<const std::byte> first_frame`, delivered before `initial_bytes` under the same
disposition (FR-006).

## E-6. Pre-session reject reason (`src/session/engine.cpp`, anonymous namespace)

`enum class pre_session_reject : std::uint8_t`, logged by value and by a fixed name:

| Value | Name in the record | `run_accept_loop` branch | CompIDs read | `error` |
|---|---|---|---|---|
| `not_tls_transport` | `not_tls` | the accepted transport is not a `TlsTransport` on a TLS profile | no | 0 |
| `tls_handshake_failed` | `tls_handshake` | `async_handshake` failed | no | the handshake's error |
| `no_establishment_time` | `no_time_left` | no establishment time left after the handshake | no | 0 |
| `first_frame_read_failed` | `first_frame` | `read_first_frame_bounded` failed (timeout, over budget, an over-limit frame, a read error) | no | the read's error |
| `first_frame_without_ids` | `no_ids` | `scan_first_frame_ids` found no BeginString or no CompIDs | what was read | 0 |
| `compid_mismatch` | `no_session` | the reversed SessionId does not match the entry | yes | 0 |

Re-derive the branch list before coding: every `(void)transport->close(); continue;` in `run_accept_loop` before
`open()` is one row; a branch with no row is a defect of this table.

## E-7. The pre-session log gate (hoisted rate gate)

| Entity | Where | Fields |
|---|---|---|
| `detail::log_rate_gate` | `include/fixpp/session/detail/log_rate_gate.hpp` (installed: `session.hpp` declares two gates) | `logged`, `next`, `unlogged` (Session's struct, moved; layout unchanged; `Session` keeps a member alias) |
| `detail::admit_log_record(gate, now, window, occurrences)` | same, inline | returns the suppressed count since the last record and starts the next window, or counts and returns `nullopt` |
| `Session::admit_log_record_` | unchanged signature | forwards with `effective_clock_` and `max(HeartBtInt, 1 s)` |
| the accept loop's gate | a local of `run_accept_loop` | one per entry; clock `engine_cfg.clock`; window `max(entry's HeartBtInt or the default, 1 s)` |

## E-8. Log records added (each registered in `src/log/format_registry.cpp`)

| Writer | Level | Format | Rate |
|---|---|---|---|
| `run_liveness_loop` (R-1), written **before** the Logout's write (ruling D-e), so a write that never completes cannot lose it | warn | `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}` | once per connection |
| `log_pre_session_reject` (E) | warn | `acceptor connection refused before a session: reason={} begin_string={} sender_comp_id={} target_comp_id={} error={} suppressed_since_last={}` | E-7's gate |
| `run_accept_loop`'s listener catch | error | `acceptor listener could not bind: begin_string={} sender_comp_id={} target_comp_id={} error={}` | once per entry |

## E-9. `ReconnectFsm` after FR-013

Removed: `attempt_index_`, `heartbeat_interval_`, `logout_disconnect_timeout_`, `heartbeat_timer_`,
`test_request_timer_`, `logout_timer_`, `last_outbound_testreqid_`, `drive_logout`, every other stub with no
caller (`run_heartbeat_cadence`, `run_inbound_liveness_watch`, `validate_inbound_heartbeat_testreqid`,
`process_inbound_sequence_reset`, `reply_to_inbound_resend_request`, as the caller census of research D7 confirms at
the implementation head; owner ruling on SI-1, 2026-11-20), and the constructor's `heartbeat_interval` and
`logout_disconnect_timeout` parameters.
