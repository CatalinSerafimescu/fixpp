# Feature Specification: Inbound frames 092 leaves out

**Feature Branch**: `093-inbound-frame-dispositions`

**Created**: 2026-10-02

**Status**: Gate A converged 2026-10-03 (round 3)

**Input**: User description: "B22: fixpp #514, #515, #516, #523, #524, as one bundle and one PR, building
on 092-garbled-frame-reject." The full input is the `/speckit-specify` invocation of 2026-10-02, which
carried owner rulings R-1 to R-4 (below). Its requirements are restated below as FRs.

**Issues**: fixpp#514, #515, #516, #523, #524, and #540 (unconfirmed, reproduce first; FR-015). This is
batch B22 in the parent's `phases/phase-4/issue-batches.md`. The PR settles #540 only if FR-015's
reproduction terminates on the base; SC-008 states both outcomes.

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
  session-guard handling, unchanged (FR-008). At a frame boundary the Framer does not compare the
  BeginString value, so a well-framed frame is framed whatever its profile. One carve-out: a value
  longer than the BeginString cap is garbled at framing (contract C-1 W-2), which is §4.5.2's own "not
  a defined profile identifier" criterion. The cap is the longest supported profile identifier's
  length, or the configured BeginString's length when that is longer. After a garbled region the Framer
  searches for `8=FIX`, and reads only that `FIX` prefix, which every supported profile identifier
  shares (contract C-1).
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
  it atomically (FR-040, FR-041). The ruling does not name the timeout's value or what happens on expiry.
  Those are orchestrator decision OD-1 in plan.md, open to Gate A review.
- Q: Where does the parse index live, given that the worst case is roughly 6 × L per session? → A: **Per
  session, allocated once at `open()`, as R-2 ruled.** The cost is stated as a formula in B&L, and an
  operator lowers it by advertising a smaller MaxMessageSize (FR-012, SC-007).
  *(Gate A round 1 annotation, not part of the ruling: the question's "6 × L" left out the carry. The
  derived totals are in research R-3. The ruling is unaffected.)*
- Q: The research found three suspected pre-existing defects by code reading. → A: **File them, each
  marked unconfirmed until reproduced.** They are fixpp#536, #537 and #538 (batch B28), outside 093.

### Session 2026-10-02 (Gate A round 1)

These are code facts the reviews established, which change how the requirements above read. They are
not owner rulings. The design choices that follow from them are orchestrator decisions in plan.md.

- Q: Does a `Session` exist during the acceptor's first-frame read? → A: **No.** The Session is built
  and opened only after a first frame whose CompIDs match the registry entry. Before that, every
  failure closes the raw transport with nothing recorded. So FR-006's "event MUST be recorded" can
  only apply once a Session exists, and the establishment timeout has two phases (FR-006, contract
  C-4).
- Q: Can a peer that sends only garbage be closed **at** the timeout on the acceptor? → A: **No, and
  the clarify ruling does not require it.** The first-frame read keeps its byte budget, which counts
  discarded bytes, and its own 5 s deadline (Assumptions). Those bounds are stricter than the timeout,
  which satisfies "bounded by the timeout". SC-003 is stated per phase.
- Q: Does `Engine::stop()` reach the 141=Y reset unit before `close()` does? → A: **Yes.** Its first
  step emits total cancellation on the slot the role loops, and so the pump, are bound to. It calls
  `close()` only after the join. The #524 protection therefore cannot sit in `close()` alone (FR-041,
  contract C-6).
- Q: How many `Session`s does the engine build per SessionId? → A: **One per `Engine::start()`.** Both
  role loops return after one connection, and `start()` runs once. The garbled-frame counter on the
  Session is therefore monotonic per SessionId for the engine's life (FR-003).

### Session 2026-10-02 (Gate A round 2)

These are code facts the round-2 reviews established, and one filed issue. They are not owner rulings.
The design choices that follow from them are orchestrator decisions in plan.md.

- Q: Does the per-read race between the read and the deadline decide expiry when both are ready? → A:
  **It must not.** Nothing orders the two completions, so expiry is a loop-head check against the clock,
  and the race only wakes a blocked read (FR-006, contract C-4, plan.md OD-6).
- Q: What does each lazy read inside a callback report when the headroom runs out? → A: **What its
  declaration allows, which differs per API** (contract C-3 I-5). `MessageView::unknown_fields()` is
  `noexcept` and can reach `std::terminate` today. That is filed as fixpp#540, unconfirmed, and 093
  reproduces it first (FR-015, SC-008).
- Q: Does `Engine::stop()` stop a Logon arm after a shielded reset unit? → A: **Not without a flag.**
  `logon_arm_superseded` cannot see `stop()`, which calls `close()` only after its join, and the
  shield's fresh cancellation state does not replay stop's emission. A session-side engine-stop flag
  closes that, in strand order: after `Engine::stop()`'s step 1 has run on the session's strand (FR-041,
  contract C-6, plan.md OD-15).
- Q: Does the establishment deadline use `SessionConfig::clock_override`? → A: **No.** Phase (a) begins
  before a Session exists, so the deadline runs on the engine's clock (FR-006, contract C-4).

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
   keeps waiting for its next frame. The wait is bounded by the establishment timeout (User Story 2) or
   the existing logout timeout. On the acceptor's first-frame read it is bounded by that read's own,
   stricter bounds.

---

### User Story 2: establishment cannot hang (Priority: P1)

An operator sets how long a connection may take to reach Active. A peer that connects and then sends
nothing, sends only garbled bytes, or never answers our Logon is disconnected when that time runs out,
and the transport is closed.

**Why this priority**: User Story 1 makes it safe to keep reading after a garbled frame before Active.
Without a bound, a disregard there turns into an unbounded hang, so the two must ship together.

**Independent Test**: on the initiator after its Logon, and on an acceptor whose first frame matched
its CompIDs but left it pre-Active, a peer that then sends only garbled bytes is closed at the
configured timeout and not before. On an acceptor before any matching frame, a peer that sends only
garbled bytes is closed at the first-frame read's byte budget or its deadline, whichever comes first.
The cells are set from C++, the C ABI and Python.

**Acceptance Scenarios**:

1. **Given** an initiator that sent its Logon, **When** no Logon reply arrives within the timeout,
   **Then** the transport is closed, the session ends in Disconnected, and an event is recorded.
2. **Given** an acceptor connection, **When** no valid Logon completes establishment within the timeout,
   **Then** the transport is closed. An event is recorded when a Session exists, that is, after a first
   frame whose CompIDs match.
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
- **#524**: a close whose drain completes inside the reset unit, and an `Engine::stop()` that begins
  inside it, each run once without a teardown reset and once with one.

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
  stall, so the liveness loop takes the FSM to Disconnected. The transport stays open until the peer
  closes it or the carry overflows (fixpp#534). Disclosed exactly that way (contract L-1).
- **A garbled BodyLength over L**, or a frame over L, including at a candidate the resync search found.
  Refused at framing with a loud close (R-2). It is not disregarded, because the bytes cannot be
  bounded.
- **A resync that lands inside a data payload** containing `8=FIX`. The candidate is usually malformed
  and is disregarded in turn. A complete, well-formed frame embedded there is framed and delivered like
  any other, and the session's guards then apply to it (contract L-8). Every resync step advances by at
  least one byte, so a buffer is always consumed in finite steps.
- **A wrong CheckSum on an otherwise complete frame.** That frame is discarded through its own end, so a
  frame lying inside it is discarded with it (contract L-15). ResendRequest recovers it.
- **Several garbled frames in one read.** Each is disregarded and each is counted. Good frames between
  them are processed in order. Events and log records summarise them (FR-003).
- **A garbled frame retransmitted identically on every ResendRequest.** SL2020 §4.5.2 recommends
  recognising this loop. fixpp has no resend-loop guard (as in `L-092-1`), so this is disclosed and not
  fixed here.
- **A garbled frame during a resend recovery** (Active, a ResendRequest outstanding). It is disregarded
  as in any state, even when it was one of the resent frames. Its number then stays missing, and the
  frames after it meet today's gap handling while the request is outstanding, which 093 does not
  change. When the peer resends it garbled every time, this is the loop above (contract L-7).
- **A garbled frame during LogoutSent.** It is disregarded and not taken as the Logout reply. The logout
  timeout runs as usual.
- **The acceptor's first-frame read.** Garbled bytes before the first Logon are disregarded within the
  existing first-frame deadline and byte budget, and the budget counts the discarded bytes. The bounded
  first read's own size limit is unchanged. Its Framer is capped at L, so a first frame over L is
  refused as anywhere else.
- **A dense frame at L on MSVC debug**, where the arena falls back to the heap today. The bound must hold
  without that fallback: the parse of a frame of at most L fits B(L), and the spill witness records
  nothing for it (quickstart Q-11). Only a lazy read in a callback past `kCallbackReadHeadroom` may
  spill there, and the spill is recorded (FR-011).
- **A custom `MessageStore` subclass** that does not override the new operation. The unit's shield and
  `close()`'s wait give it FR-041's outcomes, unless the wait expires. A crash in the middle of its
  default body, or an expired wait, can leave the intermediate state (disclosed in B&L).
- **A fault-free 35-not-third frame under strict validation.** It is disregarded (TC 2t). The validator's
  Step 0 is the only producer of 373=14, and it checks exactly this shape, so it becomes unreachable from
  the session. It stays for direct `validate` callers.
- **`8=` with no SOH after it.** On the session's inbound path, a BeginString value longer than the
  BeginString cap is a garble (contract C-1 W-2). Callers of the Framer that keep resync off are
  unchanged: there it stays a partial frame until the carry fills.
- **A zero-padded BodyLength.** FIX `int` permits leading zeros. On the session's inbound path a
  BodyLength digit run longer than the digit cap is a garble (contract C-1 W-2), even when its value is
  small. The cap's condition admits the padding counterparties legitimately send.
- **A session configured with a BeginString that does not begin with `FIX`.** It frames at a frame
  boundary as today, but the resync search looks for `8=FIX`, so after a garble it finds no next frame
  start (contract L-16). No supported profile has such a BeginString.
- **A frame that is exactly L bytes** arrives split across reads at any boundary. It is always admitted,
  because the carry holds L plus one read.

## Requirements *(mandatory)*

### Functional Requirements

**#514: garbled frames (§4.5.2)**

- **FR-001**: A frame the Framer finds garbled (criteria 1, 2 or 4) MUST be disregarded in every state
  the read pump serves, and on the acceptor's first-frame read. The session MUST continue. This replaces
  `L-004-4`'s session-fatal handling. A candidate whose BodyLength or length exceeds L is not a garble,
  even inside a garbled region: it closes (FR-013).
- **FR-002**: After a garbled frame, framing MUST resume at the next frame start (contract C-1).
  - At a frame boundary (the first byte received, or the first byte after a complete frame), a frame
    starts at `8=` as today. After a garble, the next frame start is the next `8=FIX`, whatever byte
    precedes it, searched from after the garbled frame's first byte, or from after its own end for a
    wrong-CheckSum frame consumed whole (contract C-1).
  - A structurally complete frame whose CheckSum value is wrong is one garbled frame through its own
    end.
  - The frames delivered and the garbled-frame counter MUST NOT depend on how the stream is segmented
    into reads. A garbled region that spans several reads counts as **one** garbled frame. The events
    and log records do depend on the segmentation, because FR-003 emits one event per feed.
  - Bytes before the frame start are discarded. A well-formed, complete frame lying wholly after a
    garbled region MUST NOT be lost. A frame lying inside a wrong-CheckSum frame's extent is part of
    that garble.
  - Amortised over any segmentation, the Framer's work per received byte MUST be at most a constant
    times (1 + L ÷ R + the BeginString cap + the BodyLength digit cap), where R is the pump's read size
    (contract C-1 W-1 to W-4). Both caps bound encoded bytes, not values. It never steps one byte per
    feed, and never re-feeds per garble.
- **FR-003**: Every disregarded garbled frame MUST be counted in a per-session garbled-frame counter, and
  MUST be recorded in a session event and in the log, as follows. This follows §4.5.2's "should log
  each encountered garbled message", rate-bounded.
  - **Counter.** Exact and monotonic, readable from C++ and through the C ABI (FR-007), from any thread.
  - **Event.** One `session_event_garbled_frame` per Framer feed that opened at least one garbled
    region, and one per criterion-3 frame. It carries the first kind, the number of frames and the
    bytes discarded. The kind is the Framer's error, or `wire_header_out_of_order` for "35 not third".
  - **Log.** At most one record per `max(HeartBtInt, 1 s)`, so a HeartBtInt of 0, which is legal and
    disables liveness, still bounds the rate. A record carries the kind and bytes of the garble that
    triggered it, and the number counted since the previous record (contract L-11).
  - **Before the Session exists.** Garbled frames seen during the acceptor's first-frame read are handed
    to the Session after `open()` as one summary, which counts them and emits one event. A connection
    that never yields a Session is not counted, and its close is silent (contract L-6).
  - A garbled frame is a transport observation, not an action on the frame's content. It is counted,
    evented and logged even after `close()` began (FR-030 carves it out).
  - The session has no production log site today, and `SessionConfig::logger_override` is never read.
    FR-003 adds the first site (`FIXPP_SLOG` with the session's `trace_context`), resolving the logger
    at `open()` (research R-2; plan.md OD-5).
- **FR-004**: A frame whose third field is not MsgType(35) MUST be disregarded in every state except
  Disconnected (which ignores every frame, contract C-2) and in both validation modes, whether or not it is otherwise faulty. B-005-7 narrows to fields other than the first
  three, and its pinned cell is rewritten to assert the disregard. Research R-1 confirmed this
  (Clarifications).
- **FR-005**: 092's pre-Active refusal of a faulty frame whose third field is not 35 (contract C-2, the
  D-1 and D-2 rows for that shape) MUST become a disregard, now that FR-006 bounds establishment. FR-004's
  check runs before 092's fault branch in every arm, so it takes those frames and 092's D-8 row becomes
  unreachable. Every other 092 faulty-frame disposition is unchanged.
- **FR-006**: `SessionConfig` MUST carry an establishment timeout T (R-4) with a 10 s default. A zero
  value MUST be refused by `Engine::register_session` and by `open()` with the invalid-session-config
  error. The deadline is one absolute time per connection, running until the session first reaches
  Active, in two phases (contract C-4).
  - **Expiry is checked, not raced.** At every loop head of the read pump before the first Active,
    before each read and before each frame's delivery, the deadline MUST be tested against the clock. A read that blocks is raced
    against the deadline only so that it wakes. Which arm asio completes first when both are ready
    MUST NOT decide expiry (contract C-4).
  - **Phase (a), acceptor before a Session exists.** The clock starts at accept. The existing first-frame
    read is bounded by the smaller of its own deadline and the time remaining, and by its unchanged
    byte budget. When no time remains after the TLS handshake, the transport MUST be closed without a
    read. On TLS, phase (a) lasts at most `max(T, the handshake bound)`, because 093 does not shorten
    the handshake bound. On expiry or over-budget the raw transport MUST be closed. No event can be
    recorded, because no Session exists (contract L-6).
  - **Phase (b), a Session that has not yet reached Active.** On the acceptor this is from the first
    frame's delivery, against the same deadline. On the initiator it starts when `drive_reconnect`
    returns, which is after it has installed the transport and sent the Logon (contract C-4). On expiry an event MUST be recorded and `close(terminal)` MUST be
    called, which closes the transport and ends in Disconnected.
  - Phase (b) also ends a pre-Active connection the session has already refused into Disconnected,
    whose transport stays open today (fixpp#534's pre-Active half).
  - The deadline races the waits for peer bytes. It does not race the local suspensions inside frame
    processing (store hydrate, store reset, the Logon reply write). The condition: a peer cannot hold
    them open. A store operation that never completes hangs the session in any state, as today.
  - The deadline is measured on the engine's clock and ignores `SessionConfig::clock_override`, because
    phase (a) begins before a Session exists.
  - It MUST survive a clock-wide sleep cancellation by re-arming, as `await_deadline` does (fixpp#536).
  - Once the session first reaches Active, the deadline MUST have no effect, with or without an
    application attached (US2 AS-3).
- **FR-007**: The C ABI MUST expose `fixpp_session_config_set_logon_timeout_ms` and
  `fixpp_session_garbled_frame_count`.
  - The setter refuses a null handle and zero.
  - The getter refuses a null handle and a null `out`, and reports 0 before the session exists.
  - Both are added to the symbol golden and the C-ABI freeze hashes.
  - Python exposes the setter through the existing automatic binding. The getter needs an explicit
    out-parameter typemap and a row in the GIL table (research R-7).
  - The TOML loader MUST accept the timeout key as a bare integer of milliseconds, as
    `logout_disconnect_timeout_ms` is. It MUST refuse zero, a negative value, a value above
    `UINT32_MAX` and a non-integer. `logout_disconnect_timeout_ms`'s mapper accepts zero, so its range
    check is not a pattern to copy.
- **FR-008**: A well-framed frame whose BeginString value does not match the session's MUST keep today's
  handling, unchanged by 093. It is not a garbled frame. Today's handling is Disconnected with no Logout
  and no close in Active, a Logon refusal before Active, and a transport close on the acceptor's first
  frame. TC 2020 2i's "send Logout" is not met there, which is fixpp#534's to settle.
  - Carve-out: a BeginString value longer than the BeginString cap is garbled at framing (FR-001;
    contract C-1 W-2), as §4.5.2's "not a defined profile identifier" allows. The cap is never shorter
    than the configured BeginString, so a session's own BeginString is always framed. A value within
    the cap that does not match keeps the handling above.

**#515: every admitted frame parses (R-2)**

- **FR-010**: The session MUST have one inbound limit L: the advertised MaxMessageSize when it is
  configured, otherwise 64 KiB. One function derives L from the `SessionConfig`. `open()` stores it,
  the pump reads it from the Session, and the accept loop computes it before a Session exists.
  - The read pump's Framer limit is L, and so is the acceptor's first-frame Framer limit. The pump's
    carry holds L plus one read, so a frame of exactly L never fails because of how the stream was
    segmented. The carry is allocated at `open()`, and an allocation failure is an `open()` error.
  - The parse index capacity and the offset-table entry cap are derived from L at the densest legal field
    layout, 3 bytes per field. An empty value is legal.
  - `Engine::register_session` and `open()` MUST refuse an advertised MaxMessageSize above 256 KiB
    (owner ruling), or below 4096, with the invalid-session-config error. The 4096 floor is plan.md
    OD-2.
  - Only C++ can set 383. C, Python and TOML sessions always get L = 64 KiB.
- **FR-011**: Every admitted frame MUST parse at every inbound parse site without a resource failure, on
  every build lane. That includes a frame of L bytes at the densest legal field layout.
  - A lazy read inside an application callback (repeating-group slices, nested tables, unknown-field
    lists, C-ABI cursors) draws on a stated headroom, `kCallbackReadHeadroom`, in the same buffer.
  - Exhausting the headroom in a read that reports a status (the table in contract C-3 I-5) MUST NOT
    end the session. The failed read reports what its declaration allows, per API: an empty span from
    `group_slices()`, `FIXPP_ERR_TYPE_MISMATCH` from `fixpp_msg_get_group`,
    `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` from the nested getter, and an empty view from `unknown_fields()`
    (FR-015). These are today's reports except the last, and B&L discloses the two that cannot be told
    from an absent result (contract L-5).
  - These reports hold on the lanes where the parse buffer's spill witness is null. On MSVC debug the
    witness forwards to the heap and records, so a read past the headroom succeeds from the heap and
    the spill is recorded instead (research R-3).
  - The C cursor shells' allocation (`fixpp_msg_get_group`, `fixpp_group_get_nested_group`) is the
    exception. It has no catch, so an exhaustion there ends the session, or the process terminates, as
    it does today (contract L-17, fixpp#541: unconfirmed, pre-existing on main, batch B28, not fixed by
    093; plan.md OD-19).
- **FR-012**: Parse capacity MUST be allocated once per session, not per frame. Stack use per parse MUST
  NOT grow (`[const §VIII.5]`).
- **FR-013**: A frame larger than L MUST be refused at framing in every state, the acceptor's first
  frame included, before its CheckSum is read, so an over-L frame is never mistaken for a garbled one.
  The check-order change applies only to Framers with resync on (plan.md OD-4).
  - No guard or handler reads any field of the frame.
  - Once a Session exists (contract C-4 phase (b), and Active), the session closes terminally with a
    log record, and no `SessionEvent` (plan.md OD-24). On the acceptor's first frame (phase (a)) no Session exists yet, so the transport
    is closed with no event and no log (contract L-6).
  - This replaces today's Active-only advertised-MaxMessageSize check, which only writes Disconnected.
  - It also reverses 070's pre-establishment exemption (`test_070_max_message_size_test`); see plan.md
    OD-3, which stood through Gate A's convergence (plan.md `## Gate A`).
- **FR-014**: 092's late-parse-failure close (092 FR-016) MUST remain as a defence. It MUST be
  unreachable for an admitted frame, and a test that deletes the capacity derivation MUST turn it RED.
- **FR-015 (fixpp#540)**: `MessageView::unknown_fields()` MUST NOT reach `std::terminate` when its arena
  is exhausted. It keeps `noexcept`, and an internal catch returns an empty view (contract C-3 I-5).
  - **Reproduce first.** Before the catch lands, #540's reproduction runs on the base: a `Parser` over a
    small `monotonic_buffer_resource` whose upstream is `null_memory_resource`, sized so the parse
    succeeds and the unknown-field list does not fit, then `unknown_fields()` under a death test. The
    upstream is set explicitly, so the result does not depend on the lane. Its output is recorded in the
    evidence file.
  - The behaviour cell asserts that the call returns an empty view and the process lives (quickstart
    Q-32). If the reproduction terminates on the base, that cell is RED on the base and the PR settles
    #540. If it does not terminate, #540 is closed as not a bug with that output, the catch stays as
    hardening that changes nothing observable, and Q-32 is reclassified as a regression guard.

**#516: liveness (R-1)**

- **FR-020**: Every inbound frame that reaches the LogonReceived/Active arm and is neither garbled nor
  faulty MUST refresh inbound liveness. The refresh happens once, right after the fault check and the
  35-not-third check, and before the validate gate and every early return. It replaces the single writer
  at the end of the arm. That covers the validate and PossDup Rejects, the CompID and SendingTime guards,
  the Reset-mode SequenceReset, too-high, too-low (with or without PossDup), GapFill, the knob-off path
  (`validate_sequence_numbers = false`, where 028 delivers a frame without advancing NextNumIn), inbound
  Reject(35=3) and inbound Logout(35=5). A refresh in LogonReceived is harmless: the liveness loop runs only in Active,
  and both roles seed the value on entering Active.
- **FR-021**: Faulty and garbled frames MUST NOT refresh liveness (092 FR-018; this spec's Disregard).

**#523: a closing session**

- **FR-030**: Once `close()` has begun, or `Engine::stop()`'s step 1 has run on the session's strand
  (plan.md OD-28), the NotConnected and LogonSent arms MUST return success without effect: no callback, no counter advance, no event, no Reject, no state write and no outbound frame.
  Returning an error would make the pump call `close()` again for no gain.
  - The LogonReceived, Active and LogoutSent arms are unchanged, so phase 1's Logout confirmation path
    still works.
  - Garbled-frame accounting is not an arm effect (FR-003). That covers the pump's accounting and
    the arm's own 35-not-third disregard, its counter increment and its event, which run before this
    check (contract C-2 steps 1 and 2).
  - FR-030 MUST land no later than FR-041, because the initiator's reset unit is otherwise entered after
    close began.

**#524: atomic reset unit (R-3)**

- **FR-040**: `MessageStore` MUST gain one non-pure virtual that resets the store and sets the next
  inbound and next outbound numbers to given values as one durable unit.
  - It takes `next_in, next_out ∈ {1, 2}` and refuses any other value with no effect. Its only caller
    passes values in that set.
  - Its default body runs `reset()` and then one advance for each target that is 2.
  - `MemoryStore` and `FileStore` MUST override it so that no reader, and no restart after a crash, can
    observe the intermediate state.
  - Article XIV §2 counts pure virtuals (at most 5), and the interface keeps its four.
- **FR-041**: The 141=Y reset unit MUST, in both roles (contract C-6):
  - be unreachable by cancellation from start to end, because `Engine::stop()`'s cancellation
    reaches the unit before any `close()` does. Steps up to the store operation have no suspension, and
    the store operation runs on an empty cancellation slot, so no emission can reach it (plan.md
    OD-25, amended at implementation 2026-10-04; it replaces the in-place disable of OD-14);
  - set the `SeqnumManager` to its targets, with no suspension in between, under L-518-1's
    uncontended-grant condition, which closes `L-518-1`'s drain residual;
  - then issue one `reset_to(in, out)` with the true targets, on persistent and volatile stores alike;
  - then restore the pump's cancellation state (dropping any cancellation recorded while the unit ran) and run the existing superseded check, which also tests a
    session-side engine-stop flag that `Engine::stop()` sets before it emits cancellation (contract
    C-6). So after `Engine::stop()`'s step 1 has run on the session's strand, the arm emits no event,
    calls no `toAdmin` or `onLogon`, writes nothing, and does not reach Active. Effects the arm ran on
    that strand before stop's step-1 handler are ordered before it, and `stop()`'s normal sequence
    handles that session as one that reached Active just before step 1 reached its strand. This holds
    on the condition contract C-6 states: every suspension in a Logon arm is followed by the
    predicate before the next effect.

  `close()` MUST wait for an in-flight unit before it issues its teardown reset, with a timeout (owner
  ruling). It waits only when it is about to issue one, and the wait is event-driven, not a poll. The
  bound and the expiry behaviour are plan.md OD-1.
  - Result when `close()` or `Engine::stop()` begins at any point of the unit: with no teardown reset
    configured, the unit's targets (in, out), for every store: in = 2 when the Logon advanced, and
    out = 2 only on the initiator's own reset (contract C-6, research R-6). With one configured, 1/1 for
    every store while the wait does not expire. After expiry, 1/1 still holds for an overriding store
    with a FIFO writer lock, but not for a default-body store (disclosed).
  - `Engine::stop()` waits for an in-flight unit's store operation, which is not cancellable
    (disclosed).
  - The in-unit `teardown_reset_done_` stops become dead and are removed. The flag remains as `close()`'s
    single-fire latch.
- **FR-042**: An existing C++ `MessageStore` subclass MUST compile without changes. The existing
  test-local subclasses, built with `-Werror`, are the witness. With the shield and `close()`'s wait it
  gets FR-041's outcomes too. Its crash atomicity is not guaranteed, and neither is the (1, 1) outcome
  after an expired wait (disclosed in B&L).

**Cross-cutting**

- **FR-050**: B&L:
  - move `L-004-4` to the closed file;
  - narrow `B-005-7`;
  - add B rows for FR-001 to FR-041;
  - add L rows for the residuals, contract C-8 L-1 to L-17:
    - the BodyLength stall, in which the FSM reaches Disconnected while the transport stays open;
    - the failure kind reported where the §4.5.2 criterion is meant;
    - the custom-store crash atomicity;
    - `close()`'s bounded wait expiring, for default-body stores;
    - the callback-read headroom, with each lazy read's report on exhaustion;
    - the unobserved pre-Session connection: no count and a silent close;
    - the resend-loop guard;
    - an embedded frame found by resync is delivered;
    - event-ring eviction;
    - 383 settable from C++ only;
    - rate-bounded garble logging;
    - `Engine::stop()` waiting on a shielded unit;
    - the pre-Active deadline race's allocations, as measured;
    - the underived admin and outbound parse arenas;
    - a frame inside a wrong-CheckSum frame's extent;
    - no resync for a configured BeginString that does not begin with `FIX`;
    - the C cursor shells' uncaught allocation;
  - add a B row for the per-session cost: the formula, the 64 KiB and 256 KiB worked totals, and the
    admission bound (one carry plus B(L) per registered session with a live or establishing
    connection, research R-3);
  - update the 092 rows that state D-8's disregard or the pre-Active refusal of a faulty frame whose
    third field is not 35 (`B-092-2`, `B-092-9`, `L-092-1` are leads; FR-005 makes D-8 unreachable),
    and the row stating D-9 (LogoutSent): its frames whose third field is not 35 stay disregarded and
    are now counted, evented and logged (contract C-2);
  - update `L-092-6` and `L-518-1` where this feature changes what they state. That includes
    L-518-1's "Frames that arrive with the Logon" bullet. Its LogonReceived half becomes by design,
    because FR-030 leaves LogonReceived unchanged on purpose: a graceful close from LogonReceived runs
    phase 1's Logout exchange, which must process inbound frames as an Active graceful close does.
    #523 then closes with no B&L bullet still calling it open.
- **FR-051**: C-ABI version: one MINOR bump that carries the new setter and getter, and declares the
  behaviour changes BREAKING (`[const §X.7]`, following 091 and 092). BREAKING is marked in the doc
  block of each C declaration through which a change is observed. `version.h`'s history entry is
  headed BREAKING with a one-line pointer per change, and details an effect only where no declaration
  carries it. The 1.10 sentences 093 falsifies are amended in place. Contract C-7's matrix
  names each declaration and each amended bullet. The BREAKING changes:
  - a garbled frame no longer ends the session, and two shapes that were framed are now garbled: a
    BeginString over its cap and a BodyLength digit run over its cap;
  - a 35-not-third frame is no longer processed, and a Logon of that shape is disregarded, not refused;
  - a frame over L now closes in every state, and a frame of at most L is admitted however the stream
    is segmented, where today the carry refuses some frames near 64 KiB by segmentation;
  - a late parse failure no longer ends the session for an admitted frame, because it cannot occur;
  - a pre-Active connection now closes at the establishment timeout, including one already refused into
    Disconnected;
  - liveness refreshes on more frames.

  Not BREAKING:
  - the 383 refusals, which are reachable from C++ only and so are not C-ABI. They are a C++ behaviour
    change, declared in the B&L delta and among the PR description's C++ deltas (contract C-7 row 10);
  - #523 and #524, following B-518-1's owner ruling that the old outcomes were the defect. These two
    issues are that ruling's follow-ups.
- **FR-052**: No new heap allocation on the per-frame inbound path once the session has first reached
  Active. The resync search runs over the existing buffer in place. Before the first Active, the
  establishment deadline race may allocate. Whether it does is measured in the verify record and
  disclosed (contract L-13), not asserted. This is the B15 (#497) hot-path check, made explicit at Gate A.
  - The race's disarm at the first Active is witnessed by behaviour: a session with no application
    reaches Active before T, idles past T, and stays Active (quickstart Q-36).
  - A regression cell drives the real pump past Active under a global `operator new` counter, shown
    first to count a known allocation, and asserts zero per Active read after a warm-up read (Q-19).
    It is not claimed to catch a race that is never disarmed, because asio's recycling allocator can
    serve the race's state without calling `operator new`.
- **FR-053**: Test access to private state MUST go through `tests/support/*_test_access` (B21). Nothing
  in a class may be gated on `FIXPP_TEST_HOOKS`, which B25's census enforces.

### Key Entities

- **Garbled-frame event**: one per Framer feed that disregarded at least one garbled frame, and one per
  35-not-third frame. It carries the first failure kind, the number of frames and the bytes discarded.
- **Garbled-frame counter**: per session, exact and monotonic, readable from C++, C and Python.
- **Inbound limit L**: one per session, derived at open. It sizes the Framer limit, the carry, the parse
  index and the offset-table entry cap.
- **Reset-unit-in-flight flag**: per session. It is set across the unit's single store await, which
  is shielded from cancellation. `close()` waits on its completion signal with a bound before a
  teardown reset.
- **Engine-stop flag**: per session. `Engine::stop()` sets it on the session strand before it emits
  cancellation, and the Logon arms' superseded check reads it.
- **Establishment timeout**: a per-session duration, configurable from C++, C, Python and TOML.
- **Store reset unit**: the new `MessageStore` operation, which takes the next inbound and next outbound
  numbers.

## Success Criteria *(mandatory)*

### Measurable Outcomes

**The base.** "RED on the base" below means RED on the merge base that tasks.md T002 records after
the rebase onto `origin/main` (`00c1f720` at spec time). Every RED claim is run on that base.

- **SC-001**: Each of TC 2020 Scenario 2 rows d, m and t and Scenario 3 rows b, c and e has a cell
  asserting disregard-and-continue. Each is RED on the base and GREEN on the branch.
- **SC-002**: Resync cells show the following. Each has a mutant that turns it RED (quickstart §2).
  - A well-formed frame lying wholly after a garbled region is never lost. That includes leading junk
    that does not end in SOH (`XYZ8=FIX…`) and a truncated frame followed by a good one, at every split
    point.
  - A buffer of only garbage is consumed in finite steps.
  - On the adversarial shapes, the Framer's work per fed byte stays under the bound of contract C-1
    W-1 to W-4, measured by a counted-work instrument:
    - `8=␁` triples behind a failed large candidate;
    - small frames behind a failed large candidate;
    - nested candidates sharing one `10=`;
    - `8=FIX` repeated with no SOH;
    - a boundary `8=` with no SOH;
    - a `9=` followed by a run of zeros with no SOH;
    - each of these fed one byte per read as well as in full reads.
  - A valid frame whose BodyLength is zero-padded to exactly the digit cap is framed and delivered, and
    one padded to the cap plus one is garbled, each fed whole and one byte per read.
- **SC-003**: The establishment timeout, per phase (contract C-4). The timeout is set from each of C++,
  C and Python.
  - **Initiator, after its Logon**, and **acceptor after a matching first frame that leaves it
    pre-Active**: a peer that then sends only garbled bytes is disconnected **at** the timeout, not
    before. On the base the cell is RED because the close is immediate.
  - **Same phase, silent peer**: disconnected at the timeout. On the base it is never disconnected
    (RED, bounded by the test's deadline).
  - **Same phase, a peer that keeps the socket readable across T**, through a transport double whose
    read completes at initiation so the read arm wins every race: no frame is delivered after T, and
    the session closes at the first loop head after T.
  - **Same phase, a session with no application** that reaches Active before T and idles past it stays
    Active.
  - **Acceptor before any matching frame**: a peer that sends only garbled bytes is closed at the
    first-frame byte budget or at `min(5 s, T)`, whichever comes first. On TLS the 5 s runs from the
    end of the handshake and T from accept (contract C-4 phase (a)); the cells below that name
    `min(5 s, T)` run on plain TCP, where the two coincide. Bytes under the budget, sent
    slowly, are not closed before `min(5 s, T)`. On the base that cell is RED because the close
    comes at the first garbled byte.
  - **Acceptor on TLS with T below the handshake bound**: a stalled handshake closes at the handshake
    bound; a handshake that completes after T closes the transport without a first-frame read.
- **SC-004**: On every CI lane, MSVC debug included, a frame of L bytes at the densest field layout
  parses, and a frame of L+1 bytes is refused at framing before any guard acts. The dense-L cell is RED
  on the base.
- **SC-005**: For each FR-020 class that leaves the session up, a cell shows that no TestRequest is sent
  within the interval. Each is RED on the base. The classes are:
  - one too-high frame (a second one may be fatal, fixpp#537);
  - Reset-mode SequenceReset;
  - GapFill;
  - the validate and PossDup Rejects;
  - too-low Heartbeat and too-low PossDup;
  - the knob-off path (`validate_sequence_numbers = false`: a frame delivered without advancing
    NextNumIn, 028's deliver-without-advance);
  - Reject(35=3).

  Logout and a fatal too-low end the session, so they have no such cell. So do the CompID guard (a
  mismatch, when `check_comp_id` holds, writes Disconnected) and the SendingTime guard (Reject, then
  Logout, then disconnect): FR-020 refreshes on them, but no interval follows in which a TestRequest
  could be sent.
- **SC-006**: Per role, the #523 cell and the #524 cell without a teardown reset are RED on the base
  and GREEN on the branch. Two cells stay green throughout as regression guards: the #524 cell with a
  teardown reset (its 1/1 outcome holds on the base), and the LogoutSent confirmation cell.
  - A non-overriding store cell shows that `close()`'s wait gives FR-041's outcomes. Deleting the wait
    turns it RED.
  - `Engine::stop()` cells begin during the unit, for `MemoryStore`, `FileStore` and a default-body
    store, with and without a teardown reset. Each shows FR-041's table, and per role shows no
    `toAdmin`, no reset event, no `onLogon` and no Active transition after `Engine::stop()`'s step 1
    has run on the session's strand. Deleting the
    shield turns the default-body and `FileStore` cells RED. Dropping the engine-stop flag from the
    superseded check turns the per-role effect assertions RED.
- **SC-007**: The per-session memory added by FR-010 and FR-012, the carry plus B(L), is measured and
  stated in the B&L row as a formula in L, with worked totals at 64 KiB and 256 KiB. Steady-state inbound
  throughput is unchanged within `[const §VIII.2]`'s budget.
- **SC-008 (fixpp#540)**: #540's reproduction (FR-015) is run on the base before the catch lands, and
  its output is recorded. If it terminates, the no-terminate cell is RED on the base and GREEN on the
  branch, and the PR settles #540. If it does not, #540 is closed as not a bug with that output, and the
  cell is kept as a regression guard.

## Assumptions

- The acceptor's existing first-frame deadline and byte budget stay as they are, and the budget keeps
  counting discarded bytes. The establishment timeout shortens the deadline when less time remains, and
  covers the rest of establishment.
- The densest legal field layout is a one-digit tag with an empty value, `1=<SOH>`: three bytes per field
  (research R-3). That sets the worst-case field count for L. It holds while the header scan and
  `OffsetTable::build` accept an empty value: re-derived at implementation by tasks.md T007's
  dense-frame measurement, which parses frames at that layout on the base. Making an empty value a
  scan fault is a separate decision, outside 093; it would leave B(L) an over-estimate, which is safe,
  and change only which dense frames the Q-11 cells can build.
- LFIXT (§5.4.12) is not a supported profile, so its "terminate on garbled" rule does not apply.

## Out of scope (follow-ups to file)

- Entering `Disconnected` without closing the transport, on paths other than the establishment timeout:
  a refused Logon, an unanswered TestRequest, and today's MaxMessageSize breach before FR-013. The plan
  re-derives this. Only FR-006's expiry closes the transport here: a pre-Active refused Logon's
  transport closes at T, not at the refusal, and closing it at the refusal is #534's. Filed as
  fixpp#534 (batch B27).
- `test_request_threshold` is computed and never read (`run_liveness_loop`). Filed as fixpp#535 (batch B27).
- A resend-loop guard for a garbled frame retransmitted identically (§4.5.2's recommendation).
- The size limit of the acceptor's bounded first-frame read.
- A lazy `open()` failure inside the role loops is silent, and on the acceptor it ends the accept loop
  for good. That already applies to a null dictionary. 093 moves its two new refusals to
  `register_session`, and the general case is a follow-up. Filed as fixpp#550 (batch B31).
- The admin and outbound parse sites' stack arenas are not derived from what they parse, so a dense
  outbound body can skip its send callback (contract C-3 I-6, L-14). Filed as fixpp#549 (batch B31).
- The C cursor shells that the two group getters allocate from the parse arena have no catch (contract
  L-17). Unconfirmed, found by code reading at Gate A round 2. Filed as fixpp#541 (batch B28), with a
  reproduce-first item; not in 093's scope (plan.md OD-19).
- `fixpp_msg_get_group` reports an exhausted `group_slices()` as `FIXPP_ERR_TYPE_MISMATCH` (contract
  L-5). Changing that report changes a C-ABI result, so it is left as it is and disclosed.
