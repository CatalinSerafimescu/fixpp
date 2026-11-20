# Contract: transport and liveness (105)

**Feature**: [../spec.md](../spec.md) · **Decisions**: [../research.md](../research.md) · **State**:
[../data-model.md](../data-model.md)

The observable rules of this feature, by ending class. "The peer observes EOF" means the peer's read of the
connection ends (for TLS, after a close_notify where C-8 says so). "At once" means with no further mock-clock
advance and no further inbound frame or local API call: the close is initiated when the executor has run until it
has no ready work, and the peer observes it on the plain transport then; on TLS the peer observes it once
`close_async` has completed, which is bounded by `tls_close_timeout` (wall time). One definition, used by SC-001 too.
A cell for such a rule rules out the wall-clock timers that can also close a connection while the mock clock is
frozen: the TLS handshake and first-frame deadlines, `close()`'s grace timer (`close_grace`), `drain_for_reconnect_`'s
bound (each an `asio::steady_timer` on wall time), and `asio_tls_transport::close_async`'s `close_deadline`
(`std::chrono::steady_clock::now() + tls_close_timeout`, after which it closes abortively). Every rule
names the requirement it discharges. Internals named here are the design's; a test asserts the observable, never
the name.

## C-1. Every ending closes the connection (FR-001, FR-002, FR-005, FR-008)

| # | Condition | Wire, from fixpp | The connection closes | Notes |
|---|---|---|---|---|
| 1 | An inbound frame's arm records `Disconnected` with no Logout sent and returns success (after Active: TestReqID mismatch, BeginString or CompID guard, MsgSeqNum 0, D-3, a too-low frame whose Logout did not fit, a reply leaf whose number assignment failed) | nothing more | at once, after the arm returns | no event, as today; inside an R-2 wait a peer-frame condition is absorbed (C-2 W-8) and an internal failure ends the wait (W-9) |
| 2 | An inbound frame's arm returns an error | whatever the arm wrote | at once, as today | unchanged |
| 3 | The peer's Logout in Active or LogonReceived (a request) | the reply Logout | after the reply, the persist and, under `reset_on_logout`, the receipt reset unit | B-104-1's order kept; `onLogout` before the reset; the reply's write is limited (C-2.7) |
| 4 | The peer's Logout in LogoutSent (`close()`'s exchange) | nothing | by `close()`, as today | excluded class X |
| 5 | A too-high Logout's acknowledgement (by the bound or at once) | the reply Logout | after the reply and the reset unit | from the bound's exit, or the pump |
| 6 | fixpp's own Logout that ends the session (C-2's list) | that Logout | by C-2 | |
| 7 | The unanswered TestRequest (R-1) | the Logout of C-3 | at once after its write, or at the write limit (C-2.7) | not C-2's wait |
| 8 | The liveness loop's `toAdmin` throw, number-assignment failure or store-and-emit failure | nothing, or a partly sent frame | at once after the loop exits | |
| 9 | An application `send()` or `Engine::send` ending the session (a store or emit failure) | nothing more | at once after the call returns, or after the arm in flight returns | |
| 10 | A direct initiator's Logon emission failing in `Session::open` or `drive_reconnect` | nothing more | at once after the call returns | the Engine's connect loop also closes, as today |
| 11 | A refusal before Active that sends nothing (C-4) | nothing | at once after the frame | not an establishment timeout |
| 12 | A refusal before Active that sends a Logout | that Logout | by C-2's pre-Active rule | |
| 13 | `close()` (graceful or terminal) | as today | as today | class X |
| 14 | `drive_reconnect`'s drain from Active | as today | as today (B-103-9's bound) | class X |

Common rules:
- **C-1.a One effective close per ending.** A connection is closed through `close_async()` (C-8), and the first
  call is the one that closes it; a later call on the same transport (the pump's `close(terminal)` after a
  write-limit close, `Engine::stop()`'s step 2) is subsumed, as `close_async`'s "Idempotent, exactly like close()"
  makes it (research F-5). The design owes at most one close per connection: the close is owed only on a
  transition into `Disconnected`, and a write of `Disconnected` over `Disconnected` owes nothing (research D1;
  data-model E-2 I-5). An `onLogout` callback that starts `close()`, and an `Engine::stop()` that races the
  close, produce no second close and no use after close.
- **C-1.b The session's own work runs to its end.** No close in this table runs while a frame is inside an arm,
  with one exception: the write limit of C-2.7 closes the transport while an arm is suspended in an ending
  Logout's write or queued behind another write at the session's write gate. It cuts no store work: a store call
  completes before its frame's transmit, the receipt reset unit is store-only, and the close only makes the
  pending writes complete with an error. Otherwise a store call issued before the close returns before it; the
  receipt reset unit runs before rows 3 and 5 close.
  `close()`'s bounded waits and `Engine::stop()`'s waits keep their bounds (B-104-2).
- **C-1.c An Engine session ends terminally** at every row, as it ends at EOF today: its pump closes it with
  `close(terminal)`. **A direct Session** (no Engine pump; the caller drives `on_inbound_frame` itself) has its
  transport closed and stays open in `Disconnected`, so `drive_reconnect` can reconnect it. On a direct Session the
  rows reached from a detached root or an application call (5 by the bound, 7 to 10, and C-2's bound) close at
  that root's exit; the rows reached from an inbound frame (1 to 3, 11, 12) have no pump to consume them, and close
  at the exit of a later, separately started detached root or application call, at `close()`, or at
  `drive_reconnect`; the `on_inbound_frame` call that reached the ending does not close it (L-105-b).
- **C-1.d** `onLogout` fires at the `Disconnected` edge, as today, once; during an R-2 wait after Active, that
  edge is the wait's end (C-2.1).

## C-2. The R-2 wait (FR-004, FR-004a)

**Which Logouts.** Every Logout fixpp sends to end the session, before or after Active, except R-1's: the
gap-deadline Logout (B-537-2), the SendingTime Reject's Logout, the OrigSendingTime Reject's Logout of the PossDup
arm, the too-low Logout (B-103-10), and every Logon refusal with a Logout (the too-low Logon of B-104-6, the
TestMessageIndicator posture mismatch, the strict-reset refusal, LogonSent's Logout-with-Text). Replies to the
peer's Logout are not in this list (C-1 rows 3 and 5).

**No wait is armed** when the Logout was not transmitted (it did not fit its buffer, `toAdmin` threw, its number or
store failed, its write failed, it was held back, there was no transport to take it, or the write limit of C-2.7 cut
it), when `logout_disconnect_timeout_ms` is 0, when the session has no clock, when the bound could not be spawned (an
allocation failure; the ending then proceeds as with timeout 0), when another root ended the session during the
Logout's write, or when the ending claimed a pending too-high Logout's mark (C-2.8). The session then ends as the site ends today, and the connection closes at once
(C-1 rows 1 and 11). With timeout 0 or no clock the write itself is not limited either (C-11 L-105-f).

### While the wait is armed

One table; each row is one observable rule. "Absorbed" means: no Logout written, no state change, no `onLogout`, no
second wait, and the frame not delivered.

| # | Frame or event | After Active (the session stays Active, a Logout of the session in flight) | Before Active (the session is `Disconnected`) |
|---|---|---|---|
| W-1 | The peer's Logout: no header fault, MsgType(35) third, past the validation gate where it runs, BeginString and CompIDs right, **any MsgSeqNum** | Ends the wait. No reply. When its MsgSeqNum is NextNumIn it advances NextNumIn and reaches `fromAdmin`, as an in-sequence Logout does today; otherwise neither. Under `reset_on_logout` the reset point runs (B-104-1). Then the connection closes, after the reset unit (C-1 row 3's order). | Ends the wait. Nothing delivered, nothing advanced, nothing reset. Then the connection closes. |
| W-2 | An application message at NextNumIn | Reaches `fromApp`; NextNumIn advances. | Drained. |
| W-3 | A TestRequest at NextNumIn | Answered with a Heartbeat. | Drained. |
| W-4 | A ResendRequest at NextNumIn, or above it while a resend is outstanding | Answered, as today. | Drained. |
| W-5 | A Heartbeat: a matching or absent TestReqID(112), or the 112 of an earlier TestRequest fixpp sent on this connection | The 112 rule of C-5.4: a matching or absent one answers a pending TestRequest; an earlier one neither answers nor ends. | Drained. |
| W-6 | A frame above NextNumIn with no resend outstanding | Processed as in Active: not delivered, NextNumIn kept, a ResendRequest for the gap, as today (QuickFIX C++ sends one during its wait too: `validLogonState` admits a non-Logout frame once `sentLogout` is set, and `verify` reaches `doTargetTooHigh`). | Drained. |
| W-7 | A frame while a resend is outstanding and the gap deadline has passed | The gap deadline does not fire; the frame takes the row it takes before the deadline (W-4, W-6, W-8). | Drained. |
| W-8 | A peer-frame condition that ends the session today: with a Logout (a frame below NextNumIn without 43=Y, a SendingTime Reject, an OrigSendingTime Reject) or silently (a TestReqID mismatch, a BeginString or CompID mismatch, MsgSeqNum 0, D-3). The set is defined by cause, not by the arm's return value. | Absorbed. A Reject the arm writes before its Logout is written, as in Active; its Logout and its `Disconnected` are not. NextNumIn is left as the arm leaves it. | Drained. |
| W-9 | Any internal failure that writes `Disconnected`, from any root: an arm's store, persist, number-assignment or emit failure or `toAdmin` throw, whether the arm then returns an error or success (a reply leaf's failed assignment returns success); a send admitted before the Logout whose store or emit then fails; a liveness Heartbeat or TestRequest admitted before the Logout whose emit fails; the too-high Logout bound's handler | Not absorbed: that `Disconnected` ends the wait (`onLogout` once), and the connection closes at once: by the pump after the frame (on its error or on the latch), or at the failing root's exit (C-1 rows 2, 8, 9). | Not reached (nothing is processed; the other roots do not run before Active). |
| W-10 | An application `send()` | Refused, `session_invalid_state_for_send` (103's rule for a Logout ending). | Refused (not Active), as today. |
| W-11 | The liveness loop | Originates nothing more; it leaves at its next test. | Not running. |
| W-12 | The bound expires | `Disconnected`, `onLogout`, then the connection closes. | The connection closes. |
| W-13 | `close(graceful)` | The wait runs on: `close()` proceeds at W-1 or W-12; one close, by `close()` (C-8.4). | As `close(terminal)`: `close()`'s Logout phase runs only from Active, as today. |
| W-14 | `close(terminal)`, `Engine::stop()`, `drive_reconnect`'s drain | End the wait at once; one close, by the closer. | Same. |
| W-15 | The Logout's own write has not completed at the bound (C-2.7) | No wait follows: the connection closes at the bound through `close_async()`, the ending proceeds as "Logout not sent", `Disconnected` and `onLogout` once. | The connection closes at the bound; nothing is reset. |

Common rules:
- C-2.1 After Active the session stays Active (`fixpp_session_is_established` true) from the Logout's write until the
  wait ends, and ends only at W-1, W-9, W-12, W-13 (at the wait's end), W-14 or W-15. `onLogout` fires once, at that end.
  Before Active it does not fire, as today.
- C-2.2 The cells send the peer's frames at the numbers a real peer uses. W-1: the peer's Logout below NextNumIn
  after the too-low Logout, above it after the gap-deadline Logout, at NextNumIn after the SendingTime Reject's
  Logout. W-2 to W-5 (frames at NextNumIn) are cells on the SendingTime-Reject route, where the peer's numbers are in
  sequence; on the too-low route the peer's next frames are below NextNumIn and are cells of W-8 (absorbed), and on
  the gap-deadline route they are above it (W-4, W-6, W-7).
- C-2.3 **Numbering, not wire order.** No application frame, and no Heartbeat or TestRequest of the liveness loop, is
  numbered after fixpp's Logout (W-10, W-11). A frame numbered before it and still inside its store call when the
  Logout is written can reach the wire after the Logout. On the SendingTime-Reject and gap-deadline routes the peer
  can ask for it by ResendRequest during the wait (W-4); on the too-low route its ResendRequest is below NextNumIn
  and is absorbed (W-8). Disclosed as L-105-e; it exists on `main` for every Logout ending since 103.
- C-2.4 fixpp originates nothing of its own during the wait: no Heartbeat on its cadence, no TestRequest, no second
  Logout. Replies to the peer's frames are written as in Active: a Heartbeat answering a TestRequest, a resend
  answer, a ResendRequest for a gap a frame opens (W-6), and a Reject a validation or an arm sends.
- C-2.5 **The bound.** With no Logout from the peer, the connection closes `logout_disconnect_timeout_ms` after the
  start of the Logout's write, on the session's clock: one budget for the write and the wait (C-2.7). A later send
  does not move it, and the write's completion does not restart it.
- C-2.6 **Before Active** the bound is `logout_disconnect_timeout_ms` from the start of the Logout's write. On an
  Engine-managed session the connection's establishment deadline still runs in the read pump (a local of
  `run_read_pump`, computed by the accept and connect loops from `logon_timeout_ms`), so the connection closes at the
  earlier of the two, and its expiry then closes with no `session_event_establishment_timeout` and no
  establishment-timeout record. A Session driven without an Engine has no establishment deadline, so its pre-Active
  wait ends at the bound alone. The numbers are not reset (owner ruling on OQ-1, 2026-11-20).
- C-2.7 **The write limit** (ruling D-e). Every Logout fixpp writes to end a session (R-1's, every Logout of this
  section's list, before and after Active, and the replies of C-1 rows 3 and 5) is limited: its write is raced
  against `logout_disconnect_timeout_ms` counted from the write's start, before the session's write gate is
  acquired, so a Logout queued behind another blocked write is covered too. When the write has not completed at that
  instant, the session closes the transport through `close_async()` (C-8); the blocked write then completes with an
  error, nothing is cancelled or destroyed while suspended, and the ending proceeds as "Logout not sent": no wait,
  `Disconnected` and `onLogout` once, the connection closed. With timeout 0 or no clock the write is not limited.
  `close()`, `Engine::stop()` and `drive_reconnect`'s drain during the write close the transport themselves, which
  ends the write the same way. The write starts at the Logout's owner claim, so the limit can fire while the Logout
  is still inside its number assignment or store call: the peer sees EOF at the bound, and `Disconnected` and
  `onLogout` follow when that store call returns (a store call is not cut, C-1.b). While an ending Logout is being
  written, an ending another root records (the peer's Logout answered while the Logout owner is set, an internal
  failure) does not close the connection until that write has returned, so the Logout is not cut; an arm that
  returns an error still closes at once, as on `main` (C-1 row 2).
- C-2.8 **A pending too-high Logout** (103's mark, held while a resend recovers). An ending of this list that claims
  the mark (the gap-deadline Logout and Arm B's too-low Logout claim it today) treats the peer's Logout as already
  received: it arms no wait and closes after its write, as R-1 does. The claimed receipt does not reach its reset
  point under `reset_on_logout`, as on `main`, where both sites claim and end at once.

## C-3. The unanswered TestRequest (FR-003, R-1)

- C-3.1 After 2T of inbound silence, counted from the last inbound frame that refreshes liveness, with a TestRequest
  pending and no inbound frame since it was sent (C-6.2), fixpp writes the record of C-3.3, then sends one Logout
  whose Text(58) is exactly `TestRequest unanswered, TestReqID <id>`, `<id>` being the TestReqID(112) of the
  TestRequest it sent, and the connection closes as soon as the write has returned. fixpp does not wait for the
  peer's Logout. The write is limited (C-2.7): when it has not returned `logout_disconnect_timeout_ms` after its
  start, the connection closes then, and the Logout counts as not sent.
- C-3.2 When the Logout cannot be sent, the connection still closes at the same point. Six classes, each with its own
  cell asserting the record, the wire (no complete Logout read by the peer) and the peer's EOF: the Logout does not
  fit its buffer; `toAdmin` throws on it; its number cannot be assigned; its store fails (on a persistent store the
  leaf returns the error, on a volatile one it is logged and the write proceeds, so both are cells, the second
  asserting the Logout on the wire); its write fails (the leaf discards the emit's result, so the loop must still
  record and close); its write does not complete (a peer that stops reading; C-2.7).
- C-3.3 One warn log record, `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}`, with
  `error` the numeric value of `fixpp::core::error::session_test_request_unanswered`, through the session's logger,
  written **before** the Logout's write, so a write that never completes cannot lose it (ruling D-e). Whether the
  Logout went out is observable on the wire, not in the record. No call returns that error.
- C-3.4 A peer that answers with a Heartbeat whose TestReqID(112) matches, or that carries none, is not ended. A
  Heartbeat carrying the 112 of an earlier TestRequest fixpp sent on this connection is not an answer and does not
  end the session (C-5.4).

## C-4. Refusals before Active and the acceptor's first frame (FR-006, FR-007)

- C-4.1 A refusal before Active that sends nothing closes the connection at once after the frame: 092's D-1, D-2
  and D-3, the NotConnected and LogonSent refusals, the BeginString and CompID guard, MsgSeqNum 0. What is refused
  is unchanged (092 contract C-2 D-1/D-2/D-3).
- C-4.2 Such a close is not an establishment timeout: no `session_event_establishment_timeout`, no record
  `session not established within logon_timeout_ms={}`.
- C-4.3 A peer that sends nothing, or does not complete a Logon, is still closed at `logon_timeout_ms` with that
  event and record (B-093-4).
- C-4.4 The acceptor's first frame has the disposition of every later frame: an error result closes the
  connection; a refusal closes by C-4.1 or C-2.
- C-4.5 A first frame that is accepted, followed by a later frame refused before Active: C-4.1 or C-2 at that frame.

## C-5. A Heartbeat with `validate_sequence_numbers` off (FR-009, #582)

- C-5.1 With the knob off, a Heartbeat whose TestReqID(112) matches the pending TestRequest, or that carries no 112,
  answers it whatever its MsgSeqNum(34). The session stays Active. NextNumIn does not move, and `fromAdmin` is not
  called for it, as today for an out-of-sequence Heartbeat with the knob off.
- C-5.2 With the knob off, a Heartbeat whose 112 is a mismatch under C-5.4 ends the session with no Logout, and the
  connection closes at once (C-1 row 1).
- C-5.3 With the knob on, the rules are B-537-3's and 103's, unchanged except for C-5.4.
- C-5.4 **The TestReqID(112) rule, every Heartbeat route** (ruling D-d), applied while a TestRequest is pending, as
  today: a matching or absent 112 answers it; the 112 of an earlier TestRequest fixpp sent on this connection
  neither answers it nor ends the session (the frame still refreshes liveness, as every accepted frame does); any
  other 112, one fixpp never sent on this connection, ends the session (`session_testreqid_mismatch`). "Sent on this
  connection" is `TR` followed by the canonical decimal of a TestReqID number issued since the connection started;
  a reconnect starts a new set. With no TestRequest pending the 112 is not compared, as today.

## C-6. TestRequest timing (FR-010, FR-011, FR-012, R-3)

- C-6.1 T is `SessionConfig::test_request_threshold` when set, else 1.2 × HeartBtInt (exactly H × 1200 ms).
- C-6.2 A TestRequest is sent after T of inbound silence; its TestReqID becomes the pending one. The session gives up
  (C-3) after 2T of inbound silence, 2.4 × HeartBtInt by default, counted from the last inbound frame that refreshes
  liveness (clarification D-c): a frame after the TestRequest that does not answer it defers the give-up and leaves
  the TestRequest pending, and a further T of silence sends a new TestRequest. The give-up runs strictly after the 2T
  instant, by one clock tick (R-3's "after 2T"). Every instant, that tick included, saturates at the clock's range.
- C-6.3 The outbound Heartbeat every HeartBtInt of outbound silence, as today, except while a TestRequest is pending
  with no inbound frame since it was sent, when none is sent. A frame that arrives after the TestRequest makes the
  Heartbeat due again at once, as in QuickFIX C++ (`Session::verify` runs `testRequest(0)` on every accepted frame,
  and `needHeartbeat` requires no TestRequest outstanding). Unchanged: the gap deadline at 2 × HeartBtInt (B-537-2);
  every frame that refreshes inbound liveness today refreshes it; the TestReqID(112) rule is C-5.4.
- C-6.4 HeartBtInt ≤ 0 disables the liveness loop and the gap deadline (owner ruling on SI-2, 2026-11-20).
- C-6.5 Precedence with a gap open: the gap deadline is tested only when a frame arrives, and every frame that reaches
  it has refreshed liveness. A peer that keeps sending frames that do not advance NextNumIn ends at the gap deadline
  (2 × HeartBtInt) with its Logout and C-2, whatever T; a silent peer ends by C-3 at 2T.
- C-6.6 A set T of zero or below is refused. C++: `Session::open()` and `Engine::register_session` return
  `invalid_session_config` for zero and for a negative value. TOML: a zero duration reports `out_of_range` at
  `session[i].test_request_threshold`; a negative token is refused earlier by the duration parser as
  `malformed_value` ("duration must start with a numeric value"), unchanged. Any positive T is accepted, T above 2H
  included.
- C-6.7 No C-ABI setter and no Python surface for T.

## C-7. Records before a Session exists (FR-014, FR-015, #687)

- C-7.1 Each connection the acceptor refuses before a Session exists writes one warn record through the entry's
  logger: `acceptor connection refused before a session: reason={} begin_string={} sender_comp_id={} target_comp_id={} error={} suppressed_since_last={}`.
  `reason` is one of `not_tls`, `tls_handshake`, `no_time_left`, `first_frame`, `no_ids`, `no_session`. The CompIDs
  are the first frame's when it was read, each at most 15 bytes; `error` is the handshake's or the first-frame
  read's error code, or 0.
- C-7.2 The records are bounded per entry: at most one per `max(HeartBtInt, 1 s)` of the engine clock; the next
  record carries in `suppressed_since_last` the refusals not written since the last one.
- C-7.3 A listener that cannot bind writes one error record,
  `acceptor listener could not bind: begin_string={} sender_comp_id={} target_comp_id={} error={}`, `error` being
  the system error's value, or 0 when the exception carries none. The entry then never listens, as today.
- C-7.4 No counter is added. Garbles of a connection that yields no Session are still not counted (L-093-6).

## C-8. The close is orderly where `close()` is (FR-002)

- C-8.1 Every close of C-1 goes through `Transport::close_async()`. On TLS the peer receives a close_notify
  wherever `close()` sends one today (B-348-1 and its amendments), including when the pump is parked in a read.
- C-8.2 No row of C-1 calls the synchronous `Transport::close()` on a live transport.
- C-8.3 A `close()` that starts while a C-1 close is in flight waits for it before its own `close_async()`.
- C-8.4 A `close(graceful)` during an R-2 wait after Active (C-2 W-13) reaches its own `close_async()` when the
  wait ends, by the peer's Logout or by the bound. `close()`'s grace (`close_grace`) and the bound are the same knob,
  `logout_disconnect_timeout_ms`, and the bound started at the Logout's write, before the `close()` call, so on a
  session whose clock is the wall clock the wait ends first and the close is orderly. The grace's direct
  `Transport::close()` (abortive on TLS, B-348-1) is reached only when the wait outlives the grace, as for any
  graceful close today. A cell separates the two only through the clock split: `logout_disconnect_timeout_ms` set
  long, and the mock clock advanced to the bound well inside that wall time.
- C-8.5 **The write limit's close on TLS** (C-2.7). `close_async()` cancels the blocked write (its quiesce covers a
  write in flight), then writes close_notify. Behind a peer that does not read, that alert cannot be written either,
  and `close_async()` falls back to the abortive close at its `tls_close_timeout` deadline. So on TLS a limited
  Logout's connection closes within the bound (session clock) plus `tls_close_timeout` (wall time), and the close is
  abortive when the peer stops reading. Permitted by ruling D-e and disclosed (L-105-g). On the plain transport
  `close_async()` is `close()`, so the close is at the bound.

## C-9. Surfaces

- **C ABI: 1.18, BREAKING** (Article X §7). No symbol, type or error code changes. The observers, each with a 1.18
  clause in `include/fix/c_api/session.h`: `fixpp_session_is_established` (C-2.1, C-5.1, C-6.2),
  `fixpp_session_send` (C-2 W-10), `fixpp_session_close` (C-2 W-13, W-14), `fixpp_session_register_callback`
  and `fixpp_session_register_send_callback` (C-2 W-1 to W-5, C-5.1). Each affected declaration has a C-driven
  witness, close and callback delivery during the wait included. The 1.11, 1.12 and 1.17 sentences these change are
  amended in place, "amended in C-ABI 1.18, 105". `version.h`'s 1.18 history carries the counterparty's side:
  every ending closes the connection; the unanswered TestRequest's Logout; the R-2 wait; the write limit on every
  ending Logout (C-2.7); the TestRequest at 1.2 × HeartBtInt; the TestReqID(112) rule of C-5.4.
- **C++, BREAKING layout:** `sizeof(Session)` changes; `ReconnectFsm` loses members, `drive_logout` and two
  constructor parameters (an installed header). `SessionConfig` is unchanged. `SessionEvent` is unchanged.
- **Configuration:** `test_request_threshold` now acts (C-6) and is validated (C-6.6).

## C-10. Departures from QuickFIX

The spec's table holds. This design adds:

| Observable | fixpp after 105 | QuickFIX C++ | QuickFIX/J | QuickFIX/Go |
|---|---|---|---|---|
| Application send during the wait after fixpp's ending Logout | refused | sent (`Session::sendRaw` does not test `sentLogout`) | not read for this plan | dropped (the spec's second reading) |
| Heartbeat and TestRequest originated during that wait | none | sent (`Session::next` goes on past `logoutTimedOut`) | sent (`Session.next()` goes on past `isLogoutTimedOut`) | not sent (`logoutState.Timeout` acts only on `LogoutTimeout`) |
| The wait after a Logout refusing a Logon | bounded, then close; no reset | close at once (`doTargetTooLow`) | close at once | close at once |
| NextNumIn for the peer's Logout response off NextNumIn | not advanced | advanced by one (`nextLogout`'s `incrNextTargetMsgSeqNum`) | not advanced (advanced only when it equals the expected number, QFJ-750) | not read for this plan |
| A Heartbeat whose TestReqID(112) is an earlier TestRequest's | neither answers nor ends (C-5.4) | not compared | not compared | not compared |
| An ending Logout whose write does not complete | the connection closes at `logout_disconnect_timeout_ms` from the write's start (C-2.7) | not read for this plan | not read for this plan | not read for this plan |

## C-11. Disclosed

- **L-105-a.** Withdrawn in Gate A round 1: a silent ending during the wait is absorbed (C-2 W-8), so the case it
  disclosed no longer exists. The ID is not reused.
- **L-105-b.** On a direct Session (no Engine pump), an ending reached from an inbound frame, and one whose
  close a detached root deferred to a pump while a frame was inside an arm, close at the exit of a later,
  separately started detached root or application call, at `close()` or at `drive_reconnect`; the
  `on_inbound_frame` call that reached the ending is not such a call; with no such call the connection stays open as
  before 105. So does any ending whose close frame could not be spawned (an allocation failure; the latch stays set
  for the next consumer), and a write limit whose bound could not be spawned leaves that write unbounded.
- **L-105-c.** Slot 74 is carried by a log record only; no API call returns it.
- **L-105-d.** The CompIDs in a pre-session record are cut at 15 bytes.
- **L-105-e.** A frame numbered before fixpp's ending Logout (an application send, or a liveness Heartbeat or
  TestRequest) that is still inside its store call when the Logout is written can reach the wire after the Logout
  (C-2.3). Nothing is numbered after the Logout. On the too-low route the peer cannot recover it during the wait.
  Pre-existing on `main` for every Logout ending (103 FR-021 promised numbering, not wire order).
- **L-105-f (narrowed by ruling D-e; supersedes the round-1 text "blocks every Logout-first ending behind the
  session's write gate … closes only at `close()` or `Engine::stop()`").** Only the ending Logouts are limited
  (C-2.7). Every other write stays unbounded, as on `main`: an application send, a Heartbeat, a TestRequest, a
  resend answer, a Reject. A peer that stops reading (a TCP zero window, for example) therefore holds such a write,
  and the write gate behind it, until `close()` or `Engine::stop()`. That includes the liveness loop's own
  Heartbeat or TestRequest when it is queued behind a blocked application send: the loop is then suspended in that
  emit and never reaches R-1. With `logout_disconnect_timeout_ms` 0, or no clock, the ending Logouts are unbounded
  too. An inbound arm that returns an error while another root's ending Logout is in flight closes at once, as on
  `main`, and can cut that Logout (C-2.7). Follow-up: a fixpp issue for bounding every write, filed before Gate B and named in this row.
- **L-105-g.** On TLS, a limited Logout's close (C-8.5) cannot write close_notify behind a peer that does not read;
  `close_async()` then closes abortively at `tls_close_timeout`. Permitted by ruling D-e.

## C-12. Performance (Article VIII §2, §3; ruling D-f)

- Per inbound frame the feature adds two tests: the pump's latch test after every `on_inbound_frame` return
  (`run_read_pump`), and the ending-phase test in `on_inbound_active_` (the response test of W-1, armed or not). Per
  outbound write it adds the transmitted out-parameter of `store_then_emit` (research D2 item 1). Per Heartbeat with
  a TestRequest pending, the TestReqID classification (C-5.4). The ending Logout's state, bound and close frame run at
  endings only.
- Paired base-vs-candidate runs (A-B-A-B, Article VIII §2's +5 % budget) are owed in this PR for
  `on_inbound_frame_bench` (the per-frame test and the Active arms' frames), `session_send_bench` (`send_body_` and
  `store_then_emit`) and `engine_send_bench`, and for a new pump case added to `engine_send_bench`'s harness that
  feeds inbound frames through `run_read_pump` (the latch test). In `bench/ci-suite.txt`, `engine_send_bench` is a
  `paired` entry (CI compares its timing), `on_inbound_frame_bench` is listed unpaired (execution-checked, no timing
  limit), and `session_send_bench` is not listed; the runs for the last two are local and recorded in the verify
  evidence.
