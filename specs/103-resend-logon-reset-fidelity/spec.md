# Feature Specification: Resend and Logon-reset fidelity — QuickFIX's and FIX-SL's endings for sequence recovery

**Feature Branch**: `103-resend-logon-reset-fidelity`

**Created**: 2026-11-14

**Status**: Draft

**Base**: `2c74af92` (feature 102's head, PR #680, owner rule 2026-11-14: new work branches from the open PR's tip).
Rebased `--onto origin/main 2c74af92` once PR #680 squash-merges. Feature 102 changes no file under `src/session`,
`include/fixpp/session` or the session C ABI.

**Input**: batch B43 (fixpp#580, #577, #581, #578, #588) plus fixpp#682 (B&L L-029-3), the owner's v1.0 goal 1
("no known bugs"). Pre-spec research, which reads every point below against the FIX Session Layer (Nov 2020), the
FIX Session Test Cases (June 2020) and the session source of QuickFIX C++, QuickFIX/J and QuickFIX/Go: parent repo
`research/G19-fix-fpml-iso20022/research/reviews/opus_103_preSpec_research_2026-11-14.md` (engine SHAs in its
`_engine_shas.txt`). The owner's rulings are posted as a comment on each issue.

## Context & Problem

A counterparty sees six behaviours on the sequence-recovery and Logon-reset paths that differ from QuickFIX's, from
the FIX Session Layer's, or from both:

1. **The default reset policy (#682).** fixpp's default `reset_seqnum_policy` is `bilateral_strict`. Under it an
   initiator puts ResetSeqNumFlag(141)=Y on **every** Logon, with MsgSeqNum(34) = its stored next-outbound number and
   no reset first, so a restarted FileStore initiator sends `141=Y` with `34=N` (N > 1). The initiator's LogonSent arm
   then checks the peer's acknowledgement's 34 against the hydrated NextNumIn **before** it reads 141, so a
   conforming acknowledgement (141=Y, 34=1) is too low whenever NextNumIn > 1, and the session ends silently there;
   when it does get past the acknowledgement, the next outbound number becomes 1 while the peer has already counted
   the Logon. A strict acceptor refuses, silently, any Logon without 141=Y. QuickFIX C++ (`shouldSendReset`),
   QuickFIX/J (`isResetNeeded`) and QuickFIX/Go (`shouldSendReset`) send 141=Y only with a reset setting on and both
   numbers at 1, always at 34=1 after resetting their store, and all three accept a Logon without 141. FIX-SL §4.4.2
   requires 34=1 on a 141=Y Logon, after both numbers are set to 1. `bilateral_lenient` already matches QuickFIX on
   every observable read. No interop cell runs the default policy: every interop helper forces `bilateral_lenient`.
2. **BeginSeqNo(7)=0 (#580).** A ResendRequest with 7=0 is answered with a GapFill whose 34 is 0. An absent tag 7 is
   read as 0 and answered the same way. QuickFIX C++ and J start the answer at the first stored frame; all three
   engines and Test Case 14b answer an absent tag 7 with Reject 373=1.
3. **A too-high ResendRequest or Logout (#577, unconfirmed).** The too-high gate runs before any MsgType dispatch, so
   a ResendRequest or Logout above NextNumIn only opens fixpp's own gap: the request is never answered, and the
   Logout is never acknowledged (the peer gap-fills over it). FIX-SL Table 3, Test Case 20 and all three engines
   service a too-high ResendRequest. For a Logout, FIX-SL Table 3 and §4.10 precedence 15 say: request the gap, wait a
   bounded time, then acknowledge; the engines acknowledge at once and disconnect.
4. **An unfilled gap against a peer that sends nothing too high (#581, L-537-1).** The gap deadline (B-537-2) is
   checked only at a frame above NextNumIn, so a peer that sends only frames at or below NextNumIn that move nothing
   keeps the session Active with the gap open, without end. Separately, a too-low Heartbeat without PossDupFlag(43)=Y
   is ignored, with or without a gap; FIX-SL §4.8.1 and all three engines end the session with a Logout, as fixpp
   does for every other too-low frame without 43=Y.
5. **A live send inside a resend answer (#588).** The replay suspends between frames, and `Session::send` checks only
   the session state, so a live application frame can reach the wire between two replayed frames. QuickFIX C++ and Go
   serialise the replay against live sends; QuickFIX/J does not.
6. **Resend state across an in-place reconnect (#578, unconfirmed).** The awaiting-resend flag, the outstanding
   range and the deadline's start are cleared only when the gap closes, never on a reconnect. A session that
   reconnects in place with a resend outstanding treats the first too-high frame on the new connection as "already
   requested": it is ignored, or it trips the old deadline. All three engines clear resend state on disconnect, and
   Test Cases 1B(c)/1S(a) expect a fresh ResendRequest on the new connection. The reset route (`reset_on_logon`, or
   item 1's fix) reaches this with no further setting.

## Clarifications

### Session 2026-11-14 (owner rulings; research `opus_103_preSpec_research_2026-11-14.md`)

- Q: #682 — which reset policy is the default, and what does `bilateral_strict` mean? → A: **`bilateral_lenient`
  becomes the default** (C++, C ABI, TOML). `bilateral_strict` stays as an opt-in and is fixed: a Logon carrying
  141=Y always carries 34=1, built after both numbers are durably reset to 1; the LogonSent arm accepts the peer's
  141=Y acknowledgement at 34=1; the strict acceptor's refusal of a Logon without 141=Y sends a Logout with Text
  (FIX-SL §4.4.3) instead of a silent disconnect.
- Q: #580 — 7=0, and an absent tag 7? → A: 7=0 is answered exactly as 7=1 is, so no frame carries 34=0 (QuickFIX
  C++ and J). An absent tag 7 draws Reject 373=1 with RefTagID(371)=7 and no replay (all three engines, Test Case
  14b). This supersedes B28's R-10 deferral.
- Q: #577 — a too-high ResendRequest? → A: serviced as if it were in sequence; NextNumIn does not move; at most one
  ResendRequest of fixpp's own is outstanding.
- Q: #577 — a too-high Logout? → A: **follow FIX-SL, bounded** (Table 3, precedence 15; consistent with B-016-1):
  send a ResendRequest, then send the Logout acknowledgement once the gap closes past the Logout's MsgSeqNum, or when
  `logout_disconnect_timeout_ms` expires, whichever comes first; then wait for the peer to disconnect. fixpp keeps a
  "peer Logout pending at 34=S" mark, not the frame. A named exception to the owner's "keep QuickFIX's observable
  behaviour" rule: FIX-SL is explicit here and its route is bounded, and QuickFIX peers keep servicing a
  ResendRequest during their logout timeout.
- Q: #581 — an unfilled gap that nothing above NextNumIn trips? → A: while a resend is outstanding and NextNumIn has
  not advanced for 2 × HeartBtInt, the next inbound frame of **any** MsgSeqNum draws the B-537-2 Logout and
  `Disconnected` (supersedes ruling R-11). And a too-low Heartbeat without 43=Y draws a Logout and disconnect, like
  every other too-low frame without 43=Y. *(Narrowed by the `/speckit-clarify` session below: a frame that advances
  NextNumIn is processed and restarts the deadline.)*
- Q: #588 — a live send during a resend answer? → A: serialised, scoped to the replay: the live frame reaches the
  wire after the answer's last frame (QuickFIX C++ and Go). Not a general session send gate (#544 R-10 stands).
- Q: #578 — resend state across an in-place reconnect? → A: no ruling needed (docs and all three engines agree): a
  reconnected session starts with no resend outstanding.

### Session 2026-11-14 (`/speckit-clarify`; each answer is the stated recommendation, owner pre-approved: "go ahead with everything, plan is good")

- Q: Is changing the C-ABI default reset policy (enum values unchanged) a breaking C-ABI change under Article X §7,
  and what does it bump? → A: **BREAKING.** A consumer that never sets the policy sees a result other than the one
  documented (strict), which §7 defines as breaking. It bumps `FIXPP_C_ABI_VERSION_MINOR` to the next value after
  the base's (1.16 at `2c74af92` → 1.17), is marked BREAKING in `fixpp_reset_seqnum_policy`'s documentation, the PR
  description and the B&L delta, updates every in-repository consumer in the same PR (Python binding, tests,
  examples, interop harnesses), and takes all four Appendix A controls (clarify, analyze, Codex Gate A, the owner's
  `/plan` sign-off).
- Q: How long does fixpp wait for a gap before acknowledging a too-high Logout? → A: **`logout_disconnect_timeout_ms`**
  (default 2000 ms), no new setting. It matches QuickFIX's LogoutTimeout default (2 s), which is how long a QuickFIX
  peer that sent the Logout waits for the acknowledgement; a longer bound would see the peer disconnect first. A
  value of 0 acknowledges at once (the QuickFIX ending).
- Q: May the application send while a too-high peer Logout is pending? → A: **No.** `Session::send` returns
  `session_invalid_state_for_send`, as it does after a peer's in-sequence Logout: the condition "the peer asked to
  log out" has one ending for the application whatever the Logout's MsgSeqNum. Administrative traffic (resend
  answers, Heartbeats, TestRequests) continues.
- Q: Does #581's deadline end the session on a late frame that fills or advances the gap? → A: **No.** The deadline
  acts only on a frame that does not advance NextNumIn. A frame that advances it (a replay, a GapFill) is processed
  as today and restarts the deadline, so a late but real fill is never cut off.

## User Scenarios & Testing *(mandatory)*

Every item is test-first: a cell that is RED on the base `2c74af92` comes before its fix. #577 and #578 are
unconfirmed: the first cell of each is a reproduction, and if it is GREEN the issue closes as not-a-bug with the
evidence recorded.

### User Story 1 — A restarted initiator logs on to a QuickFIX acceptor with the default configuration (Priority: P1)

An operator restarts a FileStore initiator that has exchanged messages, with every reset setting at its default. It
logs on and resumes at its stored numbers, as a QuickFIX initiator does.

**Why this priority**: it is the default path of every persistent deployment, and today it sends a Logon FIX-SL
forbids and then disconnects or desynchronises.

**Independent Test**: a default-configured initiator over a store at {in=37, out=42}, cold open, against a peer
acceptor fixture: the Logon carries 34=42 and no 141; the peer's in-sequence acknowledgement reaches Active.

**Acceptance Scenarios**:

1. **Given** a default `SessionConfig` and a store at {in=37, out=42}, **When** the initiator opens, **Then** its
   Logon carries 34=42 and no 141=Y, and an acknowledgement at 34=37 reaches Active.
2. **Given** `bilateral_strict` and the same store, **When** the initiator opens, **Then** the store is at {1, 1}
   before the Logon is built, the Logon carries 141=Y and 34=1, an acknowledgement with 141=Y and 34=1 reaches
   Active, and the next application message carries 34=2.
3. **Given** a strict acceptor, **When** a Logon without 141=Y arrives, **Then** the acceptor sends a Logout whose
   Text(58) names the missing reset and disconnects.
4. **Given** a default acceptor, **When** a Logon without 141=Y arrives, **Then** it is accepted (as today under
   `bilateral_lenient`).
5. **Given** any policy, **When** fixpp sends a Logon with 141=Y, **Then** it carries 34=1 (the option-neutral
   invariant).

### User Story 2 — A reconnected session asks for the new gap (Priority: P1)

A session that loses its connection while a resend is outstanding reconnects in place. On the new connection a
too-high frame draws a ResendRequest for the new gap.

**Why this priority**: today the session can sit with a gap it never requests, or end at once on a stale deadline; and
User Story 1's strict fix and `reset_on_logon` reach it with no other setting.

**Independent Test**: reach Active, open a gap, lose the connection, `drive_reconnect()`, then feed a too-high frame:
a ResendRequest for the new gap is written. Control: a fresh Session given the same steps writes the same request.

**Acceptance Scenarios**:

1. **Given** a resend outstanding, **When** the session reconnects in place, **Then** it reaches the new connection
   with no resend outstanding (flag, range and deadline start cleared).
2. **Given** that reconnected session, **When** a frame above NextNumIn arrives within 2 × HeartBtInt, **Then** fixpp
   sends a ResendRequest from NextNumIn.
3. **Given** the same with the clock advanced past 2 × HeartBtInt during the downtime, **When** the first too-high
   frame arrives, **Then** it draws a ResendRequest, not the deadline Logout.

### User Story 3 — A too-high ResendRequest is answered; a too-high Logout is acknowledged after recovery (Priority: P2)

A peer that is ahead sends a ResendRequest or a Logout whose 34 is above NextNumIn.

**Why this priority**: FIX-SL Table 3 and Test Case 20 (mandatory) require servicing the ResendRequest; today the
peer's request is never answered and its Logout never acknowledged.

**Independent Test**: from Active, send two application messages, then feed a ResendRequest (7=1, 16=0) at
34 = NextNumIn+3: the two messages are replayed with 43=Y and one ResendRequest of fixpp's own is written.

**Acceptance Scenarios**:

1. **Given** Active with no gap, **When** a ResendRequest above NextNumIn arrives, **Then** the requested range is
   answered as for an in-sequence request, fixpp sends one ResendRequest from NextNumIn, and NextNumIn does not move.
2. **Given** a resend already outstanding, **When** a ResendRequest above NextNumIn arrives, **Then** it is answered
   and no second ResendRequest of fixpp's own is sent.
3. **Given** Active, **When** a Logout above NextNumIn arrives, **Then** fixpp sends a ResendRequest; when the peer's
   replay or GapFill moves NextNumIn past the Logout's 34, fixpp sends its Logout acknowledgement once, then waits for
   the peer to disconnect.
4. **Given** the same, **When** the gap is not closed within `logout_disconnect_timeout_ms`, **Then** fixpp sends its
   Logout acknowledgement once at expiry, then waits for the peer to disconnect.
5. **Given** a too-high Logout pending, **When** the application calls `Session::send`, **Then** it returns
   `session_invalid_state_for_send` and nothing is written.

### User Story 4 — A ResendRequest from 0, or with no BeginSeqNo, gets a valid answer (Priority: P2)

**Why this priority**: a frame with 34=0 is invalid in every FIX version; an absent required tag must be Rejected.

**Independent Test**: on the B28 store fixture (Logon at 1, application messages 2 and 3), feed 7=0, 16=0: the first
frame written is a GapFill with 34=1 and 36=2, then 2 and 3 replayed; no frame anywhere carries 34=0.

**Acceptance Scenarios**:

1. **Given** a store holding frames from 1, **When** a ResendRequest with 7=0 arrives, **Then** the answer is
   byte-for-byte the answer to 7=1 (modulo SendingTime).
2. **Given** any store, **When** a ResendRequest without tag 7 arrives, **Then** fixpp sends Reject 373=1, 371=7,
   replays nothing, and consumes the frame's sequence number.

### User Story 5 — A live send does not land inside a replay (Priority: P2)

**Why this priority**: a frame above the answer's range landing mid-replay is ignored by a fixpp-style receiver and
queued by a QuickFIX one; serialising removes the case for every receiver.

**Independent Test**: hold the store's retrieve of the second replayed frame, start `Session::send` while it is held,
release: every frame with 34 above the answer's end comes after the answer's last frame. Control: the same send with
no ResendRequest is written at once.

**Acceptance Scenarios**:

1. **Given** a resend answer in progress, **When** the application calls `Session::send`, **Then** its frame is
   written after the answer's last frame, with the next outbound number.
2. **Given** no resend answer in progress, **When** the application calls `Session::send`, **Then** nothing changes
   from today (no added wait).

### User Story 6 — An open gap always ends (Priority: P3)

**Why this priority**: closes L-537-1, the last gap shape with no ending; and a too-low Heartbeat is the one too-low
frame fixpp ignores.

**Independent Test**: B28's residual-pattern cells: with a gap open, feed only frames at or below NextNumIn that move
nothing; before 2 × HeartBtInt the session waits; the first frame at or after it draws the B-537-2 Logout.

**Acceptance Scenarios**:

1. **Given** a resend outstanding and no advance of NextNumIn for 2 × HeartBtInt, **When** an inbound frame that
   does not advance NextNumIn arrives (at, below or above NextNumIn), **Then** fixpp sends the B-537-2 Logout
   ("ResendRequest unanswered, expecting MsgSeqNum n") and records `Disconnected`.
2. **Given** the same, **When** the peer's GapFill or replay that advances NextNumIn arrives after 2 × HeartBtInt,
   **Then** it is processed as today and the session stays up.
3. **Given** a resend outstanding, **When** frames keep advancing NextNumIn, **Then** the deadline restarts at each
   advance, as today.
4. **Given** Active, gap or not, **When** a Heartbeat below NextNumIn without 43=Y arrives, **Then** fixpp sends the
   same too-low Logout it sends for any other too-low frame without 43=Y and disconnects.

### Edge Cases

- A strict acceptor receives a 141=Y Logon whose 34 is not 1: unchanged by this feature (disclosed in the B&L delta).
- A too-high Logout arrives while a resend is already outstanding: the pending-Logout mark is set and no second
  ResendRequest is sent; the acknowledgement follows User Story 3's rule.
- A too-high Logout's bound and the gap deadline both apply: the first to expire acts; the pending-Logout bound
  (`logout_disconnect_timeout_ms`) ends with the acknowledgement, the gap deadline with its Logout; exactly one
  Logout is sent.
- The peer disconnects before fixpp's acknowledgement: no acknowledgement is sent; `Disconnected` as today.
- A too-high ResendRequest whose range reaches past what fixpp has sent: answered as an in-sequence request with the
  same range is answered today.
- A ResendRequest with 7=0 on an empty store: unchanged (already clamped to 1).
- A live send during a replay whose answer fails mid-way (a store fault): the send is released when the answer ends,
  whatever its ending.
- HeartBtInt ≤ 0: the gap deadline does not exist, as today; User Story 6 scenario 1 does not apply (disclosed).
- Reconnect with no resend outstanding: unchanged.
- An application or test that relied on `bilateral_strict` being the default without naming it: its behaviour changes
  (the BREAKING default).

## Requirements *(mandatory)*

### Functional Requirements

**Logon reset (#682)**

- **FR-001**: The default `reset_seqnum_policy` MUST be `bilateral_lenient` on every configuration surface (C++
  `SessionConfig`, the C ABI session config, TOML). The C ABI and TOML spellings of every policy are unchanged. The
  change is BREAKING under Article X §7: `FIXPP_C_ABI_VERSION_MINOR` moves to the next value after the base's, the
  BREAKING mark goes on `fixpp_reset_seqnum_policy`'s documentation, and every in-repository consumer is updated in
  the same change.
- **FR-002**: A Logon that fixpp sends with 141=Y MUST carry 34=1, under every policy and every reset setting, and
  both stored numbers MUST be 1 (durably) when it is built.
- **FR-003**: Under `bilateral_strict`, an initiator MUST durably reset both numbers to 1 before building its Logon,
  so FR-002 holds, and MUST accept the peer's acknowledgement carrying 141=Y at 34=1.
- **FR-004**: Under `bilateral_strict`, an acceptor that refuses a Logon without 141=Y MUST send a Logout whose
  Text(58) names the reason before disconnecting (FIX-SL §4.4.3), and MUST still return
  `session_seqnum_reset_mismatch` from the arm.
- **FR-005**: Under the new default, the initiator's and acceptor's 141 behaviour MUST equal QuickFIX's on each
  observable in the research's policy table (141 sent only with a reset setting and both numbers at 1; a Logon
  without 141 accepted; a 141=Y Logon mirrored).

**Resend state across reconnect (#578)**

- **FR-006**: An in-place reconnect MUST start the new connection with no resend outstanding: the awaiting-resend
  flag, the outstanding range and the gap deadline's start are cleared before the new Logon is sent.
- **FR-007**: The header documentation of the resend state MUST state when it is reset, matching FR-006.

**Too-high ResendRequest and Logout (#577)**

- **FR-008**: A ResendRequest whose 34 is above NextNumIn MUST be answered as an in-sequence ResendRequest with the
  same 7 and 16 is answered, whether or not a resend is outstanding. NextNumIn MUST NOT move for it.
- **FR-009**: At most one ResendRequest of fixpp's own MUST be outstanding; a too-high ResendRequest or Logout opens
  one only if none is outstanding.
- **FR-010**: A Logout whose 34 is above NextNumIn MUST set a pending-Logout mark (its 34 and the time it arrived)
  and draw a ResendRequest per FR-009. The session MUST send its Logout acknowledgement exactly once: when NextNumIn
  passes the mark's 34, or when `logout_disconnect_timeout_ms` has elapsed since the mark, whichever comes first (a
  value of 0 acknowledges at once). After the acknowledgement it MUST wait for the peer's disconnect as a graceful
  close does today.
- **FR-011**: While the pending-Logout mark is set, `Session::send` MUST return `session_invalid_state_for_send`
  (the result after a peer's in-sequence Logout), and administrative traffic MUST continue. The mark MUST be
  cleared on disconnect and on an in-place reconnect (with FR-006's resend state).

**BeginSeqNo (#580)**

- **FR-012**: A ResendRequest with BeginSeqNo(7)=0 MUST be answered exactly as one with 7=1. No outbound frame MAY
  carry 34=0.
- **FR-013**: A ResendRequest without tag 7 MUST draw Reject 373=1 (Required tag missing) with RefTagID(371)=7 and
  QuickFIX's Text(58) for that reason, MUST NOT be replayed, and MUST consume its sequence number like any Rejected
  frame (FIX-SL §4.5.4).

**Replay serialisation (#588)**

- **FR-014**: A `Session::send` that starts while a resend answer is in progress MUST NOT write its frame before the
  answer's last frame is written. It MUST be assigned its outbound number when it is written.
- **FR-015**: A `Session::send` while no answer is in progress MUST NOT wait (no added latency on the normal path).
- **FR-016**: FR-014's ordering MUST hold whatever the answer's ending (complete, store fault, disconnect).

**Gap endings (#581)**

- **FR-017**: While a resend is outstanding and NextNumIn has not advanced for 2 × HeartBtInt, the next inbound frame
  of any MsgSeqNum that does not advance NextNumIn MUST draw the B-537-2 Logout ("ResendRequest unanswered,
  expecting MsgSeqNum " + NextNumIn) and `Disconnected`. A frame that advances NextNumIn is processed as today and
  restarts the deadline. The deadline's start and restart rules are otherwise B-537-2's.
- **FR-018**: A Heartbeat below NextNumIn without 43=Y MUST draw the same Logout and disconnect as any other frame
  below NextNumIn without 43=Y, gap or not.

**Records**

- **FR-019**: The B&L file MUST be amended for every behaviour above: L-029-3 retired; L-537-1 retired; B-013-6,
  B-016-1, B-537-1, B-537-2 and L-028-3 amended where they state today's behaviour; new B-rows for the default
  change (marked BREAKING), the strict acceptor's Logout, the 7=0/absent-7 answers, the pending-Logout ending, and
  the replay serialisation.
- **FR-020**: Every test cell that pins today's behaviour of these paths MUST be flipped or re-read in the same
  change, and every test that relies on the default policy without naming it MUST be found by a census, not by
  failures alone.

### Key Entities

- **Reset policy** (`reset_seqnum_policy`): `bilateral_strict`, `bilateral_lenient`, `unilateral`; one per session.
- **Resend state**: the awaiting-resend flag, the outstanding range [begin, end], the deadline's start time.
- **Pending-Logout mark**: the too-high Logout's 34 and its arrival time; cleared when the acknowledgement is sent or
  the session disconnects.
- **Resend answer in progress**: the interval between a ResendRequest's first answer frame and its last, during which
  live sends wait.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each of the six issues has at least one cell that is RED on `2c74af92` and GREEN at the feature's head
  (or, for #577/#578, a reproduction that is GREEN on the base, recorded as not-a-bug). Each RED is shown, and each
  fix's mutant (reverting it) turns its cell RED again.
- **SC-002**: A default-configured fixpp initiator and acceptor pass a live restart-and-resume scenario against each
  QuickFIX counterparty in the interop matrix with no helper forcing a policy.
- **SC-003**: No outbound frame in any test carries 34=0 (an assertion in the resend-answer cells).
- **SC-004**: In every L-537-1 residual-pattern cell, the session ends within one frame after 2 × HeartBtInt without
  an advance.
- **SC-005**: A send started during a replay is written after the replay's last frame in 100 % of runs of the held-
  retrieve cell under the sanitizer lanes (repeat count set in the plan).
- **SC-006**: The normal-path send latency does not regress beyond the bench gate's tolerance.

## Assumptions

- After fixpp's Logout acknowledgement, the wait for the peer's disconnect is today's graceful-close wait.
- The C ABI keeps its enum values; only the default and its documentation change (BREAKING per the clarification).
- The too-low Logout text for FR-018 is the one fixpp already sends for other too-low frames.
- Gate A is required: the feature changes wire behaviour and a default configuration.
- QuickFIX/J 3.0.2 and QuickFIX/Go v0.9.12 were checked for changes on these paths (parent
  `research/reviews/quickfix-engines-update-2026-11-14.md`): none changes a ruling; the research already read QFJ
  and QFC at master. The interop pin moves to QuickFIX/J 3.0.2 separately (owner, 2026-11-14), so SC-002's live
  runs use a peer with QFJ #902 fixed.

## Out of Scope

- A general session send gate (#544 R-10) and ordered emission inside `Session` beyond the replay.
- A 141=Y Logon received with 34 ≠ 1.
- Holding application frames during gap recovery (L-013-2 stands).
- #684 (L-509-1, PossDup with unparseable 52): batch B65.
