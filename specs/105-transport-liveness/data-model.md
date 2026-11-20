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
| **D — detached root** | `run_liveness_loop`, `run_peer_logout_bound_`, `run_ending_logout_bound_` | `close_transport_`, spawned at the root's exit; on an Engine session the pump then runs `close(terminal)` | at the root's exit when no frame is inside an arm; otherwise the pump, after that arm |
| **A — application call** | `Session::send` and `Engine::send`'s session body (`send_body_`, `send_deferred_`), `Session::drive_reconnect`, `Session::open` (a direct initiator's Logon) | as class D | as class D |
| **W — R-2 wait** | any of the above, where the ending sent a Logout (E-3) | as E-3's "Who closes" column: the pump after the response, the bound's own release at its expiry, the closer for `close()`, `Engine::stop()` and the drain | at the peer's Logout or the bound |
| **X — excluded** | `Session::close` and `run_logout_phase1` under it; `drain_for_reconnect_` under `drive_reconnect` | the close or drain that owns it, as today | as today (B-103-9's bounded waits kept) |

A write reached from several roots takes the class of the root it runs under at that moment. The census table in
tasks.md lists each write site with every root that reaches it.

## E-2. The connection-end latch (`Session`, private)

| Field | Type | Written by | Read by |
|---|---|---|---|
| `reconnect_drain_in_flight_` | `bool` | `drive_reconnect`, around its `drain_for_reconnect_` call only | `owe_connection_end_()`, the R-2 bound's live predicate |
| `connection_end_owed_` | `std::optional<std::uint32_t>`: the `conn_gen_` the close is owed on | set: `owe_connection_end_()` only; cleared: `take_connection_end` (pump), `release_connection_end_`, `close()` at its commit of `closing`, `drain_for_reconnect_` | the pump after each frame; each class D and A root at its exit |
| `connection_close_counter_` | `std::shared_ptr<std::atomic<int>>` | `release_connection_end_` (+1 before the spawn); the close frame's guard (−1) | `close()` and `drain_for_reconnect_` (join by post); `~Session` (assert 0) |

**The one setter**, `owe_connection_end_() noexcept`: sets `connection_end_owed_ = conn_gen_` when
`state_ == lifecycle::open && !reconnect_drain_in_flight_ && ending_wait_.phase != armed`, else does nothing. Its
callers (a census row and a mutant each):
1. `record_state_transition_`, on a transition **into** `Disconnected` from another state only. A write of
   `Disconnected` over `Disconnected` (Arm B's tail, the liveness loop's emit failures, `inbound_logout_leaf_`'s
   `toAdmin` throw, among others) owes nothing.
2. The R-2 bound at its expiry, when its wait was armed before Active (the session is `Disconnected` already; E-3
   row 10).
3. The pre-Active response, the peer's Logout during a wait armed before Active (E-3 row 7).

**The close frame.** `release_connection_end_()` decides synchronously, on the strand: the latch is set, its
generation is `conn_gen_`, `state_ == lifecycle::open` and `inbound_depth_ == 0`. It then clears the latch, takes
`live_transport_shared_()` and a copy of `connection_close_counter_`, and spawns a static coroutine
`close_transport_(std::shared_ptr<Transport>, counter)` on `exec_.underlying()`. That frame awaits
`close_async()` on the transport it holds and touches no member of `Session`, so it may outlive the Session.

**Invariants.**
- I-1. At most one consumer acts per set: every consumer tests and clears on the session strand.
- I-2. A class D or A consumer acts only when `inbound_depth_ == 0`, tested at the spawn, not when the frame runs.
  That is enough because a frame that enters after the edge finds the session `Disconnected` with no wait armed and
  is drained by the `Disconnected` row, with no store work.
- I-3. The close frame's only effect is `close_async()` on the transport taken at the spawn; it never reads `this`.
- I-4. `close()` never runs its own `close_async()` while a close frame is in flight (the join).
- I-5. At most one owe per connection: each of the three callers fires at most once per connection (one transition
  into `Disconnected`; the wait resolves once).

**Session-engine access.** `session_engine_access::take_connection_end(Session&) noexcept -> bool` (test and clear),
for the pump; `session_engine_access::ended(Session const&) noexcept -> bool` (`state() == Disconnected`), for the
pump's `expire()` (E-5).

## E-3. The R-2 wait (`Session`, private): one state machine for the wait, the bound and the latch

| Field | Type | Meaning |
|---|---|---|
| `ending_wait_.phase` | enum `{ none, armed, resolved }` (one byte) | the wait's state |
| `ending_wait_.from_active` | `bool` | the wait was armed after Active (the session stays Active) or before it (the session is `Disconnected`) |
| `ending_wait_.deadline` | `fixpp::core::steady_time_point` | when the bound ends the wait |
| `ending_logout_wake_` | the shape of `peer_logout_wake_` | published by the bound while it waits |
| `ending_logout_bound_counter_` | `std::shared_ptr<std::atomic<int>>` | the bound's join counter; joined by `close()` and `drain_for_reconnect_`; asserted 0 by `~Session` |

**The bound's wake sources:** the `Disconnected` edge; `close()` at its first commit of `closing` (beside its
`claim_peer_logout_none_()`); `drain_for_reconnect_` at its entry; the response (E-3 rows 5 to 7). A wait armed
before Active sees no edge at `close()` or at the drain, since the session is `Disconnected` already; that is why
the last three are explicit.

**The bound's live predicate**, read after every wake: `phase == armed`, `conn_gen_ == gen` (asserted),
`!engine_stop_requested_`, `!reconnect_drain_in_flight_`, and
- for a wait armed after Active: `fsm_state_ == Active`. A graceful `close()` leaves the session Active while it
  poll-joins the Logout owner, so the bound runs on (row 6, row 9); a terminal close, an escalated graceful close, the
  drain and an arm that fails all write `Disconnected` first, so it exits;
- for a wait armed before Active: `state_ == lifecycle::open`. Any `close()` there is terminal in effect, since
  `close()`'s Logout phase runs only from Active or LogonReceived.
Unlike `run_peer_logout_bound_`'s `live()`, it does not test `closing_or_stopped_()` after Active. Every
exit of the bound sets `resolved` when the phase is still `armed`, clears its wake, and calls
`release_connection_end_()`.

**Transitions.** "Owes" means `owe_connection_end_()` sets the latch (E-2); "no owe" means its predicate refuses.

| # | FSM, lifecycle, phase | Event | Phase after | Latch | Bound | Who closes |
|---|---|---|---|---|---|---|
| 1 | Active, open, `none` | `send_ending_logout_`: the Logout was transmitted, a clock is present, timeout > 0 | `armed`, `from_active`, deadline = `deadline_after(now, logout_disconnect_timeout_ms)` | none (the FSM stays Active) | spawned | rows 5, 8, 11, 12, 13 |
| 2 | NotConnected or LogonSent, open, `none` | a refusal's Logout transmitted, clock, timeout > 0 | `armed`, not `from_active` | the refusal's own `Disconnected` write: no owe (armed) | spawned **after** that write | rows 7, 10, 12, 13 |
| 3 | any, open, `none` | the Logout was not transmitted; or timeout 0; or no clock | `none` | the site records `Disconnected` as today: owes | none | the root's consumer, at once (E-1) |
| 4 | Active, any, `armed` | a second ending condition (contract C-2 W-8) | `armed` | none (nothing recorded) | runs on | — |
| 5 | Active, open, `armed` | the peer's Logout (W-1) | `resolved`, before the arm's `Disconnected` write | that transition owes | woken, exits | the pump, after the arm returns (reset unit first) |
| 6 | Active, `closing` (graceful), `armed` | the peer's Logout | `resolved` | no owe (`closing`) | woken, exits | `close()`, released from its poll-join |
| 7 | Disconnected, open, `armed` (before Active) | the peer's Logout (W-1) | `resolved` | `owe_connection_end_()` called directly | woken, exits | the pump, after the frame |
| 8 | Active, open, `armed` | the bound expires | `resolved`, then `Disconnected` | that transition owes | exits | the bound's own release |
| 9 | Active, `closing` (graceful), `armed` | the bound expires | `resolved`, then `Disconnected` | no owe | exits | `close()`, released from its poll-join |
| 10 | Disconnected, open, `armed` (before Active) | the bound expires | `resolved` | `owe_connection_end_()` called directly | exits | the bound's own release |
| 11 | Active, open, `armed` | an arm returns an error (W-9) | `resolved` at the bound's exit | the arm's transition: no owe (armed) | woken by the edge, exits | the pump, on the error |
| 12 | any, `closing` (terminal or escalated) or the stop flag, `armed` | `close(terminal)`, `Engine::stop()`, an escalation | `resolved` at the bound's exit | none (`closing`) | woken, exits; `close()` joins it | `close()` |
| 13 | any, open, `armed` | `drive_reconnect`'s drain | `resolved` at the bound's exit | none (drain flag) | woken, exits; the drain joins it | the drain, as today |
| 14 | any, any, `resolved` | the next connection (`install_reconnected_transport`, `attach_accepted_transport`) | `none` (asserted `resolved` or `none` there) | — | — | — |

**The order before Active (row 2)** is: the Logout's transmit, then the arming, then the refusal's own
`Disconnected` write, then the bound's spawn. Arming after the write would let the edge owe the close and end the
wait before it began; spawning before it would let the edge wake the bound at once.

**The invariant that makes the latch work for the wait:** an ender that relies on the latch (the response, the
bound) sets `resolved` before it writes `Disconnected`. An arm that fails relies on the pump's error close
instead (row 11). No other ending writes `Disconnected` during a wait armed after Active: the silent sites go
through one helper that records nothing while such a wait is armed, and the Logout-producing sites go through
`send_ending_logout_`, which owns their `Disconnected` write (research D2). The armed test is in those two helpers,
never in `record_state_transition_`, whose `Disconnected` write also serves `close()` and the drain.

**During `armed`:** the rows of contract C-2 "While the wait is armed" (W-1 to W-14).

## E-4. Liveness timing (`run_liveness_loop`, locals)

| Quantity | Value | Arithmetic |
|---|---|---|
| H | `cfg_.heartbeat_interval.value_or(kDefaultHeartBtInt)` (whole seconds) | as today |
| T | `cfg_.test_request_threshold` when set, else `default_test_request_threshold(H)` = H × 1200 ms | saturating at `milliseconds::max()` |
| Heartbeat instant | `last_outbound_steady_ + H` | as today |
| TestRequest instant | `deadline_after(last_inbound_steady_, T)` | saturating |
| Give-up instant | `deadline_after(deadline_after(deadline_after(last_inbound_steady_, T), T), std::chrono::nanoseconds{1})`, with `last_inbound_steady_` re-read after the grace sleep (D-c) | three saturating steps, no product; the guard is the third (`deadline_after`'s template takes nanoseconds, since `steady_time_point` is `std::chrono::steady_clock`'s and its static_asserts require a whole multiple of its tick) |
| Gap deadline (unchanged, B-537-2) | `detail::resend_gap_deadline(started_at, H)` | as today |

**One predicate**, `liveness_may_originate_()` = `fsm_state_ == fsm_state::Active && ending_wait_.phase != armed`: the
loop condition and every test after a suspension (after the main sleep, after the Heartbeat's emit, before and after
the grace sleep). It is keyed on the wait, not on `logout_in_flight_`, because `run_logout_phase1` sets that flag
before it records `LogoutSent`. The arming adds no wake: the predicate is what keeps the loop from originating, and
`close()` and the drain join the loop as today (research D2).

**The give-up test after the grace sleep:** the session gives up only when a TestRequest is pending and the clock is
at or past the give-up instant computed from the `last_inbound_steady_` read then. Otherwise the loop goes back to
its top, which recomputes the Heartbeat and TestRequest instants, so outbound Heartbeats resume; the TestRequest stays
pending. The "inbound arrived" test at the top reads the TestRequest instant (T), not HeartBtInt.

The loop is off when H ≤ 0 (owner ruling on SI-2, 2026-11-20).

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
| `run_liveness_loop` (R-1) | warn | `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} logout_sent={} error={}` | once per connection |
| `log_pre_session_reject` (E) | warn | `acceptor connection refused before a session: reason={} begin_string={} sender_comp_id={} target_comp_id={} error={} suppressed_since_last={}` | E-7's gate |
| `run_accept_loop`'s listener catch | error | `acceptor listener could not bind: begin_string={} sender_comp_id={} target_comp_id={} error={}` | once per entry |

## E-9. `ReconnectFsm` after FR-013

Removed: `attempt_index_`, `heartbeat_interval_`, `logout_disconnect_timeout_`, `heartbeat_timer_`,
`test_request_timer_`, `logout_timer_`, `last_outbound_testreqid_`, `drive_logout`, every other stub with no
caller (`run_heartbeat_cadence`, `run_inbound_liveness_watch`, `validate_inbound_heartbeat_testreqid`,
`process_inbound_sequence_reset`, `reply_to_inbound_resend_request`, as the caller census of research D7 confirms at
the implementation head; owner ruling on SI-1, 2026-11-20), and the constructor's `heartbeat_interval` and
`logout_disconnect_timeout` parameters.
