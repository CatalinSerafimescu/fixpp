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
- **Resolved application version** — `ApplVerID(1128)` of the message if present, else the version negotiated
  from the peer's `DefaultApplVerID(1137)`.
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
- **Validate gate** — `Session::validate_inbound_`, as the three arms call it. **FIXT pre-gate** — the checks that run ahead of it on a validating
  FIXT session (FR-010, FR-025).

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

## Clarifications

### Session 2026-11-03

- Q: On a FIXT session with validation off, is the view handed to `fromApp` parsed with the dictionary of the message's resolved version? → A: Yes, as QuickFIX/J: one rule whether validation is on or off, so turning validation on never changes how a message parses. A frame with no answering dictionary uses the session dictionary (owner).
- Corrections from research, no question asked: the C API also needs a `validate_inbound_messages` setter (FR-016); sessions are built per registered entry, not per connection (NFR-003, SC-007); FR-008 narrows to the header. From the probe: a FIX 4.4 table rejects a FIXT Logon on tag 1137, so "defines the admin messages" also requires that its Logon accepts 1137 (FR-003).

### Session 2026-11-03 (Gate A round 1)

Measured answers are in research R-9. Items marked **owner** are recommendations in `plan.md`'s Owner sign-off, not decisions.

- Q: Where does a validating FIXT session's 1137 check run, given the validate gate answers a Logon first and does not disconnect? → A: Ahead of the gate, in both establishment arms, as QuickFIX C++ and J read 1137 before they validate; each input then has one ending on every shape and role. **Owner** (FR-010).
- Q: What answers a 1128 past the header, when no validator step tests position (measured: it passes)? → A: A probe after the parse on an application route, ahead of the validator (FR-026).
- Q: What happens when a frame's last MsgType differs from its third field's, since routing and the validator read the first and dispatch reads the last? → A: A validating FIXT session rejects it before any dispatch, 373=13 when the second 35 is in the header and 373=14 past it, as QuickFIX C++ and Go. A non-validating session stays as today, disclosed (FR-025).
- Q: Who owns the shared tables and validators? → A: A route set in the session library, built once per Engine in `Engine::start()`. `dict::version_registry` cannot own a validator (`[arch §2.3]`). This replaces the lock-free publish and restates NFR-003 and SC-007 in copies held, not only walks.
- Q: Does a refused validating FIXT config surface at registration too, since the Engine's role loops discard an `open()` failure? → A: Yes: `Engine::register_session` and `Session::open()` share one check. This adds to ruling 2's wording. **Owner** (FR-004).
- Q: Does an overlay header tag on S pass on admin messages in shape A, where the transport dictionary is not S? → A: Yes. S is the envelope on the transport route too, or a venue header tag stops the Logon (measured, R-6) (FR-008).
- Q: May a stale negotiated version select a table after a reconnect? → A: No. Selection reads a routing value both arms write on every Logon, `Unknown` when the initiator's reply has nothing it can resolve. `negotiated_appl_version_` and `negotiated_version_profile()` keep today's writes, so validation-off observables do not move (FR-027).
- Q: What does a table build failure do? → A: `out_of_memory`, from `Engine::start()` or a directly constructed session's `open()`, before any observable effect; never a route with a hole (FR-028).
- Corrections, no question asked: NFR-001's branch count is restated with its witness; FR-024 misattributed Session Default, which is S-025; an A shape needs S's base in the engine dictionaries.

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

**Independent Test**: Frames with an unknown 1128, a known 1128 with no dictionary, a 1128 past the header, a repeated
MsgType, and a Logon with an unserviceable 1137 in either role, on validating and non-validating sessions.

**Acceptance Scenarios**:

1. **Given** a validating session, **When** a message carries `1128=99` or `1128=abc`, **Then** Reject 373=5, 371=1128,
   not delivered, session up, and the sequence number is consumed as for any validation Reject.
2. **Given** a validating session with no FIX 4.2 dictionary, **When** a message carries `1128=4`, **Then** the same.
3. **Given** a validating FIXT session of either role, **When** the peer's Logon has a 1137 that is absent, empty, unparseable
   or names a version with no dictionary, **Then** Reject 371=1137 (373=1 when absent, 373=5 otherwise) and a disconnect,
   on every shape.
4. **Given** a non-validating session, **When** any of those frames arrives, **Then** behaviour is as today: 1128 is
   tolerated, the message is delivered and the initiator records what it can (033 FR-004a).
5. **Given** a validating FIXT session with no transport dictionary available, **When** `Engine::register_session` or a
   directly constructed session's `open()` runs, **Then** it returns `invalid_session_config` before any observable effect,
   and the C and Python surfaces report it to the caller (`contracts/surface.md`).
6. **Given** a validating session, **When** an application frame's first 1128 is past the header, **Then** Reject 373=14,
   371=1128, not delivered, session up.
7. **Given** a validating session, **When** a frame's last MsgType differs from its third field's, **Then** Reject 371=35
   (373=13 with the second 35 in the header, 373=14 past it), not delivered under either type, session up.

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

### Edge Cases

- **Non-FIXT session.** Byte-identical to today, including that a FIX.4.4 session's strict validation rejects a frame
  carrying 1128.
- **1128 on an admin message.** Ignored for dictionary selection, as in QuickFIX C++ and J; the transport dictionary
  judges the field. `FIXT11.xml`'s 1128 enum stops at 9, so `1128=10` on a Logon draws 373=5 from that dictionary. An
  application message with `1128=10` routes to FIX Latest, as QuickFIX/J (whose FIXT11.xml has 10); QuickFIX C++'s enum stops
  at 9, so it rejects 10 on every message. Disclosed, not changed.
- **1128 past the header.** Routing reads 1128 from the header only. A validating session answers an application frame whose first
  1128 is past the header with Reject 373=14, 371=1128, whatever its value, so a delivered validated message has one answer for routing
  and `dict::reify`. A non-validating session routes it by the negotiated version and delivers it (research R-5). On an admin frame the
  transport dictionary judges it, as any field.
- **Two 1128s in the header.** Both routing and `dict::reify` read the first. The validator has no repeated-tag rule on any session, which
  QuickFIX does; this feature does not add one.
- **MsgType repeated with a different value.** Validating FIXT: Reject before dispatch, including when the last value is `3` or `5`, which
  the gate exempts. Non-validating: as today, routed by the third field, dispatched by the last, disclosed (FR-025). A repeat of the same
  value splits nothing and stays as today.
- **Empty `1128=`.** Present, never read as absent. Validating: Reject 373=4, 371=1128, as QuickFIX. Non-validating: the session dictionary.
- **Validation off, no dictionary for the version, or an unknown 1128.** The view uses the session dictionary, as today.
- **Resend and PossDup paths.** Every path that validates or dispatches an application frame uses the same selection.
- **Application message before Logon completes.** On a FIXT session the establishment arms validate admin frames only, so the
  frame disconnects whatever the session dictionary, one ending (research R-8, owner sign-off).
- **Session dictionary is FIX 4.4 on a FIXT session.** Its own version is 4.4. Its Logon does not accept 1137, so it is not
  a transport dictionary: a validating session needs `FIXT11.xml` in the engine dictionaries or `open()` refuses it. A
  non-validating session is not refused. Existing test shapes use a partial FIX 4.4 dictionary with 1137 on its Logon: with validation off they
  must keep passing; with validation on they fail FR-003 and are refused, and those tests change (research R-17).
- **A validating FIXT session on a partial session dictionary.** Refused unless a transport dictionary is registered, though it worked before for the
  messages it declares (B-row, research R-17).
- **Several sessions on one engine.** Dictionary tables and validators are shared: the cost must not be sessions times dictionaries
  (NFR-003).
- **A header Length+Data pair an overlay adds to S, on a frame routed to another table.** That table does not know the pair: the scan and
  the validator split inside the Data. Measured: a `34=7` inside the Data is read as MsgSeqNum. Disclosed as an L-row, on the owner's list.
- **Message Type Default.** Logon's 372/1130/1410 do not affect resolution. Disclosed (FR-024).

## Requirements *(mandatory)*

### Functional Requirements

**Resolution and selection**

- **FR-001**: On a FIXT session, the resolved application version of an application message MUST be 1128 if present, else
  the negotiated version, using the same resolver `dict::reify` uses, so the version validated and the version reified
  cannot differ.
- **FR-002**: The application dictionary for a resolved version MUST be the session dictionary when the version is its own
  version; otherwise the engine dictionary for that version, including the registry's one-way FIX50SP2 → FIX Latest
  fallback; and when that answer is a dictionary of the session dictionary's own version, the session dictionary instead.
- **FR-003**: The transport dictionary MUST be the session dictionary if it defines all seven admin messages and its Logon
  accepts `DefaultApplVerID(1137)`, else `FIXT11.xml` from the engine dictionaries. The check MUST be made on the dictionary's
  content, not its version label, so a partial dictionary or a FIX 4.x table fails it.
- **FR-004**: A validating FIXT session with no transport dictionary MUST fail `open()` with `invalid_session_config`, and
  `Engine::register_session` MUST refuse the same config through the same check (owner sign-off), so a host on an Engine route is
  told. A non-validating session MUST NOT be refused for this.
- **FR-005**: Admin messages MUST use the transport dictionary and application messages the application dictionary, on
  every inbound path and FSM arm that validates or dispatches a frame.

**Validation**

- **FR-006**: An application message MUST be validated against its application dictionary. All existing validator
  checks and the Reject mapping apply unchanged.
- **FR-007**: On a FIXT session the FIXT standard header and trailer tags MUST be accepted and type-checked on an
  application message whatever its application dictionary, FIX 4.0–4.4 included. A non-FIXT session's validation of the
  same dictionaries MUST NOT change.
- **FR-008**: A header tag the session dictionary declares beyond the standard ones MUST be accepted on an application message and
  checked against the session dictionary, enum and type included. The same holds on an admin message when the transport dictionary
  is not the session dictionary. The trailer equivalent is disclosed as a limitation (research R-6).
- **FR-009**: A validating session MUST answer an unknown 1128 value, or a valid value with no application dictionary,
  with Reject 373=5, 371=1128, no `fromApp`, and the session stays up. Sequence numbers follow the existing validation
  Reject rule (fixpp#423).
- **FR-010**: A validating FIXT session of either role MUST answer a Logon whose 1137 is absent (373=1), empty, unparseable or has
  no dictionary (373=5) with Reject 371=1137 and a disconnect. The check MUST run ahead of the validate gate, so the ending is the same
  on every shape (owner sign-off). The negotiated version is still written where it is today, after `interpret_logon`.
- **FR-011**: A non-validating FIXT session MUST keep today's dispositions for FR-009 and FR-010's inputs (033 FR-004a, FR-010).

**Parse and dispatch**

- **FR-012**: The frame scan, the validator's parse and walk, and the parse that builds the view handed to `fromApp` MUST
  use one dictionary per frame, the one FR-002 or FR-003 selected, with or without validation. The peek that selects it MUST walk with the
  session dictionary's Length+Data pairs and stop at the first tag that is not a header tag; a trailer tag is not one.
- **FR-013**: With validation off and a resolved version that is unknown or has no dictionary, the frame MUST be scanned and
  parsed with the session dictionary. Delivery is as today.
- **FR-014**: An application MUST be able to obtain the resolved application version of a delivered message, in C++, through the C
  API and from Python (the "resolved-app-version accessor" in S-026's scope). It MUST survive `fixpp_msg_clone` and `dict::reify` (data-model
  E-5). `fixpp_msg_version` is unchanged.
- **FR-015**: An outbound application message whose header carries 1128 MUST keep it, and a witness MUST pin that, so S-026's
  "explicit application version per message" holds in both directions.

**Surfaces**

- **FR-016**: The C API MUST add setters for `default_appl_ver_id` and `validate_inbound_messages` (validation is unreachable from C
  today) and a call that adds a dictionary to an engine config. All are additive, a C-ABI MINOR bump; Python follows through SWIG.
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
  its condition in research R-16 holds. S-026's row MUST state what the live cell proved, whichever way R-13 is ruled.
- **FR-021**: B-095-1, B-097-1, B-097-3, B-097-4, L-098-1, L-033-5 and the coverage-index rows for §4.3.7 and §5.3.x MUST be re-read and
  corrected where they state or depend on the old behaviour.
- **FR-022**: The QuickFIX C++ departure (ruling 1), and the dispositions FR-010, FR-025 and FR-026 add, MUST be recorded as B-rows with their
  reasons.
- **FR-023**: `brain/components/session.md` and `brain/components/dictionary.md` MUST record the two-dictionary decision
  and what was rejected; `brain/components/config.md`, `python-api.md` and `c-api.md` MUST record the surfaces this adds. Any file whose
  decision this supersedes MUST name the new one in a header comment.
- **FR-024**: The Message Type Default level MUST be recorded as a live L-row: not implemented, as in all three QuickFIX
  engines. `coverage-index.md` §5.3.4 MUST be re-pointed from S-026 to an explicit disposition naming that row, so it does not map to a `done`
  row it is not part of. S-026 closes on Explicit (§5.3.5) and §5.3.2; Session Default (§5.3.3) is S-025, already `done`.

**Added at Gate A**

- **FR-025**: A validating FIXT session MUST reject a frame whose last MsgType differs from its third field's, before any dispatch and ahead of
  the gate's 3/5 exemption: 371=35, 373=13 when the second 35 is in the header, 373=14 when past it, RefMsgType the third field's value.
- **FR-026**: A validating FIXT session MUST reject an application frame whose first 1128 is past the header with 373=14, 371=1128,
  whatever its value, ahead of the validator.
- **FR-027**: Table selection MUST read a per-connection value written on every Logon, `Unknown` when an initiator's reply carries nothing it
  can resolve, so a table is never selected from a previous connection's value. `negotiated_version_profile()` is unchanged.
- **FR-028**: A table or validator build failure MUST surface as `out_of_memory` from `Engine::start()`, or from a directly constructed
  session's `open()`, before any observable effect, distinct from a missing dictionary.

### Non-functional Requirements

- **NFR-001**: A non-FIXT session MUST gain no more than one branch on a non-validated inbound frame and two on a validated one (the FIXT
  check at arm entry; the pre-gate check), both on a value fixed at `open()`. Its inbound and validate paths MUST NOT regress beyond the
  paired-bench band (`[const §VIII.2]` as gated today). The bench is the witness; the branch count is a design statement checked at review.
- **NFR-002**: No per-message heap allocation on a FIXT session's inbound path, validating or not. A real-`Session` guard modelled on
  `tests/perf/test_session_recovery_alloc_guard.cpp` MUST cover a message routed to a dictionary other than the session's.
- **NFR-003**: On an Engine with a FIXT session, each distinct dictionary it uses (engine dictionaries and FIXT sessions' dictionaries) MUST be
  walked once, in `Engine::start()`, never per session, per message, or inside a coroutine. N sessions on M distinct dictionaries MUST hold M
  validators, each holding the one table for its dictionary, not N×M. A witness MUST count walks and heap, and go RED under a per-session
  copy. A directly constructed session builds its own set. A C-ABI session's outbound snapshot stays per session, as today.
- **NFR-004**: C ABI changes MUST be additive: ABI golden and symbol map updated, version bumped, existing symbols
  unchanged.
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
- **SC-003**: No validating session delivers a message it could not validate: every refusal input in User Story 3, on every shape, reaches
  `fromApp` zero times, with a counted callback, and the arm is shown able to report non-zero.
- **SC-004**: An overlay on the session dictionary behaves the same with and without other engine dictionaries registered.
- **SC-005**: Non-FIXT behaviour is unchanged: the existing test suite passes unmodified on every non-FIXT session test.
- **SC-006**: The same FIXT session reaches Active and routes `1128` from C++, C, Python and TOML.
- **SC-007**: N FIXT sessions on one Engine over M distinct dictionaries walk M times and hold M validators; an added session adds, beyond what a
  non-FIXT session with a minimal dictionary adds, less heap than one copy of the smallest application dictionary it routes to.

## Assumptions

- The header-required divergence from QuickFIX stays: fixpp's dictionary validator does not enforce header required-presence;
  the session FSM owns it (B-081-1). This feature changes which dictionary answers, not that decision.
- Reject wording for unknown MsgTypes and undefined tags stays as is; B85 (#655, #653) changes them later on this same validator.
- The vendored `FIXT11.xml` is not edited. FIX Latest's ApplVerID 10 on an application message is resolved before any
  dictionary is consulted.
- Multi-dictionary TOML covers the engine; per-session TOML keys are unchanged.
- FIXT session-dictionary overlays reach only that session (L-098-1): no feature here merges an overlay into engine dictionaries.

## Normative References

- **Spec refs** (`[const §VI.5]` form, titles as `spec/coverage-index.md` spells them):
  - `[FIX-SL §4.3.7] Specifying application version (DefaultApplVerID 1137 / FIXT)` (S-025, S-026);
  - `[FIX-SL §5.3] FIXT session profile` (S-025, S-026; informative);
  - `[FIX-SL §5.3.2] Multiple application version support` (S-026; informative);
  - `[FIX-SL §5.3.3] Session default application version identification (DefaultApplVerID 1137)` (S-025; informative);
  - `[FIX-SL §5.3.4] Message type default application version` (not implemented, FR-024; informative);
  - `[FIX-SL §5.3.5] Explicit application version per message (ApplVerID 1128)` (S-026; informative).
- **QuickFIX reference behaviour** (cited, not vendored): QuickFIX C++ `386ce46e` (`Session`, `Message`, `DataDictionary`), QuickFIX/J
  `9ac173b2a69ef2305258330d88685eccad80512d` (`Session`, `Message`, `DefaultDataDictionaryProvider`, `org.quickfixj.SimpleCache`, the MINA I/O
  handlers), QuickFIX/Go `eff945dd` (`validation.go`).
- **Catalogue rows**: S-026 (closes); S-025 (the negotiated version it reads); D-009 (the overlay it must keep).
- **Constitution**: `[const §VI.4]`–`[const §VI.6]`; `[const §VIII.2]`, `[const §VIII.3]`, `[const §VIII.5]`; `[const §X.1]`, `[const §X.5]`–`[const §X.7]`;
  `[const §XI.7]`; `[const §XIV.2]`; `[const §XV.1]`, `[const §XV.9]`, `[const §XV.13]`.
- **Design documents**: `[arch §2.3]`, `[arch §2.4]`; `[2b §6.5]`; `[2c §4.6]`, `[2c §4.9]`, `[2c §5]`; `[2i §3.10]`, `[2i §4.6]`. The notes they gain are in
  research R-11.
