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

- **Base dictionary** — a complete dictionary file, either QuickFIX XML (e.g. the vendored
  `dictionaries/FIX44.xml`) or a FIX Orchestra repository (the vendored
  `dictionaries/orchestra/OrchestraFIXLatest.xml`), loaded today by `dict::XmlLoader::load`,
  `dict::OrchestraLoader` or `dict::load_any`.
- **Overlay** — a document in its base's format that holds **only additions**. For a QuickFIX-XML
  base: the `<fix>` root and schema, with new `<field>` definitions and enum `<value>`s, and new or
  extended `<header>`, `<trailer>`, `<component>`, `<message>` and `<group>` elements. For an
  Orchestra base: the `fixr:repository` root and schema, with new or extended code sets, fields,
  components, groups and messages. It is never loaded on its own.
- **Definition an edit lands on** — every overlay element names one base definition (a field, code
  set, component, group or message, or a group element at one position in the XML), and the edit
  changes that definition. Every place the base uses that definition sees the edit, exactly as a hand
  edit of the same element would.
- **Merged dictionary** — the ordinary `dict::Dictionary` that results from applying one overlay to one
  base. Nothing downstream can tell it apart from a dictionary loaded from a file.
- **Equivalent edited XML** — the base file with each overlay addition **appended at the end of its
  matched container, in overlay document order** (a new field at the end of `<fields>`, a new member
  after a group's last member, a new message at the end of `<messages>`, and so on; for Orchestra, the
  same rule in the repository's own containers). It is what a QuickFIX user gets by copying the base and
  adding the same lines at those places, and it is the reference this feature is measured against. A
  different hand edit (an alphabetical insert, say) can give a different dictionary, because several
  stores depend on document order (component ids, enum order, the group delimiter, the Length+Data first
  writer), and FR-002 does not claim equivalence with it.
- **Standard pair tag** — a tag named in fixpp's standard Length+Data table
  (`include/fixpp/core/length_data_pairs.hpp`). A dictionary pair is honoured only when neither of its
  tags is standard (`include/fixpp/wire/dict_hooks.hpp`, B-426-3 precedence).

## Context: why this feature exists

**What QuickFIX does.** Read at QuickFIX C++ `386ce46e`, QuickFIX/J `bc7e8b23` and QuickFIX/Go
`eff945dd` (2026-10-31; citations in the parent repo's
`research/findings/v1-d009-quickfix-dictionary-model-2026-10-31.md`). None of the
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
- Q: Is the result checked against an independent reference? → A: Yes. ~~The equivalent edited XML is
  hand-written and checked in, never generated from the merge (FR-017, SC-001).~~ **Superseded at Gate A
  round 1 (orchestrator, technical, 2026-10-31):** the reference is hand-written from the base file and
  never derived from the merge, in one of two kinds: a complete hand-written file for the small bases,
  and a hand-written insertion patch of the vendored base file for FIX44, FIX50SP2 and the Orchestra
  file, which are too large to copy by hand (FR-017; plan research R-10).

### Session 2026-10-31 (`/speckit-clarify`)

- Q: Should an overlay be allowed on top of the FIX Latest dictionary, which fixpp loads from FIX
  Orchestra rather than from QuickFIX XML? → A: **Yes, in 098** (owner): a second merge path for an
  Orchestra base, with its own equivalence witness. ~~The overlay keeps one grammar, a QuickFIX-XML
  fragment, for both kinds of base (orchestrator reading, for plan and Gate A to confirm).~~
  **Superseded at `/speckit-plan` (orchestrator, 2026-10-31): the overlay is written in its base's
  format** — a QuickFIX-XML fragment for a QuickFIX-XML base, a FIX Orchestra (`fixr:repository`)
  fragment for an Orchestra base. Measured reasons (plan research R-2): the Orchestra loader has its
  own state and grammar and silently ignores non-`fixr:` elements; it references fields, components
  and groups by numeric id with no name index; several Orchestra group definitions share one
  NumInGroup tag, so "extend group NoXxx" has no single target; the header is component 1024
  (StandardHeader), not a `<header>` element; and fixpp's QuickFIX-XML version table has no FIX
  Latest row. A same-format fragment spliced into the base document needs none of that translation.
  The equivalent edited reference is the base file hand-edited in its own format.
- Q: May an overlay add allowed values to an existing standard field's enumeration (e.g. a venue
  OrdType(40) value)? → A: **Yes** (owner). Values the overlay lists are added to the base field's;
  a value already present is a no-op; a base value cannot be removed or renamed (FR-003, US1 AS-6).
- Q: When an overlay adds a custom header field, should the send side move that field into the header
  when the caller places it among body fields, as it does for standard header fields? → A: **Yes, fix
  it in 098** (owner), closing L-422-1 for overlays and complete edited dictionaries alike. The
  send-side header set becomes the union of today's fixed set and the session dictionary's declared
  `<header>` fields (FR-022), as QuickFIX C++ and J take the union of their hardcoded list and the
  dictionary header.
- Q: Should one load accept a single overlay, or an ordered list of overlays? → A: **One overlay per
  load** (owner). A user with several layers combines them into one file; an overlay meets only the
  base. The C++ call, the C-ABI function and the TOML key each take one overlay path (FR-001, FR-013,
  FR-016).

### Session 2026-10-31 (Gate A round 1)

Technical answers, each integrated into the FR, SC or edge case it affects. Three are orchestrator
defaults on product behaviour; they are marked, and the owner confirms them at Gate A sign-off. The
third (the side-table wrap) is superseded at Gate A round 2.

- Q: Which hand edit is "the" equivalent edited XML? → A: The one that appends each addition at the end
  of its matched container, in overlay document order (Terminology, FR-002). Other edits can differ in
  document-order stores.
- Q: What bounds the work an overlay can cause? → A: Besides the shape caps, an expansion budget on the
  ~~entries `finalize()` will emit~~ steps `finalize()`'s expansion takes (amended at Gate A round 2),
  checked by one iterative, memoized walk of the merged reference graph
  before any check that follows references and before the splice; a reference cap; and caps that count **new** definitions only
  (FR-009, FR-010).
- Q: Which class does a counted Data value take on send? → A: Its Length's, as today, so the pair stays
  adjacent (FR-022).
- Q: Which `required` / `presence` values may an overlay carry? → A: `Y`, `N` or absent (QuickFIX XML);
  `required`, `optional` or absent (Orchestra). Anything else is a conflict error (FR-005, edge cases).
- Q: An overlay lists an enum value on a base field that has no values (an open domain, such as Text(58)
  or FIXT11's MsgType(35)). Is that accepted? → A: **Yes, and disclosed** — **orchestrator default,
  owner confirmed at Gate A sign-off 2026-11-01.** Validation then accepts only the listed values for that field,
  as a QuickFIX hand edit would. The alternative, refusing it as a narrowing, departs from QuickFIX's
  behaviour (FR-005, edge cases, B-098-7).
- Q: Can a `[default]` `dialect_overlay` be cleared for one session? → A: **No; disclosed** —
  **orchestrator default, owner confirmed at Gate A sign-off 2026-11-01.** TOML has no null, and a reserved "none"
  value would be new surface. QuickFIX's `[DEFAULT]` behaves the same (FR-016, edge cases, B-098-3).
- Q: The base loaders store a side-table start index as `std::uint16_t`, which the review's model shows
  wrapping on FIX50SP2. Is that fixed in 098? → A: ~~**No** — **orchestrator default, owner to confirm at
  Gate A sign-off.** It is pre-existing and not caused by an overlay. 098 runs a probe, records it, files
  the defect if the probe shows it, and keeps the oracle's census check to definitions below the wrap
  point (plan research R-10).~~ **Superseded at Gate A round 2** (the premise was false; see that session).

### Session 2026-10-31 (Gate A round 2)

Technical answers, each integrated into the FR, edge case or research section it affects. One is an
orchestrator default on product behaviour, marked; the owner confirms it at Gate A sign-off.

- Q: When is a Length+Data pair a conflict? → A: When the loader's own pair detection, run on the base
  and on the spliced document, gives a different relation for any base tag: a pair gained, lost or given
  a second partner, in either direction and in either format. Element-shape rules are not used (FR-005,
  research R-3, R-5 step 8a).
- Q: What unit does the expansion budget count, and what makes it hold? → A: Steps: every child node the
  loaders' `expand_field_list` loop visits, so a reference to an empty component is charged too. A
  pre-walk predicts the steps and refuses early. The build also counts steps on the overlay path and
  refuses with a limit error the moment the count passes the prediction, so an expansion the pre-walk
  does not model causes a refusal, never unbounded work. Tests assert count equals prediction (FR-009,
  research R-5).
- Q: How is the declared header store computed? → A: By a visited-set walk from the header, which yields
  a set, not by an expansion; the step counter does not charge it (FR-022, research R-7).
- Q: What if a base repeats a key the overlay matches on (two messages of one MsgType, two fields of one
  name)? → A: The base is still admitted; an overlay element whose lookup hits the repeated key is a
  conflict error, because it cannot say which one it extends (edge cases, research R-3).
- Q: Can an overlay make the `std::uint16_t` side-table index wrap? → A: **Yes**, within the budget, on a
  base that does not wrap today, so round 1's premise was false. **Refused at load** — **orchestrator
  default, owner confirmed at Gate A sign-off 2026-11-01.** An overlay that makes a side table whose starts and
  counts all fit in 16 bits stop fitting fails with a limit error. On a base whose table already does
  not fit, the overlay is admitted, the result equals the equivalent edited XML, and the defect stays
  pre-existing: probed, filed if shown, and excluded from the census check. The alternative, widening the
  index, changes the pinned 12-byte `ComponentRef` / `GroupRef` layout (research R-10).

### Session 2026-10-31 (Gate A loop 2 round 1)

Technical answers, each integrated into the user story, FR or witness it affects. None is an owner
matter: the shipped surfaces (C++, C ABI, Python through SWIG, TOML) are unchanged.

- Q: What does US4's inbound read observe at the C ABI and in Python? → A: A custom Length+Data value
  with an SOH inside, read intact by its count. C-ABI and Python sessions do not validate inbound
  (`SessionConfig::validate_inbound_messages` defaults to `false`, and neither surface sets it), and
  `fixpp_msg_get_string` reads any top-level tag, so a custom scalar tag reads the same on the base
  alone. Python has no group reader. Pairing follows the session's dictionary (fixpp#426), so only the
  merged dictionary reads the value intact. The C and Python tests read the same frame (US4, FR-013,
  FR-014; contracts W-9, W-10).
- Q: What bounds the build when the pre-walk's budget comparison is missing? → A: The step counter is
  armed at the smaller of the pre-walk's prediction and the base's total plus the budget. Wherever the
  comparison ran, the armed value is the prediction, so behaviour is unchanged; without it, the counter
  refuses with its own origin in bounded time (FR-009, research R-5).
- Q: Is a wall-clock bound part of the expansion-budget refusal? → A: No. The refusal's budget origin
  shows that the pre-walk refused before the build started (SC-003). Timing is measured only by W-12.

### Session 2026-11-01 (Gate A sign-off, owner)

- Q: Does the owner sign off Gate A, the `[const §X.6]` `/plan` review, and the orchestrator defaults marked for confirmation? → A: **Yes** (owner, 2026-10-31: "If gate-a converges, I sign off and you may continue with other steps from pipeline.md"). Gate A converged at loop 2 round 2 on 2026-11-01. The three defaults stand as written: an enum value on a base field with no values closes that field's domain (B-098-7); a `[default]` `dialect_overlay` cannot be cleared for one session (B-098-3); an overlay that would push a fitting side table past its 16-bit index is refused at load.

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

**Independent Test**: Load base + overlay, and load the hand-written equivalent edited reference (a
complete file, or a patch of the vendored base, FR-017). Compare the two dictionaries store by store,
then drive the same inbound frames through a session on each and compare the outcomes.

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
7. **Given** an overlay adding custom header field 5020, **When** the application sends a message with
   5020 placed after a body field, **Then** the frame on the wire carries 5020 in the header, before the
   first body field, as a standard header field placed the same way would be; the unmodified tree sends
   it in the body.

---

### User Story 2 — A bad or hostile overlay is refused at load, and the base is never silently altered (Priority: P1)

An operator ships an overlay with a mistake: it redefines a standard tag with another type, or relaxes a
required field, or references a field it never defined, or is many megabytes of nested elements. The load
fails at start-up with an error that names the cause. Nothing is half-applied, and no session runs with a
dictionary the operator did not intend.

**Why this priority**: The overlay is a new XML input. Silent acceptance of a conflicting definition would
change wire behaviour without notice; an unbounded document is a denial-of-service vector at start-up.

**Independent Test**: A table of malformed and oversized overlays, each expected to fail with its named
error.

**Acceptance Scenarios**:

1. **Given** an overlay that declares tag 44 with a type other than the base's PRICE, **When** it is
   loaded, **Then** the load fails with a conflict error naming tag 44.
2. **Given** an overlay that marks a base-Required field as not required, **When** it is loaded, **Then**
   the load fails with a conflict error naming the field and the MsgType.
3. **Given** an overlay larger than the byte cap, deeper than the depth cap, carrying a DOCTYPE or entity
   declaration, adding more entries or references than a cap allows, or whose references would make the
   loader's expansion take more steps than the expansion budget allows, **When** it is loaded, **Then** the load
   fails with a limit error naming the cap, before the expansion exceeds the budget, and through the C
   ABI it returns `FIXPP_ERR_DICT_LIMIT_EXCEEDED`.
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
   diagnostic saying the key is a session key.
5. **Given** `dialect_overlay` in `[default]` and two sessions, one of which sets its own
   `dialect_overlay`, **When** the file is loaded, **Then** the first session gets the default's merged
   dictionary and the second its own.

---

### User Story 4 — A C or Python program loads a base with an overlay (Priority: P2)

A C or Python host loads a base dictionary with an overlay through the C ABI and sets it on a session
config, as it does today with a plain dictionary.

**Why this priority**: The C ABI is a supported surface (Article X) and Python sits on it. The entry point
is additive.

**Independent Test**: A C-ABI test and a Python test each load base + overlay, open a session and read,
from an inbound message, a value only the merged dictionary produces: the overlay's custom Length+Data
value with an SOH inside, read intact by its count. A custom scalar tag would not show it, because C-ABI
and Python sessions do not validate inbound and read any top-level tag whatever the dictionary declares
(Clarifications, Gate A loop 2 round 1).

**Acceptance Scenarios**:

1. **Given** a base path and an overlay path, **When** the C-ABI load-with-overlay function is called,
   **Then** it returns a dictionary handle usable with `fixpp_session_config_set_dictionary`, and a
   session using it reads the overlay's custom Length+Data value from an inbound message intact.
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
2. **Given** `.specify/2c-codegen.md` and every other record the plan's inventory recipe finds, **When**
   each hit is read, **Then** it carries an in-place "Updated (098-dialect-overlay)" note naming what 098
   replaced, or the verify record says why the hit is history or still true (FR-020).

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
  Required base child is a conflict error; an absent attribute leaves the child's rule unchanged. Any
  value other than `Y` / `N` (for example `required="y"` or `required="false"`), or for Orchestra other
  than `required` / `optional`, is a conflict error: the base loader would read it as Optional.
- **Enum value on a field with no values**: the field's domain closes to the listed values, as a hand
  edit would close it. Example: a private MsgType code added to MsgType(35) on a FIXT11 base makes
  validation refuse every standard MsgType. Disclosed (B-098-7); orchestrator default, owner confirmed 2026-11-01.
- **Length+Data pair touching a base field**: refused when the merge would give a base tag a pair it
  did not have, take one away, or give it a second partner: a new DATA the loader pairs with an unpaired
  base LENGTH (in `<fields>`, in any container, or through Orchestra's `lengthId`), and a new LENGTH the
  loader pairs with a base DATA. A new DATA after a base LENGTH the base already pairs is accepted only
  when the loader forms the base pair first (it visits `<fields>`, then containers, in a fixed order, and
  keeps a LENGTH's first DATA); otherwise the base pair would be lost, and it is refused.
- **Base with a repeated key** (two messages of one MsgType, two `<field>`s of one name, a repeated
  direct child): the base is accepted with an empty overlay; an overlay element that names the repeated
  key is refused, because it cannot say which one it extends.
- **One tag twice at one level**: an addition that puts a tag twice in the level of a message, component
  or group (a direct child the parent already reaches through a component, or a component reference
  that reaches a tag the parent already has) is refused.
- **Side table past 16 bits**: an overlay that would push a fitting component or group side table past
  the `std::uint16_t` index is refused with a limit error (orchestrator default, owner confirmed 2026-11-01).
- **Overlay root child outside the allowed list** (for example `fixr:datatypes`, `fixr:sections`, or a
  non-QuickFIX element under `<fix>`): refused. `fixr:annotation` and `fixr:documentation` inside a
  definition are accepted and dropped.
- **Overlay that extends many existing definitions** (one venue tag on every FIX44 message): accepted;
  the entry caps count new definitions only, and the reference cap and expansion budget bound the rest.
- **New member in a group defined inside a component**: applies wherever that component is used, as an
  edit to the component in the equivalent edited XML would.
- **New group on an existing message**: its first declared child is its delimiter, as for any group.
- **New member appended to an existing group**: it is placed after the group's existing members, so it
  is not the delimiter unless every existing member emits nothing (an empty component, or under
  `unresolved_group_policy::tolerant` a group the base left unregistered). The equivalent edited XML
  places it at the same position, so the outcome matches in both cases.
- **Group-delimiter collision** created by the merge (fixpp's existing `group_delimiter_collision_error`
  rule): refused as it would be for the equivalent edited XML.
- **Component cycle** closed through a base component: refused (US2 AS-4).
- **Custom DATA field whose LENGTH partner is a standard tag**, or a standard DATA tag retyped: not
  honoured for pairing, exactly as for the equivalent edited XML (L-426-2, unchanged).
- **Sending a custom Data value**: `body_builder::field_data` refuses a custom Data tag
  (`wire_unexpected_tag`, 091 R-2). A hand-written pair through `field()` is checked at commit when the
  builder holds the session's hooks (091 FR-009). Unchanged by 098, and documented in the B&L row.
- **Custom header field** added by the overlay: accepted inbound (header fields are valid on every
  message), and on send moved into the header like a standard header field (FR-022, US1 AS-7).
- **Dictionary header narrower than the fixed set** (e.g. a FIX 4.x header that does not declare a tag
  the fixed set names): the fixed set still applies; the union only adds tags, so no standard header tag
  changes position.
- **Two sessions on one engine with different overlays on the same base**: each parses and validates
  with its own merged dictionary.
- **TOML `[default]` overlay and one session that wants none**: not expressible; the key moves out of
  `[default]` into the sessions that want it (B-098-3). Orchestrator default, owner confirmed 2026-11-01.
- **FIXT.1.1 session**: the overlay applies to the session's own dictionary. The engine registry is
  engine-wide (one dictionary per application version), so a per-session overlay is not visible through
  it; today the registry only answers serviceability (FR-021).
- **Orchestra base** (FIX Latest): supported with an Orchestra-format overlay (FR-001). The merged
  dictionary's session version stays `vlatest`, and it is indistinguishable from the dictionary loaded
  from a hand-edited copy of the Orchestra repository carrying the same additions (FR-002).
- **New Orchestra message without the header and trailer refs**: an Orchestra message carries header
  and trailer fields only through its structure's references to components 1024 and 1025, whereas the
  QuickFIX-XML loader folds the header in by itself. A new message that omits those refs has no valid
  header tags, as a hand edit omitting them would.
- **Orchestra code sets are shared.** Orchestra attaches enum values to a code set, and one code set can
  serve several fields. An overlay that adds a value to a code set adds it for every field that uses
  that code set, as a hand edit would. To add a value for one field only, the base would need its own
  code set for that field, which is a type change and is refused (FR-005). Disclosed as a `B-098-*` row.
- **Orchestra groups are shared definitions.** An Orchestra group is defined once and referenced by id,
  so a member added to it appears in every message that references that group. In QuickFIX XML the same
  edit lands on the one `<group>` element the overlay names, which may be inline in a single message or
  inside a shared component.
- **Overlay in the other format** (e.g. a `<fix>` fragment on an Orchestra base): refused (FR-001).
- **Base of neither format**: refused with the error `load_any` gives for an unrecognised root.
- **Typed (codegen'd) tier**: generated `fixpp::vXX` classes, builders and `validate_<Msg>` functions
  reflect the base dictionary only. Overlay fields are read and written by tag number. The session's
  inbound validator is dictionary-driven and does see the overlay.

## Requirements *(mandatory)*

### Functional Requirements

**Loading and merging**

- **FR-001 (C++ load with overlay)**: The dictionary library MUST offer a load that takes a base
  dictionary and one overlay, each as a file path, and a string form of the same for in-process use, and
  returns an ordinary `dict::Dictionary`. The base MAY be QuickFIX XML or a FIX Orchestra repository
  (the two formats `dict::load_any` recognises today); the overlay MUST be in its base's format, and an
  overlay in the other format is a load error. The
  existing `load` / `load_from_string` / `load_any` signatures and behaviour, and `OrchestraLoader`'s,
  MUST NOT change.
- **FR-002 (equivalence)**: For every overlay the grammar admits, the merged dictionary MUST be
  indistinguishable from the dictionary loaded from the equivalent edited XML as Terminology defines it
  (each addition appended at the end of its matched container, in overlay document order; for an
  Orchestra base, the Orchestra repository edited by the same rule): every public `Dictionary`
  query, and every store of the `table_view` it produces (valid tags per MsgType, required tags, field
  types, enum values, group bare, exact and context stores, group delimiters, Length+Data pair tables and
  `has_nonstandard_pair`), MUST answer the same.
- **FR-003 (grammar: may add)**: An overlay MUST be able to add: new fields with any type the base loader
  accepts; enum values to new or existing fields; new components; new MsgTypes; fields, components and
  groups to existing MsgTypes, components and groups; fields to the header and trailer; and Required
  status to an Optional child of an existing MsgType, component or group.
- **FR-004 (grammar: Length+Data)**: An overlay MUST be able to declare a custom Length+Data pair. It is
  honoured on receive by the existing `dict_hooks` precedence. `body_builder::field_data` stays
  standard-pairs-only (091 R-2 / FR-009, not reversed).
- **FR-005 (grammar: may not)**: An overlay MUST NOT change a base field's number, name or type, rename an
  existing MsgType, relax a Required child to Optional, or remove anything (including a base enum value);
  each attempt is a load error (FR-007). A `required` / `presence` value outside `Y` / `N` (QuickFIX XML)
  or `required` / `optional` (Orchestra) is also a load error, so that no typo relaxes a rule. An overlay
  MUST NOT change the Length+Data pair relation of a base field: a pair touching a base tag that the
  loader would gain, lose or give a second partner is a load error. Adding the first enum value
  to a base field with no values is **not** a removal, but it narrows what validation accepts for that
  field; it is accepted and disclosed (Clarifications, Gate A round 1).
- **FR-006 (version match)**: The overlay's root MUST identify the same version as the base: the
  `<fix>` type/major/minor/servicepack attributes for QuickFIX XML, the `fixr:repository` `version`
  attribute for Orchestra. A mismatch is a load error.
- **FR-007 (conflict = refusal)**: Every conflict between the overlay and the base MUST fail the whole
  load with an error that names the conflicting element (tag, name or MsgType). There is no conflict
  policy knob.
- **FR-008 (base unchanged)**: Loading with an overlay MUST NOT change any other `Dictionary`; a failed
  load returns no dictionary and leaves no partial state visible. This holds by construction: the overlay
  entry points take paths or strings, never a `Dictionary`, and the loaders are stateless, so no
  `Dictionary` is an input to the overlay load. The surface contract states it; no test can make two fresh
  base loads differ, so none is claimed as its witness.

**Limits (the overlay is untrusted input)**

- **FR-009 (caps)**: The overlay load MUST enforce a cap on the overlay's size in bytes (before parse), its
  element nesting depth, its count of **new** field, message and component definitions (2c §4.4.2 gives
  starting values; plan fixes them), and its count of added references, and MUST refuse a DOCTYPE or
  entity declaration. It MUST also bound the **work** the overlay causes: before any check that follows
  references, the splice and the dictionary build, it MUST compute, without recursion, how many steps
  (child nodes visited, references included) the loader's expansion of the merged messages, components
  and groups will take, and refuse when the overlay's increase over the base exceeds a fixed budget. The
  build MUST refuse as soon as its own step count passes that prediction, so every refusal happens
  before the expansion exceeds the budget, even if the prediction misses an expansion. Any base the base loader accepts is admitted with an
  empty overlay. Exceeding any cap or the budget fails the load with a limit error naming it.
- **FR-010 (cycles)**: The merged component graph MUST be checked for reference cycles, including cycles
  that pass through base components; a cycle fails the load with a schema error instead of unbounded
  recursion. The check runs in the same non-recursive walk as FR-009's budget, before any other step
  that follows references (the conflict checks, the splice, the build).
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
  handle is returned on failure. A session given the returned handle parses with the merged dictionary
  (US4 AS-1). The C-ABI MINOR version is bumped.
- **FR-014 (Python)**: The new C-ABI function MUST be reachable from the Python bindings through SWIG and
  covered by a Python test that reads the same inbound Length+Data value as the C-ABI test (US4) and
  checks the cap error's code.
- **FR-015 (TOML session dictionary)**: Loading a TOML configuration MUST set each `[[session]]`'s
  `SessionConfig::dictionary`: to the `[dictionary]` dictionary for a session without an overlay, and to
  the merged dictionary for a session with one. `EngineConfig::dictionaries` keeps holding the base
  dictionary as today.
- **FR-016 (TOML key)**: `dialect_overlay` MUST be accepted as a path at `[[session]]` scope and in
  `[default]` (which every session inherits unless it sets its own, as QuickFIX's `[DEFAULT]` does),
  resolved relative to the configuration file as `[dictionary]`'s path is. It MUST stay refused at root
  scope, with a diagnostic that names it as a session key. A load or merge failure is a diagnostic at the
  key path where the value was written (`default.dialect_overlay` or `session[N].dialect_overlay`).
  Diagnostics are reported once per authoring origin: a value written in `[default]` and the same value
  written in `session[N]` are two diagnostics. A non-string or empty value is a diagnostic at its key
  path. A `dialect_overlay` with no usable `[dictionary]` (absent, or failed to load) is a diagnostic at
  its key path ("dialect_overlay requires [dictionary]"), never a crash. A `[default]` value cannot be
  cleared for one session (Clarifications, Gate A round 1). The other deferred TOML keys stay deferred.

**Witnesses and records**

- **FR-017 (independent oracle)**: The equivalence of FR-002 MUST be witnessed against references written
  by hand from the base file and never derived from the merge, of two kinds: (a) a complete hand-written
  edited file, for a small QuickFIX-XML base and a small Orchestra base; (b) a hand-written insertion
  patch of the vendored base file, for FIX 4.4, FIX 5.0SP2 and the Orchestra FIX Latest base, whose
  hunks each anchor on a multi-line context block that must match the base exactly once. Every FR-003 /
  FR-004 addition kind MUST be covered on each format's small base; each vendored base MUST cover at
  least a header field, an enum value on an existing field or code set, a member added to an existing
  group, a new message and a field added to existing messages, and each QuickFIX-XML vendored base a
  custom Length+Data pair. The comparison MUST take its keys from both sides' raw documents and the
  dictionary's internal indexes as well as its public API, and MUST check the component and group side
  tables against the raw document, not only side against side. Each differential MUST be shown RED
  against a seeded mismatch before it is trusted, and a patch hunk that matches zero or two places MUST
  fail its cell.
- **FR-018 (wire witnesses)**: Inbound session witnesses MUST show each US1 acceptance scenario on the
  shipped receive path, each behaving as it does today on the unmodified base (refused, or for AS-2 read
  as a field after the group), and each matching the equivalent edited XML. The unmodified-base arm is a
  negative control, not a RED arm: its assertion holds before and after 098.
- **FR-019 (records)**: D-009 and COM-011 flip to `done` with their witnesses; D-009 gains a
  coverage-index entry. L-002-2 and L-422-1 retire. B-066-1's extension story is updated to the shipped
  overlay. L-004-1, L-426-1, L-426-2 and L-426-3 are each assessed and either left, reworded to cover the
  overlay, or retired, with the reason recorded. New `B-098-*` rows record the overlay's behaviour, the
  TOML session-dictionary change, the removed field, the closing of an open enum domain and the
  uncleared `[default]` overlay; new `L-098-*` rows record the limits below. D-009's and COM-011's
  version scope widens to include FIXT.1.1 and FIX Latest.
- **FR-020 (design-doc supersession)**: Every record that describes the superseded overlay design, or
  that 098 makes false, MUST carry an in-place "Updated (098-dialect-overlay)" note naming what 098
  replaced, or a recorded reason why it is history or still true. The set is found by the plan's grep
  recipe (data-model §G), not by a hand list. It includes at least `.specify/2c-codegen.md` §1.2's
  merge-cost line, §2's Non-goals bullet on overlay grammar, §4.1's `FieldRef` comment naming
  `dict_overlay_unsupported_length_pair`, §4.3, §4.4, §4.4.1, §4.4.2 and §6.4, and the brain component pages for the code 098
  changes; `brain/components/dictionary.md` MUST flag the 2c notes.

**Send-side header membership (L-422-1)**

- **FR-022 (header set from the dictionary)**: The set of tags the send path treats as header-class MUST
  be the union of today's fixed set and every field the session dictionary declares in its `<header>`
  (base or overlay; QuickFIX-XML or Orchestra), except that a counted Data value keeps following its
  Length's class, as today, so the pair stays adjacent. The dictionary MUST expose that header membership, which
  today is lost because the loader folds header fields into every message's field list. A tag in the
  fixed set MUST keep its position rule unchanged. Plan MUST also assess the second fixed header set the
  session keeps for replay (`kReplayHeaderTags`, `src/session/session.cpp`) and state whether it needs
  the same treatment.

**FIXT / row 6**

- **FR-021 (registry interaction)**: 098 MUST NOT change how the engine registry is built or queried.
  The spec records, for row 6 (fixpp#643), that a session's overlay lives only in its own
  `SessionConfig::dictionary`: when row 6 starts validating FIXT application messages with a
  per-application-version dictionary, it must keep using the session's own dictionary for the session's
  own version, or the overlay is lost. This is recorded as an `L-098-*` row until row 6 settles it.

### Key Entities

- **Overlay document** — a document in its base's format holding additions only: a QuickFIX-XML `<fix>`
  document for a QuickFIX-XML base, a FIX Orchestra `fixr:repository` fragment for an Orchestra base;
  versioned to match its base.
- **Merged dictionary** — an ordinary `dict::Dictionary`; per session through `SessionConfig::dictionary`.
- **Equivalent edited reference** — the test oracle, written by hand from the base and never derived
  from the merge: a complete edited file for a small base, or an insertion patch of a vendored base
  (FR-017).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For every overlay and its equivalent edited reference (complete file or patch, FR-017), the
  differential over every `Dictionary` query and `table_view` store reports zero differences, and reports
  at least one difference against its seeded mismatch.
- **SC-002**: Each inbound US1 acceptance scenario (AS-1..AS-6) is GREEN on base + overlay and on the
  equivalent edited XML, and shows today's behaviour on the unmodified base, on at least one base version
  each; AS-7 is measured by SC-008.
- **SC-003**: Each US2 refusal is produced with its named error (one table-driven witness, every row
  named, in both formats where the refusal applies), and the expansion-budget refusal completes before
  the expansion exceeds the budget, shown by its origin: the pre-walk refuses before the build starts.
- **SC-004**: A TOML file whose sessions name no host-side dictionary opens every session (US3 AS-1),
  where the unmodified tree fails `open()` with `invalid_session_config`.
- **SC-005**: Loading FIX50SP2 with the named at-cap fixture overlay (the cap's maximum count of new
  definitions) takes no more than 1.5× the time of loading the base alone, on the same machine (one
  measured comparison, recorded in the verify record with the fixture's expansion count). It is not a
  claim for every overlay the caps and budget admit. This replaces 2c §1.2's "merge ≤ 1 ms", which assumed a separate merge step.
- **SC-006**: The full existing test suite passes unchanged, apart from the tests that name
  `SessionConfig::dialect_overlay` or the TOML deferred key, which are rewritten.
- **SC-007**: D-009 and COM-011 are `done`; L-002-2 and L-422-1 are in the closed B&L file.
- **SC-008**: For every header field a witness dictionary declares beyond the fixed set, a message whose
  caller places that field after a body field is sent with it in the header (US1 AS-7), RED on the
  unmodified tree; and the send-side output of every existing header-ordering witness is byte-identical
  before and after.

## Assumptions

- **Hot path.** The merged dictionary is an ordinary `Dictionary`, so parse and validate costs per message
  are those of any dictionary of the same size. FR-022 adds a header-membership lookup on the send path:
  it MUST NOT allocate (`[const §XV.1]`), and its per-message cost is measured against the unmodified
  tree in the verify record (paired bench, `[const §VIII.2]`).
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
- More than one overlay per load (Clarifications, `/speckit-clarify`).
- Fixing the base loaders' `std::uint16_t` side-table start index on a base whose side table already
  does not fit (pre-existing; probed and, if shown, filed). An overlay that would make a fitting table
  stop fitting is refused instead (Clarifications, Gate A round 2).
- A TOML value meaning "no overlay" (Clarifications, Gate A round 1).
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
- **Constitution**: `[const §XV.13]` (the hybrid this delivers); `[const §XV.1]` and `[const §VIII.2]`
  (FR-022's send-path lookup: no allocation, paired bench); `[const §I.1]` (version surface,
  unchanged); `[const §VI]` (coverage-index entry); `[const §X.7]` (C ABI pre-release rule, additive);
  `[const §XVI.3]` (clarify mandatory: dictionary, ABI); `[const §XVII.1]` (Gate A: public API);
  `[const §XVII.8]` (verify).
- **Design docs**: `.specify/2c-codegen.md` §1.2, §4.3, §4.4, §4.4.1, §4.4.2, §6.4 and every other hit
  of the plan's inventory recipe, such as `.specify/2d-threading.md` and `.specify/2m-pybind.md`
  (superseded in part, FR-020); `.specify/426-428-length-data-pairs.md` §3 (pair precedence); `.specify/215-dictionary-view.md`
  (the snapshot identity check that motivates merge-in-loader).
- **B&L**: L-002-2, L-422-1 (retire); B-066-1 (update); L-004-1, L-426-1, L-426-2, L-426-3 (assess);
  L-097-1 (TOML / C ABI single engine dictionary, read and unchanged).
- **Precedent**: `specs/002-dictionary-xml-loader/` (the loader, F2 deferral of `load_overlay`);
  `specs/091-data-field-bytes/` (R-2 / FR-009); `specs/066-dict-backed-inbound-parse/` (B-066-1).
