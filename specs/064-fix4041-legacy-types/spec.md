# Feature Specification: FIX 4.0/4.1 dictionary loader legacy-type support (A-5 / D-004)

**Feature Branch**: `064-fix4041-legacy-types`
**Created**: 2026-07-05
**Status**: Draft
**Input**: User description: "FIX 4.0/4.1 dictionary loader legacy-type support (closes A-5 / D-004). The XML dictionary loader currently fail-closes any field type outside the `[FIX50SP2 §3.3]` vocabulary (recorded contract AC-L8). FIX 4.0/4.1 use exactly two legacy field-type names absent from that vocabulary — `DATE` and `TIME`. Add two collapse-table alias rows (`TIME → UtcTimestamp`, `DATE → LocalMktDate`), vendor `FIX40.xml`/`FIX41.xml` verbatim from the pinned upstream SHA, add headline lookup-test rows. Completes Constitution §I.1's all-nine-versions commitment."

## Overview

The runtime XML dictionary loader (`src/dictionary/xml_loader.cpp`) resolves each `<field type="…">`
attribute against a fixed field-type vocabulary and **fail-closes** any name outside it with
`dict::xml_parse_error` / `dict_xml_parse_failed` — the recorded acceptance criterion **AC-L8**
(`specs/002-dictionary-xml-loader/data-model.md:297`), sourced from `[FIX50SP2 §3.3]`. The vocabulary
already carries a documented **collapse-table carve-out** (`src/dictionary/xml_loader.cpp:97-104`) that
accepts a handful of *post-canonical* FIX 5.0 type names (`TAGNUM`, `LOCALMKTTIME`, `XID`, `XIDREF`) by
mapping them to the nearest representable `field_data_type` enum value — the enum itself stays frozen at
the `[FIX50SP2 §3.3]` canonical set.

FIX 4.0 and FIX 4.1 are the two runtime versions Constitution `[const §I.1]` commits v1.0 to support
that are **not yet loadable**. Their dictionaries use exactly **two** *pre-canonical* legacy field-type
names absent from the vocabulary — `DATE` and `TIME` — so both `FIX40.xml` and `FIX41.xml` fail-close on
the first such field. Every other type they use (`CHAR`, `DATA`, `FLOAT`, `INT`, `LENGTH`, `DAYOFMONTH`,
`MONTHYEAR`, `STRING`) is already present. (Verified empirically 2026-07-05 by fetching both dictionaries
at the pinned SHA and enumerating their distinct `type=` attributes.)

This feature closes that gap by **extending the existing collapse-table carve-out backward** with two
legacy-alias rows — `TIME → field_data_type::UtcTimestamp` and `DATE → field_data_type::LocalMktDate` —
vendoring `FIX40.xml`/`FIX41.xml` verbatim from the `dictionaries/UPSTREAM.txt`-pinned SHA, and adding
headline lookup-test rows for both versions. It makes **no change to the `field_data_type` enum**, adds
no codegen, and touches no public / C-ABI / wire / error surface. It is the pre-canonical analogue of the
merged **D-005/006** work (PR #171), which vendored FIX 4.3 / 5.0 / 5.0SP1 as pure data because those
versions used only in-vocabulary type names.

Because it relaxes AC-L8's fail-closed guarantee for two type names, it is run as a full Spec-Kit feature
with a Gate-A design review (user decision, 2026-07-05) — the same treatment precedent as **060**, which
formally amended the Gate-A `no-__int128` decision rather than editing quietly.

## Clarifications

### Session 2026-07-05

- Q: What `field_data_type` should the legacy `TIME` type map to?
  → A: **`UtcTimestamp`** — unambiguous and evidence-backed on two independent axes. (1) QuickFIX's own
  `DataDictionary::XMLTypeToType` (pinned SHA `19ef6a4c`, `src/C++/DataDictionary.cpp:675-677`) maps
  `"TIME" → TYPE::UtcTimeStamp`. (2) All seven `TIME`-typed fields across FIX 4.0/4.1 (`SendingTime`,
  `TransactTime`, `OrigTime`, `OrigSendingTime`, `ExpireTime`, `ValidUntilTime`, `EffectiveTime`) are
  typed `UTCTIMESTAMP` uniformly in the already-vendored `FIX42.xml`/`FIX43.xml`/`FIX44.xml`.
- Q: What `field_data_type` should the legacy `DATE` type map to, given QuickFIX and the FIX successor
  dicts disagree?
  → A: **`LocalMktDate`** — a deliberate semantic upgrade over QuickFIX (user decision). QuickFIX has
  **no** `DATE` branch in `XMLTypeToType`, so it returns `TYPE::Unknown` and performs no type validation
  on `DATE` fields. But the two `DATE`-typed fields (`TradeDate`, `FutSettDate`) are typed `LOCALMKTDATE`
  in **every** FIX 4.2+ canonical dict, so mapping to `LocalMktDate` gives our typed reads real date
  semantics rather than leaving the fields untyped. **This divergence from QuickFIX MUST be recorded**
  (see FR-006, FR-009, SC-005).
- Resolved factually (not a user decision): the vocabulary relaxation is **global**, not version-scoped —
  after the rows land, a `FIX44.xml` that used `type="DATE"` would also be accepted rather than
  fail-closing. This matches the existing global `TAGNUM`/`LOCALMKTTIME`/`XID`/`XIDREF` collapse rows
  (which are likewise not gated on version) and is named explicitly in the decision record so Gate B does
  not raise it as a silently-widened fail-closed surface (FR-007, SC-006).

## Normative References

Per `[const §VI.5]`. This feature introduces **no new OFFICIAL FIX spec rows** — FIX 4.0/4.1 message and
field semantics are already carried by the vendored dictionary XML data, and no new wire/session behavior
is asserted — so there are no new `[DocAbbrev §X.Y.Z]` coverage-index entries. The governing internal
contract this feature **relaxes** is:

- `specs/002-dictionary-xml-loader/data-model.md:297` — **AC-L8** ("`<field type="UNKNOWN_TYPE">` outside
  `[FIX50SP2 §3.3]` → `xml_parse_error` / `dict_xml_parse_failed`"). This feature adds two named legacy
  types to the accepted set via the collapse table; AC-L8's fail-closed disposition is preserved for
  every name **still** outside the (now two-larger) accepted set.
- `[FIX50SP2 §3.3]` — the canonical field-type vocabulary the enum is frozen to (unchanged; the enum gains
  no variant).
- `[const §I.1]` — the v1.0 commitment to all nine versions' runtime XML (this feature completes it).
- `[const §XVIII.6]` — rates FIX 4.0/4.1 lowest deployment priority ("vanishingly few production
  deployments"); informs the P-levels below, not the correctness bar.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Load a FIX 4.0 or FIX 4.1 runtime dictionary (Priority: P1)

An operator points the engine at a FIX 4.0 or FIX 4.1 runtime dictionary (either the vendored copy or an
equivalent upstream `FIX40.xml`/`FIX41.xml`). Today the load throws on the first `DATE`- or `TIME`-typed
field; after this feature the dictionary loads to completion and every field resolves to a
`field_data_type`.

**Why this priority**: This is the entire feature — the two versions Constitution `[const §I.1]` names
that cannot currently be loaded. Every other story is a guardrail around it.

**Independent Test**: Load the vendored `dictionaries/FIX40.xml` and `dictionaries/FIX41.xml` through
`XmlLoader`; assert the load succeeds (no `xml_parse_error`), and that a `DATE`-typed field
(`TradeDate`) resolves to `field_data_type::LocalMktDate` and a `TIME`-typed field (`SendingTime`)
resolves to `field_data_type::UtcTimestamp`.

**Acceptance Scenarios**:

1. **Given** the vendored `FIX40.xml`, **When** it is loaded, **Then** the load completes without error
   and lookups for its session and application messages succeed.
2. **Given** the vendored `FIX41.xml`, **When** it is loaded, **Then** the load completes without error
   and lookups for its session and application messages succeed.
3. **Given** a field typed `TIME` (e.g. `SendingTime`), **When** the dictionary is loaded, **Then** the
   field's resolved type is `field_data_type::UtcTimestamp`.
4. **Given** a field typed `DATE` (e.g. `TradeDate`, `FutSettDate`), **When** the dictionary is loaded,
   **Then** the field's resolved type is `field_data_type::LocalMktDate`.

---

### User Story 2 - Session-bearing lookups resolve for the pre-FIXT versions (Priority: P1)

FIX 4.0/4.1 are **pre-FIXT**: their session-layer messages (`Logon`, `Heartbeat`, `ResendRequest`,
`Logout`, `TestRequest`, `SequenceReset`, `Reject`) live **inside their own dictionary**, not in a
separate FIXT.1.1 transport dictionary. The headline lookup tests must therefore assert those session
messages are **present** — the opposite of the merged D-006 FIX 5.0/5.0SP1 carve-out, where session
messages moved to FIXT.1.1 and are asserted **forbidden** in the application dict.

**Why this priority**: A lookup-test row copied from the D-006 app-only shape would assert session
messages *absent* and either false-pass or wrongly fail; getting the version shape right is a correctness
condition on the test itself, equal in priority to Story 1.

**Independent Test**: Add `VersionParam` rows for FIX 4.0 and FIX 4.1 to the dictionary lookup test that
assert a session message (`Logon`, msgtype `A`) and an application message resolve within the loaded
dictionary; run `dictionary_lookup_test` and confirm the new rows pass alongside the existing seven.

**Acceptance Scenarios**:

1. **Given** the loaded FIX 4.0 dictionary, **When** the session message `Logon` (msgtype `A`) is looked
   up, **Then** it resolves within that dictionary (present, not forbidden).
2. **Given** the loaded FIX 4.1 dictionary, **When** a headline application message is looked up, **Then**
   it resolves within that dictionary.
3. **Given** the renamed/extended lookup-test parameter set, **When** the suite runs, **Then** all
   pre-existing version rows continue to pass unchanged (no regression to the FIX 4.2–5.0SP2 rows).

---

### User Story 3 - The fail-closed contract still holds for genuinely unknown types (Priority: P2)

Adding two named legacy aliases must not turn the loader into a permissive "accept anything" parser. A
field type that is neither in the `[FIX50SP2 §3.3]` vocabulary nor one of the sanctioned collapse aliases
(post-canonical or the two new legacy ones) must still fail-close exactly as AC-L8 requires.

**Why this priority**: This is the guardrail that keeps the AC-L8 relaxation *narrow* — two named
additions, not a hole. It is a hard release condition but not the core value, hence P2.

**Independent Test**: Load a minimal dictionary containing `<field type="TOTALLY_BOGUS">`; assert it
still throws `xml_parse_error` with `dict_xml_parse_failed`. (Extends the existing AC-L8 negative test.)

**Acceptance Scenarios**:

1. **Given** a dictionary with a field typed by a name outside both the vocabulary and the full collapse
   set, **When** it is loaded, **Then** the loader throws `xml_parse_error` / `dict_xml_parse_failed`
   unchanged.
2. **Given** a dictionary with a field typed `DATE` or `TIME`, **When** it is loaded, **Then** it is
   accepted (the two names are now in the collapse set) — confirming the addition is exactly two names.

### Edge Cases

- **`DATE`/`TIME` appearing in a newer dictionary**: because the relaxation is global, a `FIX44.xml`
  using `type="DATE"` would now load (mapped to `LocalMktDate`) rather than fail-close. This is
  intentional and consistent with the existing global collapse rows; it is recorded (FR-007) so it is not
  read as an accidental widening. No vendored dictionary FIX 4.2+ actually uses these names (they use the
  canonical `LOCALMKTDATE`/`UTCTIMESTAMP`), so no vendored file's resolved typing changes.
- **Case sensitivity**: the loader matches type names by the existing table's convention; the two new
  rows follow that exact convention (no new case-handling path).
- **QuickFIX behavioral divergence on `DATE`**: our loader ascribes `LocalMktDate` where QuickFIX ascribes
  no type (`TYPE::Unknown`, no validation). This is a deliberate stronger-typing choice, not a defect, and
  is recorded as a behaviors-and-limitations row (FR-009).
- **`FutSettDate` absent in FIX 4.4**: not an issue here (FIX 4.4 already loads); noted only because the
  field-type cross-check used it — `FutSettDate` is `LOCALMKTDATE` in FIX 4.2/4.3 and dropped by 4.4.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The loader's field-type resolution MUST accept the legacy type name `TIME` and resolve it
  to `field_data_type::UtcTimestamp`.
- **FR-002**: The loader's field-type resolution MUST accept the legacy type name `DATE` and resolve it
  to `field_data_type::LocalMktDate`.
- **FR-003**: The two additions MUST be made as collapse-table alias rows in the **same mechanism and
  style** as the existing post-canonical rows (`src/dictionary/xml_loader.cpp:97-104`) — new
  `xml_name → existing-enum` entries only. The `field_data_type` enum MUST NOT gain a variant and MUST NOT
  be otherwise modified.
- **FR-004**: `dictionaries/FIX40.xml` and `dictionaries/FIX41.xml` MUST be vendored **verbatim** from
  `quickfix/quickfix` at the exact SHA recorded in `dictionaries/UPSTREAM.txt` (`19ef6a4c…`), matching how
  the other eight dictionaries were vendored. `dictionaries/README.md` MUST be updated to list them.
- **FR-005**: Both vendored dictionaries MUST load through `XmlLoader` to completion without
  `xml_parse_error`, and headline `VersionParam` lookup-test rows MUST be added for FIX 4.0 and FIX 4.1
  that assert their **session** messages (pre-FIXT, in-dictionary) resolve present, plus at least one
  application message each. Pre-existing version rows MUST continue to pass unchanged.
- **FR-006**: The AC-L8 relaxation MUST be **recorded** in this feature's `research.md` and
  `data-model.md`: the accepted-type set grows by exactly the two named legacy aliases; the fail-closed
  disposition is unchanged for every name still outside the accepted set. A negative test MUST prove a
  genuinely unknown type still throws `xml_parse_error` / `dict_xml_parse_failed`.
- **FR-007**: The **global (non-version-scoped)** nature of the relaxation MUST be stated in the decision
  record (consistent with the existing global collapse rows), so it is not mistaken for an accidental
  widening. No mechanism to version-scope the aliases is added (out of scope, and inconsistent with the
  existing table).
- **FR-008**: The feature MUST make **zero change to public surface**: no public header, C-ABI symbol,
  wire format, or `fixpp::core::error` variant changes. Scope is `src/dictionary/xml_loader.cpp` (two
  table rows), `dictionaries/` (two XML files + README), and the dictionary test(s).
- **FR-009**: The **divergence from QuickFIX** on the `DATE` type — QuickFIX resolves `DATE` to
  `TYPE::Unknown` (no validation) whereas this loader resolves it to `LocalMktDate` — MUST be recorded as
  a behaviors-and-limitations row in `spec/behaviors-and-limitations.md` (B-/L- convention), citing the
  QuickFIX `XMLTypeToType` anchor and the FIX 4.2+ successor-typing rationale. The `TIME → UtcTimestamp`
  mapping agrees with QuickFIX and needs no divergence row.
- **FR-010**: The feature-catalogue rows for **D-004** (FIX 4.0 / FIX 4.1) MUST be flipped to `done` with
  this PR as evidence, closing the A-5 "dictionary XMLs" work item and, with it, the last gap in
  `[const §I.1]`'s all-nine-versions runtime-XML commitment.

### Key Entities

- **`field_data_type`**: the frozen `[FIX50SP2 §3.3]` field-type enum (28 named variants +
  `DialectExtension` sentinel). **Unchanged** by this feature — no variant added.
- **Field-type collapse table** (`kFieldTypeTable`, `src/dictionary/xml_loader.cpp`): the
  `xml_name → field_data_type` lookup, already carrying post-canonical aliases; this feature appends two
  pre-canonical legacy rows.
- **Vendored dictionary XML** (`dictionaries/FIX40.xml`, `dictionaries/FIX41.xml`): verbatim upstream data
  files, checked in (not regenerated), so a clone builds without network access — same contract as the
  other eight.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: `dictionaries/FIX40.xml` and `dictionaries/FIX41.xml` load through `XmlLoader` with **zero**
  `xml_parse_error`, and every field in each resolves to a `field_data_type` (no name falls through to the
  fail-closed path).
- **SC-002**: The dictionary lookup test's FIX 4.0 and FIX 4.1 rows assert **session** messages present
  (pre-FIXT shape) and pass; all pre-existing version rows still pass — `dictionary_lookup_test` is green
  with the new rows added.
- **SC-003**: A negative test confirms a field type outside both the vocabulary and the full collapse set
  still throws `xml_parse_error` / `dict_xml_parse_failed` — the AC-L8 relaxation is **exactly two named
  aliases**, not a hole (mutation intuition: deleting either new row re-fails the corresponding
  dictionary; adding neither leaves both dictionaries fail-closing).
- **SC-004**: No public API, C-ABI, wire, error-enum, or layout surface changes (verifiable: the diff
  touches only `src/dictionary/xml_loader.cpp`, `dictionaries/`, `spec/`, `specs/064-*/`, and dictionary
  test files; no header/ABI/wire file is modified).
- **SC-005**: The QuickFIX `DATE` divergence and the verified `TIME`/`DATE` mappings are recorded in
  `research.md`, `data-model.md`, and a `behaviors-and-limitations.md` row, each citing the QuickFIX
  `XMLTypeToType` anchor and the successor-typing evidence — grep-verifiable that no project document
  claims `DATE → UtcDateOnly` (the retired hypothesis) or leaves the divergence unstated.
- **SC-006**: The decision record explicitly states the relaxation is global and consistent with the
  existing collapse rows; the vendored FIX 4.2+ dictionaries' resolved typing is unchanged by the addition
  (they use no `DATE`/`TIME` type names) — verifiable by loading them before/after and diffing resolved
  types.
- **SC-007**: The D-004 feature-catalogue rows read `done` with this PR as evidence, and no open tracker
  still lists FIX 4.0/4.1 runtime-XML as an unmet `[const §I.1]` gap.

## Assumptions

- The vendored `FIX40.xml`/`FIX41.xml` at the pinned SHA are structurally loadable once `DATE`/`TIME` are
  accepted — i.e. `DATE` and `TIME` are the **only** two type names they use that are outside the current
  vocabulary. Verified 2026-07-05 by enumerating every distinct `type=` attribute in both files (FIX40:
  `CHAR`, `DATA`, `DATE`, `FLOAT`, `INT`, `LENGTH`, `TIME`; FIX41: adds `DAYOFMONTH`, `MONTHYEAR`,
  `STRING`); the plan phase re-confirms this against the vendored files before implementation.
- `field_data_type::UtcTimestamp` and `field_data_type::LocalMktDate` already exist in the enum (they do —
  they back the canonical `UTCTIMESTAMP` / `LOCALMKTDATE` rows). No enum change is needed or made.
- The existing collapse-table carve-out (post-canonical rows) is the sanctioned precedent for accepting a
  non-`[FIX50SP2 §3.3]` type name; extending it backward for pre-canonical names is the same mechanism,
  not a new one.
- Loading and typing are the scope. This feature does **not** add FIX 4.0/4.1 codegen, typed message
  classes, wire round-trip conformance, or interop tests — only runtime-XML loadability + lookup, matching
  the merged D-005/006 pattern. Any deeper FIX 4.0/4.1 conformance is a separate, lower-priority follow-up
  per `[const §XVIII.6]`.
- QuickFIX at SHA `19ef6a4c` is the reference for the mapping decision; its `DataDictionary::XMLTypeToType`
  is the authoritative statement of how the reference engine resolves these type names.

## Dependencies

- Upstream data: `quickfix/quickfix @ 19ef6a4c…` (`dictionaries/UPSTREAM.txt`), files `spec/FIX40.xml`,
  `spec/FIX41.xml`.
- Reference behavior: QuickFIX `src/C++/DataDictionary.cpp` `XMLTypeToType` (`"TIME" → TYPE::UtcTimeStamp`
  at :675; no `DATE` branch → `TYPE::Unknown`).
- Anchor contract relaxed: `specs/002-dictionary-xml-loader/data-model.md:297` (AC-L8).
- Existing mechanism reused: the field-type collapse table at `src/dictionary/xml_loader.cpp:97-104`.
- Merged precedent: D-005/006 (PR #171) — the pure-data half of A-5; this feature is its loader-touching
  complement.
- Test vehicle: `tests/dictionary/lookup_test.cpp` (the `VersionParam`/`AllRuntimeVersions` suite) and the
  loader's existing AC-L8 negative test.
