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
   The parity claim is made against those two engines only; QuickFIX/Go's mapping was not read (Normative References).

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
  in every record store `table_view::wire_group_resolve` probes before it. The validator reaches it too, on a context
  miss: Step 3's level walk (`validate_group_level`) and `consume_group` ask `table_view::group_first_field(msg_type, parent_path, no_tag)` and
  `group_member_tags(msg_type, parent_path, no_tag)`, and each falls back to the bare store when `group_ctx_` has no
  record for that context (for example a NumInGroup tag at top level that the message declares only at a nested
  path). Those members decide each instance's extent, so a validating session's Reject or delivery can depend on
  them (contract §6 class (g)).
- A dictionary of more than 65,535 components gets wrong ids, parents and member `component_index` values.

**Independent Test**: For every vendored dictionary, compare each component's and each group's members as returned by
the dictionary with the members read independently from the source file; for generated dictionaries of more than
65,535 components, compare every identity with the file.

**Acceptance Scenarios**:

1. **Given** the vendored FIX50SP2 dictionary, **When** `component_fields("RgstDistInstGrp")` is called, **Then** it
   returns that component's own members (today: another definition's).
2. **Given** every vendored base (QuickFIX XML and Orchestra), **When** every component's and every group's members and
   every component's name are compared with the raw file, **Then** all match.
3. **Given** a base plus an overlay that adds definitions, **When** the merged dictionary is loaded, **Then** the same
   comparison holds and no side-table limit refuses it. The overlay caps are unchanged (`kMaxNewComponents`,
   `kMaxNewGroups` and the other caps B-098-1 lists): an overlay over a cap is still refused.
4. **Given** a QuickFIX XML or Orchestra base of more than 65,535 components whose tables all stay below 2^32
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
4. **Given** v42, v44 and FIXT.1.1, **Then** no accessor or builder input changes type (no MultiCharValue fields);
   their generated `Fields.hpp` rows change only by FR-006's row trailer.

### Edge Cases

- A MultiCharValue field inside a repeating group (e.g. `LegExecInst(1384)`): same type, through the group entry.
- An owning (reified) message class: same string type as the view accessor.
- A dictionary that types a field `MULTIPLECHARVALUE` in one version and `CHAR` in another: each version's generated
  type follows its own dictionary. The vendored files do this (FIX42 types fields that FIX50SP2 types
  `MULTIPLECHARVALUE` as `CHAR`; re-derive: each MultiCharValue tag's `type` attribute in every file under
  `dictionaries/`), so the v42 accessors of those tags stay `char`; the witness is SC-003's differential (no v42 hunk).
- A child component whose parent sits at index 65,535 (its one-based value, 65,536, is the first that wrapped on the
  parent base) and one whose parent sits beyond it: each `parent_component_id` is the parent's id + 1, not 0.
- An Orchestra id of exactly 2147483647 loads; 2147483648 is a parse error (the schema's bound). An Orchestra id of 0
  stays admitted, as today.
- An Orchestra component or group whose id is 1024 or 1025 plus a multiple of 65,536 is an ordinary definition: it is
  neither the header (1024) nor the trailer (1025).
- A base just under 65,536 components plus an overlay within `kMaxNewComponents` that crosses it: loads, with correct
  identities.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001** (#654): No index, start, offset or count a loader records for a component, group or field definition is
  held in fewer than 32 bits unless its source's own domain bounds it: a FIX tag (1..65535, the library's tag type), a
  group index (both loaders deduplicate groups by NumInGroup tag, a FIX tag, so a loader holds at most one group per
  tag and its 16-bit group index cannot wrap), or a member no loader writes or reads (`condition_index`,
  `enum_table_index`, and a loader-internal member nothing writes or reads, such as XML `GroupDef`'s
  `first_field_index` and `field_count`, which tasks T029 classifies). No value is clamped. A 32-bit value is exact
  while its table stays below 2^32 entries (L-102-1).
- **FR-001a** (#654): The component identity (`ComponentRef::component_id`, `parent_component_id`,
  `FieldRef::component_index`, and every loader-internal counter, map and one-based cast that carries it, and every
  read of such a map's value; re-derive the reads: tasks `## Census manifest` "D1 identity readers") is 32-bit in both
  loaders. In both formats `component_id` is the component's zero-based declaration index, not an Orchestra
  document's `fixr:component id`. Both loaders write one encoding: `component_index` and `parent_component_id` are 0
  for "none" (not in a component, top level) and otherwise the component's `component_id` + 1, so below 2^32
  components 0 never names a component. No component count is refused: 102 removes only the width-driven wrong
  answers, and the loaders' other refusals (an empty or repeated component name or id, an allocation failure, and
  B86's budget once it lands) are unchanged. Each component's identity is exact while the component count stays
  below 2^32 (FR-001's condition). At exactly 2^32 components the last component's zero-based `component_id`
  (`UINT32_MAX`) is still exact, but its one-based value does not fit in 32 bits: every `FieldRef` whose enclosing
  component it is carries `component_index` 0, and every component whose parent it is carries `parent_component_id` 0
  (both meaning "none"); if it has no members and is no component's parent, no value is wrong. Above 2^32 components
  the zero-based `component_id`, and the XML loader's component index by name, wrap too. An Orchestra base cannot reach it: its component ids are distinct `fixr:id_t`
  values, so it holds at most 2^31 components. A QuickFIX XML base reaches it only with 2^32 distinct component names,
  which first puts the name pool past 2^32 bytes (L-102-1 (b)). Disclosed as L-102-1 (c); no guard and no signal.
- **FR-001b** (#654): Orchestra component, group, componentRef and groupRef ids are read in 0..2147483647, by the
  loader, the overlay merge and the code generator alike: the schema's `fixr:id_t` (`xs:positiveInteger`,
  `maxInclusive 2147483647`) with two departures, both kept from today so that 102 adds no refusal. 0 is admitted, as
  the loader admits it today, although `positiveInteger` excludes it. The lexical rule is today's id parse, with only
  its upper bound raised: the whole attribute is read by `std::from_chars` into a signed integer with nothing left
  over, so leading zeros are admitted and a leading `+` or surrounding whitespace, which `xs:positiveInteger` admits,
  is refused. A value outside the domain, or one that rule refuses, is `orchestra_parse_error`, as today.
  - The loader's parse decides the ending. The code generator loads its input with `OrchestraLoader::load` before its
    own walk, and an overlay load ends in the loader's build of the spliced document. So the generator's and the
    merge's parse misses are reached only on input the loader refuses (an earlier overlay guard may end such a load
    first, with its own error).
  - The code generator is a build tool run over the vendored dictionaries. For it, US1.5 requires that its walk
    resolve every reference as the loader does on the same document (tasks T016 runs it over a fixture); it does not
    make generating code from another Orchestra document a supported use.
  - A component id and a group id are separate key spaces: a document may give a component and a group the same id.
    No container keys both kinds by id alone. In the loader, the overlay graph build and `ir.cpp`, each surrogate
    container the D1b holder recipe reports holds one kind, and the overlay's child key carries the element name.
    100's test oracle, which keyed both in one domain, is re-keyed by (kind, id) (tasks T015b, T034a). The
    loader's witness is tasks T012's shared-id control. The overlay graph build's `component_by_id` and
    `group_by_id`, and `ir.cpp`'s two `by_id` maps, hold one kind per container and the overlay's child key carries
    the element name, by construction: the D1b holder recipe's classification shows it (tasks T025, T035), and no
    cell of its own is written for them.
  - Witnesses for the lexical rule are tasks T012's lexical controls: a leading zero loads; a leading `+` and
    surrounding whitespace are refused.
  - FIX tag ids (`<fixr:field id>`, `fieldRef`, `lengthId`, `numInGroup`) stay 1..65535 and are refused as today,
    with the same exception and the same message per kind (research D4 (g)).
- **FR-002** (#654): `Dictionary::component_fields` and `Dictionary::group_fields` MUST return each definition's own
  members, and `Dictionary::component` each component's own id, parent id and name offset, for every component and
  group of every vendored base, with and without an overlay, and of a base of more than 65,535 components whose tables
  all stay below 2^32 entries (FR-001's condition: components, side-table entries, name-pool bytes; L-102-1 (a)–(c)).
  - A component's own name offset is one at which the name-pool slice of the name's length equals the component's
    name (research D4 "name offset"); `name_offset` has no public reader.
  - The vendored bases are every dictionary file under `dictionaries/` and `dictionaries/orchestra/` (re-derive: the
    `*.xml` files there), FIX50SP1 included although it is not a codegen target.
  - "With an overlay" means the equivalence fixture's overlays over those bases (the empty overlay and the
    patched-base overlays of the `OverlayEquivalenceW2*` rows), plus US1.3's and the Edge Cases' overlays that add
    definitions.
- **FR-003** (#654): 098's side-table fit refusal is removed; B-098-1 is amended in place and L-098-4 closed.
  - B-098-1 loses the side-table fit from the walk's refusals, with its "(L-098-4)" pointer, and from the causes it
    lists for `dict::overlay_limit_error`.
  - The overlay's one walk over the merged reference graph then refuses a component cycle and an expansion over the
    budget only. B-098-1's amendment states that order, as do the live texts research D8 amends: `xml_loader.hpp`'s
    `load_with_overlay` comment, `error.hpp`'s `overlay_limit_error` comment and `overlay_merge.hpp`'s guard-order
    banner.
  - `overlay_limit_error` stays, for its other causes (a cap, a DOCTYPE, the expansion budget, the step counter).
- **FR-004** (#621): Every generated typed accessor of a MultiCharValue field (view and owning classes, top level and
  group entries) MUST return the same string type MultipleStringValue fields return, and decode any value.
  - A MultiCharValue field is one the loader types `field_data_type::MultiCharValue`: in QuickFIX XML,
    `type="MULTIPLECHARVALUE"`; in Orchestra, the datatype `MultipleCharValue`, named directly or as the `type` of the
    code set the field names. The generator's kind reads that loaded type.
  - "Any value" is whatever the String kind's decoder returns for a MultipleStringValue field. There is no check of
    the MultipleCharValue grammar: a token longer than one character and a doubled or trailing space are returned
    verbatim (tasks T019's cells), and an empty value ends as it does for a MultipleStringValue field. That holds by
    construction: the same kind emits the same accessor text over the same decoder, and tasks T022's census
    witnesses the kind.
  - 102 changes neither the generated validators nor the runtime validator (neither reads the kind; research Survey
    "Codegen"), so a typed read and a validated read take the same value as the same string.
- **FR-005** (#621): Every generated typed builder input for a MultiCharValue field MUST take that string type and
  write it verbatim, through the path a MultipleStringValue input takes. A value no FIX field may carry (an empty
  value, an embedded SOH) ends exactly as it does for a MultipleStringValue input. That holds by construction: the
  same member type selects the same builder call, and tasks T021's type check witnesses the type. 102 adds no allocation path of
  its own (`[const §XV.1]`; plan Constitution Check).
- **FR-006**: The generated text of every other field is unchanged, except that every generated `FieldRef` row drops
  its trailing `_reserved` initializer; every generated-file pin that moves is re-pinned with the reason, and the
  accessor census's value-type table maps MultiCharValue to the string kind.
- **FR-007**: The C ABI's and the Python binding's types and symbols are unchanged (neither exposes these records),
  but their results change. The C++ changes are declared as breaking in the B&L (B-102-1: `FieldRef`, `ComponentRef`,
  `GroupRef` layouts; B-102-2: MultiCharValue accessor and builder types; B-102-3: the loads that now succeed), with
  the migration in `contracts/cpp-api-breaks.md`, and in the PR description (tasks T069).
  - **The C-ABI result change is BREAKING under `[const §X.7]`.** Owner ruling, 2026-11-13: "All BREAKING, bump
    (Recommended)". It has three kinds, each class in contract §6:
    - a failure turned into a success: (a), (b), (c), and Python's wrappers (d), which return where they raised;
    - the same code, and a different dictionary: (f) and (g), whose effect reaches every downstream call
      data-model.md Appendix A marks BREAKING;
    - a failure turned into a different failure, or into a success: (a′) and (b′).
  - The population is derived, not listed by example: data-model.md Appendix A classifies every export of
    `tests/abi/golden/fixpp_capi_symbols.txt` by one predicate, and research D8's C-ABI population recipe re-derives
    it at the head (tasks T067).
  - `FIXPP_C_ABI_VERSION_MINOR` moves to one past the value `main` holds after the post-squash rebase (tasks T055,
    T059a). The BREAKING mark goes where C-ABI 1.9 put it: a root-cause note on both `dict.h` load declarations; one
    family paragraph in each header whose calls Appendix A marks (`message.h`, `session.h`); `version.h`'s version
    comment, for the effects no declaration carries; B-102-3; and the PR description (tasks T069). Every
    in-repository consumer the version touches is updated in the same PR.
  - Appendix A's controls for this ABI-affecting change: the owner's sign-off is the ruling above; the Codex Gate A
    review scoped to the C-ABI delta is in progress, and its state is plan Constitution Check row X's.
- **FR-008** (#654): Every dictionary run reader does its bounds arithmetic so that it cannot wrap (`std::size_t`, or
  `start <= size && count <= size - start`); a record whose start and count would overrun its table returns an empty
  span.
  - A run reader is any reader that turns a record's or a run's start and count into a range over a table. Re-derive
    the set with research D7's two recipes and tasks `## Census manifest`'s D7 sum supplement. It includes the
    readers that build a span with no bounds check today: `field_ref_impl`, `required_fields_impl` and
    `find_incomplete_group_context` (tasks `## Bundle findings` 6).
  - `name_at`, the name-pool reader, is not a run reader under this rule (L-102-1 (b)).
  - The overrun ending, per reader shape:
    - a reader that returns a span returns an empty span;
    - a reader that returns a value returns its not-found value: `field_ref_impl` returns a default `FieldRef`, as
      for an undeclared tag;
    - a reader that walks a run element by element (`for_each_group_ctx_record`) visits nothing;
    - the load-time check `find_incomplete_group_context` reads the run as empty.
  - The guard is arithmetic only: it adds no allocation and no exception, and the readers keep `noexcept`.
  - A caller cannot tell an overrunning record from an empty definition or a miss. One ending for both is acceptable
    because no loader writes an overrunning record while its tables stay below 2^32 entries (FR-001's condition). The
    guard turns a record past that condition into an empty read, not an out-of-bounds one.

### Key Entities

- **Component/group definition record**: identity, name, start and count of its members in the side table.
- **Typed accessor kind**: the generated value type of a field, derived from its dictionary type.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The member comparison of US1.2 is RED on the parent base `99e956a0` for `RgstDistInstGrp` on FIX50SP2, and the
  name-offset comparison RED on FIX50SP1, FIX50SP2 and FIX Latest; both GREEN on the feature head for every component and
  group of every vendored base.
- **SC-002**: `exec_inst()` on `18=1 2` is RED on the parent base `99e956a0` and GREEN on the head, on v50sp2 and vlatest; the builder
  round-trip of US2.3 likewise. On the parent base the builder cell's RED is its assignability check's failure branch
  (a `std::string_view` value is not assignable to the member); on the head that check and the cell's wire and
  read-back assertions are GREEN.
- **SC-003**: Generating every tier with the base and the head code generators and diffing the trees shows only
  MultiCharValue hunks for tags {18, 286, 291, 292, 529, 546, 1308, 1384} and the `FieldRef` row-trailer change; the
  filter is shown able to report a stray hunk, both in a file with no MultiCharValue hunk and inside a file that
  carries them; each moved pin names this feature. The tag set is the one tasks T042's recipe re-derives per tier
  from the dictionary files; where it differs from the list above, the re-derived set governs and the difference is
  recorded. The base is the parent base:
  `99e956a0` (101's Gate B head, which 102 is stacked on) until the post-squash rebase onto `origin/main`, and the
  merge-base of the candidate and `origin/main` after it (tasks.md Execution rules' **Base**); the deciding run is the
  one after that rebase, or the run before it when the rebase's base delta touches none of the paths tasks T057
  names (a golden, a pin, `tools/codegen`, `dictionaries`), the condition under which T058 is N/A,
  and the verify record names the base commit.
- **SC-004**: `xml_loader_bench`, paired by Article VIII §2's local protocol against the parent base at measurement
  time (`99e956a0` until the post-squash rebase, the merge-base of the candidate and `origin/main` after it; the
  deciding run is the one after that rebase), stays within +5% or carries the §2 approval; the table-view footprint of that base and of the head
  is recorded. The verify record names the base commit.
  - The bench's dictionaries are its `BENCHMARK` registrations. They must include the vendored QuickFIX XML base with
    the most components (re-derive: count each file's component definitions).
  - No paired bench exists for `OrchestraLoader::load` or the overlay merge (re-derive: the `bench/dictionary/` rows
    of `bench/ci-suite.txt`), and 102 adds none. Their per-element work changes as `XmlLoader::load`'s does, by record
    width only, which `xml_loader_bench` measures.
  - A run before the rebase decides nothing. Above +5% it is recorded, and only the deciding run passes SC-004 or
    needs the approval.
  - The approval is `[const §VIII.2]`'s: the measurement, a rationale, and approval by someone other than the change's
    author.
  - The footprint half cannot fail and is a control. `table_view_footprint_bench` counts `table_view`'s stores, which
    hold tags, not records, so it is expected unchanged; a change in it is recorded and explained.
  - The growth of a loaded dictionary is stated as a derivation, computed at verify time for each vendored base: per
    component, `sizeof(ComponentRef)` on the head minus on the base; per group, the same for `GroupRef`; `FieldRef`'s
    `sizeof` is unchanged. Each is multiplied by the base's component or group count.
  - Article VIII sets no memory budget for a loaded dictionary, so the growth is recorded, not checked against one.
- **SC-005**: The identity cells of US1.4 and US1.5 are RED on the parent base `99e956a0` and GREEN on the head, for both formats.
  On the parent base the Orchestra cells' RED is `orchestra_parse_error` on the first id past 65535; on the head the
  same cells' identity assertions are GREEN.
- A cell whose RED cannot be shown on the parent base satisfies its SC with a RED at the later head its task names, or
  under the mutant its `## Mutant table` row names (tasks Execution rules **H0**).

## Assumptions

- The side tables are built in `finalize` in a known order, so the expected start of every definition can be
  recomputed independently in a test. The order is research "Re-derivation recipes"' "Side-table starts", for both
  loaders: components in declaration order, each fully expanded, then groups by NumInGroup tag. With an overlay it
  is the same order over the spliced document. The equivalence fixture's census reads it from the raw file, never
  from the loader. Its existing rows, GREEN on the parent base below the wrap for both formats, show that the census
  order and each loader's agree.
- Generated headers are built, not checked in; their goldens and SHA pins are checked in and republished by hand.
- The precompiled builder libraries are C++ only and pre-release, so their ABI change needs no compatibility shim.
- B86 (#650) follows this feature and inherits the 32-bit run obligation (research D2).

## Out of Scope

- B86 (#650, base loader input caps and the component-cycle guard), which follows this feature on the same `finalize`
  paths.
- The 32-bit residual, disclosed as L-102-1 (research D8 holds its one statement; this is a copy): (a) until B86, an
  unbounded base expansion can wrap a 32-bit run end (the side tables, the per-message runs, and 100's delimiter and
  section member pools); on the overlay path an overlay adds at most `kExpansionBudget` beyond the base's own
  expansion (the step counter is armed at the base's total plus that budget, research D2), so only a base's own
  expansion reaches it, and the base-only load paths have no bound until B86;
  (b) a name pool past 2^32 bytes (4 GiB or more of names) gives wrong names, in bounds, and outlives B86;
  (c) a QuickFIX XML base of 2^32 or more components can give wrong component identities, in bounds, with no error:
  at exactly 2^32, the members and the children of the last component carry 0 for its one-based id
  (`component_index`, `parent_component_id`); above 2^32, zero-based `component_id`s wrap too. It
  needs 2^32 distinct names, so (b) is reached first; an Orchestra base cannot reach it (distinct ids, at most 2^31).
  B86's budget does not bound the component count (a component with no members costs no step), and B86 adds no entry
  cap, so (c) outlives B86, as (b) does. No call reports that a dictionary is in region (b) or (c): there is no
  error and no query, and every value read is in bounds. Each leg that outlives B86 gets a tracking issue, which its
  B&L row cites (tasks T047, T048).
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
- **Constitution**: `[const §I]`; `[const §VI.5]`; `[const §VII]`; `[const §VIII.2]`; `[const §X.1]`,
  `[const §X.6]` and `[const §X.7]` (the C-ABI result change declared BREAKING, owner ruling 2026-11-13: "All
  BREAKING, bump (Recommended)"; types and symbols unchanged; FR-007, contract §6, data-model.md Appendix A);
  `[const §XV.13]`; `[const §XVII.1]`. The whole check is in `plan.md`.
- **Design documents**: `[2c §4.1]`, `[2c §4.2]` (the record layouts this feature amends), `[2c §1]` and
  `[2c §4.1.3]` (the field representation types and the `field_traits` listing, whose MultiCharValue / MultiStringValue
  split this feature removes); research D8.
