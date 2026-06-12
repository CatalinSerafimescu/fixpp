# Contract: FIXT.1.1 / FIX 5.0 SP2 Logon Establishment

Behavioural contract for the FIXT establishment path. Each clause is testable (witness IDs from
research.md R8). "FIXT session" ⇔ `SessionConfig::is_fixt()` (`begin_string=="FIXT.1.1"` &&
`default_appl_ver_id` set).

## C1 — Outbound Logon advertises DefaultApplVerID (both roles)

- **Given** a FIXT session, **when** fixpp emits a Logon (initiator's Logon OR an acceptor's Logon reply),
  **then** the frame carries `8=FIXT.1.1` and `DefaultApplVerID(1137)` = the configured
  `default_appl_ver_id`, ordered after `108`. *(FR-001/FR-002; W1)*
- **And** when credentials are configured, the Logon also carries `Username(553)`/`Password(554)`;
  otherwise it carries neither. *(FR-007; W6)*

## C2 — FIX.4.x byte-identical (regression guard)

- **Given** a non-FIXT session (`!is_fixt()`), **when** fixpp emits any message, **then** no
  `1137/553/554` appears and the wire bytes are identical to pre-033. *(FR-009/SC-002; W4 — load-bearing)*

## C3 — Inbound Logon: read + record the negotiated version

- **Given** an inbound FIXT Logon carrying `DefaultApplVerID(1137)` resolvable to a serviceable
  application version, **then** fixpp records it as the session's negotiated application version and
  exposes it as the default `version_profile` for reify call-sites (the session delivers inbound app
  messages dict-free to `fromApp` as it does for FIX.4.x — it does not add a session-layer reify gate;
  research R4). Admin messages use the FIXT.1.1 session layer. *(FR-003/FR-005; W1)*
- **Note**: fixpp does NOT reject merely because the peer's advertised version differs from fixpp's own
  default — each side declares its own (R1).

## C4 — Missing DefaultApplVerID ⇒ Reject(35=3) RequiredTagMissing

- **Given** an inbound `8=FIXT.1.1` Logon that omits `1137`, **then** fixpp responds with a session-level
  `Reject(35=3)` carrying `SessionRejectReason=RequiredTagMissing(1)` and does **not** reach Active.
  *(FR-004/SC-005; W2)*

## C5 — Unserviceable application version ⇒ refuse establishment

- **Given** an inbound FIXT Logon advertising an `ApplVerID` with no serviceable dictionary, **then**
  fixpp refuses establishment (does not reach Active) conformantly; it does not silently downgrade.
  *(FR-004a/SC-005; W3)*

## C6 — Version-general (no 5.0SP2 hardcoding)

- **Given** a FIXT session configured for `ApplVerID`=FIX.4.4 (6), **when** it establishes against a
  FIXT-carrying-4.4 counterparty, **then** establishment succeeds through the same code path as 5.0SP2
  (selection driven by configured/negotiated `ApplVerID` via the version layer). *(FR-006/SC-006; W5)*

## C7 — Credentials surfaced, not validated (this feature)

- **Given** an inbound FIXT Logon with `553`/`554`, **then** the values are made available to the existing
  authorization decision path; **and** a credential-free FIXT Logon still establishes when the deployment
  does not require credentials. No new credential-validation/rejection is introduced by 033. *(FR-008/FR-008a; W6)*

## C8 — Password redaction

- **Given** any persisted log/transcript/golden of a Logon carrying a populated `554`, **then** the
  password value is redacted/elided (never clear-text). *(FR-011; W7)*

## C9 — Per-message ApplVerID(1128) tolerated, not routed

- **Given** an established FIXT session, **when** an inbound application message carries `ApplVerID(1128)`,
  **then** it is parsed without failure and the resolved application dictionary remains the negotiated
  default (no mid-session switch). *(FR-010 / S-026 deferred; covered by an established-session unit)*

## C10 — Interop (live, both engines, both roles)

- **Given** QFcpp/QFJ configured for FIXT.1.1/FIX.5.0SP2 (and a FIXT/FIX.4.4 variant), **when** the
  un-deferred cells run with fixpp as initiator and acceptor against each engine, **then** each
  establishes and matches a banked golden; the manifest no longer carries `deferred:fixt-routing`.
  *(SC-004/SC-006)*
