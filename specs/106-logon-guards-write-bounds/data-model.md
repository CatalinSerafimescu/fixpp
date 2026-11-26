# Data model: 106 — Logon guards, write bounds, handshake bounds

State and types the decisions in [research.md](research.md) add or change. Names are proposals that the implementer
may adjust; the shape, the owner of each value and the transitions are binding. Every new member lives on the session
strand, like the members 105 added. None is atomic except a join counter, as 103's and 105's are.

## E-1. The identity decision (D1)

| Item | Kind | Owner | Notes |
|---|---|---|---|
| `logon_identity_refuses_(frame, consume)` | private callee of `Session` | both Logon arms, on the validate-first Reject path only | returns true when an applicable gate refuses. Its locals stay in its own frame (104 T093). |
| `logon_identity_admitted_()` | existing | both arms, once, right after `interpret_logon` | consuming form: an event and the one-shot `live_peer_id_` reset, as today |
| `logon_credentials_admitted_(result)` | existing | acceptor, once, right after the mTLS gate | unchanged body |
| `CompIdAuthorizationPolicy::has_logon_validator() const noexcept` | new public accessor | the precheck's unreadable-credentials rule | true when `set_logon_validator` installed a callable. Pimpl, so no layout change. |

The precheck's result, per gate:

The precheck is two independent halves. A half that does not apply admits and consumes nothing; the result is admitted only when every half that applies admits.

| Half | Applies when | Admits | Refuses |
|---|---|---|---|
| mTLS (`consume` = false) | `mtls_ca` or `mtls_pinned`, either role | `live_peer_id_` authorised for `cfg_.target_comp_id`; no event, nothing consumed | not authorised, or no identity |
| credentials | the acceptor, whatever its security profile (plaintext and one-way TLS included) | readable and `authorize_logon` true; or unreadable (`interpret_logon` refused) and no validator | readable and `authorize_logon` false; or unreadable and `has_logon_validator()` |

On a non-mTLS acceptor the credentials half alone decides; on an initiator the mTLS half alone decides.

A refused result ends the arm with no frame written. When the mTLS half applies, the consuming form of the mTLS gate then runs, for its event; the arm records
`Disconnected` and returns success. A non-mTLS acceptor refused by the credentials half ends the same way: no Reject, a silent `Disconnected`. An admitted result leads to the Reject:
- If the Reject disconnects (`logon_reject_disconnects_`, computed before the send), the consuming form runs **before**
  the Reject is written, and a refusal there ends the arm silently (Gate A N-13.2).
- If the Reject stays in the arm, nothing is consumed.

"Unreadable" is any `interpret_logon` refusal, not only an unreadable 553/554 (research D1, N-11; disclosed in
`B-106-1`).

## E-2. The tentative inbound advance (D2)

| Item | Kind | Notes |
|---|---|---|
| `SeqnumManager::classify_inbound(seq)` | new member, `asio::awaitable<expected_t<void>>`, `noexcept` | the body of `check_inbound` without `++next_inbound_`: the lock (a drained one gives `session_already_closed`), `frozen_` (`session_already_closed`), too low (`session_seqnum_too_low`), too high (`session_seqnum_too_high`), `seqnum_max` (`store_seqnum_overflow`). An installed header; a member function only, so no layout change. |
| `logon_inbound_advanced` (acceptor), `logon_inbound_advanced_init` (initiator) | existing locals | now: "the classification found this Logon in sequence; its number is consumed at the commit". No new local in the arms. |
| commit | `co_await seqnum_mgr_.check_inbound(seq)` | acceptor: after the 1137 block, before `LogonReceived`, when `logon_inbound_advanced`. Initiator: after the 789 honour and the superseded test, before `Active`, when `logon_inbound_advanced_init && !peer_ack_sent_reset_flag`. A refusal records `Disconnected` unless superseded and returns success. |

Invariant **INV-106-1**: from the arm's entry to its commit point, nothing changes `next_inbound_` except a reset unit
(absolute targets), the acceptor's knob-driven reset that precedes classification, and the acceptor's hydration (`ensure_hydrated_`, which loads the store's value after the identity gates). A refusal in between therefore
leaves NextNumIn unchanged in FR-006's sense (the store at its value before the frame; in memory, the value the arm classified the frame against), unless one of those resets ran first. That
case is disclosed (`L-106-a`, research D2's residual rows).

## E-3. The connection bound (D3, D5; Gate A ruling R2)

One coroutine per connection, `run_connection_bound_`. It is spawned at most once per connection (one respawn point, below), by the liveness
loop's start or by the first live write with the limit on, whichever comes first, on that coroutine's executor. It is
joined by `connection_bound_counter_`. New `Session` members (private, strand-confined):

| Member | Type | Written by | Read by |
|---|---|---|---|
| `connection_bound_` | struct: `running`, `gen` (the connection generation), `shipping` (the spawn's `dynamic_cast` result), `wake` (a callable that cancels the bound's own timer, as `ending_logout_wake_`) | the bound; `wake` by the bound only | the wakers |
| `emit_in_flight_` | bool | the loop: set at an emit's start, cleared by an RAII guard in its frame at the return or an exception | the bound |
| `write_started_at_` / `write_epoch_` / `write_in_flight_` / `write_cut_epoch_` | steady time point / `std::uint32_t` / bool / `std::uint32_t` | `live_write_serialized_`, just before `async_write`, with the limit on; the bound sets `write_cut_epoch_` at an expiry | the bound; the returning write, which returns `session_write_timeout` when its epoch is `write_cut_epoch_` |
| `connection_bound_counter_` | `std::shared_ptr<std::atomic<int>>` | the bound's join (`counter_join`) | `close()` and `drain_for_reconnect_` wait for zero; `install_reconnected_transport` and `~Session` assert it (N-4) |
| `connection_bound_spawn_fails_`, `connection_bound_wait_throws_` | `std::function<bool()>` | `session_test_access` only | the spawn; the wait (105's seam shape) |

**The wait** (`connection_bound_wait_(timer, deadline)`). The timer is a local of the bound's frame, constructed on its
executor at the spawn.
- `shipping`: `timer.expires_at(deadline)` (max to park), then one `timer.async_wait(as_tuple(use_awaitable))`.
- Otherwise: `timer.expires_at(max)`, then `timer.async_wait(…) || detail::await_deadline(*effective_clock_, deadline)`.
- Either branch returns to the re-test. The deadline is the earlier of G (E-4), when `emit_in_flight_`, and
  `write_started_at_` + `write_timeout_ms`, when `write_in_flight_` and the limit is on.

**Respawn after a failed drain (Gate A loop 2 round 1, N-6).** A drain can fail after the bound left on the drain term: the caller's cancellation at a step-3 suspension (`dispatch_aborted`) leaves the old connection in LogonSent with its Logon-arm write still blocked. No later arming can follow (later writes queue at `write_gate_` and the liveness loop starts only at Active). So `drive_reconnect`, at its failed-drain return and after clearing `reconnect_drain_in_flight_`, spawns the bound again when `conn_gen_` has not moved, `closing_or_stopped_()` does not hold, `write_in_flight_` or `emit_in_flight_` holds, and `connection_bound_.running` is false (Gate A loop 2 round 2, N-5: on one strand the old bound's exit runs during step 3's park, so the term is defensive and has no cell; it states the invariant rather than inferring it from that order). The new bound re-arms on the in-flight write's own `write_started_at_`; a refused spawn fails closed as C-4's spawn failure. This is the one exception to "at most once per connection": at most one bound runs at a time.

**Wakers:** the loop's guard (both edges); a live write's start, unless the bound is already armed on an earlier
write's deadline; `close()` and
`Engine::stop()`'s step 1, before their joins; `drain_for_reconnect_` at its entry, beside `wake_ending_logout_bound_()` (105's position), because from LogonSent with a Logon-arm write blocked the drain parks at its step 3 on `inbound_depth_` before any join; the `Disconnected` edge.

**Joiners and their exit terms (Gate A round 3, N-1 and root cause #2).** Every coroutine that waits for the bound to
end, what it waits on, and the exit term only it sets. Re-derive: `git grep -n 'FIXPP_JOIN_COUNTERS\|connection_bound_counter_' -- src/session/session.cpp`, each join read for the state the session can be in when it is reached, and `Engine::stop()`'s steps in `src/session/engine.cpp`.

| Joiner | Waits on | Its exit term, set before its wake | Cell, and the mutant a hidden exit would mask |
|---|---|---|---|
| `drain_for_reconnect_` (`drive_reconnect`) | `FIXPP_JOIN_COUNTERS` on `connection_bound_counter_`, beside `ending_logout_bound_counter_` | `reconnect_drain_in_flight_`, set by `drive_reconnect` around the drain. From LogonSent the drain records no `Disconnected` (it records one only from Active or LogonReceived) and `conn_gen_` has not moved, so it is the only term | `ConnectionBound106.Teardown_Drain_*` from LogonSent, scripted from the drain's step order (entry wake, step 2, step 3, joins): `_Parked` with the acknowledgement withheld, so no write is in flight and step 3 does not park; `_Timing` with a Logon-arm Reject's write blocked, so step 3 parks on `inbound_depth_` and only the entry wake ends the bound before the join. The bound binds no cancellation slot, so step 4's `root_cancel_.emit` does not end it. The wake and the term deleted, the counter never 0 |
| `close()` | `FIXPP_JOIN_COUNTERS` after `root_cancel_.emit`, beside `liveness_counter_` | `state_ == lifecycle::closing`, in `closing_or_stopped_()` | none counted: `close()` records `Disconnected` (unless already) before `root_cancel_.emit` and its joins, in every state, so the edge also ends the bound. Its term's own observable is the decision during phase 1: `OneEnding106.CloseThenWatchdog`, under M9 with the closing term deleted from the exit row (the exit row precedes the expiry row) |
| `Engine::stop()` | no join of its own: step 4's `Session::close(terminal)` joins | `engine_stop_requested_` (step 1, `note_engine_stop`), in `closing_or_stopped_()` | none counted: step 2 closes every session's transport, which fails an armed write or emit, and the `Disconnected` edge that follows ends the bound before step 4 joins. No test seam sits between step 1 and step 2 (`test_hook_post_send_drain_` runs in step 3, after the closes), so the term's decision side rests on `claim_ending_`'s refusal, with no cell |
| the `Disconnected` edge (the connection's end) | no join: a later `close()` or drain finds the bound gone | `fsm_state_ == Disconnected`, and `ending_claim_` taken (the edge sets `internal`) | `Teardown_ConnectionEnd_*`, the counter read before any `close()`, drain or stop; the two terms are redundant, so the mutant deletes the edge's wake |
| `install_reconnected_transport` | asserts zero, after `begin_connection_()` advances `conn_gen_`, with no suspension between | none: the `gen` term is defensive, since the assertion forbids a bound alive across the advance | none |
| `~Session` | asserts zero, "close() first", as it asserts `ending_logout_bound_counter_` | none: `close()`'s join | none |

| From | Event | To | Action |
|---|---|---|---|
| any | `gen` changed, `Disconnected`, `closing_or_stopped_()`, `reconnect_drain_in_flight_`, or `ending_claim_` taken | exit | none |
| waiting | a wake or the deadline, nothing due | waiting | recompute the deadline and wait again |
| waiting | `emit_in_flight_`, `liveness_may_originate_()`, the clock ≥ G | exit | `decide_give_up_()` (E-10's claim first; the claim refused → nothing; 105's ending bound refused → `decide_give_up_()` closes at once, below); the loop finds the claim when its emit returns |
| waiting | `write_in_flight_`, the same epoch, the clock ≥ start + limit | exit | E-5's expiry (E-10's claim first; refused → nothing) |
| waiting | the wait threw, or a deadline arm completed before its instant | exit | with the claim taken, nothing: no close, no record (the claimant owns the ending, R10); with it free, claim, `Disconnected`, close once (E-5 step 5's form); no record, no Logout |

The rows are binding in order (N-7). At one re-test with both due, the give-up row is evaluated first (its Logout is
the more informative ending, and 105's limit still cuts it); the write row then finds the claim taken
(`OneEnding106.BothDueAtOneWake`).

`decide_give_up_()` is one function for the loop's give-up block and the bound. It takes the claim, writes the slot-74
record, takes the Logout owner and calls `enter_ending_write_(r1)`. The write half stays in the loop: the replay-gate
wait, the shared hold, `inbound_logout_leaf_` and the `Disconnected` record. When `enter_ending_write_` returns `refused` (105's ending bound cannot be spawned, clarification D-g), `decide_give_up_()` itself fails closed: it records `Disconnected` (unless already), then, when `state_ == lifecycle::open`, `spawn_connection_close_(live_transport_shared_())` and `connection_end_owed_.reset()` (research D3, "One close"), and returns. It does not leave the close to the loop's exit, as the base's give-up block does: when the bound decides, the loop is suspended in its emit and reaches its exit only after that emit returns, which on a blocked write needs the close. The loop, on its emit's return with the claim `give_up` and no ending phase entered, writes no Logout and returns. When the loop itself decides, the same function closes at the decision, so its exit finds `connection_end_owed_` reset and closes nothing (`LivenessTiming105.GiveUp_BoundSpawnFails_StillEof`, a guard). When `enter_ending_write_` returns `unlimited` (`logout_disconnect_timeout_ms` 0 or no clock), no close comes (`L-105-f`; `GiveUp106.ZeroLogoutTimeout_StaysOpen`). The connection bound's own spawn and wait failures are research D3's.

What ends the loop's emit after each give-up step, per root cause #1 of Gate A loop 2 round 2:
- the decision (claim, slot 74, owner, `enter_ending_write_(r1)`) runs in the decider, the bound or the loop;
- the Logout write and the `Disconnected` record run in the loop after its emit returns; a blocked write is ended by 105's cut at G + B;
- when 105's ending bound cannot be spawned, the close runs inside `decide_give_up_()` at G, which fails the blocked write;
- an emit still inside its `MessageStore::store` call is ended by nothing 106 adds: it returns when the store returns (research D3, "The ending"; `L-093-12`, amended).

**The held cap with none pending.** The loop's replay-gate wait, today `replay_gate_.wait_for_wake()` alone when
`pending_test_req_id_` is empty, becomes the same `wait_for_wake() || await_deadline(held_test_request_cap(...))` that
the pending case uses, and `held_cap` drops its `!pending_test_req_id_.empty()` term. That wait replaces the loop's own
sleep, so it adds no concurrent sleeper (research D3, "#715 stays out of scope").

## E-4. The give-up instants (D3, D4; Gate A ruling R1)

T = `test_request_threshold` (default 1.2 × HeartBtInt), and L = `last_inbound_steady_`. Each sum saturates, and
"+ 1 ns" is 105's strictly-after guard.

| Situation | Instant | Decided by | Text(58) |
|---|---|---|---|
| 1. a TestRequest pending and written | max(L + 2T, `tr_written_at` + T) + 1 ns | the loop (unchanged, 105) | `TestRequest unanswered, TestReqID <id>` |
| 2. a TestRequest issued, its store and write not returned | L + 3T + 1 ns | the connection bound (new) | the same, naming the issued TestReqID |
| 3. a Heartbeat emit not returned (answerable: none pending, or a frame since the pending one) | L + 3T + 1 ns | the connection bound (new) | names the pending TestReqID when one is pending; otherwise `TestRequest unanswered` with no TestReqID clause (Q2) |
| 4. a TestRequest due and held behind a resend answer, one pending | L + 3T + 1 ns | the loop (unchanged, 105's cap) | names the pending one |
| 5. the same with none pending | L + 3T + 1 ns | the loop (new: the cap without the pending term) | `TestRequest unanswered`, no TestReqID clause (Q2) |

Rows 2 to 5 are the owner's "still held (not yet written)" state, with one instant. Nothing new reads `tr_issued_at`.

## E-5. The write limit's expiry (D5)

With `cfg_.write_timeout_ms != 0`, `live_write_serialized_` sets E-3's write members before `live->async_write(frame)`
and clears `write_in_flight_` by an RAII guard on every exit of the write, both `catch` returns included; `begin_connection_()` clears it and `write_cut_epoch_` with `ending_claim_`. A `logout`-class write (E-6) sets nothing.

At the bound's expiry, in order:
1. `claim_ending_(write_timeout)`; when it is refused, nothing happens;
2. E-8's record;
3. `write_cut_epoch_ = write_epoch_`;
4. `record_disconnected_unless_already_()`: from Active, `onLogout` once; before Active, none;
5. when `state_ == lifecycle::open`, `spawn_connection_close_(live_transport_shared_())` then
   `connection_end_owed_.reset()`, as `expire_ending_wait_` does (105 E-2, one close; N-2).

The cut write returns `session_write_timeout`. `store_then_emit` passes that value through, and every other live-write
error stays `dispatch_aborted` (research D5, N-3).

Cost per write:
- limit off: one test of `cfg_.write_timeout_ms`;
- limit on: one `steady_now()` read and three stores, plus, when the bound is parked, one timer cancel and one re-arm.

The allocation is decided by the mallocnesia cell (research D9), on the shipping clock.

## E-6. The write class (D5, FR-011)

`enum class write_class : std::uint8_t { ordinary, logout };` is a defaulted (`ordinary`) argument of
`store_then_emit`, forwarded to `live_write_serialized_`. Every site that writes a Logout passes `logout`. Find them
with `git grep -n 'build_logout(' -- src/session`, each followed to its `store_then_emit`. A `logout` write never
arms the connection bound's write deadline (E-3).

## E-7. Configuration

| Field / key | Type, default | Range | Surfaces |
|---|---|---|---|
| `SessionConfig::write_timeout_ms` / TOML session scalar `write_timeout_ms` | `std::uint32_t`, 0 (off) | any `uint32`; TOML out of range → `out_of_range`; another type → `malformed_value` (Q4, resolved) | C++, TOML; no C setter |
| `[session.transport].tls_handshake_timeout_ms` → `Transport::Config::tls_handshake_timeout` | TOML integer; absent → 30 000 ms (unchanged) | 0 < v ≤ `UINT32_MAX`; else `out_of_range`; wrong type `malformed_value` | TOML; C++ already through `Transport::Config` |
| `EngineConfig::acceptor_tls_handshake_timeout` / TOML root `acceptor_tls_handshake_timeout_ms` → `fixpp::config::EngineEstablishment::acceptor_tls_handshake_timeout`, which the host copies into `EngineConfig` as it copies `clock` (research D6, C-2) | `std::chrono::milliseconds`, 1500 in both structs | > 0; TOML as above; refused at session scope | C++, TOML; no C setter |

The TOML `kRecognized` set gains `write_timeout_ms` and `acceptor_tls_handshake_timeout_ms`. The sub-table key is
read by `resolve_transport`, whose `build_tls_factory` gains a `Transport::Config` parameter.

## E-8. Error and log records

| Item | Value |
|---|---|
| `core::error::session_write_timeout` | 136, appended after `wire_undefined_tag`; `error_message`: "session: write did not complete within write_timeout_ms" |
| C-ABI | `translate` → `FIXPP_ERR_TRANSPORT_IO`; CSV row `session_write_timeout,FIXPP_ERR_TRANSPORT_IO`; no new C constant |
| log record (warn, `fixpp::log::cat::session`) | `session ended: write timed out: limit_ms={} error={}`, written before the close; registered in `src/log/format_registry.cpp` |
| give-up record | 105's `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}`, unchanged; written by `decide_give_up_()` whichever caller decides. Its `test_req_id` may be empty (E-4, rows 3 and 5). |

## E-9. Layout changes (C++ BREAKING, D7)

- `Session`: E-3's and E-10's members.
- `SessionConfig`: `write_timeout_ms`.
- `fixpp::core::EngineConfig`: `acceptor_tls_handshake_timeout`.
- `fixpp::config::EngineEstablishment`: `acceptor_tls_handshake_timeout` (C-2; add it to F-8's size-pin recipe).

The size-pin census (research F-8) runs before the first change. `SeqnumManager` and `CompIdAuthorizationPolicy` gain
member functions only.

## E-10. The ending claim (research D11; Gate A ruling R3)

| Item | Kind | Notes |
|---|---|---|
| `ending_claim_` | `enum class ending_claim : std::uint8_t { none, give_up, write_timeout, ending_logout, establishment_timeout, internal }` | strand-confined; reset to `none` in `begin_connection_()`; the `Disconnected` edge sets `internal` when it is `none` |
| `claim_ending_(ending_claim who) noexcept` | private, synchronous | refuses when the claim is taken, `closing_or_stopped_()` holds or `reconnect_drain_in_flight_` holds; otherwise sets the claim and returns true. Taken once, at the decision: `run_logout_phase1` and `inbound_logout_leaf_` for kind `r1` write under their decider's claim and neither take nor test it |
| `session_engine_access::claim_ending` | test and Engine access | the pump's establishment expiry claims before `note_establishment_timeout` |

Invariant **INV-106-2** (FR-017): per connection, exactly one `Disconnected` entry; exactly one reason record when the
claimant is a recording decider (D11's table, rows 1, 2 and 4: slot 74, the write-timeout record, the establishment
timeout's), none otherwise (an ending Logout, `close()`, `Engine::stop()`, a bound failure exit). Every `Disconnected` write that
a claimed ending's cut return or close reaches goes through `record_disconnected_unless_already_()` (research D11's
census).
