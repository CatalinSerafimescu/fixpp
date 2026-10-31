# Feature Specification: Dialect overlay — venue additions layered on a base dictionary (D-009)

**Feature Branch**: `098-dialect-overlay`

**Created**: 2026-10-31

**Status**: Draft

**Input**: User description: "D-009 custom dictionary extension (dialect overlay), REMAINING-WORK FUNCTIONAL
TRACK row 5 (owner, 2026-10-30: build it). A user layers venue-specific additions — custom fields,
messages, components, repeating groups and Length+Data pairs on non-standard tags — onto a standard base
FIX dictionary, per session, without editing the vendored XML. Owner rulings 2026-10-31: QuickFIX-equivalent
grammar; C++ + C ABI + TOML; merge in the loader."

## Terminology

- **Base dictionary** — a complete QuickFIX-XML dictionary file, e.g. the vendored
  `dictionaries/FIX44.xml`, loaded today by `dict::XmlLoader::load` / `dict::load_any`.
- **Overlay** — a QuickFIX-XML document with the base's `<fix>` root and schema that holds **only
  additions**: new `<field>` definitions and enum `<value>`s, new or extended `<header>`, `<trailer>`,
  `<component>`, `<message>` and `<group>` elements. It is never loaded on its own.
- **Merged dictionary** — the ordinary `dict::Dictionary` that results from applying one overlay to one
  base. Nothing downstream can tell it apart from a dictionary loaded from a file.
- **Equivalent edited XML** — the complete file a QuickFIX user would have written by copying the base
  and hand-editing the same additions into it. It is the reference this feature is measured against.
- **Standard pair tag** — a tag named in fixpp's standard Length+Data table
  (`include/fixpp/core/length_data_pairs.hpp`). A dictionary pair is honoured only when neither of its
  tags is standard (`include/fixpp/wire/dict_hooks.hpp`, B-426-3 precedence).

## Context: why this feature exists

**What QuickFIX does.** Read at QuickFIX C++ `386ce46e`, QuickFIX/J `bc7e8b23` and QuickFIX/Go
`eff945dd` (2026-10-31; citations in the parent repo's research notes for this feature). None of the
three has an overlay, a dialect or a merge mechanism at runtime. A user who needs venue fields edits a copy
of the dictionary XML and points the session at it (`DataDictionary`, or `TransportDataDictionary` +
`AppDataDictionary`). The dictionary decides parse structure (groups, header/trailer membership, DATA
length) and what inbound validation rejects. Load errors stop session start-up. QuickFIX/J's own guide
documents exactly this workflow, followed by regenerating typed classes.

**What fixpp has today** (verified at `8319c206`):

- A user can already load a complete edited XML through `XmlLoader::load`, `dict::load_any`, the C ABI
  `fixpp_dict_load_from_xml`, or TOML `[dictionary] kind = "path"`. That is the QuickFIX model, and it
  works, within the limits below.
- `SessionConfig::dialect_overlay` (`include/fixpp/session/session_config.hpp`) is a public
  `std::shared_ptr<const dict::DialectOverlay>` field that **nothing reads**. `DialectOverlay` is
  forward-declared and defined nowhere. `Dictionary::with_overlay`, `was_dialect_promoted` and
  `XmlLoader::load_overlay` do not exist; their headers name them as deferred.
- The TOML key `dialect_overlay` is recognised and refused as "not yet supported"
  (`src/config/toml_config_loader.cpp`, `kDeferred`).
- TOML `[dictionary]` reaches only `EngineConfig::dictionaries` (`src/config/selector_resolver.cpp`).
  Nothing outside `src/capi/config.cpp` assigns `SessionConfig::dictionary`, so a TOML-only setup cannot
  open a session unless host code copies the dictionary across.
- `XmlLoader` enforces no size, depth or entity limit, and has no component-cycle guard; the C error
  `FIXPP_ERR_DICT_LIMIT_EXCEEDED` exists and is never returned.
- Receive-side Length+Data pairing is dictionary-driven since fixpp#426: a pair a dictionary declares on
  two non-standard tags is honoured by every scanner. Send-side `body_builder::field_data` pairs through
  the standard table only, by the 091 Gate A owner ruling (091 R-2 / FR-009).
- Repeating-group membership has been dictionary-bounded on the shipped receive path since 066
  (B-066-1). B-066-1 names `dialect_overlay` as the *planned* way to make an undeclared in-group field a
  member, and says it is not shipped.

**What the signed-off design said, and what changes.** `.specify/2c-codegen.md` §4.3, §4.4, §4.4.1,
§4.4.2 and §6.4 designed a `DialectOverlay` value type merged at session open by
`Dictionary::with_overlay`, a three-way conflict policy, and a restricted grammar: no Length+Data pairs,
no new groups on existing messages, no new members in existing groups. Those restrictions rested on two
premises that are now false: a static Length+Data table in the Iter-mode iterator (replaced by
`dict_hooks`, fixpp#426) and codegen-bound group structure (replaced by dictionary-driven membership,
066/082/083). The owner's rulings below supersede 2c on the grammar, the attachment point and the policy
knob.

## Clarifications

### Session 2026-10-31 (owner, before `/speckit-specify`)

Asked with the QuickFIX C++/J/Go source above already read.

- Q: What may an overlay add? → A: **QuickFIX-equivalent.** New fields, messages, components and groups;
  fields added to existing messages; members added to existing groups; an optional field made required.
  Never a change to a base field's number or type, and never a removal. Custom Length+Data pairs whose two
  tags are both non-standard are allowed. `field_data` stays standard-pairs-only (091 R-2 / FR-009 not
  reversed). Supersedes 2c §4.4.1; delivers B-066-1's extension promise.
- Q: Which surfaces ship? → A: **C++ API, C ABI (additive; Python follows through SWIG), and the
  per-session TOML `dialect_overlay` key**, including wiring TOML's dictionary into each session's
  `SessionConfig::dictionary`, without which the key could not take effect.
- Q: How does an overlay attach to a session? → A: **Merge in the loader.** A loader call returns a new
  ordinary `Dictionary`, which the user places in `SessionConfig::dictionary` (and in
  `EngineConfig::dictionaries` where wanted). The never-read `SessionConfig::dialect_overlay` field is
  deleted, a disclosed pre-release breaking change to the public C++ API. Reason: `Session::open()`
  checks a supplied snapshot's `source()` against `SessionConfig::dictionary` by pointer identity, and a
  merge inside `open()` would break that check and put an allocating merge on the open path.

### Session 2026-10-31 (orchestrator, technical, during `/speckit-specify`)

- Q: Is a conflict-policy knob (2c's `OverlayWins` / `BaseWins` / `Reject`) needed? → A: **No.** The
  grammar ruling forbids changing or removing base content, so the only conflict outcome is refusal.
  An overlay that would change a base definition is a load error (FR-007). This matches QuickFIX's
  `ConfigError` on a field name defined twice.
- Q: Is the result checked against an independent reference? → A: Yes. The equivalent edited XML is
  hand-written and checked in, never generated from the merge (FR-017, SC-001).

## User Scenarios & Testing *(mandatory)*

### User Story 1 — A session understands a venue's dialect without an edited vendored file (Priority: P1)

A firm connects to a venue whose rules of engagement add custom tags to NewOrderSingle and
ExecutionReport, a custom tag inside the Parties group, a private MsgType, a custom enum value on a
standard field, and a custom Length+Data pair. The firm writes these additions as an overlay file, loads
the vendored FIX 4.4 dictionary with that overlay, and gives the result to the session. Inbound messages
carrying the custom tags parse, group correctly and pass validation exactly as they would with a
hand-edited copy of FIX44.xml.

**Why this priority**: This is D-009 and COM-011. Without it the only route is to maintain an edited
copy of a vendored dictionary per venue, which diverges from upstream on every dictionary update.

**Independent Test**: Load base + overlay, and load the checked-in equivalent edited XML. Compare the two
dictionaries store by store, then drive the same inbound frames through a session on each and compare
the outcomes.

**Acceptance Scenarios**:

1. **Given** the FIX 4.4 base and an overlay adding custom field 5001 to NewOrderSingle, **When** an
   inbound 35=D carrying 5001 arrives at a session validating inbound messages, **Then** it is accepted
   and 5001 is readable by tag, exactly as on a session using the equivalent edited XML; on a session
   using the unmodified base it is refused as it is today.
2. **Given** an overlay adding member 5002 to the Parties group, **When** an inbound message carries 5002
   inside a Parties entry, **Then** 5002 is read as a member of that entry (not as a field after the
   group), matching the equivalent edited XML (B-066-1's extension story).
3. **Given** an overlay declaring a new MsgType `U1` with a required field, **When** a `U1` arrives without
   that field, **Then** the session refuses it as it would refuse a standard message missing a required
   field; with the field present, it is accepted and reaches the application callback.
4. **Given** an overlay declaring the custom pair 5010 (LENGTH) / 5011 (DATA) on a message, **When** an
   inbound message carries a 5011 value containing SOH, **Then** the value is read by its count, intact.
5. **Given** an overlay that makes an Optional base field Required on one MsgType, **When** that message
   arrives without the field, **Then** it is refused; other MsgTypes are unaffected.
6. **Given** an overlay adding enum value `"Z"` to OrdType(40), **When** a 35=D with 40=Z arrives at a
   validating session, **Then** it is accepted, where the base alone refuses it.

---

### User Story 2 — A bad or hostile overlay is refused at load, and the base is never silently altered (Priority: P1)

An operator ships an overlay with a mistake: it redefines a standard tag with another type, or relaxes a
required field, or references a field it never defined, or is many megabytes of nested elements. The load
fails at start-up with an error that names the cause. Nothing is half-applied, and no session runs with a
dictionary the operator did not intend.

**Why this priority**: The overlay is a new XML input. Silent acceptance of a conflicting definition would
change wire behaviour without notice; an unbounded document is a denial-of-service vector at start-up.

**Independent Test**: A table of malformed and oversized overlays, each expected to fail with its named
error; the base loaded alone before and after is unchanged.

**Acceptance Scenarios**:

1. **Given** an overlay that declares tag 44 with a type other than the base's PRICE, **When** it is
   loaded, **Then** the load fails with a conflict error naming tag 44.
2. **Given** an overlay that marks a base-Required field as not required, **When** it is loaded, **Then**
   the load fails with a conflict error naming the field and the MsgType.
3. **Given** an overlay larger than the byte cap, deeper than the depth cap, carrying a DOCTYPE or entity
   declaration, or adding more entries than an entry cap allows, **When** it is loaded, **Then** the load
   fails with a limit error naming the cap, and through the C ABI it returns
   `FIXPP_ERR_DICT_LIMIT_EXCEEDED`.
4. **Given** an overlay whose component references form a cycle with base components, **When** it is
   loaded, **Then** the load fails with a schema error naming the cycle, and does not recurse without
   bound.
5. **Given** an overlay whose `<fix>` version differs from the base's, **When** it is loaded, **Then** the
   load fails with a version-mismatch error.

---

### User Story 3 — A TOML-configured session names its overlay (Priority: P2)

An operator configures the engine in TOML. Each `[[session]]` may name a `dialect_overlay` file. Every
session gets its dictionary from the configuration: the base for a session without an overlay, the
merged dictionary for a session with one.

**Why this priority**: The TOML key is the operator-facing surface. It cannot work until TOML sessions
receive a dictionary at all, which they do not today.

**Independent Test**: Load a TOML file with two sessions, one with an overlay and one without, and check
each session's configured dictionary and its inbound behaviour on a custom tag.

**Acceptance Scenarios**:

1. **Given** a TOML file with `[dictionary]` and a `[[session]]` with no overlay, **When** it is loaded,
   **Then** that session's `SessionConfig::dictionary` is the base dictionary, and the session opens
   without host code setting it.
2. **Given** a `[[session]]` with `dialect_overlay = "venue.xml"`, **When** the file is loaded, **Then**
   that session's dictionary is the merged dictionary, and the other sessions are unaffected.
3. **Given** a `dialect_overlay` path that does not exist or an overlay that fails to merge, **When** the
   file is loaded, **Then** the load reports a diagnostic at that session's `dialect_overlay` key path.
4. **Given** `dialect_overlay` at root scope, **When** the file is loaded, **Then** it is refused with a
   diagnostic saying the key is session-scoped.

---

### User Story 4 — A C or Python program loads a base with an overlay (Priority: P2)

A C or Python host loads a base dictionary with an overlay through the C ABI and sets it on a session
config, as it does today with a plain dictionary.

**Why this priority**: The C ABI is a supported surface (Article X) and Python sits on it. The entry point
is additive.

**Independent Test**: A C-ABI test and a Python test each load base + overlay, open a session and read a
custom tag from an inbound message.

**Acceptance Scenarios**:

1. **Given** a base path and an overlay path, **When** the C-ABI load-with-overlay function is called,
   **Then** it returns a dictionary handle usable with `fixpp_session_config_set_dictionary`.
2. **Given** an overlay that exceeds a cap, **When** loaded through the C ABI, **Then** the call returns
   `FIXPP_ERR_DICT_LIMIT_EXCEEDED` and no handle; a conflicting or malformed overlay returns a documented
   non-OK code and no handle.
3. **Given** the Python bindings, **When** the same function is called through SWIG, **Then** the
   behaviour matches the C ABI.

---

### User Story 5 — The records say what is true (Priority: P3)

A reader of the catalogue, the B&L, the 2c design doc and the brain finds the shipped overlay described
correctly, and the superseded 2c design flagged in place.

**Why this priority**: The 2c overlay design is signed off and was never built. Left unflagged, it would
lead a later reader to "fix" the code back to it (issue #334's failure mode).

**Independent Test**: Read the named records; run the brain freshness and B&L delta checks.

**Acceptance Scenarios**:

1. **Given** the merged feature, **When** the catalogue is read, **Then** D-009 and COM-011 are `done`
   with their witnesses, and L-002-2 sits in the closed B&L file.
2. **Given** `.specify/2c-codegen.md`, **When** §4.3, §4.4, §4.4.1, §4.4.2 and §6.4 are read, **Then**
   each carries an in-place "Updated (098-dialect-overlay)" note naming what 098 replaced.

---

### Edge Cases

- **Empty overlay** (`<fix>` with the base's version and no children): the merged dictionary is
  equivalent to the base.
- **Overlay redeclares a base field identically** (same number, name and type): accepted; enum values
  it lists are added to the base's (a value already present is a no-op).
- **Same number, different name**, or **same name, different number**: conflict error.
- **Unknown field type string**: refused exactly as the base loader refuses it.
- **Reference to an undefined field or component**: refused; a name may resolve to the base or to the
  overlay.
- **Existing MsgType redeclared with a different name**: conflict error.
- **Required on an existing child**: `required="Y"` promotes an Optional child; `required="N"` on a
  Required base child is a conflict error; an absent attribute leaves the child's rule unchanged.
- **New member in a group defined inside a component**: applies wherever that component is used, as an
  edit to the component in the equivalent edited XML would.
- **New group on an existing message**: its first declared child is its delimiter, as for any group.
- **New member appended to an existing group**: it is placed after the group's existing members; it can
  never become the delimiter. The equivalent edited XML places it at the same position.
- **Group-delimiter collision** created by the merge (fixpp's existing `group_delimiter_collision_error`
  rule): refused as it would be for the equivalent edited XML.
- **Component cycle** closed through a base component: refused (US2 AS-4).
- **Custom DATA field whose LENGTH partner is a standard tag**, or a standard DATA tag retyped: not
  honoured for pairing, exactly as for the equivalent edited XML (L-426-2, unchanged).
- **Sending a custom Data value**: `body_builder::field_data` refuses a custom Data tag
  (`wire_unexpected_tag`, 091 R-2). A hand-written pair through `field()` is checked at commit when the
  builder holds the session's hooks (091 FR-009). Unchanged by 098, and documented in the B&L row.
- **Custom header field** added by the overlay: accepted inbound (header fields are valid on every
  message); on send, `is_send_header_tag` does not know it, so it is not reordered into the header
  (L-422-1, reworded to name the overlay case).
- **Two sessions on one engine with different overlays on the same base**: each parses and validates
  with its own merged dictionary.
- **FIXT.1.1 session**: the overlay applies to the session's own dictionary. The engine registry is
  engine-wide (one dictionary per application version), so a per-session overlay is not visible through
  it; today the registry only answers serviceability (FR-021).
- **Orchestra base** (FIX Latest): not supported in 098 (Assumptions); the overlay load refuses a base
  that is not QuickFIX-XML with a documented error.
- **Typed (codegen'd) tier**: generated `fixpp::vXX` classes, builders and `validate_<Msg>` functions
  reflect the base dictionary only. Overlay fields are read and written by tag number. The session's
  inbound validator is dictionary-driven and does see the overlay.

## Requirements *(mandatory)*

### Functional Requirements

**Loading and merging**

- **FR-001 (C++ load with overlay)**: `dict::XmlLoader` MUST offer a load that takes a base dictionary
  and one overlay, each as a file path, and a string form of the same for in-process use, and returns an
  ordinary `dict::Dictionary`. The existing `load` / `load_from_string` / `load_any` signatures and
  behaviour MUST NOT change.
- **FR-002 (equivalence)**: For every overlay the grammar admits, the merged dictionary MUST be
  indistinguishable from the dictionary loaded from the equivalent edited XML: every public `Dictionary`
  query, and every store of the `table_view` it produces (valid tags per MsgType, required tags, field
  types, enum values, group bare and context stores, group delimiters, Length+Data pair tables and
  `has_nonstandard_pair`), MUST answer the same.
- **FR-003 (grammar: may add)**: An overlay MUST be able to add: new fields with any type the base loader
  accepts; enum values to new or existing fields; new components; new MsgTypes; fields, components and
  groups to existing MsgTypes, components and groups; fields to the header and trailer; and Required
  status to an Optional child of an existing MsgType, component or group.
- **FR-004 (grammar: Length+Data)**: An overlay MUST be able to declare a custom Length+Data pair. It is
  honoured on receive by the existing `dict_hooks` precedence. `body_builder::field_data` stays
  standard-pairs-only (091 R-2 / FR-009, not reversed).
- **FR-005 (grammar: may not)**: An overlay MUST NOT change a base field's number, name or type, rename an
  existing MsgType, relax a Required child to Optional, or remove anything; each attempt is a load error
  (FR-007).
- **FR-006 (version match)**: The overlay's `<fix>` element MUST carry the same version attributes as the
  base; a mismatch is a load error.
- **FR-007 (conflict = refusal)**: Every conflict between the overlay and the base MUST fail the whole
  load with an error that names the conflicting element (tag, name or MsgType). There is no conflict
  policy knob.
- **FR-008 (base unchanged)**: Loading with an overlay MUST NOT change any other `Dictionary`; a failed
  load returns no dictionary and leaves no partial state visible.

**Limits (the overlay is untrusted input)**

- **FR-009 (caps)**: The overlay load MUST enforce, before or during parse, a cap on the overlay's size in
  bytes, its element nesting depth, and its count of field, message and component additions (2c §4.4.2
  gives starting values; plan fixes them), and MUST refuse a DOCTYPE or entity declaration. Exceeding any
  cap fails the load with a limit error naming the cap.
- **FR-010 (cycles)**: The merged component graph MUST be checked for reference cycles, including cycles
  that pass through base components; a cycle fails the load with a schema error instead of unbounded
  recursion.
- **FR-011 (scope of the caps)**: The caps apply to the overlay input. The base load path's behaviour is
  unchanged (its own lack of caps is out of scope, filed as an issue).

**Surfaces**

- **FR-012 (remove the dead field)**: `SessionConfig::dialect_overlay` and the forward declaration of
  `dict::DialectOverlay` MUST be removed, and the removal disclosed as a pre-release breaking change to
  the public C++ API. `FieldType::DialectExtension`'s comment that it is "re-enabled when D-009 ships"
  MUST be corrected: overlays use the base type vocabulary, and the enumerator stays reserved.
- **FR-013 (C ABI)**: The C ABI MUST gain an additive function that loads a base and an overlay by path
  and returns a dictionary handle with the same ownership as `fixpp_dict_load_from_xml`. A cap failure
  returns `FIXPP_ERR_DICT_LIMIT_EXCEEDED`; every other failure returns a documented non-OK code; no
  handle is returned on failure. The C-ABI MINOR version is bumped.
- **FR-014 (Python)**: The new C-ABI function MUST be reachable from the Python bindings through SWIG and
  covered by a Python test.
- **FR-015 (TOML session dictionary)**: Loading a TOML configuration MUST set each `[[session]]`'s
  `SessionConfig::dictionary`: to the `[dictionary]` dictionary for a session without an overlay, and to
  the merged dictionary for a session with one. `EngineConfig::dictionaries` keeps holding the base
  dictionary as today.
- **FR-016 (TOML key)**: `dialect_overlay` MUST be accepted at `[[session]]` scope as a path, resolved
  relative to the configuration file as `[dictionary]`'s path is. It MUST stay refused at root scope,
  with a diagnostic that names it as session-scoped. A load or merge failure is a diagnostic at that
  session's `dialect_overlay` key path.

**Witnesses and records**

- **FR-017 (independent oracle)**: The equivalence of FR-002 MUST be witnessed against hand-written,
  checked-in equivalent edited XML files, never generated from the merge, covering every FR-003 / FR-004
  addition kind on at least two base versions (one FIX 4.x, one FIX 5.0SP2). The differential MUST be
  shown RED against a seeded mismatch before it is trusted.
- **FR-018 (wire witnesses)**: Inbound session witnesses MUST show each US1 acceptance scenario on the
  shipped receive path, each RED on the unmodified base, and each matching the equivalent edited XML.
- **FR-019 (records)**: D-009 and COM-011 flip to `done` with their witnesses; D-009 gains a
  coverage-index entry. L-002-2 retires. B-066-1's extension story is updated to the shipped overlay.
  L-004-1, L-422-1, L-426-1, L-426-2 and L-426-3 are each assessed and either left, reworded to cover the
  overlay, or retired, with the reason recorded. New `B-098-*` rows record the overlay's behaviour, the
  TOML session-dictionary change and the removed field; new `L-098-*` rows record the limits below.
- **FR-020 (design-doc supersession)**: `.specify/2c-codegen.md` §1.2's merge-cost line, §4.3, §4.4,
  §4.4.1, §4.4.2 and §6.4 MUST carry in-place "Updated (098-dialect-overlay)" notes naming what 098
  replaced; `brain/components/dictionary.md` MUST flag them.

**FIXT / row 6**

- **FR-021 (registry interaction)**: 098 MUST NOT change how the engine registry is built or queried.
  The spec records, for row 6 (fixpp#643), that a session's overlay lives only in its own
  `SessionConfig::dictionary`: when row 6 starts validating FIXT application messages with a
  per-application-version dictionary, it must keep using the session's own dictionary for the session's
  own version, or the overlay is lost. This is recorded as an `L-098-*` row until row 6 settles it.

### Key Entities

- **Overlay document** — a QuickFIX-XML `<fix>` document holding additions only; versioned to match its
  base.
- **Merged dictionary** — an ordinary `dict::Dictionary`; per session through `SessionConfig::dictionary`.
- **Equivalent edited XML fixture** — a checked-in complete dictionary, hand-written, used only as the
  test oracle.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For every checked-in overlay / equivalent-edited-XML pair, the differential over every
  `Dictionary` query and `table_view` store reports zero differences, and reports at least one
  difference against its seeded mismatch.
- **SC-002**: Each US1 acceptance scenario is GREEN on base + overlay and on the equivalent edited XML,
  and RED on the unmodified base (6 scenarios × at least one base version).
- **SC-003**: Each US2 refusal is produced with its named error, and the base loaded alone is unchanged
  before and after (one table-driven witness, every row named).
- **SC-004**: A TOML file whose sessions name no host-side dictionary opens every session (US3 AS-1),
  where the unmodified tree fails `open()` with `invalid_session_config`.
- **SC-005**: Loading a base with an overlay of the cap's maximum entry count takes no more than 1.5× the
  time of loading the base alone, on the same machine (one measured comparison, recorded in the verify
  record). This replaces 2c §1.2's "merge ≤ 1 ms", which assumed a separate merge step.
- **SC-006**: The full existing test suite passes unchanged, apart from the tests that name
  `SessionConfig::dialect_overlay` or the TOML deferred key, which are rewritten.
- **SC-007**: D-009 and COM-011 are `done`; L-002-2 is in the closed B&L file.

## Assumptions

- **Base is QuickFIX-XML.** An Orchestra-loaded base (FIX Latest) is out of scope: the overlay is a
  QuickFIX-XML document and the merge is defined over that schema. *Open for `/speckit-clarify`*: the
  owner may want FIX Latest covered; the cost is a merge defined over the loader's internal state
  instead of the XML document.
- **One overlay per load.** A user with several layers combines them into one overlay file. *Open for
  `/speckit-clarify`.*
- **Enum values on existing fields are additions** (FR-003). QuickFIX users add venue values to standard
  enums by editing the XML; the owner's grammar ruling forbids changing a type, and a new value does not.
  *Confirm in `/speckit-clarify`.*
- **Hot path unchanged.** The merged dictionary is an ordinary `Dictionary`, so parse, validate and send
  costs per message are those of any dictionary of the same size.
- **Pre-release.** Removing a never-read public field is a permitted pre-release breaking change to the C++
  API; the C ABI change is additive (`[const §X.7]`).
- **No constitution amendment is expected.** `[const §XV.13]` mandates the hybrid "runtime XML loader for
  custom (D-007 + D-009)", which 098 delivers; no article names the overlay's grammar or attachment point.
  If plan or Gate A finds an article that must change, fixpp#642 (B83) rides that Article XX amendment;
  otherwise #642 goes to a docs PR.

## Out of Scope

- Typed accessors, builders or `validate_<Msg>` for overlay fields (codegen stays base-only; the
  documented route to typed accessors is regenerating against an edited XML).
- XML caps and a cycle guard on the base `XmlLoader::load` path (filed as an issue).
- The unimplemented engine fallback for the dictionary axis (`session_config.hpp` header comment; filed).
- `redact_tag554` having no production caller (filed).
- fixpp's single `wire_unexpected_tag` against QuickFIX's 373=0 (unknown tag) / 373=2 (tag not defined
  for the message) distinction (filed).
- Changing `field_data`'s standard-pairs-only rule (091 R-2), or re-pairing standard tags (L-426-2).
- Per-session overlays in the engine registry; per-application-version dictionaries (row 6, fixpp#643).
- An overlay on an Orchestra base; more than one overlay per load (see Assumptions).
- A QuickFIX `.cfg` parser or `AppDataDictionary` keys.

## Normative References

- **Spec refs**: `[FIX44 Vol1] User Defined Fields` (tag ranges 5000–9999 and 20000–39999 reserved for
  user-defined fields; local copy per the FIX primary-docs reference); `[FIX50SP2 §3.2] Repeating groups`
  (D-009's current catalogue Spec ref, re-examined in FR-019); `[FIX50SP2 §3.3] Data (raw bytes) field
  pairs` (FR-004).
- **QuickFIX reference behaviour** (cited, not vendored): QuickFIX C++ `386ce46e` (`DataDictionary`,
  `SessionFactory`, `Message`), QuickFIX/J `bc7e8b23` (`DataDictionary`, `DefaultSessionFactory`,
  `Message`, `customising-quickfixj.md`), QuickFIX/Go `eff945dd` (`datadictionary`, `validation.go`,
  `session_factory.go`).
- **Catalogue rows**: D-009, COM-011 (close); D-007 (the loader this extends); W-008 (Length+Data).
- **Constitution**: `[const §XV.13]` (the hybrid this delivers); `[const §I.1]` (version surface,
  unchanged); `[const §VI]` (coverage-index entry); `[const §X.7]` (C ABI pre-release rule, additive);
  `[const §XVI.3]` (clarify mandatory: dictionary, ABI); `[const §XVII.1]` (Gate A: public API);
  `[const §XVII.8]` (verify).
- **Design docs**: `.specify/2c-codegen.md` §1.2, §4.3, §4.4, §4.4.1, §4.4.2, §6.4 (superseded in part,
  FR-020); `.specify/426-428-length-data-pairs.md` §3 (pair precedence); `.specify/215-dictionary-view.md`
  (the snapshot identity check that motivates merge-in-loader).
- **B&L**: L-002-2 (retire); B-066-1 (update); L-004-1, L-422-1, L-426-1, L-426-2, L-426-3 (assess);
  L-097-1 (TOML / C ABI single engine dictionary, read and unchanged).
- **Precedent**: `specs/002-dictionary-xml-loader/` (the loader, F2 deferral of `load_overlay`);
  `specs/091-data-field-bytes/` (R-2 / FR-009); `specs/066-dict-backed-inbound-parse/` (B-066-1).
