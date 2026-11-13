# Feature Specification: Dictionary index width and the MultiCharValue accessor type

**Feature Branch**: `102-dict-width-multichar-type`

**Created**: 2026-11-07

**Status**: Draft (Gate A rewrite 2)

**Input**: REMAINING-WORK ★ V1.0 FUNCTIONAL TRACK row 7, dictionary and codegen half: batch B84 (fixpp#654, L-098-4)
and batch B67 (fixpp#621, L-621-1). Two places where the library returns the wrong answer for a valid dictionary or
a valid value, with no error.

## Context & Problem

1. **Side-table index width (#654, L-098-4).** A loaded dictionary keeps every component's and every group's member
   fields in one side table each, and each definition records where its members start and how many there are, in
   16-bit fields. On a dictionary whose side table holds more than 65535 entries, a later definition's start wraps and
   a large count is clamped. The vendored `FIX50SP2.xml` already does this in its component table: the component
   `RgstDistInstGrp`'s member list is another definition's (098's wrap probe). `Dictionary::component_fields` then
   returns the wrong members, silently. 098 refuses an overlay that would make a fitting table stop fitting, and admits
   a base that already does not fit.

   The same loaders narrow the rest of the class to 16 bits: a component's name offset is clamped at 65535 (on every
   component of FIX50SP1, FIX50SP2 and FIX Latest), and a component's identity (its id, its parent's id, and the
   component index every member `FieldRef` carries) is 16-bit end to end, so a dictionary of more than 65,535
   components gets wrong identities with no error. Orchestra component and group ids, which the Orchestra schema allows
   up to 2147483647, are parsed 16-bit, so a valid Orchestra document with a larger id is refused.

2. **MultiCharValue accessor (#621, L-621-1).** FIX's MultipleCharValue is space-separated single characters, e.g.
   `ExecInst(18)=1 2`. The code generator gives such a field a one-character accessor, and the one-character decoder
   refuses any value that is not one byte, so `exec_inst()` on `18=1 2` fails on `fixpp::v50sp2` and `fixpp::vlatest`.
   The typed builders take a single `char` for the same fields, so a multi-valued value cannot be built through them
   either. The runtime validator already treats these fields as strings, and QuickFIX types them as strings (QuickFIX
   C++ `typedef std::string MULTIPLECHARVALUE` and `StringField`; QuickFIX/J `FieldType.MULTIPLECHARVALUE`, a `String`).

## Clarifications

### Session 2026-11-07 (orchestrator, owner-delegated)

- Q: Widen, or refuse a table that does not fit? → A: Widen. Refusing would refuse the vendored FIX50SP2 (#654's fix
  shape; the batch entry calls widening the expected answer).
- Q: 098's side-table fit check after widening? → A: Retired. A 32-bit value wraps only at 2^32 entries, reachable from
  a small input only through an unbounded base expansion, which B86's absolute budget bounds (research D2).
- Q: MultiCharValue's typed type? → A: A string view, as for MultipleStringValue and as QuickFIX types it. No new
  multi-character view type (nothing in fixpp or QuickFIX needs one; an application splits on spaces).
- Q: Builders too? → A: Yes. A typed builder for a MultiCharValue field takes the same string type its accessor
  returns; a single-character value is a one-character string.

### Gate A round 1, 2026-11-07 (orchestrator ruling, from the binding owner ruling of 2026-11-02 on B86)

- Q: Cap the component count at 65,535, or widen the component identity? → A: **Widen; the cap is dropped.** The B86
  owner ruling (`phases/phase-4/issue-batches.md` §B86, "No byte, element-depth or entry caps") ruled on a note whose
  §"Entry caps" says: "Where a count wraps a 16-bit index (component and group counts, side-table starts), the fix is
  to widen it, which is B84's job, not a cap." So every 16-bit holder of a component index, count or id becomes
  32-bit, with no refusal, and a 65,536-component file that QuickFIX loads loads here too.
- Q: Orchestra component and group ids above 65535? → A: Parsed in the schema's own domain (`fixr:id_t`,
  `xs:positiveInteger`, `maxInclusive 2147483647`). The 16-bit parse is not a format rule, so keeping it would be the
  rejected cap in another place. FIX tags stay 1..65535 (research D1b).
- Q: The 32-bit values that remain (side tables, message runs, the name pool)? → A: Not guarded. The run class is
  bounded by B86's budget, which 102 hands the obligation to; the name pool past 2^32 bytes is disclosed (L-102-1).
  The component identity at 2^32 components is disclosed too (L-102-1 (c), added in Gate A round 2): B86 adds no
  entry cap, and its budget does not count a component with no members.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A component's or group's members and identity are its own on every dictionary (Priority: P1)

An application, or the library's own table build, asks a loaded dictionary for a component's or a group's member
fields, or for a component's id and parent.

**Why this priority**: two wrong answers with no error.

- On a shipped dictionary (FIX50SP2), the public `component_fields("RgstDistInstGrp")` returns another definition's
  members. Nothing in `src/` calls `component_fields`, so the read path is not reached through it.
- The read path's context-free group store is filled from `group_fields`, and is reached only by a base whose group
  side table passes 65,535 entries; no vendored base does. After 100, the wire read reaches that store only on a miss
  in every record store `table_view::wire_group_resolve` probes before it.
- A dictionary of more than 65,535 components gets wrong ids, parents and member `component_index` values.

**Independent Test**: For every vendored dictionary, compare each component's and each group's members as returned by
the dictionary with the members read independently from the source file; for generated dictionaries of more than
65,536 components, compare every identity with the file.

**Acceptance Scenarios**:

1. **Given** the vendored FIX50SP2 dictionary, **When** `component_fields("RgstDistInstGrp")` is called, **Then** it
   returns that component's own members (today: another definition's).
2. **Given** every vendored base (QuickFIX XML and Orchestra), **When** every component's and every group's members and
   every component's name are compared with the raw file, **Then** all match.
3. **Given** a base plus an overlay that adds definitions, **When** the merged dictionary is loaded, **Then** the same
   comparison holds and no side-table limit refuses it.
4. **Given** a QuickFIX XML or Orchestra base of more than 65,536 components whose tables all stay below 2^32
   entries (FR-001's condition; L-102-1), **When** it is loaded, **Then** it loads, and every component's
   `component_id`, `parent_component_id`, members, and each member's `component_index`, are its own.
5. **Given** an Orchestra document whose component and group ids pass 65535, **When** it is loaded, as a base or with
   an overlay naming those ids, and when the code generator reads it, **Then** it loads and every reference resolves to
   the definition it names.

---

### User Story 2 - A multi-valued MultiCharValue field reads and builds (Priority: P1)

An application reads `ExecInst(18)=1 2` from an inbound `fixpp::v50sp2` or `fixpp::vlatest` message through the typed
accessor, or builds one through the typed builder.

**Acceptance Scenarios**:

1. **Given** a v50sp2 NewOrderSingle carrying `18=1 2`, **When** `exec_inst()` is read, **Then** it returns `"1 2"`.
2. **Given** a single-valued `18=1`, **Then** it returns `"1"`.
3. **Given** the v50sp2 and vlatest builders, **When** an `exec_inst` of `"1 2"` is built, **Then** the wire carries
   `18=1 2`, and the frame round-trips through the typed reader.
4. **Given** v42, v44 and FIXT.1.1, **Then** nothing changes (no MultiCharValue fields).

### Edge Cases

- A MultiCharValue field inside a repeating group (e.g. `LegExecInst(1384)`): same type, through the group entry.
- An owning (reified) message class: same string type as the view accessor.
- A dictionary that types a field `MULTIPLECHARVALUE` in one version and `CHAR` in another: each version's generated
  type follows its own dictionary.
- A child component whose parent sits at index 65,535 or beyond: its `parent_component_id` is the parent's id + 1, not 0.
- An Orchestra id of exactly 2147483647 loads; 2147483648 is a parse error (the schema's bound). An Orchestra id of 0
  stays admitted, as today.
- A base just under 65,536 components plus an overlay that crosses it: loads, with correct identities.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001** (#654): No index, start, offset or count a loader records for a component, group or field definition is
  held in fewer than 32 bits unless its source's own domain bounds it: a FIX tag (1..65535, the library's tag type), a
  group index (one group per NumInGroup tag), or a member no loader writes (`condition_index`, `enum_table_index`).
  No value is clamped. A 32-bit value is exact while its table stays below 2^32 entries (L-102-1).
- **FR-001a** (#654): The component identity (`ComponentRef::component_id`, `parent_component_id`,
  `FieldRef::component_index`, and every loader-internal counter, map and one-based cast that carries it) is 32-bit in
  both loaders. No component count is refused. Each component's identity is exact while the component count stays
  below 2^32 (FR-001's condition). At exactly 2^32 components the last component's zero-based `component_id`
  (`UINT32_MAX`) is still exact, but its one-based value does not fit in 32 bits: every `FieldRef` whose enclosing
  component it is carries `component_index` 0, and every component whose parent it is carries `parent_component_id` 0
  (both meaning "none"); if it has no members and is no component's parent, no value is wrong. Above 2^32 components
  the zero-based `component_id`, and the XML loader's component index by name, wrap too. An Orchestra base cannot reach it: its component ids are distinct `fixr:id_t`
  values, so it holds at most 2^31 components. A QuickFIX XML base reaches it only with 2^32 distinct component names,
  which first puts the name pool past 2^32 bytes (L-102-1 (b)). Disclosed as L-102-1 (c); no guard.
- **FR-001b** (#654): Orchestra component, group, componentRef and groupRef ids are read in the schema's `fixr:id_t`
  domain, by the loader, the overlay merge and the code generator alike. FIX tag ids stay 1..65535.
- **FR-002** (#654): `Dictionary::component_fields` and `Dictionary::group_fields` MUST return each definition's own
  members, and `Dictionary::component` each component's own id, parent id and name offset, for every component and
  group of every vendored base, with and without an overlay, and of a base of more than 65,536 components whose tables
  all stay below 2^32 entries (FR-001's condition: components, side-table entries, name-pool bytes; L-102-1 (a)–(c)).
- **FR-003** (#654): 098's side-table fit refusal is removed; B-098-1 is amended and L-098-4 closed.
- **FR-004** (#621): Every generated typed accessor of a MultiCharValue field (view and owning classes, top level and
  group entries) MUST return the same string type MultipleStringValue fields return, and decode any value.
- **FR-005** (#621): Every generated typed builder input for a MultiCharValue field MUST take that string type and
  write it verbatim.
- **FR-006**: The generated text of every other field is unchanged, except that every generated `FieldRef` row drops
  its trailing `_reserved` initializer; every generated-file pin that moves is re-pinned with the reason, and the
  accessor census's value-type table maps MultiCharValue to the string kind.
- **FR-007**: The C ABI and Python binding are unchanged (neither exposes these types). The C++ changes are declared as
  breaking in the B&L (B-102-1: `FieldRef`, `ComponentRef`, `GroupRef` layouts; B-102-2: MultiCharValue accessor and
  builder types; B-102-3: the loads that now succeed), with the migration in `contracts/cpp-api-breaks.md`.
- **FR-008** (#654): Every dictionary run reader does its bounds arithmetic so that it cannot wrap (`std::size_t`, or
  `start <= size && count <= size - start`); a record whose start and count would overrun its table returns an empty
  span.

### Key Entities

- **Component/group definition record**: identity, name, start and count of its members in the side table.
- **Typed accessor kind**: the generated value type of a field, derived from its dictionary type.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The member comparison of US1.2 is RED on the parent base `99e956a0` for `RgstDistInstGrp` on FIX50SP2, and the
  name-offset comparison RED on FIX50SP1, FIX50SP2 and FIX Latest; both GREEN on the feature head for every component and
  group of every vendored base.
- **SC-002**: `exec_inst()` on `18=1 2` is RED on the parent base `99e956a0` and GREEN on the head, on v50sp2 and vlatest; the builder
  round-trip of US2.3 likewise.
- **SC-003**: Generating every tier with the base and the head code generators and diffing the trees shows only
  MultiCharValue hunks for tags {18, 286, 291, 292, 529, 546, 1308, 1384} and the `FieldRef` row-trailer change; the
  filter is shown able to report a stray hunk; each moved pin names this feature. The base is the parent base:
  `99e956a0` (101's Gate B head, which 102 is stacked on) until the post-squash rebase onto `origin/main`, and the
  merge-base of the candidate and `origin/main` after it (tasks.md Execution rules' **Base**); the deciding run is the
  one after that rebase, or the run before it when the rebase's base delta is empty (tasks.md T055),
  and the verify record names the base commit.
- **SC-004**: `xml_loader_bench`, paired by Article VIII §2's local protocol against the parent base at measurement
  time (`99e956a0` until the post-squash rebase, the merge-base of the candidate and `origin/main` after it; the
  deciding run is the one after that rebase), stays within +5% or carries the §2 approval; the table-view footprint of that base and of the head
  is recorded. The verify record names the base commit.
- **SC-005**: The identity cells of US1.4 and US1.5 are RED on the parent base `99e956a0` and GREEN on the head, for both formats.

## Assumptions

- The side tables are built in `finalize` in a known order, so the expected start of every definition can be
  recomputed independently in a test.
- Generated headers are built, not checked in; their goldens and SHA pins are checked in and republished by hand.
- The precompiled builder libraries are C++ only and pre-release, so their ABI change needs no compatibility shim.
- B86 (#650) follows this feature and inherits the 32-bit run obligation (research D2).

## Out of Scope

- B86 (#650, base loader input caps and the component-cycle guard), which follows this feature on the same `finalize`
  paths.
- The 32-bit residual, disclosed as L-102-1: (a) until B86, an unbounded base expansion can wrap a 32-bit run end;
  (b) a name pool past 2^32 bytes (4 GiB or more of names) gives wrong names, in bounds, and outlives B86;
  (c) a QuickFIX XML base of 2^32 or more components can give wrong component identities, in bounds, with no error:
  at exactly 2^32, the members and the children of the last component carry 0 for its one-based id
  (`component_index`, `parent_component_id`); above 2^32, zero-based `component_id`s wrap too. It
  needs 2^32 distinct names, so (b) is reached first; an Orchestra base cannot reach it (distinct ids, at most 2^31).
  B86's budget does not bound the component count (a component with no members costs no step), and B86 adds no entry
  cap, so (c) outlives B86, as (b) does.
- The library's 16-bit FIX tag type (an Orchestra field id above 65535 stays refused).
- An enum value-domain check for MultiCharValue fields (L-069-1, fixpp#637).

## Normative References

- **Spec refs** (`[const §VI.5]` form, titles as `spec/coverage-index.md` spells them):
  - `[FIX50SP2 §3.3] Field data types (28 types: STRING, CHAR, INT, FLOAT, BOOLEAN, UTCTIMESTAMP, etc.)` (W-009;
    FR-004, FR-005);
  - `[FIX50SP2 §3.2] Repeating groups (NoXxx delimiter, ordered field list, nested groups)` (W-006, W-007, D-010;
    FR-002, the group-entry edge case);
  - [FIX-Orchestra] FIX Orchestra machine-readable standard — `fixr:repository` schema (datatypes, codesets, fields,
    components, groups, messages) (D-011; FR-001b: `fixr:id_t` in `repositorytypes.xsd`,
    FIXTradingCommunity/fix-orchestra `master`, read 2026-11-07; informative, not vendored). Written outside a code span
    so the title's own backticks are kept as the coverage index spells them.
- **QuickFIX reference behaviour** (cited, not vendored; read at the local clones): QuickFIX C++ `2ce8a606`
  (`FieldTypes.h` `MULTIPLECHARVALUE`, `Field.h` `MultipleCharValueField`, `DataDictionary.cpp` `XMLTypeToType`);
  QuickFIX/J `4969564f` (`quickfixj-base` `FieldType.MULTIPLECHARVALUE`). B86's note read QuickFIX C++ `386ce46e`,
  QuickFIX/J `bc7e8b23` and QuickFIX/Go `eff945dd` for the loader's expansion; QuickFIX/Go's MultipleCharValue mapping
  was not read for 102.
- **Catalogue rows**: D-007 (the loaders), D-010 (components), D-011 (the Orchestra reader), W-009 (field data types).
- **Constitution**: `[const §I]`; `[const §VI.5]`; `[const §VII]`; `[const §VIII.2]`; `[const §X.1]` (C ABI
  unchanged); `[const §XV.13]`; `[const §XVII.1]`. The whole check is in `plan.md`.
- **Design documents**: `[2c §4.1]`, `[2c §4.2]` (the record layouts this feature amends), `[2c §1]` and
  `[2c §4.1.3]` (the field representation types and the `field_traits` listing, whose MultiCharValue / MultiStringValue
  split this feature removes); research D8.
