# Contract: transport and liveness (105)

**Feature**: [../spec.md](../spec.md) · **Decisions**: [../research.md](../research.md) · **State**:
[../data-model.md](../data-model.md)

The observable rules of this feature, by ending class. "The peer observes EOF" means the peer's read of the
connection ends (for TLS, after a close_notify where C-8 says so). "At once" means with no further mock-clock
advance and no further inbound frame or local API call: the executor is run until it has no ready work. Every rule
names the requirement it discharges. Internals named here are the design's; a test asserts the observable, never
the name.

## C-1. Every ending closes the connection (FR-001, FR-002, FR-005, FR-008)

| # | Condition | Wire, from fixpp | The connection closes | Notes |
|---|---|---|---|---|
| 1 | An inbound frame's arm records `Disconnected` with no Logout sent and returns success (after Active: TestReqID mismatch, BeginString or CompID guard, MsgSeqNum 0, D-3, a too-low frame whose Logout did not fit) | nothing more | at once, after the arm returns | no event, as today |
| 2 | An inbound frame's arm returns an error | whatever the arm wrote | at once, as today | unchanged |
| 3 | The peer's Logout in Active or LogonReceived (a request) | the reply Logout | after the reply, the persist and, under `reset_on_logout`, the receipt reset unit | B-104-1's order kept; `onLogout` before the reset |
| 4 | The peer's Logout in LogoutSent (`close()`'s exchange) | nothing | by `close()`, as today | excluded class X |
| 5 | A too-high Logout's acknowledgement (by the bound or at once) | the reply Logout | after the reply and the reset unit | from the bound's exit, or the pump |
| 6 | fixpp's own Logout that ends the session (C-2's list) | that Logout | by C-2 | |
| 7 | The unanswered TestRequest (R-1) | the Logout of C-3 | at once after its write | not C-2 |
| 8 | The liveness loop's `toAdmin` throw, number-assignment failure or store-and-emit failure | nothing, or a partly sent frame | at once after the loop exits | |
| 9 | An application `send()` or `Engine::send` ending the session (a store or emit failure) | nothing more | at once after the call returns, or after the arm in flight returns | |
| 10 | A direct initiator's Logon emission failing in `Session::open` or `drive_reconnect` | nothing more | at once after the call returns | the Engine's connect loop also closes, as today |
| 11 | A refusal before Active that sends nothing (C-4) | nothing | at once after the frame | not an establishment timeout |
| 12 | A refusal before Active that sends a Logout | that Logout | by C-2's pre-Active rule | |
| 13 | `close()` (graceful or terminal) | as today | as today | class X |
| 14 | `drive_reconnect`'s drain from Active | as today | as today (B-103-9's bound) | class X |

Common rules:
- **C-1.a One close per ending.** A connection is closed once, through `close_async()` (C-8). An `onLogout` callback
  that starts `close()`, and an `Engine::stop()` that races the close, produce no second close and no use after
  close.
- **C-1.b The session's own work runs to its end.** No close in this table runs while a frame is inside an arm. A
  store call issued before the close returns before it; the receipt reset unit runs before rows 3 and 5 close.
  `close()`'s bounded waits and `Engine::stop()`'s waits keep their bounds (B-104-2).
- **C-1.c An Engine session ends terminally** at every row, as it ends at EOF today: its pump closes it with
  `close(terminal)`. **A direct Session** (no Engine pump; the caller drives `on_inbound_frame` itself) has its
  transport closed and stays open in `Disconnected`, so `drive_reconnect` can reconnect it. On a direct Session the
  rows reached from a detached root or an application call (5 by the bound, 7 to 10, and C-2's bound) close at
  that root's exit; the rows reached from an inbound frame (1 to 3, 11, 12) have no pump to consume them, and close
  at the next detached-root or application-call exit, at `close()`, or at `drive_reconnect` (L-105-b).
- **C-1.d** `onLogout` fires at the `Disconnected` edge, as today, once.

## C-2. The R-2 wait (FR-004, FR-004a)

**Which Logouts.** Every Logout fixpp sends to end the session, before or after Active, except R-1's: the
gap-deadline Logout (B-537-2), the SendingTime Reject's Logout, the OrigSendingTime Reject's Logout of the PossDup
arm, the too-low Logout (B-103-10), and every Logon refusal with a Logout (the too-low Logon of B-104-6, the
TestMessageIndicator posture mismatch, the strict-reset refusal, LogonSent's Logout-with-Text). Replies to the
peer's Logout are not in this list (C-1 rows 3 and 5).

A Logout that was not transmitted (it did not fit its buffer, its emit failed, or it was held back) starts no
wait: the session ends and the connection closes at once (C-1 rows 1 and 11).

**After Active.**
- C-2.1 After the Logout's write the session stays Active (`fixpp_session_is_established` true) until the wait ends.
- C-2.2 Every inbound frame is processed as in Active: an application message reaches `fromApp` and advances
  NextNumIn; a TestRequest is answered with a Heartbeat; a ResendRequest is answered; a too-low or too-high frame
  meets the sequence checks.
- C-2.3 An application `send()` is refused with `session_invalid_state_for_send` from the write to the end (the
  rule 103 gave every Logout ending).
- C-2.4 fixpp originates nothing else: no Heartbeat, no TestRequest, no second Logout.
- C-2.5 A frame that would itself end the session with a Logout sends none and starts no wait. A frame that would
  end it silently moves the session to `Disconnected` and the connection stays open until the peer's Logout or the
  bound. An arm that fails (an error result) closes at once (C-1 row 2).
- C-2.6 **The peer's Logout ends the wait.** It is processed as a response: no reply; under `reset_on_logout` it
  resets at its reset point (B-104-1); then the connection closes (C-1 row 3's order).
- C-2.7 **The bound.** With no Logout from the peer, the connection closes `logout_disconnect_timeout_ms` after the
  Logout's write, on the session's clock. A later send does not move the bound. `Disconnected` is recorded at that
  instant, and `onLogout` fires then.
- C-2.8 `logout_disconnect_timeout_ms` 0: no wait; the connection closes after the write. No clock (a direct
  Session without one): the same.
- C-2.9 `close(graceful)` during the wait waits for the wait's end within its own grace, then proceeds;
  `close(terminal)` and `Engine::stop()` end the wait at once. One close either way.

**Before Active.**
- C-2.10 The refusal records `Disconnected` after the Logout's write, as today. Every frame is drained, except the
  peer's Logout, which ends the wait; the connection then closes. The numbers are not reset (OQ-1's recommendation).
- C-2.11 The bound is the earlier of `logout_disconnect_timeout_ms` after the write and the connection's
  establishment deadline. Its expiry closes the connection with no `session_event_establishment_timeout` and no
  establishment-timeout record.

## C-3. The unanswered TestRequest (FR-003, R-1)

- C-3.1 After 2T of inbound silence with the TestRequest unanswered (C-6), fixpp sends one Logout whose Text(58) is
  exactly `TestRequest unanswered, TestReqID <id>`, `<id>` being the TestReqID(112) of the TestRequest it sent, and
  the connection closes as soon as the write has returned. fixpp does not wait for the peer's Logout.
- C-3.2 When the Logout cannot be sent (it does not fit the Logout buffer, `toAdmin` throws on it, the number
  cannot be assigned, or the store or the write fails), the connection still closes at the same point.
- C-3.3 One warn log record, `session ended: TestRequest unanswered: test_req_id={} threshold_ms={} logout_sent={} error={}`,
  with `error` the numeric value of `fixpp::core::error::session_test_request_unanswered`, through the session's
  logger. No call returns that error.
- C-3.4 A peer that answers with a Heartbeat whose TestReqID(112) matches, or that carries none, is not ended.

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
- C-5.2 With the knob off, a Heartbeat whose 112 differs from the pending one ends the session with no Logout, and
  the connection closes at once (C-1 row 1).
- C-5.3 With the knob on, the rules are B-537-3's and 103's, unchanged.

## C-6. TestRequest timing (FR-010, FR-011, FR-012, R-3)

- C-6.1 T is `SessionConfig::test_request_threshold` when set, else 1.2 × HeartBtInt (exactly H × 1200 ms).
- C-6.2 A TestRequest is sent after T of inbound silence. The session gives up (C-3) after 2T of inbound silence,
  2.4 × HeartBtInt by default. The instants saturate at the clock's range.
- C-6.3 Unchanged: the outbound Heartbeat every HeartBtInt of outbound silence; the gap deadline at 2 × HeartBtInt
  (B-537-2); every frame that refreshes inbound liveness today refreshes it.
- C-6.4 HeartBtInt 0 disables both (and ≤ 0 if SI-2 is accepted).
- C-6.5 Precedence with a gap open: with the default T the gap deadline (2H) comes before the give-up (2.4H), so the
  session ends with the deadline's Logout and C-2; with T below H the give-up can come first.
- C-6.6 A set T of zero or below is refused: `Session::open()` and `Engine::register_session` return
  `invalid_session_config`; the TOML loader reports `out_of_range` at `session[i].test_request_threshold`. Any
  positive T is accepted, T above 2H included.
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

## C-9. Surfaces

- **C ABI: 1.18, BREAKING** (Article X §7). No symbol, type or error code changes. The observers, each with a 1.18
  clause in `include/fix/c_api/session.h`: `fixpp_session_is_established` (C-2.1, C-5.1, C-6.2),
  `fixpp_session_send` (C-2.3), `fixpp_session_close` (C-2.9), `fixpp_session_register_callback` and
  `fixpp_session_register_send_callback` (C-2.2, C-5.1). The 1.11, 1.12 and 1.17 sentences these change are
  amended in place, "amended in C-ABI 1.18, 105". `version.h`'s 1.18 history carries the counterparty's side:
  every ending closes the connection; the unanswered TestRequest's Logout; the R-2 wait; the TestRequest at
  1.2 × HeartBtInt.
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

## C-11. Disclosed

- **L-105-a.** A silent ending during the R-2 wait leaves the connection open until the peer's Logout or the bound
  (C-2.5); the peer's Logout then resets nothing (C-2.10's rule applies in `Disconnected`).
- **L-105-b.** On a direct Session (no Engine pump), an ending reached from an inbound frame, and one whose
  close a detached root deferred to a pump while a frame was inside an arm, close at the next detached-root or
  application-call exit, at `close()` or at `drive_reconnect`; with no such call the connection stays open as
  before 105. So does any ending whose close spawn failed.
- **L-105-c.** Slot 74 is carried by a log record only; no API call returns it.
- **L-105-d.** The CompIDs in a pre-session record are cut at 15 bytes.
