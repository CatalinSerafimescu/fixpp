# Feature Specification: FIX Latest is ApplVerID 10 — its own application version, both directions

**Feature Branch**: `097-fix-latest-applverid-10`

**Created**: 2026-10-29

**Status**: Draft

**Input**: User description: "V1.0 FUNCTIONAL TRACK row 4 (REMAINING-WORK row 4b, re-scoped by the owner
2026-10-29). FIX Latest is identified on the wire by ApplVerID value 10 = FIXLatest. Feature 074's
reconcile was a factual error and is recorded as an erratum. Give FIX Latest its own application version
in both directions and its own registry slot, so a FIX50SP2 and a FIX Latest dictionary coexist and
L-074-1 / L-080-1 retire; accept and ignore inbound DefaultApplExtID(1407) / ApplExtID(1156)."

## Terminology

- **ApplVerID value** — the wire string carried by DefaultApplVerID(1137) on a FIXT.1.1 Logon and by
  ApplVerID(1128) in an application message header. `"9"` = FIX50SP2, `"10"` = FIXLatest.
- **Application version** — fixpp's C++ identity for an ApplVerID value
  (`fixpp::dict::application_version`). Today it has no member for FIX Latest.
- **Session version** — fixpp's identity for a loaded dictionary (`fixpp::dict::session_version`).
  `vlatest` already exists (074) and is the identity of the dictionary loaded from
  `dictionaries/orchestra/OrchestraFIXLatest.xml`.
- **Registry** — `fixpp::dict::version_registry`, built from `EngineConfig::dictionaries`; answers "is
  there a dictionary for application version X". Read today only by the FIXT DefaultApplVerID
  serviceability checks (the acceptor's inbound-Logon check and the `open()` check of the session's
  own configured default). Inbound validation does **not** go through it: each session validates with
  its own `SessionConfig::dictionary`.
- **Serviceable** — an ApplVerID value is serviceable when it parses to an application version and the
  registry holds a dictionary for that version.

## Context: why this feature exists

Row 4b was planned as "ApplExtID(1156)=303 on-wire differentiation + `version_registry` re-key". That
plan rested on feature 074's reconcile: *"FIX Latest has no distinct ApplVerID(1128) value — that enum
caps at 9 = FIX50SP2"*, so 074 mapped FIX Latest's application version onto FIX50SP2's, and a registry
holding both dictionaries `std::abort`s (FR-010; L-074-1, L-080-1).

**The premise is false.** Verified in primary sources on 2026-10-29 (parent
`research/findings/v1-row4b-applverid-10-sweep-2026-10-29.md` carries every citation):

- FIX Session Layer with Errata (November 2020), §11.2 (p77) lists `FIXLatest = 10` as an ApplVerID
  value.
- fixpp's own vendored `dictionaries/orchestra/OrchestraFIXLatest.xml` (`version="FIX.Latest_EP303"`)
  defines ApplVerID code `FIXLatest`, `value="10"`, `addedEP="260"`. The FIXT11.xml enums that stop at 9
  predate EP260.
- QuickFIX/J maps `FIX.Latest` ↔ `10` (`quickfixj-base/.../MessageUtils.java`) and sends `1137=10` on
  its Logon when configured for FIX Latest.

**Consequences in the shipped tree (fixpp `23895374`):**

1. An inbound FIXT Logon with `1137=10` does not parse (`resolve_application_version` accepts `"2"`..`"9"`),
   so the acceptor answers Reject(373=5, 371=1137) and disconnects. **A QuickFIX/J peer configured for
   FIX Latest cannot log on to fixpp.**
2. One registry cannot hold FIX50SP2 and FIX Latest at once (the FR-010 abort), so one engine cannot
   serve both.
3. A message resolved under a FIX Latest session reifies as a `fixpp::v50sp2` handle
   (`tests/codegen/vlatest_dispatch_exclusion_test.cpp` (a1) pins this as a regression anchor "so a
   future fix is deliberate"). This feature fixes it **for a session that negotiates FIX Latest**,
   i.e. whose configured (initiator) or advertised (acceptor) DefaultApplVerID is `10`. A session that
   loads the FIX Latest dictionary but is configured `default_appl_ver_id = "9"` still negotiates
   FIX50SP2 (the one-way fallback, FR-006) and still reifies under the FIX50SP2 arm; B-097-4 discloses
   this.

**Peer behaviour this feature must stay compatible with** (same findings file): QuickFIX C++, QuickFIX/J
and QuickFIX/Go all **accept** an inbound Logon with `1137=10` (1137 is an enum-less STRING in their
FIXT11.xml), all can be configured to **send** it, and **no session code in any of them reads
DefaultApplExtID(1407) or ApplExtID(1156)**. Whether a frame carrying either tag passes **validation** is
decided by each engine's dictionary: QuickFIX C++ and QuickFIX/J ship a FIXT11.xml declaring both, so they
pass; QuickFIX/Go's shipped `spec/FIXT11.xml` declares neither, so with that transport dictionary
configured it fails a Logon carrying 1407 with `InvalidTagNumber` and disconnects (findings file Q4;
inferred from source, not run). QuickFIX C++ and QuickFIX/Go **reject a header `1128=10`** (their 1128
enum stops at 9). The FIX documents mandate no receiver behaviour for an unsupported 1407/1156 and state
engines have "no requirement … to dynamically configure and apply Extension Packs".

**Owner ruling, 2026-10-29:** ApplVerID 10 in both directions; FIX Latest gets its own application
version and registry slot; the abort goes; inbound 1407/1156 are accepted and ignored; fixpp emits no
1407; 074's reconcile is recorded as an erratum. *("Accepted and ignored" is read as "no new rule" by the
owner's Gate A round-1 ruling — Clarifications, Session 2026-10-29 (Gate A round 1).)*

## Clarifications

### Session 2026-10-29 (orchestrator, from the owner ruling above)

- Q: Does FIX Latest get its own application version, or a composite (ApplVerID 9, ApplExtID 303) key? →
  A: Its own application version, wire value `"10"`. 074 rejected a FIX Latest application version only
  because it would have rendered as `"9"` and made the wire map non-injective; with `"10"` distinct, that
  objection does not apply. The composite key is rejected: no QuickFIX engine reads 1407/1156, so it
  would differentiate nothing for a real peer.
- ~~Q: What does fixpp do with inbound 1407 / 1156? → A: Accept and ignore, on every path (the behaviour
  of all three QuickFIX engines). No Reject, no disconnect, no effect on version resolution.~~
  **Superseded by the Gate A round-1 bullet below (owner, 2026-10-29).** Kept as history. It overstated
  the peers: QuickFIX/Go with its shipped FIXT11.xml rejects 1407. It also read as a dictionary-agnostic
  validation promise, which the shipped validator does not make.
- Q (Q2): A message resolved to FIX Latest — a FIX Latest reify-dispatch arm, or an explicit error? →
  A: The arm (orchestrator, technical, 2026-10-29). Measured at `23895374`: the generated
  `_dispatch/reify_dispatch_application.hpp` (1027 lines, 77 KB) includes no per-version `Reify.hpp`;
  each arm is a MsgType switch minting a handle from version metadata, so a FIX Latest arm adds one more
  generated switch, not the 8 MB `vlatest/Reify.hpp`. The error option would only defer the same switch
  to a follow-up. When the FIX Latest codegen tier is disabled (`FIXPP_CODEGEN_FIX_LATEST=OFF`) there is
  no arm and FIX Latest falls to the existing fail-loud outer default.
- Q (Q1): An engine holding only the FIX Latest dictionary, asked to serve a peer's `1137=9` — strict
  (refuse) or fall back to the FIX Latest dictionary? → A: **One-way fallback** (owner, 2026-10-29,
  second ruling). The owner first ruled Strict on an option text that said serving both was a matter of
  registering FIX50SP2 too; that holds for direct C++ only. TOML config loads exactly one dictionary
  (`src/config/selector_resolver.cpp`) and the C API never fills `EngineConfig::dictionaries`
  (`src/capi/config.cpp`), so Strict would have forced every TOML FIX Latest FIXT session from `"9"` to
  `"10"` — on the wire, QuickFIX/Go then routes app messages under `"10"` (BusinessMessageReject without
  routes) and QuickFIX C++ skips app validation without `AppDataDictionary.10` — and left a TOML acceptor
  unable to admit `1137=9`. QuickFIX/J admits `9` with only FIX Latest configured (it loads FIX50SP2.xml
  from its jar). Ruling: exact match first; FIX Latest answers FIX50SP2 when FIX50SP2 is absent; never the
  reverse. Not BREAKING for the registry; inbound validation still uses each session's own dictionary.

### Session 2026-10-29 (`/speckit-clarify`)

> ⚠️ **Out of pipeline order.** `/speckit-clarify` is mandatory before `/speckit-plan` for a wire/codegen
> feature (`[const §XVI.3]`). It was skipped and is run here after `/speckit-plan` (`cebca1eb`, reconciled
> with the second Q1 ruling at `afb8f92a`) and before Gate A. The decisions in the session above were taken with the owner directly, ahead of this scan. Any
> answer below that touches a plan artifact is carried into it in the same commit. The deviation is
> recorded in `plan.md`'s Constitution Check (`[const §XVI.3]` row) and Complexity Tracking.

- Q: When an acceptor session whose own dictionary is FIX50SP2 runs on an engine that also holds FIX
  Latest, and the peer's Logon says `1137=10`, is the peer admitted? → A: **Yes — admission stays
  engine-wide** (owner, 2026-10-29). A peer is admitted when the engine's registry serves the advertised
  version (FR-007). This is today's rule for every version, e.g. `1137=6` on a FIX50SP2 session when
  FIX44 is registered. Validation still uses the session's own dictionary, so such a peer's FIX-Latest-only
  MsgTypes are rejected by that validator. Tying admission to the session's dictionary is a new rule for all
  versions and is out of scope.

### Session 2026-10-29 (Gate A round 1)

- Q: With `validate_inbound_messages` on, inbound validation uses the session's own
  `SessionConfig::dictionary`, and it rejects a tag that dictionary does not declare for the MsgType.
  Does "accept and ignore 1407/1156 on every path" mean fixpp adds a validator rule to accept them
  whatever the session dictionary is, or that fixpp adds no rule? → A: **No new rule** (owner,
  2026-10-29). fixpp adds no 1407- or 1156-specific code. Neither tag affects version resolution,
  serviceability or session state. Whether a validated inbound frame carrying either tag passes is
  decided by the session's own dictionary, as for every tag (L-041-2). The vendored `FIXT11.xml` and the
  Orchestra FIX Latest file both declare both tags. A session dictionary that does not declare them
  rejects them when validation is on. One example is a FIX4.x-shaped dictionary with 1137 injected.
  QuickFIX/Go behaves the same way with its shipped FIXT11.xml, which declares neither tag. B-097-3
  discloses this. There is no validator change, and "no `src/session/` change" stands. The rejected
  alternative is a validator-private whitelist for Logon 1407 and header 1156 across all versions. It
  would have put `src/dictionary` and `include/fixpp/wire` changes into the plan.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — A FIX Latest peer can log on (Priority: P1)

An operator runs a fixpp FIXT.1.1 acceptor whose engine holds the FIX Latest dictionary. A counterparty
configured for FIX Latest (QuickFIX/J `DefaultApplVerID=FIX.Latest`) sends a Logon carrying `1137=10`.
The session is established, and the session's negotiated application version reads as FIX Latest.

**Why this priority**: today this Logon is refused. It is the interop defect the false premise caused.

**Independent Test**: drive a FIXT acceptor with a FIX Latest dictionary through an inbound Logon with
`1137=10`; observe a Logon reply and the Active state, and the negotiated version profile's application
version equal to FIX Latest. On the unfixed tree the same input yields Reject(373=5, 371=1137).

**Acceptance Scenarios**:

1. **Given** a FIXT acceptor whose registry holds the FIX Latest dictionary, **When** a Logon with
   `1137=10` arrives, **Then** the session replies with a Logon, reaches Active, and its negotiated
   application version is FIX Latest.
2. **Given** a FIXT initiator, **When** the peer's Logon reply carries `1137=10`, **Then** the initiator
   records FIX Latest as the negotiated application version.
3. **Given** a FIXT session configured with FIX Latest as its default application version, **When** it
   sends its Logon, **Then** the Logon carries `1137=10`, and no frame the session originates (Logon,
   Logon reply, Reject, Heartbeat, TestRequest, Logout) carries 1407, 1156 or a header 1128. An
   application payload that itself carries 1128 or 1156 is sent as given; this is unchanged (FR-008).
4. **Given** a FIXT acceptor whose registry holds no FIX Latest dictionary, **When** a Logon with
   `1137=10` arrives, **Then** it is refused exactly as any other unserviceable version is today
   (Reject 373=5, 371=1137, then disconnect).

---

### User Story 2 — One engine serves FIX50SP2 and FIX Latest (Priority: P1)

An integrator configures one engine, in C++ (`EngineConfig::dictionaries`; the TOML and C-API surfaces
load one dictionary — L-097-1), with both the FIX50SP2 dictionary and the FIX Latest dictionary.
Construction succeeds. A peer advertising `1137=9` resolves to the FIX50SP2 dictionary, a peer advertising
`1137=10` to the FIX Latest dictionary.

**Why this priority**: it is the limitation (L-074-1, L-080-1) row 4b exists to retire.

**Independent Test**: build a registry from both dictionaries; look each version up; assert each returns
its own dictionary. On the unfixed tree the construction aborts with the FR-010 diagnostic.

**Acceptance Scenarios**:

1. **Given** both dictionaries, **When** the registry is built, **Then** construction succeeds and no
   diagnostic is written.
2. **Given** that registry, **When** FIX50SP2 is looked up, **Then** the FIX50SP2 dictionary is returned;
   **When** FIX Latest is looked up, **Then** the FIX Latest dictionary is returned.
3. **Given** a FIXT acceptor on that engine, **When** two peers log on with `1137=9` and `1137=10`,
   **Then** both are established, each session negotiating the version its peer advertised (admission is
   engine-wide; each session validates with its own dictionary — Clarifications, `/speckit-clarify`).

---

### User Story 3 — A FIX Latest message is typed as FIX Latest (Priority: P2)

An application reifies an inbound application message whose resolved application version is FIX
Latest. It never receives a `fixpp::v50sp2`-labelled handle for it. (FIXT admin MsgTypes route to the
FIXT admin dispatch before application dispatch and are unchanged.)

**Why this priority**: the silent mis-label is pinned today only as an anchor; once FIX Latest is a real
application version the anchor's premise is gone, and the outcome must be stated.

**Independent Test**: reify a FIX Latest MsgType under a FIX Latest profile; inspect the handle's
version. On the unfixed tree no input can resolve to FIX Latest at all (FR-001), so the RED is the
anchor (a1)'s FIX50SP2-labelled handle under the profile a FIX Latest session negotiates today, and
`vlatest_compile_smoke_test.cpp`'s `which() == v50sp2`.

**Acceptance Scenarios**:

1. **Given** a message of any MsgType the FIX Latest dictionary defines, other than the seven FIXT
   admin MsgTypes `dict::reify` intercepts first, resolved to FIX Latest, **When** it is reified at
   runtime (`dict::reify`), **Then** the result is an `owning_message_handle` whose
   `version().application` is `application_version::vlatest` (the partition of FR-009(a)). This
   includes XMLnonFIX (35=n): it reifies as `application` / `vlatest`. Runtime dispatch returns this
   untyped handle for every version. The typed downcast (`as<Msg>()`) is declared-only and stays out
   of scope.
2. **Given** the generated typed class `fixpp::vlatest::owning_<Msg>` for any FIX Latest MsgType,
   **When** its static `which()` is read, **Then** it returns `application_version::vlatest`. Before
   097 it returned `v50sp2`.
3. **Given** a MsgType FIX Latest does not define, resolved to FIX Latest, **When** it is reified,
   **Then** the existing unknown-MsgType error is returned.
4. **Given** a build with the FIX Latest codegen tier disabled (`FIXPP_CODEGEN_FIX_LATEST=OFF`), **When**
   a message resolved to FIX Latest is reified, **Then** the existing fail-loud outer-default error
   (`dict_reify_unknown_msg_type`) is returned, never a FIX50SP2 handle. That build compiles under
   `FIXPP_WERROR=ON`. No CI lane builds OFF, so this scenario is witnessed by the one OFF configure+build
   that `/speckit-verify` runs (contracts W-9(c)).

---

### User Story 4 — The record says what is true (Priority: P2)

A maintainer reading the constitution, B&L, catalogue and code comments about FIX Latest's wire identity
finds ApplVerID 10, and finds 074's reconcile flagged as an erratum naming this feature.

**Independent Test**: grep the live surfaces listed in FR-012 for the superseded claim; every remaining
occurrence is flagged as superseded in place or lives in a frozen record that a component page flags.

**Acceptance Scenarios**:

1. **Given** the merged tree, **When** the constitution's FIX Latest clauses are read, **Then** they state
   that FIX Latest's application version is ApplVerID 10 (delivered by 097), negotiated when the
   DefaultApplVerID is `10`, and Article I §1 (bullet and milestone line), Article XVIII §2, the
   coverage-index Gap Registry D-011 row and the catalogue D-011 row each carry the canonical residue
   sentence (data-model §E.0) verbatim:

   For FIX Latest, only the EP-level field back-port into the legacy dictionaries and Extension Pack negotiation (DefaultApplExtID(1407) / ApplExtID(1156), which fixpp does not read) remain post-1.0, both in the Article XVIII §2 v1.2 milestone.
2. **Given** the live B&L, **When** it is read, **Then** L-074-1 and L-080-1 are absent from it and
   present in the closed file with the retiring feature.

### Edge Cases

- **Inbound `1137=10` and `1407=303` together** — 1407 has no effect on resolution, serviceability or
  state (FR-008). The Logon is admitted when `1137=10` is serviceable, and when validation is off or
  the session dictionary declares 1407 for Logon (as `FIXT11.xml` and the Orchestra file do).
- **Inbound `1407` on a validated session whose dictionary does not declare it** (e.g. a FIX4.x-shaped
  dictionary with 1137 injected) — the validator rejects the Logon on 1407, as it rejects any undeclared
  tag. This happens before the 1137 check. It is today's behaviour, is unchanged, and is disclosed in
  B-097-3.
- **Inbound `1407` with no `1137`** — the existing missing-1137 rule fires (Reject 373=1, 371=1137); 1407
  changes nothing (validation permitting, as above).
- **Inbound `1137="10 "`, `"010"`, `"1O"`** — not a valid ApplVerID value; refused as today's
  unparseable values are (373=5).
- **Inbound header `1128=10` on an application message** — parses to FIX Latest (FR-001). With
  validation off it is tolerated per 033 FR-010, unchanged. With validation on, the session dictionary
  decides, as for any enum value. `FIX50SP2.xml` and the Orchestra file enumerate `10`. `FIXT11.xml`
  enumerates 1128 only up to `9`, so a session validating with it rejects `1128=10` (373=5), today and
  after 097.
- **Inbound header `1156=…`** — no effect on resolution or state (FR-008). With validation on it
  passes when the session dictionary declares 1156 in the header: `FIXT11.xml`, the Orchestra file, and
  the FIX50/SP1/SP2 framing surface (081). Otherwise it is rejected as an undeclared tag.
- **Registry given two dictionaries with the same session version** — unchanged, last writer wins (not
  FR-010's case and not this feature's).
- **FIX-Latest-only registry asked for FIX50SP2** — the FIX Latest dictionary answers (FR-006, one-way
  fallback): a FIX-Latest-only acceptor admits `1137=9` as today, and a session whose own default is `"9"`
  on such an engine opens as today. The reverse never falls back: a FIX50SP2-only registry asked for FIX
  Latest has no dictionary, so `1137=10` is refused with Reject 373=5.
- **A session whose own dictionary is FIX Latest, configured `default_appl_ver_id = "9"`** — it sends
  `1137=9` and negotiates FIX50SP2. Its messages reify under the FIX50SP2 arm, so they come back as
  `v50sp2`-labelled handles, and MsgTypes outside that arm fail loud. One example is XMLnonFIX
  (35=n), which the FIX50SP2 arm does not define: it gets `dict_reify_unknown_msg_type` under `"9"`
  and reifies as `application` / `vlatest` under `"10"`, for the same FIX Latest dictionary. The
  `"9"` outcome is unchanged from today.
  Such a session negotiates FIX Latest only if it is configured `"10"` (B-097-4).
- **Both dictionaries registered** — each version is answered by its own (exact match first).
- **Advertised version differs from the session's own dictionary** (e.g. `1137=10` on a session whose
  dictionary is FIX50SP2, the engine holding both) — admitted, as for every version today; the session
  validates with its own dictionary, so FIX-Latest-only MsgTypes from that peer are rejected by validation.
  Unchanged rule, not a 097 behaviour.
- **Config token** — `default_appl_ver_id = "10"` is accepted; any other new token is not added.
- **A consumer's exhaustive `switch` over `application_version`** — a new enumerator is source-visible
  to a `-Wswitch` build; this is an additive public C++ change, disclosed (FR-011).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001 (parse)**: The ApplVerID value `"10"` MUST resolve to a new application version, FIX Latest,
  wherever an ApplVerID value is parsed (DefaultApplVerID(1137) on Logon, ApplVerID(1128) in a header).
  Every value that resolves today MUST resolve unchanged; every value refused today other than `"10"`
  MUST stay refused with the same error.
- **FR-002 (render)**: Rendering the FIX Latest application version for the wire MUST produce `"10"`.
  Rendering MUST stay injective over all application versions.
- **FR-003 (config)**: The session configuration's `default_appl_ver_id` MUST accept the token `"10"` and
  map it to FIX Latest; the diagnostic for an unknown token MUST name the accepted range including `"10"`.
- **FR-004 (dictionary → version)**: A dictionary whose session version is FIX Latest MUST map to the FIX
  Latest application version; FIX50SP2 MUST keep mapping to FIX50SP2.
- **FR-005 (registry)**: The registry MUST hold a FIX50SP2 and a FIX Latest dictionary at once, each in its
  own slot, and MUST NOT abort or write a diagnostic for that combination. The FR-010 abort is removed.
- **FR-006 (one-way fallback)**: A lookup MUST return the dictionary that maps to the requested version
  when one is registered (exact match first). When FIX50SP2 is requested and no FIX50SP2 dictionary is
  registered, the FIX Latest dictionary, if registered, MUST answer — today's shipped answer, kept
  (`OrchestraFR010Guard.SingleDictConfigsSucceed`'s FIX-Latest-alone arm stays as it is). When FIX Latest
  is requested and no FIX Latest dictionary is registered, the lookup MUST return "no dictionary"; no other
  version falls back to anything (Clarifications, Q1).
- **FR-007 (Logon)**: The FIXT acceptor's DefaultApplVerID check and the `open()` check of the session's
  own default MUST treat FIX Latest like every other application version: serviceable iff FR-001 parses
  and FR-005/FR-006 return a dictionary from the engine's registry. Serviceability stays engine-wide; it
  does not consult the session's own dictionary. The initiator MUST record a peer's `1137=10` as FIX Latest. The
  Reject shape for an unserviceable value is unchanged. The three session paths differ and are stated
  separately (research R-6):
  - `open()` looks up the session's already-typed configured default in the registry;
  - the acceptor parses the peer's 1137, then looks it up;
  - the initiator parses the peer's 1137 and records it, with no registry lookup.

  So the one-way fallback (FR-006) reaches `open()` and acceptor admission, never initiator recording.
- **FR-008 (extension pack tags — no new rule)**: fixpp MUST add no DefaultApplExtID(1407)- or
  ApplExtID(1156)-specific rule. 1407 and 1156 MUST NOT affect version resolution, serviceability or
  session state. Whether a validated inbound frame carrying either tag passes is decided by the session's own
  dictionary, as for every tag (L-041-2). The vendored `FIXT11.xml` (Logon 1407, header 1156) and the
  Orchestra FIX Latest file declare both. A session dictionary that declares neither rejects them when
  `validate_inbound_messages` is on (Clarifications, Gate A round 1). fixpp MUST NOT originate 1407 or
  1156. It MUST NOT begin originating a header ApplVerID(1128). An application payload's own 1128 or
  1156 is moved into the header and sent as given, as today.
- **FR-009 (reify)**: A message whose resolved application version is FIX Latest MUST NOT yield a
  handle whose application version reads FIX50SP2. Two outcomes, each witnessed separately:
  - (a) **Runtime**: the generated application dispatch MUST gain a FIX Latest arm. The partition is
    reify's own, stated here once (Gate A round 3, G097-019):
    - the seven FIXT admin MsgTypes (`0 1 2 3 4 5 A`, the `is_fixt_admin` set) are intercepted by
      `dict::reify` before application dispatch and keep routing to the FIXT admin dispatch;
    - every other MsgType the FIX Latest dictionary defines, under a FIX Latest resolution, makes
      `dict::reify` return an `owning_message_handle` whose
      `version().application == application_version::vlatest`;
    - a MsgType the FIX Latest dictionary does not define gets the existing
      `dict_reify_unknown_msg_type` (Clarifications, Q2).

    So XMLnonFIX (35=n), which the Orchestra file places in `category="Session"`, is not
    intercepted and reifies as `application` / `vlatest`. This is consistent with the shipped v44 arm
    (35=n reifies as `application` / `v44`), with QuickFIX C++ and QuickFIX/J (whose admin set is
    `0A12345`), and with catalogue row A-034 (35=n reaches `Application::fromApp`). The arm is not
    filtered by message category. The handle is untyped, as for every version; no typed
    `fixpp::vlatest` object is returned at runtime.
  - (b) **Static**: the generated `fixpp::vlatest::owning_<Msg>::which()` MUST return
    `application_version::vlatest`.

  Under `FIXPP_CODEGEN_FIX_LATEST=OFF` no FIX Latest arm is emitted, and FIX Latest falls to the
  outer fail-loud `default`. The anchor `vlatest_dispatch_exclusion_test.cpp` (a1) is rewritten to this
  outcome, and its (a2)/(b) halves are re-derived (one arm per application version whose IR is present).
- **FR-010 (witnesses, RED first)**: Each of FR-001, FR-002, FR-003, FR-005, FR-006, FR-007, FR-008 and
  FR-009 MUST have a named test that fails for the reason the requirement states. It fails on the
  unfixed tree where that tree compiles it and the behaviour is new.
  `OrchestraFR010Guard.BothDictsFailLoud` becomes a coexistence test (it aborts on the unfixed tree).
  FR-008's witness MUST include the false-positive arm: a Logon carrying `1137=10` + `1407=303` against a
  registry WITHOUT FIX Latest is still refused (1407 must not make an unserviceable version serviceable).
  Each FR-008 arm MUST name its session dictionary and carry a control arm (the same frame without
  1407/1156) that passes first (contracts W-7).
  Where a requirement already holds on the unfixed tree, its witness is a characterization pin. This
  covers FR-006's FIX50SP2-from-FIX-Latest fallback, FR-008 with `1137=9` under a session dictionary
  that declares 1407, the dictionary-decided rejects of 1407 and of header 1156 under one that does
  not, and FR-008's no-origination rule. Such a witness is instead shown RED against a named mutant,
  derived from the code path that produces the pinned outcome, that breaks the behaviour it pins
  (contracts). A seeded positive for an instrument proves the instrument counts; it is not the
  witness's RED. The same applies where a witness names the new enumerator and so cannot
  compile on the unfixed tree.
- **FR-011 (compatibility disclosure)**: The new application version is an additive public C++ enumerator;
  the C ABI is unchanged (it carries the raw 1137 string). `dict::version_registry`, and `Engine`, which
  holds it by value, grow by one slot; C++ consumers rebuild (B-097-2; no C++ ABI promise
  pre-release). The B&L records the behaviour changes as
  `B-097-*` rows, BREAKING where a shipped C++ answer changes (the `fixpp::vlatest` typed classes' static
  application version, FR-012(c)). A new limitation `L-097-1` records that only direct C++ can register
  FIX50SP2 and FIX Latest together (TOML loads one dictionary; the C API fills none).
- **FR-012 (the record)**: (a) The constitution's statements that FIX Latest has no distinct ApplVerID and
  that ApplExtID(1156)=303 differentiation is post-1.0 (Article I §1's FIX Latest bullet and post-1.0
  milestone line; Article XVIII §2's v1.2 annotation) are amended under Article XX, riding this
  feature's Gate A, owner-ratified. Each of the three states the post-1.0 residue with the canonical
  sentence of data-model §E.0, verbatim. (b) L-074-1 and L-080-1 move to the closed B&L file naming 097.
  (c) Code comments that state the superseded mapping (`version_profile.hpp`'s `vlatest` comment,
  `version_registry.cpp`'s mapping and FR-010 comments, `tools/codegen` `ir.cpp`'s `vlatest` row and
  `gen_util.hpp`'s `app_version_enum`, `vlatest_dispatch_exclusion_test.cpp`'s header,
  `emit_dispatch.cpp`'s version-set comments and generated comment lines (data-model §B)) are
  rewritten, each naming "097" as the superseding feature. The test comments that state a value range or count
  which 097 makes false by omission are rewritten to point at the mapper rather than restate a count
  (`tests/config/fixtures/pos_fixt_appl_ver_ids.toml`'s header and `tests/config/test_load_happy_path.cpp`'s
  section banner for that fixture; find them with `grep -rn '"2".."9"' tests/config`). The generated
  `fixpp::vlatest` typed classes' static application version follows FR-004 (FIX Latest), so the
  vlatest typed tier and the dispatch arm agree.
  (d) The catalogue rows S-025 and S-043 and D-011's note gain FIX Latest / ApplVerID 10. D-011's Tests
  cell renames the 074 guard tests that W-4 renames or deletes. Its case count is marked "at 074" and
  is not recomputed. D-011's own residue sentence becomes the canonical one (data-model §E.0), and its
  `v50sp2` / `std::abort` statements are flagged as retired by 097. The coverage-index §4.3.7 / §5.3.3
  / §5.3.5 rows and the DefaultApplVerID(1137) / ApplVerID(1128) rows are updated. The Post-1.0 Gap
  Registry D-011 row carries the canonical sentence and the target "v1.2 (EP-level field back-port;
  Extension Pack negotiation)". The catalogue's 076 partial-progress note, the coverage index's 074
  link note and the live B&L 074 section intro get an in-place 097 SUPERSEDED comment (data-model §F).
  (e) `brain/components/dictionary.md` flags 074's reconcile (frozen `specs/074-orchestra-native-reader/`)
  as superseded in place, naming `097-fix-latest-applverid-10`, and points at the 2c notes of (f).
  (f) `.specify/2c-codegen.md` §4.3 gets an in-place `Updated (097 …)` note at the `application_version`
  enum and at the wire `1128` → `application_version` table (`"10"` → `vlatest`). This follows 2c's
  own `Updated (fixpp#458 …)` precedent. The application-dispatch version set gains `vlatest`. The
  rule is keyed on the claim, not on a phrase (Gate A round 3, G097-020): a note goes at every
  statement in §4.8's runtime-dispatch description and in §6's behavioural contract that gives the
  application switch's version set. Those are §4.8's "Runtime-dispatch switch" paragraph, §4.8's
  `reify_dispatch_application.hpp` sketch (its version list and its outer-default comment), §6.3
  step 3 ("Pass the resolved `application_version` to the dispatch switch") and §6.6 item 8
  ("Runtime-dispatch table generation"). Data-model §C gives each note's text. Left alone, each
  predating 097 and none a statement of the switch's version set inside §4.8 or §6: §6.3's registry
  sentence ("the four codegen-version dictionaries"), which is about the registry, not the switch, and
  was never accurate; §9 seam 15b, which is test-seam prose; the generic "four codegen versions" /
  "~470 cases" wording outside §4.8 and §6 (§1's size bullet, §4.3's `resolved_message_version` sketch
  comment, §7.6's dispatch row); §4.8's `dict::reify` algorithm-sketch comment "(the ~470-case
  application switch)", which gives a count and no version set; and the appendices, which are history.
  (g) The constitution's Sync Impact Report lists every affected catalogue row and spec (data-model §E.4).
  The positive-presence checks of quickstart §4 (contracts W-11) confirm (a)–(f) were made, not only that
  superseded text is gone.
- **FR-013 (no regression)**: No FIX.4.x session, and no FIXT session whose own default is FIX50SP2 or
  older, changes observable behaviour (wire bytes, Rejects, state transitions), except in two places:
  - **(i)** the handling of the inbound value `"10"` wherever it is parsed: DefaultApplVerID(1137) and
    ApplVerID(1128), through admission and reify. It now resolves to FIX Latest, so a `1137=10` peer
    is admitted when the engine holds FIX Latest (FR-007, Clarifications `/speckit-clarify`);
  - **(ii)** registry contents that abort on the unfixed tree (FIX50SP2 together with FIX Latest).

  The claim is finite. It holds when the full existing suite passes unchanged except the tests this
  feature names as rewritten (SC-006, W-10), and the characterization pins W-5 (FIX-Latest-alone
  arm), W-6(c), W-6(f)'s second case, W-7(a)/(d)/(e)/(f) and W-8's phase-2 arms pass.

### Key Entities

- **Application version** — gains FIX Latest (wire `"10"`).
- **Registry** — one slot per application version; FIX Latest's slot is new.
- **Negotiated version profile** — the per-session `{session = FIXT.1.1, default_appl = <negotiated>}`
  value; may now carry FIX Latest. Its size is unchanged.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An inbound Logon with `1137=10` against a FIX-Latest-serving acceptor is established (0
  Rejects), where the unfixed tree refuses it (1 Reject, 373=5).
- **SC-002**: A registry built from the FIX50SP2 and FIX Latest dictionaries is constructed and answers
  both lookups with their own dictionary, where the unfixed tree aborts.
- **SC-003**: Every FR-010 witness is GREEN at the head and shown RED for its stated reason, either on the
  unfixed tree or against its named mutant (contracts' per-witness RED arm).
- **SC-004**: Over all eleven ApplVerID values `"0"`..`"10"` plus the edge-case malformed values,
  parse/render agree with the FIX ApplVerID table and render is injective (one table-driven witness).
- **SC-005**: Zero 1407, zero 1156 and zero header 1128 in every session-originated outbound frame the
  097 session witnesses capture, with FIX Latest and FIX50SP2 defaults both, every frame family of
  US1 AS-3 captured. The counting oracle is shown non-zero against a seeded frame carrying each tag,
  and the witness is shown RED against production mutants that make an admin builder originate each
  tag (contracts W-8).
- **SC-006**: The full existing test suite passes unchanged, except the tests FR-006/FR-009/FR-010 name
  as rewritten.
- **SC-007**: L-074-1 and L-080-1 absent from the live B&L; constitution amended with owner sign-off.

## Assumptions

- The vendored FIX Latest dictionary stays EP303; ApplVerID 10 identifies "FIX Latest" generically, not a
  specific EP (FIX Session Layer §11.2; the EP is a separate 1407/1156 axis this feature ignores).
- fixpp remains pre-release; an additive enumerator and a declared BREAKING answer change are
  permitted (`[const §X.7]` governs the C ABI only, which does not change).
- Per-message ApplVerID(1128) override handling (S-026, row 7) is not changed beyond FR-001's parse;
  nothing here fixes its design.

## Out of Scope

- Extension Pack negotiation: any behaviour for 1407/1156/1406 (fixpp reads none of them); RefApplExtID
  in NoMsgTypes; emitting 1407.
- A validator rule that accepts 1407/1156 whatever the session dictionary declares (rejected by the
  owner's Gate A round-1 ruling; validation stays dictionary-decided, L-041-2).
- Emitting per-message ApplVerID(1128); S-026's accessor and override semantics (row 7).
- Selecting the inbound validation dictionary from the negotiated version (each session still validates
  with its own `SessionConfig::dictionary`).
- Adding ApplVerID 10 to the vendored `FIXT11.xml` enums (fixpp's 1137 is an enum-less STRING, as in
  QuickFIX C++; the 1128 enum is a QuickFIX-sourced file kept byte-identical).
- Extension Packs beyond EP303 (post-v1.0).

## Normative References

- **Spec refs** (`[const §VI.5]` form, titles as `spec/coverage-index.md`'s FIX-SL section table spells
  them):
  - `[FIX-SL §4.3.7] Specifying application version (DefaultApplVerID 1137 / FIXT)` (S-025, S-043);
  - `[FIX-SL §5.3.3] Session default application version identification (DefaultApplVerID 1137)`
    (S-025);
  - `[FIX-SL §5.3.5] Explicit application version per message (ApplVerID 1128)` (S-026; FR-001's parse
    only);
  - `[FIX-SL §5.3.6] Use of extension packs` (out of scope beyond FR-008; Extension Pack negotiation
    stays post-1.0, data-model §E.0).

  The "FIXT.1.1 (FIXT)" table indexes the same sections by concept ("DefaultApplVerID(1137) on Logon —
  default app version", "ApplVerID(1128) per message — per-message app version override").
- **Supporting citations** (no coverage-index entry, cited, not vendored): FIX Session Layer with
  Errata, November 2020, §11.2 (p77), the ApplVerID value list (`FIXLatest = 10`); the FIXT 1.1
  specification (Logon 1137/1407; header 1128/1156).
- **FIX Orchestra**: `dictionaries/orchestra/OrchestraFIXLatest.xml` — ApplVerID codeset (`FIXLatest`,
  `value="10"`).
- **Catalogue rows**: S-025, S-043, D-011 (annotated, FR-012(d)); A-035..A-065 (one sentence each,
  data-model §F); no `backlog` row closes.
- **Constitution**: `[const §I.1]`, `[const §XVIII.2]` amended (FR-012(a)); `[const §XVIII.1]`,
  `[const §XVIII.5]` read and unchanged (data-model §E.4); `[const §XX.1]`–`[const §XX.4]` the amendment
  process; `[const §XVI.3]`, `[const §XVI.4]` the `/clarify` order deviation and the `/analyze` drift
  check (plan Constitution Check); `[const §XVI.6]` mutation proofs in a scratch copy;
  `[const §XVII.1]`, `[const §XVII.7]`, `[const §XVII.8]` Gate A, local build, verify; `[const §X.7]`
  (C ABI unchanged).
- **Design doc**: `.specify/2c-codegen.md` §4.3, §4.8, §6.3 and §6.6 (annotated in place, FR-012(f)).
- **B&L**: L-074-1, L-080-1 (retired); L-041-2 (unchanged; decides FR-008's validation outcome); new
  `B-097-*`, `L-097-1`.
- **Precedent**: `specs/033-fixt-fix50sp2-session/` (the DefaultApplVerID gates); `specs/074-orchestra-native-reader/`
  (the superseded reconcile, FR-010); `specs/080-orchestra-runtime-load/` (L-080-1).
- **Evidence**: parent `research/findings/v1-row4b-applverid-10-sweep-2026-10-29.md`.
