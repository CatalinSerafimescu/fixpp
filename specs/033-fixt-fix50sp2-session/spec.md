# Feature Specification: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

**Feature Branch**: `033-fixt-fix50sp2-session`  
**Created**: 2026-06-12  
**Status**: Draft  
**Input**: User description: "G4a — FIXT.1.1 / FIX 5.0 SP2 session establishment. Enable fixpp to establish a FIXT.1.1 transport session carrying FIX 5.0 SP2 application messages, decoupling the transport (session) version from the application version. Scope: BeginString split (8=FIXT.1.1), DefaultApplVerID(1137) on the FIXT Logon, Username(553)/Password(554) optional Logon auth, un-defer the HP-fixt11-fix50sp2-cells interop cell driven live vs QuickFIX-cpp + QuickFIX-J both roles. Build on the existing dictionary version layer + 005 session FSM; must not regress FIX.4.2/4.4. Open: whether ApplVerID(1128) per-application-message (S-026) is in scope or deferred."

## Overview

FIX 5.0 SP2 introduced a **transport/application version split**: the wire `BeginString(8)` advertises the *session* (transport) protocol `FIXT.1.1`, while the *application* protocol version (e.g. `FIX.5.0SP2`) is negotiated separately on the Logon via `DefaultApplVerID(1137)`. Today fixpp speaks only the FIX.4.x family, where `BeginString` carries both at once (e.g. `8=FIX.4.4`). The FIXT.1.1/5.0SP2 half of session establishment is currently **deferred-with-traceability** (catalogue S-020 FIXT half; S-025 `DefaultApplVerID` backlog), and the corresponding live interop cell is parked as `deferred:fixt-routing`.

This feature enables an operator to run a fixpp session over `FIXT.1.1` carrying `FIX.5.0SP2` application messages, as both initiator and acceptor, interoperating with conformant counterparties — without disturbing existing FIX.4.x sessions.

## Clarifications

### Session 2026-06-12

- Q: Is per-message `ApplVerID(1128)` routing (switching the application dictionary mid-session, per message; catalogue S-026) in scope, or deferred? → A: Deferred. Session-wide `DefaultApplVerID(1137)` only; inbound `1128` is tolerated without parse failure but never switches dictionaries mid-session. Per-message routing is a follow-on feature.
- Q: How should fixpp (acceptor) respond to an inbound FIXT Logon that omits `DefaultApplVerID(1137)`? → A: Session-level `Reject(35=3)` with `SessionRejectReason=RequiredTagMissing`, no establishment — matching QuickFIX-cpp/J, which treat `1137` as a required FIXT field.
- Q: Which application version(s) must 033 establish? → A: The design MUST support **any** `ApplVerID` by configuration (no `FIX.5.0SP2` hardcoding), including FIXT.1.1 carrying a FIX.4.x application version.
- Q: How far does "any ApplVerID" extend — design generality vs live-test coverage? → A: General design + live conformance cells for **FIX.5.0SP2** AND one representative **FIXT.1.1-carrying-FIX.4.4** session. Other versions are establishable by config but not each separately live-celled.
- Q: For `Username(553)`/`Password(554)`, what acceptor-side behaviour is in scope? → A: Parse inbound `553`/`554` and surface them to the existing authorization path; no NEW credential-validation policy in this feature, and credential-free Logon still establishes. Credential **validation/rejection** is a committed future feature (gated behind a config knob) — recorded as a forward obligation, not implemented here.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Establish a FIXT.1.1 / FIX 5.0 SP2 session (Priority: P1)

An operator configures a fixpp session for the FIX 5.0 SP2 application protocol. As an **initiator**, fixpp connects to a 5.0 SP2 counterparty, sends a `FIXT.1.1` Logon advertising the agreed application version, and reaches an established (Active) session. As an **acceptor**, fixpp admits an incoming `FIXT.1.1` Logon that advertises a supported application version and establishes the session. Once established, every wire message carries `BeginString=FIXT.1.1` and application messages are interpreted against the negotiated application dictionary.

**Why this priority**: This is the irreducible MVP. Without `8=FIXT.1.1` handling and `DefaultApplVerID(1137)` negotiation, no FIXT session can be established at all and every other story is unreachable. It is the single capability that converts the deferred axis into a working session.

**Independent Test**: Configure a fixpp initiator for FIX 5.0 SP2 against a counterparty configured for FIXT.1.1/FIX.5.0SP2; observe the session reach Active with `8=FIXT.1.1` on the Logon and `1137` advertising the agreed application version. Repeat with fixpp as acceptor. Delivers value standalone: a usable 5.0 SP2 session.

**Acceptance Scenarios**:

1. **Given** a fixpp initiator configured for FIX 5.0 SP2, **When** it connects to a conformant FIXT.1.1/5.0SP2 acceptor, **Then** it sends a Logon with `8=FIXT.1.1` and `1137` set to the agreed application version and the session reaches Active.
2. **Given** a fixpp acceptor configured to support FIX 5.0 SP2, **When** a counterparty initiator sends a `FIXT.1.1` Logon advertising `1137` = a supported application version, **Then** fixpp admits the Logon and the session reaches Active.
3. **Given** an established FIXT.1.1 session, **When** either side exchanges subsequent admin or application messages, **Then** every message carries `BeginString=FIXT.1.1` and application messages are parsed/validated against the negotiated application dictionary.
4. **Given** a fixpp acceptor, **When** an incoming `FIXT.1.1` Logon omits `DefaultApplVerID(1137)`, **Then** fixpp responds with `Reject(35=3)` (RequiredTagMissing) and does not establish; and **When** the Logon advertises an application version fixpp cannot service, **Then** fixpp refuses establishment conformantly rather than establishing a mis-versioned session.

---

### User Story 2 - Optional credentialed Logon (Priority: P2)

An operator deploys against a counterparty that requires application-level credentials. fixpp's FIXT Logon optionally carries `Username(553)` and `Password(554)`; an acceptor-side fixpp optionally surfaces inbound credentials for the deployment's authorization decision.

**Why this priority**: Common real-world requirement for 5.0 SP2 venues, but orthogonal to establishment itself — a session can be established without credentials, so this is a separable increment on top of P1.

**Independent Test**: Configure a fixpp initiator with a username/password; observe `553`/`554` present on the outbound Logon and the counterparty accepting it. As acceptor, confirm inbound `553`/`554` are parsed and made available to the existing authorization path without breaking establishment when absent.

**Acceptance Scenarios**:

1. **Given** a fixpp initiator configured with credentials, **When** it sends its FIXT Logon, **Then** the Logon carries `Username(553)` and `Password(554)` with the configured values.
2. **Given** a fixpp initiator with no credentials configured, **When** it sends its FIXT Logon, **Then** `553`/`554` are absent and establishment is unaffected.
3. **Given** a fixpp acceptor, **When** an inbound Logon carries `553`/`554`, **Then** the values are available to the deployment's authorization decision and a clean Logon (no credentials) still establishes when the deployment does not require them.

---

### User Story 3 - Live interop conformance against reference engines (Priority: P3)

The FIXT.1.1/5.0SP2 establishment behaviour is validated live against the QuickFIX-cpp and QuickFIX-J reference engines, both roles, replacing the parked `deferred:fixt-routing` matrix cell with a passing live cell.

**Why this priority**: Conformance proof is the point of the interop ladder, but it depends on P1 (and optionally P2) existing first; it adds confidence rather than capability.

**Independent Test**: Run the previously-deferred FIXT interop cells against each reference engine as both initiator and acceptor — one for FIX.5.0SP2 and one for FIXT.1.1-carrying-FIX.4.4 — each establishes its session and matches a banked golden.

**Acceptance Scenarios**:

1. **Given** the reference engines configured for FIXT.1.1/FIX.5.0SP2, **When** the interop cell runs with fixpp as initiator and as acceptor against each engine, **Then** every variant establishes a session and passes its golden comparison.
2. **Given** the reference engines configured for FIXT.1.1 carrying FIX.4.4, **When** the FIXT-4.4 interop cell runs both roles against each engine, **Then** every variant establishes and passes its golden — demonstrating transport/application decoupling live.
3. **Given** the matrix manifest, **When** the feature completes, **Then** the FIXT axis is no longer `deferred:fixt-routing` but carries live dispositions with real statuses.

---

### Edge Cases

- **Peer declares a different application version than fixpp's own default**: this is *normal* FIXT behaviour — each side advertises its own `DefaultApplVerID`; fixpp records the peer's declared version for inbound application messages (it does NOT reject merely because the peer's default differs from its own).
- **Unsupported application version**: the peer-advertised `ApplVerID` has no application dictionary fixpp can service → refuse establishment conformantly (FR-004a); do not silently downgrade or establish mis-versioned.
- **Missing `1137` on a FIXT Logon**: an inbound `8=FIXT.1.1` Logon omits `DefaultApplVerID` → session-level `Reject(35=3)` RequiredTagMissing (FR-004), not establishment, rather than guessing a default.
- **Wrong-family BeginString**: a peer sends `8=FIX.4.4` to a fixpp session configured for FIXT.1.1 (or vice-versa) → handled by the existing BeginString version-gating without establishing a cross-family session.
- **Inbound per-message `ApplVerID(1128)`** on an application message of an established session → tolerated without parse failure even if per-message routing is out of scope (see Assumptions).
- **Credentials present but unwanted**, or required but absent (acceptor) → the deployment's authorization decision governs; establishment plumbing must not crash or leak the password into logs/transcripts.
- **Regression guard**: an existing `8=FIX.4.2`/`8=FIX.4.4` session must establish and operate byte-for-byte as before.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST accept and emit `BeginString(8)=FIXT.1.1` as the transport/session protocol version on every message of a FIXT-configured session (admin and application), distinct from the application protocol version.
- **FR-002**: The system MUST, for a FIXT-configured session, include `DefaultApplVerID(1137)` set to the configured application version (e.g. FIX.5.0 SP2) on **every** outbound Logon — both an initiator's Logon and an acceptor's Logon response (each side advertises its own default application version, per the reference-engine convention).
- **FR-003**: The system MUST, on receiving an inbound FIXT Logon, read the peer's `DefaultApplVerID(1137)` and use the indicated application version as the application dictionary for inbound application messages for the duration of the session.
- **FR-004**: The system MUST, as acceptor, respond to an inbound FIXT Logon that omits `DefaultApplVerID(1137)` with a session-level `Reject(35=3)` carrying `SessionRejectReason = RequiredTagMissing`, and MUST NOT establish (reach Active) — matching the reference engines, which treat `1137` as a required FIXT field.
- **FR-004a**: The system MUST NOT reach an established (Active) session whose negotiated application version it cannot service (no application dictionary available for the peer-advertised `ApplVerID`); it MUST refuse establishment conformantly rather than silently downgrade or proceed mis-versioned.
- **FR-005**: The system MUST interpret application messages on an established FIXT session against the negotiated application version's dictionary, while interpreting session/admin messages against the FIXT.1.1 session layer.
- **FR-006**: The system MUST allow an operator to configure, per session, that it speaks FIXT.1.1 with a named default application version, independently of the existing FIX.4.x configuration path. The configuration mechanism MUST accept **any** `ApplVerID` value (no `FIX.5.0SP2` hardcoding), including FIXT.1.1 carrying a FIX.4.x application version; application-dictionary selection MUST resolve from the configured/negotiated `ApplVerID` via the existing version layer.
- **FR-007**: The system MUST optionally include `Username(553)` and `Password(554)` on the outbound FIXT Logon when credentials are configured, and MUST omit them when not configured.
- **FR-008**: The system MUST, as acceptor, parse inbound `Username(553)`/`Password(554)` when present and make them available to the existing authorization decision path, while still permitting credential-free establishment when the deployment does not require them. This feature introduces **no new** credential-validation policy; it only parses and surfaces.
- **FR-008a**: The system MUST be designed so that a future feature can add acceptor-side credential **validation/rejection** gated behind a configuration knob (default off / current behaviour) — i.e. the authorization seam that receives the surfaced credentials (FR-008) MUST be the natural place to add reject-on-invalid later, without re-plumbing establishment. *(Forward obligation only; validation itself is out of scope for 033.)*
- **FR-009**: The system MUST NOT regress existing FIX.4.2/FIX.4.4 session establishment or steady-state behaviour; the FIX.4.x wire path MUST remain unchanged (byte-identical) when FIXT is not configured.
- **FR-010**: The system MUST tolerate (parse without failure) an inbound per-message `ApplVerID(1128)` on application messages of an established FIXT session; per-message application-version routing (catalogue S-026) is out of scope and `1128` MUST NOT switch the application dictionary mid-session.
- **FR-011**: The system MUST NOT emit a populated `Password(554)` value in clear text into any persisted log, transcript, or golden artifact (redact/elide the value).
- **FR-012**: The system MUST update the interop matrix manifest so the FIXT.1.1/5.0SP2 cell is no longer deferred and reflects its real live status, and MUST bank goldens for the live cell consistent with the existing harness conventions.

### Key Entities *(include if feature involves data)*

- **Transport (session) version**: the protocol governing the session layer — `FIXT.1.1` — carried on the wire `BeginString(8)`. Determines header/trailer and admin-message handling.
- **Application version**: the protocol governing business messages — e.g. `FIX.5.0SP2` — identified by an `ApplVerID` enumeration. Conveyed session-wide via `DefaultApplVerID(1137)` on Logon and (optionally) per message via `ApplVerID(1128)`. Selects the application dictionary used to parse/validate application messages.
- **FIXT Logon**: the establishment message; for FIXT it additionally carries the mandatory `DefaultApplVerID(1137)` and optional `Username(553)`/`Password(554)`.
- **Session configuration**: the operator-facing description of a session, extended to express "FIXT.1.1 transport + a named default application version (+ optional credentials)".

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An operator can establish a FIXT.1.1 session negotiating FIX 5.0 SP2, in both initiator and acceptor roles, against a conformant counterparty, reaching an established session with no manual intervention.
- **SC-002**: 100% of existing FIX.4.2/FIX.4.4 establishment and steady-state behaviour is preserved — the FIX.4.x wire output is byte-identical to before this feature when FIXT is not configured (zero regression).
- **SC-003**: A FIXT Logon carrying configured credentials authenticates successfully against a credential-requiring counterparty, and a credential-free FIXT Logon establishes against a counterparty that does not require them.
- **SC-004**: The previously `deferred:fixt-routing` interop axis passes live against both reference engines (QuickFIX-cpp and QuickFIX-J), in both roles, with banked goldens — for a **FIX.5.0SP2** session AND a representative **FIXT.1.1-carrying-FIX.4.4** session — i.e. the FIXT axis of the parity matrix is no longer deferred and the transport/application decoupling is live-proven.
- **SC-005**: An inbound FIXT Logon that is missing `1137` (→ `Reject(35=3)` RequiredTagMissing) or advertises an application version fixpp cannot service never results in an established (Active) mis-versioned session.
- **SC-006**: Application-version selection is driven entirely by configured/negotiated `ApplVerID` through the existing version layer — demonstrated by establishing at least two distinct application versions (5.0SP2 and 4.4-over-FIXT) with no version-specific establishment code path.

## Assumptions

- **Per-message `ApplVerID(1128)` routing (catalogue S-026) is OUT of scope** (clarified 2026-06-12) and deferred to a follow-on. The feature negotiates a single session-wide default application version via `1137`; inbound per-message `1128` is tolerated (FR-010) but never switches dictionaries mid-session.
- **Application-version handling is general** (clarified 2026-06-12): the design supports any `ApplVerID` by configuration via the existing version layer (no `FIX.5.0SP2` hardcoding), including FIXT.1.1 carrying a FIX.4.x application version. **Live conformance validation covers FIX.5.0SP2 plus one representative FIXT.1.1-carrying-FIX.4.4 session**; other versions are establishable by config but are not each separately live-celled.
- **Credential validation is a committed future feature, not part of 033** (clarified 2026-06-12): this feature parses and surfaces `553`/`554` (FR-008) and leaves the authorization seam ready for a later config-gated validation/reject feature (FR-008a). No credential-validation policy ships here.
- The existing dictionary/version layer (version profiles, version registry, reify) already carries FIXT footprint and is the substrate for application-dictionary selection; this feature builds on it rather than introducing a parallel mechanism.
- The existing 005 session-establishment FSM and BeginString version-gating are extended, not replaced; the FIX.4.x establishment path is the regression baseline.
- The reference engines (QuickFIX-cpp, QuickFIX-J) are the conformance oracles, configured with a FIXT.1.1 transport dictionary + a FIX.5.0 SP2 application dictionary, mirroring the existing live-cell harness conventions.
- Credential transport (`553`/`554`) is plaintext at the FIX layer per the spec; confidentiality is provided by the existing TLS transport, not by this feature. Authorization policy (whether credentials are required/validated) reuses the existing session authorization path rather than introducing new policy here.
- Application-message *business* coverage for FIX 5.0 SP2 (the full 5.0 SP2 message catalogue) is NOT in scope; this feature is about establishing the session and routing to the correct application dictionary, not implementing 5.0 SP2 business messages beyond what the interop cell exercises.
