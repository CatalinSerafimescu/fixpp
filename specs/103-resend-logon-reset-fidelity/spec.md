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

### Session 2026-11-14 (plan phase: owner rulings on design findings; research D5′, D9)

- Q: The #581 ruling says a too-low Heartbeat ends "like every other too-low frame" with a Logout, but fixpp sends
  no Logout for any too-low frame (Arm B disconnects silently; B-013-1's "Logs out" is false by source). Which
  ending? → A: **every** frame below NextNumIn without 43=Y, the Heartbeat included, draws a Logout with QuickFIX's
  text `MsgSeqNum too low, expecting <N> but received <M>` (QuickFIX C++ and J `doTargetTooLow`), then
  `Disconnected` (FIX-SL §4.8.1; all three engines). Owner ruling.
- Q: Does the replay serialisation also cover a send already numbered and in its store write when an answer
  starts, and the session's own Heartbeat / TestRequest and `close()`'s Logout? → A: **yes, all of them** (QuickFIX
  C++'s session mutex, QuickFIX/Go's `resendMutex`), with no terminal cancellation filter and no allocation on the
  normal send path. Owner ruling; research D5′.
- Q: A peer's 141=Y acknowledgement at 34=1 that fixpp did not ask for, while the hydrated NextNumIn is above 1?
  → A: out of 103's rulings; disclosed as a limitation with a follow-up issue (research D2.3). Orchestrator.
- Q: A too-high Logout with `logout_disconnect_timeout_ms` = 0, or on a session with no clock? → A: acknowledged at
  once with no ResendRequest of fixpp's own (the QuickFIX ending). Orchestrator; research D4.2.
- Q: What does "wait for the peer's disconnect" after the acknowledgement mean? → A: the in-sequence peer Logout's
  ending: `Disconnected`, transport open until the peer's EOF, `close()` or `Engine::stop()`; not `close(graceful)`'s
  timed wait. Orchestrator (research Risk R2).

### Session 2026-11-14 (Gate A round 1; owner rulings)

- Q: Article XI §3 names `async_mutex` "the only allowed mutex shape for coroutines", and the replay gate (research
  D5′) is a coroutine shared/exclusive exclusion primitive; Article XX §1 requires the article to be amended
  first. How is the conflict resolved? → A: **the Article XI §3 amendment rides 103's PR** (MINOR, Article XX;
  precedent 097's v4.3): it names a strand-confined shared/exclusive gate — no OS primitive, no thread exclusion,
  the caller's cancellation filter left unchanged — as a second permitted coroutine mutex shape, for 103's replay
  gate. The exact replacement text and its Sync Impact Report entry are
  [contracts/constitution-xi3-amendment.md](contracts/constitution-xi3-amendment.md); applying it is the first
  implementation step, before any D5′ code. Owner ruling.
- Q: SC-002 asks for a live restart-and-resume scenario, but the 029 restart-resume interop suites are not live
  (fixpp#503). Absorb #503, or re-scope? → A: **SC-002 is re-scoped; fixpp#503 stays separate.** SC-002 becomes
  the existing live default-config Logon cells, once the interop helpers stop forcing a policy, plus an
  in-process FileStore restart-and-resume cell against the peer fixture. The live restart-and-resume run is
  #503's. Owner ruling.
- Q: FR-018 says every too-low frame without 43=Y draws a Logout, but a too-low frame that fails dictionary
  validation is Rejected at the validate gate first (research D9). Which ending? → A: **the carve-out stays:** a
  too-low frame without 43=Y that fails dictionary validation is Rejected at the validate gate and the session
  stays Active, as QuickFIX C++ does (its `Session::next` runs `DataDictionary::validate` before `verify`);
  unparseable and garbled frames keep their 093 dispositions. Owner ruling.
- Q: Is the `/plan` sign-off still good after the round-1 delta (D1's clear at `install_reconnected_transport` with
  the connection generation, D4's bound protocol and `close()` join, the XI §3 amendment riding 103)? → A:
  **re-confirmed** (2026-11-15); Appendix A control 4 is discharged for the round-1 bundle. Owner ruling.
- Q: A direct `Session::drive_reconnect()` from Active can install a new transport while an emit of the old
  connection is in flight (a send in its store call, a claimed acknowledgement), and that frame is then written on
  the new transport. Accept, refuse from Active, or drain? → A: **drain, then reconnect** (2026-11-15): from Active
  or LogonReceived it first records `Disconnected` and waits for every in-flight emit to finish (the replay gate's
  shared holders: sends in their store call, liveness frames, `close()`'s phase-1 Logout; a claimed acknowledgement;
  any other emitter holding an outbound number), then installs the new transport (the connection generation moves,
  the resend state and the mark are cleared). No frame numbered or claimed on the old connection may be written on
  the new one. Owner ruling; research D1.

### Session 2026-11-15 (Gate A loop 1 exhausted; owner rulings)

- Q: The Engine's read pump holds the installed transport by reference, so a direct `drive_reconnect` on an
  Engine-managed Session destroys that transport under the pump's outstanding read, and nothing reads the new one
  (Gate A round 3, RC-α). Fence it, or disclose only? → A: **fence Engine-managed:** `drive_reconnect` refuses with
  `session_already_open` when the Session is Engine-managed (`cfg_.engine_managed`) and already has an installed
  transport, the case where the Engine's read pump owns it; the Engine's own first call has no transport and is
  unaffected. A standalone Session keeps the drain, with a documented precondition: the caller's reader must have
  left `live_transport()` before the call and starts on the new `live_transport()` after it returns. Owner ruling;
  research D1 step 1a.
  - **Erratum (Gate A loop 2 round 1), pending owner confirmation at Gate A sign-off.** The ruling's condition is
    "where the Engine's read pump owns the installed transport"; its parenthetical named only `cfg_.engine_managed`,
    which the Engine sets for initiators alone. The Engine's acceptor loop also runs a read pump on its Session's
    transport, so the fence is `cfg_.role == session_role::acceptor || (cfg_.engine_managed &&` a transport
    installed`)`, tested after the closing/closed/stopped test (which returns `session_already_closed`). An acceptor
    is never reconnected in place, so refusing every acceptor removes nothing supported.
- Q: How does the bundle continue after loop 1's round-3 exhaustion? → A: **hand-edit plus one targeted round:** the
  pre-loop-2 edit applies RC-α (above), RC-β (the Logout owner moves into `inbound_logout_leaf_`), RC-γ and the
  round-3 P3s, then a fresh Gate A loop runs. Owner ruling.

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

1. **Given** a resend outstanding, **When** the session reconnects in place, whether or not it passed through
   `Disconnected` first (a direct caller of `drive_reconnect` can install a new transport from Active), **Then** it
   reaches the new connection with no resend outstanding (flag, range and deadline start cleared) and no
   pending-Logout mark.
2. **Given** that reconnected session, **When** a frame above NextNumIn arrives within 2 × HeartBtInt, **Then** fixpp
   sends a ResendRequest from NextNumIn.
3. **Given** the same with the clock advanced past 2 × HeartBtInt during the downtime, **When** the first too-high
   frame arrives, **Then** it draws a ResendRequest, not the deadline Logout.
4. **Given** Active with a resend outstanding and a too-high peer Logout pending, **When** the transport is dropped
   and `drive_reconnect()` is called from Active, **Then** it records `Disconnected` first, and after the new Logon
   exchange reaches Active no resend is outstanding, no Logout of the old connection's mark is sent at T, and
   `Session::send` is admitted.
5. **Given** Active with a send held in its store call (or a claimed acknowledgement held in its store call),
   **When** `drive_reconnect()` is called, **Then** it records `Disconnected`, waits until that frame's store and
   write end on the old transport, and only then installs the new transport; the frame is never written on the new
   transport (a standalone Session; its reader follows the precondition of FR-006).
6. **Given** an Engine initiator or acceptor whose read pump is reading the installed transport, **When** the
   application calls `drive_reconnect()` directly, from Active or from the `Disconnected` that follows a Logout
   acknowledgement, **Then** it returns `session_already_open`, the session's state is unchanged, the installed
   transport is untouched, and the pump keeps reading it.

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
6. **Given** a too-high Logout pending, **When** the peer sends a TestRequest or an in-sequence ResendRequest,
   **Then** fixpp answers it (a Heartbeat; the replay) as it does with no mark.
7. **Given** the mark's acknowledgement claimed and in its store call (or any other Logout of the session in its
   store call: an in-sequence acknowledgement, the deadline's, Arm B's), **When** the application calls
   `close(graceful)`, **Then** exactly one Logout (the one in flight) is written before the transport closes; and,
   the other way round, an inbound ending that meets `close(graceful)`'s Logout in flight sends none of its own.

In every scenario fixpp sends at most one Logout for the condition, and exactly one except in the cases FR-010
enumerates.

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
5. **Given** Active with dictionary validation on, **When** a frame below NextNumIn without 43=Y fails dictionary
   validation, **Then** it is Rejected at the validate gate and the session stays Active, with no Logout (QuickFIX
   C++ validates before `verify`). An unparseable or garbled frame keeps its 093 disposition.

### Edge Cases

- A strict acceptor receives a 141=Y Logon whose 34 is not 1: unchanged by this feature (disclosed in the B&L delta).
- A too-high Logout arrives while a resend is already outstanding: the pending-Logout mark is set and no second
  ResendRequest is sent; the acknowledgement follows User Story 3's rule.
- A too-high Logout's bound and the gap deadline both apply: the first to expire acts; the pending-Logout bound
  (`logout_disconnect_timeout_ms`) ends with the acknowledgement, the gap deadline with its Logout; at most one
  Logout is sent (exactly one except FR-010's enumerated cases).
- The peer disconnects before fixpp's acknowledgement: no acknowledgement is sent; `Disconnected` as today (one of
  FR-010's enumerated cases).
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
  flag, the outstanding range and the gap deadline's start are cleared before the new Logon is sent, when the new
  transport is installed, whether or not the session passed through `Disconnected` before it. Recovery state of
  the old connection (the resend state, the pending-Logout mark and its bound) MUST NOT act on the new
  connection. A `drive_reconnect` called in Active or LogonReceived MUST first record `Disconnected` and wait until
  no emit of the old connection is in flight (no frame numbered or claimed there is still being stored or written)
  and no detached coroutine of the old connection (its liveness loop, the mark's bound) is alive, and only then
  install the new transport; no frame numbered or claimed on the old connection MAY be written on the new one. The
  emit wait is bounded by `logout_disconnect_timeout_ms`, after which the old transport is force-closed so a blocked
  write unwinds. After its closing/closed/stopped test (`session_already_closed`), `drive_reconnect` MUST refuse
  with `session_already_open` and change nothing on an acceptor, or on an `engine_managed` initiator that already
  has an installed transport (the Engine's read pump owns that transport; owner ruling 2026-11-15, the acceptor
  disjunct an erratum pending owner confirmation; a standalone initiator configured `engine_managed` is treated the
  same, disclosed); on a standalone initiator the caller's reader MUST have left `live_transport()` before the call
  and starts on the new `live_transport()` after it returns (a documented precondition, not detected). The operation
  (research D1): one at a time (a second concurrent call returns
  `session_already_open` and connects nothing); started from a callback, it runs after one post, as `close()` does;
  a `close()` or `Engine::stop()` that starts during it, during the drain or during connect and handshake, ends it
  with `session_already_closed`, no install, and the newly connected transport closed (the install is the commit
  point); the caller's own cancellation of the drain's wait ends it with `dispatch_aborted` and no install, while
  one that reaches the backoff, connect or handshake keeps today's result (fixpp#585).
- **FR-007**: The header documentation of the resend state MUST state when it is reset, matching FR-006.

**Too-high ResendRequest and Logout (#577)**

- **FR-008**: A ResendRequest whose 34 is above NextNumIn MUST be answered as an in-sequence ResendRequest with the
  same 7 and 16 is answered, whether or not a resend is outstanding. NextNumIn MUST NOT move for it.
- **FR-009**: At most one ResendRequest of fixpp's own MUST be outstanding; a too-high ResendRequest or Logout opens
  one only if none is outstanding.
- **FR-010**: In Active, a Logout whose 34 is above NextNumIn MUST set a pending-Logout mark (its 34 and the time
  it arrived) and draw a ResendRequest per FR-009. (Active is the only state that can see one: LogonReceived is
  entered and left inside the Logon's own inbound chain, and inbound frames are serialised, so no other frame is
  processed there; research D4.2.) The session MUST send its Logout acknowledgement when NextNumIn passes the
  mark's 34, or when `logout_disconnect_timeout_ms` has elapsed since the mark, whichever comes first. With a value
  of 0, or on a session with no clock, it MUST acknowledge at once and send no ResendRequest of its own. After the
  acknowledgement the session ends as after an in-sequence peer Logout: `Disconnected`, the transport open until
  the peer's EOF, `close()` or `Engine::stop()`. Across every ending that competes for the mark (the
  acknowledgement, the gap deadline, the peer's in-sequence Logout, `close(graceful)`, a too-low frame's Logout)
  the session MUST send **at most one** Logout, and **exactly one** except in these cases, each disclosed in its
  B&L row (research "B&L delta"):
  1. the Logout does not fit its leaf's frame buffer because of the CompIDs (B-103-7 for the acknowledgement; B-537-2
     and B-103-10 for the deadline and too-low Logouts);
  2. `toAdmin` throws on the Logout (`app_callback_threw`, `Disconnected`; B-103-7, B-103-10);
  3. the Logout's sequence-number assignment or store call fails, or its number is taken back while it is stored
     (D-OUT-1), which every Logout leaf, `close()`'s phase 1 included, fails closed on (`Disconnected`; B-103-7,
     B-103-10);
  4. the transport ends before the Logout is written: the peer's EOF or a read error (the read pump's
     `close(terminal)`), `close(terminal)`, or `Engine::stop()`, including a terminal close started inside the
     Logout's own `toAdmin`, whose Logout is stored and not transmitted (fixpp#590, rulings R-8 and R-9) (B-103-7);
  5. `close(graceful)` whose `logout_disconnect_timeout_ms` grace runs out before the Logout is written, because a
     resend answer or the claimed acknowledgement's own store and write outlast it (L-103-6).
  One Logout is owned at a time, session-wide (research D4.6): `close(graceful)` that finds any Logout in flight
  (the acknowledgement, an in-sequence acknowledgement, the deadline's, Arm B's) MUST wait for it, inside its
  grace, before it records `Disconnected` and closes the transport, and send none of its own; an inbound ending that
  finds one in flight (phase 1's included) MUST skip its own Logout and keep the rest of its ending.
- **FR-011**: While the pending-Logout mark is pending (before its acknowledgement is claimed), `Session::send`
  MUST return `session_invalid_state_for_send` (the result after a peer's in-sequence Logout), and administrative
  traffic MUST continue: a TestRequest is answered and a ResendRequest is serviced. Once the acknowledgement is
  claimed, inbound frames are drained until its `Disconnected`, as after an in-sequence Logout. The mark MUST end
  with its connection: it has no effect once the session leaves Active, and it is cleared when a new transport is
  installed (with FR-006's resend state).

**BeginSeqNo (#580)**

- **FR-012**: A ResendRequest with BeginSeqNo(7)=0 MUST be answered exactly as one with 7=1. No outbound frame MAY
  carry 34=0.
- **FR-013**: A ResendRequest without tag 7 MUST draw Reject 373=1 (Required tag missing) with RefTagID(371)=7 and
  QuickFIX's Text(58) for that reason, MUST NOT be replayed, and MUST consume its sequence number like any Rejected
  frame (FIX-SL §4.5.4).

**Replay serialisation (#588)**

- **FR-014**: Every outbound frame fixpp numbers outside the inbound chain — `Session::send`, the liveness loop's
  Heartbeat and TestRequest, `close()`'s Logout, the pending-Logout acknowledgement — MUST NOT be written inside a
  resend answer: one numbered after the answer started is numbered and written after the answer's last frame, and
  one numbered before it started (including a send suspended in its store write) is written before the answer's
  first frame.
- **FR-015**: An emitter while no answer is in progress MUST NOT wait and MUST NOT allocate for this (no added
  latency on the normal path), and the serialisation MUST NOT leave a terminal cancellation filter on the caller.
  A wait that its caller's own cancellation ends returns `dispatch_aborted` with the session's state unchanged, no
  number consumed, and the caller's cancellation still pending (as `send_deferred_`'s hop returns it; research
  D5′ "Cancellation").
- **FR-016**: FR-014's ordering MUST hold whatever the answer's ending (complete, store fault, disconnect).

**Gap endings (#581)**

- **FR-017**: While a resend is outstanding and NextNumIn has not advanced for 2 × HeartBtInt, the next inbound frame
  of any MsgSeqNum that does not advance NextNumIn MUST draw the B-537-2 Logout ("ResendRequest unanswered,
  expecting MsgSeqNum " + NextNumIn) and `Disconnected`. A frame that advances NextNumIn is processed as today and
  restarts the deadline. The deadline's start and restart rules are otherwise B-537-2's. "Inbound frame" excludes
  a frame the session cannot read (a garbled frame the Framer disregards, B-093-1…B-093-3, and a faulty frame
  disposed of before any handler reads it, the 092 B-rows): FIX-SL §4.5.2 treats a garbled message as never received,
  and such frames do not refresh liveness (B-093-9), so a peer that sends only those ends at the TestRequest grace
  check and the gap cannot stay open without end (research D6). No Logout is sent where the CompIDs overflow the
  Logout buffer (B-537-2).
- **FR-018**: In Active (LogonReceived sees no frame but the Logon itself; FR-010), on a session with sequence
  validation on, gap or not, every frame below NextNumIn without 43=Y, the Heartbeat included, that reaches the
  sequence check MUST draw a Logout whose Text(58) is `MsgSeqNum too low, expecting <N> but received <M>`
  (N = NextNumIn before the frame, M = its MsgSeqNum), then `Disconnected`, without advancing NextNumIn. Where the
  gap deadline (FR-017) has expired, its Logout is the one Logout. Not covered, each keeping its ending: a frame
  that fails dictionary validation is Rejected at the validate gate and the session stays Active, as QuickFIX C++
  does (`Session::next` runs `DataDictionary::validate` before `verify`; owner ruling, Gate A round 1); a frame
  that fails the CompID or SendingTime check takes that check's ending; an unparseable or garbled frame keeps its
  093 disposition. No Logout is sent where the CompIDs overflow the Logout buffer, or where `toAdmin` throws on it
  (FR-010's cases 1–3).

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
- **Pending-Logout mark**: the too-high Logout's 34, its arrival time, and the connection it arrived on; cleared
  when the acknowledgement is sent, when the session disconnects, or when a new transport is installed.
- **Connection generation**: a per-session count, moved when a new transport is installed; connection-scoped
  recovery work (the mark's bound) acts only while the generation it started on is current.
- **Resend answer in progress**: from the answer's entry, before it reads the store's tail, to its end, on every
  ending; outbound emitters outside the inbound chain wait for it, and it waits for emitters already holding a
  number.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each of the six issues has at least one cell that is RED on `2c74af92` and GREEN at the feature's head
  (or, for #577/#578, a reproduction that is GREEN on the base, recorded as not-a-bug). Each RED is shown, and each
  fix's mutant (reverting it) turns its cell RED again.
- **SC-002** (re-scoped by owner ruling, Gate A round 1; the live restart-and-resume run stays fixpp#503's): with no
  interop helper forcing a policy, the live default-config Logon cells pass against each QuickFIX counterparty
  they run against: `Fix44/HappyLogonHbLogout.LogonHeartbeatLogout`, `Fixt/HappyFixtLogonHbLogout.LogonHeartbeatLogout`,
  `Fix44/ResetOnLogonInitiator.LogonAcceptedAndResyncs`, `Fix44/ResetOnLogonAcceptor.AdmitsFresh34eq1AndResyncsFrom1`
  and `Fix44/HappyDisconnectReconnectNoReset.ReconnectPreservesSequenceContinuity` (the one that logs on at a
  stored number above 1, so the one that fails if the default resets; re-derive the live set from
  `tests/interop/expected-skips-without-counterparty.txt` minus `tests/interop/live-cells-excluded.txt`). And an
  in-process FileStore restart-and-resume cell, in both roles, against the peer fixture (User Story 1's Independent
  Test and its acceptor twin) passes with the default configuration and fails with the strict default restored.
- **SC-003**: No outbound frame in any test carries 34=0 (an assertion in the resend-answer cells).
- **SC-004**: In every L-537-1 residual-pattern cell, the session ends within one frame after 2 × HeartBtInt without
  an advance.
- **SC-005**: A send started during a replay is written after the replay's last frame in 100 % of runs of the held-
  retrieve cells (deterministic: the hold forces the interleaving) and of the multi-threaded cell R-588-h under the
  sanitizer lanes, R-588-h's repeat count derived from its mutant's measured miss rate (research R-588-h).
- **SC-006**: The normal-path send latency, and the normal inbound-frame path (an Active Heartbeat and an Active
  application message through parse → validate → dispatch, `inbound_depth_`'s holder included), each in a paired
  base-vs-candidate run on one runner against the merge-base with the base worktree path padded, do not regress
  beyond the bench gate's tolerance.

## Assumptions

- The C ABI keeps its enum values; only the default and its documentation change (BREAKING per the clarification).
- The too-low Logout text for FR-018 is QuickFIX's, which Arm B sends from 103; fixpp sends none at the base (Arm B
  is silent).
- Gate A is required: the feature changes wire behaviour and a default configuration.
- QuickFIX/J 3.0.2 and QuickFIX/Go v0.9.12 were checked for changes on these paths (parent
  `research/reviews/quickfix-engines-update-2026-11-14.md`): none changes a ruling; the research already read QFJ
  and QFC at master. The interop pin moves to QuickFIX/J 3.0.2 separately (owner, 2026-11-14), so SC-002's live
  Logon cells run against a peer with QFJ #902 fixed.

## Normative References

Entries are the exact `[DocAbbrev §X.Y.Z] Title` keys of `spec/coverage-index.md` (Article VI §5). Detail below a
key (a table, a precedence row, a sub-case) is in the prose after it. Every point this spec cites has a key; no
coverage-index row needs adding.

- `[FIX-SL §4.1] Sequence numbers` — sequence numbers start at 1; no frame carries 34=0 (FR-012).
- `[FIX-SL §4.4.2] Using ResetSeqNumFlag(141) for 24-hour connectivity` — 34=1 on a 141=Y Logon, after both numbers
  are set to 1 (FR-002, FR-003).
- `[FIX-SL §4.4.3] Using ResetSeqNumFlag(141) during connection establishment` — the refusal by Logout with Text
  (FR-004).
- `[FIX-SL §4.5.2] Garbled message processing` — a garbled message is treated as never received (FR-017's
  exclusion).
- `[FIX-SL §4.5.3] Missing sequence number (gap detection → ResendRequest)` — the ResendRequest for a gap (FR-008,
  FR-009, FR-010).
- `[FIX-SL §4.5.4] Rejecting invalid messages (Reject 35=3)` — a Rejected message consumes its number (FR-013).
- `[FIX-SL §4.6.3] Logout with retransmission of missed messages` — the recovery before a Logout's acknowledgement
  (FR-010).
- `[FIX-SL §4.8.1] Ordered message processing` — a too-low message without PossDupFlag ends the session with a
  Logout (FR-018).
- `[FIX-SL §4.8.2] Request retransmission of messages (ResendRequest)` — at most one outstanding request of
  fixpp's own (FR-009).
- `[FIX-SL §4.8.3] Responding to ResendRequest(35=2)` — the answer to 7=0 and to a too-high request (FR-008,
  FR-012).
- `[FIX-SL §4.8.5] Gap fill process (SequenceReset-GapFill)` — GapFill numbering (FR-012, FR-014).
- `[FIX-SL §4.8.8] Processing gaps for session layer messages (admin msg gap-fill)` — its Table 3, "Response by
  message type", the action on MsgSeqNum > NextNumIn for a ResendRequest and a Logout (FR-008, FR-010).
- `[FIX-SL §4.10] FIX session state matrix` — precedence rows 14 and 15, the too-high Logout's bounded recovery
  (FR-010).
- `[FIX-TC §4 scenario 1B] Connect and Send Logon (Initiator)` — sub-case 1B(c): a too-high Logon on a new
  connection draws a ResendRequest (FR-006).
- `[FIX-TC §4 scenario 1S] Receive Logon (Acceptor)` — sub-case 1S(a), the same on the acceptor (FR-006).
- `[FIX-TC §4 scenario 2] Receive Message Standard Header` — sub-case 2c, MsgSeqNum too low (FR-018).
- `[FIX-TC §4 scenario 8] Receive Resend Request message` — the answer to a ResendRequest (FR-008, FR-012).
- `[FIX-TC §4 scenario 13] Receive Logout message` — the acknowledgement of the peer's Logout (FR-010).
- `[FIX-TC §4 scenario 14] Receive application or session layer message` — sub-case 14b, a required tag missing
  draws Reject 373=1 (FR-013).
- `[FIX-TC §4 scenario 20] Simultaneous Resend request` — a ResendRequest is serviced whatever its MsgSeqNum
  (FR-008).

Informative, not coverage-index entries: the reference engines' session source (QuickFIX C++, QuickFIX/J,
QuickFIX/Go) at the SHAs in the pre-spec research's `_engine_shas.txt`, for observable behaviour.

## Out of Scope

- A general session send gate (#544 R-10) and ordered emission inside `Session` beyond the resend answer (the
  scoped gate of research D5′ is not one).
- A 141=Y Logon received with 34 ≠ 1.
- Holding application frames during gap recovery (L-013-2 stands).
- #684 (L-509-1, PossDup with unparseable 52): batch B65.
