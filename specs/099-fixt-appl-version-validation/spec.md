# Feature Specification: FIXT application version, the validation half (S-026)

**Feature Branch**: `099-fixt-appl-version-validation`

**Created**: 2026-11-03

**Status**: Draft

**Input**: User description: "REMAINING-WORK FUNCTIONAL TRACK row 6, B80 / fixpp#643 + #651. A FIXT.1.1
application message is validated, parsed and handed to fromApp using the dictionary of its resolved
application version: ApplVerID(1128) if present, else the DefaultApplVerID(1137) negotiated at Logon. Admin
messages use the session's transport dictionary. Closes S-026; retires L-041-2, L-033-1, L-097-1. Owner
rulings of 2026-11-03: refuse when no dictionary answers, never deliver unchecked; a session dictionary that
defines admin messages is its own transport dictionary, else FIXT11.xml from the engine's dictionaries, else
refuse at open; C++, C ABI, Python and TOML; #651 corrects both comments."

## Terminology

- **FIXT session** — a session whose `SessionConfig::begin_string` is `FIXT.1.1`.
- **Admin message** — MsgType `0 1 2 3 4 5 A`, the same set `dict::reify` and all three QuickFIX engines
  treat as admin. Every other MsgType is an **application message**, including `n` (XMLnonFIX).
- **Session dictionary** — the session's own `SessionConfig::dictionary`. It may be a merged dictionary
  (098). It can be `FIXT11.xml`, an application dictionary such as `FIX50SP2.xml`, or FIX Latest.
- **Engine dictionaries** — `EngineConfig::dictionaries`, which fills the engine's `version_registry`.
- **Transport dictionary** — the dictionary that validates a FIXT session's admin messages (QuickFIX:
  `TransportDataDictionary`).
- **Application dictionary** — the dictionary that validates an application message and structures the view
  handed to `fromApp` (QuickFIX: `AppDataDictionary`).
- **Resolved application version** — `ApplVerID(1128)` of the message if present, else the peer's Message Type Default for its MsgType (FR-030), else the version negotiated from the peer's `DefaultApplVerID(1137)`.
- **Own version** — the application version the session dictionary describes, if any. `FIXT11.xml` describes
  none.
- **Validating session** — one with `SessionConfig::validate_inbound_messages` set.
- **S** — short for the session dictionary.
- **Shapes** — the three session-dictionary setups every witness runs on. **T**: S is `FIXT11.xml`. **A**: S is `FIX50SP2.xml`, and the
  engine dictionaries hold `FIXT11.xml` and S's base, because `Session::open()`'s FIXT guard requires a registry dictionary for
  `default_appl_ver_id`. **L**: S is FIX Latest.
- **Route** — for one frame, the table and validator it is scanned, validated and parsed with, or the refusal when no dictionary answers.
- **Route set** — the session-library object holding one validator, and the table inside it, per distinct dictionary (data-model E-1).
- **Peek** — the walk that reads MsgType and the header's 1128 before the frame scan, so the scan can use the route's table.
- **Validate gate** — `Session::validate_inbound_`, as the three arms call it.
- **Establishment arm** — `on_inbound_not_connected_` or `on_inbound_logon_sent_`: the FSM has not reached LogonReceived.
- **Third field's MsgType** — the value of the `35` that is the frame's third field, what `FrameHeader::fault_ref_msg_type` holds. **Last MsgType** — the last `35` in the frame, what `FrameHeader::msg_type` holds.

## Context: why this feature exists

**What the FIXT standard says** (FIX Transport 1.1 with errata, "Application Version Independence"). One FIXT
session can carry several application versions. Precedence, highest first: Explicit (`ApplVerID(1128)`, on that
message instance only, defaults unaltered), Message Type Default (`RefMsgType(372)` + `RefApplVerID(1130)` +
`DefaultVerIndicator(1410)` in Logon), Session Default (`DefaultApplVerID(1137)`). It does not say what a
receiver does with a version it cannot serve.

**What QuickFIX does.** Read at QuickFIX C++ `386ce46e`, QuickFIX/J `9ac173b2`, QuickFIX/Go `eff945dd`.

| | C++ | QuickFIX/J | Go |
|---|---|---|---|
| Admin message validated with | transport dictionary | transport dictionary | transport dictionary |
| Application message validated with | header and trailer by transport, body by application | same | same |
| Application dictionary chosen by | 1128, else peer's 1137 | 1128, else peer's 1137 | one configured dictionary, never by 1128 |
| Implements Message Type Default | no | no | no |
| No dictionary for the version | empty dictionary: body unchecked, delivered | auto-loads `FIXnn.xml` from its classpath and validates; an unknown version throws an unchecked exception | not applicable |
| Unknown 1128 value | 373=5 from the transport enum | unchecked exception | 373=5 from the transport enum |
| Transport dictionary not configured | `ConfigError` | loads `FIXT11.xml` from its classpath | validates nothing unless both are set |
| Peer's Logon has no 1137 | Reject, disconnect | disconnect | error, no session |

**What fixpp does today** (measured at `c0a828ef`: real parser and validator over the vendored dictionaries).
One validator, built from the session dictionary, sees every inbound frame. A validating FIXT session works only
when its session dictionary is FIX Latest.

| Session dictionary | Logon | TestRequest | NewOrderSingle |
|---|---|---|---|
| `FIXT11.xml` (the shape `tests/interop/happy/hp_support.hpp`'s production FIXT config uses) | pass | pass | rejected on tag 8, 373=2, whatever 1128 says |
| `FIX50SP2.xml` | rejected on tag 98, 373=2: the session cannot establish | rejected on tag 112, 373=2 | passes, and `1128=10` is validated as FIX50SP2, so a FIX-Latest-only field (3115) is rejected |
| FIX Latest | pass | pass | passes, and `1128=6` is validated as FIX Latest |
| `FIX44.xml` | rejected on tag 1137, 373=2 | pass | rejected on tag 1128, 373=2, if the frame carries 1128 |

`dict::reify` already resolves 1128, so a `1128=10` frame reifies as FIX Latest while validation treats it as
FIX50SP2. The parse that builds the view handed to `fromApp` and the frame scan's Length+Data pairs also read the
session dictionary. On a `FIXT11.xml` session they know no application-dictionary pair or group.

The C API has no `default_appl_ver_id` setter and cannot fill `EngineConfig::dictionaries`, so a C or Python
session cannot open as FIXT.1.1 at all. TOML loads one `[dictionary]`. (L-097-1.)

## Owner rulings (2026-11-03)

1. **No dictionary answers → refuse, never deliver unchecked.** Per message: Reject 373=5, 371=1128, the session
   stays up. An initiator's Logon reply whose 1137 is absent, unparseable or unserviceable: Reject 371=1137 and a
   disconnect, the acceptor's existing ending. Validation-off sessions are unchanged. This departs from QuickFIX
   C++ on purpose: a session that asked for validation never delivers an unvalidated message.
2. **Transport dictionary.** A session dictionary that defines admin messages is its own transport dictionary, so
   its overlay is kept. Otherwise `FIXT11.xml` in the engine dictionaries is. A validating FIXT session with
   neither fails at `open()` with `invalid_session_config`.
3. **Surfaces.** C++, C ABI (additive MINOR), Python through SWIG, TOML (optional extra dictionaries).
4. **#651.** Correct both comments. The session dictionary stays required; there is no engine fallback.
5. **Ruled after Gate A loop 1:** the rulings in the last Clarifications session. Three change the original scope: `fixpp_msg_version` becomes the resolved version, a declared break that narrows ruling 3's "additive" for that one symbol; an overlay's header Length+Data pair is honoured on every route; Message Type Default is implemented, which reverses the description's silence on it and the earlier "out of scope".

## Clarifications

### Session 2026-11-03

- Q: On a FIXT session with validation off, is the view handed to `fromApp` parsed with the dictionary of the message's resolved version? → A: Yes, as QuickFIX/J: one rule whether validation is on or off, so turning validation on never changes how a message parses. A frame with no answering dictionary uses the session dictionary (owner).
- Corrections from research, no question asked: the C API also needs a `validate_inbound_messages` setter (FR-016); sessions are built per registered entry, not per connection (NFR-003, SC-007); FR-008 narrows to the header. From the probe: a FIX 4.4 table rejects a FIXT Logon on tag 1137, so "defines the admin messages" also requires that its Logon accepts 1137 (FR-003).

### Session 2026-11-03 (Gate A round 1)

Measured answers are in research R-9. Items marked **owner** were ruled in the last session below.

- Q: What answers a 1128 past the header, when no validator step tests position (measured: it passes)? → A: A probe after the parse on an application route, ahead of the validator (FR-026).
- Q: What happens when a frame's last MsgType differs from its third field's, since routing and the validator read the first and dispatch reads the last? → A: A validating FIXT session rejects it before any dispatch, 373=13 when the second 35 is in the header and 373=14 past it, as QuickFIX C++ and Go. A non-validating session stays as today, disclosed (FR-025). Scoped to LogonReceived and Active at round 2.
- Q: Who owns the shared tables and validators? → A: A route set in the session library, built once per Engine in `Engine::start()`. `dict::version_registry` cannot own a validator (`[arch §2.3]`). This replaces the lock-free publish and restates NFR-003 and SC-007 in copies held, not only walks.
- Q: Does a refused validating FIXT config surface at registration too, since the Engine's role loops discard an `open()` failure? → A: Yes: `Engine::register_session` and `Session::open()` share one check. This adds to ruling 2's wording. **Owner** (FR-004).
- Q: Does an overlay header tag on S pass on admin messages in shape A, where the transport dictionary is not S? → A: Yes. S is the envelope on the transport route too, or a venue header tag stops the Logon (measured, R-6) (FR-008).
- Q: May a stale negotiated version select a table after a reconnect? → A: No. Selection reads a routing value both arms write on every Logon, `Unknown` when the initiator's reply has nothing it can resolve. `negotiated_appl_version_` and `negotiated_version_profile()` keep today's writes, so validation-off observables do not move (FR-027).
- Q: What does a table build failure do? → A: `out_of_memory`, from `Engine::start()` or a directly constructed session's `open()`, before any observable effect; never a route with a hole (FR-028).
- Corrections, no question asked: NFR-001's branch count is restated with its witness; FR-024 misattributed Session Default, which is S-025; an A shape needs S's base in the engine dictionaries.

### Session 2026-11-03 (Gate A round 2)

Measured answers are in research R-18. Items marked **owner** were ruled in the last session below.

- Q: What does each FSM state do with each input? → A: One table, data-model E-7. Its "today" half is the keyed logs measured through a real `Session` at `c0a828ef`; its "after" half is labelled design. It replaces the per-section endings.
- Q: Where does the 1137 check run? → A: The gate first. A gate Reject naming 1137 on a Logon in an establishment arm disconnects, and a validating initiator gains a block like the acceptor's, ahead of its 141=Y reset unit. The gate, posture and SendingTime keep today's order. **Owner** (FR-010).
- Q: What does a repeated MsgType, a route refusal or a past-header 1128 do before Logon completes, since FR-009, FR-025 and FR-026 promise a Reject and R-8 a disconnect? → A: The disconnect, for any frame whose third field is not admin or whose last MsgType differs from its third field's. The test reads the third field: `interpret_logon` reads the last 35, and measured, `35=D … 35=A` carrying Logon fields establishes a session it reaches. **Owner** (FR-029).
- Q: Does FR-025 reach LogoutSent, which has no gate? → A: No. The arm drains as today, and a frame whose last MsgType is 5 confirms the Logout. Disclosed (FR-025).
- Q: Which MsgType and sequence number does a FIXT refusal's Reject carry, since the arms pass the last-wins `hdr.msg_type`? → A: The third field's MsgType as 372 and as what keys the consume, and the first 34 as 45 (FR-025, data-model E-7).
- Q: Is `35=A` in LogonReceived or Active a Logon under FR-010? → A: No: it is outside it, and stays as measured today: on T it reaches `fromApp`, and on L `1137=99` draws a Reject. Disclosed (FR-010).
- Q: What ending has a header tag only some FIX 4.x tables declare (370)? → A: One, in every field order: it joins the peek's standard set, so a route's table judges it. QuickFIX C++ refuses it on every route. Disclosed (FR-012, research R-5).
- Q: FR-027 said a table is never selected from a previous connection's value, but its only writes are at Logon. → A: Selection does not read the slot in an establishment arm, and a stale read there is observable on a validation-off reconnect (FR-027, W-5).
- Corrections, no question asked: QuickFIX C++ and J read BeginString first and check only 1137's presence ahead of validating (research R-9); a blocking `Engine::start()` has a precondition, and its comments change (research R-3).

### Session 2026-11-03 (after Gate A loop 1)

The first four are clarify's questions. The rest are `[const §X.6]` sign-off items the owner chose to rule one by one in the same sitting; `plan.md`'s Owner sign-off numbers them.

- Q: On a frame routed to another table, which dictionary judges a header field the session dictionary declares? → A: The session dictionary, membership, enum and type, ahead of the routed table, for every header tag it declares except 35, 1128, 1129 and 1156, which the route answers (owner; FR-008).
- Q: When is a Logon's 1137 checked, relative to the validate gate? → A: The gate first; a gate Reject naming 1137 on a Logon in an establishment arm disconnects; a validating initiator gains the acceptor's check right after `check_inbound`, ahead of its 141=Y reset unit (owner; FR-010).
- Q: What does a validating FIXT session do with an application frame, or one with two different MsgTypes, before Logon completes? → A: Disconnects, sending nothing (owner; FR-029).
- Q: Does the live cell put a real peer's application frame through selection? → A: Yes: one peer-to-fixpp 35=BN with no 1128, attempted without a counterparty republish first; back to the owner if one proves necessary (owner; NFR-005, W-18 option B).
- Q: Does `Engine::register_session` refuse a validating FIXT session failing the check, as well as `open()`? → A: Yes, one shared check (owner; FR-004).
- Q: Is a non-validating bad FIXT config refused at registration too? → A: No: `open()` only, logged, a live L-row (owner; FR-004).
- Q: Which stands, the binding `[2i §4.6]` `fixpp_msg_version` or the shipped one? → A: `[2i §4.6]`: `fixpp_msg_version` returns the resolved version, a declared breaking C-ABI change under `[const §X.7]`, and no separate getter. This narrows ruling 3's "additive MINOR" for that one symbol (owner; FR-014).
- Q: Is an overlay's header Length+Data pair honoured on a frame routed to another dictionary? → A: Yes, as QuickFIX; the limit on FR-008, FR-012 and SC-004 is removed (owner).
- Q: Is FIXT's Message Type Default level implemented? → A: Yes: it is read from the peer's Logon and takes its place in resolution, between 1128 and 1137. This reverses the feature description's "out of scope" (owner; FR-024).
- Q: Does a validating FIXT session reject a frame whose last MsgType differs from its third field's, in LogonReceived and Active? → A: Yes (owner; FR-025).
- Q: Does a validating FIXT session reject an application frame whose first 1128 is past the header? → A: Yes, 373=14 (owner; FR-026).
- Q: Does "defines admin messages" mean all seven and a Logon accepting 1137? → A: Yes; a partial session dictionary is refused without `FIXT11.xml` (owner; FR-003).
- Q: How does `FIXT11.xml`, which has no NoMsgTypes on Logon, come to accept Message Type Default? → A: The group and its three missing fields are added to the vendored file, nothing else; `dictionaries/README.md` records the exception to "verbatim" (owner; FR-032).
- Q: What answers a header tag S declares when it appears past the body? → A: It is judged by S and passes, as every fixpp route does today; QuickFIX answers 373=14. A B-row (owner; FR-008).
- Q: Does a malformed 384 or 385 on a Logon disconnect, as a bad 1130 or 372 does? → A: No. It draws its Reject and stays, NextNumIn kept, so S2's list stays 1137, 1130 and 372 (owner, 2026-11-03; data-model E-10's `MT-p`).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A validating FIXT session validates each message by its own version (Priority: P1)

An operator runs a FIXT.1.1 session with inbound validation on and an application dictionary per version the peer
may use. Each application message is checked against the dictionary of its resolved version; admin messages are
checked against the transport dictionary.

**Why this priority**: This is the defect. Today such a session rejects every application message or cannot
establish, unless its dictionary is FIX Latest, and then every version is judged as FIX Latest.

**Independent Test**: Establish validating sessions in each session-dictionary shape and feed frames by version.
Each cell is RED on today's tree.

**Acceptance Scenarios**:

1. **Given** shape T with engine dictionaries holding FIX50SP2, **When** the peer logs on
   with `1137=9` and sends a valid NewOrderSingle with no 1128, **Then** the session is Active and `fromApp` runs.
2. **Given** the same session and FIX Latest also in the engine dictionaries, **When** the peer sends `1128=10`
   with a valid field 3115, **Then** it is accepted; **and When** it sends the same frame without 1128, **Then** it is
   rejected with 373=2, 371=3115.
3. **Given** shape A, **When** the peer logs on and sends TestRequest, **Then** both are validated by `FIXT11.xml` and
   accepted.
4. **Given** a session whose negotiated version is FIX50SP2 and FIX44 in the engine dictionaries, **When** the peer
   sends a FIX 4.4-valid NewOrderSingle with `1128=6`, **Then** it is accepted, 1128 included.
5. **Given** an application message that breaks its resolved version's dictionary, **When** it arrives, **Then** the
   Reject has the same 373 and 371 as the same fault on a session whose only version that is, and the message is
   not delivered to `fromApp`.

---

### User Story 2 - A session's own dictionary and its overlay survive (Priority: P1)

An operator overlays venue fields on a session's dictionary. Traffic in that dictionary's version keeps using it.

**Why this priority**: L-098-1 and the comment on fixpp#643 forbid replacing a session's dictionary with a
registry one: that would silently drop the overlay.

**Independent Test**: A session with an overlay on its own dictionary, the base in the engine dictionaries. An
overlay-added field passes on a message resolved to the own version and fails on one resolved to another.

**Acceptance Scenarios**:

1. **Given** a session dictionary merged from FIX50SP2 plus an overlay field, and the plain base in the engine
   dictionaries, **When** a message resolved to FIX50SP2 carries the field, **Then** it is accepted, and an overlay
   Required promotion is enforced.
2. **Given** that session, **When** a message resolved to FIX 4.4 carries the field, **Then** it is rejected, 373=2.
3. **Given** a FIX Latest session dictionary with an overlay, with no FIX50SP2 dictionary registered, **When** a
   message resolves to FIX50SP2, **Then** the session's own dictionary answers (the registry's one-way fallback
   would otherwise answer with the plain base).
4. **Given** a `FIXT11.xml` session dictionary whose overlay adds a header field, **When** an application message
   carries it, **Then** it is accepted and its enum and type are checked against that dictionary, as QuickFIX validates
   header fields with the transport dictionary.
5. **Given** a `FIX50SP2.xml` session dictionary whose overlay adds a header field, in shape A, **When** a Logon or a
   Heartbeat carries it, **Then** it is accepted, as the same overlay is in shape T.
6. **Given** a session dictionary whose overlay adds a Length+Data pair to the header, **When** a message resolved to its own
   version carries the pair with `SOH 1128=<other version> SOH` inside the Data, **Then** selection stays with the session
   dictionary and the Data value reads whole.

---

### User Story 3 - An unanswerable version is refused, never delivered unchecked (Priority: P1)

The operator asked for validation, so a message that cannot be validated does not reach the application.

**Why this priority**: Ruling 1. A silent fall-through is this repository's most recurring defect class.

**Independent Test**: Data-model E-7's table, walked: frames with an unknown 1128, a known 1128 with no dictionary, a 1128 past the header, a repeated
MsgType, and a Logon with an unserviceable 1137 in either role, on validating and non-validating sessions, in each state.

**Acceptance Scenarios**:

1. **Given** a validating session that is Active, **When** a message carries `1128=99` or `1128=abc`, **Then** Reject 373=5, 371=1128,
   not delivered, session up, and the sequence number is consumed as for any validation Reject.
2. **Given** a validating session that is Active with no FIX 4.2 dictionary, **When** a message carries `1128=4`, **Then** the same.
3. **Given** a validating FIXT session of either role, in an establishment arm, **When** the peer's Logon has a 1137 that is absent, empty, unparseable
   or names a version with no dictionary, **Then** Reject 371=1137 (373=1 when absent, 373=5 otherwise) and a disconnect,
   on every shape.
4. **Given** a non-validating session, **When** any of those frames arrives, **Then** behaviour is as today: 1128 is
   tolerated, the message is delivered and the initiator records what it can (033 FR-004a).
5. **Given** a validating FIXT session with no transport dictionary available, **When** `Engine::register_session` or a
   directly constructed session's `open()` runs, **Then** it returns `invalid_session_config` before any observable effect,
   and the C and Python surfaces report it to the caller (`contracts/surface.md`).
6. **Given** a validating session that is Active, **When** an application frame's first 1128 is past the header, **Then** Reject 373=14,
   371=1128, not delivered, session up.
7. **Given** a validating session that is Active, **When** a frame's last MsgType differs from its third field's, **Then** Reject 371=35
   (373=13 with the second 35 in the header, 373=14 past it), 372 the third field's, not delivered under either type, session up.
8. **Given** a validating FIXT session in an establishment arm, **When** a frame's third field is not admin, or its last MsgType differs from its third field's, **Then** Disconnected and nothing sent, whatever else the frame carries, including Logon fields and a later `35=A`.

---

### User Story 4 - The view handed to the application follows the version (Priority: P2)

An application reads a delivered message's repeating groups and Length+Data fields. They split by the dictionary of
that message's version, with or without validation, and the application can ask which version that was.

**Why this priority**: Per-message routing is S-026's stated scope. A `FIXT11.xml` session's view knows no application
group today, and validation must never parse differently from dispatch (093 E-2, fixpp#426).

**Independent Test**: A frame with a version-specific group and a dictionary-declared Length+Data pair whose data
embeds SOH, on `FIXT11.xml` sessions, validation on and off; read groups and fields back in `fromApp`.

**Acceptance Scenarios**:

1. **Given** a `FIXT11.xml` session with validation off and FIX50SP2 in the engine dictionaries, **When** an application
   message with a repeating group arrives, **Then** `fromApp`'s view reads the group.
2. **Given** an application dictionary declaring a Length+Data pair, **When** its data embeds a SOH and `34=7`, **Then**
   the frame scan, the validator and the delivered view all read one value, and MsgSeqNum is the header's, on every path
   that delivers to `fromApp`.
3. **Given** a delivered message, **When** the application asks for its resolved version, in C++ or through the C API,
   **Then** it gets the version validation used. A clone of the message and a `dict::reify` handle's view give the same answer.

---

### User Story 5 - FIXT works from C, Python and TOML (Priority: P2)

A C or Python host, or a TOML-only host, opens a FIXT.1.1 session with several dictionaries.

**Why this priority**: Ruling 3, L-097-1. Without it routing is reachable from direct C++ only.

**Independent Test**: The same session set up through each surface reaches Active against a peer and routes `1128`.

**Acceptance Scenarios**:

1. **Given** the C API, **When** a host sets `default_appl_ver_id` on a session config and adds FIX50SP2 and FIX Latest
   to an engine config, **Then** the session opens as FIXT.1.1 and routes by 1128.
2. **Given** Python, **When** it does the same through the bindings, **Then** the same.
3. **Given** TOML with `[dictionary]` naming `FIX50SP2.xml` and an extra list naming `FIXT11.xml` and FIX Latest,
   **When** it loads, **Then** every file reaches the engine dictionaries, the session dictionary is still the
   `[dictionary]` file, and the session validates admin messages and routes by 1128.
4. **Given** a TOML extra path that cannot be loaded, **When** the config loads, **Then** a diagnostic names the key and
   no engine is built.

---

### User Story 6 - A peer's per-MsgType default version is honoured (Priority: P2)

A peer declares at Logon that it sends one MsgType in a version other than its session default.

**Why this priority**: FIXT 1.1's middle precedence level, ruled in scope by the owner. Without it such a peer is refused at Logon on `FIXT11.xml`, or draws Rejects for messages valid in the version it meant.

**Independent Test**: Raw frames on T, A and L, validating and not, since no QuickFIX engine sends the group.

**Acceptance Scenarios**:

1. **Given** a validating session whose peer logged on with `1137=9|384=1|372=D|385=S|1130=10|1410=Y`, **When** it sends a NewOrderSingle with no 1128 carrying FIX-Latest-only 3115, **Then** it is accepted and its resolved version is FIX Latest; **and When** it sends the same frame with `1128=9`, **Then** it is rejected, 373=2, 371=3115.
2. **Given** that session, **When** the peer sends another application MsgType with no 1128, **Then** 1137 answers.
3. **Given** a Logon entry with `385=R`, or `1410=N`, or no 1410, **When** the same NewOrderSingle arrives, **Then** 1137 answers.
4. **Given** a validating session, **When** a Logon entry with `385=S|1410=Y` names a 1130 of `99`, or a version with no dictionary, **Then** Reject 373=5, 371=1130 and a disconnect, in either role.
5. **Given** a reconnect whose Logon declares no default, **When** that NewOrderSingle arrives, **Then** 1137 answers.

### Edge Cases

- **Non-FIXT session.** Byte-identical to today, including that a FIX.4.4 session's strict validation rejects a frame
  carrying 1128.
- **1128 on an admin message.** Ignored for dictionary selection, as in QuickFIX C++ and J; the transport dictionary
  judges the field. `FIXT11.xml`'s 1128 enum stops at 9, so `1128=10` on a Logon draws 373=5 from that dictionary. An
  application message with `1128=10` routes to FIX Latest, as QuickFIX/J (whose FIXT11.xml has 10); QuickFIX C++'s enum stops
  at 9, so it rejects 10 on every message. Disclosed, not changed.
- **1128 past the header.** Routing reads 1128 from the header only. A validating session that is Active answers an application frame whose first
  1128 is past the header with Reject 373=14, 371=1128, whatever its value, so a delivered validated message has one answer for routing
  and `dict::reify`. A non-validating session routes it by the negotiated version and delivers it (research R-5). On an admin frame the
  transport dictionary judges it, as any field.
- **Two 1128s in the header.** Both routing and `dict::reify` read the first. The validator has no repeated-tag rule on any session, which
  QuickFIX does; this feature does not add one.
- **MsgType repeated with a different value.** Data-model E-7 per state (FR-025, FR-029). A repeat of the same value splits nothing and stays as today. A non-FIXT validating session keeps the split: third field `0`, later `35=D`, validated as Heartbeat, delivered to `fromApp` (byte-identical, to file).
- **Empty `1128=`.** Present, never read as absent. Validating: Reject 373=4, 371=1128, as QuickFIX. Non-validating: the session dictionary.
- **Validation off, no dictionary for the version, or an unknown 1128.** The view uses the session dictionary, as today.
- **Validation off, a malformed pair only the route's dictionary declares.** It now takes `dispose_unparseable_`'s Reject in Active, where S's hooks read past it and delivered the frame at `c0a828ef`, because the route's table scans the frame (the clarification). Disclosed.
- **Resend and PossDup paths.** Every path that validates or dispatches an application frame uses the same selection.
- **Application message before Logon completes.** Disconnects on a validating FIXT session (FR-029).
- **`35=A` once Active.** Outside FR-010. It stays as measured today: routed to the transport dictionary, and with a value that dictionary accepts it reaches `fromApp`, with `resolved_application_version()` `Unknown`. `1137=99` passes on T and draws 373=5, 371=1137 on L. Disclosed.
- **A header tag only some FIX 4.x tables declare** (370, today). A header tag on a FIXT session in every field order, judged by the route's table: accepted on a FIX 4.2 or 4.3 route, 373=2 on any other. QuickFIX C++ refuses it on every route. Disclosed.
- **Session dictionary is FIX 4.4 on a FIXT session.** Its own version is 4.4. Its Logon does not accept 1137, so it is not
  a transport dictionary: a validating session needs `FIXT11.xml` in the engine dictionaries or `open()` refuses it. A
  non-validating session is not refused. Existing test shapes use a partial FIX 4.4 dictionary with 1137 on its Logon: with validation off they
  must keep passing; with validation on they fail FR-003 and are refused, and those tests change (research R-17).
- **A validating FIXT session on a partial session dictionary.** Refused unless a transport dictionary is registered, though it worked before for the
  messages it declares (B-row, research R-17).
- **Several sessions on one engine.** Dictionary tables and validators are shared: the cost must not be sessions times dictionaries
  (NFR-003).
- **A header Length+Data pair an overlay adds to S, on a frame routed to another table.** Honoured: for that session the route's table carries the pair, as QuickFIX honours it. If the routed dictionary pairs the same tag differently, S's pair wins on that session, so a body pair of that dictionary is not honoured there. Disclosed (FR-012).
- **Message Type Default.** FR-030 to FR-032. Beyond all three QuickFIX engines, which ignore the group. Not implemented: fixpp declaring such defaults in its own Logon, and acting on a peer's `385=R` entries for what fixpp sends. Disclosed (FR-024).

## Requirements *(mandatory)*

### Functional Requirements

**Resolution and selection**

- **FR-001**: On a FIXT session, the resolved application version of an application message MUST be 1128 if present, else
  the peer's Message Type Default for its MsgType (FR-030), else the negotiated version, using the same resolver for a wire value that `dict::reify` uses. `dict::reify` MUST take the view's resolved version when the session wrote one, so the version validated and the version reified cannot differ on a message the session resolved.
- **FR-002**: The application dictionary for a resolved version MUST be the session dictionary when the version is its own
  version; otherwise the engine dictionary for that version, including the registry's one-way FIX50SP2 → FIX Latest
  fallback; and when that answer is a dictionary of the session dictionary's own version, the session dictionary instead.
- **FR-003**: The transport dictionary MUST be the session dictionary if it defines all seven admin messages and its Logon
  accepts `DefaultApplVerID(1137)`, else `FIXT11.xml` from the engine dictionaries. The check MUST be made on the dictionary's
  content, not its version label, so a partial dictionary or a FIX 4.x table fails it (owner item 11).
- **FR-004**: A validating FIXT session with no transport dictionary MUST fail `open()` with `invalid_session_config`, and
  `Engine::register_session` MUST refuse the same config through the same check (owner item 4), so a host on an Engine route is
  told. A non-validating session MUST NOT be refused for this. **Limit (owner item 5):** a non-validating FIXT session failing today's guards is refused at `open()` only, so on an Engine route the host gets a log line and no return value.
- **FR-005**: Admin messages MUST use the transport dictionary and application messages the application dictionary, on
  every inbound path and FSM arm that validates or dispatches a frame. An application frame in an establishment arm is neither validated nor dispatched (FR-029).

**Validation**

- **FR-006**: An application message MUST be validated against its application dictionary. All existing validator
  checks and the Reject mapping apply unchanged.
- **FR-007**: On a FIXT session the FIXT standard header and trailer tags MUST be accepted and type-checked on an
  application message whatever its application dictionary, FIX 4.0–4.4 included. NoHops's count and delimiter MUST be judged on every route (data-model E-3). A non-FIXT session's validation of the
  same dictionaries MUST NOT change.
- **FR-008**: On a frame whose route has an envelope, a header tag the session dictionary declares, other than 35, 1128, 1129 and 1156, MUST be accepted and judged by the session dictionary, enum and type included, whatever the routed table declares for that tag. That holds on an application message and on an admin message when the transport dictionary is not the session dictionary. The four excepted tags depend on the route: `FIXT11.xml` closes 1128 at 9, so S judging it would refuse `1128=10`, and a FIX 4.x table closes 35. A later occurrence of such a tag past the body is judged the same way and passes, where QuickFIX answers 373=14 (B-row; W-3 pins it). **Limit:** the trailer equivalent is not honoured (research R-6, L-row), and the structure of a header group an overlay adds to S is not judged on another table (data-model E-3, the same L-row). A Length+Data pair S declares in its header is honoured on every route (FR-012).
- **FR-009**: A validating session in LogonReceived or Active MUST answer an unknown 1128 value, or a valid value with no application dictionary,
  with Reject 373=5, 371=1128, 45 the first 34, no `fromApp`, and the session stays up. Sequence numbers follow the existing validation
  Reject rule (fixpp#423). An establishment arm answers such a frame under FR-029.
- **FR-010**: A validating FIXT session of either role, in an establishment arm, MUST answer a Logon whose 1137 is absent (373=1), empty, unparseable or has
  no dictionary (373=5) with Reject 371=1137 and a disconnect, on every shape. The validate gate answers first where the transport dictionary judges the value, and otherwise a block later in the arm answers: the acceptor's today's, after mTLS; the initiator's new one, right after `check_inbound` and so ahead of its 141=Y reset unit, so a refused Logon resets nothing (owner item 3). The negotiated version is still written where it is today. **Limits:** the ending is one per input for a single fault, on the wire. A double fault follows dictionary order: `1137=99` with a stale 52 is 373=10 on T and A and 373=5 on L, and an absent 1137 with an undeclared tag is 373=2 naming that tag, and stays in the arm. `1137=99` and `abc` keep NextNumIn on L and consume it on T and A (E-7 `NC-e`, `LS-e`). W-5 pins each. A Logon in LogonReceived or Active is outside this requirement (Edge Cases).
- **FR-011**: A non-validating FIXT session MUST keep today's dispositions for FR-009 and FR-010's inputs (033 FR-004a, FR-010). What its frames gain is the route's table: its scan, which can give a malformed pair only that table declares a Reject where it was delivered (Edge Cases; W-6's `off-pair`), and its group metadata, so on shape A the view's `group(627)` answers where it did not (data-model E-3; W-6's `off-hops`).

**Parse and dispatch**

- **FR-012**: The frame scan, the validator's parse and walk, and the parse that builds the view handed to `fromApp` MUST
  use one dictionary per frame, the one FR-002 or FR-003 selected, with or without validation. The peek that selects it MUST walk with the
  session dictionary's Length+Data pairs and stop at the first tag that is not a header tag; a trailer tag is not one. The FIXT standard header's tags, and 370, count as header tags whatever the route (research R-5). For a session whose S declares a Length+Data pair in its header, a route's table is the dictionary's table plus those pairs (data-model E-1), so the frame is still one table's.
- **FR-013**: With validation off and a resolved version that is unknown or has no dictionary, the frame MUST be scanned and
  parsed with the session dictionary. Delivery is as today.
- **FR-014**: An application MUST be able to obtain the resolved application version of a delivered message, in C++, through the C API and from Python. In C that is `fixpp_msg_version`, which MUST return `{kind, session, application}` as `[2i §4.6]` specifies: a declared breaking change under `[const §X.7]`, because today it returns BeginString and raw tag 1137, which remain readable with `fixpp_msg_get_string`. It MUST survive `fixpp_msg_clone` and `dict::reify` (data-model E-5).
- **FR-015**: An outbound application message whose header carries 1128 MUST keep it, and a witness MUST pin that, so S-026's
  "explicit application version per message" holds in both directions.

**Surfaces**

- **FR-016**: The C API MUST add setters for `default_appl_ver_id` and `validate_inbound_messages` (validation is unreachable from C today), a call that adds a dictionary to an engine config, and the `FIXPP_APPL_VER_*`, `FIXPP_SESSION_VER_*` and message-kind constants `[2i §3.10]` promises. These are additive; with FR-014 the bump is a C-ABI MINOR, as `[const §X.7]` requires. Python follows through SWIG.
- **FR-017**: TOML `[dictionary]` MUST accept an optional list of extra dictionary paths, resolved like `path`, added to the
  engine dictionaries after `path`, so the session dictionary default stays the `path` file. A load failure MUST be a
  diagnostic naming the key.
- **FR-018**: Overlay behaviour (098) MUST be unchanged: a TOML session's `dialect_overlay` still merges onto that session's
  base, and the extra dictionaries take no overlay.

**Comments (#651)**

- **FR-019**: The header of `include/fixpp/session/session_config.hpp` and `Session::open()`'s comment MUST state that the
  dictionary has no engine anchor and is required on the session. No fallback is added.

**Records**

- **FR-020**: S-026 MUST go to `done` with named witnesses, and L-041-2, L-033-1 and L-097-1 MUST move to the closed B&L file, each only when
  its condition in research R-16 holds. S-026's row MUST state what the live cell proved.
- **FR-021**: B-095-1, B-097-1, B-097-3, B-097-4, L-098-1, L-033-5 and the coverage-index rows for §4.3.7 and §5.3.x MUST be re-read and
  corrected where they state or depend on the old behaviour.
- **FR-022**: The QuickFIX C++ departure (ruling 1), and the dispositions FR-010, FR-025, FR-026 and FR-029 add, MUST be recorded as B-rows with their
  reasons, along with every "Disclosed" of Edge Cases and the splits in data-model E-5 (`quickstart.md` lists them).
- **FR-023**: `brain/components/session.md` and `brain/components/dictionary.md` MUST record the two-dictionary decision
  and what was rejected; `brain/components/config.md`, `python-api.md` and `c-api.md` MUST record the surfaces this adds. Any file whose
  decision this supersedes MUST name the new one in a header comment.
- **FR-024**: Message Type Default (§5.3.4) is implemented by FR-030 to FR-032 and S-026 claims it. A live L-row MUST record what is not: fixpp never declares such defaults in its own Logon, a peer's `385=R` entries do not change what fixpp sends, and `RefApplExtID(1406)` and `RefCstmApplVerID(1131)` are ignored, as `ApplExtID` and `CstmApplVerID` are today. The eight-byte RefMsgType and the 256 counted entries of data-model E-10 are limits of this feature, not of FIX: a longer custom MsgType with a default is refused. No live cell can witness it: no QuickFIX engine sends the group.

**Added at Gate A**

- **FR-025**: A validating FIXT session in LogonReceived or Active MUST reject a frame whose last MsgType differs from its third field's, before any dispatch and ahead of
  the gate's 3/5 exemption: 371=35, 373=13 when the second 35 is in the header, 373=14 when past it, 45 the first 34. The third field's value MUST be both RefMsgType and what keys the consume, so `35=D … 35=4` is consumed and `35=4 … 35=D` is not (owner item 9). **Limits:** an establishment arm answers under FR-029. LogoutSent has no gate: it drains the frame, and a last MsgType of 5 confirms the Logout. A non-validating session and a non-FIXT session keep the split.
- **FR-026**: A validating FIXT session in LogonReceived or Active MUST reject an application frame whose first 1128 is past the header with 373=14, 371=1128,
  whatever its value, ahead of the validator (owner item 10). An establishment arm answers under FR-029. **Limit:** a dictionary declaring 1128 in a message body would be false-rejected; none vendored does (W-4 recomputes it).
- **FR-027**: Outside the establishment arms, table selection MUST read a per-connection value written on every Logon, `Unknown` when an initiator's reply carries nothing it can resolve, and in an establishment arm it MUST NOT read that value, so a table is never selected from a previous connection's value. A stale read there is observable on a validation-off reconnect (W-5). `negotiated_version_profile()` is unchanged.
- **FR-028**: A table or validator build failure, or the failure of `open()`'s reservation for Message Type Defaults (data-model E-10), MUST surface as `out_of_memory` from `Engine::start()`, or from a directly constructed
  session's `open()`, before any observable effect, distinct from a missing dictionary. On an Engine route the reservation's failure takes the role loop's failure branch: logged, not returned (research R-2).
- **FR-029**: In an establishment arm, a validating FIXT session MUST disconnect, sending nothing, on a frame whose third field's MsgType is not admin or whose last MsgType differs from its third field's, before the validate gate and without reaching `interpret_logon` (owner item 2).

**Message Type Default (owner item 8)**

- **FR-030**: On every FIXT session, validating or not, each Logon that reaches the record site MUST replace the connection's Message Type Defaults with the entries data-model E-10 keeps. Only a Logon writes them, and a connection whose Logon declares none has none (FR-027 applies to them).
- **FR-031**: A validating FIXT session of either role, in an establishment arm, MUST answer each fault data-model E-10 lists in a Logon's MsgTypeGrp with E-10's Reject and a disconnect, at the place and with the ending FR-010 gives a bad 1137; a Reject the gate raises naming 1130 or 372 on a Logon disconnects too. A non-validating session MUST refuse nothing, and keeps what E-10 says.
- **FR-032**: The vendored `FIXT11.xml` MUST declare MsgTypeGrp on Logon, as FIXT 1.1 and QuickFIX/J's copy do, with no other change, and `dictionaries/README.md` MUST state the exception and its refresh recipe MUST re-apply it.

### Non-functional Requirements

- **NFR-001**: A non-FIXT session MUST gain no more than one branch on a non-validated inbound frame and two on a validated one (the FIXT
  check at arm entry; the check ahead of the gate), both on a value fixed at `open()`. Its inbound and validate paths MUST NOT regress beyond the
  paired-bench band (`[const §VIII.2]` as gated today). The bench is the witness; the branch count is a design statement checked at review.
- **NFR-002**: No per-message heap allocation on a FIXT session's inbound path, validating or not, in the scope `plan.md`'s `[const §XV.1]` row cites. A real-`Session` guard modelled on
  `tests/perf/test_session_recovery_alloc_guard.cpp` MUST cover a message routed to a dictionary other than the session's, and one routed by a Message Type Default.
- **NFR-003**: On an Engine with a FIXT session, each distinct dictionary it uses (engine dictionaries and FIXT sessions' dictionaries) MUST be
  walked once, in `Engine::start()`, never per session, per message, or inside a coroutine. N sessions on M distinct route tables MUST hold M
  validators, each holding its one table, not N×M. A route table is a dictionary, plus S's header pairs when S declares any (FR-012). A witness MUST count walks and heap, and go RED under a per-session
  copy. A directly constructed session builds its own set. A C-ABI session's outbound snapshot stays per session, as today.
- **NFR-004**: C ABI changes other than `fixpp_msg_version` (FR-014) MUST be additive. ABI golden, freeze manifest and symbol map updated, version bumped, and the break marked **BREAKING** in each place `[const §X.7]` names.
- **NFR-005**: A live FIXT.1.1 / FIX.5.0SP2 cell against QuickFIX-cpp and QuickFIX/J with fixpp's validation ON (B-095-1
  ran it off because of L-041-2) MUST pass, or the plan MUST state what stops it and the owner rules.

### Key Entities

- **Session dictionary, Engine dictionaries, Transport dictionary, Application dictionary, Resolved application version, Route, Route set**: see Terminology.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every cell in the Context's "today" table that is wrong goes from RED to GREEN, witnessed by a test RED on
  `c0a828ef` and by a mutant that restores the old selection.
- **SC-002**: A message resolved to version A carrying a field only version B defines is rejected, and accepted when resolved
  to B, for at least FIX50SP2 / FIX Latest / FIX 4.4, in both session-dictionary shapes.
- **SC-003**: No validating session delivers a message it could not validate: every refusal input in User Story 3, in each state of data-model E-7, on every shape, reaches
  `fromApp` zero times, with a counted callback, and the arm is shown able to report non-zero.
- **SC-004**: An overlay on the session dictionary behaves the same with and without other engine dictionaries registered, on every frame its dictionary answers, and on a header field of a frame routed to another table, a Length+Data pair and a standard field's domain included; the tags S does not judge are FR-008's four.
- **SC-005**: Non-FIXT behaviour is unchanged: the existing test suite passes unmodified on every non-FIXT session test.
- **SC-006**: The same FIXT session reaches Active and routes `1128` from C++, C, Python and TOML.
- **SC-007**: N FIXT sessions on one C++ Engine route over M distinct route tables walk M times and hold M validators; an added session adds, beyond what a
  non-FIXT session with a minimal dictionary adds, less heap than one copy of the smallest application dictionary it routes to. A C-ABI session is outside the second half: `fixpp_session_open` mints a per-session snapshot for its outbound path (NFR-003), one further walk and one full table.
- **SC-008**: A message with no 1128 is validated by, and its view and version follow, the peer's Message Type Default for its MsgType; 1128 beats it; another MsgType uses 1137; the next connection's Logon replaces it; on every shape.

## Assumptions

- The header-required divergence from QuickFIX stays: fixpp's dictionary validator does not enforce header required-presence;
  the session FSM owns it (B-081-1). This feature changes which dictionary answers, not that decision.
- Reject wording for unknown MsgTypes and undefined tags stays as is; B85 (#655, #653) changes them later on this same validator.
- The vendored `FIXT11.xml` changes only as FR-032 says. FIX Latest's ApplVerID 10 on an application message is resolved before any
  dictionary is consulted.
- Multi-dictionary TOML covers the engine; per-session TOML keys are unchanged.
- FIXT session-dictionary overlays reach only that session (L-098-1): no feature here merges an overlay into engine dictionaries.

## Normative References

- **Spec refs** (`[const §VI.5]` form, titles as `spec/coverage-index.md` spells them):
  - `[FIX-SL §4.3.7] Specifying application version (DefaultApplVerID 1137 / FIXT)` (S-025, S-026);
  - `[FIX-SL §5.3] FIXT session profile` (S-025, S-026; informative);
  - `[FIX-SL §5.3.2] Multiple application version support` (S-026; informative);
  - `[FIX-SL §5.3.3] Session default application version identification (DefaultApplVerID 1137)` (S-025; informative);
  - `[FIX-SL §5.3.4] Message type default application version` (S-026; informative);
  - `[FIX-SL §5.3.5] Explicit application version per message (ApplVerID 1128)` (S-026; informative).
- **QuickFIX reference behaviour** (cited, not vendored): QuickFIX C++ `386ce46e` (`Session`, `Message`, `DataDictionary`), QuickFIX/J
  `9ac173b2a69ef2305258330d88685eccad80512d` (`Session`, `Message`, `DefaultDataDictionaryProvider`, `org.quickfixj.SimpleCache`, the MINA I/O
  handlers), QuickFIX/Go `eff945dd` (`validation.go`).
- **Catalogue rows**: S-026 (closes); S-025 (the negotiated version it reads); D-009 (the overlay it must keep).
- **Constitution**: `[const §VI.4]`–`[const §VI.6]`; `[const §VII.3]`, `[const §VII.4]`, `[const §VII.7]`, `[const §VII.8]`; `[const §VIII.2]`, `[const §VIII.3]`, `[const §VIII.5]`; `[const §X.1]`, `[const §X.5]`–`[const §X.7]` (a declared break);
  `[const §XI.7]`; `[const §XIV.2]`; `[const §XV.1]`, `[const §XV.9]`, `[const §XV.13]`. The whole check is in `plan.md`.
- **Design documents**: `[arch §2.3]`, `[arch §2.4]`; `[2b §6.5]`; `[2c §4.6]`, `[2c §4.9]`, `[2c §5]`; `[2i §3.10]`, `[2i §4.6]`. The notes they gain are in
  research R-11.
