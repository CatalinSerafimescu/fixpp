# Feature Specification: Inbound Reject fidelity — QuickFIX's reasons and text, one MsgType per frame

**Feature Branch**: `101-inbound-reject-fidelity`

**Created**: 2026-11-07

**Status**: Draft

**Input**: REMAINING-WORK ★ V1.0 FUNCTIONAL TRACK row 7, session half: batch B85 (fixpp#655, #653; owner-ruled
2026-11-02), batch B82 (fixpp#646) and batch B88 (fixpp#657, #658). How a session answers an inbound frame whose
MsgType or tags the dictionary does not define, what a Reject echoes in RefMsgType(372), and which MsgType a frame
carrying two of them is handled as.

## Context & Problem

A counterparty, and the people reading its logs, see three things that differ from QuickFIX C++/J/Go today:

1. **Unknown MsgType (#655).** A validating session answers an application message whose MsgType(35) its dictionary
   does not define with Reject 373=2 (Tag not defined for this message type), RefTagID(371)=8. The validator has no
   MsgType check: an unknown MsgType has an empty tag set, so BeginString(8) is the first "unexpected" tag. All three
   QuickFIX engines answer 373=11 (Invalid MsgType).
2. **Undefined tag (#653).** One validator result serves a tag the dictionary does not define at all and a tag it
   defines but not for this MsgType; both answer 373=2. QuickFIX answers 373=0 (Invalid tag number) for the first and
   373=2 for the second, both with 371 = the tag. And fixpp's validator Rejects carry no Text(58); all three engines
   send QuickFIX's reason text.
3. **Two MsgTypes in one frame (#657, #658).** The header scan keeps the **last** 35, the Logon interpreter reads the
   last 35, and the validator and the message view read the **first**. So a validation-off session establishes on a
   frame whose third field is `35=D` and which carries Logon fields and a later `35=A` (#657), and a non-FIXT validating
   session validates a frame by its first MsgType and dispatches it by its last (#658): third field `0` with a later
   `35=D` is validated as a Heartbeat and delivered to `fromApp`. Feature 099 closed this for a **validating FIXT**
   session only (its FR-025: Reject 373=13 in the header / 373=14 past it, 371=35; FR-029: disconnect in an
   establishment arm). QuickFIX C++ answers a later 35 with 373=14 (past the header) or 373=13 (in it), and neither
   QuickFIX acceptor establishes a session on such a first frame.
4. **RefMsgType echo (#646).** The Reject and BusinessMessageReject builders echo the inbound MsgType into 372 as
   sliced by the header scan, which keeps an `=` inside the value (`35=D=X` → `D=X`). The issue asked whether that
   value can mislead a counterparty's parser. Read for this feature: QuickFIX C++ (`2ce8a60`) and QuickFIX/J
   (`4969564`) split a field at its first `=` and write 372 back verbatim (`372=D=X`); every FIX parser reads that as
   one field, tag 372, value `D=X`. What the issue did not name, and the survey for this feature found: the inbound
   Reject sites other than the disposer pass the inbound MsgType unbounded, and a value too long for the Reject's
   build buffer makes the build fail, so **no Reject is sent at all**, silently.

## Clarifications

### Session 2026-11-02 (owner, batch B85; analysis `research/reviews/ruling_B85_reftagid_invalid_msgtype.md`, parent repo)

- Q: What does the 373=11 Reject carry? → A: 373=11; 372 = the received MsgType; 58 = `Invalid MsgType`; **no 371**
  (QuickFIX C++ and Go's `2q_MsgTypeNotValid.def` frame; QuickFIX/J's 371=35 loses 2–1).
- Q: #653's 371? → A: the offending tag, on both 373=0 and 373=2 (all three engines agree).
- Q: Text(58) scope? → A: **every Reject reason the validator gives**, with QuickFIX's text (option (ii), "every
  reason").
- Q: A new slot or reuse `wire_invalid_tag_number` for an undefined tag? → A: a new validator error slot;
  `wire_invalid_tag_number` keeps its contract (a malformed tag, no 371).
- Q: QuickFIX's `ValidateUserDefinedFields` / `AllowUnknownMsgFields`? → A: nothing to implement (their defaults check
  every tag, as fixpp does); disclose that fixpp's config loader ignores both keys.
- Q: Where does the MsgType check go? → A: after the header-order check (373=14), before the field walk.

### Session 2026-11-07 (orchestrator, owner-delegated; QuickFIX read for this feature)

- Q: #646 — refuse, omit, or keep the `=`? → A: **keep the echo verbatim**, as QuickFIX C++ and J do; a value with `=`
  is one field to any FIX parser. Fix instead the silent loss the survey found: an inbound MsgType too long for the
  Reject is not echoed (372 is optional, `Req'd N` in FIX 4.4 and the 2020 Session Layer), and the Reject is sent.
- Q: #657/#658 — which MsgType is a frame? → A: **its third field's**, everywhere: the header scan, the Logon
  interpreter, dispatch, the validator and the message view read one value. This is the value 099's FR-025 already
  makes RefMsgType and the consume key.
- Q: A validating **non-FIXT** session and a repeated 35? → A: the same endings as 099's FIXT ones: FR-025's Reject in
  LogonReceived and Active, FR-029's disconnect in an establishment arm.
- Q: A **validation-off** session and a repeated 35? → A: in an establishment arm (acceptor before Logon, initiator
  awaiting the Logon reply), a frame whose MsgTypes differ is refused with a disconnect, sending nothing: both QuickFIX
  acceptors close such a connection and neither establishes. Once established, the frame is handled as its third
  field's MsgType with no Reject (QuickFIX/J's validation-off ending is delivery as the first value; C++ rejects under
  every setting; the split is decided for "validation off means no Reject", which keeps 099's ruling that validation-off
  dispositions do not grow Rejects).
- Q: An undefined tag inside a repeating-group instance? → A: 373=0 with 371 = the tag, the same rule as anywhere else
  (QuickFIX/J answers 373=2, QuickFIX C++ answers 0, 2 or 16 by layout; one rule is chosen, confirmed at Gate A).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A counterparty's invalid MsgType gets QuickFIX's Reject (Priority: P1)

A peer sends an application message whose MsgType the session's dictionary does not define, to a session with inbound
validation on.

**Why this priority**: Peer-reachable on ordinary traffic; certification test 2q checks 373=11; operators migrating from
QuickFIX compare Rejects.

**Independent Test**: Feed the message to a validating session on each dictionary family (FIX 4.2, 4.4, FIXT.1.1 with
a FIX 5.0SP2 application message) and read the Reject.

**Acceptance Scenarios**:

1. **Given** a validating FIX 4.4 session, **When** it receives `35=U1` (undefined), **Then** it sends Reject 373=11,
   372=`U1`, 58=`Invalid MsgType`, no 371, 45 = the message's MsgSeqNum; the message is consumed and the session
   continues.
2. **Given** a validating FIXT.1.1 session, **When** an application message's MsgType is undefined in its resolved
   application version's dictionary, **Then** the same Reject.
3. **Given** a validation-off session, **When** it receives `35=U1`, **Then** today's handling, unchanged.

---

### User Story 2 - Tag Rejects name the right reason and say why (Priority: P1)

A peer sends a message carrying a tag the dictionary does not define at all, or a tag it defines but not for this
message.

**Why this priority**: Peer-reachable; FIX-TC 14a/14c distinguish the two; every validator Reject today carries no text.

**Acceptance Scenarios**:

1. **Given** a validating session, **When** a NewOrderSingle carries tag 9999 (undefined), **Then** Reject 373=0,
   371=9999, 58=`Invalid tag number`.
2. **Given** the same, **When** a Heartbeat carries tag 44 (defined, not in Heartbeat), **Then** 373=2, 371=44,
   58=`Tag not defined for this message type`.
3. **Given** the same, **When** any other validator reason fires (1, 5, 6, 13, 14, and 099's 4/5/14 on 1128), **Then**
   the Reject carries QuickFIX C++'s text for that reason, and its other fields are unchanged.
4. **Given** an undefined tag inside a repeating-group instance, **Then** 373=0, 371 = the tag.

---

### User Story 3 - A frame has one MsgType (Priority: P1)

A peer sends a frame carrying MsgType twice with different values.

**Why this priority**: #657 lets a peer establish a session with a frame that is not a Logon by its third field; #658
lets a frame be validated as one message and handled as another.

**Acceptance Scenarios**:

1. **Given** a validation-off acceptor before Logon, **When** it receives `35=D|…|98=0|108=30|…|35=A`, **Then** it
   disconnects, sends nothing and does not establish (today: establishes).
2. **Given** a validation-off initiator awaiting its Logon reply, **When** the reply is a frame whose MsgTypes differ,
   **Then** it disconnects, sends nothing.
3. **Given** a validating non-FIXT session in Active, **When** a frame's third field is `35=0` and a later field is
   `35=D`, **Then** Reject 373=14 (past the header) or 373=13 (inside it), 371=35, 372=`0`, the frame consumed as `0`,
   no `fromApp` (today: delivered to `fromApp`).
4. **Given** a validating non-FIXT acceptor before Logon, **When** such a frame arrives, **Then** disconnect, nothing
   sent.
5. **Given** a validation-off session in Active, **When** a frame's third field is `0` and a later 35 is `D`, **Then**
   it is handled as a Heartbeat (today: `fromApp`); third field `D` with a later `35=5` is an application message
   (today: logs the session out); third field `D` with a later `35=3` is an application message (today: consumed in
   silence).
6. **Given** a frame with one MsgType, **When** it arrives in any state, **Then** handling is unchanged.

---

### User Story 4 - A Reject is never dropped for its echo (Priority: P2)

A peer sends a message whose MsgType value is long (hundreds of bytes) and which draws a session Reject.

**Acceptance Scenarios**:

1. **Given** any inbound-fed Reject or BusinessMessageReject site, **When** the inbound MsgType does not fit the
   message being built, **Then** the Reject is sent without 372, every other field as for a short MsgType.
2. **Given** `35=D=X` drawing a Reject, **Then** the Reject carries `372=D=X` verbatim, and fixpp's own parser reads it
   back as one field 372 with value `D=X`.

### Edge Cases

- MsgType repeated with the **same** value: one MsgType, so FR-004..FR-006 do not apply and handling is today's
  (099's FR-025 compares values too). QuickFIX C++ answers a repeated tag with 373=13 whatever its value; a general
  repeated-tag check is not this feature's (recorded at plan).
- A repeated 35 in a frame that also fails another check: the frame-level check order is 099's (FR-025 ahead of the
  validator), extended to non-FIXT.
- The resend replay and the outbound store masking also read the header scan's MsgType: they read the third field
  after this feature; stored outbound frames have one MsgType, so their behaviour does not change.
- An unknown MsgType on an admin-length value (one character, e.g. `35=Z`): an unknown MsgType, 373=11.
- FIXT: the MsgType check uses the dictionary the frame is validated with (099's resolved application version or the
  transport dictionary).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001** (#655): The validator MUST check that the dictionary it validates with defines the frame's MsgType, after
  the header-order check and before the field walk, and MUST report an undefined MsgType with its own result, mapped to
  373=11 with no 371.
- **FR-002** (#653): The validator MUST report a tag the dictionary does not define at all with its own result, mapped
  to 373=0 with 371 = the tag, and a tag the dictionary defines but not for this message (header, trailer and body
  sets as validated today) with 373=2, 371 = the tag. "Defined" means present in the dictionary's field definitions.
- **FR-003** (B85 ruling): Every session Reject the inbound validation gate sends MUST carry Text(58) with QuickFIX
  C++'s text for its reason (`SessionRejectReason_*_TEXT`). Rejects from other sites (the 092 disposer, the
  sequence-reset and admin paths) keep their current text.
- **FR-004** (#657, #658): A frame's MsgType MUST be its third field's value for every reader: header scan, Logon
  interpretation, state-arm dispatch, validation, the message view, RefMsgType echo and the consume key.
- **FR-005** (#658): A validating session, FIXT or not, MUST apply 099's FR-025 (Reject in LogonReceived and Active)
  and FR-029 (disconnect in an establishment arm) to a frame whose MsgTypes differ.
- **FR-006** (#657): A validation-off session in an establishment arm MUST refuse a frame whose MsgTypes differ with a
  disconnect, sending nothing. In LogonReceived, Active and LogoutSent a validation-off session handles such a frame
  as its third field's MsgType and sends no Reject for the repeat.
- **FR-007** (#646): Every inbound-fed Reject and BusinessMessageReject site MUST send its message when the inbound
  MsgType does not fit, omitting 372; a MsgType that fits is echoed verbatim, `=` included.
- **FR-008**: A frame not covered by FR-001..FR-007 MUST be handled byte-identically to today, on every session kind;
  no per-message allocation is added to the inbound path (`[const §XV.1]`), and a paired benchmark of the header scan
  and the inbound dispatch shows no regression beyond the paired gate's band.
- **FR-009** (records): The B&L rows for #646 (L-452-2), #657/#658 (099's B-rows naming them) move or change to the new
  endings; a B-row discloses that fixpp's config loader ignores QuickFIX's `ValidateUserDefinedFields` and
  `AllowUnknownMsgFields`; the new validator error slots are in the error contract and its completeness tests.

### Key Entities

- **Validator result**: one per refusal cause; this feature adds "MsgType undefined" and "tag undefined".
- **Reject decision**: reason, RefTagID, the third-field flag (099), and now a reason text.
- **Frame header**: the MsgType (third field) and whether a later field repeats 35 with another value.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each acceptance scenario has a witness that is RED on `fa778a8e` where today's behaviour differs and GREEN
  on the feature head.
- **SC-002**: The Reject frames for 373=0, 2 and 11 match QuickFIX C++'s acceptance definitions `14a_BadField`,
  `14c_TagNotDefinedForMsgType` and `2q_MsgTypeNotValid` field for field (other than the session-specific values).
- **SC-003**: No existing cell changes its expected outcome except those named in the plan, each with the requirement
  that supersedes it (098's AS-1/AS-3, the validation-off rep35 rows, the cells and interop goldens that pin no 58).
- **SC-004**: The paired header-scan and dispatch benchmarks, base `fa778a8e` against the head, stay within the paired
  gate's band.

## Assumptions

- The dictionary's field definitions (not only the tags messages reach) are available to build a "tag is defined" set
  once per table, with no per-message cost.
- QuickFIX/Go was not read for this feature (no local checkout); the owner's B85 ruling recorded Go's answers for 373=0,
  2 and 11.
- 099's FIXT endings are kept exactly; this feature widens them to non-FIXT and fixes the shared reader.

## Out of Scope

- B91 (#661, per-frame `SessionId`) and #549 (outbound parse arenas): the next session-lane feature.
- QuickFIX's `ValidateUserDefinedFields` / `AllowUnknownMsgFields` behaviour (disclosed, not built).
- Garbled-frame dispositions (092/093), which already carry their own texts.
