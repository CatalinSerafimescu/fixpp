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
| **D — detached root** | `run_liveness_loop`, `run_peer_logout_bound_`, `run_ending_logout_bound_` | `close_connection_`, spawned at the root's exit; on an Engine session the pump then runs `close(terminal)` | at the root's exit when no frame is inside an arm; otherwise the pump, after that arm |
| **A — application call** | `Session::send` and `Engine::send`'s session body (`send_body_`, `send_deferred_`), `Session::drive_reconnect`, `Session::open` (a direct initiator's Logon) | as class D | as class D |
| **W — R-2 wait** | any of the above, where the ending sent a Logout (E-3) | as the class of the root that ends the wait | at the peer's Logout or the bound |
| **X — excluded** | `Session::close` and `run_logout_phase1` under it; `drain_for_reconnect_` under `drive_reconnect` | the close or drain that owns it, as today | as today (B-103-9's bounded waits kept) |

A write reached from several roots takes the class of the root it runs under at that moment. The census table in
tasks.md lists each write site with every root that reaches it.

## E-2. The connection-end latch (`Session`, private)

| Field | Type | Written by | Read by |
|---|---|---|---|
| `reconnect_drain_in_flight_` | `bool` | `drive_reconnect`, around its `drain_for_reconnect_` call only | the edge |
| `connection_end_owed_` | `bool` | set: the edge in `record_state_transition_`; cleared: `take_connection_end` (pump), `release_connection_end_`, `close()` at its commit of `closing`, `drain_for_reconnect_` | the pump after each frame; each class D and A root at its exit |
| `connection_close_counter_` | `std::shared_ptr<std::atomic<int>>` | `release_connection_end_` (+1 before the spawn); `close_connection_`'s guard (−1) | `close()` and `drain_for_reconnect_` (join by post); `~Session` (assert 0) |

**Set condition** (the only setter, at the `Disconnected` edge):
`state_ == lifecycle::open && !reconnect_drain_in_flight_ && ending_wait_.phase != armed`.
`reconnect_drain_in_flight_` is a new private `bool`, held only around `drain_for_reconnect_`'s call (research D1
item 1).

**Invariants.**
- I-1. At most one consumer acts per set: every consumer tests and clears on the session strand.
- I-2. A class D or A consumer acts only when `inbound_depth_ == 0`; otherwise it leaves the latch for the pump.
- I-3. `close_connection_(gen)` acts only when `state_ == lifecycle::open` and `conn_gen_ == gen`, and its only
  effect is `close_async()` on the live transport.
- I-4. `close()` never runs its own `close_async()` while a `close_connection_` is in flight (the join).

**Session-engine access.** `session_engine_access::take_connection_end(Session&) noexcept -> bool` (test and clear),
for the pump; `session_engine_access::ended(Session const&) noexcept -> bool` (`state() == Disconnected`), for the
pump's `expire()` (E-5).

## E-3. The R-2 wait (`Session`, private)

| Field | Type | Meaning |
|---|---|---|
| `ending_wait_.phase` | enum `{ none, armed, resolved }` (one byte) | the wait's state |
| `ending_wait_.deadline` | `fixpp::core::steady_time_point` | when the bound ends the wait |
| `ending_logout_wake_` | `std::function<void()>` (or the bound's existing wake shape) | published by the bound while it waits; called by the edge |
| `ending_logout_bound_counter_` | `std::shared_ptr<std::atomic<int>>` | the bound's join counter; joined by `close()` and `drain_for_reconnect_`; asserted 0 by `~Session` |

**Transitions.**

| From | Event | To | Side effects |
|---|---|---|---|
| `none` | `send_ending_logout_` returns with a clock present | `armed` | `deadline = deadline_after(now, logout_disconnect_timeout_ms)`; `run_ending_logout_bound_` spawned (before Active the pump's establishment deadline also still runs, E-5) |
| `none` | `send_ending_logout_` with no clock | `none` | the caller records `Disconnected`; the latch is set; the connection closes after the write |
| `none` | `send_ending_logout_` and the Logout was not transmitted (did not build, emit failed, held back) | `none` | as the row above: close at once |
| `armed` | `send_ending_logout_` again (a second ending) | `armed` | nothing written, nothing recorded |
| `armed` | a receipt arm enters (the peer's Logout, after Active) | `resolved` | the arm's own `Disconnected` edge sets the latch; its reset point runs (104) |
| `armed` | `on_inbound_cold_` in `Disconnected` sees a fault-free `35=5` with MsgType third | `resolved` | the latch is set; no reset (OQ-1) |
| `armed` | the bound expires | `resolved` | `Disconnected` recorded if not already (the edge sets the latch), else the latch set directly |
| `armed` | `close()` or `drain_for_reconnect_` records `Disconnected` | `resolved` | no latch (excluded); the edge wakes the bound, which exits |
| `resolved` | the next connection (`install_reconnected_transport`, `attach_accepted_transport`) | `none` | asserted `resolved` or `none` there |

**During `armed`, after Active** (FSM `Active`, `logout_in_flight_` set):
- application `send()`: refused, `session_invalid_state_for_send` (the existing `send_body_` test);
- `run_liveness_loop`: leaves at its next test (`Active && ending_wait_.phase != armed`); sends nothing more;
- every inbound frame: the Active arms, unchanged;
- a silent ending (`Disconnected` with no Logout): the FSM moves to `Disconnected`, no latch, the bound holds.

**During `armed`, before Active** (FSM `Disconnected`): every frame drained, except the peer's Logout (the table).

## E-4. Liveness timing (`run_liveness_loop`, locals)

| Quantity | Value | Arithmetic |
|---|---|---|
| H | `cfg_.heartbeat_interval.value_or(kDefaultHeartBtInt)` (whole seconds) | as today |
| T | `cfg_.test_request_threshold` when set, else `default_test_request_threshold(H)` = H × 1200 ms | saturating at `milliseconds::max()` |
| Heartbeat instant | `last_outbound_steady_ + H` | as today |
| TestRequest instant | `deadline_after(last_inbound_steady_, T)` | saturating |
| Give-up instant | `deadline_after(deadline_after(last_inbound_steady_, T), T)` plus the 1 ns guard | two saturating steps, no product |
| Gap deadline (unchanged, B-537-2) | `detail::resend_gap_deadline(started_at, H)` | as today |

**Loop condition:** `fsm_state_ == fsm_state::Active && ending_wait_.phase != armed`. The loop is off when H is 0 (SI-2 asks
whether "≤ 0").

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
`test_request_timer_`, `logout_timer_`, `last_outbound_testreqid_`, `drive_logout`, and the constructor's
`heartbeat_interval` and `logout_disconnect_timeout` parameters. The other no-op stubs are SI-1's.
