# Data Model: 104 sequence-reset fidelity

All the state below is private to `Session` and lives on the session strand. No public type, configuration key or
default changes. No `Session` member is added or renamed (the one `Engine` member is B-104-7). One member keeps
its name and changes its type and contract (`logout_seen_`, `bool` → a one-byte enum). One member is deleted: `reset_unit_wake_`, the waker of 093's
event-driven wait, which `close()`'s poll replaces (contract RR-2 item 4; re-derive its users with
`git grep -n reset_unit_wake_ -- src include tests`). So `sizeof(Session)` is expected to shrink, an expectation that phase 7's one-off size reading settles. There is no pin on
it to move (re-derive: `git grep -n "sizeof(Session)\|sizeof(fixpp::session::Session)" tests include src`).
Decisions are in [research.md](research.md); the observable contract is in
[contracts/session-wire-endings-104.md](contracts/session-wire-endings-104.md) (Part 1: RT-1, RR-1 to RR-4).

## `logout_seen_` (name kept; type and contract changed): the receipt marker

A one-byte enum with three values. It replaces the `bool` whose contract was "a Logout in either direction".

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
| `install_reconnected_transport` | `none` |

- **Invariant RO-1.** With rol set, the marker is `pending` exactly while one awaited read-pump frame of a receipt
  arm (A, L, S, or B's immediate acknowledgement) is between its receipt and its reset point or exit. That frame
  is its one owner (contract INV-P). It leaves `pending` once, to `reached` (the frame at its reset point, or
  `close()` closing out a receipt it could not wait for, with no reset) or to `none` (the frame's owner).
  - A too-high Logout's detached bound never makes it `pending`: its owed receipt is the 103 mark.
  - The bound writes the marker once, `none` → `reached` at its reset point (contract INV-M, INV-X).
  - With rol off the marker stays `none`, so 104 adds no wait, reset or refusal to such a session.
- **Invariant RO-2.** No Logout that fixpp sends changes the marker. FR-015 follows, and B-538-7's first half
  still holds.
- **Invariant RO-3.** A receipt reset under rol has one owner, the arm's unit, which `close()` waits for: by its
  wait (contract RR-2 item 4), or for the bound's unit by 103's bound join, which precedes that wait. The
  teardown never resets a `pending` receipt (RU-1). The one case it would own, `reached` with the unit not started,
  has no input because the reset point is one strand turn. An arm that finds the marker `reached` at its reset
  point runs no unit, and issues no store call from `reached` on (contract RR-2 item 5).
- The header comment states RO-1 to RO-3, names 104 and the 024 T013 / C3.1 clause it supersedes, and gives the
  recipe `git grep -n logout_seen_ -- src include`. It contains no count of sites (FR-006).
- **Readers of the member outside `session.cpp`** (re-derive: `git grep -n logout_seen -- include src tests bindings
  tools`). At the base there is no code reader outside `session.cpp`: the hits are comments, in `session.hpp`
  (the member's own comment, and the paragraph saying the latches including `logout_seen_` stay set across a
  reconnect), `session_config.hpp`'s knob comments, and comments in `test_reset_on_lifecycle.cpp`. So the type
  change compiles against no test; each comment is a census hit (research D8) and is reworded.
- **Order against an in-place reconnect.** `drive_reconnect` drains, then `reconnect_fsm_.drive_reconnect_attempt()`
  runs `install_reconnected_transport` (which sets `none`), then `emit_initiator_logon_`. So the reconnect Logon's
  `store_then_emit` never meets a `reached` marker.

## The reset unit: new callers

`run_reset_unit_(next_in, next_out)` (093 data-model E-10, contract C-6) is unchanged but for one deletion, its
call of `reset_unit_wake_` (below). It gains callers:

| Caller | Targets | Before the call | After the call |
|---|---|---|---|
| arms A, L, B, S at their reset point (rol set) | `(1, 1)` | `Disconnected` recorded (RR-3); the thread's cancellation state reset; the marker `pending` → `reached` (B's bound: `none` → `reached`); no unit if it was already `reached` (RR-2 item 5) | `reset_cancellation_state(enable_total_cancellation{})`; a manager or store error is logged; the arm continues as it does today (L: `logout_confirmed_` and phase 1's wake) |
| `on_inbound_logon_sent_`, the unasked branch (D3.1) | `(2, 1)` when the acknowledgement's 34 is 1; `(1, 1)` otherwise | the mTLS CompID-authorisation gate passed (clarification N-8) | today's step 5 to step 7 (030's persistent-fatal disposition) |

`reset_unit_in_flight_` is unchanged, and `close()` reads it earlier (below). `reset_unit_wake_` and its call in
`run_reset_unit_` are deleted (contract RR-2 item 4).

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

A Logout (35=5) in LogonSent is handled ahead of `interpret_logon` (arm S): marker `pending`; on an mTLS session
the live identity's authorisation; then `Disconnected`, marker `reached`, and the unit under rol (contract RR-1).

## The too-low Logout text

One file-local builder fills a fixed `std::array<char, K>` with `"MsgSeqNum too low, expecting " + N + " but
received " + M`, the numbers written with `std::to_chars`. It is used by 103's Arm B and by both Logon arms. K is
fixed by Arm B's existing `static_assert` (two phrases plus two `digits10 + 1` numbers). No number is written into a
comment.

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
  too-high Logout left unacknowledged, a receipt still short of that point when `close()` could not wait for it
  because there was no clock or its `logon_timeout_ms` bound expired) does not reset; only
  `reset_on_disconnect` then resets, at teardown. The
  session records Disconnected first and then resets, so `onLogout` sees the numbers from before the reset. A
  Logout fixpp sends never resets, answered or not: a graceful close whose Logout goes unanswered, by the timeout
  or a dropped connection, keeps the numbers. A Logout received in NotConnected does not reset. Supersedes 024
  C3.1 and B-538-7's "answered or not". As QuickFIX C++, QuickFIX/J and QuickFIX/Go, except the departures DQ-9
  to DQ-14 of 104's spec.
- **B-104-2** — `close()` waits, before the teardown reset decision, for a
  received Logout that is between its receipt and its reset point, and for any 141=Y reset unit in flight (a
  receipt reset or a Logon's), whatever the knobs. The wait is bounded by `logon_timeout_ms` and needs a clock. A
  too-high Logout whose acknowledgement is already under way is not in it: `close()` waits for that one, and for its
  reset, by joining its bound, as before 104.
  The wait ends as soon as that Logout leaves the interval, whatever ended it, with no expiry event. While it
  waits, the session's executor runs one posted turn per pass, for up to `logon_timeout_ms` when a store call is
  held. Where it
  cannot wait (no clock, or the bound expired), a received Logout still short of its reset point is not reset
  under `reset_on_logout`, and its processing issues no store call afterwards. A store call it had already issued
  completes after `close()` returned: a held reply is stored, and is transmitted only on a config-time
  `transport_send` sink (as B-563-5); a held inbound persist lands (B-598-1). Its other processing then runs after
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
  below its own MsgSeqNum: the frame QuickFIX C++ and QuickFIX/J send. Supersedes 027 C3's explicit-end case.
- **B-104-6** — a frame below NextNumIn in NotConnected or LogonSent draws a Logout with Text "MsgSeqNum too low,
  expecting N but received M", whatever its PossDupFlag, once the identity gates have passed (the mTLS
  CompID-authorisation gate, and on the acceptor `authorize_logon`). A frame they refuse is disconnected with no
  message, as FIX-TC 1S(c) requires. Then Disconnected, with NextNumIn not advanced and no 1409. The endings when
  the Logout cannot be sent are those of research D5's table. It is not a `reset_on_logout` trigger. It is sent
  with `validate_sequence_numbers` off too. Retires L-103-7. Amends B-103-10's "In Active".
- **B-104-7** — `sizeof(Session)` changes (its direction is written from phase 7's one-off size reading, base
  against head, never from this text) and `sizeof(Engine)` changes, C++ layout changes in installed headers,
  in the shape of B-544-3 and B-103-11. `reset_unit_wake_`, the waker of 093's event-driven `close()` wait, is deleted; `logout_seen_` changes
  from `bool` to a one-byte enum. A C++ consumer that compiles against the installed header observes it and must be
  rebuilt; as B-544-3, the row carries no BREAKING mark. No C-ABI change: the C ABI exposes `Session` only through
  an opaque handle. `sizeof(Engine)` changes too, a C++ layout change in the installed header
  `include/fixpp/session/engine.hpp`, before the first release: `Engine` gains R-1t's step-3 mark, a private test
  seam beside `test_hook_post_send_drain_`, set only through `engine_test_access`. A C++ consumer that compiles
  against the installed header observes it and must be rebuilt; the row carries no BREAKING mark for it either. No
  C-ABI change: the C ABI exposes `Engine` only through the opaque `fixpp_engine_t`. Re-derive the members: every
  added non-static data member in `git diff origin/main...HEAD -- include/fixpp/session/engine.hpp`. No test or
  source pins either size: `git grep -nE 'sizeof ?\([^)]*\b(Engine|Session)\b' -- tests include src` has no hit at
  the base, and the same pattern with `FieldRef` in place of the two names hits (its known positive); the full
  `-k 0` build is the check that nothing else depends on it.

New limitations:
- **L-104-1** — a ResendRequest answered when fixpp has sent nothing at or above BeginSeqNo writes a GapFill whose
  NewSeqNo equals its MsgSeqNum, (1, 1) right after an unasked reset. QuickFIX C++ and QuickFIX/J send none at
  next outbound 1, and QuickFIX/Go none at all. The receiver treats it as a no-op.
- **L-104-2** — a failed receipt reset is logged. On a persistent store that refused, the stored numbers keep
  their pre-reset values until the next reset (as the teardown reset, I-07; as QuickFIX/Go).
- **L-104-3** — on an mTLS acceptor, the posture, strict and SendingTime refusals and the 1137 Reject are still
  sent before the CompID-authorisation gate, so a TLS-validated peer that is not authorised for the CompID can
  receive them, and each consumes an outbound number. FIX-TC 1S(c) asks for a disconnect with no message there.
  This is pre-existing; 104's too-low Logout and the unasked reset come after the gates.
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
  `m_state.queue`, `nextQueued`, `disconnect()`), not traced on a live peer.

Amended in place:
- **B-024-1**: its "a Logout teardown (sent OR received)" points to B-104-1 (the trigger is a received Logout at
  its reset point).
- **B-538-7**: "answered or not" struck, pointing to B-104-1.
- **B-563-5**: the clause "does not count as a Logout that was sent, so a configured `reset_on_logout` does not
  fire" keeps its ending; its rationale points to B-104-1 (since 104 no Logout fixpp sends resets). Its "a Logout
  from the peer that arrives then brings no reset" stays true under 104: the cut route records Disconnected in
  `run_logout_phase1` before the Logout takes its number, so the session never enters LogoutSent and a peer Logout
  reaches the Disconnected row, which drains it (arm L is not reached).
- **B-093-11**: points to B-104-2 for when `close()` waits.
- **L-093-12**: a pointer to B-104-2. The store operations a held receipt makes `stop()` wait for are now also a
  received Logout's reply store and its inbound persist, not only the reset unit's.
- **B-103-9**'s disclosure: "and a Logout seen on the old connection still triggers `reset_on_logout`'s teardown
  reset" is replaced by "a peer Logout received on the old connection reset the numbers at its reset point, before
  the reconnect; nothing received there resets the new connection (B-104-1)".
- **B-103-10**: "In Active" becomes "In Active, and since 104 in the Logon arms (B-104-6)".

Retired, moved to `spec/behaviors-and-limitations-closed.md`:
- **L-103-2**, resolved by B-104-4.
- **L-103-7**, resolved by B-104-6. 103's spec row "A too-low frame in NotConnected or LogonSent still ends
  silently" is superseded by name there.
