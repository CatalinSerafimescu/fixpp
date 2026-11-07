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
3. **Two MsgTypes in one frame (#657, #658).** The header scan keeps the **last** 35, the Logon interpreter and
   LogoutSent's reply test read the last 35, and the validator and the message view read the **first**. So a validation-off session establishes on a
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
   Reject and BusinessMessageReject sites other than the disposer pass the inbound MsgType unbounded, and a value too
   long for the site's build buffer makes the build fail, so **no Reject (or BMR) is sent at all**, silently. Nothing
   upstream bounds a MsgType's length below the inbound limit, and on a validation-off session (the default) any
   non-admin MsgType reaches `fromApp`, so the BMR case is reachable from the wire.

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
  *(Narrowed at Gate A round 2: not echoed only above `kSendFrameBytes`.)*
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

### Session 2026-11-07 (Gate A round 1)

Resolutions of the reviews' findings; the decisions they land in are research D1–D10 and its Supersessions table.

- Q: What is "the third field's MsgType" in a reader that scans? → A: the **first 35**. On every frame a state arm
  handles it is the third field, because 093 disregards any other frame first (D4).
- Q: LogoutSent, which has no validation gate? → A: on every session kind, a frame is the Logout reply only when its
  first 35 is 5: `D…5` no longer confirms, `5…D` does. No Reject. This follows from "the third field everywhere" and
  changes 099's pinned `LO-a` rows (research Supersessions).
- Q: A frame with three or more 35s? → A: a conflict whenever **any** later 35 differs from the first (`D…A…D` is one).
  The Reject is 373=14 when any later 35 lies past the header, else 373=13 when one lies in it, else 14 — QuickFIX
  C++'s structure-before-iterate precedence (research D5's three-clause rule). With two 35s this equals 099's answer (D5).
- Q: The header boundary for that choice on a non-FIXT session? → A: the session dictionary's header ∪ the FIXT standard
  header set, as on FIXT, which is the closer model of QuickFIX C++'s `isHeaderField`; the residual (tags fixpp's
  FIXT standard header set holds and QuickFIX C++'s static list lacks) is disclosed. Codex's dictionary-only boundary is not adopted (plan.md `## Gate A`, round 1 disagreements).
- Q: "Handled as X, no Reject" when X's own path can Reject (X = 4, X = A)? → A: defined by a differential oracle: the
  ending equals that of the same frame with every later 35 removed and re-framed. "No Reject" means none for the repeat
  (D6).
- Q: An undefined tag inside a group — confirmed? → A: yes for the AS-2 layout (the tag last in a single instance):
  QuickFIX C++ answers 373=0, 371 = the tag, as fixpp. Mid-instance or several instances, C++ can answer 2 or 16;
  disclosed as a B-row (D2).
- Q: 092's disposer bounds 372 at the longest shipped MsgType; does FR-007 reach it? → A: yes. The bound is retired and
  the disposer echoes verbatim through the same helper (D7). One rule for 372.
- Q: QuickFIX's text below FIX.4.2? → A: fixpp sends the FIX.4.2-and-later form on every BeginString (58 = the text, with
  371/373 as today). QuickFIX C++'s `<text> (<tag>)` form below 4.2, and its omission of 373 for reasons above 11 on 4.2,
  are disclosed (D3).
- Q: Long CompIDs plus 58 overflow the Reject buffer? → A: disclosed as a residual; 58 is not dropped to make room (the
  B85 ruling). A CompID length bound at `open()` is a follow-up issue (D7). *Narrowed at round 2 (below).*
- Q: The BusinessMessageReject whose echo does not fit? In 35=j RefMsgType(372) is **required**, so the 2026-11-07
  "372 is optional" rationale covers Reject (35=3) only. → A: left open at round 1 for a ruling; **decided at round 2**
  (below).

### Session 2026-11-07 (Gate A round 2; orchestrator ruling on the owner's delegation)

- Q: The BusinessMessageReject whose echo does not fit its leaf's buffer? → A: it **always carries 372 verbatim**
  whenever its frame fits the engine's outbound ceiling `Session::kSendFrameBytes`, which is what QuickFIX C++ and J do
  (they echo with no frame bound). On an overflow of the leaf's own buffer it is rebuilt, with 372, into a
  session-owned `kSendFrameBytes` inbound reply slot, before `toApp` and `assign_outbound`. Above the ceiling it is not sent, as no
  outbound frame above it is: one rate-bounded session log record, the session stays Active, and a B-row discloses it.
  Full parity up to the inbound limit is a follow-up for the owner (research D7, "(b-L)").
- Q: Does the session Reject follow the same rule? → A: yes, one rule for 372 on both messages. The Reject echoes 372
  verbatim whenever it fits `kSendFrameBytes`, through the same slot rebuild. Above the ceiling it is sent without 372,
  which is optional in 35=3. This **narrows** the 2026-11-07 orchestrator answer to #646 ("an inbound MsgType too long
  for the Reject is not echoed … and the Reject is sent"): the echo is now dropped only above the ceiling. The #646
  ruling (keep the echo verbatim; never lose the Reject for it) stands.
- Q: Long CompIDs, now? → A: the slot rebuild recovers any overflow of a Reject's own buffer, CompIDs and 58 included,
  up to `kSendFrameBytes`. The residual is a Reject that exceeds the ceiling even without 372: the outbound-ceiling
  ending, disclosed. The CompID length bound at `open()` stays a follow-up issue.
- Q: May the rebuild allocate (a coroutine owning the buffer allocates one frame per run)? → A: **no**. A reply to
  an inbound frame is on `[const §XV.1]`'s parse → validate → dispatch path, a peer can repeat the overflow, and #544
  R-10's disposition (an engine-caused fallback) does not carry over. The buffer is a session-owned slot used only by
  the inbound-fed reply sites, sound under the precondition that a caller awaits each `on_inbound_frame` before the
  next. The Engine already does so, and the arms are unsound under overlap. The precondition is stated on the public
  header, asserted in debug builds and disclosed (research D7).
- Q: Which hooks read the field that ends the header, in D5's 13/14 walk? → A: the route's, for that field and every
  later one; S's before it; one `length_data_carry` throughout. A walk that faults answers 14, the 35s it saw discarded
  (research D5).

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
   the Reject carries QuickFIX C++'s text for that reason (the FIX.4.2-and-later form), and its other fields are
   unchanged.
4. **Given** an undefined tag inside a repeating-group instance, **Then** 373=0, 371 = the tag.
5. **Given** a hand-built table that gives a tag a type, an enum or a group role but no message, **When** a message
   carries that tag, **Then** 373=2 (the tag is defined), not 373=0.

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
   `35=D`, **Then** Reject 373=14 (the later 35 past the header) or 373=13 (inside it), 371=35, 372=`0`, the frame
   consumed as `0`, no `fromApp` (today: delivered to `fromApp`).
4. **Given** a validating non-FIXT acceptor before Logon, **When** such a frame arrives, **Then** disconnect, nothing
   sent.
5. **Given** a validation-off session in Active, **When** a frame's MsgTypes differ, **Then** its ending equals that of
   the same frame with every later 35 removed, and no Reject is sent for the repeat. Third field `0` with a later `35=D`
   is a Heartbeat (today: `fromApp`); third field `D` with a later `35=5` is an application message (today: logs the
   session out); third field `D` with a later `35=3` is an application message (today: consumed in silence).
6. **Given** any session in LogoutSent, **When** a frame's third field is `35=D` and a later field is `35=5`, **Then**
   it is not the Logout reply (today: confirms the Logout); third field `5` with a later `35=D` confirms it (today: does
   not).
7. **Given** any session, **When** a frame carries three or more 35s and any later one differs from the first (`D…A…D`,
   `A…D…A`), **Then** it is a conflict like any other: disconnect in establishment, FR-005's Reject on a validating
   session after it, with 373 by research D5's three-clause rule.
8. **Given** a frame with one MsgType, or a repeat with the same value, **When** it arrives in any state, **Then**
   handling is unchanged.

---

### User Story 4 - A Reject is never dropped for its echo (Priority: P2)

A peer sends a message whose MsgType value is long (hundreds of bytes or more) and which draws a session Reject or a
BusinessMessageReject.

**Acceptance Scenarios**:

1. **Given** any inbound-fed session Reject site, **When** the Reject with the inbound MsgType overflows the site's own
   buffer but fits `kSendFrameBytes`, **Then** the Reject is sent with 372 verbatim, every other field as for a short
   MsgType (today: no Reject).
2. **Given** the same, **When** the Reject with 372 exceeds `kSendFrameBytes`, **Then** it is sent without 372.
3. **Given** a validation-off session whose application rejects a message in `fromApp`, **When** the
   BusinessMessageReject with the inbound MsgType overflows its leaf's buffer but fits `kSendFrameBytes`, **Then** the
   BMR is sent with 372 verbatim (today: no BMR), and a later ResendRequest covering it resends it with 372.
4. **Given** the same, **When** the BMR with 372 exceeds `kSendFrameBytes`, **Then** no BMR is sent, no outbound
   number is consumed, the inbound message is consumed, the session stays Active, and the session log records it.
5. **Given** `35=D=X` drawing a Reject, **Then** the Reject carries `372=D=X` verbatim, and fixpp's own parser reads it
   back as one field 372 with value `D=X`.
6. **Given** a garbled frame (092's disposer) whose MsgType fits `kSendFrameBytes`, **Then** the Reject carries it in
   372 (today: 372 omitted for any MsgType longer than the longest shipped one).

### Edge Cases

- MsgType repeated with the **same** value only: one MsgType, so FR-004..FR-006 do not apply and handling is today's
  (099's FR-025 compares values too). QuickFIX C++ answers a repeated tag with 373=13 whatever its value; a general
  repeated-tag check is not this feature's (the existing row B-099-22 already discloses that the validator has no
  repeated-tag rule).
- A repeated 35 in a frame that also fails another check: the frame-level check order is 099's (FR-025 ahead of the
  validator), extended to non-FIXT. A frame with several defects can draw a different first reason from QuickFIX C++,
  which checks required tags before the field walk and walks fields in tag order (disclosed, FR-009).
- The resend replay and the outbound store masking also read the header scan's MsgType: they read the first 35 after
  this feature; stored outbound frames have one MsgType, so their behaviour does not change (controls, research D9).
- A FIXT-only header tag (1128, say) before a later 35 on a non-FIXT session keeps that 35 in the header (373=13), as
  QuickFIX C++'s static header list does.
- An unknown MsgType on an admin-length value (one character, e.g. `35=Z`): an unknown MsgType, 373=11.
- FIXT: the MsgType check uses the dictionary the frame is validated with (099's resolved application version or the
  transport dictionary).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001** (#655): The validator MUST check that the dictionary it validates with defines the frame's MsgType, after
  the header-order check and before the field walk, sharing the field walk's one MsgType lookup, and MUST report an
  undefined MsgType with its own result, mapped to 373=11 with no 371.
- **FR-002** (#653): The validator MUST report a tag the dictionary does not define at all with its own result, mapped
  to 373=0 with 371 = the tag, and a tag the dictionary defines but not for this message (header, trailer and body
  sets as validated today) with 373=2, 371 = the tag. "Defined" means present in the dictionary's merged field store; on
  a hand-built table, named by any population operation (research D1).
- **FR-003** (B85 ruling): Every session Reject the inbound validation gate sends MUST carry Text(58) with QuickFIX
  C++'s text for its reason (`SessionRejectReason_*_TEXT`, the FIX.4.2-and-later form), derived from the reason at the
  emit site. Rejects from other sites (the 092 disposer, the sequence-reset and admin paths) keep their current text.
- **FR-004** (#657, #658): A frame's MsgType MUST be its first 35 — the third field on every frame a state arm handles —
  for every reader: header scan, Logon interpretation, state-arm dispatch, LogoutSent's reply test, validation, the
  message view, RefMsgType echo and the consume key. A frame's MsgTypes **differ** when any later 35's value differs
  from the first.
- **FR-005** (#658): A validating session, FIXT or not, MUST refuse a frame whose MsgTypes differ: in LogonReceived and
  Active with Reject 371=35, 372 = the first 35, 45 = the first 34, and 373 by research D5's three-clause rule (14
  when any later 35 lies past the header, else 13 when one lies in it, else 14; a walk that faults answers 14), ahead
  of the 3/5 exemption, consuming the frame as its first 35; in an establishment arm with a
  disconnect, sending nothing. This supersedes 099's FR-025/FR-029 comparison and carve-outs (research Supersessions).
- **FR-006** (#657): A validation-off session in an establishment arm MUST refuse a frame whose MsgTypes differ with a
  disconnect, sending nothing. In LogonReceived, Active and LogoutSent a validation-off session MUST handle such a frame
  as the same frame with every later 35 removed would be handled, and send no Reject for the repeat. LogoutSent, on any
  session, takes a frame as the Logout reply only when its first 35 is 5.
- **FR-007** (#646): Every inbound-fed Reject and BusinessMessageReject builder — both `emit_session_reject_` forms,
  `inbound_reject_leaf_`, `inbound_reject_best_effort_leaf_`, `inbound_business_reject_leaf_` at round 1 (research D7's
  census recipe decides), and so the 092 disposer and the validation gate — MUST echo the inbound MsgType verbatim,
  `=` included, in every message whose frame fits `Session::kSendFrameBytes`. A build that runs out of room in the
  site's own buffer MUST be rebuilt into the session's inbound reply slot (a `kSendFrameBytes` member, used only by
  the inbound-fed reply sites), and the site MUST run its unchanged tail over that frame. Every attempt runs after the outbound-number peek and the SendingTime stamp and before the site's first
  callback (`toAdmin` for a Reject, `toApp` for the BMR) and `assign_outbound`, so nothing is re-stamped, re-numbered
  or fired twice. Text(58) is never dropped to make room. Above the ceiling (research D7):
  - (a) **session Reject (35=3):** it MUST be sent without 372. A Reject that exceeds the ceiling even without 372 is
    not sent, and the session log records it;
  - (b) **BusinessMessageReject (35=j):** it MUST NOT be sent without 372. It is not sent: no callback, no outbound
    number, the inbound consumed as today, the session in its state, and the session log records it (rate-bounded).
- **FR-008**: A frame not covered by FR-001..FR-007 MUST be handled byte-identically to today, on every session kind.
  No per-message allocation is added to the inbound path, the overflowed replies of FR-007 included (`[const §XV.1]`).
  `on_inbound_frame` MUST be documented as requiring each call to complete before the next on the same Session, and
  a build with assertions MUST assert when the inbound reply slot is taken while held (research D7). The paired
  benchmarks of research D8 show no slowdown above the `[const §VIII.2]` budget.
- **FR-009** (records): The B&L rows for #646 (L-452-2, closed; B-092-3's RefMsgType bullet, rewritten to D7's rule) and
  #657/#658 (B-099-3's conflict clause; B-099-4's headline, its "Repeated MsgType" bullet and its "Not validating"
  bullet; B-099-5; B-099-11) move or change to the new endings. Research's Supersessions list is the complete set and
  its recipe re-derives it. New B-rows disclose: fixpp's config loader ignores QuickFIX's
  `ValidateUserDefinedFields` and `AllowUnknownMsgFields`; the FIX.4.0–4.2 text and 373 differences (D3); the
  header-set residual against QuickFIX C++'s static list (D5); the group-layout divergence (D2); multi-defect
  precedence (Edge Cases); `interpret_logon`'s first-wins change, public C++ (D4); the outbound-ceiling ending (D7: a
  BMR above `kSendFrameBytes` is not sent, where QuickFIX sends it; a Reject above it is sent without 372, or not at all
  when it exceeds it even without 372, the long-CompID residual); `on_inbound_frame`'s serialized-caller
  precondition, now stated and asserted (D7); the new LogoutSent and validation-off endings. The new validator error slots are in the error contract and
  its completeness tests.

### Key Entities

- **Validator result**: one per refusal cause; this feature adds "MsgType undefined" and "tag undefined".
- **Reject decision**: reason, RefTagID, the third-field flag (099); unchanged. The reason text is derived from the
  reason at the emit site, not stored.
- **Frame header**: the MsgType (the first 35) and whether any later 35 has another value.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each acceptance scenario has a witness that is RED on `fa778a8e` where today's behaviour differs and GREEN
  on the feature head, and RED with its production change reverted.
- **SC-002**: For a frame with one defect, the Reject frames for 373=0, 2 and 11 match QuickFIX C++'s acceptance
  definitions `14a_BadField`, `14c_TagNotDefinedForMsgType` and `2q_MsgTypeNotValid` field for field (other than the
  session-specific values), on FIX.4.2 and later.
- **SC-003**: No existing cell changes its expected outcome except those in the manifest that research D9's recipes
  derive at the head being accepted (re-derived after the last fix commit), each with the requirement that supersedes it
  and, where a merged feature's clause is superseded, that clause by name (research Supersessions).
- **SC-004**: Research D8's paired runs (A-B-A-B, min-per-tree, base = the merge-base with `origin/main` at measurement
  time) of `scan_frame_header_bench`, `on_inbound_frame_bench`, `validator_bench` and the timed `BM_TableView_Build*` /
  `BM_TableView_Copy*` rows of `table_view_footprint_bench` show no slowdown above +5%, or carry the `[const §VIII.2]`
  approval.

## Assumptions

- The dictionary's field definitions (not only the tags messages reach) are available to build a "tag is defined" set
  once per table, with no per-message cost.
- QuickFIX/Go was not read for this feature (no local checkout); the owner's B85 ruling recorded Go's answers for 373=0,
  2 and 11.
- 099's two-occurrence FIXT endings in LogonReceived and Active, its validating FIXT establishment disconnect and its
  1128 refusals are kept. Its endings change for frames with three or more 35s (sticky conflict, 13/14 precedence), for
  LogoutSent's reply test, and for validation-off establishment; each superseded clause is named in research's
  Supersessions table.
- QuickFIX/Go was not read for #646's echo; C++ and J agree.

## Out of Scope

- B91 (#661, per-frame `SessionId`) and #549 (outbound parse arenas): the next session-lane feature.
- QuickFIX's `ValidateUserDefinedFields` / `AllowUnknownMsgFields` behaviour (disclosed, not built).
- Garbled-frame dispositions (092/093), which already carry their own texts.

## Normative References

- **Spec refs** (`[const §VI.5]` form, titles as `spec/coverage-index.md` spells them):
  - `[FIX-SL §4.5.4] Rejecting invalid messages (Reject 35=3)` (S-007, S-034);
  - `[FIX-SL §4.5.2] Garbled message processing` (the 092 disposer's echo; informative);
  - `[FIX-TC §4 scenario 2] Receive Message Standard Header` (TC-002; sub-case 2q);
  - `[FIX-TC §4 scenario 14] Receive application or session layer message` (TC-010; sub-cases 14a, 14c);
  - `[FIX50SP2 §3] Message validator — required fields, type conformance, enum values, group structure` (W-014);
  - `[FIX50SP2 §3.1] Standard header (BeginString/8, BodyLength/9, MsgType/35, mandatory ordering)` (W-002, W-004, W-005).
- **QuickFIX reference behaviour** (cited, not vendored): QuickFIX C++ `2ce8a60` (`Session`, `Message`,
  `DataDictionary`, `Values.h`), QuickFIX/J `4969564`, QuickFIX/Go `eff945d` per the B85 ruling note (not read for #646).
- **Catalogue rows**: S-007, S-034, W-014, TC-002, TC-010.
- **Constitution**: `[const §VI.5]`; `[const §VIII.2]`; `[const §X.1]`, `[const §X.4]` (no C-ABI version step: no `fixpp_error_t` code added);
  `[const §XV.1]`; `[const §XVII.1]`. The whole check is in `plan.md`.
