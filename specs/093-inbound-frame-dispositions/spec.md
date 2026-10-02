# Feature Specification: Inbound frames 092 leaves out

**Feature Branch**: `093-inbound-frame-dispositions`

**Created**: 2026-10-02

**Status**: Draft (specify; clarify pending)

**Input**: User description: "B22: fixpp #514, #515, #516, #523, #524, as one bundle and one PR, building
on 092-garbled-frame-reject." The full input is the `/speckit-specify` invocation of 2026-10-02, which
carried owner rulings R-1 to R-4 (below). Its requirements are restated below as FRs.

**Issues**: fixpp#514, #515, #516, #523, #524. This is batch B22 in the parent's
`phases/phase-4/issue-batches.md`.

**Depends on**: fixpp#530 (B25, the CI census for `FIXPP_TEST_HOOKS`), which must merge first. Every
test here that reaches private state does so through a `tests/support/*_test_access` friend (B21's
ruling).

---

## Terminology

- **Garbled** is used exactly as 092 uses it: a frame that fails one of FIX-SL 2020 §4.5.2's four
  criteria.
  1. BeginString(8) is not the first field.
  2. BodyLength(9) is not the second field, or its count is wrong.
  3. MsgType(35) is not the third field.
  4. CheckSum(10) is not the last field, or its value is wrong.

  §4.5.2 also counts a BeginString value that is not a defined profile identifier. A well-framed frame
  whose BeginString does not match the session's is not treated as garbled here. It keeps today's
  session-guard handling, which is FIX-TC 2020 Scenario 2 row i (Logout, then disconnect). Research
  must confirm that this keeps every supported profile reachable.
- **Framer-detected garbling**: criteria 1, 2 and 4 as the read pump's Framer detects them, which today
  ends the session (`L-004-4`). Criterion 3 is not a Framer check today. It is detected by the header
  scan (092's `msg_type_is_third`).
- **Faulty**: 092's "framed but unparseable", a frame that passes framing and in which the header scan
  finds an encoding fault. 092's dispositions for faulty frames are unchanged, except where FR-005 says
  otherwise.
- **Disregard**: no Reject, NextNumIn unchanged, no disconnect, and no refresh of inbound liveness. The
  frame is logged and recorded as a session event (FR-003).
- **Admitted frame**: a frame of at most L bytes (FR-010) that passes framing. **The inbound limit L**
  is the one size from which the read pump's buffer and the parse capacity are both derived.
- **Establishment**: the period from a connected transport until the session reaches Active.

## Context: what is broken today

Measured by reading the code at `00c1f720` (origin/main, 2026-10-02). Paths are cited as leads; the
code is authoritative.

- **#514.** The Framer reports four error kinds, and on each one it drops its whole carry buffer. The
  read pump then closes the session (`run_read_pump`, `src/session/engine.cpp`). No resynchronisation
  exists, although one error is named `wire_framing_resync`. FIX-SL 2020 §4.5.2 and FIX-TC 2020
  Scenario 2 rows d, m and t and Scenario 3 rows b, c and e all say instead: consider it garbled,
  ignore it, do not increment NextNumIn, continue.
  - A fault-free frame whose third field is not 35 is accepted and processed. That is B-005-7, the
    lenient default the owner ratified on 2026-06-19, and it is pinned by
    `HeaderFieldsOutOfOrder_MsgTypeNotFirst_Accepted_DivergesFromQuickFix`.
  - Before Active, nothing ends a connection that receives only bytes it disregards. The first-frame
    deadline bounds only the acceptor's first read, LogonSent has no Logon-reply timer, and the liveness
    loop starts only in Active.
  - Entering `Disconnected` does not close the transport by itself (found by the survey; the plan
    re-derives it).
- **#515.** The inbound parse index lives in a fixed 16 KiB stack buffer (`kInboundParseArena`, from
  019-app-callbacks). Nothing derives that size. The read pump's carry admits frames up to 64 KiB, so a
  well-formed frame with a few hundred fields passes framing and then fails the full parse. The point at
  which it fails depends on the allocator's growth pattern, and on MSVC debug it never fails, because
  the arena falls back to the heap there. 092 made the failure fail-closed (FR-016, O-2), but the
  failure is detected only at a late site, after the guards and some handlers have acted (`L-092-6`).
- **#516.** Inbound liveness (`last_inbound_steady_`) is refreshed in the Active arm at one site. Every
  Active-arm path that returns before that site does not refresh it. That covers the Reset-mode
  SequenceReset, which returns before the seqnum check, too-high, the PossDup Rejects, too-low, GapFill,
  inbound Logout and inbound Reject. A peer whose recent traffic is only such frames is sent a
  TestRequest that SL2020 heartbeat rows 13 and 14 would not send.
- **#523.** Once a graceful `close()` has begun, a frame coalesced behind a Logon is still processed by
  the `NotConnected` or `LogonSent` arm. That arm can refuse the frame, write `Disconnected`, or emit a
  validate Reject after close began.
- **#524.** A peer Logon with 141=Y runs a reset unit: a durable store reset, then a restore of
  NextNumIn to 2 by one increment. If `close()` completes its drain while the arm is suspended inside
  that unit and no teardown reset is configured, the restore is skipped and NextNumIn stays at 1. A
  peer that next logs on at 34=2 without 141=Y is then too high, which is fatal with 789 tolerance off.

## Clarifications

### Session 2026-10-02 (owner rulings carried by the specify input)

- **R-1 (#516):** option (a). Every inbound frame that is neither garbled nor faulty refreshes inbound
  liveness, which is the literal SL2020 rows 13 and 14. 092 FR-018 (a faulty frame does not refresh)
  stands.
- **R-2 (#515):** a frame the read pump admits must always parse. No well-formed admitted frame is lost
  to a resource limit.
  - The inbound limit is derived from `SessionConfig`'s advertised MaxMessageSize(383) when that is set,
    and is otherwise today's 64 KiB carry limit.
  - The carry and the parse capacity are both sized from that one limit, and a frame over it is refused
    at framing with a loud close.
  - The parse index moves off the 16 KiB stack buffer to a per-session buffer allocated once. There is
    still no per-frame allocation (`[const §VIII.5]`).
  - The owner asked why a well-formed frame larger than a fixed buffer should be lost; the answer was
    that nothing derives the 16 KiB.
- **R-3 (#524):** a new `MessageStore` virtual with a default body that runs today's reset-then-advance
  sequence. `MemoryStore` and `FileStore` override it atomically. It is not BREAKING for C++ subclasses,
  and the residual for custom stores is disclosed in B&L.
- **R-4 (#514):** the establishment timeout is a new `SessionConfig` field plus a C-ABI setter
  `fixpp_session_config_set_logon_timeout_ms`. That is a C-ABI MINOR addition, and Python picks the
  setter up automatically. On expiry the transport is closed.

### Session 2026-10-02 (specify)

- Q: A fault-free frame whose third field is not MsgType(35). §4.5.2 criterion 3 and TC2020 2t say
  garbled, so disregard. B-005-7's ratified default says accept and process, and strict validation
  Rejects it with 373=14. Which rule ships?
  → A: **Disregard it in both validation modes and in every state** (option 1). B-005-7 narrows to
  fields other than the first three. The owner added: *"check official specs and QuickFIX first"*.
  **Confirmed 2026-10-02 by Research R-1** (`research.md`). SL2020 §4.5.2 ("must disregard"), TC2020
  2t and FIX 4.4 Vol 2 all say garbled, and none ties 373=14 to the first three fields. All four QuickFIX
  engines ignore the frame once logged on. Before Logon, QuickFIX/C++ and QuickFIX/Go disconnect, while
  J and n do not unless the frame is a Logon. No spec text requires that disconnect, so FR-004 and FR-005
  keep the disregard, bounded by FR-006. That divergence is recorded for Gate A.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1: a garbled frame is ignored, and the session carries on (Priority: P1)

A counterparty's link corrupts one frame: a wrong CheckSum, a wrong BodyLength, stray bytes before
`8=`, or a frame whose third field is not 35. fixpp ignores that frame, keeps NextNumIn, logs it, and
processes the next good frame. The gap the next frame reveals triggers today's ResendRequest recovery.

**Why this priority**: the session today ends on a single corrupted byte. That diverges from six FIX-TC
2020 rows and from QuickFIX, and it is the main behaviour #514 owns.

**Independent Test**: feed a good frame, a garbled frame and a good frame to an Active session in one
write. The session stays Active, NextNumIn reflects only the good frames, a ResendRequest covers the
garbled frame's number, and one garbled-frame event is recorded. On today's tree the same cell sees the
session closed.

**Acceptance Scenarios**:

1. **Given** an Active session, **When** a frame with a wrong CheckSum arrives (TC 3b), **Then** it is
   disregarded and the next well-formed frame is processed.
2. **Given** an Active session, **When** a frame whose BodyLength count is wrong arrives (TC 2m),
   **Then** it is disregarded, and framing resumes at the next frame start in the buffered bytes.
3. **Given** an Active session, **When** a frame whose CheckSum is not last, or not three digits, or not
   SOH-terminated arrives (TC 3e), **Then** it is disregarded.
4. **Given** an Active session, **When** a frame whose third field is not 35 arrives, in either
   validation mode (TC 2t), **Then** it is disregarded and NextNumIn is unchanged.
5. **Given** an Active session, **When** bytes that do not begin with `8=` precede a good frame (TC 2d
   and 3c), **Then** the leading bytes are discarded and the good frame is processed.
6. **Given** a session in NotConnected, LogonSent, LogonReceived or LogoutSent, or an acceptor still
   reading its first frame, **When** a garbled frame arrives, **Then** it is disregarded and the session
   keeps waiting for its next frame, bounded by the establishment timeout (User Story 2) or the existing
   logout timeout.

---

### User Story 2: establishment cannot hang (Priority: P1)

An operator sets how long a connection may take to reach Active. A peer that connects and then sends
nothing, sends only garbled bytes, or never answers our Logon is disconnected when that time runs out,
and the transport is closed.

**Why this priority**: User Story 1 makes it safe to keep reading after a garbled frame before Active.
Without a bound, a disregard there turns into an unbounded hang, so the two must ship together.

**Independent Test**: on each role, connect a peer that sends only garbled bytes. The transport is closed
at the configured timeout and not before. The cell is set from C++, the C ABI and Python.

**Acceptance Scenarios**:

1. **Given** an initiator that sent its Logon, **When** no Logon reply arrives within the timeout,
   **Then** the transport is closed, the session ends in Disconnected, and an event is recorded.
2. **Given** an acceptor connection, **When** no valid Logon completes establishment within the timeout,
   **Then** the transport is closed.
3. **Given** a session that reaches Active before the timeout, **Then** the timer has no further effect.
4. **Given** a C or Python application, **When** it sets the timeout through
   `fixpp_session_config_set_logon_timeout_ms`, **Then** the session honours it, and an invalid value is
   refused at the setter.

---

### User Story 3: a large well-formed message is never lost (Priority: P1)

A counterparty sends a well-formed message with many fields, such as a deep market-data snapshot or a
security list, within the size the session accepts. fixpp always parses and delivers it. A message
larger than that size is refused at framing with a loud close. It is never half-processed.

**Why this priority**: today such a message can pass framing and then fail to parse after the session
has already acted on it. That decision is made by an underived constant and differs per build lane.

**Independent Test**: a boundary pair per lane, MSVC debug included. A frame of exactly L bytes at the
densest field layout parses and is delivered. A frame of L+1 bytes is refused at framing, the session
closes, and no guard or handler acts on it. On today's tree, the dense frame of L bytes fails to parse.

**Acceptance Scenarios**:

1. **Given** no advertised MaxMessageSize, **When** a well-formed frame of up to 64 KiB with the maximum
   field count arrives, **Then** it is parsed and delivered.
2. **Given** an advertised MaxMessageSize of N, **When** a frame of N bytes arrives, **Then** it is
   parsed. **When** a frame of N+1 bytes arrives, **Then** it is refused at framing and the session
   closes.
3. **Given** any lane, **Then** the boundary is the same. It is a derived constant, not the allocator's
   growth pattern.

---

### User Story 4: every good inbound frame proves the peer is alive (Priority: P2)

During recovery, a peer's traffic can be only too-high frames, GapFills, SequenceResets, Rejects or a
Logout. Each of them refreshes inbound liveness, so fixpp does not send a TestRequest that SL2020 rows 13
and 14 would not send.

**Independent Test**: for each early-return class in the Active arm, keep a peer silent apart from that
class for longer than HeartBtInt. No TestRequest is sent. The same cell sends one on today's tree.

**Acceptance Scenarios**:

1. **Given** an Active session, **When** only too-high frames arrive for longer than the interval,
   **Then** no TestRequest is sent.
2. Likewise for a Reset-mode SequenceReset, an in-sequence GapFill, a frame Rejected at a #423 Reject
   site, a too-low PossDup frame, an inbound Reject(35=3) and an inbound Logout(35=5).
3. **Given** an Active session, **When** only faulty frames (092) or garbled frames arrive, **Then**
   liveness is not refreshed (092 FR-018 and this spec's Disregard).

---

### User Story 5: a closing session acts on nothing more (Priority: P3)

The application calls `close()` while a Logon arm is suspended. A frame the peer coalesced behind that
Logon causes no callback, counter advance, event, Reject or outbound frame (#523). If `close()` drains
inside a 141=Y reset unit, the durable counters are still right (#524).

**Independent Test**: one cell per role per issue, each RED on today's tree:
- **#523**: `Logon || second frame` in one write, with a graceful close posted during the hydrate or the
  peer-reset store yield.
- **#524**: a close whose drain completes inside the reset unit, run once without a teardown reset and
  once with one.

**Acceptance Scenarios**:

1. **Given** a graceful close that has begun, **When** the NotConnected or LogonSent arm receives a
   frame, **Then** nothing observable happens. The LogoutSent confirmation cell stays green.
2. **Given** a close that drains inside a 141=Y reset unit and no teardown reset, **Then** the durable
   NextNumIn reflects the consumed Logon, and the peer's next Logon at 34=2 without 141=Y is accepted.
3. **Given** the same with a teardown reset configured, **Then** the final durable state is 1/1.

---

### Edge Cases

- **A garbled BodyLength that is too large but under L.** The Framer waits for bytes that belong to later
  frames. The stall lasts until enough bytes arrive for the count to be checked, and then that frame is
  disregarded and framing resumes at the next frame start. The establishment timeout or the liveness loop
  bounds it. Disclosed as a limitation, with the bound stated.
- **A garbled BodyLength over L**, or a frame over L. Refused at framing with a loud close (R-2). It is
  not disregarded, because the bytes cannot be bounded.
- **A resync that lands inside a data payload** containing `8=`. The bogus candidate frame then fails
  framing itself and is disregarded in turn. Every resync step must advance by at least one byte, so a
  buffer is always consumed in finite steps.
- **Several garbled frames in one read.** Each is disregarded and each is recorded. Good frames between
  them are processed in order.
- **A garbled frame retransmitted identically on every ResendRequest.** SL2020 §4.5.2 recommends
  recognising this loop. fixpp has no resend-loop guard (as in `L-092-1`), so this is disclosed and not
  fixed here.
- **A garbled frame during LogoutSent.** It is disregarded and not taken as the Logout reply. The logout
  timeout runs as usual.
- **The acceptor's first-frame read.** Garbled bytes before the first Logon are disregarded within the
  existing first-frame deadline and byte budget. The bounded first read's own size limit is unchanged.
- **A dense frame at L on MSVC debug**, where the arena falls back to the heap today. The bound must hold
  without that fallback.
- **A custom `MessageStore` subclass** that does not override the new operation. It keeps today's
  non-atomic sequence, and #524's window stays open for it (disclosed in B&L).
- **A fault-free 35-not-third frame under strict validation.** It is disregarded (TC 2t), and the 373=14
  Reject no longer applies to it. Other header-order violations keep 373=14 under strict validation.

## Requirements *(mandatory)*

### Functional Requirements

**#514: garbled frames (§4.5.2)**

- **FR-001**: A frame the Framer finds garbled (criteria 1, 2 or 4) MUST be disregarded in every state
  the read pump serves, and on the acceptor's first-frame read. The session MUST continue. This replaces
  `L-004-4`'s session-fatal handling.
- **FR-002**: After a garbled frame, framing MUST resume at the next frame start in the buffered bytes,
  searched from after the garbled frame's first byte. A frame start is `8=` at the start of the remainder
  or immediately after an SOH. Bytes before it are discarded. A well-formed, complete frame buffered after
  the garbled region MUST NOT be lost. Each resync step MUST advance by at least one byte.
- **FR-003**: Each disregarded garbled frame MUST be logged and recorded as a session event that carries
  the criterion that failed. That makes it observable to tests and operators, following §4.5.2's "should
  log each encountered garbled message".
- **FR-004**: A frame whose third field is not MsgType(35) MUST be disregarded in every state and in both
  validation modes, whether or not it is otherwise faulty. B-005-7 narrows to fields other than the first
  three, and its pinned cell is rewritten to assert the disregard. Research R-1 confirmed this
  (Clarifications).
- **FR-005**: 092's pre-Active refusal of a faulty frame whose third field is not 35 (contract C-2, the
  D-1 and D-2 rows for that shape) MUST become a disregard, now that FR-006 bounds establishment. Every
  other 092 faulty-frame disposition is unchanged.
- **FR-006**: `SessionConfig` MUST carry an establishment timeout (R-4) with a default value. It is armed
  when the transport connects and cancelled when the session reaches Active. On expiry the transport MUST
  be closed, the session MUST end in Disconnected, and an event MUST be recorded. It applies to both
  roles.
- **FR-007**: The C ABI MUST expose `fixpp_session_config_set_logon_timeout_ms`. It follows the existing
  setter pattern, refuses a null handle and an invalid value, and is added to the symbol golden. Python
  MUST expose it through the existing automatic binding. The TOML loader MUST accept the matching key,
  following `logout_disconnect_timeout_ms`.
- **FR-008**: A well-framed frame whose BeginString value does not match the session's MUST keep today's
  handling (TC 2020 2i). It is not a garbled frame.

**#515: every admitted frame parses (R-2)**

- **FR-010**: The session MUST have one inbound limit L: the advertised MaxMessageSize when it is
  configured, otherwise 64 KiB. The read pump's buffer and the parse capacity MUST both be derived from L.
- **FR-011**: Every admitted frame MUST parse at every inbound parse site without a resource failure, on
  every build lane. That includes a frame of L bytes at the densest legal field layout.
- **FR-012**: Parse capacity MUST be allocated once per session, not per frame. Stack use per parse MUST
  NOT grow (`[const §VIII.5]`).
- **FR-013**: A frame larger than L MUST be refused at framing in every state. The session closes
  terminally with an event and a log, and no guard or handler reads any field of the frame. This
  replaces today's Active-only advertised-MaxMessageSize check.
- **FR-014**: 092's late-parse-failure close (092 FR-016) MUST remain as a defence. It MUST be
  unreachable for an admitted frame, and a test that deletes the capacity derivation MUST turn it RED.

**#516: liveness (R-1)**

- **FR-020**: In Active, every inbound frame that is neither garbled nor faulty MUST refresh inbound
  liveness before any of the arm's early returns. That includes the Reset-mode SequenceReset, too-high,
  the #423 Reject sites, too-low (with or without PossDup), GapFill, inbound Reject(35=3) and inbound
  Logout(35=5).
- **FR-021**: Faulty and garbled frames MUST NOT refresh liveness (092 FR-018; this spec's Disregard).

**#523: a closing session**

- **FR-030**: Once `close()` has begun, the NotConnected and LogonSent arms MUST return without effect:
  no callback, no counter advance, no event, no Reject, no state write and no outbound frame. The
  LogonReceived, Active and LogoutSent arms are unchanged, so phase 1's Logout confirmation path still
  works.

**#524: atomic reset unit (R-3)**

- **FR-040**: `MessageStore` MUST gain one operation that resets the store and sets the next inbound and
  next outbound numbers to given values as one durable unit. Its default body MUST run today's sequence
  (reset, then advance). `MemoryStore` and `FileStore` MUST override it so that no reader, and no
  restart after a crash, can observe the intermediate state. The interface stays within Article XIV §2's
  limit on virtuals.
- **FR-041**: The 141=Y reset unit MUST use that operation in both roles. A `close()` whose drain
  completes inside the unit then leaves the durable state at next-inbound 2 when no teardown reset is
  configured, and at 1/1 when one is. 092 and #518's `teardown_reset_done_` rule is re-derived against the
  new unit, not copied.
- **FR-042**: An existing C++ `MessageStore` subclass MUST compile and behave as today without changes.

**Cross-cutting**

- **FR-050**: B&L:
  - move `L-004-4` to the closed file;
  - narrow `B-005-7`;
  - add B rows for FR-001 to FR-041;
  - add L rows for the residuals: the BodyLength stall bound, the custom-store residual, and the
    resend-loop guard;
  - update `L-092-6` and `L-518-1` where this feature changes what they state.
- **FR-051**: C-ABI version: one MINOR bump that carries the new setter and declares the behaviour changes
  BREAKING (`[const §X.7]`, following 091 and 092): a garbled frame no longer ends the session, a
  35-not-third frame is no longer processed, a frame over L now closes in every state, and liveness
  refreshes on more frames.
- **FR-052**: No new heap allocation on the per-frame inbound path. The resync search runs over the
  existing buffer in place. This is the B15 (#497) hot-path check, made explicit at Gate A.
- **FR-053**: Test access to private state MUST go through `tests/support/*_test_access` (B21). Nothing
  in a class may be gated on `FIXPP_TEST_HOOKS`, which B25's census enforces.

### Key Entities

- **Garbled-frame event**: one per disregarded frame. It carries the failed criterion and the byte count
  discarded.
- **Inbound limit L**: one per session, derived at open. It sizes the read buffer and the parse capacity.
- **Establishment timeout**: a per-session duration, configurable from C++, C, Python and TOML.
- **Store reset unit**: the new `MessageStore` operation, which takes the next inbound and next outbound
  numbers.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each of TC 2020 Scenario 2 rows d, m and t and Scenario 3 rows b, c and e has a cell
  asserting disregard-and-continue. Each is RED on `00c1f720` and GREEN on the branch.
- **SC-002**: A resync cell shows that no well-formed frame buffered after a garbled region is lost, and
  that a buffer of only garbage is consumed in finite steps. A mutant that skips the rescan is RED.
- **SC-003**: Per role, a peer that sends only garbled bytes is disconnected within the configured
  establishment timeout, set from each of C++, C and Python. That cell is RED on `00c1f720` as a hang
  bounded only by the test's own deadline.
- **SC-004**: On every CI lane, MSVC debug included, a frame of L bytes at the densest field layout
  parses, and a frame of L+1 bytes is refused at framing before any guard acts. The dense-L cell is RED
  on `00c1f720`.
- **SC-005**: For each FR-020 class, a cell shows that no TestRequest is sent within the interval. Each is
  RED on `00c1f720`.
- **SC-006**: The #523 and #524 cells (User Story 5) are RED on `00c1f720` and GREEN on the branch, per
  role. The LogoutSent confirmation cell stays green.
- **SC-007**: The per-session memory added by FR-012 is measured and stated as a function of L in the B&L
  row. Steady-state inbound throughput is unchanged within bench noise (`[const §VIII.2]`).

## Assumptions

- The establishment timeout's default is 10 seconds, which is QuickFIX's LogonTimeout default. A zero
  value is refused, because an unbounded establishment is the hazard this timeout exists to close. Gate
  A may revise both.
- The acceptor's existing first-frame deadline and byte budget stay as they are. The establishment
  timeout covers the rest of establishment.
- The densest legal field layout is one-digit tags with one-byte values, four bytes per field. That
  sets the worst-case field count for L. Research confirms it against the parser's grammar.
- LFIXT (§5.4.12) is not a supported profile, so its "terminate on garbled" rule does not apply.

## Out of scope (follow-ups to file)

- Entering `Disconnected` without closing the transport, on paths other than the establishment timeout:
  a refused Logon, an unanswered TestRequest, and today's MaxMessageSize breach before FR-013. The plan
  re-derives this. Only FR-006's expiry closes the transport here. Filed as fixpp#534 (batch B27).
- `test_request_threshold` is computed and never read (`run_liveness_loop`). Filed as fixpp#535 (batch B27).
- A resend-loop guard for a garbled frame retransmitted identically (§4.5.2's recommendation).
- The size limit of the acceptor's bounded first-frame read.
