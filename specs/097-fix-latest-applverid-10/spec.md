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
3. A message resolved under a FIX Latest session reifies as a `fixpp::v50sp2` typed owner
   (`tests/codegen/vlatest_dispatch_exclusion_test.cpp` (a1) pins this as a regression anchor "so a
   future fix is deliberate"). This feature is that fix.

**Peer behaviour this feature must stay compatible with** (same findings file): QuickFIX C++, QuickFIX/J
and QuickFIX/Go all **accept** an inbound Logon with `1137=10` (1137 is an enum-less STRING in their
FIXT11.xml), all can be configured to **send** it, and **none reads DefaultApplExtID(1407) or
ApplExtID(1156)** on any path. QuickFIX C++ and QuickFIX/Go **reject a header `1128=10`** (their 1128
enum stops at 9). The FIX documents mandate no receiver behaviour for an unsupported 1407/1156 and state
engines have "no requirement … to dynamically configure and apply Extension Packs".

**Owner ruling, 2026-10-29:** ApplVerID 10 in both directions; FIX Latest gets its own application
version and registry slot; the abort goes; inbound 1407/1156 are accepted and ignored; fixpp emits no
1407; 074's reconcile is recorded as an erratum.

## Clarifications

### Session 2026-10-29 (orchestrator, from the owner ruling above)

- Q: Does FIX Latest get its own application version, or a composite (ApplVerID 9, ApplExtID 303) key? →
  A: Its own application version, wire value `"10"`. 074 rejected a FIX Latest application version only
  because it would have rendered as `"9"` and made the wire map non-injective; with `"10"` distinct, that
  objection does not apply. The composite key is rejected: no QuickFIX engine reads 1407/1156, so it
  would differentiate nothing for a real peer.
- Q: What does fixpp do with inbound 1407 / 1156? → A: Accept and ignore, on every path (the behaviour of
  all three QuickFIX engines). No Reject, no disconnect, no effect on version resolution.
- Q (Q2): A message resolved to FIX Latest — a FIX Latest reify-dispatch arm, or an explicit error? →
  A: The arm (orchestrator, technical, 2026-10-29). Measured at `23895374`: the generated
  `_dispatch/reify_dispatch_application.hpp` (1027 lines, 77 KB) includes no per-version `Reify.hpp`;
  each arm is a MsgType switch minting a handle from version metadata, so a FIX Latest arm adds one more
  generated switch, not the 8 MB `vlatest/Reify.hpp`. The error option would only defer the same switch
  to a follow-up. When the FIX Latest codegen tier is disabled (`FIXPP_CODEGEN_FIX_LATEST=OFF`) there is
  no arm and FIX Latest falls to the existing fail-loud outer default.
- Q (Q1): An engine holding only the FIX Latest dictionary, asked to serve a peer's `1137=9` — strict
  (refuse) or fall back to the FIX Latest dictionary? → A: **Strict** (owner, 2026-10-29). One
  deterministic ending; matches QuickFIX/J, which selects the dictionary per ApplVerID. To serve both, an
  operator registers both dictionaries, which FR-005 makes possible. The changed answer is declared
  BREAKING in the B&L (FR-011); fixpp is pre-release.

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
   sends its Logon, **Then** the Logon carries `1137=10`, and no message the session emits carries
   1407 or a header 1128.
4. **Given** a FIXT acceptor whose registry holds no FIX Latest dictionary, **When** a Logon with
   `1137=10` arrives, **Then** it is refused exactly as any other unserviceable version is today
   (Reject 373=5, 371=1137, then disconnect).

---

### User Story 2 — One engine serves FIX50SP2 and FIX Latest (Priority: P1)

An operator configures one engine with both the FIX50SP2 dictionary and the FIX Latest dictionary.
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
   **Then** both are established.

---

### User Story 3 — A FIX Latest message is typed as FIX Latest (Priority: P2)

An application reifies an inbound message whose resolved application version is FIX Latest. It never
receives a `fixpp::v50sp2`-labelled owner for it.

**Why this priority**: the silent mis-label is pinned today only as an anchor; once FIX Latest is a real
application version the anchor's premise is gone, and the outcome must be stated.

**Independent Test**: reify a FIX Latest MsgType under a FIX Latest profile; inspect the handle's
version. On the unfixed tree no input can resolve to FIX Latest at all (FR-001), so the RED is the
anchor (a1)'s FIX50SP2-labelled owner under the profile a FIX Latest session negotiates today.

**Acceptance Scenarios**:

1. **Given** a message of a MsgType the FIX Latest dictionary defines, resolved to FIX Latest, **When**
   it is reified, **Then** the owner's application version reads FIX Latest and its type is the
   `fixpp::vlatest` class for that MsgType.
2. **Given** a MsgType FIX Latest does not define, resolved to FIX Latest, **When** it is reified,
   **Then** the existing unknown-MsgType error is returned.
3. **Given** a build with the FIX Latest codegen tier disabled, **When** a message resolved to FIX Latest
   is reified, **Then** the existing fail-loud default error is returned, never a FIX50SP2 owner.

---

### User Story 4 — The record says what is true (Priority: P2)

A maintainer reading the constitution, B&L, catalogue and code comments about FIX Latest's wire identity
finds ApplVerID 10, and finds 074's reconcile flagged as an erratum naming this feature.

**Independent Test**: grep the live surfaces listed in FR-012 for the superseded claim; every remaining
occurrence is flagged as superseded in place or lives in a frozen record that a component page flags.

**Acceptance Scenarios**:

1. **Given** the merged tree, **When** the constitution's FIX Latest clauses are read, **Then** they state
   that FIX Latest's application version is ApplVerID 10 (delivered by 097) and that only Extension Pack
   negotiation beyond accept-and-ignore remains post-1.0.
2. **Given** the live B&L, **When** it is read, **Then** L-074-1 and L-080-1 are absent from it and
   present in the closed file with the retiring feature.

### Edge Cases

- **Inbound `1137=10` and `1407=303` together** — accepted; 1407 has no effect (FR-008).
- **Inbound `1407` with no `1137`** — the existing missing-1137 rule fires (Reject 373=1, 371=1137); 1407
  changes nothing.
- **Inbound `1137="10 "`, `"010"`, `"1O"`** — not a valid ApplVerID value; refused as today's
  unparseable values are (373=5).
- **Inbound header `1128=10` on an application message** — parses to FIX Latest (FR-001); today it is
  tolerated per 033 FR-010 and that tolerance is unchanged.
- **Inbound header `1156=…`** — accepted and ignored (FR-008).
- **Registry given two dictionaries with the same session version** — unchanged, last writer wins (not
  FR-010's case and not this feature's).
- **FIX-Latest-only registry asked for FIX50SP2** — no dictionary (FR-006, strict): a FIX-Latest-only
  acceptor refuses `1137=9` with Reject 373=5; the reverse (FIX50SP2-only, `1137=10`) is refused the same
  way. A FIXT session whose own default is FIX50SP2 but whose engine holds only FIX Latest now fails the
  `open()` check (BREAKING, FR-011).
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
- **FR-006 (strict lookup)**: Each application version MUST be answered only by a dictionary that maps to
  it (FR-004). A registry holding only the FIX Latest dictionary MUST answer FIX50SP2 with "no dictionary",
  and one holding only FIX50SP2 MUST answer FIX Latest with "no dictionary" — no fallback in either
  direction (Clarifications, Q1). This changes a shipped answer: `OrchestraFR010Guard.SingleDictConfigsSucceed`'s
  FIX-Latest-alone arm, which pins the FIX50SP2 lookup to the FIX Latest dictionary, is rewritten to the
  strict answer.
- **FR-007 (Logon)**: The FIXT acceptor's DefaultApplVerID check and the `open()` check of the session's
  own default MUST treat FIX Latest like every other application version: serviceable iff FR-001 parses
  and FR-005/FR-006 return a dictionary. The initiator MUST record a peer's `1137=10` as FIX Latest. The
  Reject shape for an unserviceable value is unchanged.
- **FR-008 (extension pack tags)**: Inbound DefaultApplExtID(1407) on a Logon and ApplExtID(1156) in any
  header MUST be accepted and MUST NOT affect version resolution, serviceability, validation outcome, or
  session state. fixpp MUST NOT emit 1407. fixpp MUST NOT begin emitting a per-message ApplVerID(1128).
- **FR-009 (reify)**: A message whose resolved application version is FIX Latest MUST NOT yield an owner
  whose application version reads FIX50SP2. The generated application dispatch MUST gain a FIX Latest
  arm returning `fixpp::vlatest` owners for every MsgType of the FIX Latest dictionary, and the existing
  fail-loud error otherwise (Clarifications, Q2). The anchor `vlatest_dispatch_exclusion_test.cpp` (a1)
  is rewritten to this outcome, and its (a2)/(b) halves re-derived (one arm per application version).
- **FR-010 (witnesses, RED first)**: Each of FR-001, FR-002, FR-003, FR-005, FR-006, FR-007, FR-008 and
  FR-009 MUST have a named test that fails on the unfixed tree for the reason the requirement states.
  `OrchestraFR010Guard.BothDictsFailLoud` becomes a coexistence test (it aborts on the unfixed tree).
  FR-008's witness MUST include the false-positive arm: a Logon carrying `1137=10` + `1407=303` against a
  registry WITHOUT FIX Latest is still refused (1407 must not make an unserviceable version serviceable).
  Where a requirement already holds on the unfixed tree (FR-008 with `1137=9`, which no QuickFIX-derived
  path rejects today), its witness is a characterization pin and is instead shown RED against a named
  mutant that makes the tag change the outcome.
- **FR-011 (compatibility disclosure)**: The new application version is an additive public C++ enumerator;
  the C ABI is unchanged (it carries the raw 1137 string). The B&L records the behaviour change(s) as
  `B-097-*` rows, BREAKING where FR-006 changes a shipped answer.
- **FR-012 (the record)**: (a) The constitution's statements that FIX Latest has no distinct ApplVerID and
  that ApplExtID(1156)=303 differentiation is post-1.0 (Article I §1's FIX Latest bullet and post-1.0
  milestone line; Article XVIII §2's v1.2 annotation) are amended under Article XX, riding this
  feature's Gate A, owner-ratified. (b) L-074-1 and L-080-1 move to the closed B&L file naming 097.
  (c) Code comments that state the superseded mapping (`version_profile.hpp`'s `vlatest` comment,
  `version_registry.cpp`'s mapping and FR-010 comments, `tools/codegen` `ir.cpp`'s `vlatest` row and
  `gen_util.hpp`'s `app_version_enum`, `vlatest_dispatch_exclusion_test.cpp`'s header) are rewritten,
  each naming "097" as the superseding feature. The generated `fixpp::vlatest` typed classes' static
  application version follows FR-004 (FIX Latest), so the vlatest typed tier and the dispatch arm agree. (d) The catalogue rows S-025 and S-043 and D-011's note gain FIX Latest / ApplVerID 10;
  coverage-index §4.3.7 / §5.3.3 / §5.3.5 entries are updated. (e) `brain/components/dictionary.md`
  flags 074's reconcile (frozen `specs/074-orchestra-native-reader/`) as superseded in place.
- **FR-013 (no regression)**: No FIX.4.x session, and no FIXT session whose default is FIX50SP2 or older,
  changes observable behaviour (wire bytes, Rejects, state transitions), except as FR-006 decides.

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
- **SC-003**: Every FR-010 witness is RED on the unfixed tree and GREEN at the head, each shown, with the
  RED for its stated reason.
- **SC-004**: Over all eleven ApplVerID values `"0"`..`"10"` plus the edge-case malformed values,
  parse/render agree with the FIX ApplVerID table and render is injective (one table-driven witness).
- **SC-005**: Zero emitted 1407 and zero emitted header 1128 in every outbound frame the 097 session
  witnesses capture, FIX Latest and FIX50SP2 defaults both (the counting oracle shown non-zero against
  a seeded frame carrying each tag).
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

- Extension Pack negotiation: any behaviour for 1407/1156/1406 beyond accept-and-ignore; RefApplExtID in
  NoMsgTypes; emitting 1407.
- Emitting per-message ApplVerID(1128); S-026's accessor and override semantics (row 7).
- Selecting the inbound validation dictionary from the negotiated version (each session still validates
  with its own `SessionConfig::dictionary`).
- Adding ApplVerID 10 to the vendored `FIXT11.xml` enums (fixpp's 1137 is an enum-less STRING, as in
  QuickFIX C++; the 1128 enum is a QuickFIX-sourced file kept byte-identical).
- Extension Packs beyond EP303 (post-v1.0).

## Normative References

- **FIX documents** (cited, not vendored): FIX Session Layer with Errata, November 2020 — §11.2 (ApplVerID
  values), §5.3.3 (session default application version), §5.3.5 (explicit application version per
  message), §5.3.6 (extension packs); FIXT 1.1 specification (Logon 1137/1407; header 1128/1156).
- **FIX Orchestra**: `dictionaries/orchestra/OrchestraFIXLatest.xml` — ApplVerID codeset (`FIXLatest`,
  `value="10"`).
- **Catalogue rows**: S-025, S-043, D-011 (annotated, FR-012(d)); no `backlog` row closes.
- **Constitution**: `[const §I.1]`, `[const §XVIII.2]` amended (FR-012(a)); `[const §XX.1]`–`[const §XX.4]`
  the amendment process; `[const §XVII.1]`, `[const §XVII.7]`, `[const §XVII.8]` Gate A, local build,
  verify; `[const §X.7]` (C ABI unchanged).
- **B&L**: L-074-1, L-080-1 (retired); new `B-097-*`.
- **Precedent**: `specs/033-fixt-fix50sp2-session/` (the DefaultApplVerID gates); `specs/074-orchestra-native-reader/`
  (the superseded reconcile, FR-010); `specs/080-orchestra-runtime-load/` (L-080-1).
- **Evidence**: parent `research/findings/v1-row4b-applverid-10-sweep-2026-10-29.md`.
