# Feature Specification: Transport and liveness — a session that ends closes its connection; TestRequest timing and pre-session rejects

**Feature Branch**: `105-transport-liveness`

**Created**: 2026-11-20

**Status**: Draft

**Base**: `52aeed4e` (fixpp `main`, the squash of PR #711; the session code is 104's `20a45577` plus #711's receipt-reset log).

**Input**: batch B27, lane S1 (`phases/phase-4/issue-batches.md` in the parent repo): fixpp#534, #582, #535, #559 (sites 2
and 3) and #687, with the folds the owner approved on 2026-11-20 (R-4 below). The issues' mechanisms were re-derived
against `52aeed4e`, and the protocol questions were read against FIX Session Layer 2020 (with errata), FIX Session Test
Cases 2020, and the session source of QuickFIX C++ v1.16.0, QuickFIX/J 3.0.2 and QuickFIX/Go v0.9.12. Both readings are
kept in the parent repo, `research/G19-fix-fpml-iso20022/research/findings/b27-rederive-2026-11-20.md`. Every location
below is named by symbol. A line citation in that file is a lead to re-derive, not a pin.

## Context & Problem

A counterparty, and the operator running fixpp, see five groups of defects on the transport and liveness surface.

1. **A session that has ended keeps its connection open (#534, L-103-5, B-537-2).**
   - `Session::record_state_transition_` records `Disconnected` and fires `onLogout`. It never closes the transport.
     The Disconnected arm of the inbound dispatch ignores every frame and returns success.
   - The engine's read pump (`run_read_pump`) closes the connection only on EOF or a read error, an over-limit frame,
     an error returned by the inbound dispatch, or expiry of the establishment deadline while the session has not yet
     reached Active.
   - So every path that records `Disconnected` and then returns success, or that runs outside the pump, leaves the
     connection open until the peer closes it, the application calls `close()`, or `Engine::stop()` runs:
     - **after Active, on an inbound frame:** the too-low Logout (B-103-10), the gap-deadline Logout (B-537-2), the
       SendingTime Reject that becomes a Logout, a TestReqID mismatch, the acknowledgement of the peer's Logout in
       Active, the reply in LogoutSent, the acknowledgement of a too-high Logout;
     - **outside the pump:** the liveness loop's unanswered TestRequest, and its endings on a `toAdmin` throw, an
       outbound-number assignment failure or a store-and-emit failure; the peer-Logout bound;
     - **before Active:** 092's refusals of an unparseable frame (D-1, D-2, D-3), the NotConnected and LogonSent
       refusals, the BeginString and CompID guard, MsgSeqNum 0. These are closed only when the establishment deadline
       (`logon_timeout_ms`, default 10 s) expires, and that close is then reported as an establishment timeout,
       which it is not;
     - **the acceptor's first frame:** the engine delivers the first Logon directly and discards the result, so even a
       first frame whose dispatch returns an error does not close the connection by itself.
   - The unanswered TestRequest ends with no Logout, no log record and no event. Its error slot,
     `session_test_request_unanswered` (74), is never produced anywhere in `src/`, and the flag `unanswered_tr_` it sets is
     never read.
   - What a peer sees: fixpp has stopped processing, the TCP connection stays up, and the peer's reconnect logic does
     not start. On the acceptor the entry's slot stays held for the same time.
2. **With `validate_sequence_numbers` off, an out-of-sequence Heartbeat cannot answer a TestRequest (#582).**
   - In `Session::on_inbound_active_out_of_sequence_`, a Heartbeat with the knob off returns at the Heartbeat test. It
     answers a pending TestRequest only when it is too high while a resend is outstanding, and a resend is never
     outstanding with the knob off. With the knob off an out-of-sequence frame does not move NextNumIn, so once the
     peer's numbers are ahead no Heartbeat is in sequence again. The session then ends at the grace check, although the
     peer answered with the right TestReqID(112).
3. **`test_request_threshold` is accepted and ignored, and fixpp's TestRequest timing is earlier than every engine's (#535).**
   - `SessionConfig::test_request_threshold` is read from C++ and TOML. `run_liveness_loop` computes a local
     threshold from it and never reads that threshold. The TestRequest goes out after 1×HeartBtInt of inbound silence,
     and the session gives up 1×HeartBtInt later, at about 2× in total.
   - QuickFIX C++ and QuickFIX/Go send it at 1.2×HeartBtInt and give up at 2.4×. QuickFIX/J sends it at 1.5× and gives
     up at 2.4×, both configurable. FIX-SL §3.1.25 defines TestRequestThreshold as the time before the TestRequest.
   - The comment on `session_test_request_unanswered` in `include/fixpp/core/error.hpp` says it fires when silence
     "exceeded test_request_threshold". That is false today.
4. **Dead state kept by `[[maybe_unused]]` (#559 sites 2 and 3).**
   - `ReconnectFsm` (`include/fixpp/session/reconnect_fsm.hpp`) stores `attempt_index_`, `heartbeat_interval_` and
     `logout_disconnect_timeout_` and never reads them. It also declares `heartbeat_timer_`, `test_request_timer_`,
     `logout_timer_` and `last_outbound_testreqid_` and never uses them. `drive_logout` is a stub that only a test
     calls, and the header comment claims it uses the stored timeout. These members are private but part of
     `sizeof(Session)`.
   - `parse_cn_from_dn_local` in `src/session/session.cpp` has no caller. The CN extractor that runs is
     `parse_cn_from_dn` in `compid_authorization_policy.cpp`.
   - Site 1, `Writer::scratch_mr_`, moved to lane W1 (owner, 2026-11-20).
5. **An acceptor connection refused before a Session exists leaves no trace (#687, L-093-6).**
   - In the engine's accept loop, every pre-session reject closes the transport and continues with no log record:
     not a TLS transport, a TLS handshake failure, no establishment time left, a first-frame timeout or over-budget
     or read error (an over-limit first frame included), a first frame without BeginString or CompIDs, and a CompID or
     registry mismatch. The first-frame read's disregarded garbles are dropped with it.
   - A listener bind failure ends the accept loop with no record, so the entry silently never listens.
   - `log_role_loop_open_failure` already logs through the entry's logger without a Session (it is used for an
     `open()` failure). So L-093-6's "no out-of-session log site exists" is stale.

## Clarifications

### Session 2026-11-20 (owner rulings before the spec)

The questions were asked with the comparison tables of the findings file (FIX-SL 2020, TC 2020, the three engines).

- **R-1 (#534, unanswered TestRequest):** follow FIX-SL 2020 §4.5.5. fixpp sends a Logout whose Text(58) states that
  the TestRequest was not answered, then closes the transport as soon as that Logout has been written, without waiting
  for the peer's Logout. This departs from QuickFIX C++, /J and /Go, which disconnect with no Logout.
- **R-2 (#534, L-103-5, B-537-2):** after any other Logout fixpp sends that ends the session (the too-low Logout, the
  gap-deadline Logout and the rest of that class), fixpp waits for the peer's Logout and closes when it arrives. If none
  arrives, it closes when `logout_disconnect_timeout_ms` runs out. This matches FIX-SL §4.6, TC 2c and QuickFIX/Go,
  and what QuickFIX C++ and /J do after their bad-CompID and bad-SendingTime Logouts. On the too-low path QuickFIX C++
  and /J close at once.
- **R-3 (#535):** one knob, with QuickFIX C++'s default. `test_request_threshold` T, default 1.2×HeartBtInt, is the
  inbound silence before the TestRequest. The session gives up after 2T of inbound silence (2.4×HeartBtInt by
  default). No new field.
- **R-4 (scope):** fold into this feature the #534 siblings (the acceptor's discarded first-frame result, the
  write-only `unanswered_tr_`, slot 74 never produced, the pre-Active refusals reported as an establishment timeout),
  the whole `ReconnectFsm` dead set into #559, and the silent listener-bind failure into #687. Move #559 site 1 to W1.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — A session that has ended closes its connection (Priority: P1)

A counterparty whose session fixpp has ended sees the TCP connection end, so its own reconnect logic runs and the
acceptor's slot is released, without waiting for its own timeout or for the fixpp application to act.

**Why this priority**: it is the only defect in the batch that a peer can reach to hold a resource (a connection and a
slot) open indefinitely.

**Independent Test**: for each class of ending, drive the session to it and assert that the peer observes EOF within
the bound the class allows, with no further inbound frame and no local API call.

**Acceptance Scenarios**:

1. **Given** an Active session whose peer has stopped sending, **When** the TestRequest goes unanswered, **Then** the
   peer reads one Logout whose Text states that the TestRequest was not answered, then EOF, with no wait for its Logout.
2. **Given** an Active session, **When** the peer sends a frame below NextNumIn without PossDupFlag=Y, **Then** the
   peer reads the too-low Logout and EOF follows either its own Logout or `logout_disconnect_timeout_ms`, whichever
   comes first.
3. **Given** an Active session with a gap open, **When** the gap deadline passes, **Then** the same R-2 rule applies
   after its Logout.
4. **Given** an Active session, **When** the peer sends a Logout, **Then** fixpp replies, runs its receipt reset point
   (B-104-1) to its end, and closes the connection.
5. **Given** an acceptor, **When** the first frame is a Logon that the session refuses, **Then** the connection is
   closed without waiting for the establishment deadline, and no establishment-timeout event is reported.
6. **Given** a session before Active, **When** any refusal records `Disconnected`, **Then** the connection is closed
   at once and the ending is not reported as an establishment timeout.
7. **Given** an Active session, **When** the liveness loop's `toAdmin` throws, or its number assignment or
   store-and-emit fails, **Then** the connection is closed.
8. **Given** a TLS session, **When** any of the endings above closes the connection, **Then** the close is the orderly
   TLS close wherever today's `close()` closes orderly; no ending introduces an abortive close.

---

### User Story 2 — A peer that answers is not disconnected with sequence validation off (Priority: P2)

An operator who runs with `validate_sequence_numbers = false` sees a live peer kept alive: a Heartbeat that carries
the pending TestRequest's TestReqID answers it whatever its MsgSeqNum.

**Why this priority**: a false disconnect of a healthy counterparty, reachable only with a non-default knob.

**Independent Test**: knob off, peer numbers ahead of NextNumIn, fixpp sends a TestRequest, the peer answers with a
Heartbeat carrying the TestReqID; the session stays Active past the grace check. RED on the base.

**Acceptance Scenarios**:

1. **Given** the knob off and the peer's numbers ahead, **When** the peer's Heartbeat carries the pending TestReqID,
   **Then** the TestRequest is answered and the session stays Active.
2. **Given** the knob off, **When** the Heartbeat's TestReqID does not match, **Then** the session ends as an in-sequence
   mismatch ends today (and closes per Story 1).
3. **Given** the knob on, **Then** nothing changes from today's rules (B-537-3 and 103's rules).

---

### User Story 3 — TestRequest timing is QuickFIX C++'s and the threshold works (Priority: P2)

An operator sees fixpp send its TestRequest after T of inbound silence and give up after 2T, with T defaulting to
1.2×HeartBtInt and settable through `test_request_threshold` in C++ and TOML.

**Why this priority**: a configuration key that silently does nothing, and a default that declares a QuickFIX peer dead
earlier than QuickFIX would.

**Independent Test**: per role, a cell for the default (TestRequest at 1.2×HeartBtInt, give-up at 2.4×) and a cell with
T set to a value different from the default; both RED on the base.

**Acceptance Scenarios**:

1. **Given** HeartBtInt H and no threshold set, **When** the peer is silent, **Then** the TestRequest goes out after
   1.2H of silence and the session gives up at 2.4H.
2. **Given** `test_request_threshold` T, **Then** the TestRequest goes out after T and the session gives up at 2T.
3. **Given** any inbound frame that refreshes liveness today, **Then** it restarts the silence measure as it does today.
4. **Given** HeartBtInt ≤ 0, **Then** no TestRequest is sent, as today.

---

### User Story 4 — An operator can see every acceptor connection refused before a session exists (Priority: P3)

An operator reading the entry's log sees one record per refused pre-session connection naming the reason, and the
peer's CompIDs when they were read, and one record when the listener cannot bind.

**Why this priority**: diagnosis only; nothing a peer can hold open.

**Independent Test**: an acceptor with a capturing log sink; a Logon whose CompIDs match no entry, then a connection
that sends nothing until the first-frame deadline; one record per connection naming its reason. RED on the base.

**Acceptance Scenarios**:

1. **Given** each pre-session reject reason, **When** a connection hits it, **Then** exactly one record is written
   through the entry's logger, naming the reason.
2. **Given** a flood of refused connections, **Then** the records are rate-bounded so that an unauthenticated peer
   cannot drive the log without limit, and the suppressed count is visible.
3. **Given** a listener that cannot bind, **Then** one record names the entry and the error.

---

### User Story 5 — Dead state is gone (Priority: P3)

A maintainer finds no stored-never-read member, unused timer or dead parser in `ReconnectFsm` and `session.cpp`, and
no comment claiming behaviour the code does not have.

**Independent Test**: a build under the project's warning flags with the `[[maybe_unused]]` markers removed, and a
`git grep` showing no remaining reference.

### Edge Cases

- A transition to `Disconnected` inside `close()` (its own Logout phase) or inside `drain_for_reconnect_` keeps the
  close that path already owns; the new rule must not close a second time or cut that path's bounded waits (B-103-9).
- Store work that runs after the `Disconnected` edge (a receipt reset under `reset_on_logout`, B-104-1 and B-104-2)
  must finish before the transport is closed, or be unaffected by the close.
- A close requested by an `onLogout` callback that the edge fires, or an `Engine::stop()` that races the new close:
  one ending, one close, no double close or use after close.
- The peer's Logout arriving during R-2's wait, the wait expiring, and `close()` or `Engine::stop()` starting during
  the wait: each ends with exactly one close.
- R-1's Logout cannot be written (the CompIDs overflow the Logout buffer, `toAdmin` throws, the store or emit fails):
  the connection is still closed.
- `logout_disconnect_timeout_ms` of 0, and a null clock: R-2's wait has a defined, tested ending.
- Threshold values: zero, negative, larger than the gap deadline (2×HeartBtInt), and 2T overflowing the clock's range.
  Their handling is decided in the plan and tested.
- The initiator's engine-managed connect loop and a direct `drive_reconnect`: a reconnect must not be blocked by the
  held transport after this feature.
- A first frame that is accepted but whose session then refuses a later frame before Active.

## Requirements *(mandatory)*

### Functional Requirements

**A session that ends closes its connection (#534, R-1, R-2, R-4)**

- **FR-001**: Every transition to `Disconnected` MUST end with the session's transport closed, so the peer observes
  the connection end, with no further inbound frame and no local API call. This holds on every path the re-derive
  lists: inbound arms that return success, detached coroutines (the liveness loop, the peer-Logout bound), the pre-Active
  refusals, and the acceptor's first frame. Two paths keep the close they already own: `close()` (its Logout phase and
  terminal close), and `drain_for_reconnect_`. The plan MUST derive the full set of transition sites by a census that
  is shown to find a known positive, and assign each to a class.
- **FR-002**: The close MUST NOT be abortive where today's `close()` path is orderly. On TLS it MUST send close_notify
  wherever `close()` does today (B-348-1 and its amendments), and it MUST go through the same close path `close()`
  uses, so the two cannot race to close one transport.
- **FR-003 (R-1)**: When a TestRequest goes unanswered, the session MUST send one Logout whose Text(58) states that the
  TestRequest was not answered, then close the transport as soon as the Logout has been written. It MUST NOT wait for
  the peer's Logout. If the Logout cannot be sent, the session MUST still close. The ending MUST write one log record
  and MUST be observable through the session's existing ending surfaces with `session_test_request_unanswered`
  (slot 74). The plan decides which surface, and `unanswered_tr_` is then read by that surface or removed.
- **FR-004 (R-2)**: After any other Logout fixpp sends that ends the session (at least the too-low Logout, the
  gap-deadline Logout, and the SendingTime Reject that becomes a Logout; the census of FR-001 finds the rest), the
  session MUST close when the peer's Logout arrives, or when `logout_disconnect_timeout_ms` runs out from the
  Logout's write, whichever is first. Inbound frames other than the peer's Logout during the wait are ignored, as the
  Disconnected arm ignores them today.
- **FR-005**: When the session acknowledges the peer's Logout (in Active, in LogoutSent, and the too-high Logout's
  acknowledgement), it MUST close the transport after its reply Logout and after its receipt reset point under
  `reset_on_logout` (B-104-1) has run to its end.
- **FR-006**: The acceptor MUST act on the result of the first frame's dispatch as the read pump acts on every later
  frame's: an error result closes the connection.
- **FR-007**: A refusal before Active MUST close the connection when it records `Disconnected`, not at the
  establishment deadline, and MUST NOT be reported as `session_event_establishment_timeout`. The establishment
  deadline's own expiry keeps its event (B-093-4).
- **FR-008**: The closes of FR-001 MUST NOT cut store work the ending owns: the receipt reset (B-104-1, B-104-2) and a
  store call already issued run to their end, and `close()`'s and `Engine::stop()`'s existing waits keep their
  bounds.

**TestRequest answering with sequence validation off (#582)**

- **FR-009**: With `validate_sequence_numbers` off, a Heartbeat whose TestReqID(112) matches the pending TestRequest
  MUST answer it whatever its MsgSeqNum. A Heartbeat whose 112 does not match MUST end the session as an in-sequence
  mismatch does. With the knob on, today's rules (B-537-3, 103's D6 rule) are unchanged.

**TestRequest timing and `test_request_threshold` (#535, R-3)**

- **FR-010**: The TestRequest MUST be sent after T of inbound silence, and an unanswered one MUST end the session after
  2T of inbound silence, where T is `SessionConfig::test_request_threshold` when set and 1.2×HeartBtInt otherwise. The
  outbound Heartbeat cadence (1×HeartBtInt) and the gap deadline (B-537-2) are unchanged. HeartBtInt ≤ 0 disables both,
  as today.
- **FR-011**: The threshold MUST be validated where it is accepted (C++ `open()` and the TOML loader) for the edge
  values of the Edge Cases, with the rule the plan decides. The C ABI gains no setter (unchanged scope).
- **FR-012**: The comment on `session_test_request_unanswered` in `error.hpp`, the QuickFIX parity-table row, and any
  other surface that states the TestRequest timing MUST state the new rule.

**Dead state (#559 sites 2 and 3, R-4)**

- **FR-013**: `ReconnectFsm`'s unread members (`attempt_index_`, `heartbeat_interval_`,
  `logout_disconnect_timeout_`), its unused members (`heartbeat_timer_`, `test_request_timer_`, `logout_timer_`,
  `last_outbound_testreqid_`), the `drive_logout` stub and the false header comment MUST be removed, and so MUST
  `parse_cn_from_dn_local`. The test that probes `drive_logout` is retired or re-pointed with a note. Every size pin
  of `Session` and `ReconnectFsm` in `tests/` and `ci/` MUST be updated, found by a whole-repo search and a full build.

**Pre-session rejects (#687, R-4)**

- **FR-014**: Each pre-session reject in the acceptor's accept loop MUST write one log record through the entry's
  logger, naming the reason and the peer's CompIDs when they were read, with a format string registered in the format
  registry. The records MUST be rate-bounded per entry, with the suppressed count reported.
- **FR-015**: A listener bind failure MUST write one record naming the entry and the error.

**Records and evidence**

- **FR-016**: The live B&L file MUST be amended in this PR: L-103-5 retired; B-537-2's "transport stays open" clause,
  B-537-3, L-093-1, L-093-6, B-093-4, B-092-5 and L-092-1 amended; new rows for the close rule (FR-001…FR-008), R-1's
  departure from QuickFIX, the #582 rule and the TestRequest timing. L-103-5's statement of QuickFIX's behaviour holds
  only for the too-low path and is not carried forward as written.
- **FR-017**: Every behaviour cell MUST be shown RED on the base `52aeed4e` before its fix, and every close cell MUST
  assert the peer-observed EOF, not an internal flag. Where a close is awaited with a bound, the bound MUST be derived
  from the competing timeout, with the arm that forces the spurious close.
- **FR-018**: Comments that state a superseded rule (for example "Phase 6 US4 wires the Logout emission before
  Disconnected", and 092's "D-1 and D-2 are a state transition, not a close") MUST be corrected or removed, naming this
  feature's rule.

### Key Entities

- **Ending class**: a group of `Disconnected` transition sites that share how the connection must be closed (pump
  arm returning success, detached coroutine, pre-Active, acceptor first frame, inside `close()`, reconnect drain).
- **Logout wait (R-2)**: the interval from the write of a fixpp-started ending Logout to the peer's Logout or
  `logout_disconnect_timeout_ms`.
- **TestRequest threshold T**: the silence before the TestRequest; 2T is the silence at which the session gives up.
- **Pre-session reject reason**: one of the accept loop's refusal causes, logged by name.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For every ending class, a peer observes EOF within its bound: at once (within one scheduling round of the
  session's executor, measured as no further mock-clock advance) for R-1, the pre-Active refusals, the acceptor first
  frame and the detached-coroutine failures; at the earlier of the peer's Logout and `logout_disconnect_timeout_ms` for
  R-2. Zero ending sites in the census are left without a cell or a named exemption.
- **SC-002**: A silent peer is disconnected at 2T of silence (2.4×HeartBtInt by default) and not before, on both roles,
  in both the default and the set-threshold cells.
- **SC-003**: With the knob off, a peer that answers each TestRequest with a matching TestReqID is never disconnected by
  the liveness loop in a run of at least 10 TestRequest cycles with its numbers ahead.
- **SC-004**: Every pre-session reject reason produces exactly one log record per connection below the rate bound, and a
  flood beyond the bound produces a bounded number of records with the suppressed count.
- **SC-005**: No `[[maybe_unused]]` marker pointing at fixpp#559 remains in `src/` or `include/` for sites 2 and 3.
- **SC-006**: The sanitizer lanes report no new finding, and no existing `close()`, `Engine::stop()` or reconnect test
  regresses.

## Assumptions

- The close of FR-001 is scheduled, not done inline inside `record_state_transition_`, which is synchronous and
  `noexcept`; the re-derive's six constraints (TLS abortive close, Logouts in flight, inside `close()`, the reconnect
  drain, `root_cancel_` not reaching transport I/O, callbacks) bind the design. The mechanism is the plan's.
- Engine-level counters for pre-session rejects are out of scope; they wait for the OTel plan the owner scoped on
  2026-11-20. This feature writes logs only.
- fixpp keeps matching TestReqID(112) on the answering Heartbeat, as FIX-SL §4.5.5 and TC scenario 6 require, although
  all three engines accept any Heartbeat (Departures below).
- Neither engine loop serves a second connection today (the acceptor ends after its pump; the initiator's multi-cycle
  reconnect is deferred), so "a reconnect is not blocked" is tested for a direct initiator through `drive_reconnect`
  and for the engine-managed loop's single cycle.
- C ABI: no new function. The behaviour changes reach C callers; the plan decides whether the C-ABI minor moves.
- C++: `sizeof(Session)` shrinks (FR-013); `SessionConfig` is unchanged.

### Departures from QuickFIX

| Observable | fixpp after 105 | QuickFIX C++ | QuickFIX/J | QuickFIX/Go |
|---|---|---|---|---|
| Unanswered TestRequest | Logout with Text, then close (FIX-SL §4.5.5) | close, no Logout | close, no Logout | close, no Logout |
| After a too-low Logout | wait for the peer's Logout, bounded by `logout_disconnect_timeout_ms` | close at once | close at once | wait, bounded by LogoutTimeout |
| Answering Heartbeat's TestReqID | must match | not compared | not compared | not compared |
| TestRequest timing | T (default 1.2×HeartBtInt), give up at 2T, one knob | 1.2× / 2.4×, fixed | 1.5× / 2.4×, two multipliers | 1.2× / 2.4×, fixed |
| Out-of-sequence Heartbeat, validation off | answers with a matching 112 | no such switch | answers (any Heartbeat) | no such switch |

## Out of Scope

- `Writer::scratch_mr_` (#559 site 1): lane W1 with #707.
- Engine counters for pre-session rejects (see Assumptions).
- #550 (`open()` failure leaves the accept loop) and #549: batch B31.
- The initiator's multi-cycle reconnect in the engine loop.
- L-424-1 (an oversized stored frame aborts the resend answer): its own batch after S1.

## Normative References

- FIX Session Layer with Errata, November 2020: §3.1.25 (TestRequestThreshold), §4.5.5 (TestRequest), §4.6 (Logout),
  §4.10 rows 13, 14 and 16.
- FIX Session Test Cases, June 2020: scenarios 2c, 6.
- QuickFIX C++ v1.16.0, QuickFIX/J 3.0.2, QuickFIX/Go v0.9.12 (local checkouts in the parent's `reference-engines/`).
