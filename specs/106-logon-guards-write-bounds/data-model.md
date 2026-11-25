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

| mTLS (`consume` = false) | Credentials (acceptor) | Result |
|---|---|---|
| not mTLS | — | admitted |
| mTLS, `live_peer_id_` authorised for `cfg_.target_comp_id` | readable, `authorize_logon` true | admitted; no event, nothing consumed |
| mTLS, not authorised, or no identity | any | refused |
| admitted | readable, `authorize_logon` false | refused |
| admitted | unreadable (`interpret_logon` refused) and `has_logon_validator()` | refused |
| admitted | unreadable and no validator | admitted |

A refused result ends the arm. The consuming form of the mTLS gate then runs, for its event, and the arm records
`Disconnected` and returns success. An admitted result leads to the Reject. If the Reject disconnects
(`logon_reject_disconnects_`), the consuming form runs before the `Disconnected` write. If the Reject stays in the
arm, nothing is consumed.

## E-2. The tentative inbound advance (D2)

| Item | Kind | Notes |
|---|---|---|
| `SeqnumManager::classify_inbound(seq)` | new member, `asio::awaitable<expected_t<void>>`, `noexcept` | the body of `check_inbound` without `++next_inbound_`: the lock (a drained one gives `session_already_closed`), `frozen_` (`session_already_closed`), too low (`session_seqnum_too_low`), too high (`session_seqnum_too_high`), `seqnum_max` (`store_seqnum_overflow`). An installed header; a member function only, so no layout change. |
| `logon_inbound_advanced` (acceptor), `logon_inbound_advanced_init` (initiator) | existing locals | now: "the classification found this Logon in sequence; its number is consumed at the commit". No new local in the arms. |
| commit | `co_await seqnum_mgr_.check_inbound(seq)` | acceptor: after the 1137 block, before `LogonReceived`, when `logon_inbound_advanced`. Initiator: after the 789 honour and the superseded test, before `Active`, when `logon_inbound_advanced_init && !peer_ack_sent_reset_flag`. A refusal records `Disconnected` unless superseded and returns success. |

Invariant **INV-106-1**: from the arm's entry to its commit point, nothing changes `next_inbound_` except a reset unit
(absolute targets) and the acceptor's knob-driven reset that precedes classification. A refusal in between therefore
leaves NextNumIn as it was before the Logon, in memory and in the store, unless one of those resets ran first. That
case is disclosed (`L-106-a`, research D2's residual rows).

## E-3. The liveness emit bound (D3)

New `Session` members (private, strand-confined):

| Member | Type | Written by | Read by |
|---|---|---|---|
| `liveness_emit_state_` | small struct: `in_flight` (bool), `kind` (TestRequest or Heartbeat), `issued_at` (steady time point), `gen` (the connection generation) | the loop, before and after each emit | the bound |
| `liveness_emit_wake_` | the wake idiom of `ending_logout_wake_`: a callable that cancels the bound's local timer | the bound sets and clears it | an RAII guard in the loop's frame (the emit's return or an exception out of it); `close()`, `Engine::stop()` and `drain_for_reconnect_` before their joins; the `Disconnected` edge |
| `liveness_give_up_decided_` | bool | `decide_give_up_()` | the loop, first, after each emit returns; the loop's give-up block |

States of one bound (one per liveness emit; spawned by the loop and counted by `liveness_counter_`):

| From | Event | To | Action |
|---|---|---|---|
| sleeping | the emit returned (wake) | exit | none |
| sleeping | left Active, a generation change, or cancellation | exit | none |
| sleeping | its clock reaches G, and the give-up is ineligible (an inbound frame since `issued_at`, or the pending TestRequest was answered) | sleeping | recompute G (D4), sleep again |
| sleeping | its clock reaches G, eligible, `liveness_give_up_decided_` false and `logout_in_flight_` free | decided → exit | `decide_give_up_()`: the slot-74 record, the owner claim, `enter_ending_write_(r1)`, the flag set |
| sleeping | its clock reaches G, eligible, but the owner is held by another ending | exit | record nothing. The other ending owns the session's end (105's owner rule). |

`decide_give_up_()` is the decision half of today's give-up block, extracted. The loop calls it at its own give-up,
and the bound calls it at G. The write half (the replay-gate wait, the shared hold, `inbound_logout_leaf_`, the
`Disconnected` record) stays in the loop and runs when the flag is found set. A bound that cannot be spawned
(allocation) fails closed, as 105's clarification D-g: the emit is not started, and the session ends as the loop's
other emission failures do (105 contract C-1 row 8).

The held cap with none pending: the loop's replay-gate wait, today `replay_gate_.wait_for_wake()` alone when
`pending_test_req_id_` is empty, becomes the same `wait_for_wake() || await_deadline(held_test_request_cap(...))`
that the pending case uses. `held_cap` drops its `!pending_test_req_id_.empty()` term.

## E-4. The give-up instants (D3, D4)

T = `test_request_threshold` (default 1.2 × HeartBtInt). L = `last_inbound_steady_`. Each sum saturates, and "+ 1 ns"
is 105's strictly-after guard.

| Situation | Instant | Decided by | Text(58) |
|---|---|---|---|
| a TestRequest pending and written | max(L + 2T, `tr_written_at` + T) + 1 ns | the loop (unchanged, 105) | `TestRequest unanswered, TestReqID <id>` |
| a TestRequest issued, its store and write not returned | max(L + 2T, `tr_issued_at` + T) + 1 ns | the emit bound (new) | the same, naming the issued TestReqID |
| a Heartbeat emit not returned, no TestRequest pending | L + 2T + 1 ns | the emit bound (new) | plan Q2 (default `TestRequest unanswered`) |
| a TestRequest due and held behind a resend answer, one pending | L + 3T + 1 ns | the loop (unchanged, 105's cap) | names the pending one |
| the same with none pending | L + 3T + 1 ns | the loop (new: the cap without the pending term) | plan Q2 |

## E-5. The write watchdog (D5)

New `Session` members (private, strand-confined unless marked):

| Member | Type | Notes |
|---|---|---|
| `write_started_at_` | steady time point | set just before `live->async_write(frame)`, when the limit is on |
| `write_epoch_` | `std::uint32_t` | incremented at that point |
| `write_in_flight_` | bool | true from that point until the write returns |
| `write_cut_epoch_` | `std::uint32_t` | the epoch the watchdog cut. The returning write compares it with its own epoch and returns `session_write_timeout` when they match. |
| `write_watchdog_` | struct: `running`, `parked`, `gen`, the parked wait's timer wake, and an `asio::cancellation_signal` of its own (never `root_cancel_`) | the per-connection coroutine's state. `close()`, `Engine::stop()`, `drain_for_reconnect_` and the connection end emit the signal before joining. |
| `write_watchdog_counter_` | `std::shared_ptr<std::atomic<int>>` | the join counter, the shape of `liveness_counter_`. `close()` and `drain_for_reconnect_` wait for zero; `install_reconnected_transport` asserts it. |

Watchdog transitions (one coroutine per connection, spawned lazily by the first live write while
`cfg_.write_timeout_ms != 0`):

| From | Event | To | Action |
|---|---|---|---|
| parked | a write starts (the writer cancels the wake) | timing | `sleep_until(write_started_at_ + limit)` alone |
| timing | the deadline, the write returned | parked | none |
| timing | the deadline reached, the same epoch still in flight, the session not ending | exit | E-8's record; `write_cut_epoch_ = write_epoch_`; `record_disconnected_unless_already_()`; `spawn_connection_close_(live_transport_shared_())` |
| timing | the deadline reached, a newer epoch | timing | re-arm to the newer write's start + limit |
| any | its cancellation signal (teardown, drain, connection end), `Disconnected`, a generation change, or `closing_or_stopped_()` | exit | none |

The write path's cost with the limit off is one test of `cfg_.write_timeout_ms`. With it on: one `steady_now()` and two
stores. When the watchdog is parked (spaced writes), there is also one timer cancel and the watchdog's re-armed
`sleep_until`. Whether that allocates is decided by the spaced-write allocation cell (research D9).

## E-6. The write class (D5, FR-011)

`enum class write_class : std::uint8_t { ordinary, logout };` is a defaulted (`ordinary`) argument of
`store_then_emit`, forwarded to `live_write_serialized_`. Every site that writes a Logout passes `logout`. Find them
with `git grep -n 'build_logout(' -- src/session`, each followed to its `store_then_emit`. A `logout` write never
starts the watchdog's clock.

## E-7. Configuration

| Field / key | Type, default | Range | Surfaces |
|---|---|---|---|
| `SessionConfig::write_timeout_ms` / TOML session scalar `write_timeout_ms` | `std::uint32_t`, 0 (off) | any `uint32`; TOML out of range → `out_of_range`; wrong type per plan Q4 | C++, TOML; no C setter |
| `[session.transport].tls_handshake_timeout_ms` → `Transport::Config::tls_handshake_timeout` | TOML integer; absent → 30 000 ms (unchanged) | 0 < v ≤ `UINT32_MAX`; else `out_of_range`; wrong type `malformed_value` | TOML; C++ already through `Transport::Config` |
| `EngineConfig::acceptor_tls_handshake_timeout` / TOML root `acceptor_tls_handshake_timeout_ms` | `std::chrono::milliseconds`, 1500 | > 0; TOML as above; refused at session scope | C++, TOML; no C setter |

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

- `Session`: E-3's and E-5's members.
- `SessionConfig`: `write_timeout_ms`.
- `fixpp::core::EngineConfig`: `acceptor_tls_handshake_timeout`.

The size-pin census (research F-8) runs before the first change. `SeqnumManager` and `CompIdAuthorizationPolicy` gain
member functions only.
