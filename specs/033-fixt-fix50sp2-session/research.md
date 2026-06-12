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
strand-confined session state at inbound-Logon time; construct + **expose** the negotiated application
`version_profile` (`{session=vt11, default_appl=negotiated}`) for downstream reify call-sites. The session
itself never calls `dict::reify` (it delivers inbound app messages dict-free — see the RESOLVED block
below and R4); `dict::reify` (`include/fixpp/dict/reify.hpp:111-126`) is the path a *downstream* consumer
uses against the exposed profile. **No new ApplVerID→dictionary mapping is invented.**

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
see R4).

**Serviceability handle + refuse disposition (PINNED — Gate A round 1; was "confirm at implement").**
The substrate exists at engine level and is threaded to the Session, not invented:
1. **Build**: `Engine::open` already builds the registry via `core::build_version_registry(cfg)`
   (`include/fixpp/core/engine_config.hpp:211-213`) from `EngineConfig::dictionaries`
   (`engine_config.hpp:129`) — a `dict::version_registry` whose `get(application_version)` returns the
   `Dictionary const*` or `core::error::dict_no_dictionary_for_application_version`
   (`include/fixpp/dict/version_registry.hpp:51-58`).
2. **Thread**: pass a reference to the engine-built `version_registry` to each `Session` at construction
   (the registry outlives all sessions — engine-lifetime; the `Session` holds a non-owning reference).
   Today `SessionConfig` holds only a single `dictionary` (`session_config.hpp:176`); the registry is the
   added handle. NEW `Session` member: `dict::version_registry const& app_version_registry_` (or a
   `version_registry const*`).
3. **Resolve + test serviceability at inbound Logon**: `resolve_application_version({vt11, Unknown, false}, raw_1137)`
   (`version_profile.hpp:111`, wire→C++) → on success `registry.get(resolved)`. If `resolve_*` fails
   (unparseable `1137` value) OR `get()` returns `dict_no_dictionary_for_application_version` ⇒
   **unserviceable**.
4. **Refuse disposition (EXACT wire — distinct from missing-1137)**: a present-but-unserviceable `1137` ⇒
   `Reject(35=3, 371=1137, 373=ValueIsIncorrect(5))` then no Active (FR-004a). This differs from the
   **missing**-`1137` case (FR-004 / R7), which is `RequiredTagMissing(1)`. W3 MUST assert the emitted
   Reject frame (`373=5`, `371=1137`) AND non-Active — not merely "not Active."

**Alternatives rejected**: adding a session-layer reify/validation gate for inbound app
messages (net-new for all versions, regression risk, not required by any SC).

## R3 — Configuration surface

**Decision**: add `SessionConfig::default_appl_ver_id` of type **`std::optional<dict::application_version>`**
(the dict-layer enum, PINNED Gate A round 1 — not a raw wire string). A session is FIXT iff
`begin_string == "FIXT.1.1"` **and** `default_appl_ver_id` is set. Unset → existing FIX.4.x path,
byte-identical (FR-009/SC-002). Credentials carried by optional `SessionConfig` username/password fields
(confirm exact names at /tasks; reuse any existing field if present).

**Outbound render rule (PINNED — and the helper is ABSENT today, so it is a /tasks deliverable).** The
wire `ApplVerID(1137)` value does **not** coincide with the C++ `application_version` enum index in
general (`version_profile.hpp:117-132` warns this explicitly and ships the canonical wire→C++ table). The
real divergences are e.g. enum `v40`(1)→wire `"2"`, enum `v50`(6)→wire `"7"`; some values *coincide*
(enum `v44`(5)→wire `"6"`, enum `v50sp2`(8)→wire `"9"`) but that coincidence MUST NOT be relied on. The
existing `resolve_application_version` is **wire→C++ only**; an inverse `application_version → wire 1137
string` helper does **not** exist anywhere in `include/fixpp/dict` or `src/dictionary` (grep-confirmed
2026-06-12). 033 MUST add that inverse render helper. Required emit/validate tests:
- config `v50sp2` ⇒ Logon emits `1137=9`;
- config `v44` ⇒ Logon emits `1137=6`;
- config `v50` ⇒ Logon emits `1137=7` (a divergent value — proves the helper, not the C++ index);
- an invalid / `Unknown` configured value ⇒ fails **before** Logon (no garbage `1137` on the wire).

**Rationale**: explicit, additive, default-off — the 024/028 knob pattern; zero-regression by
construction; the enum type keeps config in the typed dict-layer vocabulary and forces the wire mapping
through the one canonical table. **Alternatives rejected**: implicit FIXT activation by inferring from the
dictionary (ambiguous, surprising); a separate FIXT config struct (over-engineered for a few fields); a
raw `std::optional<std::string>` wire value (pushes validation onto every call-site and risks an
unvalidated value reaching the wire).

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

**Exposure API (PINNED — Gate A round 1).** The exposure surface is a NEW
`Session::negotiated_version_profile() const → dict::version_profile` accessor returning
`{session=vt11, default_appl=negotiated_appl_version_}` (the profile's `has_per_message_override` is a
documentation-only descriptor not consulted by resolution; it does not control `1128` handling), reachable from a
`fromApp` handler via the EXISTING engine spine `Engine::lookup(SessionId) → std::shared_ptr<Session>`
(`include/fixpp/session/engine.hpp:294`). `SessionId` (`engine.hpp:62-76`) carries no app version, and the
`fromApp` callback shape (`MessageView`+`SessionId`) is **unchanged** — the version is retrieved on demand
through `Engine::lookup`. This is what makes SC-006 *demonstrable* (New-1 / W5): the witness asserts
`Engine::lookup(sid)->negotiated_version_profile().default_appl == v44` for the 4.4 cell and `== v50sp2`
for the 5.0SP2 cell — discriminating version-general selection from a hardcoded path.

SC-006 (version-general, two versions establish through one code path) is satisfied at **establishment**
(config-driven `1137` + serviceability check, R2/R3) AND **demonstrated** via the exposure accessor above,
not by per-message routing.

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
`interpret_logon` parses them when present and returns them.

**Credential sink (PINNED — Gate A round 1; the existing seam CANNOT receive credentials).** The existing
authorization seam is `cfg_.compid_authorization_policy.authorize(peer_identity, asserted_compid)`
(`session.cpp:1849-1916`, `include/fixpp/session/compid_authorization_policy.hpp:78`). It takes
`(peer_identity, compid)` — **no** username/password parameter — and is **gated on `is_mtls`** (it does
not run for non-mTLS sessions). Credentials (`553`/`554`) are FIX-layer fields and must reach a sink on
**every** establishment (mTLS or not — FR-008/SC-003). Therefore 033 adds a dedicated default-accept
credentials seam: NEW `CompIdAuthorizationPolicy::authorize_logon(std::string_view asserted_compid,
logon_credentials const&)` (default implementation accepts; `logon_credentials` = the parsed
`{username?, password?}` value), fired on the establishment path independently of mTLS. **No new
validation policy** (FR-008); FR-008a's future config-gated validation knob attaches to this exact seam.
`Password(554)` is **redacted** (value elided) anywhere a frame is logged/transcribed/goldened (FR-011);
`logon_credentials` must redact its `password` in any `operator<<`/debug representation.

**Rationale**: credentials need a transport-independent recipient the existing mTLS-gated seam cannot
provide; the new seam co-locates with the authorization policy so a future validation feature is a local
change; redaction prevents credential leakage into banked goldens. **Alternatives rejected**: reusing the
mTLS-gated `authorize(peer_identity, compid)` seam (unreachable for non-mTLS, no credential parameter);
implementing credential validation now (clarified out of scope); logging raw 554 (security defect).

## R7 — Missing-1137 reject mechanism

**Decision**: reuse `build_reject` (`admin_messages.cpp:556-660`) with
`SessionRejectReason=RequiredTagMissing(1)` for the **missing**-`1137` case — the **exact pattern** already
used for a Logon missing `EncryptMethod(98)` (`session.cpp:2370-2421`). No new error slot. (The
present-but-**unserviceable**-`1137` case is a DISTINCT disposition — `ValueIsIncorrect(5)` with
`371=1137` — pinned in R2; same `build_reject` mechanism, different `373`/`371`.)

**Rationale**: conformant (matches QF `RequiredTagMissing`), zero new wire/error surface.
**Alternatives rejected**: Logout(35=5) (diverges from the oracle golden); silent disconnect
(non-diagnostic).

## R8 — Witness / test strategy (RED-first)

Per user story, with the FIX.4.x regression guard load-bearing:
- **W1 (US1)** FIXT Logon round-trip: initiator emits `8=FIXT.1.1`+`1137`; acceptor parses + replies with
  its own `1137`; session reaches Active — both roles, asserting the wire fields directly (not a proxy).
- **W2 (US1, FR-004)** inbound FIXT Logon missing `1137` ⇒ `Reject(35=3, 373=1)` + no Active (RED-first).
- **W3 (US1, FR-004a)** inbound FIXT Logon advertising an unserviceable `ApplVerID` ⇒ emit
  `Reject(35=3, 371=1137, 373=ValueIsIncorrect=5)` AND not Active. Assert the **frame** (`373=5`, `371=1137`),
  not merely "not Active" — distinct from W2's `373=1`.
- **W4 (SC-002/FR-009)** FIX.4.4 session: outbound Logon carries **no** `1137`; full wire byte-identical
  to pre-033 (regression guard — capture bytes, compare).
- **W5 (SC-006, New-1)** FIXT.1.1 carrying FIX.4.4 (wire `ApplVerID=6`) establishes via the same code
  path (version-general; no 5.0SP2 hardcoding). **Discriminating assertion** (not the "both reach Active"
  proxy): `Engine::lookup(sid)->negotiated_version_profile().default_appl == application_version::v44` for
  the 4.4 cell, and `== application_version::v50sp2` for the 5.0SP2 cell — proving the negotiated profile
  is exposed and version-selected, not hardcoded.
- **W6 (US2/FR-007/FR-008)** `553`/`554` emitted when configured, parsed inbound, surfaced to authz;
  credential-free FIXT Logon still establishes.
- **W7 (FR-011)** a logged/transcribed Logon with a populated `554` shows the value **redacted**.
- **Live (VII.6)** un-deferred `HP-fixt11-fix50sp2-cells` + a 4.4-over-FIXT family, both roles × QFcpp/QFJ.

**Anti-proxy discipline** ([[feedback_witness_asserts_named_postcondition_not_proxy]]): W1/W4 assert the
actual emitted/absent wire fields; W2 asserts the Reject `373=1` value + non-Active, not merely
"disconnected"; W3 asserts `373=5`/`371=1137` (distinct from W2); W5 asserts the exposed
`negotiated_version_profile().default_appl` per version (New-1, discriminating).

## R9 — Structural traps probed and CONFIRMED SAFE (Gate A round 1, New-2)

Three structural assumptions were probed at source and **hold for FIXT** — recorded so the convergence
pass does not over-correct them (they are *confirmations*, not defects):
- **BeginString length-agnostic**: `build_logon` emits `append_raw(8, sv_to_bytes(begin_string))`
  (`src/session/admin_messages.cpp:86`) — "FIXT.1.1" (8 chars) vs "FIX.4.4" (7) is a non-issue.
- **SessionId keying distinct per family**: `SessionId::from_config` keys on the `begin_string` *string
  value* (`engine.hpp:74-76`) — a FIXT session keys on `"FIXT.1.1"`, distinct from `"FIX.4.4"`; no
  collision / mis-route (R5's exact-match gate holds).
- **`interpret_logon` permissive scanner**: the inbound scanner is a `switch(tag)` with `default: break`
  (`admin_messages.cpp:270`) — unknown tags are skipped, so adding `1137`/`553`/`554` cases is purely
  additive; no reject-unknown assumption to break.
