# Feature Specification: Transport and liveness — a session that ends closes its connection; TestRequest timing and pre-session rejects

**Feature Branch**: `105-transport-liveness`

**Created**: 2026-11-20

**Status**: Draft

**Base**: the branch's merge-base with `origin/main` ("the base" everywhere in this bundle). Its `src/` and `include/` are
identical to `52aeed4e` (the squash of PR #711; the session code is 104's `20a45577` plus #711's receipt-reset log), where
the mechanisms were re-derived; the branch was then rebased onto #713, which changed tests only. tasks.md T001 re-checks
that identity before any RED reading (clarification "Session 2026-11-21").

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

### Open questions for `/speckit-clarify`

- **OQ-1 (FR-007):** answered 2026-11-20 (Q1 below).
- **OQ-2 (FR-004):** answered 2026-11-20 (Q2 below).

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

### Session 2026-11-20 (`/speckit-clarify`)

- Q: When fixpp refuses a peer before Logon and sends nothing back, does it close at once or at the establishment deadline? → A: close at once (FR-007); the deadline still closes a peer that sends nothing.
- Q (plan OQ-1/SI-3): With `reset_on_logout`, does the peer's Logout answering a Logout fixpp sent to refuse a Logon reset the numbers? → A: no reset; the wait ends and the connection closes (FR-004).
- Q (plan SI-1): Remove the other caller-less `ReconnectFsm` stubs too? → A: yes, every stub with no caller (FR-013).
- Q (plan SI-2): Does a negative HeartBtInt disable liveness? → A: yes, HeartBtInt ≤ 0 disables it (FR-010).
- Q (plan SI-4): Must a Session driven without an Engine close on its own after an inbound-frame ending? → A: no; accepted as limitation L-105-b, FR-001 scoped to Engine-managed sessions.
- Q: During the bounded wait after a Logout fixpp sent to end the session, what happens to frames other than the peer's Logout? → A: process them as in Active, like QuickFIX C++/J (FR-004); fixpp's own LogoutSent drain for `close()` is not reused for this wait. This answer covers the wait after Active only; before Active see D-b below (Gate A round 1).

### Session 2026-11-20 (Gate A round 1)

Decisions taken by the orchestrator on the round-1 reviews (`research/reviews/codex_105-transport-liveness_gate_a_review.md`
and `opus_105-transport-liveness_gate_a_adversarial_review.md` in the parent repo).

- Q (D-a, settled by evidence): After Active, does the peer's Logout end the R-2 wait when its MsgSeqNum is not NextNumIn (the too-low and gap-deadline routes, where a real peer's reply is off NextNumIn)? → A: yes. The peer's Logout ends the wait whatever its MsgSeqNum: it is recognised ahead of the sequence checks, Arm B and the gap-deadline handling, under `reset_on_logout` it resets at its reset point (104 RU-1 to RU-3), and then the connection closes. Evidence: QuickFIX C++ `Session::nextLogout` calls `verify(logout, false, false)` (no too-high or too-low test) and resets under `m_resetOnLogout`; QuickFIX/J `Session.nextLogout` calls `verify(logout, false, false)` and then `resetState()` under `resetOnLogout`; QuickFIX/Go `handleLogout` calls `verifySelect(msg, false, false, ...)`; FIX-SL 2020 is silent (FR-004).
- Q (D-b, owner 2026-11-20): Before Active (a refused Logon), what happens during the R-2 wait to frames other than the peer's Logout? → A: they are drained: no delivery and no NextNumIn advance, as fixpp's LogoutSent arm drains. The peer's Logout or the bound ends the wait, and it resets nothing (the earlier OQ-1 ruling). Q2's "processed as in Active" applies after Active only (FR-004).
- Q (D-c, settled by R-3's wording "2T of inbound silence", FIX-SL 2020 §4.10 row 14, and all three engines refreshing liveness on any accepted inbound frame): Is the give-up measured from the TestRequest, or from the last inbound frame? → A: from the last inbound frame that refreshes liveness today. An unanswered TestRequest with other inbound traffic after it does not end the session at 2T; the session gives up only after 2T of inbound silence. A Heartbeat whose TestReqID(112) does not match the pending one still ends the session (unchanged rule) (FR-010).

### Session 2026-11-20 (Gate A round 2)

Decisions on the round-2 reviews (`research/reviews/codex_105-transport-liveness_gate_a_2_review.md` and
`opus_105-transport-liveness_gate_a_2_adversarial_review.md` in the parent repo).

- Q (D-d, owner 2026-11-20): A Heartbeat carries the TestReqID(112) of an earlier TestRequest fixpp sent on this connection, not the pending one. Does it end the session? → A: no. It counts as liveness: it does not end the session and does not answer the current TestRequest, which stays pending. Only a 112 fixpp never sent on this connection ends the session (`session_testreqid_mismatch`). An absent 112 answers, as today; with no TestRequest pending the 112 is not compared, as today; a reconnect starts a new set. fixpp's TestReqIDs are `TR` plus a per-Session counter that only increases, so "earlier" is decided from the value with two integers, no set (FR-009, FR-010; research D5).
- Q (D-e, owner 2026-11-20; closes OQ-3): Is the write of a Logout that ends a session bounded? → A: yes, in 105. R-1's Logout and every R-2 ending Logout, before and after Active, is raced against `logout_disconnect_timeout_ms` counted from the write's start. If the write has not completed then, the session closes the transport through `close_async`, and the ending proceeds as "Logout not sent", with no wait after it. On TLS, where close_notify cannot be written behind a blocked write, the abortive fallback at `tls_close_timeout` is permitted and disclosed (L-105-g). R-1's slot-74 record is written before the Logout's write. With timeout 0 or no clock the write is not limited. Every other write stays unbounded (L-105-f, narrowed), with a follow-up issue filed before Gate B. This supersedes the round-1 interim decision (disclose the blocked-write ending as L-105-f, "Pre-existing on `main` for the TestRequest") by name (FR-003, FR-004b). Two points are the design's, not the ruling's (research D2 item 11): the replies to the peer's Logout are limited too, because they share the write point (`inbound_logout_leaf_`); and one budget from the write's start covers the write and the R-2 wait, which moves R-2's origin from the write's end to its start (FR-004).
- Q (D-f, orchestrator, Article VIII §3): Which benchmarks does the PR owe? → A: paired base-vs-candidate runs of `on_inbound_frame_bench`, `session_send_bench` and `engine_send_bench`, and a new pump case for the per-frame latch test, added to `engine_send_bench`'s harness. No owner performance ruling is sought (plan Performance Goals; contract C-12).

### Session 2026-11-20 (Gate A round 3, owner targeted edit)

Decisions on the round-3 reviews (`research/reviews/codex_105-transport-liveness_gate_a_3_review.md` and
`opus_105-transport-liveness_gate_a_3_adversarial_review.md` in the parent repo). The loop's round cap was reached at P1 0 /
P2 4; the owner chose a targeted edit and one fresh round.

- Q (D-g, owner 2026-11-20): What happens when the close task, or the ending Logout's bound, cannot be spawned because allocation fails? → A: the session fails closed. A close frame that cannot be spawned is replaced by the synchronous `Transport::close()` of the live transport, on the session strand, so the peer still observes EOF with no later call; on TLS with a read or write in flight that close is abortive (no close_notify). This departs from FR-002 and ruling D-e ("through `close_async`") on allocation failure only, and is disclosed (L-105-h). A bound that cannot be spawned means the Logout is not written: the ending proceeds as "Logout not sent" and the connection closes at once. Round 2's readings (a failed bound spawn proceeds as timeout 0, with an unbounded write; a failed close-frame spawn leaves the latch to a later consumer, L-105-b) are withdrawn (FR-002, FR-004b; round-3 root cause #1).
- Q (scope, owner 2026-11-20): What does the targeted edit change? → A: exactly the round-3 Opus review's four root causes and its confirmed P3s: (1) spawn failure fails closed (D-g); (2) the R-2 bound's expiry closes the connection as the write limit does, whatever arm is in flight, and the Engine's establishment deadline is handed to the ending machine as a cap; (3) the liveness loop sleeps to the earliest instant of a set that keeps the not-yet-reached Heartbeat instant, with no spin; (4) the lifetime, accounting and test-plan lists. Nothing else is redesigned.

### Session 2026-11-20 (Gate A loop 2 round 1)

Decisions on the loop-2 round-1 reviews (`research/reviews/codex_105-transport-liveness_gate_a_4_review.md` and
`opus_105-transport-liveness_gate_a_4_adversarial_review.md` in the parent repo). The orchestrator took no new decision.

- Q (scope, orchestrator 2026-11-20): What does this pass change? → A: exactly the Opus review's root causes 1 to 3 and its confirmed P3s: (1) the ending bound's own failure, its `catch (...)` or a deadline wait that completes early on an absorbed exception, closes as the bound's expiry closes in the same phase, never through the depth-gated release (FR-004b; data-model E-3, "The bound's exits", row 16); (2) the establishment cap's two waits are independent sleeps on an Engine session, stated as a condition with its re-derivation recipe; one wait when both clocks are one object is optional and not taken; (3) the test-plan instruments (the listener's generic arm, the no-spin counter and its bounded drive, the cap cell list) and the requirements checklist's D-g refresh.
- Q (follow-ups, orchestrator 2026-11-20): Are the shared slot-pool timer of a direct driver that spawns on `sess.executor()`, and the read pump's establishment race re-racing an absorbed exception, 105 scope? → A: no. Both are pre-existing and become follow-up issues filed before Gate B, beside the L-105-f non-ending-writes issue (plan, "Follow-ups to file before Gate B").

### Session 2026-11-21 (post-Gate-A plan re-run)

Decisions of the orchestrator after Gate A converged (loop 2 round 2), recorded by the `/speckit-plan` consistency pass.
No design change.

- Q (FR-017, orchestrator 2026-11-21): A cell that names a symbol the base lacks (an allocation-failure seam, the ending bound's in-body race seam, the listener-construction seam, a test-access read of a member 105 adds, a new `constexpr` helper) cannot compile on the base, so it cannot be read RED there. How does FR-017 apply to it? → A: it is a **[seam]** cell (a behaviour cell) or a **[guard]** cell (a `constexpr` helper cell). It lives in its own test file and ctest entry, which a base reading never builds, and its RED is shown at the implementation head under its named mutant, which deletes or reverts exactly the mechanism the cell tests; that reading stands in for the base reading and is recorded as such. A reach-proof twin (a cell that repeats a RED cell's scenario and only adds a test-access read) is a [seam] cell; the RED cell it twins keeps its peer-visible oracle and its base reading (FR-017; quickstart section 2; tasks.md Execution rules).
- Q (the base, orchestrator 2026-11-21): Which commit is "the base" of a RED reading? → A: the branch's merge-base with `origin/main`. Its `src/` and `include/` are identical to `52aeed4e`, where the design was read; tasks.md T001 checks and records that identity, and a difference stops the RED readings and goes to the orchestrator. No commit hash is pinned as the base: a rebase moves it.

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
5. **Given** an acceptor, **When** the first frame's dispatch returns an error, **Then** the connection is closed as
   the read pump closes it for any later frame.
6. **Given** a session before Active, **When** a refusal records `Disconnected`, **Then** the ending is not reported
   as an establishment timeout. A refusal that sends a Logout closes by R-2; a silent one closes at once (FR-007).
7. **Given** an Active session, **When** the liveness loop's `toAdmin` throws, or its number assignment or
   store-and-emit fails, **Then** the connection is closed.
8. **Given** a TLS session, **When** any of the endings above closes the connection, **Then** the close is the orderly
   TLS close wherever today's `close()` closes orderly; no ending introduces an abortive close, except the write limit
   of scenario 9 behind a peer that does not read (ruling D-e, L-105-g) and the synchronous close used when a close
   cannot be spawned (an allocation failure; clarification D-g, L-105-h).
9. **Given** a peer that has stopped reading, **When** a Logout that ends the session cannot be written, **Then** the
   connection closes `logout_disconnect_timeout_ms` after the start of that write, with no further wait.

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
2. **Given** the knob off, **When** the Heartbeat's TestReqID is one fixpp never sent on this connection, **Then** the
   session ends as an in-sequence mismatch ends (and closes per Story 1); the TestReqID of an earlier TestRequest of
   this connection neither answers nor ends it (clarification D-d).
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
- During R-2's wait: an application message, a TestRequest, a ResendRequest, a too-low or too-high frame, and a
  second ending condition (FR-004: no second wait); an application `send()` during the wait (the plan decides and
  tests it).
- The peer's Logout arriving during R-2's wait, the wait expiring, and `close()` or `Engine::stop()` starting during
  the wait: each ends with exactly one close.
- R-1's Logout cannot be written (the CompIDs overflow the Logout buffer, `toAdmin` throws, the store or emit fails),
  or its write never completes (a peer that stops reading; clarification D-e): the connection is still closed. The
  same holds for every R-2 Logout and every reply to the peer's Logout, and for a Logout queued behind another
  blocked write.
- A Heartbeat whose TestReqID(112) is an earlier TestRequest's of this connection, one of a previous connection, or
  not of fixpp's form at all (clarification D-d).
- `logout_disconnect_timeout_ms` of 0, and a null clock: R-2's wait has a defined, tested ending.
- Threshold values: zero, negative, larger than the gap deadline (2×HeartBtInt), and 2T overflowing the clock's range.
  Their handling is decided in the plan and tested.
- The initiator's engine-managed connect loop and a direct `drive_reconnect`: a reconnect must not be blocked by the
  held transport after this feature.
- A first frame that is accepted but whose session then refuses a later frame before Active.

## Requirements *(mandatory)*

### Functional Requirements

**A session that ends closes its connection (#534, R-1, R-2, R-4)**

- **FR-001**: Every transition to `Disconnected` of an Engine-managed session MUST end with the session's transport
  closed, so the peer observes the connection end, with no further inbound frame and no local API call. A Session
  driven without an Engine closes at the exit of a later, separately started detached root or application call,
  at `close()` or at `drive_reconnect`; the `on_inbound_frame` call that reached the ending is not such a call
  (limitation L-105-b, owner 2026-11-20). This holds on every path the re-derive
  lists: inbound arms that return success, detached coroutines (the liveness loop, the peer-Logout bound), the pre-Active
  refusals, and the acceptor's first frame. Two paths keep the close they already own: `close()` (its Logout phase and
  terminal close), and `drain_for_reconnect_`. The plan MUST derive the full set of transition sites by a census that
  is shown to find a known positive, and assign each to a class.
- **FR-002**: The close MUST NOT be abortive where today's `close()` path is orderly. On TLS it MUST send close_notify
  wherever `close()` does today (B-348-1 and its amendments), and it MUST go through the same close path `close()`
  uses, so the two cannot race to close one transport. Two exceptions: a Logout write cut by FR-004b on TLS behind a peer
  that does not read, where `close_async` cannot write close_notify and closes abortively at `tls_close_timeout`
  (clarification D-e, L-105-g); and an allocation failure at the close's spawn, where the session closes with the
  synchronous `Transport::close()` instead, abortive on TLS while a read or write is in flight (clarification D-g,
  L-105-h).
- **FR-003 (R-1)**: When a TestRequest goes unanswered, the session MUST send one Logout whose Text(58) states that the
  TestRequest was not answered, then close the transport as soon as the Logout has been written. It MUST NOT wait for
  the peer's Logout. If the Logout cannot be sent, or its write has not completed within the limit of FR-004b, the
  session MUST still close. The ending MUST write one log record, before the Logout's write (clarification D-e), and
  MUST be observable through the session's existing ending surfaces with `session_test_request_unanswered`
  (slot 74). The plan decides which surface, and `unanswered_tr_` is then read by that surface or removed.
- **FR-004 (R-2)**: After any other Logout fixpp sends that ends the session, before or after Active (at least the
  too-low Logout in Active and in the Logon arms, B-103-10 and B-104-6, the gap-deadline Logout, and the SendingTime
  Reject that becomes a Logout; the census of FR-001 finds the rest), the session MUST close when the peer's Logout
  arrives, or when `logout_disconnect_timeout_ms` runs out counted from the start of the write of fixpp's Logout,
  whichever is first. One budget covers the write (FR-004b) and the wait: the bound does not slide with later sends
  and does not restart when the write completes. On an Engine-managed session before Active the connection's
  establishment deadline caps the budget, during the Logout's store call and write as during the wait, and closes it
  if that comes first (B-093-4; clarification "Gate A round 3", root cause #2); a Session driven without an Engine has no
  establishment deadline, and its pre-Active wait ends at the bound. When the ending claims a pending too-high
  Logout (103's mark, the peer's Logout already received), it waits for nothing: it closes after its write, as R-1's
  does. While the wait is armed:
  - **The peer's Logout ends the wait whatever its MsgSeqNum** (clarification D-a): it is recognised once it has
    passed the identity checks every frame meets (garble, header fault, the validation gate where it runs,
    BeginString, CompIDs) and ahead of every sequence-number test, so a reply below NextNumIn after the too-low Logout, or above it after the gap-deadline
    Logout, is the response. After Active it is a Logout response: under `reset_on_logout` it resets at its reset
    point, as a response in LogoutSent does (104 FR-003, B-104-1), and the close follows the reset unit. All three
    engines reset on that Logout and disconnect. NextNumIn advances for it only when its MsgSeqNum is NextNumIn
    (QuickFIX/J's rule). Before Active the peer's Logout resets nothing (owner 2026-11-20).
  - **After Active, every other inbound frame is processed as it is in Active** (clarification Q2): application
    messages reach `fromApp` and advance NextNumIn, a TestRequest is answered, a ResendRequest is answered, and the
    sequence checks apply (a frame above NextNumIn draws a ResendRequest, as QuickFIX C++'s does during its wait).
    fixpp originates nothing of its own during the wait: no Heartbeat on its cadence, no TestRequest.
  - **Before Active, every other inbound frame is drained** (clarification D-b): no delivery and no NextNumIn advance,
    as fixpp's LogoutSent arm drains.
  - **A second ending condition from a peer frame is absorbed**: a frame during the wait whose content would itself
    end the session, with a Logout or silently (contract C-2 W-8), MUST NOT start a second wait, send a second ending
    Logout, change the session's state or fire `onLogout`; the first wait's bound holds. The absorbed set is defined
    by that cause, not by what the arm returns. **Every internal failure is not absorbed**: a store, persist,
    number-assignment or emit failure or a `toAdmin` throw, in a frame's arm (whether it then returns an error or
    success), in a send or a liveness emit admitted before the Logout, or in a detached root, ends the wait, fires
    `onLogout` once and closes the connection at once.
  The state the session is in during the wait, and when `onLogout` fires, are the plan's, with the observables above
  fixed.
- **FR-004a (precedence)**: A refusal that sends a Logout falls under FR-004 (R-2), before Active as after. A refusal
  that sends nothing falls under FR-007. R-1's Logout (FR-003), and an R-2 Logout that claims a pending too-high
  Logout (FR-004), close after the write.
- **FR-004b (the write limit; clarification D-e)**: Every Logout fixpp writes to end a session (R-1's, every R-2
  Logout before and after Active, and the reply Logouts of FR-005) MUST be time-limited: if its write has not
  completed `logout_disconnect_timeout_ms` after the write's start (a Logout queued behind another blocked write
  included), the session MUST close the transport through the close path of FR-002, and the ending MUST proceed as
  "Logout not sent": no wait, `Disconnected` and `onLogout` once. Nothing may be cancelled or destroyed while
  suspended by that close, and no store work is cut (FR-008). With `logout_disconnect_timeout_ms` 0 or no clock the
  write is not limited. If the limit cannot be set up because its bound cannot be spawned (an allocation failure), the
  Logout MUST NOT be written and the ending proceeds as "Logout not sent" (clarification D-g). If the bound fails after
  it is set up, the connection MUST close when the bound fails, not at its deadline, by the close its phase's deadline
  makes (data-model E-3 row 16), whatever frame is being processed (Gate A loop 2 round 1). A terminal `close()`
  and `Engine::stop()` during the write end it through their own transport close; under a graceful `close()` and
  the reconnect drain, which close the transport only at their own wall-clock bound, the write ends at this limit or
  at that bound, whichever comes first. The budget starts at the Logout's owner claim, so it covers the Logout's
  number assignment and store call too (disclosed in B-105-2). While such a write is in flight, an ending another root records MUST NOT close the connection
  before the write has returned (an arm that returns an error still closes at once, as today). Writes that do not end
  a session stay unbounded (L-105-f).
- **FR-005**: When the session acknowledges the peer's Logout (in Active, in LogoutSent, and the too-high Logout's
  acknowledgement), it MUST close the transport after its reply Logout and after its receipt reset point under
  `reset_on_logout` (B-104-1) has run to its end. The reply's write is limited by FR-004b.
- **FR-006**: The acceptor MUST act on the result of the first frame's dispatch as the read pump acts on every later
  frame's: an error result closes the connection.
- **FR-007**: A refusal before Active that sends nothing MUST close the connection when it records `Disconnected`,
  not at the establishment deadline, and MUST NOT be reported as `session_event_establishment_timeout` (R-4;
  clarification 2026-11-20 Q1). The establishment deadline still closes a peer that sends nothing, with its event
  (B-093-4). 092's O-1 is superseded for the close timing only: D-1/D-2/D-3 still refuse what they refuse today. The establishment
  deadline's own expiry keeps its event (B-093-4).
- **FR-008**: The closes of FR-001 MUST NOT cut store work the ending owns: the receipt reset (B-104-1, B-104-2) and a
  store call already issued run to their end, and `close()`'s and `Engine::stop()`'s existing waits keep their
  bounds. The ending Logout's deadlines and its bound's failure exit are the closes made while a frame may be inside its
  arm: the write limit (FR-004b; the arm is the one suspended in the write), the R-2 bound's expiry (FR-004; for example
  a resend answer blocked in its write during the wait), and the bound's failure exit, which closes when the bound fails, not at its deadline, by the close its phase's deadline makes
  (FR-004b; data-model E-3 row 16). Each closes only the transport, so each cuts no store work.

**TestRequest answering with sequence validation off (#582)**

- **FR-009**: With `validate_sequence_numbers` off, a Heartbeat whose TestReqID(112) matches the pending TestRequest,
  or that carries no 112, MUST answer it whatever its MsgSeqNum, as an in-sequence Heartbeat with a matching or absent
  112 answers it today. On every Heartbeat route, knob on or off, while a TestRequest is pending (clarification D-d):
  the 112 of an earlier TestRequest fixpp sent on this connection MUST neither answer it nor end the session; a 112
  fixpp never sent on this connection MUST end the session (`session_testreqid_mismatch`), as a mismatch does today.
  With the knob on, today's rules (B-537-3, 103's D6 rule) are otherwise unchanged.

**TestRequest timing and `test_request_threshold` (#535, R-3)**

- **FR-010**: The TestRequest MUST be sent after T of inbound silence, and an unanswered one MUST end the session after
  2T of inbound silence, where T is `SessionConfig::test_request_threshold` when set and 1.2×HeartBtInt otherwise.
  The silence is measured from the last inbound frame that refreshes liveness today (clarification D-c): a frame
  that arrives after the TestRequest, answering it or not, restarts it, and the TestRequest stays pending until a
  Heartbeat with a matching or absent TestReqID(112) answers it; the 112 rule is FR-009's (clarification D-d). The outbound Heartbeat cadence (1×HeartBtInt) and the gap deadline (B-537-2, 2×HeartBtInt) are unchanged,
  except that no Heartbeat is sent while a TestRequest is pending with no inbound frame since it was sent, as QuickFIX
  C++'s `needHeartbeat` requires no TestRequest outstanding; a frame after the TestRequest makes the Heartbeat due
  again, as QuickFIX C++'s `testRequest(0)` on every accepted frame does: it is sent at its instant, or, for a frame
  that arrives after that instant, at the liveness loop's next wake, at most max(0, T − H) late (QuickFIX C++ acts at
  its next one-second tick). HeartBtInt ≤ 0 disables the liveness loop
  and the gap deadline (today the loop is off only at 0; owner 2026-11-20). Precedence with a gap open: the gap
  deadline acts only when a frame arrives (it is tested in the inbound arms, not by a timer), and every frame that
  reaches it has refreshed liveness. So a peer that keeps sending frames that do not advance NextNumIn ends at the gap
  deadline (2×HeartBtInt) with its Logout, then R-2, whatever T; a silent peer ends by R-1 at 2T. Both are tested.
- **FR-011**: The threshold MUST be validated where it is accepted (C++ `open()` and the TOML loader) for the edge
  values of the Edge Cases, with the rule the plan decides. The C ABI gains no setter (unchanged scope).
- **FR-012**: The comment on `session_test_request_unanswered` in `error.hpp`, the QuickFIX parity-table row, and any
  other surface that states the TestRequest timing MUST state the new rule.

**Dead state (#559 sites 2 and 3, R-4)**

- **FR-013**: `ReconnectFsm`'s unread members (`attempt_index_`, `heartbeat_interval_`,
  `logout_disconnect_timeout_`), its unused members (`heartbeat_timer_`, `test_request_timer_`, `logout_timer_`,
  `last_outbound_testreqid_`), the `drive_logout` stub and every other `ReconnectFsm` stub with no caller (found by a caller census), and the false
  header comment MUST be removed, and so MUST
  `parse_cn_from_dn_local`. The test that probes `drive_logout` is retired or re-pointed with a note. Every size pin
  of `Session` and `ReconnectFsm` in `tests/` and `ci/` MUST be updated, found by a whole-repo search and a full build.

**Pre-session rejects (#687, R-4)**

- **FR-014**: Each pre-session reject in the acceptor's accept loop MUST write one log record through the entry's
  logger, naming the reason and the peer's CompIDs when they were read, with a format string registered in the format
  registry. The records MUST be rate-bounded per entry, with the suppressed count reported.
- **FR-015**: A listener bind failure MUST write one record naming the entry and the error.

**Records and evidence**

- **FR-016**: The live B&L file MUST be amended in this PR, with the list equal to research D11's B&L plan: L-103-5
  retired; B-537-2's "transport stays open" clause, B-537-2(b) (the silent ending at the grace check, whose reach the
  default T changes), B-537-3, L-093-1, L-093-6, B-093-4, B-092-2, B-092-5, L-092-1, B-103-10, B-104-6 and B-103-13
  amended (B-103-13: a direct driver runs `close()` or `drive_reconnect` and lets the R-2 bound and the close frame
  exit before destroying the Session; `close()` joins both, with no exemption; the matching `session.hpp`
  caller-precondition comment states the same); new rows for the close rule (FR-001…FR-008), the R-2 wait and the write limit (FR-004, FR-004b), R-1's
  departure from QuickFIX, the #582 rule, the TestRequest timing and the TestReqID(112) rule (FR-009, FR-010), and
  the limitations L-105-b to L-105-h, L-105-f naming its follow-up issue, filed before Gate B. L-103-5's statement of QuickFIX's behaviour holds
  only for the too-low path and is not carried forward as written.
- **FR-017**: Every behaviour cell MUST be shown RED on the base (the branch's merge-base with `origin/main`; its `src/`
  and `include/` identical to `52aeed4e`) before its fix; a regression guard (a rule
  the base already keeps) is GREEN on the base by design and MUST instead be shown RED under its named mutant; a cell
  that names a symbol the base lacks (a seam cell, a `constexpr` helper cell, a test-access read of a new member) is a
  [seam] or [guard] cell, built in a separate ctest entry that base readings do not build, and MUST be shown RED at the
  implementation head under its named mutant, which removes exactly the mechanism it tests (amended by clarification
  "Session 2026-11-21"); a cell
  whose oracle the base meets for another reason MUST carry a positive control at the instant it tests. Every close
  cell MUST assert the peer-observed EOF, not an internal flag. Where a close is awaited with a bound, the bound MUST be derived
  from the competing timeout, with the arm that forces the spurious close. A timing cell MUST discriminate the old
  value from the new (today's T018 Cell B is named "1.2×" but passes at 1×, so it pins nothing).
- **FR-018**: Comments that state a superseded rule (for example "Phase 6 US4 wires the Logout emission before
  Disconnected", and 092's "D-1 and D-2 are a state transition, not a close" with its D-3 twin) MUST be corrected or
  removed, naming this feature's rule.
- **FR-019 (supersession)**: Every surface that states or pins a rule this feature changes MUST be superseded by name
  or re-pinned, found by a census over `specs/`, `.specify/`, `brain/`, `spec/`, `tests/`, `include/` and `src/`
  comments that is shown to find the known positives below. A merged feature's bundle is archival and is superseded by
  naming it here, not edited.
  - **Rulings superseded:** 536-537-538 note R-6 ("A Logout on the dead-peer ending. Not taken") by R-1; the C-3
    disclosure of D-537-5 (the transport stays open after the gap-deadline Logout) by FR-004; 103 research Arm B's
    disclosure and 104's DQ-8 (L-103-5) by FR-004; 093 FR-006 phase (b) and its contract C-4/C-7 (the establishment
    deadline as the pre-Active close) by FR-007; 092 contract C-2 O-1's close timing by FR-007; 005 research D-8 (threshold = 1×
    HeartBtInt) by R-3. 013 FR-004 (TestRequest at 1.2×, Logout and disconnect) is the older intent R-1 and R-3
    restore in part.
  - **Known-positive pins that must flip:** `GapDeadlineOutOfSequenceSite.FrameAtTheBoundDrawsTheLogoutAndTheTransportStaysOpen`
    (helper `expect_logout_and_open_transport`), `Capi537Witness.Deadline_CompIdsLeaveNoRoomForTheLogout_EndsSilently`,
    `InboundFrameDispositionsQ10.WithinCapMismatchInActive_DisconnectsWithoutALogout`, the #534-commented cells of
    `SecondFrameInFlight.*` and `ReconcileAfterTheTakeBack.*`, `EstablishmentTimeoutQ16.AcceptorAfterRefusedLogon_*`
    and `ResendAnswerStoreFailureEngine.Acceptor789Path_SlotReadFails_ConnectionStaysOpenUntilTheEstablishmentDeadline`
    (FR-007), T018 `CellC_*_UnansweredTestRequestDisconnects` (R-3 moves the give-up), the 537
    note's control cell "D-537-6, N", and `DriveLogoutStub_*` / `Initiator_DriveLogout_*` (FR-013).
  - **Brain pages:** `brain/components/session.md` (R-6, and its deferral of the grace-check reading to B27) and
    `brain/components/session-liveness-and-reconnect.md` (the `ReconnectFsm` correction block).

### Key Entities

- **Ending class**: a group of `Disconnected` transition sites that share how the connection must be closed (pump
  arm returning success, detached coroutine, pre-Active, acceptor first frame, inside `close()`, reconnect drain).
- **Logout wait (R-2)**: the interval from the start of the write of a fixpp-started ending Logout to the peer's
  Logout or `logout_disconnect_timeout_ms`; the write itself is inside the same budget (FR-004b).
- **TestRequest threshold T**: the silence before the TestRequest; 2T is the silence at which the session gives up.
- **Pre-session reject reason**: one of the accept loop's refusal causes, logged by name.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For every ending class, a peer observes EOF within its bound: at once for R-1, the pre-Active refusals,
  the acceptor first frame and the detached-coroutine failures, where "at once" is the contract's definition (the
  close initiated with no further mock-clock advance, inbound frame or API call, once the executor has no ready work;
  plus completion of the transport's close, bounded by `tls_close_timeout` on TLS); at the earlier of the peer's
  Logout and `logout_disconnect_timeout_ms` from the start of the Logout's write for R-2 (before Active on an Engine
  session, the establishment deadline when that is earlier); and, for an ending Logout
  whose write does not complete, at `logout_disconnect_timeout_ms` from that write's start (plus `tls_close_timeout`
  on TLS). Zero ending sites in the census are left without a cell or a named exemption (an exemption names its
  disclosed limitation).
- **SC-002**: A silent peer is disconnected at 2T of silence (2.4×HeartBtInt by default) and not before, on both roles,
  in both the default and the set-threshold cells. "At 2T" is within one clock tick: the give-up runs strictly after
  the 2T instant (R-3's "after 2T"; research D6).
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
  all three engines accept any Heartbeat (Departures below); a late answer to an earlier TestRequest of the same
  connection is liveness, not an ending (clarification D-d).
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
| Answering Heartbeat's TestReqID | matching or absent answers; an earlier one of this connection neither answers nor ends; one fixpp never sent on this connection ends the session | not compared | not compared | not compared |
| An ending Logout whose write does not complete | the connection closes `logout_disconnect_timeout_ms` after the write's start (abortive on TLS behind a peer that does not read) | not read for this plan | not read for this plan | not read for this plan |
| Peer's Logout reply to fixpp's ending Logout, after Active | ends the wait whatever its MsgSeqNum; resets under `reset_on_logout`, then close; NextNumIn advances only when in sequence | reset, close (`verify(logout, false, false)`; NextNumIn advanced) | reset, close (advanced only when in sequence) | reset, close |
| The same reply before Active (a refused Logon) | ends the wait; no reset | not reached (close at once) | not reached | not reached |
| Other inbound frames during that wait, after Active | processed as in Active; a second ending condition from a peer frame is absorbed, an internal failure ends the wait | processed | processed | processed; outbound replies dropped |
| Other inbound frames during that wait, before Active | drained | not reached | not reached | not reached |
| Bounded wait after fixpp's Logout counts from | the start of the Logout's write | the last send (sliding) | the last send (sliding) | the Logout's write |
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
