# Feature Specification: FIX 4.4 closeout — session-negotiation fields + XMLnonFIX passthrough

**Feature Branch**: `070-fix44-closeout`

**Created**: 2026-07-12

**Status**: Draft

**Input**: User description: "FIX 4.4 closeout — implement the remaining unimplemented FIX 4.4 session-negotiation fields plus XMLnonFIX passthrough, closing catalogue rows S-029, S-030, S-037, and A-034."

## Clarifications

### Session 2026-07-12

- Q: S-029 — when posture enforcement is ON and an inbound Logon omits TestMessageIndicator(464), how is absence treated? → A: Absent = production (symmetric). `464=Y` ⇒ test; absent or `464=N` ⇒ production. A production-posture session rejects only `464=Y`; a test-posture session rejects absent/`464=N`. Blocks both cross-connect directions.
- Q: S-030 — which enforcement direction for the negotiated MaxMessageSize(383)? → A: Inbound-only hard enforce (disconnect a peer exceeding the size we advertised); capture the peer's advertised 383 for observability but no hard outbound block this feature. (Reference: QuickFIX-cpp implements neither enforcement nor advertisement — field defined, zero Session logic — so there is no interop pressure to build the outbound guard now.)
- Q: S-037 — source of the advertised supported-MsgTypes list? → A: Explicit operator-provided config list of (MsgDirection, MsgType) pairs; empty by default (no 384 group emitted). No auto-derivation from the dictionary. (Reference: QuickFIX-cpp does not advertise NoMsgTypes at all.)
- Q: A-034 — behavior of an inbound 35=n when opt-in dictionary validation is ENABLED? → A: The validator accepts a well-formed XMLnonFIX regardless of the `validate_inbound_messages` setting — correct passthrough in all configs.

## User Scenarios & Testing *(mandatory)*

This feature closes the last four open FIX 4.4 catalogue rows. Three are session-layer
negotiation fields exchanged in the Logon(A) handshake (per FIX Session Layer §4.3); the
fourth is a pass-through obligation for the XMLnonFIX (35=n) message. Each is an
independently testable slice: implementing any one alone delivers a usable, demonstrable
capability, and none depends on the others.

### User Story 1 - Refuse a test/production posture mismatch on Logon (Priority: P1)

An operator runs a session endpoint configured for a **production** posture. A counterparty
mistakenly connects a **test** endpoint that marks its Logon with TestMessageIndicator(464)=Y
(or the reverse: our test endpoint is contacted by a production peer). The engine must detect
the posture conflict during Logon and refuse the session rather than silently interoperating,
which would let test traffic reach production books (a real operational hazard).

**Why this priority**: Preventing a test/production cross-connect is a safety property — the
worst outcome (test orders hitting a production venue, or vice-versa) is materially damaging
and silent today. Highest value of the four.

**Independent Test**: Configure a session with `production` posture; feed an inbound Logon
carrying `464=Y`; assert the engine emits a rejection (Logout/Reject) and disconnects without
reaching the established state. Mirror with a `test`-posture session receiving a production
Logon. Fully testable at the session-FSM level with no other story implemented.

**Acceptance Scenarios**:

1. **Given** a session configured `posture=production`, **When** an inbound Logon(A) carries `TestMessageIndicator(464)=Y`, **Then** the engine refuses the session (does not reach established) and disconnects, surfacing a distinct "posture mismatch" reason.
2. **Given** a session configured `posture=test`, **When** an inbound Logon(A) carries `464=N` or omits 464 (production peer), **Then** the engine refuses the session and disconnects.
3. **Given** a session configured `posture=production`, **When** an inbound Logon(A) carries `464=N` or omits 464, **Then** the Logon proceeds normally (no false rejection).
4. **Given** posture enforcement is not configured (default), **When** any Logon arrives with or without 464, **Then** behavior is unchanged from today (no new rejection path fires) — the feature is opt-in.

---

### User Story 2 - Honor a negotiated maximum message size (Priority: P2)

Two endpoints negotiate the largest message each is willing to accept. Our endpoint advertises
its own MaxMessageSize(383) in the Logon it sends, and reads the peer's advertised 383 from the
peer's Logon. Thereafter, if the peer sends a message larger than the size **we advertised we
would accept**, the engine disconnects the session (the peer violated the negotiated contract).
This negotiated limit is separate from, and stricter-or-equal to, the pre-existing absolute
frame-size backstop.

**Why this priority**: A negotiated bound is a robustness/DoS property and a FIX-SL conformance
item, but the engine already has an absolute frame cap as a backstop, so the incremental safety
is smaller than Story 1.

**Independent Test**: Configure a session advertising `MaxMessageSize=N`; assert the outbound
Logon carries `383=N`; feed an inbound application message of length > N; assert the engine
disconnects citing a "negotiated max message size exceeded" reason. Testable with no other story.

**Acceptance Scenarios**:

1. **Given** a session configured to advertise `MaxMessageSize=N`, **When** it sends its Logon, **Then** the Logon carries `383=N`.
2. **Given** an established session that advertised `383=N`, **When** the peer sends a message whose total length exceeds `N`, **Then** the engine disconnects the session and surfaces a "negotiated max message size exceeded" reason.
3. **Given** an established session that advertised `383=N`, **When** the peer sends messages all ≤ `N`, **Then** no negotiated-size disconnect fires.
4. **Given** MaxMessageSize advertisement is not configured (default), **When** the session logs on, **Then** no `383` is emitted and no negotiated-size enforcement occurs (the absolute frame backstop is unchanged).
5. **Given** the peer advertises its own `383=M` in its Logon, **When** we read it, **Then** the value is captured/observable (available to the outbound-guard behavior; see Assumptions on outbound scope).

---

### User Story 3 - Advertise supported message types in Logon (Priority: P2)

An endpoint tells its counterparty which message types it supports and in which direction, by
including the NoMsgTypes(384) repeating group (RefMsgType(372) + MsgDirection(385) members, in
that dictionary order) in the Logon(A) it sends. The supported set comes from an explicit configuration source. This lets
a counterparty tailor what it sends.

**Why this priority**: An interop nicety improving negotiation transparency; inbound parsing of
the group is already tolerated, so only the advertise side is missing and nothing breaks without it.

**Independent Test**: Configure a supported-type list `[(send, "D"), (receive, "8"), …]` (typed
`msg_direction` enum + MsgType string); assert the outbound Logon contains a well-formed
`NoMsgTypes(384)=k` group with matching `372`/`385` member pairs (RefMsgType then MsgDirection,
per dictionary order) in configuration order. Testable with no other story.

**Acceptance Scenarios**:

1. **Given** a configured supported-type list of k entries, **When** the session sends its Logon, **Then** the Logon carries `NoMsgTypes(384)=k` followed by k well-formed `372`/`385` member pairs (RefMsgType then MsgDirection, per dictionary order) in configuration order.
2. **Given** no supported-type list is configured (default), **When** the session sends its Logon, **Then** no `384` group is emitted (behavior unchanged from today).
3. **Given** a configured list, **When** the outbound Logon is parsed back, **Then** every advertised `(MsgDirection, RefMsgType)` pair round-trips exactly.

---

### User Story 4 - Deliver an XMLnonFIX (35=n) payload intact to the application (Priority: P3)

A counterparty sends an XMLnonFIX (35=n) message carrying an XML document in the
XmlDataLen(212)/XmlData(213) length-delimited field pair, where the XML payload itself contains
raw SOH (0x01) and '=' bytes. The engine must deliver the message to the application with the
XML payload byte-exact — treated as an opaque application message, neither rejected nor routed as
an admin message.

**Why this priority**: The engine already parses the 212/213 length-delimited pair SOH-safely
and already routes 35=n to the application path; this story pins that behavior with a
discriminating test and resolves the one open edge (behavior under opt-in dictionary validation).
Lowest incremental risk.

**Independent Test**: Feed an inbound 35=n frame whose `XmlData(213)` contains embedded SOH bytes;
assert it is delivered on the application-message callback (not the admin path, not rejected) and
that reading tag 213 returns the exact original byte sequence including the embedded SOH. Testable
with no other story.

**Acceptance Scenarios**:

1. **Given** an established session, **When** an inbound 35=n arrives with `212=len`/`213=<xml with embedded SOH>`, **Then** it is delivered to the application-message callback and tag 213 reads back byte-exact (embedded SOH preserved).
2. **Given** an inbound 35=n, **When** it is dispatched, **Then** it is NOT delivered on the admin-message callback and NOT rejected.
3. **Given** opt-in dictionary validation is enabled, **When** a well-formed 35=n arrives, **Then** it is accepted (not rejected by the validator) OR the assumed-off contract is explicitly documented and asserted (see Assumptions).

---

### Edge Cases

- **S-029**: 464 present with a non-empty value not in `{Y, N}` → rejected as malformed by an **explicit value check on the Logon posture path**. Tag 464 is newly scanned by this feature and is not validated anywhere today, so there is no pre-existing malformed-header validator to inherit — the value check is added here. An empty (`464=`) or absent 464 is **not** malformed: both map to peer-production under the symmetric rule (empty ≡ absent is safe precisely because the clarification already accepts "absent ⇒ production", so an empty-present 464 introduces no hazard beyond the accepted risk model — no presence bit is required).
- **S-029**: posture enforcement off (default) must be a strict superset-compatible no-op — no regression to any existing Logon test.
- **S-030**: a message exactly equal to the negotiated size N is accepted; N+1 disconnects (boundary).
- **S-030**: a configured `383=N` MAY exceed the absolute frame backstop — no clamp of N is applied. The two limits compose by **min-semantics**: the effective inbound cap is `min(N, max_frame_bytes)`. A frame larger than the backstop is already rejected at framing (before the negotiated check ever runs), so the negotiated bound only ever *tightens*; N > backstop simply means the backstop governs first.
- **S-030**: peer advertises no 383 → no outbound-guard obligation toward that peer.
- **S-037**: empty configured list → group omitted entirely (no `384=0`).
- **S-037**: the advertised group must not violate the existing Logon builder's zero-alloc / bounded-buffer discipline for large k.
- **A-034**: 35=n with `212` length not matching the actual `213` byte count → handled by the existing length-delimited-field guard (rejected as malformed), not silently truncated.
- **A-034**: 35=n arriving before the session is established → follows the existing pre-established application-message disposition (no new special case).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The engine MUST support a per-session **session posture** setting with values {production, test} plus an unset/disabled default; when unset, no posture enforcement occurs.
- **FR-002**: On an inbound Logon(A), when posture enforcement is enabled, the engine MUST evaluate TestMessageIndicator(464) against the local posture and MUST refuse the session (reject/logout + disconnect, never reaching established) on a mismatch, surfacing a distinct posture-mismatch reason.
- **FR-003**: When posture enforcement is enabled and the peer's posture matches, the Logon MUST proceed exactly as it does today.
- **FR-004**: The engine MUST support a per-session **advertised MaxMessageSize** setting; when set, the outbound Logon MUST carry MaxMessageSize(383) with that value; when unset (default), no 383 is emitted.
- **FR-005**: When an advertised MaxMessageSize N is set, the engine MUST disconnect an established session if the peer transmits a message whose total on-wire length exceeds N, surfacing a distinct negotiated-max-exceeded reason.
- **FR-006**: The negotiated MaxMessageSize enforcement MUST be independent of and never weaken the pre-existing absolute frame-size backstop; the absolute backstop remains in force unchanged.
- **FR-007**: On an inbound Logon(A), the engine MUST read the peer's advertised MaxMessageSize(383) when present and make it available to session state (for the outbound-respect behavior scoped in Assumptions).
- **FR-008**: The engine MUST support a per-session **supported-MsgTypes advertisement** list of (MsgDirection, MsgType) entries; when non-empty, the outbound Logon(A) MUST include a well-formed NoMsgTypes(384) group whose members (RefMsgType(372) + MsgDirection(385), in that dictionary delimiter order) reflect the list in order; when empty (default), no 384 group is emitted.
- **FR-009**: An inbound 35=n (XMLnonFIX) message MUST be delivered to the application-message callback (not the admin callback) with all fields — including the length-delimited XmlData(213) — byte-exact, preserving any embedded SOH/'=' bytes in the payload.
- **FR-010**: An inbound 35=n MUST NOT be rejected on the default (validation-off) inbound path.
- **FR-011**: An inbound well-formed 35=n (XMLnonFIX) MUST be accepted by the inbound dictionary validator when opt-in validation is enabled — i.e. the validator MUST NOT reject XMLnonFIX regardless of the `validate_inbound_messages` setting. A discriminating test MUST exercise the validation-enabled path.
- **FR-012**: All four capabilities MUST be opt-in / additive: with none of the new settings configured, engine behavior MUST be byte-for-byte and disposition-for-disposition identical to the pre-feature baseline (no regression to existing session/wire tests).
- **FR-013**: The public/C-ABI surface MUST NOT change (C-ABI is GA-frozen at 1.5.0); new configuration is exposed only through the existing C++ session-config surface.
- **FR-014**: Each capability MUST ship with a discriminating, red-provable test that fails against the pre-feature code and passes after.

### Key Entities *(include if feature involves data)*

- **Session posture**: a per-session enumeration {production, test, unset}. Drives S-029 enforcement; default unset (disabled).
- **Advertised MaxMessageSize**: a per-session non-negative size (bytes) our endpoint advertises and enforces on inbound; default unset. Distinct from the absolute frame backstop.
- **Peer MaxMessageSize**: the peer's advertised 383 captured from its Logon; informs the outbound-respect behavior (scope in Assumptions).
- **Supported-MsgTypes advertisement**: an ordered list of (direction, MsgType) pairs advertised via NoMsgTypes(384), where `direction` is a typed `msg_direction` enum {send, receive} rendering to the FIX44 CHAR domain {'S','R'} on the wire; default empty.
- **XMLnonFIX message**: an application (fromApp) message whose XML payload rides in XmlDataLen(212)/XmlData(213); delivered opaque.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of the four catalogue rows (S-029, S-030, S-037, A-034) move to a `done`-eligible state, each backed by ≥1 discriminating test that is red before the change and green after.
- **SC-002**: A production-posture session receiving a test-marked Logon is refused in 100% of runs (no established state reached), and a matched-posture Logon proceeds in 100% of runs (zero false rejections across the existing Logon test corpus).
- **SC-003**: A session advertising MaxMessageSize=N disconnects on the first inbound message exceeding N and never disconnects for messages ≤ N (boundary N vs N+1 both asserted).
- **SC-004**: An advertised supported-MsgTypes list of k entries produces a Logon whose parsed-back NoMsgTypes group has exactly k members equal to the configured pairs in order (exact-set, not subset).
- **SC-005**: An inbound 35=n with embedded-SOH XmlData is delivered on the application callback with tag 213 byte-exact in 100% of runs; never on the admin callback; never rejected on the default path.
- **SC-006**: With none of the new settings configured, the full existing session + wire test suites pass unchanged (zero regressions), and no new admin/reject/disconnect path fires.

## Assumptions

- **S-029 conflict rule** (resolved — see Clarifications): "Mismatch" means the peer's test/production indication (464=Y ⇒ test; 464=N or absent ⇒ production) differs from the locally configured posture. Symmetric — a production-posture session rejects only 464=Y; a test-posture session rejects absent/464=N.
- **S-030 enforcement direction** (resolved — see Clarifications): The MUST-enforced behavior is **inbound-only** (disconnect a peer exceeding the size *we* advertised). The peer's advertised 383 is captured as observable state (FR-007) but there is **no hard outbound guard** in this feature. Reference QuickFIX-cpp implements neither, so there is no interop pressure to add the outbound guard now.
- **S-030 vs backstop**: The absolute `max_frame_bytes` frame cap is retained unchanged as the outer backstop; the negotiated limit is an inner, per-session bound.
- **S-037 source** (resolved — see Clarifications): The supported-MsgTypes set is an explicit operator-provided configuration list, not auto-derived from the loaded dictionary. Auto-derivation is out of scope. Reference QuickFIX-cpp does not advertise NoMsgTypes at all.
- **A-034 validation-enabled** (resolved — see Clarifications): 35=n passes through on the default validation-off path; additionally, per FR-011, the validator MUST accept a well-formed XMLnonFIX when validation is enabled (no rejection in any config).
- **Scope boundaries**: S-032 residual (standalone initiator-driven 141=Y mid-session origination) is explicitly deferred to its own later feature; v50sp2 typed codegen, C-ABI changes, and Python bindings are out of scope.
- **Dependencies**: reuses the existing Logon builder (`build_logon`), inbound-Logon handler, header scanner, framer/length-delimited-field parser, and the `fromApp` dispatch surface — all already present; no new subsystem is introduced.

## Normative References

Per constitution Article VI §5, the exact citations this feature closes (strings verified
against `spec/coverage-index.md`), split into coverage-index section refs and dictionary anchors:

**Coverage-index references** (canonical `[DocAbbrev §X.Y.Z] Title` strings, mirrored exactly
from `spec/coverage-index.md`):

- **S-029** — `[FIX-SL §4.3.2] Using the TestMessageIndicator(464)` (`spec/coverage-index.md:44`).
- **S-030** — `[FIX-SL §4.3.6] Maximum message size (MaxMessageSize 383)` (`spec/coverage-index.md:51`).
- **S-037** — `[FIX-SL §4.3.8] Specifying supported message types (NoMsgTypes in Logon)` (`spec/coverage-index.md:53`).

**Dictionary references** (message-level `[FIX44]` DocAbbrev + MsgType granularity, per the
convention documented at `spec/coverage-index.md:268-276`; NOT a coverage-index FIX-SL section ref):

- **A-034** — `[FIX44] XMLnonFIX (MsgType n)` — the FIX44 application-dictionary message definition
  (`dictionaries/FIX44.xml:1008`, `msgtype='n' msgcat='admin'`; XmlDataLen(212)/XmlData(213) resident
  in `<header>`). Dictionary anchor only; catalogue row at `spec/coverage-index.md:373`.
