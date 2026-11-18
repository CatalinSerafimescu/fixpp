# Data Model: 104 sequence-reset fidelity

All the state below is private to `Session` or to its `SeqnumManager`, and lives on the session strand. No
configuration key or default changes, and no public function's signature; one installed type gains members
(`SeqnumManager`, below). One private `Session` member is added, `wire_closed_by_reset_` (below; contract RR-6),
and no `Session` member is renamed (the one `Engine` member is B-104-7). One member keeps
its name and changes its type and contract (`logout_seen_`, `bool` → a one-byte enum). One member is deleted: `reset_unit_wake_`, the waker of 093's
event-driven wait, which `close()`'s poll replaces (contract RR-2 item 4; re-derive its users with
`git grep -n reset_unit_wake_ -- src include tests`). So `sizeof(Session)` may move either way; phase 7's one-off size reading settles it. There is no pin on
it to move (re-derive: `git grep -n "sizeof(Session)\|sizeof(fixpp::session::Session)" tests include src`).
One installed header outside `Session` changes: `SeqnumManager` (`include/fixpp/session/seqnum_manager.hpp`), which
`Session` holds by value, gains the manager freeze ("`SeqnumManager`: the freeze" below; B-104-7).
Decisions are in [research.md](research.md); the observable contract is in
[contracts/session-wire-endings-104.md](contracts/session-wire-endings-104.md) (Part 1: RT-1, RR-1 to RR-4).

## `logout_seen_` (name kept; type and contract changed): the receipt marker

A one-byte enum with three values, its underlying type `std::uint8_t` (`enum class … : std::uint8_t`), so the
one byte is a requirement and not a compiler default. It replaces the `bool` whose contract was "a Logout in either
direction".

| Value | Meaning |
|---|---|
| `none` | no awaited read-pump frame of a receipt arm is between its receipt and its reset point, and no receipt on this connection reached it. A too-high Logout's bound that is owed a receipt leaves it `none`: that state is the 103 mark (`peer_logout_.phase`), not this member |
| `pending` | an awaited read-pump frame of a receipt arm (A, L, S, or B's immediate acknowledgement; contract RT-1) is between its receipt and its reset point. That frame is its one owner |
| `reached` | this connection's received Logout reached its reset point (its unit started in the same strand turn), or `close()` closed out a receipt that was still `pending` when it could not wait; either way no further unit runs, and RR-4 refuses every store call issued from then on (contract RR-2 item 5) |

| Event | Effect |
|---|---|
| A peer Logout received by an awaited read-pump frame, with rol set: arm A's entry (`on_inbound_active_logout_`), arm L's 35=5 (`on_inbound_logout_sent_`), arm S's 35=5 (`on_inbound_logon_sent_`), arm B's immediate-acknowledgement branch (`on_inbound_active_too_high_logout_`) | `pending` (with rol off: no effect) |
| `on_inbound_active_too_high_logout_`'s spawn branch (the bound is started) and its ignored branch (a second too-high Logout while the 103 mark is set) | **no effect** (contract RR-2 item 1) |
| A Logout received in NotConnected | no effect |
| fixpp's own Logout emitted (`run_logout_phase1`) | **no effect** (the base's set is deleted) |
| Any other Logout fixpp sends (error paths, the too-low Logout) | no effect |
| A pump frame reaches its reset point | the thread's cancellation state reset, then `pending` → `reached` and the unit's start, in one strand turn (contract RR-2 item 3); already `reached` (`close()` closed it out) → no unit (RR-2 item 5) |
| Arm B's bound reaches its reset point (in `acknowledge_peer_logout_`, with the 103 mark `acking`) | the bound's one write: `none` → `reached` and the unit's start, in one strand turn, asserting `none` (contract RR-2 items 2, 3 and 5) |
| A pump frame ends before its reset point, by return or by exception; and before any `close()` the arm calls itself | `pending` → `none`, by the frame's owner, only if still `pending` (contract RR-2 item 2) |
| Arm B's bound ends unacknowledged (claimed, stopped, superseded, drained, its exception handler) | **no effect**: the bound's receipt owed is the 103 mark, which 103 clears or leaves to a claim (contract O18) |
| `Session::close`: the wait before the predicate | read: a poll by post while `pending` or `reset_unit_in_flight_`, after 103's bound join, so it never sees a bound's receipt (RR-2 item 4) |
| `Session::close`: after the wait, before the predicate | a `pending` marker (no clock, or the wait expired) is set to `reached` with **no reset** under rol: the receipt did not reach its reset point (contract RR-2 item 4) |
| `Session::close`: the teardown predicate | `!teardown_reset_done_ && rod`; the rol term for "`reached`, unit not started" has no input by RR-2 item 3 and is not coded |
| `store_then_emit` (before its store call), `persist_inbound_advance_` (after its `store_is_persistent_` return, before its store call) | read: refuse a call issued while `reached` (contract RR-4); `store_then_emit` takes its number back. A call issued earlier runs to its end (RR-2 item 5) |
| `inbound_resend_request_leaf_`'s entry, before its number peek and build | read: when `reached`, return success with no write (no `toAdmin`, no number, no store call, no state write); `none` and `pending` pass (contract RR-5 item 6, "The leaf entry test") |
| `install_reconnected_transport` | `none`, and the manager thawed in the same block (contract RR-5 item 1) |
| Any write of `reached` (a reset point, `close()`'s close-out) | the manager frozen in the same strand turn: at a reset point by `run_reset_unit_`, after its manager set; at the close-out with the marker's write (contract RR-5) |

- **Invariant RO-1.** With rol set, the marker is `pending` exactly while one awaited read-pump frame of a receipt
  arm (A, L, S, or B's immediate acknowledgement) is between its receipt and its reset point or exit. That frame
  is its one owner (contract INV-P). It leaves `pending` once, to `reached` (the frame at its reset point, or
  `close()` closing out a receipt it could not wait for, with no reset) or to `none` (the frame's owner).
  - A too-high Logout's detached bound never makes it `pending`: its owed receipt is the 103 mark.
  - The bound writes the marker once, `none` → `reached` at its reset point (contract INV-M, INV-X).
  - With rol off the marker stays `none`, so 104 adds no receipt wait, receipt reset or RR-4 refusal to such a
    session. That is the marker's scope only: `close()` waits for a Logon arm's 141=Y unit in flight whatever the
    knobs (contract RT-5, B-104-2, cell R-1s), and the unasked reset (B-104-4) needs no knob.
- **Invariant RO-2.** No Logout that fixpp sends changes the marker. FR-015 follows, and B-538-7's first half
  still holds.
- **Invariant RO-3.** A receipt reset under rol has one owner, the arm's unit, which `close()` waits for: by its
  wait (contract RR-2 item 4), or for the bound's unit by 103's bound join, which precedes that wait. The
  teardown never resets a `pending` receipt (RU-1). The one case it would own, `reached` with the unit not started,
  has no input because the reset point is one strand turn. An arm that finds the marker `reached` at its reset
  point runs no unit, and issues no store call from `reached` on (contract RR-2 item 5).
- The header comment states RO-1 to RO-3 as conditions, names 104 and the 024 T013 / C3.1 clause it supersedes, and
  gives the recipe `git grep -n logout_seen_ -- src include`. It names the receipt arms by reference (contract RT-1,
  "a peer Logout received by an awaited read-pump frame") and not as a list of functions, and it contains no count
  of sites (FR-006; tasks "Comments and records").
- **Readers of the member outside `session.cpp`** (re-derive: `git grep -n logout_seen -- include src tests bindings
  tools`). At the base there is no code reader outside `session.cpp`: the hits are comments, in `session.hpp`
  (the member's own comment, and the paragraph saying the latches including `logout_seen_` stay set across a
  reconnect), `session_config.hpp`'s knob comments, and comments in `test_reset_on_lifecycle.cpp`. So the type
  change compiles against no test; each comment is a census hit (research D8) and is reworded.
- **Order against an in-place reconnect.** `drive_reconnect` drains, then `reconnect_fsm_.drive_reconnect_attempt()`
  runs `install_reconnected_transport` (which sets `none`), then `emit_initiator_logon_`. So the reconnect Logon's
  `store_then_emit` never meets a `reached` marker.

## `wire_closed_by_reset_` (new): nothing transmitted after a reset point (contract RR-6, ruling W)

A private `bool` of `Session`, beside the receipt marker, declared `bool wire_closed_by_reset_ = false;` as the
other `bool` members are.

| Event | Effect |
|---|---|
| `Session` construction | clear (the default member initializer) |
| A reset point (arm A, L or S: the frame's `pending` → `reached`; the bound: `none` → `reached`), in the same statement block, through one helper, crossing included | set |
| `close()`'s close-out of a `pending` marker | **not** written (RR-6 item 5) |
| `install_reconnected_transport`, in the block that sets the marker to `none` and thaws the manager | cleared |
| `live_write_serialized_` after the `write_gate_` grant; before `transport_send_` in `store_then_emit` and in `replay_outbound_range_`'s `transmit_async` | read: when set, no transmit, a success (a distinct return from `live_write_serialized_`, so `store_then_emit` skips `last_outbound_steady_` as its `transmit=false` exit does) |
| D-OUT-10 in `store_then_emit` | read when the test fires. Set, with no exemption: success with no transmit, the test's ending (contract RR-6 item 3). Set, with the exemption: transmitted (RR-6 item 4). Clear: `dispatch_aborted`, as before |

- **The exemption parameter.** `store_then_emit` and `live_write_serialized_` gain one parameter of the private
  scoped enumeration `wire_reset_exemption` (`none`, `crossing_logout`), defaulted to `none`. `run_logout_phase1`
  is its one origin, for our own Logout in a crossing (RR-6 item 4). `transmit_async` takes none. The census is by
  category: re-derive with `git grep -n 'crossing_logout' -- src include`.
- **Invariant RO-5.** The member is clear at construction and at each new connection. It is set exactly when a reset
  point has run on this connection, and never by a close-out. While it is set:
  - no frame is handed to the transport except the frame carrying the exemption;
  - every frame held back, whether it met D-OUT-10, the gate or the sink, ends with success and no transmit.
- It is not the marker, because the close-out also writes `reached`. It is not `frozen(freeze_key)`, which no
  production branch tests (RR-5 item 4).

## The reset unit: new callers

`run_reset_unit_(next_in, next_out)` (093 data-model E-10, contract C-6) is unchanged but for one deletion, its
call of `reset_unit_wake_` (below). It gains callers:

| Caller | Targets | Before the call | After the call |
|---|---|---|---|
| arms A, L, B, S at their reset point (rol set) | `(1, 1)` | `Disconnected` recorded (RR-3); the thread's cancellation state reset; the marker `pending` → `reached` (B's bound: `none` → `reached`); no unit if it was already `reached` (RR-2 item 5) | `reset_cancellation_state(enable_total_cancellation{})`; a manager or store error is logged; the arm continues as it does today (L: `logout_confirmed_` and phase 1's wake) |
| `on_inbound_logon_sent_`, the unasked branch (D3.1) | `(2, 1)` when the acknowledgement's 34 is 1; `(1, 1)` otherwise | the mTLS CompID-authorisation gate passed (clarification N-8) | today's step 5 to step 7 (030's persistent-fatal disposition) |

`reset_unit_in_flight_` is unchanged, and `close()` reads it earlier (below). `reset_unit_wake_` and its call in
`run_reset_unit_` are deleted (contract RR-2 item 4).

`run_reset_unit_` also arms the manager freeze (contract RR-5 item 4): when the marker is `reached`, after its
manager set, whatever that set returned, and before `reset_unit_in_flight_` and its store call (or its return, with
no store). A Logon arm's unit runs with the marker not `reached` and arms nothing. Debug asserts check that
the manager is not frozen: one at its entry, and one after its manager set, immediately before the freeze (contract
RR-5 item 4).

## `SeqnumManager`: the freeze (contract RR-5; research D11)

| Member | Kind | Contract |
|---|---|---|
| the freeze flag (one private `bool`) | new, private | set while the session's receipt marker is `reached`; read by the six refused mutators, by `frozen(freeze_key)` for `Session`'s Debug asserts, and, in tests only, by `seqnum_manager_test_access` (T049; R-5d); no production branch tests it |
| `SeqnumManager::freeze_key` | new, a passkey type | its constructor is private, with `Session` its friend, so no other caller can construct one |
| `freeze(freeze_key)`, `thaw(freeze_key)` | new | set and clear the flag; no lock (strand-confined, as `take_back_outbound` and `peek_outbound` are) |
| `[[nodiscard]] bool frozen(freeze_key) const noexcept` | new | reads the flag; called only by `Session`'s Debug asserts (contract RR-5 item 4), never by a production branch; inline, no lock, no data member |
| `check_inbound`, `assign_outbound`, `hydrate` | changed | while frozen: `unexpected(session_already_closed)`, counters unchanged; the test sits after the lock grant, before any read or write of a counter |
| `set_next_inbound`, `set_next_outbound` | changed | while frozen: success, with no write |
| `take_back_outbound` (inline, in the header) | changed | while frozen: a no-op |
| `reset_to_one` | unchanged | never refused: a reset writer's mutator (its callers are the two reset writers) |

`Session`'s sites:
- **Freeze:** inside `run_reset_unit_` (above), and at `close()`'s close-out of a `pending` receipt, written with
  the marker's `reached`.
- **Thaw:** in `install_reconnected_transport`, with the marker's `none`.
- **Debug assert:** after each site, the manager is frozen if and only if the marker is `reached`.
- **Conditional `Disconnected` writes** (contract RR-5 item 6): every `record_state_transition_(fsm_state::
  Disconnected)` on a path that a refused `assign_outbound` returns into is made only when the state is not
  `Disconnected` already: each `assign_outbound` call's failure branch, and the writes its frame makes on the way
  out to the pump. Re-derive: `git grep -n 'seqnum_mgr_.assign_outbound()' -- src/session/session.cpp`, and read
  each failure branch.
- **Test access:** `tests/support/seqnum_manager_test_access.hpp` gains two static functions, `set_frozen` (sets
  the flag) and `frozen` (reads it) (orchestrator ruling 3 on the sixteenth run; brain "Test access to private `Session` state": a function on the existing friend, with no change to the
  class's access), for cell R-5d.

- **Invariant RO-4 (the freeze).** The manager is frozen exactly while the marker is `reached`, and only while
  `Disconnected` (contract RR-5, INV-F). Every counter write is inside a mutator body, so the flag governs every
  writer (INV-Z). The header comment on the flag names 104 and RR-5, states the condition and the recipes of
  INV-Z and of `reset_to_one`'s callers, and contains no count of sites or callers.

## `Session::close`'s teardown: order changed

| Step | Base | 104 |
|---|---|---|
| a | `if (!teardown_reset_done_ && ((logout_seen_ && rol) \|\| rod))` | **poll by post while the marker is `pending` or `reset_unit_in_flight_`** (contract RR-2 item 4: needs `effective_clock_`, bounded by `logon_timeout_ms`, `session_event_close_reset_wait_expired` on expiry) |
| b | inside: set `teardown_reset_done_`, wait for an in-flight unit (event-driven, woken by `reset_unit_wake_`), `reset_seqnums_to_one_durable(logged)` | a `pending` marker → `reached`, with no reset; then `if (!teardown_reset_done_ && rod)`: set `teardown_reset_done_`, then `reset_seqnums_to_one_durable(logged)` |

024 C5.1 (at most one reset per teardown) is unchanged.

## LogonSent acknowledgement: the 141 branch

Inputs, all on the strand:
- `peer_ack_sent_reset_flag`, the acknowledgement's 141=Y, now read **before** Guard (4);
- `own_logon_sent_reset_flag_`, read there and still one-shot cleared where it is today;
- `seq`, the acknowledgement's 34.

| `peer_ack_sent_reset_flag` | `own_logon_sent_reset_flag_` | Sequence check | `logon_inbound_advanced_init` | Unit targets |
|---|---|---|---|---|
| false | any | `check_inbound(seq)` against N (today). A too-low result runs the mTLS gate, then takes the D5 Logout | as today | no unit |
| true | true (strict, or a knob at `{1,1}`) | `check_inbound(seq)` against N = 1 (today) | as today | as today (unit, then the mTLS gate, unchanged) |
| true | false (**unasked**) | **none** | `seq == 1` | in: 2 if consumed, else 1; out: 1; the unit runs after the mTLS gate |

With rol set, a Logout (35=5) in LogonSent is handled ahead of `interpret_logon` (arm S): marker `pending`; on an
mTLS session the Logon path's mTLS gate, both arms: with `live_peer_id_` set it calls `authorize` (on an initiator
that identity is set only by `install_reconnected_transport`, after `reconnect_fsm.cpp`'s step 7 authorised it), and
with mTLS and no live identity, the case of a direct initiator started by `open()`, it fails closed with the same event and no `authorize`
call. An unauthorised identity, or none, draws the gate's `session_event_compid_authorization_failed` and no reset
(contract O19); otherwise `Disconnected`, marker
`reached`, and the unit (contract RR-1). Either way the arm records `Disconnected`, returns success and sends no
Logout. With rol off the branch does not run: `interpret_logon` refuses the frame as a non-Logon, the arm records
`Disconnected` and returns success, with no mTLS gate (no `authorize` call) and no event, as the base (contract RR-1 "Arm S's
branch and the knob", W11).

## The too-low Logout text

One file-local builder fills a fixed `std::array<char, K>` with `"MsgSeqNum too low, expecting " + N + " but
received " + M`, the numbers written with `std::to_chars`. It is used by 103's Arm B and by both Logon arms. K is
fixed by Arm B's existing `static_assert` (two phrases plus two `digits10 + 1` numbers). The builder takes N and M as
`seqnum_t`, and every caller reads them as Arm B does (`seqnum_mgr_.next_inbound_unsafe()` and
`parse_seqnum(hdr.msg_seq_num)`), so that one `static_assert` bounds every caller and no caller adds a truncation
path. No number is written into a comment.

## `replay_outbound_range_`: the end model, amended (D3.3a)

Inside the walk, after the gate's shared-hold wait and the store tail read, `end_is_through_current` becomes
`end_is_through_current || requested_end >= next`, where `next` is the tail read's next outbound number (or
`peek_outbound()`, read there, when there is no store). The callers do not change: the ResendRequest answer passes
`rr_end == 0`, and the 789 walk already passes `true`. 027 C3's explicit-end case is superseded by name (contract
"Superseded by name"); the rest of 027's two-value end model is unchanged. The effect is on the empty-range GapFill:

| Case | Base GapFill NewSeqNo | 104 |
|---|---|---|
| EndSeqNo = 0 | `peek_outbound()` | same |
| EndSeqNo ≥ next outbound (999999 on FIX.4.0/4.1 included) | `requested_end + 1` when `our_last == 0` or `begin > eff_end` | `peek_outbound()`; with BeginSeqNo above it the GapFill is (34=B, 36=next), NewSeqNo below MsgSeqNum, as QuickFIX C++ and QuickFIX/J |
| EndSeqNo < next outbound | `requested_end + 1` | same |

The comment on the end model names 104 and keeps the formula. It gives no example values.

## B&L delta (text for phase 9)

New rows:
- **B-104-1** — with `reset_on_logout`, the numbers reset when a Logout received from the peer is processed as far
  as its reset point: a request in Active or LogonReceived, after the reply; the response to fixpp's Logout, in
  LogoutSent; a too-high Logout, at its acknowledgement; a Logout refusing fixpp's Logon, in LogonSent (no Logout
  is sent there). A received Logout whose processing ends before that point (a `fromAdmin` throw or reject, a late
  parse failure, a failed inbound persist, a reply whose `toAdmin` throws or whose number cannot be assigned, a
  too-high Logout left unacknowledged, a Logout in LogonSent on an mTLS session whose identity is not authorised
  (no reset; the gate's event), a receipt still short of that point when `close()` could not wait for it,
  B-104-2 stating when) does not reset; only
  `reset_on_disconnect` then resets, at teardown. The
  session records Disconnected first and then resets, so `onLogout` sees the numbers from before the reset. A
  cancellation that a direct C++ caller emits on `on_inbound_frame`'s slot and that is only recorded, not acted on,
  by the reset point (for example one emitted from inside `onLogout`) is dropped there (104 contract RR-2 item 3):
  the reset is made, and the arm returns as it does with no emission; one that ends a store call the arm is
  suspended in ends the receipt before its reset point (104 contract O9, O15), with no reset. The
  `sequence_numbers_reset{by_peer_request = false}` event that the Active arm and the too-high Logout's
  acknowledgement emit on every acknowledged peer Logout is unchanged and fires with or without the knob; it does
  not by itself mean that a reset was made. A
  Logout fixpp sends never resets, answered or not: a graceful close whose Logout goes unanswered, by the timeout
  or a dropped connection, keeps the numbers. A Logout received in NotConnected does not reset. Supersedes 024
  C3.1 and B-538-7's "answered or not". As QuickFIX C++, QuickFIX/J and QuickFIX/Go, except the departures DQ-9
  to DQ-14 of 104's spec, DQ-12a included.
- **B-104-2** — `close()` waits, before the teardown reset decision, for a
  received Logout that is between its receipt and its reset point, and for any 141=Y reset unit in flight (a
  receipt reset or a Logon's), whatever the knobs. The wait is bounded by `logon_timeout_ms` and needs a clock. A
  too-high Logout whose acknowledgement is already under way is not in it: `close()` waits for that one, and for its
  reset, by joining its bound, as before 104.
  The wait ends as soon as that Logout leaves the interval, whatever ended it, with no expiry event. While it
  waits, the session's executor runs one posted turn per pass, for up to `logon_timeout_ms` (its default:
  B-093-4) when a store call is held. Each pass is one posted handler, so other sessions on the same io_context
  threads run between passes; the cost to them is the thread time the passes take, a busy wait for the hold's
  duration. Where it
  cannot wait (no clock, or the bound expired), a received Logout still short of its reset point is not reset
  under `reset_on_logout`, and its processing issues no store call afterwards. A store call it had already issued
  completes after `close()` returned: a held reply is stored, and is transmitted only on a config-time
  `transport_send` sink (as B-563-5); a held inbound persist lands (B-598-1). Where the held call is the reply's
  store, the inbound persist after it is refused, so the store keeps NextNumIn at the peer's Logout's own number
  and the next session over that store expects that number again (104 contract O13-r). Its other processing then runs after
  `close()` returned, inside fixpp#597, which 104 neither widens nor closes. A received Logout past its reset point
  is never lost: its reset started in the same step. `Engine::stop()` waits for such a Logout, and for its reset,
  before it closes the session: it joins the session's role loops first (`L-563-4`), and they await the Logout's
  processing. That wait has no bound and does not depend on the clock. Under stop a received Logout is reset if and
  only if its processing reaches its reset point, however long its store call is held, and it is never closed out;
  a held reply is stored and not transmitted, because stop has already closed the transport. A store call that
  never completes hangs `stop()` (`L-093-12`). Supersedes 093 C-6's "`close()` waits only when it is about to
  issue its teardown reset".
- **B-104-3** — with `reset_on_logout` and `reset_on_disconnect` both set, a received peer Logout makes two store
  resets, at its reset point and at teardown. C5.1 still allows one per teardown. The numbers end at 1. As
  QuickFIX C++ and QuickFIX/J.
- **B-104-4** — a lenient or unilateral initiator accepts an unasked 141=Y acknowledgement whatever NextNumIn was.
  At 34=1: inbound next 2, outbound next 1, Active. At 34 = M > 1: inbound next 1, outbound next 1, Active, and
  ResendRequest from 1 on the peer's next frame. No second Logon is sent. With 789 enabled, a 789 above 1 then
  draws "NextExpectedMsgSeqNum too high". On a direct mTLS initiator the reset is made only after the peer's
  identity is authorised for the CompID. Retires L-103-2.
- **B-104-5** — a ResendRequest whose EndSeqNo is at or above fixpp's next outbound number is answered as one
  through-current. An empty-range GapFill's NewSeqNo is the next outbound number, never EndSeqNo + 1. With
  BeginSeqNo above the next outbound number that GapFill is (34 = BeginSeqNo, 36 = next outbound), its NewSeqNo
  below its own MsgSeqNum: the frame QuickFIX C++ and QuickFIX/J send; QuickFIX/Go sends none (104 spec DQ-15).
  Supersedes 027 C3's explicit-end case.
- **B-104-6** — a Logon (35=A) below NextNumIn in NotConnected or LogonSent draws a Logout with Text "MsgSeqNum too low,
  expecting N but received M", whatever its PossDupFlag, once the identity gates have passed (the mTLS
  CompID-authorisation gate, and on the acceptor `authorize_logon`). A frame they refuse is disconnected with no
  message, as FIX-TC 1S(c) requires. Then Disconnected, with NextNumIn not advanced and no 1409. When the Logout
  cannot be sent the session still ends Disconnected: a Logout that does not fit its 256-byte buffer (long
  CompIDs), a persistent store that refuses its store call, or a write that fails sends none on the wire (a failed
  write leaves the Logout stored), and the arm returns success; a volatile store that refuses it logs that and
  sends it; a `toAdmin` that throws on it sends none and the arm returns `app_callback_threw`; a failed
  `assign_outbound` sends none and the arm returns that error. It is not a `reset_on_logout` trigger. It is sent
  with `validate_sequence_numbers` off too. Retires L-103-7. Amends B-103-10's "In Active".
- **B-104-7** — C++ layout changes in two installed headers, in the shape of B-544-3 and B-103-11, before the
  first release. In `include/fixpp/session/session.hpp`, `reset_unit_wake_`, the waker of 093's event-driven
  `close()` wait, is deleted, `logout_seen_` changes from `bool` to a one-byte enum, and the private `bool`
  `wire_closed_by_reset_` is added (contract RR-6). The private `store_then_emit` and `live_write_serialized_` gain a
  defaulted exemption parameter; neither is callable outside `Session`. Whether `sizeof(Session)`
  moves, and which way, is written from phase 7's one-off size reading, base against head, never from this text,
  and the row says the size is unchanged if the reading shows no change (padding can absorb the deletion). In
  `include/fixpp/session/engine.hpp`, `Engine` gains R-1t's step-3 mark, a private test seam beside
  `test_hook_post_send_drain_`, set only through `engine_test_access`, so `sizeof(Engine)` changes. A C++ consumer
  that compiles against either installed header observes the change and must be rebuilt; as B-544-3, the row
  carries no BREAKING mark. No C-ABI change: the C ABI exposes `Session` only through an opaque handle and
  `Engine` only through the opaque `fixpp_engine_t`. Re-derive the members: every added or removed non-static data
  member in `git diff origin/main...HEAD -- include/fixpp/session/engine.hpp include/fixpp/session/session.hpp`. No test or
  source pins either size: `git grep -nE 'sizeof ?\([^)]*\b(Engine|Session)\b' -- tests include src` has no hit at
  the base, and the same pattern with `FieldRef` in place of the two names hits (its known positive); the full
  `-k 0` build is the check that nothing else depends on it. In `include/fixpp/session/seqnum_manager.hpp`
  (since the T011 amendment, 104 contract RR-5), `SeqnumManager` gains a private freeze flag, the passkey-gated
  `freeze` / `thaw` with their `freeze_key` type, and the passkey-gated query `frozen`, and `take_back_outbound`'s
  inline body gains the flag's test. `frozen` is an inline member function and adds no data member, so only the
  flag can move `sizeof(SeqnumManager)`. So
  `sizeof(SeqnumManager)` may change, and `sizeof(Session)` with it, since `Session` holds the manager by value. The
  direction of each is written from phase 7's size reading, never from this text. No public function's signature
  changes, and `freeze`, `thaw` and `frozen` cannot be called outside `Session` (their key cannot be constructed
  there). No
  test or source pins `sizeof(SeqnumManager)`: `git grep -nE 'sizeof ?\([^)]*SeqnumManager' -- tests include src`
  has no hit at the base. The C ABI does not expose `SeqnumManager`.
- **B-104-8** — with `reset_on_logout`, from a received Logout's reset point, or from `close()`'s close-out of a
  received Logout it could not wait for, until the connection ends, nothing but a reset changes the session's
  sequence numbers. A frame or a send that was already in progress at that point, and that later tries to number
  a message, apply a GapFill or SequenceReset's NewSeqNo, or reconcile its outbound number after a store failure,
  is refused. In particular:
  - its late Reject or other reply is not numbered, stored or sent, and `toAdmin` is not called for it. The
    ResendRequest such a frame would send for a too-high ResendRequest is not built, and its `toAdmin` is not
    called (104 contract RR-5 item 6, "The leaf entry test");
  - a GapFill's NewSeqNo is not applied;
  - a failed send keeps the store's error and is not transmitted;
  - nothing more is transmitted on that connection after a received Logout's reset point (not after a close-out).
    A frame already numbered and stored then is not sent; its store write came before the reset's, which clears
    it. The one exception: when the peer's Logout
    crossed fixpp's own graceful-close Logout, which was already numbered, that Logout is sent, whether the peer's
    Logout was in sequence or too high (104 contract RR-6 item 4). The wire then
    carries exactly one Logout, fixpp's own (104 contract RR-6).
  - a frame held back this way ends as a success that was not transmitted, wherever it was held.
    - `Session::send` for a send in flight at that point returns success, and its message is not sent. Before 104 it
      was transmitted at its pre-reset number.
    - `Session::send` returning success there departs from QuickFIX/J, whose `send` returns `false` for a message
      persisted and not sent (spec DQ-18). QuickFIX C++ and QuickFIX/Go return success.
    - A direct initiator's `open()` returns success with the session already Disconnected
      and its Logon not sent. This happens when the peer's Logout reached the LogonSent arm while that Logon was in
      its store call. On that path `open()` fires `onCreate` once, with the session already Disconnected, and fires no
      `onLogout`.
    - No `Disconnected` entry is added for such a frame (104 contract RR-6 item 3).
  The numbers stay at 1 after a receipt reset, and at their values after a close-out. The teardown's reset under
  `reset_on_disconnect` still runs. The session's state history (`Session::fsm_visit_history()`) records one
  `Disconnected` for an ending whose number or NewSeqNo is refused, and for any ending where a message cannot take
  a number while the session is already Disconnected, for example after `close()` has drained the manager (104
  contract RR-5 item 6). It also records one when a frame still in progress as a too-high Logout's bound acknowledges
  then ends the session itself (a SendingTime or OrigSendingTime Reject followed by its Logout), with or without
  `reset_on_logout`. A send, liveness frame or reply whose own store call failed keeps its handling of that
  store error. When the session is already Disconnected, that handling can add a second `Disconnected` entry, as
  before 104. With `reset_on_logout` off nothing is refused. On a too-high Logout's route the
  frame in progress is one that 103's bound let in after the Logout, which QuickFIX never processes. Nothing of it
  is sent after the reset point (contract RR-6), as in QuickFIX, on the wire. The `send` result is DQ-18. QuickFIX's own user-thread sends (C++, J) and
  QuickFIX/J's timer thread can still race past its reply, so this suppression falls within QuickFIX's behaviour
  (104 research D11). FR-019, contract RR-5, RR-6.

New limitations:
- **L-104-1** — a ResendRequest answered when fixpp has sent nothing at or above BeginSeqNo writes a GapFill whose
  NewSeqNo equals its MsgSeqNum, (1, 1) right after an unasked reset. QuickFIX C++ and QuickFIX/J send none at
  next outbound 1, and QuickFIX/Go none at all. The receiver treats it as a no-op.
- **L-104-2** — a failed receipt reset is logged. On a persistent store that refused, the stored numbers keep
  their pre-reset values until the next reset (as the teardown reset, I-07; as QuickFIX/Go). Not a departure
  against QuickFIX C++ either: its `next` catches the store's `IOException` from `nextLogout`'s `m_state.reset`,
  logs it and disconnects (read at v1.16.0). QuickFIX/J's `SessionState.reset` rethrows the store's `IOException`
  as a `RuntimeError` out of `nextLogout` (`throw new RuntimeError(e);`), before `nextLogout`'s
  `disconnect(msg, false)`. `Session.next(Message, boolean)`'s `catch (Throwable t) { // QFJ-572` rethrows it with
  `rejectMessageOnUnhandledException` at its default, false (`throw new RuntimeError(t);`), and the event-handling
  strategy's `catch (Throwable e)` around `quickfixSession.next(message)` logs it with `LogUtil.logThrowable`
  (`SingleThreadedEventHandlingStrategy.SessionMessageEvent.processMessage`, and
  `ThreadPerSessionEventHandlingStrategy.MessageDispatchingThread.doRun`, "Error during message processing"):
  logged, with that Logout's `disconnect` not reached (read at `QFJ_RELEASE_3_0_2`).
- **L-104-3** — on an mTLS acceptor, the posture, strict and SendingTime refusals and the 1137 Reject are still
  sent before the CompID-authorisation gate, and on a direct mTLS initiator the posture and SendingTime Logouts
  are still sent before its gate (`on_inbound_logon_sent_` runs both ahead of its mTLS gate), so a
  TLS-validated peer that is not authorised for the CompID can receive them, and each consumes an outbound number.
  FIX-TC 1S(c) asks for a disconnect with no message there; FIX-TC 1B has no identity sub-case, so on the initiator
  this is a departure from 1S(c)'s principle (clarification N-8, design ruling 5), not from a test case's text.
  This is pre-existing; 104's too-low Logout and the unasked reset come after the gates. An engine-managed
  initiator authorises the peer at connect, before its Logon, so the initiator half applies to a direct session only.
- **L-104-4** — a too-low Logon with 43=Y draws the Logout (FIX-SL §4.3.11, QuickFIX/Go). QuickFIX C++ and
  QuickFIX/J validate PossDup and then wait for their logon timeout.
- **L-104-5** — after an unasked 141=Y acknowledgement at 34 = M > 1, fixpp's ResendRequest waits for the peer's
  next frame. QuickFIX/J and QuickFIX/Go send it at Logon, and QuickFIX C++ continues at inbound 2.
- **L-104-6** — with 789 enabled, an unasked 141=Y acknowledgement's 789 is evaluated after the reset, as
  QuickFIX C++ and QuickFIX/J do. QuickFIX/Go skips 789 when 141 is present.
- **L-104-7** — against a counterparty acceptor whose `toAdmin` injects 141=Y into its Logon acknowledgement, the
  session ends when fixpp's outbound number reaches the number of fixpp's own Logon: the acceptor queued that
  Logon and consumes it by incrementing past it, so fixpp's own frame at that number reads as too low. Every
  QuickFIX initiator meets the same ending, and QuickFIX C++'s second Logon does not clear the queue, which only
  `disconnect()` clears. Derived from QuickFIX C++ v1.16.0's source (`nextLogon`, `doTargetTooHigh` →
  `m_state.queue`, `nextQueued`, `disconnect()`), not traced on a live peer. 104 spec DQ-16.
- **L-104-8** — a Logon acknowledgement refused at 099's S3 test (a validating initiator's DefaultApplVerID or
  ApplVerID check) consumes its MsgSeqNum; QuickFIX's Reject path exempts a Logon (`generateReject`). This is the
  ordinary acknowledgement without 141=Y, which Guard (4)'s `check_inbound` still consumes before the S3 test. An
  unasked 141=Y acknowledgement takes no sequence check, so it is refused there with NextNumIn unchanged, as
  QuickFIX does. Not fixed in 104; T103 files the issue (orchestrator ruling 1 on the T071 STOP).

Amended in place:
- **B-024-1**: its "a Logout teardown (sent OR received)" points to B-104-1 (the trigger is a received Logout at
  its reset point).
- **B-538-7**: the title's clause "a Logout that was sent still resets, answered or not" is replaced whole with
  "since 104 a Logout that was sent never resets; a received one resets at its reset point (B-104-1)". The check
  is T101's re-run of D8's census, whose key `was sent still resets` must no longer hit the row. Each other sentence
  that states the superseded rule is quoted and replaced in the amendment, as 103 did for B-538-2:
  - "`run_logout_phase1` sets `logout_seen_` only after its Logout's emit returned success, and the teardown reset
    in `Session::close` runs when that flag and `reset_on_logout` are both set; the other place that sets the flag,
    the receipt of the peer's Logout, is unchanged" becomes "since 104, `run_logout_phase1` does not set
    `logout_seen_`, and under `reset_on_logout` the reset is made at a received Logout's reset point, never at
    teardown (B-104-1)";
  - "A Logout that was sent and never answered still resets: fixpp#595." becomes "Since 104, a Logout that was
    sent and never answered does not reset (B-104-1; fixpp#595, resolved by 104)."
  The row's not-sent half is unchanged.
- **B-563-5**: the clause "does not count as a Logout that was sent, so a configured `reset_on_logout` does not
  fire" keeps its ending; its rationale points to B-104-1 (since 104 no Logout fixpp sends resets). Its "a Logout
  from the peer that arrives then brings no reset" stays true under 104: the cut route records Disconnected in
  `run_logout_phase1` before the Logout takes its number, so the session never enters LogoutSent and a peer Logout
  reaches the Disconnected row, which drains it (arm L is not reached).
- **B-093-11**: points to B-104-2 for when `close()` waits.
- **L-093-12**: a pointer to B-104-2. The row names the reset unit's store operation; the amendment adds that a
  received Logout's reply store and its inbound persist hold `stop()` the same way, and that this is not new with
  104: a pump frame held in any store call already held `stop()`'s role-loop join before 104, so only the row's
  text widens (104 contract RR-2 item 4, "Under `Engine::stop()`").
- **B-103-9**'s disclosure: "and a Logout seen on the old connection still triggers `reset_on_logout`'s teardown
  reset" is replaced by "a peer Logout received on the old connection reset the numbers at its reset point, before
  the reconnect; nothing received there resets the new connection (B-104-1)".
- **B-103-10**: "In Active" becomes "In Active, and since 104 in the Logon arms (B-104-6)". The title's "without
  PossDupFlag(43)=Y" gains "(in Active; in a Logon arm whatever its 43, B-104-6, erratum E-2)". Its closing
  sentence "A too-low frame in a Logon arm still ends silently: `L-103-7`." is replaced by "Since 104 a too-low
  Logon in a Logon arm draws the same Logout (B-104-6)", so the row no longer cites L-103-7, which 104 retires.

Retired, moved to `spec/behaviors-and-limitations-closed.md`:
- **L-103-2**, resolved by B-104-4.
- **L-103-7**, resolved by B-104-6. 103's spec row "A too-low frame in NotConnected or LogonSent still ends
  silently" is superseded by name there.
