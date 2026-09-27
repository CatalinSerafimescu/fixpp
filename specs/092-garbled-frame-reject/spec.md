# Feature Specification: The session never acts on a frame it could not parse

**Feature Branch**: `092-garbled-frame-reject`

**Created**: 2026-09-27

**Status**: Draft

**Input**: User description: "B18 / fixpp#507 — the session must never act on an inbound frame that
passes framing but fails the parse; its disposition follows the owner ruling of 2026-09-27 (issue
comment https://github.com/CatalinSerafimescu/fixpp/issues/507#issuecomment-5855333315, which
revises #423 row 4). […] Reuse the existing consume_rejected_seqnum_ / emit_session_reject_
machinery. Evidence: the #507 body, the #423 ruling table, and B-423-1." (The full input is the
`/speckit-specify` invocation of 2026-09-27; its requirements are restated below as FRs.)

**Issue**: fixpp#507 (batch B18 in the parent's `phases/phase-4/issue-batches.md`).

---

## Context — what is broken, and what was already decided

A FIX frame can pass every framing check (BeginString, BodyLength and MsgType are the first three
fields; BodyLength counts correctly; CheckSum is last and correct) and still fail the full parse.
Two shapes reach that state:

- **(A)** a Length+Data pair whose counted extent is not followed by SOH, or runs past the frame
  (fixpp detects this since 091 / PR #510; no other engine surveyed does);
- **(B)** a field whose tag is not a well-formed tag number (e.g. `9x9=1`).

**What fixpp does today** (issue #507, measured at `1f6be466`; re-derived at `d3b58922` by reading the
consumers):

- The session drives its state machine from a header **scan**, which silently skips a malformed tag
  and silently stops at a malformed count. It does not report either.
- Every consumer of the full parse treats a failure as "nothing to do": the inbound validate gate
  reports "no reject", and the callback dispatcher skips the callback and reports success.
- So a SequenceReset in Reset mode is **applied** from a frame that failed to parse (#507 T076, with
  inbound validation on and off). Logout, TestRequest, ResendRequest and Heartbeat act on scanned
  fields the same way.
- An application message that fails to parse is **persisted and its sequence number consumed**, but
  it is never delivered and never rejected. It is lost, and neither side is told.
- A Logon with a malformed count is refused (091 FR-020), but a Logon with a malformed tag is not.
- If MsgSeqNum(34) lies after a malformed count, the scan never reaches it and the session
  disconnects as if 34 were missing.

**Already decided, not re-opened here** (owner ruling 2026-09-27 on #507, after research across
the FIX primary documents, QuickFIX J/C++/n/Go source, OnixS, FIX Antenna, Artio, Fix8, fix-rs,
venue specs, and a Fable consult):

- "Garbled" (ignore, no Reject, no advance — FIX-SL 2020 §4.5.2) means **only** the four framing
  criteria. A frame that passes framing and then fails the parse is a tagvalue encoding violation,
  which FIX-SL 2020 §9.4 routes to Reject, and a Reject advances NextNumIn (§4.5.4). This revises
  #423's row "garbled (unparseable, …) → no Reject, no advance".
- The Reject follows #423's existing seqnum rows: it advances only when MsgSeqNum equals the
  expected number; Logon and SequenceReset never advance through the Reject path.
- Ignoring instead is rejected as the default: a deterministically malformed frame is resent
  identically on every ResendRequest, fixpp has no resend-loop guard, and B-423-1 records that
  stall, measured live against QuickFIX-cpp.
- No FIX.4.x / FIXT.1.1 difference. LFIXT (§5.4.12) is not a supported profile. No configuration
  switch.

---

## User Scenarios & Testing *(mandatory)*

The "user" is an operator running a fixpp session against a counterparty whose encoder produces a
frame that is framed correctly but malformed inside.

### User Story 1 — A malformed admin message is never acted on (Priority: P1)

A counterparty sends a SequenceReset, Logout, TestRequest, ResendRequest or Heartbeat that passes
framing but fails the parse. The session must not change its sequence state, reply to a
TestRequest, resend anything, or log out because of that frame's contents. It answers with a
session Reject, so the counterparty learns the frame was malformed.

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
   pair after MsgSeqNum, **Then** the outcome is the same, except SessionRejectReason is the
   Length+Data code and RefTagID names the Length tag (FR-007).
3. **Given** the same session, **When** a TestRequest that fails the parse arrives at the expected
   MsgSeqNum, **Then** no Heartbeat is sent in reply, a Reject is sent, and NextNumIn advances.
4. **Given** the same session, **When** a ResendRequest that fails the parse arrives, **Then**
   nothing is resent, a Reject is sent, and NextNumIn advances only if MsgSeqNum was the expected
   number.
5. **Given** the same session, **When** a Logout that fails the parse arrives, **Then** the session
   does not reply with a Logout or disconnect because of it; it sends a Reject.

---

### User Story 2 — A malformed application message is rejected, not silently lost (Priority: P1)

A counterparty sends an application message (e.g. NewOrderSingle) that passes framing but fails the
parse. Today it is persisted as received and its sequence number consumed, but it is never delivered
and never rejected. After this feature the counterparty receives a session Reject that identifies
the message, and can resend it with a new MsgSeqNum (FIX-SL 2020 §4.5.4).

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

---

### User Story 3 — A malformed Logon is refused (Priority: P2)

A Logon that passes framing but fails the parse is refused through the existing Logon refusal path
(the session disconnects; no Reject is sent), for both shapes (A) and (B). Today only shape (A) is
refused.

**Why this priority**: a session must not be established on identity and parameters read from a
malformed frame. It is narrower than P1 because 091 already closes shape (A).

**Independent Test**: Feed a Logon with a malformed tag, as acceptor and as initiator (the Logon
reply). The session ends in the refused/disconnected state, and no Reject or Logon reply is sent.

**Acceptance Scenarios**:

1. **Given** an acceptor awaiting Logon, **When** a Logon with a malformed tag arrives, **Then** the
   session refuses it exactly as it refuses a Logon with a malformed count.
2. **Given** an initiator that sent a Logon, **When** the reply Logon has a malformed tag, **Then**
   the session refuses it in the same way.

---

### User Story 4 — A frame whose MsgSeqNum cannot be trusted is ignored (Priority: P2)

When the malformed bytes come before MsgSeqNum(34) in the frame (for example a bad
SecureDataLen(90)/SecureData(91) pair in the header), the frame cannot be attributed to a sequence
number. It is ignored as garbled: no Reject, no advance, no disconnect. The next valid message then
exposes the gap and normal recovery runs (TC2020 17g).

**Why this priority**: a Reject needs a trustworthy RefSeqNum. Today this case disconnects the
session.

**Independent Test**: Feed a frame whose malformed field precedes MsgSeqNum, then a conformant
message at the expected MsgSeqNum + 1. The first draws nothing; the second draws a ResendRequest for
the missing number, and the session stays connected.

**Acceptance Scenarios**:

1. **Given** an Active session expecting N, **When** a frame with a malformed Length+Data pair
   before MsgSeqNum arrives, **Then** no message is sent, NextNumIn stays N, and the session stays
   Active.
2. **Given** the same, **When** the malformed field is a malformed tag before MsgSeqNum, **Then** the
   outcome is the same.

---

### Edge Cases

- **MsgSeqNum below expected** on a frame that fails the parse: Reject, no advance. The too-low
  fatal path does not run, because it would act on the frame's PossDupFlag.
- **MsgSeqNum above expected**: Reject, no advance, and no ResendRequest is triggered by that
  frame. The next valid message detects the gap (FR-005).
- **PossDupFlag(43)=Y** on a frame that fails the parse: not acted on; the frame is dispositioned
  by FR-003 to FR-006 as if the flag were absent.
- **A Reject that fails the parse** (35=3): it follows FR-003/FR-004. It is never answered with a
  Reject that triggers a Reject loop (the existing no-reject-on-inbound-Reject rule holds).
- **Wrong CompID or BeginString** on a frame that also fails the parse: no identity field of the
  frame is acted on (FR-001). Whether the identity check still runs on fields scanned before the
  failure point is settled in `/speckit-clarify` (see Assumptions A-3).
- **More than one malformed field**: the first failure point decides FR-006 and the reason code.
- **Before Active** (NotConnected, LogonSent, LogoutSent): a non-Logon frame that fails the parse is
  never acted on; its disposition follows what that state already does with an unexpected frame,
  and it is never answered with a Reject that consumes a sequence number before logon.
- **A frame that fails framing** (§4.5.2): unchanged. Ignored, no Reject, no advance (FR-008).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The session MUST NOT act on any field of an inbound frame that passes framing but
  fails the full parse, on any admin, application, or pre-Active path, with one exception.
  MsgSeqNum(34) and MsgType(35) MAY be read, from bytes before the failure point, only to address
  and account for the Reject (FR-003 to FR-006). A parse failure MUST NOT read as "no reject" or
  "dispatch succeeded".
- **FR-002**: A parse failure MUST be decided before any handler acts on the frame: before the
  SequenceReset, Logout, TestRequest, ResendRequest, Heartbeat and application handlers, and before
  the sequence-gap, too-low and PossDup arms.
- **FR-003**: When MsgSeqNum(34) was read from well-formed bytes before the failure point, and the
  message is not a Logon, the session MUST send a session Reject(35=3) with RefSeqNum(45) =
  that MsgSeqNum and RefMsgType(372) = the MsgType read, and a Text(58) that names the defect.
- **FR-004**: The Reject in FR-003 MUST advance NextNumIn exactly when MsgSeqNum equals the expected
  number and the message is not a SequenceReset. This reuses #423's rows unchanged.
- **FR-005**: When MsgSeqNum differs from the expected number, the Reject MUST NOT advance NextNumIn
  and MUST NOT by itself trigger a ResendRequest or a disconnect.
- **FR-006**: When MsgSeqNum(34) was not read from well-formed bytes before the failure point, the
  frame MUST be ignored as garbled: no Reject, no advance, no disconnect, and a log entry.
- **FR-007**: SessionRejectReason(373) MUST be 0 (Invalid tag number) with RefTagID(371) omitted for
  shape (B). For shape (A) it MUST be 5 (value incorrect for this tag) or 6 (incorrect data format),
  chosen in `/speckit-clarify`, with RefTagID(371) = the Length tag. The reason mapping MUST express
  both; today both shapes map to 3.
- **FR-008**: A frame that fails a framing criterion (FIX-SL 2020 §4.5.2) keeps its current handling:
  ignored, no Reject, no advance.
- **FR-009**: A Logon that passes framing but fails the parse MUST be refused through the existing
  Logon refusal path, for both shapes, as acceptor and as initiator. No Reject is sent.
- **FR-010**: A SequenceReset (Reset or GapFill mode) that fails the parse MUST NOT change NextNumIn
  through NewSeqNo(36), whatever its MsgSeqNum.
- **FR-011**: The behaviour MUST NOT depend on the inbound-validation setting, the session profile
  (FIX.4.2, FIX.4.4, FIXT.1.1), or the role (acceptor/initiator).
- **FR-012**: `dictionary_driven_validator::validate` MUST NOT report "conformant" for a message
  whose field walk stopped before the end of the frame. It MUST report a reject (reason per FR-007)
  instead of silently ending the walk. Today this is reachable only through the public API, when the
  view was built under hooks that differ from the validator's.
- **FR-013**: Every Reject sent under this feature MUST be persisted and emitted like any other
  outbound session Reject (sequence number assigned, toAdmin observed, stored before sent).
- **FR-014**: The documented behaviour MUST be updated wherever it states the old one. That includes
  the C-ABI header text saying a malformed pair is "dropped as a parse error, silently … no Reject
  is sent", the behaviours-and-limitations entries, and the #423 ruling reference in code comments.

### Key Entities

- **Failure point**: the byte position of the first field that could not be parsed. Only fields
  wholly before it can be read for FR-001's exception.
- **Frame disposition**: one of *ignore as garbled* (FR-006, FR-008), *Reject* (FR-003–FR-005), or
  *Logon refusal* (FR-009).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In #507's T076 arrangement, all four garbled cells (malformed count / malformed tag ×
  validation on / off) draw a Reject for the SequenceReset, and the probe at MsgSeqNum 500 draws a
  ResendRequest (the reset was not applied). Today all four apply it.
- **SC-002**: For every admin message type and one application type, a frame that fails the parse
  at the expected MsgSeqNum produces exactly one Reject and no other outbound message. The
  application callback is never invoked for it.
- **SC-003**: A frame that fails the parse at the expected MsgSeqNum, followed by a conformant
  message at the next MsgSeqNum, delivers the conformant message with no ResendRequest (advance
  witnessed). The same pair with the malformed frame being a SequenceReset draws a ResendRequest
  (no advance).
- **SC-004**: A Logon with a malformed tag is refused in 100% of cells: acceptor and initiator, all
  three profiles.
- **SC-005**: A frame whose malformed field precedes MsgSeqNum leaves the session connected and draws
  no outbound message; the next valid message draws a ResendRequest.
- **SC-006**: Each of SC-001 to SC-005 has a cell that goes RED when the mechanism is removed. That
  includes a cell that would pass if the parse failure were silently treated as "no reject".
- **SC-007**: Against the QuickFIX/J and QuickFIX C++ counterparties, a session that receives one
  malformed application message keeps delivering later messages: no stall of the B-423-1 kind.

## Assumptions

- **A-1** The frame scan can be made to report its failure point, or an equivalent. How is a
  `/speckit-plan` matter; today it reports neither a failure nor where it stopped.
- **A-2** The Reject path is the existing session Reject machinery, extended with a Text(58) input.
  Today it has none.
- **A-3** A frame that fails the parse is dispositioned before the identity (CompID/BeginString) and
  SendingTime guards, since those would act on its fields. Whether a wrong CompID read before the
  failure point should still disconnect is a `/speckit-clarify` question.
- **A-4** No new C-ABI entry point or configuration. Whether the peer-visible change needs a C-ABI
  version note is a Gate A question; the C-ABI surface itself does not change (the receive callback
  was already not invoked for such frames).
- **A-5** No resend-loop guard is added. Under the Reject disposition, a deterministically malformed
  frame advances past itself at the expected MsgSeqNum, so the B-423-1 loop does not arise.
- **A-6** The interop counterparties run default settings. QuickFIX J/C++ both process an inbound
  Reject(35=3) as an admin message and advance their own counter.
- Out of scope: a resend-loop guard; LFIXT; detecting a Length+Data mismatch in counterparties;
  fuzz-harness coverage of library code (#508).
