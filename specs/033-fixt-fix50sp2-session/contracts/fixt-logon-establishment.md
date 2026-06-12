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

- **Given** a non-FIXT session (`!is_fixt()`), **then** the FIX.4.x Logon/establishment frames built by
  `build_logon` are byte-identical to pre-033, and 033 appends no FIXT-only `1137/553/554` field to that
  establishment path. (Application payload pass-through is unmodified.) *(FR-009/SC-002; W4 — load-bearing)*

## C3 — Inbound Logon: read + record the negotiated version

- **Given** an inbound FIXT Logon carrying `DefaultApplVerID(1137)` resolvable to a serviceable
  application version, **then** fixpp records it as the session's negotiated application version and
  exposes it via `Session::negotiated_version_profile()` (reachable from `fromApp` through
  `Engine::lookup(SessionId)→shared_ptr<Session>`) as the default `version_profile` for reify call-sites
  (the session delivers inbound app
  messages dict-free to `fromApp` as it does for FIX.4.x — it does not add a session-layer reify gate;
  research R4). Admin messages use the FIXT.1.1 session layer. *(FR-003/FR-005; W1)*
- **Note**: fixpp does NOT reject merely because the peer's advertised version differs from fixpp's own
  default — each side declares its own (R1).

## C4 — Missing DefaultApplVerID ⇒ Reject(35=3) RequiredTagMissing

- **Given** an inbound `8=FIXT.1.1` Logon that omits `1137`, **then** fixpp responds with a session-level
  `Reject(35=3)` carrying `SessionRejectReason=RequiredTagMissing(1)` and does **not** reach Active.
  *(FR-004/SC-005; W2)*

## C5 — Unserviceable application version ⇒ Reject(35=3) ValueIsIncorrect, refuse establishment

- **Given** an inbound FIXT Logon whose `DefaultApplVerID(1137)` is present but resolves to no serviceable
  dictionary (`version_registry::get(resolved) == dict_no_dictionary_for_application_version`, or `1137`
  unparseable), **then** fixpp emits a session-level `Reject(35=3)` carrying `RefTagID(371)=1137` and
  `SessionRejectReason(373)=ValueIsIncorrect(5)`, and does **not** reach Active (no silent downgrade).
  This is DISTINCT from C4's missing-tag `RequiredTagMissing(1)`. W3 asserts the emitted frame
  (`373=5`, `371=1137`) AND non-Active. *(FR-004a/SC-005; W3)*

## C6 — Version-general (no 5.0SP2 hardcoding) — demonstrated via the exposure accessor

- **Given** a FIXT session configured for FIX.4.4 (wire `ApplVerID=6`), **when** it establishes against a
  FIXT-carrying-4.4 counterparty, **then** establishment succeeds through the same code path as 5.0SP2
  (selection driven by configured/negotiated `ApplVerID` via the version layer), **and**
  `Engine::lookup(sid)->negotiated_version_profile().default_appl` == `application_version::v44` (and
  `== v50sp2` for the 5.0SP2 cell) — the discriminating assertion that proves version-general selection,
  not a hardcoded path (New-1; not the "both reach Active" proxy). *(FR-006/SC-006; W5)*

## C7 — Credentials surfaced, not validated (this feature)

- **Given** an inbound FIXT Logon with `553`/`554`, **then** the values are surfaced as a
  `logon_credentials` value to the NEW default-accept `CompIdAuthorizationPolicy::authorize_logon(asserted_compid,
  logon_credentials)` seam, fired on the establishment path **independently of mTLS** (the existing
  `authorize(peer_identity, compid)` seam is mTLS-gated and takes no credentials — research R6); **and** a
  credential-free FIXT Logon still establishes when the deployment does not require credentials. No new
  credential-validation/rejection is introduced by 033; FR-008a's future validation knob attaches to
  `authorize_logon`. *(FR-008/FR-008a; W6)*

## C8 — Password redaction (named persistence sites + shared redactor)

- **Given** any persisted log/transcript/golden of a Logon carrying a populated `554`, **then** the
  password value is redacted/elided (never clear-text) at every persistence site:
  (1) the session logger/tap, (2) any transport transcript capture, (3) the `tests/interop` golden
  writer/normalizer (`phase-9-harness/tools/run_interop_cell.py`), (4) unit golden fixtures. A **single
  shared tag-`554` field redactor** is applied at each site (not per-site ad-hoc patches —
  [[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]] discipline); `logon_credentials` itself
  redacts `password` in any debug/`operator<<` form. W7 asserts redaction **per persisted-artifact class**.
  *(FR-011; W7)*

## C9 — Per-message ApplVerID(1128) tolerated, not routed

- **Given** an established FIXT session, **when** an inbound application message carries `ApplVerID(1128)`,
  **then** it is parsed without failure and the **session layer** does not switch the application
  dictionary — the session delivers the message dict-free and never reifies, so it never selects an
  application dictionary at all (research R4). *(Not a claim that reify ignores `1128`: a downstream
  consumer reifying the exposed profile on a `1128`-bearing message would honor it — that per-message
  override is the consumer's choice, S-026 deferred.)* *(FR-010 / S-026 deferred; covered by an
  established-session unit asserting the session's dict-free delivery path)*

## C10 — Interop (live, both engines, both roles) — 8 named cells

- **Given** QFcpp/QFJ configured for FIXT.1.1/FIX.5.0SP2 (and a FIXT/FIX.4.4 variant), **when** the
  un-deferred cells run with fixpp as initiator and acceptor against each engine, **then** each
  establishes and matches a banked golden; the manifest no longer carries `deferred:fixt-routing`.
  *(FR-012/SC-004/SC-006)*
- **The 8 concrete cells** (2 dialect families × 2 roles × {QFcpp, QFJ}):
  `HP-fixt50sp2-qfcpp-init`, `HP-fixt50sp2-qfcpp-acc`, `HP-fixt50sp2-qfj-init`, `HP-fixt50sp2-qfj-acc`,
  `HP-fixt44-qfcpp-init`, `HP-fixt44-qfcpp-acc`, `HP-fixt44-qfj-init`, `HP-fixt44-qfj-acc`.
- **Counterparty config templates**: `TransportDataDictionary=FIXT11.xml`;
  `AppDataDictionary=FIX50SP2.xml` (50sp2 family) / `FIX44.xml` (4.4 family);
  `DefaultApplVerID=9` (50sp2) / `6` (4.4). (Exact ids/paths registered at /tasks/implement;
  see quickstart for the run + golden-bank procedure.)
