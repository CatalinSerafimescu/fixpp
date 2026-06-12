# Phase 0 Research: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

All `[NEEDS CLARIFICATION]` are resolved here or in `spec.md` `## Clarifications` (2026-06-12).

## R1 — Reference-engine oracle (QuickFIX-cpp / QuickFIX-J)

**Decision**: Model fixpp's FIXT establishment on the QuickFIX behaviour, since QFcpp/QFJ are the live
conformance oracles for the un-deferred cell.

**Evidence (QuickFIX-cpp `reference-engines/quickfix-cpp/src/C++/Session.cpp`)**:
- **Both roles advertise their own `DefaultApplVerID`** on Logon: the initiator Logon (`:674-675`) and
  the acceptor Logon reply (`:701-702`) each `setField(DefaultApplVerID(m_senderDefaultApplVerID))`.
  → fixpp must emit `1137` on **every** outbound Logon (FR-002), not only the initiator's.
- **Inbound `1137` is read into the peer's app version** (`:1210-1212`):
  `FIELD_GET_REF(message, DefaultApplVerID)` → `setTargetDefaultApplVerID(applVerID)`. Each side records
  the *peer's* declared version for inbound app messages; there is **no mismatch reject** — different
  defaults are normal.
- **Missing `1137` on a FIXT Logon** → `FIELD_GET_REF` throws → `RequiredTagMissing` (`:1253`) → a
  session-level Reject. → FR-004: `Reject(35=3)`, `SessionRejectReason=RequiredTagMissing(1)`, no establish.
- **Application messages on a FIXT session** select the application data dictionary from the recorded
  target/sender DefaultApplVerID (`:518-525`, `:1170-1172`, `:1221-1222`); admin messages bypass the
  app dictionary (`:518` `isAdminMsgType`). → FR-005 (admin = FIXT session layer; app = negotiated
  dictionary).

**Rationale**: matching the oracle keeps the live golden stable and avoids inventing non-conformant
behaviour. **Alternatives rejected**: rejecting on app-version "mismatch" (QF does not — each side
declares its own); inferring a default when `1137` absent (QF treats it as required).

## R2 — Session ↔ version layer wiring (the design crux)

**Decision**: Reuse the shipped version layer; resolve the negotiated application dictionary from the
peer-advertised `DefaultApplVerID` via `version_profile::resolve_application_version`
(`include/fixpp/dict/version_profile.hpp:111`) + `version_registry`
(`include/fixpp/dict/version_registry.hpp:22-66`). Store the negotiated `application_version` in
strand-confined session state at inbound-Logon time; construct the inbound application `version_profile`
(`{session=vt11, default_appl=negotiated, has_per_message_override=true}`) and feed it to the existing
`dict::reify` resolution path (`include/fixpp/dict/reify.hpp:111-126`). **No new ApplVerID→dictionary
mapping is invented.**

**Evidence the substrate exists**: `version_profile` already carries
`{session, default_appl, has_per_message_override}` (`version_profile.hpp:71-79`); `reify` already
dual-dispatches FIXT-admin vs application using the profile + `ApplVerID(1128)`/`default_appl`
(`reify.hpp:111-126`); `version_registry` maps `application_version → Dictionary const*`; the XML ships
(`dictionaries/FIXT11.xml` + `FIX50SP2.xml`) and `xml_loader` loads `FIXT` as `session_version::vt11`
(`src/dictionary/xml_loader.cpp:148`).

**RESOLVED (pinned 2026-06-12 by source read — see R4)**: the session does **not** apply an application
dictionary to *inbound* application messages at all — for any version. The established-session inbound
path (`Session::parse_and_dispatch_`, `src/session/session.cpp:238-265`, called from the steady-state
routing at `:2526/:2554`) builds a **dict-free** `wire::Parser<access_mode::Index>` MessageView
("Length field, with no runtime dictionary" — `include/fixpp/wire/parser.hpp:55`) and passes it to
`Application::fromApp`. `dict::reify` is invoked **only** inside `src/dictionary/` (reify.cpp,
version_profile.cpp), **never** in `src/session/`. Therefore the session-layer wiring for 033 is:
1. **At inbound Logon**: read `1137`, resolve it to an `application_version`, and **validate
   serviceability** against the available application dictionaries (the `version_registry` /
   the engine's dictionary set) — unserviceable ⇒ refuse (FR-004a). This is the only place the registry
   is consulted.
2. **Store** the negotiated `application_version` in strand-confined session state (E2) and **expose** it
   (so reify call-sites that DO need a typed view use the negotiated profile).
No per-message session-layer `reify` gate is added (it would be net-new behaviour absent for FIX.4.x too —
see R4). **Confirm at implement**: the exact handle the session consults at logon to test serviceability
(engine dictionary set vs. a `version_registry` reference on `SessionConfig`); this is a small lookup, not
a routing rewrite. **Alternatives rejected**: adding a session-layer reify/validation gate for inbound app
messages (net-new for all versions, regression risk, not required by any SC).

## R3 — Configuration surface

**Decision**: add `SessionConfig::default_appl_ver_id` (optional — `std::optional` of the app-version
wire value/enum). A session is FIXT iff `begin_string == "FIXT.1.1"` **and** `default_appl_ver_id` is
set. Unset → existing FIX.4.x path, byte-identical (FR-009/SC-002). Credentials carried by optional
`SessionConfig` username/password fields (confirm exact names at /tasks; reuse any existing field if
present).

**Rationale**: explicit, additive, default-off — the 024/028 knob pattern; zero-regression by
construction. **Alternatives rejected**: implicit FIXT activation by inferring from the dictionary
(ambiguous, surprising); a separate FIXT config struct (over-engineered for a few fields).

## R4 — Application-message handling scope (the load-bearing fact, pinned)

**Two distinct questions must not be conflated**: (a) app-message *validation* (strictness) — out of
scope; (b) app-message *dictionary selection* (which dictionary an inbound app message resolves against)
— FR-005/SC-006, in scope. The pinned source fact resolves both:

**Pinned fact (source read 2026-06-12)**: fixpp's session layer delivers inbound application messages to
`Application::fromApp` as **dict-free wire `MessageView`s** (field-indexed; `parse_and_dispatch_` →
`wire::Parser<access_mode::Index>`, `session.cpp:238-265`; parser is "with no runtime dictionary",
`parser.hpp:55`). The session **never** calls `dict::reify` for inbound messages — true for FIX.4.4
**today**, not a FIXT-specific gap. Dictionary-driven typed interpretation happens at reify call-sites
(`src/dictionary/`), which already accept a `version_profile`.

**Decision**: 033 does **not** add a session-layer reify/validation gate for inbound application messages.
FR-005 ("interpret application messages against the negotiated dictionary") is realized as **record the
negotiated `application_version` (E2) and expose it** so that wherever an application message *is* reified,
the negotiated profile is used. The session-layer behaviour for app messages is **identical to FIX.4.x**
(dict-free `fromApp` delivery) → zero regression by construction, and **no net-new per-message plumbing**.
SC-006 (version-general, two versions establish through one code path) is satisfied at **establishment**
(config-driven `1137` + serviceability check, R2/R3), not by per-message routing.

**Rationale**: matches the existing architecture and the advisor's discrimination — selection ≠
validation, and the session does neither for inbound app messages. **Alternatives rejected**: adding a
session-layer reify gate (net-new for ALL versions, regression risk, no SC requires it); claiming FR-005
needs per-message dictionary application in the session (contradicted by the FIX.4.x path).

## R5 — BeginString acceptance (acceptor)

**Decision**: the acceptor accepts inbound `8=FIXT.1.1` **only** when configured as a FIXT session
(`begin_string=="FIXT.1.1"` + `default_appl_ver_id` set). The existing exact-match BeginString gate
(`admin_messages.cpp:297-298`, `session.cpp:1695`) is satisfied because the configured `begin_string` is
`"FIXT.1.1"`. No multi-family acceptance on one config.

**Rationale**: preserves the strict version-gating (S-020) and zero-regression; a FIX.4.4-configured
acceptor still refuses `8=FIXT.1.1` exactly as today. **Alternatives rejected**: implicit acceptance of
FIXT on a FIX.4.x-configured session (regression surface; surprising).

## R6 — Credentials (553/554) + redaction

**Decision**: `build_logon` optionally appends `Username(553)`/`Password(554)` when configured;
`interpret_logon` parses them when present and returns them; the inbound values are surfaced to the
existing CompID/authorization seam (`session.cpp:1849-1916`). **No new validation policy** (FR-008);
the seam is left ready for a future config-gated validation feature (FR-008a). `Password(554)` is
**redacted** (value elided) anywhere a frame is logged/transcribed/goldened (FR-011).

**Rationale**: minimal, additive, reuses the authz seam; redaction prevents credential leakage into
banked goldens. **Alternatives rejected**: implementing credential validation now (clarified out of
scope); logging raw 554 (security defect).

## R7 — Missing-1137 reject mechanism

**Decision**: reuse `build_reject` (`admin_messages.cpp:556-660`) with
`SessionRejectReason=RequiredTagMissing(1)` — the **exact pattern** already used for a Logon missing
`EncryptMethod(98)` (`session.cpp:2370-2421`). No new error slot.

**Rationale**: conformant (matches QF `RequiredTagMissing`), zero new wire/error surface.
**Alternatives rejected**: Logout(35=5) (diverges from the oracle golden); silent disconnect
(non-diagnostic).

## R8 — Witness / test strategy (RED-first)

Per user story, with the FIX.4.x regression guard load-bearing:
- **W1 (US1)** FIXT Logon round-trip: initiator emits `8=FIXT.1.1`+`1137`; acceptor parses + replies with
  its own `1137`; session reaches Active — both roles, asserting the wire fields directly (not a proxy).
- **W2 (US1, FR-004)** inbound FIXT Logon missing `1137` ⇒ `Reject(35=3, 373=1)` + no Active (RED-first).
- **W3 (US1, FR-004a)** inbound FIXT Logon advertising an unserviceable `ApplVerID` ⇒ refuse establish.
- **W4 (SC-002/FR-009)** FIX.4.4 session: outbound Logon carries **no** `1137`; full wire byte-identical
  to pre-033 (regression guard — capture bytes, compare).
- **W5 (SC-006)** FIXT.1.1 carrying FIX.4.4 (`ApplVerID`=6) establishes via the same code path
  (version-general; no 5.0SP2 hardcoding).
- **W6 (US2/FR-007/FR-008)** `553`/`554` emitted when configured, parsed inbound, surfaced to authz;
  credential-free FIXT Logon still establishes.
- **W7 (FR-011)** a logged/transcribed Logon with a populated `554` shows the value **redacted**.
- **Live (VII.6)** un-deferred `HP-fixt11-fix50sp2-cells` + a 4.4-over-FIXT family, both roles × QFcpp/QFJ.

**Anti-proxy discipline** ([[feedback_witness_asserts_named_postcondition_not_proxy]]): W1/W4 assert the
actual emitted/absent wire fields; W2 asserts the Reject `373=1` value + non-Active, not merely
"disconnected".
