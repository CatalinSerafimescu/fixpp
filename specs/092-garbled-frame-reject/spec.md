# Feature Specification: The session never acts on a frame it could not parse

**Feature Branch**: `092-garbled-frame-reject`

**Created**: 2026-09-27

**Status**: Draft (post-round-3 clarify/plan refresh; Gate A loop 2, round 1 applied 2026-09-27)

**Input**: User description: "B18 / fixpp#507 — the session must never act on an inbound frame that
passes framing but fails the parse; its disposition follows the owner ruling of 2026-09-27 (issue
comment https://github.com/CatalinSerafimescu/fixpp/issues/507#issuecomment-5855333315, which
revises #423 row 4). […] Reuse the existing consume_rejected_seqnum_ / emit_session_reject_
machinery. Evidence: the #507 body, the #423 ruling table, and B-423-1." (The full input is the
`/speckit-specify` invocation of 2026-09-27. Its requirements are restated below as FRs.)

**Issue**: fixpp#507 (batch B18 in the parent's `phases/phase-4/issue-batches.md`). The branch name
predates the terminology below and is kept.

---

## Terminology

- **Garbled** means only a frame that fails one of the four framing criteria of FIX-SL 2020 §4.5.2:
  8 not first or a bad profile; 9 not second or a wrong count; 35 not third; 10 not last or a bad
  value. This is the owner ruling's definition.
- **Framed but unparseable** is the input this feature handles: a frame that passes the framing
  checks the Framer performs and in which the header scan finds an encoding fault (data-model E-1).
  In this spec, "fails the parse" and "parse failure" mean that, unless a requirement says "late".
  A **late parse failure** is a full parse that fails at a site after the fault branch on a frame the
  scan found fault-free (a resource failure, or an I-4 breach). Only FR-016 governs it: the session
  closes. Effects that ran before that parse are not undone, and moving the decision ahead of them is
  fixpp#515 (owner ruling O-2).
- **Disregard** is the named disposition "no Reject, no advance, no disconnect". In 092 it applies
  to a framed but unparseable frame, in LogonReceived/Active, whose third field is not MsgType (ruling
  row 1, criterion 3) or whose MsgSeqNum was not read before the failure point (row 5, "Ignore, as
  garbled"), and to such a frame in LogoutSent. A frame that fails a framing check **the Framer
  performs** is not disregarded: it ends the session, as today (`L-004-4`). Its §4.5.2 disregard is
  fixpp#514 (owner ruling O-1).

## Context — what is broken, and what was already decided

A FIX frame can pass every framing check the Framer performs (BeginString and BodyLength first,
BodyLength counts correctly, CheckSum last and correct) and still fail the full parse. Two shapes
reach that state:

- **(A)** a Length+Data pair whose counted extent is not followed by SOH, or runs past the frame
  (fixpp detects this since 091 / PR #510; no other engine surveyed does);
- **(B)** a field whose tag is not a well-formed tag number (e.g. `9x9=1`).

**What fixpp does today** (issue #507, measured at `1f6be466`; re-derived at `d3b58922` by reading
the consumers):

- The session drives its state machine from a header **scan**. The scan silently skips a malformed
  tag and silently stops at a malformed count, and reports neither.
- Every consumer of the full parse treats a failure as "nothing to do":
  - the inbound validate gate reports "no reject";
  - the callback dispatcher skips the callback and reports success.
- So a SequenceReset in Reset mode is **applied** from a frame that failed to parse (#507 T076,
  with inbound validation on and off).
- Logout, TestRequest, ResendRequest and Heartbeat act on scanned fields the same way.
- An application message that fails to parse is **persisted and its sequence number consumed**, but
  it is never delivered and never rejected. It is lost, and neither side is told.
- A Logon with a malformed count is refused (091 FR-020). A Logon with a malformed tag is accepted.
- If MsgSeqNum(34) lies after a malformed count, the scan never reaches it, and the session
  disconnects as if 34 were missing.
- The Framer does not check that 35 is the third field (research R-11).
- A frame that fails a check the Framer does perform (BeginString, BodyLength, CheckSum) ends the
  session: `run_read_pump` stops the pump on a Framer failure (`L-004-4`). The ruling's row 1 said
  "Unchanged" about an "Ignore" that fixpp never did; the owner's correction on #507 (Gate A round 2)
  records this.

**Already decided, not re-opened here.** The owner ruled on #507 on 2026-09-27, after research
across the FIX primary documents, QuickFIX J/C++/n/Go source, OnixS, FIX Antenna, Artio, Fix8,
fix-rs, venue specs, and a Fable consult. The ruling:

- "Garbled" means **only** the four framing criteria. A frame that passes framing and then fails the
  parse is a tagvalue encoding violation. FIX-SL 2020 §9.4 routes that to Reject, and a Reject
  advances NextNumIn (§4.5.4). This revises #423's row "garbled (unparseable, …) → no Reject, no
  advance".
- The Reject follows #423's existing seqnum rows: it advances only when MsgSeqNum equals the
  expected number, and Logon and SequenceReset never advance through the Reject path.
- A frame whose MsgSeqNum(34) was not scanned before the failure point is disregarded (TC2020 17g).
- Any parse failure: no field of the frame is ever acted on, and a parse failure must never read as
  "no reject". For a resource failure of a well-formed frame, owner ruling O-2 (Gate A round 2)
  narrows this within 092 to fail-closed (FR-016); the rest is fixpp#515.
- Ignoring instead is rejected as the default. A deterministically malformed frame is resent
  identically on every ResendRequest, fixpp has no resend-loop guard, and B-423-1 records that
  stall, measured live against QuickFIX-cpp.
- No FIX.4.x / FIXT.1.1 difference. LFIXT (§5.4.12) is not a supported profile. No configuration
  switch.

---

## Clarifications

### Session 2026-09-27

- Q: Which SessionRejectReason(373) does a malformed Length+Data pair carry? → A: **5** (Value is
  incorrect (out of range) for this tag), with RefTagID(371) = the Length tag.
- Q: If a wrong SenderCompID/TargetCompID was read before the failure point, does the session still
  disconnect? → A: **No — Reject only.** The parse-failure disposition runs before the identity and
  SendingTime guards, and no identity field of a failed frame is read.
- Q: Before the session is Active, what happens to a frame that passes framing but fails the parse?
  → A: **Awaiting the first Logon (acceptor) or the Logon reply (initiator): refuse and disconnect
  through the Logon refusal path, whatever its MsgType. LogoutSent: ignore it (no Reject); it is not
  taken as the Logout reply, and the logout timeout runs as usual.**
- Q: How is SC-007 (no stall against QuickFIX) proven? → A: **In-process, with a scripted peer that
  sends the raw malformed bytes and handles our Reject as QuickFIX's `nextReject` does (source-cited).
  The live QuickFIX interop cell is a follow-up issue, placed in B17, which already republishes the
  counterparty image.**

### Session 2026-09-27 (Gate A round 1)

- Q: Does 092 need a C-ABI version bump, given that a Logon with a malformed tag, accepted today, is
  refused through the same path as 091 FR-020's malformed count? → A: **Yes: C-ABI 1.10, declared
  BREAKING (`[const §X.7]`), following 091 FR-020's 1.9 precedent.** The refusal is observable
  through the C API's session-state functions. Research R-8's round-0 "no bump" position is
  replaced (FR-017).
- Q: Does FR-012 (the validator's silent stop) stay in 092 or split into its own issue? → A:
  **It stays in 092 and is specified fully**:
  - the new `core::error` values and their numbering;
  - the C-ABI error mapping and every completeness oracle or test that enumerates error codes;
  - the `reject_reason_map` entries;
  - the Parser iterator's fault accessor, with every stop condition it must report, enumerated
    from source;
  - the validator's behaviour and its test cells.

  (data-model.md E-0, E-4 to E-6; research R-7.)

### Session 2026-09-27 (Gate A round 2)

- Q (O-1): Ruling row 1 says a §4.5.2 framing failure is ignored, "Unchanged", but fixpp ends the
  session on every Framer failure (`L-004-4`). Does 092 implement the §4.5.2 disregard? → A: **No:
  implement §4.5.2 ignore — as its own issue, outside 092.** Within 092 a Framer-detected failure
  (bad BeginString, BodyLength or CheckSum) keeps today's handling: session-fatal (`L-004-4`). Its
  disregard is **fixpp#514**, which also owns a fault-free frame whose third field is not 35 and the
  pre-Active establishment timeout a disregard needs. A framed but unparseable frame whose third
  field is not 35 is disregarded in LogonReceived/Active (D-8, where the liveness loop runs) and
  refused before Active, as today, until #514 adds the timeout (FR-008, FR-009).
- Q (O-2): Does ruling row 6 ("any parse failure") cover a *resource* failure of a well-formed
  frame, with a Reject? → A: **No: file the resource case separately** (**fixpp#515**). Within 092 a
  parse failure at any late inbound site (on a frame the scan found fault-free) is **fail-closed: the
  session closes**. It never reads as success, and there is no per-site Reject table (FR-016).

### Session 2026-09-27 (after Gate A round 3)

- Q: When a faulty frame carries MsgType(35) twice (e.g. `35=D … 35=4 … 9x9=1`), which value
  decides its disposition and fills RefMsgType(372), and must the tests check that value directly?
  → A: **The third field's 35 (`fault_ref_msg_type`) everywhere.** The scan-vs-parse oracle (I-4),
  the differential corpus and the fuzz arm compare it with the third `entries()` element. The
  disposition passes it (never the last-wins `msg_type`) to the seqnum accounting. A duplicate-35
  cell (`35=D…35=4…9x9=1` at the expected number) must be D-5: advance, and 372=D (R3-001).
- Q: Is the D-5 ordering "persist the advance before the Reject; a failed persist disconnects with
  no Reject" pinned the way #423 pinned every earlier Reject call site? → A: **Yes.** A
  "092 disposer (D-5)" case is added to both #423 tables in
  `tests/session/test_persistent_seqnum_hydrate.cpp`: `RejectedInSequence_AdvanceIsPersisted`
  (durable inbound advances) and `RejectedInSequence_PersistFailure_Fatal` (Disconnected, durable
  inbound unchanged, no Reject frame). Each goes RED in a scratch copy when the disposer emits first
  or ignores its persist (R3-003).
- Q: Is the Text-carrying Reject builder a new named function or a `build_reject` overload? → A: **A
  new function, `build_reject_with_text(…, std::string_view text)`.** `build_reject` stays a single
  declaration and delegates to it with an empty text (existing Reject bytes unchanged), so the public
  change is additive and source-compatible without qualification (R3-002).
- Q: After a late parse failure closes the session, what does each late-site cell assert about a
  reconnect? → A: **The observed outcome, per site.** Each late-site cell reconnects after the close
  and asserts whether NextNumIn includes the frame, and so whether the peer's resend is requested or
  the number was consumed. It pins today's per-site behaviour, and B&L L-6 records it; any change is
  #515's (R3-004).
- Q: Which callbacks does "does not invoke the callback" cover for a late parse failure? → A: **Only
  the parse target's receive callback** (`fromAdmin` for an admin frame, `fromApp` for an application
  frame). Lifecycle callbacks the close fires (`onLogout`) and callbacks already fired earlier at that
  site (e.g. `toAdmin` for a confirming Logout) are out of scope (R3-005).

## User Scenarios & Testing *(mandatory)*

The "user" is an operator running a fixpp session against a counterparty whose encoder produces a
frame that is framed correctly but malformed inside.

### User Story 1 — A malformed admin message is never acted on (Priority: P1)

A counterparty sends a SequenceReset, Logout, TestRequest, ResendRequest or Heartbeat that passes
framing but fails the parse. The session must not change its sequence state, reply to a
TestRequest, resend anything, or log out because of that frame's contents. It answers with a
session Reject, so the counterparty learns the frame was malformed.

Scope of User Stories 1 and 2 and of the Reject edge cases below: the session is in LogonReceived or
Active, the frame's third field is MsgType(35), and its MsgSeqNum(34) was read before the fault
(contract C-2 D-4 to D-6). A faulty Logon is User Story 3 (D-1 to D-3); a frame whose MsgSeqNum or
MsgType position cannot be trusted is User Story 4 (D-7, D-8); LogoutSent is D-9.

**Why this priority**: This is the defect #507 files. Acting on a frame fixpp itself judged
malformed lets malformed bytes move the session's sequence numbers (a SequenceReset to 500 is
applied today).

**Independent Test**: Feed a SequenceReset (Reset mode, NewSeqNo=500) at the expected MsgSeqNum
with a malformed field, in both shapes (A) and (B), with inbound validation on and off. Then feed a
conformant Heartbeat at MsgSeqNum 500. The Heartbeat must draw a ResendRequest (the reset was not
applied), and the SequenceReset must have drawn a Reject.

**Acceptance Scenarios**:

1. **Given** an Active session expecting MsgSeqNum 2, **When** a SequenceReset (Reset mode,
   NewSeqNo=500, MsgSeqNum=2) with a malformed tag arrives, **Then** the session sends a Reject with
   RefSeqNum=2, RefMsgType=4 and SessionRejectReason=0, does not advance NextNumIn, and does not apply
   NewSeqNo.
2. **Given** the same session, **When** the same SequenceReset arrives with a malformed Length+Data
   pair after MsgSeqNum, **Then** the outcome is the same, except that SessionRejectReason=5 and
   RefTagID names the Length tag (FR-007).
3. **Given** the same session, **When** a TestRequest that fails the parse arrives at the expected
   MsgSeqNum, **Then** no Heartbeat is sent in reply, a Reject is sent, and NextNumIn advances.
4. **Given** the same session, **When** a ResendRequest that fails the parse arrives, **Then**
   nothing is resent, a Reject is sent, and NextNumIn advances only if MsgSeqNum was the expected
   number.
5. **Given** the same session, **When** a Logout that fails the parse arrives, **Then** the session
   does not reply with a Logout or disconnect because of it. It sends a Reject (FR-003 supersedes
   the no-reject-loop exemption for a frame that fails the parse).

---

### User Story 2 — A malformed application message is rejected, not silently lost (Priority: P1)

A counterparty sends an application message (e.g. NewOrderSingle) that passes framing but fails the
parse. Today it is persisted as received and its sequence number consumed, but it is never delivered
and never rejected. After this feature the counterparty receives a session Reject that identifies
the message, and can resend it with a new MsgSeqNum (FIX-SL 2020 §4.5.4).

The scope stated under User Story 1 applies.

**Why this priority**: silent loss of a business message. Neither side learns of it.

**Independent Test**: Feed an application message with a malformed field at the expected MsgSeqNum,
then a conformant one at the next MsgSeqNum. The first draws a Reject (RefSeqNum = its MsgSeqNum)
and is not delivered; the second is delivered with no ResendRequest.

**Acceptance Scenarios**:

1. **Given** an Active session expecting N, **When** an application message at N fails the parse,
   **Then** the application callback is not invoked, a Reject with RefSeqNum=N is sent, and NextNumIn
   becomes N+1.
2. **Given** the same session, **When** that message arrives at a MsgSeqNum above N, **Then** a Reject
   is sent, NextNumIn stays N, and the message is not delivered (FR-005).
3. **Given** the same session with an Application registered, **When** a well-formed application
   message at N is too large for the engine's parse arena (a resource failure, FR-016), **Then** it
   is not delivered, no Reject is sent, and the session closes (fail-closed; its proper disposition
   is fixpp#515).

---

### User Story 3 — A malformed Logon is refused (Priority: P2)

A Logon that passes framing but fails the parse is refused through the existing Logon refusal path:
the session disconnects and no Reject is sent, for both shapes (A) and (B). Today only shape (A) is
refused. This is a declared BREAKING change of the C ABI (FR-017).

**Why this priority**: a session must not be established on identity and parameters read from a
malformed frame. It is narrower than P1 because 091 already closes shape (A).

**Independent Test**: Feed a Logon with a malformed **tag**, as acceptor and as initiator (the Logon
reply). A malformed count is not a witness here, because 091 FR-020 already refuses it. The session
ends in the refused/disconnected state, and no Reject or Logon reply is sent.

**Acceptance Scenarios**:

1. **Given** an acceptor awaiting Logon, **When** a Logon with a malformed tag arrives, **Then** the
   session refuses it exactly as it refuses a Logon with a malformed count.
2. **Given** an initiator that sent a Logon, **When** the reply Logon has a malformed tag, **Then**
   the session refuses it in the same way.

---

### User Story 4 — A frame whose MsgSeqNum or MsgType position cannot be trusted is disregarded (Priority: P2)

Some malformed bytes make the frame impossible to attribute:
- they come before MsgSeqNum(34), for example a bad SecureDataLen(90)/SecureData(91) pair in the
  header;
- or the frame's third field is not MsgType(35).

Such a frame is disregarded: no Reject, no advance, no disconnect. The next valid message then
exposes the gap and normal recovery runs (TC2020 17g).

**Why this priority**: a Reject needs a trustworthy RefSeqNum. Today the first case disconnects the
session, and the second is Rejected or advanced by a naive reading of "35 was seen".

**Independent Test**: Feed a frame whose malformed field precedes MsgSeqNum, then a conformant
message at the expected MsgSeqNum + 1. The first draws nothing; the second draws a ResendRequest for
the missing number, and the session stays connected.

**Acceptance Scenarios**:

1. **Given** an Active session expecting N, **When** a frame with a malformed Length+Data pair
   before MsgSeqNum arrives, **Then** no message is sent, NextNumIn stays N, and the session stays
   Active.
2. **Given** the same, **When** the malformed field is a malformed tag before MsgSeqNum, **Then** the
   outcome is the same.
3. **Given** the same, **When** a frame `8|9|49=…|35=D|34=N|9x9=1|…|10` arrives (MsgType read, but not
   in the third field), **Then** the outcome is the same, in LogonReceived and Active. (Before
   Active the frame is refused, FR-009.)

---

### Edge Cases

- **MsgSeqNum below expected** on a frame that fails the parse: Reject, no advance. The too-low
  fatal path does not run, because it would act on the frame's PossDupFlag.
- **MsgSeqNum above expected**: Reject, no advance, and that frame triggers no ResendRequest. The
  next valid message detects the gap (FR-005).
- **MsgSeqNum present but not a positive integer** (`34=abc`, `34=0`) before the failure point: it
  counts as not read, so the frame is disregarded (FR-006).
- **PossDupFlag(43)=Y** on a frame that fails the parse: not acted on. FR-003 to FR-006 disposition
  the frame as if the flag were absent.
- **A Reject (35=3) or Logout (35=5) that fails the parse** follows FR-003/FR-004. For a frame that
  fails the parse this supersedes the no-reject-loop exemption, which still holds for a well-formed
  Reject or Logout that fails validation. A loop needs a peer that garbles every Reject it sends
  and also rejects Rejects, and each fixpp Reject answers one peer frame (contract C-2).
- **A duplicate MsgSeqNum(34) on a frame that fails the parse**: the Reject is addressed from the
  first well-formed 34 before the failure point (data-model E-1). A fault-free frame is sequenced
  exactly as today.
- **Wrong CompID or BeginString** on a frame that also fails the parse: no identity field of the
  frame is acted on (FR-001), even one read before the failure point. The frame gets FR-003 to
  FR-006, and the peer's next well-formed frame meets the identity check as usual.
- **More than one malformed field**: the first failure point decides FR-006 and the reason code.
- **The malformed field is the third field**: the third field is not MsgType, so in LogonReceived
  and Active the frame is disregarded (FR-006). Before Active it is refused (FR-009).
- **An oversized frame that also fails the parse**, in Active: the negotiated MaxMessageSize(383)
  guard runs first (it reads only the frame length) and disconnects (FR-002).
- **Before Active**: awaiting the first Logon (acceptor) or the Logon reply (initiator), any frame
  that fails the parse is refused through the Logon refusal path (FR-009), whatever its MsgType and
  whatever its third field. Both arms already refuse every input that is not a valid Logon (contract
  C-2). The pre-Active disregard of a frame whose third field is not 35 is fixpp#514, because it
  needs an establishment timeout. In LogoutSent the frame is disregarded and not taken as the Logout
  reply, and the logout timeout runs as usual (FR-015).
- **A frame that fails a framing criterion the Framer checks** (§4.5.2): unchanged, which is
  session-fatal (`L-004-4`, FR-008). Its disregard is fixpp#514.
- **A well-formed frame too large for the parse arena** (a resource failure): the session closes
  (FR-016). The proper disposition is fixpp#515.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The `Session` MUST NOT act on any field of an inbound frame that passes framing but
  fails the parse (Terminology), on any admin, application, or pre-Active path. There is one exception:
  MsgSeqNum(34), MsgType(35), and whether the third field is MsgType(35) MAY be read, from bytes
  before the failure point, only to address and account for the Reject (FR-003 to FR-006). A parse
  failure MUST NOT read as "no reject" or "dispatch succeeded". Out of this requirement's scope: the
  engine's first-frame routing read of BeginString(8), SenderCompID(49) and TargetCompID(56), which
  selects the session before any `Session` exists. For a faulty Logon its outcome is still refusal
  or close. A late parse failure is governed by FR-016, not by this requirement.
- **FR-002**: A parse failure (Terminology; not a late one, FR-016) MUST be decided before any handler or guard acts on the frame:
  - before the identity (BeginString/CompID) and SendingTime guards;
  - before the SequenceReset, Logout, TestRequest, ResendRequest, Heartbeat and application
    handlers;
  - before the sequence-gap, too-low and PossDup arms.

  The Framer and, in Active, the negotiated MaxMessageSize(383) guard come first. That guard reads
  only the frame length, and an oversized frame is disconnected whether or not it is also
  unparseable (contract C-1 step 1b).
- **FR-003**: When the third field is MsgType(35), MsgSeqNum(34) was read as a positive integer
  before the failure point, and the message is not a Logon, the session MUST send a session
  Reject(35=3) with:
  - RefSeqNum(45) = that MsgSeqNum;
  - RefMsgType(372) = the MsgType read, meaning the third field's value (`fault_ref_msg_type`), never
    a later duplicate 35 (omitted when longer than any MsgType a shipped dictionary defines,
    research R-5);
  - a fixed Text(58) that names the defect.

  This holds for every MsgType, including Reject(3) and Logout(5). For a frame that fails the parse
  it supersedes the published no-reject-loop rule ("a malformed Reject/Logout is never itself
  rejected").
- **FR-004**: The Reject in FR-003 MUST advance NextNumIn exactly when MsgSeqNum equals the expected
  number and the message is not a SequenceReset. This reuses #423's rows unchanged. "The message"
  and "MsgSeqNum" are the values the fault record holds (`fault_ref_msg_type`, `fault_ref_seq_num`),
  never the scan's last-wins `msg_type`/`msg_seq_num`.
- **FR-005**: When MsgSeqNum differs from the expected number, the Reject MUST NOT advance NextNumIn
  and MUST NOT by itself trigger a ResendRequest or a disconnect.
- **FR-006**: In LogonReceived and Active, a frame that passes framing but fails the parse MUST be
  disregarded (no Reject, no advance, no disconnect) when either:
  - its third field is not a well-formed MsgType(35), which is §4.5.2 criterion 3 (ruling row 1);
    this includes the third field being the malformed one;
  - its third field is MsgType but MsgSeqNum(34) was not read as a positive integer before the
    failure point (ruling row 5).
- **FR-007**: SessionRejectReason(373) MUST be:
  - 0 (Invalid tag number), with RefTagID(371) omitted, for shape (B);
  - 5 (Value is incorrect (out of range) for this tag), with RefTagID(371) = the Length tag, for
    shape (A).

  The validator's reason mapping MUST express 0 and 5 (FR-012). Today both shapes map to 3.
- **FR-008**: A Framer-detected framing failure (BeginString, BodyLength, CheckSum) is handled as
  today: session-fatal (`L-004-4`). Its §4.5.2 disregard is fixpp#514 (owner ruling O-1). The Framer
  does not check criterion 3 (35 is the third field). For a frame that also fails the parse, FR-006
  (LogonReceived/Active), FR-009 (before Active) and FR-015 (LogoutSent) decide it. A fault-free
  frame whose third field is not 35 keeps its current handling; it belongs to fixpp#514 (research
  R-11).
- **FR-009**: Any frame that passes framing but fails the parse while the session awaits the first
  Logon (acceptor) or the Logon reply (initiator) MUST be refused through the existing Logon refusal
  path, whatever its MsgType and whatever its third field, for both shapes. Both arms already refuse
  every input that is not a valid Logon (contract C-2 cites the source). The pre-Active disregard of
  a frame whose third field is not 35 is fixpp#514, which adds the establishment timeout it needs.
  The refusal is the arm's silent transition to Disconnected. No Reject and no Logout are sent. Once
  in LogonReceived or Active, a Logon that fails the parse (third field MsgType, MsgSeqNum read) is also refused this
  way, never Rejected.
- **FR-010**: A SequenceReset (Reset or GapFill mode) that fails the parse MUST NOT change NextNumIn
  through NewSeqNo(36), whatever its MsgSeqNum.
- **FR-011**: The disposition of a frame the header scan finds faulty MUST NOT depend on any of:
  - the inbound-validation setting;
  - the `validate_sequence_numbers` setting;
  - the session profile (FIX.4.2, FIX.4.4, FIXT.1.1);
  - the role (acceptor/initiator);
  - whether an Application is registered.
- **FR-012**: `dictionary_driven_validator::validate` MUST NOT report "conformant" for a message
  whose field walk met an encoding fault. It MUST return `wire_invalid_tag_number` (reason 0,
  RefTagID untouched) for a malformed tag, and `wire_length_data_mismatch` (reason 5, RefTagID = the
  Length tag) for a Length+Data mismatch. This holds for:
  - a walk that stopped on the fault;
  - a walk that tolerated it: an empty tag, a clamped count, or a count reaching the end.

  The Parser field iterator MUST report every such fault through a read-only accessor, without
  changing what it yields. The new error values, their C mapping and every oracle that enumerates
  errors are part of this requirement (data-model.md E-0, E-4 to E-6; research R-7). Today the
  validator's silent stop is reachable only through the public API: a directly constructed
  `MessageView` whose build failed, or a view built under hooks that differ from the validator's.
- **FR-013**: Every Reject sent under this feature MUST be persisted and emitted like any other
  outbound session Reject: sequence number assigned, toAdmin observed, stored before sent. Where
  the Reject advances NextNumIn (FR-004), the advance MUST be persisted BEFORE the Reject is emitted,
  and a failed persist MUST disconnect without sending the Reject (#423's rule, pinned per call site). No
  peer-controlled byte may make the Reject fail to build after its number was consumed (research
  R-5).
- **FR-014**: The documented behaviour MUST be updated wherever it states the old one, found by the
  derivation in research R-13. That includes:
  - the C-ABI header text saying a malformed pair is "dropped as a parse error, silently … no Reject
    is sent";
  - the no-reject-loop sentence in the public `admin_messages.hpp`, which gains a header comment
    naming the ruling that supersedes it;
  - the behaviours-and-limitations entries;
  - the #423 ruling reference in code comments;
  - the brain pages that call these frames garbled.
- **FR-015**: In LogoutSent, a frame that passes framing but fails the parse MUST be disregarded: no
  Reject, no advance, and it MUST NOT be taken as the Logout reply. The logout timeout runs as
  usual.
- **FR-016**: A parse that fails at a **late** inbound parse site (a site after the fault branch, on
  a frame the header scan found fault-free) MUST be **fail-closed**: the session closes terminally,
  sends no Reject, and does not invoke the parse target's receive callback (`fromAdmin`/`fromApp`;
  `onLogout` from the close and callbacks fired earlier at the site are out of scope). It never reads as "success" or "no reject".
  Such a failure can only be a resource failure (the parse arena or the offset-table cap) or an I-4
  breach. Every late site takes this one action. The site population is derived by command
  (research R-4). Effects that ran before the late parse are not undone (contract C-6). Each
  late-site cell also reconnects and pins whether NextNumIn includes the closed-on frame. The
  disposition of a resource failure beyond fail-closed, and moving its decision ahead of the
  guards, are fixpp#515 (owner ruling O-2).
- **FR-017**: The change MUST be declared as **C-ABI 1.10, BREAKING** under `[const §X.7]`, with the
  same procedure 091 used for 1.9:
  - `FIXPP_C_ABI_VERSION_MINOR` 9 → 10, with a `version.h` history comment;
  - a BREAKING (1.10) clause in the documentation of each affected declaration;
  - the freeze manifest re-pinned;
  - every in-repo version consumer updated;
  - the declaration carried in the PR description and in the behaviours-and-limitations delta.

  The affected declarations are derived by 091's population recipe (research R-8).
- **FR-018**: In Active, a frame answered with a Reject under FR-003 MUST refresh inbound liveness,
  as any received message does. A disregarded frame MUST NOT.

### Key Entities

- **Failure point**: the byte position of the first field that could not be parsed. Only fields
  wholly before it can be read for FR-001's exception.
- **Header identification**: whether the third field is a well-formed MsgType(35), and the first
  MsgSeqNum(34) before the failure point, if it is a positive integer.
- **Frame disposition**: one of:
  - *disregard* (FR-006, FR-008, FR-015);
  - *Reject* (FR-003 to FR-005);
  - *Logon refusal* (FR-009);
  - *fail-closed close*, for a late parse failure (FR-016).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In #507's T076 arrangement, all four framed-but-unparseable cells (malformed count or
  malformed tag, validation on or off) draw a Reject for the SequenceReset, with the exact 373 and
  371 of FR-007. The probe at MsgSeqNum 500 draws a ResendRequest (the reset was not applied). Today
  all four apply the reset.
- **SC-002**: In LogonReceived and in Active, a frame that fails the parse, whose third field is
  MsgType(35) and whose MsgSeqNum was read and equals the expected number, produces exactly one
  Reject and no other outbound message for every MsgType other than Logon(A) (contract C-2 D-4,
  D-5), including Reject(3), Logout(5), SequenceReset(4) and one application type. The application
  callback is never invoked for it. Each cell asserts the exact 373 and 371. A faulty Logon is
  refused, never Rejected (D-3, SC-004). Before Active (D-1/D-2), in LogoutSent (D-9), and when
  field 3 is not 35 or 34 was not read (D-7/D-8), no Reject is sent.
- **SC-003**: Under the same conditions as SC-002, a faulty frame of any MsgType other than Logon(A)
  or SequenceReset(4) (contract C-2 D-5), followed by a conformant message at the next MsgSeqNum,
  delivers the conformant message with no ResendRequest (advance witnessed). The same pair with the
  faulty frame being a SequenceReset (D-4) draws a ResendRequest (no advance).
- **SC-004**: A Logon with a malformed tag is refused in 100% of cells: acceptor and initiator, all
  three profiles. It is also observed through every C-ABI observer FR-017 names. A faulty Logon in
  LogonReceived or Active (third field MsgType, MsgSeqNum read) ends in a silent Disconnected with no
  Reject (D-3).
- **SC-005**: In Active, a frame whose malformed field precedes MsgSeqNum, or whose third field is
  not MsgType, leaves the session connected and draws no outbound message. The next valid message draws a
  ResendRequest.
- **SC-006**: Each of SC-001 to SC-005 has a cell that goes RED when the disposition is removed from
  its state arm. The cell's witness is an input the pre-feature code mishandles: a malformed *tag*
  for the Logon rows, a *Logout* for LogoutSent. Every Reject cell asserts the exact 373 and 371.
  The deletion proof is run twice: with the late-site close (FR-016) present, and with it also
  deleted, because a late-site close can keep a refusal cell green.
- **SC-007**: A session that receives a malformed application message keeps delivering later
  messages: no stall of the B-423-1 kind on the Reject rows. It is proven in-process against a
  scripted peer that:
  - sends the raw malformed bytes;
  - handles our Reject as QuickFIX J/C++ do (their `nextReject`, cited by source);
  - answers each ResendRequest by replaying its stored bytes verbatim with PossDupFlag(43)=Y and
    OrigSendingTime(122) added, as QuickFIX does.

  The run drives one malformed frame at a too-high MsgSeqNum, so a gap forms and the resend must
  converge. The disregard rows' loop and the malformed-GapFill disconnect are pinned as disclosed
  outcomes (contract C-5). The live QuickFIX interop cell is a follow-up issue placed in B17.
- **SC-008**: A well-formed frame above the measured parse ceiling, at each late inbound parse site
  research R-4 derives, closes the session: no Reject, the parse target's receive callback (`fromAdmin`/`fromApp`) not
  invoked (FR-016 scope), never read as success. Each
  site's cell goes RED when that site's close is deleted.
- **SC-009**: The C-ABI version test pins 1.10, is RED against 1.9, and turns RED again under a
  mutant back to 9.

## Assumptions

- **A-1** (Resolved: research R-1.) The header scan reports its first fault and identifies the
  header positionally.
- **A-2** (Resolved: research R-5.) The Reject path is the existing session Reject machinery,
  extended with a Text(58) input.
- **A-3** (Resolved in Clarifications, 2026-09-27.) A frame that fails the parse is dispositioned
  before the identity and SendingTime guards. A wrong CompID read before the failure point does not
  disconnect (FR-002).
- **A-4** (Resolved in Clarifications, Gate A round 1.) There is no new C-ABI entry point, error code
  or configuration. The change of which Logons are accepted is declared C-ABI 1.10 BREAKING
  (FR-017).
- **A-5** No resend-loop guard is added. On the **Reject rows**, a deterministically malformed frame
  advances past itself at the expected MsgSeqNum, so the B-423-1 loop does not arise there. It
  **can** arise on the ruling-mandated disregard rows (fault before 34, or third field not 35). A
  malformed GapFill during resend recovery ends in a disconnect. Both are disclosed as
  behaviours-and-limitations rows with cells (contract C-5).
- **A-6** The interop counterparties run default settings. QuickFIX J/C++ both process an inbound
  Reject(35=3) as an admin message and advance their own counter.
- **A-7** TC2020 Scenario 17d draws 373=8 for an invalid SignatureLength(93). Under the ruling's
  Length+Data code this feature sends 5. The deviation is disclosed (contract C-5 L-4).
- Out of scope:
  - the live QuickFIX interop cell for SC-007 (a follow-up issue placed in B17, whose
    counterparty-image republish it needs);
  - a resend-loop guard;
  - LFIXT;
  - detecting a Length+Data mismatch in counterparties;
  - the §4.5.2 disregard of a Framer-detected failure, a fault-free frame whose third field is not
    35, and the pre-Active establishment timeout (fixpp#514, owner ruling O-1);
  - the disposition of a resource failure of a well-formed frame beyond fail-closed (fixpp#515,
    owner ruling O-2);
  - fuzz-harness coverage of library code (#508).
