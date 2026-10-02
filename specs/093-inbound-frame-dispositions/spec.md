# Feature Specification: Inbound frames 092 leaves out

**Feature Branch**: `093-inbound-frame-dispositions`

**Created**: 2026-10-02

**Status**: Draft (clarified 2026-10-02)

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
  session-guard handling, unchanged (FR-008). The Framer never reads the BeginString value, so every
  supported profile is framed (research R-2).
- **Framer-detected garbling**: criteria 1, 2 and 4 as the read pump's Framer detects them, which today
  ends the session (`L-004-4`). The Framer reports a failure *kind*, not a §4.5.2 criterion: "CheckSum
  not last" (TC 3e) surfaces as a BodyLength failure, because `10=` is not where the count says. Criterion
  3 is not a Framer check. The header scan detects it (092's `msg_type_is_third`, computed for every
  frame).
- **Faulty**: 092's "framed but unparseable", a frame that passes framing and in which the header scan
  finds an encoding fault. 092's dispositions for faulty frames are unchanged, except where FR-005 says
  otherwise.
- **Disregard**: no Reject, NextNumIn unchanged, no disconnect, and no refresh of inbound liveness. The
  frame is logged and recorded as a session event (FR-003).
- **Admitted frame**: a frame of at most L bytes (FR-010) that passes framing. **The inbound limit L**
  is the one size from which the read pump's Framer limit, its carry buffer, the parse index capacity and
  the offset-table entry cap are all derived.
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
  - Entering `Disconnected` does not close the transport by itself (fixpp#534). Only `close()` does.
  - A peer that sends only garbage is closed **immediately** today, on both roles. The hang appears only
    once garbage is disregarded, and for a silent peer after the initiator's Logon or the acceptor's
    first frame.
- **#515.** Two ceilings that nothing derives sit behind the read pump's admission:
  - **the inbound parse index**, a fixed 16 KiB stack buffer (`kInboundParseArena`, from
    019-app-callbacks). Its entries grow with no reserve in a monotonic arena, so the field count at which
    it fails depends on the allocator's growth factor. On MSVC debug the arena falls back to the heap;
  - **the offset-table entry cap** (`default_max_offset_entries`). It fails a frame with more fields than
    the cap on every lane, MSVC debug included.

  The read pump's own admission is set by its 64 KiB carry, which must hold a whole frame plus one read.
  So "a frame up to 64 KiB" really depends on how the stream is segmented (research R-3). A well-formed
  frame with a few hundred fields passes framing and then fails the full parse. 092 made that fail-closed
  (FR-016, O-2), but it is detected only at a late site, after the guards and some handlers have acted
  (`L-092-6`).
- **#516.** Inbound liveness (`last_inbound_steady_`) is refreshed in the Active arm at one site. Every
  Active-arm path that returns before that site does not refresh it. That covers the Reset-mode
  SequenceReset, which returns before the seqnum check, too-high, the PossDup Rejects, too-low, GapFill,
  inbound Logout and inbound Reject. A peer whose recent traffic is only such frames is sent a
  TestRequest that SL2020 heartbeat rows 13 and 14 would not send.
- **#523.** Once a graceful `close()` has begun, a frame coalesced behind a Logon is still processed by
  the `NotConnected` or `LogonSent` arm. That arm can refuse the frame, write `Disconnected`, or emit a
  validate Reject after close began. It is reachable only for a graceful close whose store flush yields
  (`FileStore`, or a custom store). A terminal close writes `Disconnected` before it first suspends.
- **#524.** A peer Logon with 141=Y runs a reset unit: a durable store reset, then a restore of
  NextNumIn to 2 by one increment.
  - If `close()` completes its drain while the arm is suspended inside that unit, and no teardown reset is
    configured, the restore is lost and NextNumIn stays at 1.
  - It is lost because the drain makes the `SeqnumManager`'s `set_next_inbound` fail (`L-518-1`), so the
    store write is never reached.
  - A peer that next logs on at 34=2 without 141=Y is then too high, which is fatal with 789 tolerance off.

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

### Session 2026-10-02 (clarify)

- Q: Before Logon completes, should a garbled frame be disregarded (keep waiting, bounded by the new
  logon timeout) or end the connection? → A: **Disregard it, bounded by the timeout**, in every pre-Active
  state and on the acceptor's first-frame read, whatever its MsgType (FR-001, FR-005).
- Q: What default should the new logon timeout have, and may an operator set it to zero to disable it?
  → A: **10 s default; zero is refused** at the C-ABI setter, at the TOML loader and at `open()` (FR-006,
  FR-007).
- Q: How should C and Python applications see that a garbled frame was disregarded? → A: **Also through
  a C-ABI counter getter**, `fixpp_session_garbled_frame_count`, which Python picks up automatically, in
  addition to the C++ session event and the log (FR-003, FR-007).
- Q: Should the advertised MaxMessageSize that sizes each session's buffers have an upper bound? → A:
  **`open()` refuses a value above 256 KiB** (the Framer's existing default frame ceiling) with the
  invalid-session-config error. One limit then governs framing, the buffers and the 383 fixpp advertises
  (FR-010).

### Session 2026-10-02 (plan research → owner rulings)

- Q: A store-side default body for the new reset operation cannot stop partway, as today's guarded
  sequence does when `close()` begins. How are custom stores made safe? → A: **`close()` waits for an
  in-flight reset unit before it issues its own teardown reset, and that wait has a timeout** (owner:
  "1, but with timeout"). The virtual keeps its default body. `MemoryStore` and `FileStore` still override
  it atomically (FR-040, FR-041). The bound is `logout_disconnect_timeout_ms`, the existing bound on how
  long `close()` may take (research R-6). On expiry `close()` proceeds and records an event, and the
  residual is a B&L row.
- Q: Where does the parse index live, given that the worst case is roughly 6 × L per session? → A: **Per
  session, allocated once at `open()`, as R-2 ruled.** The cost is stated as a formula in B&L, and an
  operator lowers it by advertising a smaller MaxMessageSize (FR-012, SC-007).
- Q: The research found three suspected pre-existing defects by code reading. → A: **File them, each
  marked unconfirmed until reproduced.** They are fixpp#536, #537 and #538 (batch B28), outside 093.

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
garbled frame's number, one garbled-frame event is recorded, and the garbled-frame counter reads 1
(also through the C ABI). On today's tree the same cell sees the
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
   `fixpp_session_config_set_logon_timeout_ms`, **Then** the session honours it, and zero is refused at
   the setter.
5. **Given** no timeout setting, **Then** the default of 10 s applies.

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
   field count arrives, however the stream is segmented, **Then** it is parsed and delivered.
2. **Given** an advertised MaxMessageSize of N, **When** a frame of N bytes arrives, **Then** it is
   parsed. **When** a frame of N+1 bytes arrives, **Then** it is refused at framing and the session
   closes.
3. **Given** any lane, **Then** the boundary is the same. It is a derived constant, not the allocator's
   growth pattern.
4. **Given** an advertised MaxMessageSize above 256 KiB, **When** the session is opened, **Then** `open()`
   refuses it with the invalid-session-config error.

---

### User Story 4: every good inbound frame proves the peer is alive (Priority: P2)

During recovery, a peer's traffic can be only too-high frames, GapFills, SequenceResets, Rejects or a
Logout. Each of them refreshes inbound liveness, so fixpp does not send a TestRequest that SL2020 rows 13
and 14 would not send.

**Independent Test**: for each early-return class in the Active arm, keep a peer silent apart from that
class for longer than HeartBtInt. No TestRequest is sent. The same cell sends one on today's tree.

**Acceptance Scenarios**:

1. **Given** an Active session, **When** one too-high frame arrives and nothing else does for most of the
   interval, **Then** no TestRequest is sent within the interval of that frame.
2. Likewise for a Reset-mode SequenceReset, an in-sequence GapFill, a frame Rejected at a #423 Reject
   site, a too-low PossDup frame, a too-low Heartbeat and an inbound Reject(35=3). An inbound Logout
   refreshes too, but it ends the session, so no TestRequest cell can observe it.
3. **Given** an Active session, **When** only faulty frames (092) or garbled frames arrive, **Then**
   liveness is not refreshed (092 FR-018 and this spec's Disregard).

---

### User Story 5: a closing session acts on nothing more (Priority: P3)

The application calls `close()` while a Logon arm is suspended. A frame the peer coalesced behind that
Logon causes no callback, counter advance, event, Reject or outbound frame (#523). If `close()` drains
inside a 141=Y reset unit, the durable counters are still right (#524).

**Independent Test**: one cell per role per issue (SC-006 states which are RED today):
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
  disregarded and framing resumes at the next frame start, provided the carry (L plus one read) holds the
  bytes until then. Otherwise the carry overflows and the session closes. Before Active, the
  establishment timeout bounds the wait. In Active, the peer's TestRequest reply is trapped behind the
  stall, so the liveness loop **ends** the session rather than recovering it. Disclosed exactly that way.
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
- **A custom `MessageStore` subclass** that does not override the new operation. `close()`'s wait gives
  it FR-041's outcomes. Only a crash in the middle of its default body can leave the intermediate state
  (disclosed in B&L).
- **A fault-free 35-not-third frame under strict validation.** It is disregarded (TC 2t). The validator's
  Step 0 is the only producer of 373=14, and it checks exactly this shape, so it becomes unreachable from
  the session. It stays for direct `validate` callers.
- **`8=` with no SOH after it** stays a partial frame until the carry fills, and then the session closes.
- **A frame that is exactly L bytes** arrives split across reads at any boundary. It is always admitted,
  because the carry holds L plus one read.

## Requirements *(mandatory)*

### Functional Requirements

**#514: garbled frames (§4.5.2)**

- **FR-001**: A frame the Framer finds garbled (criteria 1, 2 or 4) MUST be disregarded in every state
  the read pump serves, and on the acceptor's first-frame read. The session MUST continue. This replaces
  `L-004-4`'s session-fatal handling.
- **FR-002**: After a garbled frame, framing MUST resume at the next frame start, searched from after the
  garbled frame's first byte.
  - A frame start is `8=` that immediately follows an SOH, or that is the first byte the session has
    received since its last complete frame.
  - The rule MUST NOT depend on how the stream is segmented into reads. The Framer carries the "previous
    byte was SOH" state across feeds.
  - A garbled region that spans several reads counts as **one** garbled frame.
  - Bytes before the frame start are discarded. A well-formed, complete frame buffered after the garbled
    region MUST NOT be lost.
  - The search is one forward pass over the buffered bytes, never one byte per feed.
- **FR-003**: Each disregarded garbled frame MUST be logged, recorded as a session event that carries
  the failure kind (the Framer's error, or "35 not third") and the bytes discarded, and counted in a
  per-session garbled-frame counter. This follows §4.5.2's "should log each encountered garbled message".
  - The counter is readable from C++ and through the C ABI (FR-007), from any thread.
  - Garbled frames seen during the acceptor's first-frame read, before the Session exists, are counted
    there and replayed into the Session after `open()`. A connection that never yields a Session is not
    counted anywhere (disclosed).
  - A garbled frame is a transport observation, not an action on the frame's content. It is counted and
    logged even after `close()` began (FR-030 does not suppress it).
  - The session has no production log site today, and `SessionConfig::logger_override` is never read.
    FR-003 adds the first site, resolving the logger at `open()` (research R-2).
- **FR-004**: A frame whose third field is not MsgType(35) MUST be disregarded in every state and in both
  validation modes, whether or not it is otherwise faulty. B-005-7 narrows to fields other than the first
  three, and its pinned cell is rewritten to assert the disregard. Research R-1 confirmed this
  (Clarifications).
- **FR-005**: 092's pre-Active refusal of a faulty frame whose third field is not 35 (contract C-2, the
  D-1 and D-2 rows for that shape) MUST become a disregard, now that FR-006 bounds establishment. FR-004's
  check runs before 092's fault branch in every arm, so it takes those frames and 092's D-8 row becomes
  unreachable. Every other 092 faulty-frame disposition is unchanged.
- **FR-006**: `SessionConfig` MUST carry an establishment timeout (R-4) with a 10 s default. A zero value
  MUST be refused by `open()` with the invalid-session-config error.
  - **Acceptor:** the clock starts at accept. The existing first-frame read is bounded by the smaller of
    its own deadline and the time remaining.
  - **Initiator:** the clock starts when the transport is installed and the Logon sent.
  - The timeout runs until the session first reaches Active. On expiry, `close(terminal)` MUST be called,
    which closes the transport and ends in Disconnected, and an event MUST be recorded.
  - It also ends a pre-Active connection the session has already refused into Disconnected, whose
    transport stays open today (fixpp#534's pre-Active half).
  - It MUST survive a clock-wide sleep cancellation by re-arming, as `await_deadline` does (fixpp#536).
- **FR-007**: The C ABI MUST expose `fixpp_session_config_set_logon_timeout_ms` and
  `fixpp_session_garbled_frame_count`.
  - The setter refuses a null handle and zero.
  - The getter refuses a null handle and reports 0 before the session exists.
  - Both are added to the symbol golden and the C-ABI freeze hashes.
  - Python exposes the setter through the existing automatic binding. The getter needs an explicit
    out-parameter typemap and a row in the GIL table (research R-7).
  - The TOML loader MUST accept the timeout key and MUST refuse zero. `logout_disconnect_timeout_ms`'s
    mapper accepts zero, so it is not a pattern to copy.
- **FR-008**: A well-framed frame whose BeginString value does not match the session's MUST keep today's
  handling, unchanged by 093. It is not a garbled frame. Today's handling is Disconnected with no Logout
  and no close in Active, a Logon refusal before Active, and a transport close on the acceptor's first
  frame. TC 2020 2i's "send Logout" is not met there, which is fixpp#534's to settle.

**#515: every admitted frame parses (R-2)**

- **FR-010**: The session MUST have one inbound limit L: the advertised MaxMessageSize when it is
  configured, otherwise 64 KiB. `open()` computes L once, and the pump reads it from the Session.
  - The read pump's Framer limit is L. Its carry holds L plus one read, so a frame of exactly L never
    fails because of how the stream was segmented.
  - The parse index capacity and the offset-table entry cap are derived from L at the densest legal field
    layout, 3 bytes per field. An empty value is legal.
  - `open()` MUST refuse an advertised MaxMessageSize above 256 KiB, or below 4096 (the acceptor's
    first-frame byte budget), with the invalid-session-config error.
  - Only C++ can set 383. C, Python and TOML sessions always get L = 64 KiB.
- **FR-011**: Every admitted frame MUST parse at every inbound parse site without a resource failure, on
  every build lane. That includes a frame of L bytes at the densest legal field layout. A lazy read inside
  an application callback (repeating-group slices, nested tables, unknown-field lists, C-ABI cursors)
  draws on a stated headroom in the same buffer. Exhausting the headroom fails that read, not the session,
  and is disclosed in B&L.
- **FR-012**: Parse capacity MUST be allocated once per session, not per frame. Stack use per parse MUST
  NOT grow (`[const §VIII.5]`).
- **FR-013**: A frame larger than L MUST be refused at framing in every state, before its CheckSum is
  read, so an over-L frame is never mistaken for a garbled one.
  - The session closes terminally with an event and a log, and no guard or handler reads any field of
    the frame.
  - This replaces today's Active-only advertised-MaxMessageSize check, which only writes Disconnected.
  - It also reverses 070's pre-establishment exemption (`test_070_max_message_size_test`). Under R-2 a
    frame over L cannot be parsed in any state, and L ≥ 4096 keeps every Logon the acceptor's first read
    admits.
- **FR-014**: 092's late-parse-failure close (092 FR-016) MUST remain as a defence. It MUST be
  unreachable for an admitted frame, and a test that deletes the capacity derivation MUST turn it RED.

**#516: liveness (R-1)**

- **FR-020**: Every inbound frame that reaches the LogonReceived/Active arm and is neither garbled nor
  faulty MUST refresh inbound liveness. The refresh happens once, right after the fault check and the
  35-not-third check, and before the validate gate and every early return. It replaces the single writer
  at the end of the arm. That covers the validate and PossDup Rejects, the CompID and SendingTime guards,
  the Reset-mode SequenceReset, too-high, too-low (with or without PossDup), GapFill, inbound Reject(35=3)
  and inbound Logout(35=5). A refresh in LogonReceived is harmless: the liveness loop runs only in Active,
  and both roles seed the value on entering Active.
- **FR-021**: Faulty and garbled frames MUST NOT refresh liveness (092 FR-018; this spec's Disregard).

**#523: a closing session**

- **FR-030**: Once `close()` has begun, the NotConnected and LogonSent arms MUST return success without
  effect: no callback, no counter advance, no event, no Reject, no state write and no outbound frame.
  Returning an error would make the pump call `close()` again for no gain.
  - The LogonReceived, Active and LogoutSent arms are unchanged, so phase 1's Logout confirmation path
    still works.
  - The garbled-frame accounting in the pump is not an arm effect (FR-003).
  - FR-030 MUST land no later than FR-041, because the initiator's reset unit is otherwise entered after
    close began.

**#524: atomic reset unit (R-3)**

- **FR-040**: `MessageStore` MUST gain one non-pure virtual that resets the store and sets the next
  inbound and next outbound numbers to given values as one durable unit.
  - Its default body runs `reset()` and then the advances.
  - `MemoryStore` and `FileStore` MUST override it so that no reader, and no restart after a crash, can
    observe the intermediate state.
  - Article XIV §2 counts pure virtuals (at most 5), and the interface keeps its four.
- **FR-041**: The 141=Y reset unit MUST, in both roles:
  - set the `SeqnumManager` to its targets first, with no suspension in between, which closes `L-518-1`'s
    drain residual;
  - then issue one `reset_to(in, out)` with the true targets, on persistent and volatile stores alike;
  - then run the existing superseded check.

  `close()` MUST wait for an in-flight unit before it issues its teardown reset. The wait is bounded by
  `logout_disconnect_timeout_ms`. On expiry it proceeds and records an event (owner ruling, plan
  research).
  - Result: no teardown reset configured gives next-inbound 2, or 2/2 on the initiator; one configured
    gives 1/1, for every store.
  - The in-unit `teardown_reset_done_` stops become dead and are removed. The flag remains as `close()`'s
    single-fire latch.
- **FR-042**: An existing C++ `MessageStore` subclass MUST compile without changes. With `close()`'s wait
  it gets FR-041's outcomes too. Only its crash atomicity is not guaranteed (disclosed in B&L).

**Cross-cutting**

- **FR-050**: B&L:
  - move `L-004-4` to the closed file;
  - narrow `B-005-7`;
  - add B rows for FR-001 to FR-041;
  - add L rows for the residuals:
    - the BodyLength stall;
    - the custom-store crash atomicity;
    - `close()`'s bounded wait expiring;
    - the resend-loop guard;
    - first-frame garbles with no Session;
    - the callback-read headroom;
    - the failure kind reported where the §4.5.2 criterion is meant;
  - update `L-092-6` and `L-518-1` where this feature changes what they state.
- **FR-051**: C-ABI version: one MINOR bump that carries the new setter and getter, and declares the behaviour changes
  BREAKING (`[const §X.7]`, following 091 and 092):
  - a garbled frame no longer ends the session;
  - a 35-not-third frame is no longer processed;
  - a frame over L now closes in every state;
  - an advertised MaxMessageSize above 256 KiB or below 4096 is refused at `open()`, which is reachable
    from C++ only;
  - a pre-Active connection now closes at the establishment timeout, including one already refused into
    Disconnected;
  - liveness refreshes on more frames.
- **FR-052**: No new heap allocation on the per-frame inbound path. The resync search runs over the
  existing buffer in place. This is the B15 (#497) hot-path check, made explicit at Gate A.
- **FR-053**: Test access to private state MUST go through `tests/support/*_test_access` (B21). Nothing
  in a class may be gated on `FIXPP_TEST_HOOKS`, which B25's census enforces.

### Key Entities

- **Garbled-frame event**: one per disregarded frame. It carries the failed criterion and the byte count
  discarded.
- **Garbled-frame counter**: per session, monotonic, readable from C++, C and Python.
- **Inbound limit L**: one per session, derived at open. It sizes the Framer limit, the carry, the parse
  index and the offset-table entry cap.
- **Reset-unit-in-flight flag**: per session. It is set across the unit's single store await, and
  `close()` waits on it with a bound.
- **Establishment timeout**: a per-session duration, configurable from C++, C, Python and TOML.
- **Store reset unit**: the new `MessageStore` operation, which takes the next inbound and next outbound
  numbers.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each of TC 2020 Scenario 2 rows d, m and t and Scenario 3 rows b, c and e has a cell
  asserting disregard-and-continue. Each is RED on `00c1f720` and GREEN on the branch.
- **SC-002**: A resync cell shows that no well-formed frame buffered after a garbled region is lost, and
  that a buffer of only garbage is consumed in finite steps. A mutant that skips the rescan is RED.
- **SC-003**: Per role, a peer that sends only garbled bytes is disconnected **at** the configured
  establishment timeout, not before. The timeout is set from each of C++, C and Python.
  - On `00c1f720` the cell is RED because the close is immediate.
  - A silent peer after the initiator's Logon is disconnected at the timeout. On `00c1f720` it is never
    disconnected (RED, bounded by the test's deadline).
- **SC-004**: On every CI lane, MSVC debug included, a frame of L bytes at the densest field layout
  parses, and a frame of L+1 bytes is refused at framing before any guard acts. The dense-L cell is RED
  on `00c1f720`.
- **SC-005**: For each FR-020 class that leaves the session up, a cell shows that no TestRequest is sent
  within the interval. Each is RED on `00c1f720`. The classes are:
  - one too-high frame (a second one may be fatal, fixpp#537);
  - Reset-mode SequenceReset;
  - GapFill;
  - the validate and PossDup Rejects;
  - too-low Heartbeat and too-low PossDup;
  - the knob-off path;
  - Reject(35=3).

  Logout and a fatal too-low end the session, so they have no such cell.
- **SC-006**: Per role, the #523 cell and the #524 cell without a teardown reset are RED on `00c1f720`
  and GREEN on the branch. Two cells stay green throughout as regression guards: the #524 cell with a
  teardown reset (its 1/1 outcome holds on `00c1f720`), and the LogoutSent confirmation cell. A
  non-overriding store cell shows that `close()`'s wait gives FR-041's outcomes. Deleting the wait turns
  it RED.
- **SC-007**: The per-session memory added by FR-012 is measured and stated as a function of L in the B&L
  row. Steady-state inbound throughput is unchanged within bench noise (`[const §VIII.2]`).

## Assumptions

- The acceptor's existing first-frame deadline and byte budget stay as they are. The establishment
  timeout covers the rest of establishment.
- The densest legal field layout is a one-digit tag with an empty value, `1=<SOH>`: three bytes per field
  (research R-3). That sets the worst-case field count for L. Making an empty value a scan fault is a
  separate decision, outside 093.
- LFIXT (§5.4.12) is not a supported profile, so its "terminate on garbled" rule does not apply.

## Out of scope (follow-ups to file)

- Entering `Disconnected` without closing the transport, on paths other than the establishment timeout:
  a refused Logon, an unanswered TestRequest, and today's MaxMessageSize breach before FR-013. The plan
  re-derives this. Only FR-006's expiry closes the transport here. Filed as fixpp#534 (batch B27).
- `test_request_threshold` is computed and never read (`run_liveness_loop`). Filed as fixpp#535 (batch B27).
- A resend-loop guard for a garbled frame retransmitted identically (§4.5.2's recommendation).
- The size limit of the acceptor's bounded first-frame read.
