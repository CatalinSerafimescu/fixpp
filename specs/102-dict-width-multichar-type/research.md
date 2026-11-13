# Research: 102 dictionary index width and the MultiCharValue accessor type

Code read at `fa778a8e`. Symbols, not lines. Every inventory below is stated with the recipe that re-derives it; the
recipe, not a list or a count, is authoritative.

**Base (owner ruling, 2026-11-13).** 102 is stacked on `99e956a0` (101's Gate B head, on `origin/main` `6714582d`,
where 100 is merged), the parent base `$P` of tasks.md's Execution rules. Where this file names `fa778a8e` as a RED
base or a recipe's known positive, read `$P`: the recipes were re-run at `$P` for the 2026-11-13 analysis, the classes
of their hits there are in tasks.md `## Census at tasks time`, and T005 re-runs each on its known positive at `$P`
(one that does not report there stops). The sites 100 added are named below where a decision reaches them (D1b, D4
row (b2), D7, the §B86 text, L-102-1 (a)); D9 records the ruling. The Gate A text is otherwise unchanged.

## Survey

### Dictionary records (#654)

- `ComponentRef` (`include/fixpp/dict/component_ref.hpp`) and `GroupRef` (`group_ref.hpp`): six `std::uint16_t` each,
  `sizeof == 12` asserted in both headers and in `tests/dictionary/ref_shape_test.cpp`. `FieldRef` (`field_ref.hpp`)
  is 16 bytes at `alignof == 2`: `tag`, `type`, `rule`, then 16-bit `condition_index`, `group_no_tag`,
  `component_index`, `enum_table_index`, `length_pair_data_tag`, `_reserved`.
- Internal indexes are already 32-bit (`NameSlice`, `MsgFieldsRun`, `NamedIndex`, `EnumRun` in
  `src/dictionary/dictionary_internal.hpp`).
- Each loader's `finalize` (`XmlLoader` in `src/dictionary/xml_loader.cpp`, `OrchestraLoader` in
  `src/dictionary/orchestra_loader.cpp`) narrows, per component and per group:
  - the side-table start, `static_cast<std::uint16_t>(…size())` → **wraps on FIX50SP2's component table**
    (first at `RgstDistInstGrp`; the wrong slice still passes `component_fields_impl`'s bounds check);
  - the member count, `std::min<std::size_t>(…, 65535U)` → saturates; no vendored count reaches it;
  - the component name offset, `std::min<std::uint32_t>(ns.offset, 65535)` → **clamped for every component of FIX50SP1,
    FIX50SP2 and FIX Latest**; no `src/` reader (component lookup uses the 32-bit `NamedIndex`); the only reader is the
    equivalence fixture, which agrees because both sides saturate.
- **Component identity is 16-bit end to end.** The holders are the public `ComponentRef::component_id`,
  `ComponentRef::parent_component_id` and `FieldRef::component_index`, and inside the loaders: the XML loader's
  `collect_components` counter `next_id` and `component_index_by_name_`; Orchestra's `collect_components` index
  (`static_cast<std::uint16_t>(components_.size())`) and `component_index_by_xml_id_`; both loaders' `parent_of_`
  maps and their one-based `index + 1` casts; `expand_field_list`'s `enclosing_component_index` parameter and its
  `cit->second + 1` argument; `finalize`'s `i + 1`, `parent_comp_id`, `PendingComponent::index` and
  `cr.component_id = static_cast<std::uint16_t>(i)`; and `dict_metadata_handle::component_fields_impl`'s
  `component_id` parameter. Nothing bounds the component count:
  - QuickFIX XML: `next_id` wraps at the 65,537th component, which then duplicates index 0 in
    `component_index_by_name_`; already at the 65,536th, the one-based `i + 1` and `pit->second + 1` casts wrap to 0,
    which means "not in a component" / "top level".
  - Orchestra: `<fixr:component id>` is parsed 16-bit, so at most 65,536 distinct ids (0..65535) are expressible; on
    such a document `i + 1` wraps exactly as in XML.
  - No vendored base comes near; the wrap is a silent wrong answer, not a refusal.
- **Orchestra surrogate ids.** `parse_orchestra_id` (and `detail::try_parse_uint16` at the tolerant reads) parse every
  Orchestra `id` attribute 16-bit: field ids, `lengthId` and `numInGroup` ids (FIX tags) and component, group,
  componentRef and groupRef ids (document-local surrogate keys). The Orchestra repository schema types `id` as
  `fixr:id_t`, an `xs:positiveInteger` restricted by `maxInclusive 2147483647` (`repositorytypes.xsd` in
  FIXTradingCommunity/fix-orchestra, branch `master`, read 2026-11-07; the schema is not vendored). So a component or
  group id above 65535 is schema-valid and is refused by fixpp today: the 16-bit parse is fixpp's width, not a format
  rule. The vendored `OrchestraFIXLatest.xml` uses small ids (re-derive: the largest `id` of each element kind, by a
  regex over the file). The same 16-bit parse is repeated, as silent skips, in `overlay_merge.cpp` (`orch_id_key_of`,
  `orch_child_key`, and the Orchestra graph build's `component_by_id`, `group_by_id` and `records_of`), in the code
  generator (`tools/codegen/fixpp-codegen/ir.cpp`: `try_parse_orchestra_uint16`, `OrchestraComponentIndex::by_id`,
  `OrchestraGroupIndex::by_id`), and in the equivalence fixture's Orchestra raw reader
  (`tests/dictionary/dictionary_equivalence.hpp`, `static_cast<std::uint16_t>(….as_uint(0))`).
  - The schema's `positiveInteger` excludes 0. The comment above `parse_orchestra_field_tag` calls 0 a legal component
    id, and the loader accepts it. 102 keeps accepting 0 (no new refusal); the comment's claim is contradicted by the
    schema, which D8's comment amendment records.
- **Group identity is bounded by the tag domain.** Both loaders deduplicate groups by NumInGroup tag
  (`group_index_by_no_tag_`, first seen wins), and tags are 1..65535 (fixpp#457), so the group vector has at most
  65,535 entries and its 16-bit index (`static_cast<std::uint16_t>(groups_.size())`) cannot wrap. `GroupRef` holds no
  group index: `no_tag`, `first_field_tag` and `parent_group_no_tag` are tags.
- Group starts and counts do not exceed 16 bits on any vendored base (B86's note records the vendored side-table
  totals).
- `FieldRef::condition_index` and `enum_table_index` are never written (always 0).
- Readers: `Dictionary::component_fields` / `component_fields_impl`, `group_fields` / `group_fields_impl`
  (`src/dictionary/dictionary.cpp`). `component_fields` has no caller in `src/`; `Dictionary::build_table_view`'s legacy bare-store
  loop reads `group_fields`. Every run reader checks `start + count > size()` in the operand type: today the 16-bit
  operands promote to `int`; after D1 they are unsigned 32-bit and can wrap (D7).
- The name pool's offsets and lengths, and every `MsgFieldsRun` / `EnumRun`, are already `std::uint32_t`, narrowed from
  `size_t` with `static_cast<std::uint32_t>` (D2). So are 100's expansion-derived member pools (the delimiter and
  section member pools and the capture's member pool, written through `push_run` and `close_member_level`). Re-derive:
  `grep -n 'static_cast<std::uint32_t>' src/dictionary/{xml,orchestra}_loader.cpp src/dictionary/dictionary_internal.hpp`.
- `emit_fields.cpp`'s `emit_field_ref` writes one positional initializer per `FieldRef` member and ends each row with a
  literal `, 0 },` for `_reserved`. `accessor_resolution_census_test.cpp`'s Fields parser requires nine numbers per
  row; `validator_shape_test.cpp` reads `_reserved` of a generated v44 row.
- 098's overlay check: `kSideTableMax`, `side_table_fits`, and the refusal arms in `overlay_merge.cpp` (one per format)
  refuse only when the base fits and the merged result does not (the fits are carried in each walk result and in
  `guard_outcome`, and `def_info::entries` exists only to feed them); FIX50SP2's base already does not fit, so the check can
  never fire there. Overlay caps bound new components and groups (`kMaxNewComponents`, `kMaxNewGroups`).
- Public: the headers are installed; the C ABI is opaque (`fixpp_dict_t`; `src/capi` reads `FieldRef` members by
  name, `rule` and `type`, neither of them widened, re-derive with `grep -n 'fr\.' src/capi/*.cpp`); SWIG does not
  expose the records. `table_view`'s own stores hold tags and parent paths of tags, not indexes into the records.
- Tests touching the shapes or the wrap: `ref_shape_test.cpp`; `dictionary_equivalence.hpp`'s `census_excluded` wrap
  exclusions, its side-against-side `check` lambdas (16-bit parameters, so a 32-bit member would compare modulo 65,536,
  which is the wrap signature itself), and its `_reserved` comparisons and registry entries; the wrap probe in
  `overlay_equivalence_test.cpp`; the side-table refusal rows in `overlay_refusal_test.cpp`; readers in
  `lookup_test.cpp` (a `static_cast<std::uint16_t>(… + 1U)` comparison), `xml_loader_test.cpp`, `round_trip_test.cpp`;
  `orchestra_loader_test.cpp`'s `OrchestraFailClosed.StructuralIdOutsideTheUint16RangeThrows`, which pins the 16-bit
  surrogate parse (D1b).

### Codegen (#621)

- `kind_of` (`tools/codegen/fixpp-codegen/gen_util.hpp`) maps `Char` and `MultiCharValue` to `TypeKind::Char`;
  `MultiStringValue` to `TypeKind::String`.
- Consumers of the kind: `kind_cpp_type` / `emit_scalar` (`emit_messages.cpp`, view accessors at top level and in group
  entries); the owning accessors in `emit_reify.cpp`; `args_cpp_type` (`emit_builders.cpp`, `Args` members);
  `builder_call_kind` / `entry_set_name` (`gen_util.hpp`, `set_char` vs `set_string` in group entries). The top-level
  `bb.field(tag, …)` text is the same for both kinds (overload by type). Validators, manifest and dispatch emitters do
  not read the kind; runtime multi-value enum checking keys on `field_data_type`, not the kind. Re-derive the consumer
  files: `grep -n 'kind_of(' tools/codegen/fixpp-codegen/*.cpp`.
- MultiCharValue fields: none in v42, v44, FIXT.1.1; in FIX50SP2 and in FIX Latest the tag set is
  {18, 286, 291, 292, 529, 546, 1308, 1384} (re-derive: the fields typed `MULTIPLECHARVALUE` in each file). FIX50/SP1
  are not codegen targets.
- QuickFIX types MultipleCharValue as a string: QuickFIX C++ `FieldTypes.h` (`typedef std::string MULTIPLECHARVALUE`)
  and `Field.h` (`typedef StringField MultipleCharValueField`); QuickFIX/J `FieldType.MULTIPLECHARVALUE`, whose
  no-argument constructor sets the Java type to `String.class`. Heads in the spec's Normative References.
- Checked-in oracles that move: `specs/003-dictionary-codegen/contracts/golden/v50sp2_Messages.golden.hpp`;
  `specs/076-fix-latest-typed-codegen/contracts/golden/vlatest_Messages.golden.hpp`; the v50sp2 and vlatest trees under
  `specs/078-precompiled-builder-libs/contracts/golden/` (group `Args`, message `Args`, builder `.cpp`/`.inl` of the
  messages carrying these fields; the 078 trees hold no `FieldRef` text, re-derive with
  `git grep -l FieldRef specs/078-precompiled-builder-libs/contracts/golden`); `read_tier_byte_diff_test.cmake`'s
  v50sp2 Messages and Reify hashes (the Messages hash equals the 003 golden's); the census table `mapped_kind` in
  `tests/codegen/accessor_resolution_census_test.cpp`. vlatest `Reify.hpp` and vlatest `Fields.hpp` have no content
  pin on either side: `VlatestByteIdenticalAcrossRuns` proves run-to-run determinism, not equality to the base.
- Test sources that assign a `char` to these `Args` members: `tests/session/test_077_allversions_builder_roundtrip.cpp`
  and its vlatest twin.

## Decisions

### D1 — Widen every 16-bit holder whose value its source does not bound (FR-001, FR-001a)

Rule (B86 owner ruling, 2026-11-02, on `research/reviews/ruling_B86_base_loader_guards.md` §"Entry caps"): where a
count wraps a 16-bit index, widen it; do not cap it. 102 adds no refusal.

- `ComponentRef`: all five values become `std::uint32_t`, in their declared order (`component_id`, `name_offset`,
  `first_field_index`, `field_count`, `parent_component_id`); `_reserved` is removed. `sizeof == 20`, `alignof == 4`.
  Every member has the same type and the order is kept, so a five-value positional initializer keeps its meaning; one
  that still names `_reserved` (six values) fails to compile.
- `GroupRef`: `first_field_index` and `field_count` become `std::uint32_t`; `no_tag`, `first_field_tag`,
  `parent_group_no_tag` and `_reserved` stay 16-bit (tags, and a pad). The declared order is kept: at 2+2+4+4+2+2 the
  record is 16 bytes at `alignof == 4`, every member naturally aligned, so a six-value positional initializer keeps its
  meaning.
- `FieldRef`: `component_index` becomes `std::uint32_t` **in place**, at offset 8, which is 4-aligned; `_reserved` is
  removed. Offsets: `tag` 0, `type` 2, `rule` 3, `condition_index` 4, `group_no_tag` 6, `component_index` 8,
  `enum_table_index` 12, `length_pair_data_tag` 14. `sizeof == 16` (unchanged), `alignof == 4` (was 2). An
  eight-value positional initializer keeps its meaning; a nine-value one (with `_reserved`) fails to compile. B86's
  note relies on `sizeof(FieldRef) == 16`; that still holds.
- Loaders, both formats: every component-identity holder named in the Survey is 32-bit, or counts in `std::size_t` and
  stores 32-bit: `next_id`, the `component_index_by_name_` / `component_index_by_xml_id_` values, the `parent_of_` keys
  and values, the `index + 1` casts, `enclosing_component_index`, `PendingComponent::index`, `parent_comp_id`, and
  `component_fields_impl`'s parameter. `finalize` writes starts, counts, name offsets and ids with no clamp and no
  16-bit cast.
- Groups: identity unchanged (bounded by the tag domain, Survey); starts and counts widen as above.
- `FieldRef::condition_index` and `enum_table_index` stay 16-bit: never written.
- **Enumerate the class; do not trust this list.** Every hit of
  `git grep -nE 'uint16_t[^;,)]*(component|parent_comp_id|parent_of_|next_id)|static_cast<std::uint16_t>\((i|i \+ 1|[a-z_]+->second \+ 1|components_\.size\(\))\)' -- src/dictionary include/fixpp/dict tools/codegen tests bench`
  (the class stops at `,` and `)`, so a parameter list with a 16-bit tag before a widened index does not match) is
  classified as below, and every clamp hit of `git grep -nE 'min<std::(size_t|uint32_t)>\([^)]*65535' -- src/dictionary`
  is removed. Every hit of the first recipe is classified **identity** (widened) or **tag** (a FIX tag or a list of
  tags, kept 16-bit, for example `dictionary_equivalence.hpp`'s `raw_census::components` member-tag vectors) at tasks
  and again after the last fix commit; no identity hit, in `src`, `include`, `tools` or in a test oracle, remains on
  the head, and no 16-bit cast is applied to a component identity anywhere. The clamp recipe prints nothing on the
  head. `fa778a8e` is the known positive of both (the first hits `lookup_test.cpp`'s parent-id comparison among the
  loader holders). The surrogate-id keys are D1b's recipe, not this one.
- Rationale: QuickFIX has no component count limit, so a 65,536-component file that QuickFIX loads loads here too.
  Widening costs the record sizes above and one generated-text token per `FieldRef` row (D5).
- Alternatives:
  - cap the component count at 65,535 and refuse (round-0 D1): rejected by the B86 owner ruling ("no … entry caps";
    "widen it, which is B84's job, not a cap");
  - keep `FieldRef::_reserved` and grow `FieldRef` to 20 bytes (18 rounded up to `alignof == 4`): no generated text
    changes, but every `FieldRef` table, constexpr and runtime, grows by a quarter; rejected for the 16-byte layout,
    which the orchestrator directed and B86's step-cost model assumes;
  - keep `ComponentRef::_reserved` as a `std::uint32_t` (24 bytes): it keeps 2c §4.2's
    `FIXPP_DICT_COMPONENTREF_RESERVED_USED` forward-compatibility slot, which no code reads (re-derive:
    `git grep -n RESERVED_USED -- ':!specs' ':!.specify'` finds only header comments). Rejected: an unused slot, at
    4 bytes per component. Its loss is recorded (D8);
  - refuse a side table past 65535: refuses the vendored FIX50SP2.

### D1b — Orchestra surrogate ids are parsed in the schema's domain (FR-001b)

- Split the parse by kind. Ids that are FIX tags (`<fixr:field id>`, `fieldRef`, `lengthId`, `numInGroup`) keep the
  16-bit tag parse: it is the library's tag type, and tag width is out of scope. Ids that are surrogate keys
  (`<fixr:component>`, `<fixr:group>`, `componentRef`, `groupRef`) are parsed to 32 bits and admitted up to the
  schema's `maxInclusive` (2147483647); above it, or malformed, they are `orchestra_parse_error` as today. That bound is
  the format's own rule, so the refusal above it is not a cap. 0 stays admitted (Survey).
- **A distinct surrogate-id type** (Gate A round 2): every surrogate key and holder is an
  `enum class` over `std::uint32_t` (for example `orch_ref_id`), not a bare integer, and the surrogate parse returns
  it. Two definitions, both internal: one in `src/dictionary/dictionary_internal.hpp` (`detail`), used by the
  Orchestra loader and `overlay_merge.cpp`; one in `ir.cpp`'s anonymous namespace, because the code generator includes
  only installed `fixpp/dict` headers and its Orchestra walkers share no code with the loader by design (the banner
  above `OrchestraComponentIndex`). No installed header names a surrogate id (re-derive:
  `git grep -n -E 'xml_id|by_id|surrogate' -- include` prints nothing, while the same pattern over `src` hits), so no
  installed header changes.
  - What it closes: a scoped enum does not convert implicitly to an integer, so a surrogate container or holder left
    `std::uint16_t` (`walked_groups.insert(id)`, a `find` on a 16-bit-keyed map) fails to compile instead of
    narrowing silently. A container-width defect fed directly from the parse becomes a compile error. One fed
    through `std::to_underlying` (whose `std::uint32_t` result narrows into a 16-bit key or argument with no
    diagnostic, there being no `-Wconversion`) or through an explicit cast does not; the cast-through mutants and the
    alias-pair witnesses catch it (D4), and the `to_underlying` recipe below confines the underlying value to text.
    It also forces
    the kind split at the reads that serve both kinds through one variable (`collect_header_tags`' `id_of`,
    `records_of`'s `has_id`), because a tag set and a surrogate map no longer accept the same type.
  - What it does not close: a surrogate parse that keeps the 16-bit bound while returning the wide type, and the
    overlay keys (`orch_id_key_of`, `orch_child_key`), which turn the id into a string. Those stay parse-width
    mutants (D4).
  - What it costs: the holders the recipes below classify as surrogate change type in either design; the enum adds
    its two definitions and a `std::to_underlying` wherever a surrogate id becomes text (re-derive:
    `git grep -n -E 'to_string\((xml_)?id\)' -- src/dictionary/orchestra_loader.cpp src/dictionary/overlay_merge.cpp tools/codegen/fixpp-codegen/ir.cpp`).
    `std::hash` and the built-in comparisons are defined for scoped enums, so the `unordered_map` and `map` keys
    need nothing else.
- Every site that parses a surrogate id moves together. Most of them skip silently on a parse miss (`continue`,
  `std::nullopt`, no insert), so a site left 16-bit becomes a silent divergence, not an error:
  - the loader: `collect_components`, `collect_groups`, `expand_field_list`'s componentRef and groupRef arms, the
    parent-map pass's tolerant read, the `component_index_by_xml_id_` and `group_by_xml_id_` keys, and
    `collect_header_tags`, whose `id_of` reads the fieldRef, componentRef and groupRef ids and each group's
    `numInGroup` id through one width, and whose `walked_groups` set holds group ids; both are split by kind;
    `collect_section_groups` (added by 100), whose `id_of` and `id` local serve the fieldRef (a tag) and the
    componentRef and groupRef (surrogates) through one 16-bit variable, whose `.find`s into
    `component_index_by_xml_id_` and `group_by_xml_id_` and whose section ids 1024 and 1025 are surrogate keys, and
    whose `no_tag` is a tag; its `id_of` is split by kind as `collect_header_tags`' is. A parse miss there is a silent
    `continue`, and its output, the section-group records, is read by the wire path
    (`table_view::wire_group_resolve`, `wire_group_member_tags`);
  - `overlay_merge.cpp`: `orch_id_key_of` (one helper for the field, component and group definition arms; the field
    arm stays tag-parsed), `orch_child_key` (fieldRef, componentRef and groupRef; the fieldRef arm stays tag-parsed),
    and the Orchestra graph build's `component_by_id`, `group_by_id` and `records_of`'s `has_id` (one read for all
    three ref kinds; a `fieldRef`'s and a `numInGroup`'s id stay tag-parsed, and so does `declared`);
  - the code generator's `ir.cpp`: `try_parse_orchestra_uint16` at the component and group sites of
    `build_orchestra_component_index`, `build_orchestra_group_index`, `collect_orchestra_tags`,
    `collect_orchestra_group_tags` and `walk_orchestra_level`, and the `OrchestraComponentIndex` and
    `OrchestraGroupIndex` `by_id` keys. A miss there is "defensive only, continue": the index builders then drop the
    component from `by_name` too, and the walkers drop its members from the IR;
  - the equivalence fixture's Orchestra raw reader. The independent oracles that already key by `std::uint32_t`
    (`required_scope_oracle.hpp`) or `unsigned` (`header_tags_test.cpp`'s `orchestra_header_walk`) need no change.
- Enumerate, in two recipes, each hit classified tag or surrogate at tasks and again after the last fix commit:
  - parse sites: `git grep -nE 'try_parse_uint16|parse_orchestra_id|try_parse_orchestra_uint16|by_xml_id_|by_id\b|attribute\("id"\)\.as_u?int' -- src/dictionary tools/codegen tests/dictionary tests/codegen`;
  - holders, which the parse recipe cannot see (the GCC and Clang warning set in `cmake/Helpers.cmake` has no
    `-Wconversion`, so a 32-bit id stored into a 16-bit holder narrows silently):
    `git grep -nE '(unordered_)?(set|map)<std::uint16_t|std::uint16_t (id|xml_id|child_xml_id|no_tag)\b' -- src/dictionary/orchestra_loader.cpp src/dictionary/overlay_merge.cpp tools/codegen/fixpp-codegen/ir.cpp`.
    `fa778a8e` is its known positive: it hits `walked_groups`, `component_by_id`, `group_by_id` and both `by_id`
    maps (surrogate), the tag-keyed holders (`fields_by_tag_`, `declared`, `first_seen_groups`, the header-trailer
    sets: tag) and `parent_of_` (identity, D1).
  A surrogate hit left 16-bit on the head is a defect.
- A third recipe, the `to_underlying` sites, classified at tasks and again after the last fix commit:
  `git grep -n 'to_underlying' -- src/dictionary/orchestra_loader.cpp src/dictionary/overlay_merge.cpp tools/codegen/fixpp-codegen/ir.cpp`.
  Every `to_underlying` of a surrogate id converts it to text (an argument of `std::to_string`, or the id written into
  a message or key string); one that does not is a defect. A `to_underlying` of another enum is classified "not a
  surrogate id". `fa778a8e` has no hit, so the recipe is shown able to report by a planted
  `walked_groups.insert(std::to_underlying(id))` in a copy, then removed.
- Text the split makes false, restated per kind at tasks: the banner of `detail::try_parse_uint16` ("An Orchestra
  `id`, 0..65535"), the note above `parse_orchestra_id` and its banner, and in `tests/dictionary/orchestra_loader_test.cpp`
  the comment of `OrchestraFailClosed.ZeroStructuralXmlIdsAreStillAccepted` and the comment above the `lengthId`
  zero-tag cases, which describe a parse shared by tags and surrogate ids.
- An existing row that pins the 16-bit surrogate parse goes RED on the head and is re-pointed, not deleted:
  `OrchestraFailClosed.StructuralIdOutsideTheUint16RangeThrows` refuses a component id of `65536`; it becomes the
  `id_t` range row (`-1` and `2147483648` refused, renamed), and `65536` joins the loads (D4's id-domain pair).
  Re-derive the class: `git grep -n -E 'Uint16Range|"65536"' -- tests/dictionary tests/codegen`.
- Alternatives:
  - keep the 16-bit surrogate parse as a "format rule". Rejected: the schema says otherwise, and with the index
    widened it would be the one place a 65,537th Orchestra component is refused, which is the cap B86 rejected;
  - bare `std::uint32_t` for the surrogate ids, with the alias-pair witnesses (D4) as the only guard on container
    width. Rejected in round 2 for the distinct type above: it stays internal, it touches the holders that change in
    either design plus the text sites, and it turns the container-width class from a silent wrong answer, caught only
    by a fixture that happens to declare an alias pair, into a compile error. The alias-pair witnesses stay either way,
    as the test of the domain.

### D2 — Retire 098's side-table fit check (FR-003)

- Premise, as a condition: a 32-bit start or count wraps only when a side table reaches 2^32 entries, which at 16 bytes
  per `FieldRef` is 64 GiB of `FieldRef` in one vector. A small input gets there only through an unbounded base
  expansion (a doubling component chain, L-098-3). B86's ruled absolute budget bounds it by construction: in the walk
  (`visit_definition` in `overlay_merge.cpp`), each child adds one step and at most one side-table entry, so every
  side-table size, and every expansion-derived `MsgFieldsRun` end, is at most the walk's total, which B86's budget holds
  far below 2^32. Until B86 merges, that window is disclosed (L-102-1 (a)), not guarded.
- On the overlay path today the step counter is armed at no more than the base's total plus `kExpansionBudget`, so a
  32-bit fit check could fire only on a base already within that budget of 2^32 entries. It would be error handling for
  an input no host can load, with no witness short of a test seam; it is not kept.
- `kSideTableMax`, `side_table_fits`, the two refusal arms and their `overlay_limit_error` texts are removed, and so is
  what only they read: `guard_outcome`'s `base_components_fit`, `base_groups_fit`, `merged_components_fit` and
  `merged_groups_fit`, each walk result's `components_fit` and `groups_fit` and the `comp_counts` / `group_counts`
  vectors that feed them (both formats), `def_info::entries` and its accumulation in `visit_definition` (it feeds only
  those vectors), and `refusal_origin::side_table` with its case in `overlay_test_support.hpp`'s `origin_name`. Where
  the fit check stood in `overlay_merge.cpp`, a header comment names 102 D2 as superseding 098 R-5 step 6's
  side-table fit (D8).
- Every surface that depends on the retired check is enumerated by recipe, not by this list, and each hit is
  classified at tasks and again after the last fix commit:
  - `git grep -n -E '(base|merged)_(components|groups)_fit|components_fit|groups_fit|expect_base_fits|refusal_origin::side_table|kSideTableMax|side_table_fits' -- src tests include`;
  - `git grep -n -i 'side.\?table' -- tests ':!tests/fuzz/corpus'` (comments, row names, messages);
  - `git ls-files -- tests/fuzz/corpus | grep -i 'side.\?table'` (seed file names, which a content grep cannot see).
  `fa778a8e` is the known positive of each.
- Per surface:
  - **the side-table refusal rows** (`OverlayRefusalW3XmlSideTable` and `OverlayRefusalW3OrchSideTable`: the start
    push, the new component's count and the low-tagged group): re-pointed, not deleted. Each keeps its base, whose
    side tables fit, and its overlay, which pushes a start or a count past 65535; it now asserts that the overlay loads
    at the prediction (`expect_loaded_at_prediction`) and that the definition whose start or count passed 65535
    returns its own members, and is renamed for that. On `fa778a8e` each is RED (refused, side-table origin). They are
    the witness of the retirement itself: the side-table arm restored alone turns them RED (D4). Their completeness
    entry ("side-table fit") is renamed with them;
  - **their just-inside controls** (the `…JustInsideTheTable…` rows of both formats): deleted, with their names in
    the completeness entry. They bound a limit that no longer exists, and the re-pointed rows load past it;
  - **the guard-order rows** `OverlayRefusalW3XmlOrder.BudgetIsReportedBeforeTheSideTableFit` and its Orchestra
    twin, with their completeness entry ("guard order: budget before the side-table fit"): deleted. Each probes the
    input its format's `DoublingBombRefusedByTheBudget` row probes (read the two bodies), and those rows already pin
    the budget origin;
  - **`expect_base_fits`**, the precondition of the doubling and empty-chain rows in both formats: removed. It kept
    step 6's fit live behind the budget for 098's M-12; no fit check remains to precede or follow the budget;
  - **the fuzz seeds** `seed_x_w3_BudgetIsReportedBeforeTheSideTableFit` and `seed_o_w3_…`: deleted when `cmp` shows
    each byte-identical to its format's `…DoublingBombRefusedByTheBudget` seed (it does at `fa778a8e`), because a
    rename would keep a duplicate corpus input; a seed that differs is renamed with no side-table claim and kept.
    `tests/fuzz/CMakeLists.txt` consumes the corpus by directory, so no file names them;
  - **comments and messages** (the `fuzz_dict_overlay.cpp` invariant list and its `accept_*_refusal` comments, the
    `overlay_refusal_test.cpp` section banners, the wrap probe's "side table fits" line in
    `overlay_equivalence_test.cpp`): restated without the check, each named in the tasks from the second recipe.
- Nothing replaces the check: after D1 no 16-bit value is left for an overlay to push.
- **Text for the orchestrator to add to `phases/phase-4/issue-batches.md` §B86** (102 does not edit that file):
  > - **From 102 (B84), Gate A rounds 1 and 2, 2026-11-07.** 102 widens the component and group side-table starts and
  >   counts, the component name offset and the whole component identity to 32 bits, and retires 098's side-table fit
  >   check. The ruling note's refusal order "cycle, budget, depth, side-table fit" therefore becomes **cycle, budget,
  >   depth**. B86 owns the 32-bit obligation in its place: its absolute expansion budget must imply that every
  >   expansion-derived run end (each side table, each per-message `MsgFieldsRun`, and 100's delimiter and section
>   member pools) is at most `UINT32_MAX`. It does
  >   by construction (a walk step emits at most one entry, and the budget is far below 2^32); B86's shipped text states
  >   that derivation, and B86 amends L-102-1 (a) when it lands. The budget does not bound the component count (an
  >   empty definition costs no step); the component-identity boundary is L-102-1 (c), not B86's obligation. The
  >   name-pool leg, L-102-1 (b) (4 GiB or more of names), outlives B86, per the ruling's "no byte caps".
- The name pool: a pool past 2^32 bytes needs 4 GiB or more of host-chosen names; B86 rejected byte caps, so this
  residual outlives B86. Its effect is a wrong name, not an out-of-bounds read: a wrapped offset is smaller than the
  true one and a truncated length is shorter, so `dict_metadata_handle::name_at` stays inside the pool. Disclosed as
  L-102-1 (b); no guard.
- The component identity: one-based values wrap at 2^32 components (FR-001a). B86's unit is one step per child a walk
  visits (`visit_definition` adds a step per child record, and a component's total is its children's), so a component
  with no members costs 0 steps and the budget does not bound the component count. Counting one step per definition
  would be an entry cap, which the ruling rejects ("No byte, element-depth or entry caps"). A QuickFIX XML base needs
  2^32 distinct non-empty component names (`collect_components` refuses an empty or repeated name), each interned
  into the name pool without deduplication (`intern_name_in_pool`), so its pool is past 2^32 bytes, L-102-1 (b),
  before its component count reaches 2^32; an Orchestra base holds at most 2^31 components (distinct ids in
  0..2147483647, `collect_components` refuses a repeated id). Disclosed as L-102-1 (c); no guard.

### D3 — MultiCharValue is a string kind (FR-004, FR-005)

`kind_of` maps `MultiCharValue` to `TypeKind::String`. Every consumer then emits what it emits for MultiStringValue:
`expected_t<std::string_view>` accessors (lifetimebound), `std::optional<std::string_view>` `Args` members,
`set_string` in group entries. No other field's text changes. `field_traits<char>` is unchanged (it stays the decoder of
`Char` fields). Enum value-domain checking stays out of scope (L-069-1).

### D4 — Witnesses

- #654, side tables: the equivalence fixture's wrap exclusions are removed, so it compares every component's and
  group's members with an independent expansion of the source file, for every vendored base, with and without an
  overlay. RED on `fa778a8e` from `RgstDistInstGrp`.
- #654, fixture repair (the instrument must be able to report non-zero): both `check` lambdas in `compare()` (the
  component block and the group block) take 32-bit (or `std::uint64_t`) operands, so a difference by a multiple of
  65,536 is reported and printed untruncated; the `_reserved` comparisons and registry entries of the removed members go;
  the Orchestra raw reader keys ids 32-bit (D1b).
- #654, name offset: a named oracle with internal access (`tests/support/dictionary_test_access.hpp`): for every
  component of every vendored base, the name-pool slice at `name_offset` of the name's length equals the component's
  name. RED on `fa778a8e` for FIX50SP1, FIX50SP2 and FIX Latest (the clamp), GREEN on the head.
- #654, component identity (positive loads, not refusals):
  - QuickFIX XML: a generated base of more than 65,536 components, including a nested child whose parent's index is at
    least 65,535. Assert `component_id`, `parent_component_id`, `component_fields` of the components past index 65,535,
    and a member's `FieldRef::component_index` via `message_fields`. RED on `fa778a8e` (the wrapped name-map index and
    one-based ids).
  - Orchestra: a generated base of more than 65,536 components (its ids necessarily pass 65535), the same assertions;
    and the id-domain pair: a component id of 2147483647 loads, 2147483648 is `orchestra_parse_error`. RED on
    `fa778a8e` (the first id past 65535 is a parse error).
  - Overlay crossing: `kMaxNewComponents` admits up to its value of new components per overlay, so a base just under
    65,536 components plus an at-cap overlay crosses 65,536. One row per format asserts the merged dictionary's new
    components' ids and members. The RED observation on `fa778a8e` is whichever 098 ending fires (the side-table
    refusal or the wrap), recorded as observed.
  - Orchestra overlay over a surrogate id past 65535: an overlay that adds a field to a base component whose id passes
    65535 merges it into that component (RED with a 16-bit `orch_id_key_of` component arm).
  - Codegen: an IR build (`build_ir`) over a small Orchestra fixture whose component and group ids pass 65535 gives the
    message the component's members and the group's order (RED with a 16-bit `walk_orchestra_level` or index-builder
    parse).
- #654, surrogate ids: **the alias-pair rule.** Every fixture of the Orchestra identity cell and of rows (a)–(f) below
  declares, for each surrogate-keyed container its row reaches, a high id H > 65535 **and** its alias H − 65536, both
  referenced and with distinct members, and asserts that each member of the pair resolves to its own definition. A key
  narrowed to 16 bits keeps one of the two and loses or swaps the other, so the assertion fails whichever it keeps; one
  high id alone cannot show it. The containers: in the loader, `component_index_by_xml_id_`, `group_by_xml_id_` and
  `collect_header_tags`' `walked_groups`; in the overlay graph build, `component_by_id` and `group_by_id`; in `ir.cpp`,
  the two `by_id` maps. The holder recipe (D1b) re-derives the list.
- #654, surrogate ids, one row per independent output. Rows (a)–(f) are each RED on `fa778a8e`, where the first id
  past 65535 is an `orchestra_parse_error`; row (g) is a characterization pin, GREEN on `fa778a8e` and on the head,
  RED only under its mutant:
  - **(a) Loader, parent map.** In the Orchestra identity cell, a nested child reached through a `componentRef` whose
    id passes 65535, and its alias child under another parent: each child's `parent_component_id` equals its own
    parent's `component_id` + 1.
  - **(b) Loader, header store.** An Orchestra fixture whose header component (`fixr:component id="1024"`, named
    `StandardHeader`) references a high-id component holding a fieldRef T and a high-id group with NumInGroup N and a
    member M, and both aliases, each with its own tags (T', N', M'), all reachable from the header. `walked_groups`
    is a set, so a narrowed key skips the second group of the pair; each group must contribute its own tags. Assert
    `is_header_tag` of T, N, M, T', N' and M' on the table view, and set equality with `header_tags_test.cpp`'s
    `orchestra_header_walk` oracle, which keys by `unsigned`.
  - **(b2) Loader, section records** (100's `collect_section_groups`). Over row (b)'s fixture, the section-group
    records are asserted directly, through `table_view_test_access::section_records` and the lookup
    `make_ctx_query({}, path, no_tag)`: N's record at path `{}` holds M, and N''s holds M'. Not through
    `wire_group_member_tags` or `group_registered`: those fall back to the bare store or arm 3, so a skipped record
    still reads as correct unless the section definition differs from the bare store.
  - **(c) Codegen, header classification.** `populate_orchestra_projection`, through `build_ir`, over row (b)'s
    fixture: `ir.header_trailer_tags` ⊇ {T, N, M, T', N', M'} and `ir.group_tags` ∋ N, N'. The fixture's header
    component carries both the id 1024 and the name `StandardHeader`: the loader finds the header by id and
    `populate_orchestra_projection` by name. No message structure references the header component, so N and N' are
    reachable only from `StandardHeader`: `walk_orchestra_level` walks a message's full structure, header included,
    and `group_tags` is the union of every message's `group_order` with the header-trailer groups, so a message that
    reached the header would put N into `group_tags` without `collect_orchestra_group_tags`, and that helper's mutant
    could not go RED. Re-check this by reading `walk_orchestra_level` and `populate_orchestra_projection` at tasks.
    The row sits beside the existing IR row.
  - **(d) Overlay, definition key, group arm.** Besides the component row above, an Orchestra overlay that adds a
    field to a base `<fixr:group id=H>` whose alias group also exists: it loads, `group_fields` of H's NumInGroup tag
    contains the field, and the alias group's members are unchanged.
  - **(e) Overlay, child key.** An Orchestra overlay that restates, in place, a base message's high-id `componentRef`
    and `groupRef`: once identically, once with `presence="required"` (098 promotes any matched element whose base
    presence is not Required, and refuses only a relaxation). It loads, the promotion applies, and each member occurs
    once at its level (the ref was matched, not appended).
  - **(f) Overlay, graph build.** Rows (d) and (e) reference the high-id component and group from a message, and
    assert `expect_loaded_at_prediction` (counter equals prediction, through the seam). One more row closes a cycle
    through a high-id `componentRef` and asserts the pre-walk's "component reference cycle" message naming the
    high-id components, as `OverlayRefusalW3OrchWalk.CycleClosingThroughABaseComponent` does for small ids. With a
    narrowed `has_id` or map, the pre-walk misses the cycle and the build counter or the loader ends the load instead.
  - **(g) The kind split does not leak into tags.** A `<fixr:field id>`, a `fieldRef`, a `numInGroup` and a
    `lengthId` above 65535 are each refused by the tag parse's range check, asserted by that refusal's own message for
    the kind ("missing/invalid <…> id attribute" at `fa778a8e`), not by the exception type alone. Each id is chosen so
    that a 16-bit truncation of it gives a path that loads: 65536 + T for a ref to a declared tag T (a declared
    NumInGroup tag for `numInGroup`, the declared Length field for `lengthId`), and 65536 + U for a `<fixr:field id>`,
    with U an otherwise valid, undeclared tag. A truncation to 0 would instead meet another refusal of the same type
    (`parse_orchestra_field_tag`'s zero message, "not declared", the length pair's declaration check), and the row
    would stay GREEN with the mutant live. Read each tag site's post-parse checks at tasks to confirm the chosen ids
    reach a load. Where an existing `OrchestraFailClosed` row already pins a kind this way, it is named instead of
    duplicated.
- #654, readers (D7): an internal-access cell gives a handle a record whose start is near `UINT32_MAX`, a small count
  and a short table; every run reader returns an empty span.
- #621:
  - a v50sp2 and a vlatest decode of `18=1 2` and `18=1` through `exec_inst()`;
  - a v50sp2 and a vlatest group-entry decode of `LegExecInst(1384)="1 2"`, read back through the generated entry
    accessor;
  - one reified (owning) decode of `18=1 2`;
  - a builder round-trip of `exec_inst = "1 2"` and of a group entry's `LegExecInst(1384)`;
  - a vlatest compile-shape assertion for one view, one group-entry and one owning MultiCharValue accessor (the
    accessor census covers v42, v44, v50sp2 and vt11 against `mapped_kind`; vlatest is outside it);
  - no lifetime cells after a move or after source destruction: the emitted text is the String kind's, shared with
    every MultipleStringValue field, and its lifetime contract is not new to 102.
- Mutation: each change reverted alone turns its own cell RED. The list is per independent helper, not per
  translation unit; each mutant is applied alone, from a copy, and its named row must go RED:
  - the side-table widening → the equivalence fixture at `RgstDistInstGrp`;
  - the name-offset widening → the name-offset oracle;
  - the XML component counter or index → the XML identity cell;
  - Orchestra's index, with the id parse kept wide → the Orchestra identity cell;
  - the side-table arm restored (D2) → the re-pointed side-table rows;
  - one reader's `std::size_t` arithmetic → the reader cell;
  - `kind_of` → the decode cells and the census.
  - **Parse width**: one helper keeps the 16-bit bound while everything else is wide (where a helper is split by kind,
    its component arm and its group arm are separate mutants):
    - the loader's surrogate parse at `collect_components`, `collect_groups` and `expand_field_list`'s ref arms →
      the Orchestra identity cell (loud: `orchestra_parse_error`);
    - the parent-map pass's tolerant read → row (a);
    - `collect_header_tags`' `id_of`, component arm and group arm → row (b);
    - `collect_section_groups`' `id_of`, component arm and group arm → row (b2);
    - `build_orchestra_component_index` and `build_orchestra_group_index` → the IR row and row (c);
    - `collect_orchestra_tags` → row (c)'s `header_trailer_tags` half; `collect_orchestra_group_tags` → row (c)'s
      `group_tags` half;
    - `walk_orchestra_level` → the IR row;
    - `orch_id_key_of`, component arm → the component overlay row; group arm → row (d);
    - `orch_child_key` → row (e);
    - the graph build's definition fills (`component_by_id`, `group_by_id`) and `records_of`'s `has_id` → row (f).
  - **Container width**: the parse stays wide and one surrogate container's key is narrowed to `std::uint16_t`. With
    the distinct surrogate-id type (D1b) this must fail to compile; each container's mutant is attempted and its
    failing build output recorded (an attempt that compiles is a defect in the type's use). Each container's alias
    pair is shown able to fail by a second mutant that narrows through an explicit cast at that container's insert and
    lookup → that container's alias-pair assertion RED.
  - **Kind collapse**: one tag-parse helper's range check is raised to the surrogate bound while it still truncates
    into `std::uint16_t` → the row (g) pins of the kinds it serves RED (each such id truncates to a tag that loads, and
    the pin asserts the range message, row (g)). (Swapping in the surrogate parser instead would
    fail to compile against the tag holders, which proves nothing about row (g).)

### D5 — Republishing and the cross-revision differential (SC-003)

- Regenerate the 003 v50sp2 and 076 vlatest goldens and the 078 v50sp2 and vlatest trees with the recipes in
  `tests/codegen/determinism_test.cpp`'s failure messages; move `mapped_kind`'s MultiCharValue case to the string kind;
  change the `077` round-trip assignments to string literals.
- `FieldRef` loses `_reserved`, so `emit_field_ref` stops writing the trailing `, 0` initializer. Every `FieldRef` row
  of every tier's `Fields.hpp` changes by that one token; the value digits do not change on any vendored base, because
  no vendored component index passes 16 bits. This moves all four read-tier `Fields.hpp` hashes (v42, v44, v50sp2,
  vt11) and the unpinned vlatest `Fields.hpp`. The census parser's row check moves from nine numbers to eight. The
  `validator_shape_test.cpp` `_reserved` assertion is re-pointed (for example at `component_index` being 0 outside a
  component) or removed with a named reason.
- The read-tier hashes that move: the four `Fields.hpp`, plus v50sp2 `Messages.hpp` and `Reify.hpp`. Each moved pin
  carries a 102 banner in `read_tier_byte_diff_test.cmake` stating the re-derivation below, in the 091/099 form.
- **Differential, the SC-003 instrument.** Generate every tier (v42, v44, v50sp2, vt11, vlatest; read tier, builder tier
  and `_dispatch/`) with the base codegen and with the head codegen, and diff the trees recursively. The base is
  the parent base: `99e956a0` while 102 sits on it, and `git merge-base origin/main HEAD` after the post-squash rebase
  (D9). The head is accepted only if:
  - every `Fields.hpp` equals the base's after the row-trailer transform (each row's `, 0 },` becomes ` },`), and
    nothing else changes in it;
  - every other hunk is an accessor, an `Args` member or an entry setter of a tag in
    {18, 286, 291, 292, 529, 546, 1308, 1384};
  - every other file is byte-identical.
  The filter is shown able to report a stray hunk: a planted extra change in one non-MultiCharValue file is reported,
  then removed; and a planted non-MultiCharValue hunk inside a file that carries MultiCharValue hunks (the
  classifier's weak point) is reported, then removed. The verify record holds the commands, the hunk classification
  and the planted positives.

### D6 — Performance (SC-004)

- The records grow (`ComponentRef` from 12 to 20 bytes, `GroupRef` from 12 to 16; `FieldRef` stays 16); loader work is
  unchanged.
- Timing: `bench/dictionary/xml_loader_bench` (a `paired` row in `bench/ci-suite.txt`), run locally paired against the
  parent base at measurement time (`99e956a0` until the post-squash rebase, the merge-base after it; D9) under Article VIII §2: release build of both trees, A-B-A-B, min-per-tree, the +5% budget; above
  +5%, the measurement, a rationale and independent approval in the PR, per §2. A crashed or empty run is a failure.
  The bench's fixed `monotonic_buffer_resource` already falls back to its upstream on FIX50SP2 (the bench's own
  comment); larger records move more of the load onto that fallback, in both trees' terms, and the paired run measures
  it.
- Footprint: `bench/dictionary/table_view_footprint_bench` is tier `no` (`none:records-footprint-in-bytes-not-timing`),
  so it is not a timing instrument; its before and after bytes are recorded in the verify record.
- The accessor change moves no hot path (a `string_view` decode is cheaper than the `char` decode's length check plus
  copy).

### D7 — Run readers do their bounds arithmetic in `std::size_t` (FR-008)

After D1, `first_field_index + field_count` is an unsigned 32-bit sum that can wrap and pass a `> size()` check. Every
run reader tests `start <= size && count <= size - start` (or sums in `std::size_t`). Enumerate the class with
`git grep -nE '(start|first_field_index) \+ [a-z_.>-]*count' -- src/dictionary` (empty on the head; `fa778a8e` is the
known positive), and classify every span built from a 32-bit offset, found by `git grep -nE '\.data\(\) \+ ' --
src/dictionary`, as guarded or bounded by a guarded reader. `name_at` is the name-pool reader; it is L-102-1 (b), not
changed. 100 moved the delimiter-run guard into `msg_group_ctx_delim_run`, which bounds both
`msg_group_ctx_delims_impl` (a span) and `for_each_group_ctx_record`, which indexes the pool element by element in
`std::size_t`, so a wrapped guard there is an out-of-bounds read, not a wrong span. 100's `run_at`
(`dictionary_internal.hpp`) sums in `std::size_t` (guarded) and is the form to reuse; `for_each_section_group_record`
reads through it.

### D8 — Records and amendments (FR-007)

- B&L:
  - L-098-4 and L-621-1 closed (moved to the closed file). Each names, by `Suite.Test`, the cells that close it and
    the head on which they are GREEN, the head the verify record accepts. L-098-4: the FIX50SP2 rows of the repaired
    equivalence fixture (US1.1) and the name-offset cell (US1.2). L-621-1: the `18=1 2` decode cells of US2.1 on
    v50sp2 and vlatest, and the builder round-trip of US2.3;
  - B-098-1 amended in place: the side-table fit refusal is gone, with its "(L-098-4)" pointer and its entry in the
    `overlay_limit_error` cause list; the walk refuses a component cycle and an expansion over the budget only
    (spec FR-003);
  - new **B-102-1**: the `ComponentRef`, `GroupRef` and `FieldRef` layouts, with the contract's migration;
  - new **B-102-2**: the MultiCharValue accessor and builder types, with contract §4's migration. That includes its
    lifetime lines: the accessor now returns a view into the message buffer, not a `char` by value, and an `Args`
    member views the caller's characters until the build call;
  - new **B-102-3**: an Orchestra document whose component or group ids pass 65535 now loads; a QuickFIX XML or
    Orchestra base of more than 65,535 components loads with correct identities (below 2^32 components, (c)); on the
    vendored FIX50SP2, `component_fields` of `RgstDistInstGrp` and of every later component returns its own members.
    It names the C-ABI and Python results that change: `fixpp_dict_load_from_xml_with_overlay` returns
    `FIXPP_ERR_OK` where an overlay's only fault was the side-table fit (was `FIXPP_ERR_DICT_LIMIT_EXCEEDED`) or a
    surrogate id past 65535 (was `FIXPP_ERR_DICT_CONFIG`). `fixpp_dict_load_from_xml` returns `FIXPP_ERR_OK` for an
    Orchestra document with such ids (was `FIXPP_ERR_CAPI_CONFIG_INVALID`). Python's two wrappers return where they
    raised. It carries the BREAKING marker and the C-ABI version move if, and only if, the owner rules these a
    breaking change under `[const §X.7]` (`checklists/api-abi.md` CHK001 to CHK003);
  - new **L-102-1**: the 32-bit residual. (a) Until B86, an unbounded base expansion can wrap a 32-bit run end (the
    side tables, the per-message runs, and 100's delimiter and section member pools); on the overlay path an overlay
    adds at most `kExpansionBudget` beyond the base's own expansion (D2), so only a base's own expansion reaches it;
    B86's budget closes it. (b) A name pool past 2^32 bytes gives wrong names, in bounds; it outlives B86. (c) A QuickFIX XML
    base of 2^32 or more components can give wrong component identities, in bounds, with no error: at exactly 2^32,
    the members and the children of the last component carry 0 for its one-based id (`component_index`,
    `parent_component_id`); above 2^32, zero-based `component_id`s wrap too. It needs 2^32 distinct
    names, so (b) is reached first; an Orchestra base cannot reach it (distinct ids, at most 2^31). B86's budget does
    not bound the component count (a component with no members costs no step), and B86 adds no entry cap, so (c)
    outlives B86, as (b) does (D2). No call reports that a dictionary is in region (b) or (c). (b) and (c) each cite
    a tracking issue (tasks T047);
  - L-098-3 gains one sentence, pointing from it to L-102-1 (a): its unbounded base expansion can also wrap a 32-bit
    run end, L-102-1 (a). The row is otherwise unchanged.
- **Live texts, amended** (they describe current behaviour, so they are not history):
  - `.specify/2c-codegen.md`, each with an `Updated (102-dict-width-multichar-type)` note that names what it
    supersedes and leaves the text before it in place as history: §4.1 and §4.2 (the
    layouts, `alignof`, the `_reserved` discipline that no longer applies to `FieldRef` and `ComponentRef`, and the
    lost `FIXPP_DICT_COMPONENTREF_RESERVED_USED` slot); §1's "Field representation types" bullet and §4.1.3's
    `field_traits.hpp` listing comment (both name a MultiCharValue / MultiStringValue split; after D3 both kinds
    decode through `field_traits<std::string_view>`, which the note states as a condition, which kinds share a
    decoder, with no count of kinds);
  - `include/fix/c_api/dict.h`: `fixpp_dict_load_from_xml_with_overlay`'s `FIXPP_ERR_DICT_LIMIT_EXCEEDED` line points
    at `xml_loader.hpp`'s cause list. It needs no text of its own while that pointer resolves. If the owner rules the
    C-ABI result changes breaking (`checklists/api-abi.md` CHK001 to CHK003), both `dict.h` load declarations gain a
    BREAKING note under `[const §X.7]`;
  - the installed `include/fixpp/dict/field_traits.hpp`: its comment on the remaining specialisations names "the
    MultiCharValue / MultiStringValue split", false after D3; amended, naming 102 D3;
  - the three installed record headers' banners and `_reserved` paragraphs;
  - `include/fixpp/dict/error.hpp` (`overlay_limit_error`'s "side table stop fitting 16 bits" cause) and
    `include/fixpp/dict/xml_loader.hpp`'s `load_with_overlay` comment, both of its sentences: the guard order
    ("… the expansion budget and side-table fit, then …", which breaks across a line) and the cause list.
    `orchestra_loader.hpp` refers to `XmlLoader::load_with_overlay`'s list and has no side-table text of its own, so
    it does not change;
  - `src/dictionary/overlay_merge.hpp`'s guard-order banner and its comments on `guard_outcome` and `guard`, and in
    `overlay_merge.cpp` a header comment where `side_table_fits` stood, naming 102 D2 as superseding 098 R-5 step 6's
    side-table fit (CLAUDE.md, a pointer that names the new decision);
  - the comment above `parse_orchestra_field_tag` (D1b's split; the schema's `positiveInteger`), and the helper
    comments D1b lists.
- **Line-anchored citations.** The notes inserted into `.specify/2c-codegen.md` move its lines, and line-anchored
  citations of 2c exist outside it (`field_traits.hpp`'s own comments cite `[2c §4.1.3:…]` with a line range; 003's
  data-model cites one too). The amended `field_traits.hpp` comment replaces its line anchor with a section or symbol
  pointer, and the records phase runs `tools/check_line_citations.py --shift-audit` over 102's range and resolves
  every citation it reports. Re-derive the set: `git grep -n -E '\[2c §[0-9.]+:[0-9]' -- ':!specs/102-*'`.
- **Merged bundles, flagged, not edited.** Merged bundles are history (101's research §Supersessions; 100's plan;
  098's records recipe excludes `':!specs/'`), and no live B&L row cites the clauses below as current (B-098-1 states
  the behaviour itself and is amended above). `brain/components/dictionary.md` gains a 102 section in the form of its
  099 section's "Records this supersedes, flagged here and not edited":
  - ⚠️ **Superseded by 102 D2:** `specs/098-dialect-overlay/contracts/witnesses.md` (M-12's "the side-table fit stays"
    and its side-table-origin RED, M-18, the N-3 side-table rows and the budget-before-fit order row),
    `specs/098-dialect-overlay/tasks.md` (T019's side-table rows and order row, T037, T038's M-18) and
    `specs/098-dialect-overlay/contracts/surface.md` (the `load_with_overlay` comment's side-table fit and cause);
  - ⚠️ **Superseded by 102 D3:** `specs/003-dictionary-codegen/data-model.md` Entity 11's shape, `spec.md`'s AC-FT1 and
    `contracts/field_traits.hpp`, where each names the MultiCharValue / MultiStringValue split;
  - ⚠️ **Superseded by 102 D1:** `specs/002-dictionary-xml-loader/contracts/field_ref.hpp`, `component_ref.hpp`,
    `group_ref.hpp` and `data-model.md` (the 16-bit layouts and `_reserved`); and 102 listed against 2c §4.1/§4.2.
  The 002 extracts get no in-place note.
- **Enumerate; do not trust the list above.** Every hit of
  `git grep -n -i -E 'side.?table (fit|refusal|check)|side.?tables? *$|stop fitting|kSideTableMax|side_table_fits|MultiStringValue split|MultiCharValue( ?[/-] ?|, )MultiStringValue' -- ':!specs/102-*'`
  is classified as **amended** (live text), **flagged** (a merged bundle, through the brain section), **kept** (still
  true: the `field_data_type` enum listings, `table_view`'s `multi_value` comment) or **history** (draft and history
  files), at tasks and again after the last fix commit. `fa778a8e` is its known positive: it hits the clauses
  above, `error.hpp`, both `xml_loader.hpp` sentences and the `overlay_merge.hpp` comments, but not 002's
  `group_ref.hpp` and `data-model.md` (flagged by D1 through T050) or the texts of tasks `## Census manifest`'s "D8
  supplement"; that supplement is part of this recipe. The
  `side.?tables? *$` alternative is not in the round-2 judgement's recipe; it was added because the guard-order
  sentence in `xml_loader.hpp` breaks the line between "side-table" and "fit", which no single-line alternative of
  that recipe matches. The `field_traits.hpp` comment is caught by the "MultiStringValue split" alternative.
- Catalogue / coverage index: the D-007, D-010, D-011 and W-009 rows (spec Normative References) gain 102's
  delivering cells in their witness column; none changes status. The coverage-index entries `[FIX50SP2 §3.3]` (the
  MultiCharValue decode and builder cells), `[FIX50SP2 §3.2]` (the component and group member cells) and
  `[FIX-Orchestra]` (the surrogate-id cells) gain a 102 evidence note, with no status change.

### D9 — Integration after 100 and 101 (row 7's merge order)

**Superseded 2026-11-13 (owner ruling: "start 102 now ... just branch from this latest commit").** 102 is stacked on
101's Gate B head `99e956a0`, which already contains 100 (merged, `origin/main` `6714582d`) and 101. Every RED is
shown at `99e956a0`, a base that holds 100's and 101's code paths, so a cell needs a second RED base only when the
base delta of the post-squash rebase touches its code path. The merge or rebase below is replaced by one post-squash rebase, `git rebase --onto origin/main
99e956a0` (101's final head in place of `99e956a0` if 101 gained commits after it), run by the orchestrator when 101
squash-merges, followed by a delta check (tasks.md T055 to T059): an empty base delta needs no re-run; a non-empty one
re-runs what follows over the delta only. 100's nested-accessor republish is already in the base (the `100 (banner)`
pins in `read_tier_byte_diff_test.cmake`), so 102's republish (D5) is written against it. The text below is kept as
the Gate A decision it was.

Row 7 merges 100, then 101, then 102. 100 changes `table_view` (a section-group store and an arm-1 store), the
loaders' member capture (`DelimCapture` in `dictionary_internal.hpp`) and adds section walks to both loaders, and
regenerates every nested accessor's text (its pinned literal in `accessor_resolution_census_test.cpp`, and the
goldens); 101 adds a defined-tag store to `table_view`, filled in `Dictionary::build_table_view`. 102 edits the same
files (both loaders' `finalize`, `dictionary_internal.hpp`, `dictionary.cpp`) and the same pins. So 102 rebases (or
merges) after both have merged, and at the integrated head, before its first labelled push:

- re-runs the D1, D1b (all three recipes), D2, D7 and D8 recipes, because 100's section walks and capture are new reads of
  the same records and ids;
- republishes the goldens, the 078 trees and the read-tier hashes (D5) from the integrated tree, not by merging pins:
  100's nested-accessor text and 102's MultiCharValue and `Fields.hpp` text both move them;
- re-runs D5's differential with the **integrated merge-base**, not `fa778a8e`, as the base tree; otherwise 100's
  nested-accessor change shows up as stray hunks;
- re-runs the size pins, re-derived with
  `git grep -n -E 'sizeof\((fixpp::)?(dict::)?(ComponentRef|GroupRef|FieldRef|table_view|dict_metadata_handle)\b' -- include src tests bench`
  (the record headers' `static_assert`s, `ref_shape_test.cpp`, the footprint bench), and D6's paired bench and
  footprint against the merge-base at measurement time;
- re-runs the accessor census (`accessor_resolution_census_test.cpp`), which then carries 100's nested literal and
  102's `mapped_kind` and Fields-row parser change.
RED evidence stays on `fa778a8e` for the cells that 100 and 101 do not touch; a cell whose code path 100 or 101 changes
(a loader walk, a `table_view` read, a generated accessor) records its RED on the integrated merge-base, and the
verify record says which base each RED was shown on.

## Re-derivation recipes

- Side-table starts: expand each definition as `finalize` does (components in declaration order, each fully expanded;
  groups first seen in the message, then component, loops, then sorted by NumInGroup tag), accumulate the starts, and
  compare with 65535.
- Component name offsets: follow the name pool's interning order (field names, enum values and descriptions, message
  types and names, then component names).
- Component identity, surrogate ids and their holders, readers: the `git grep` recipes in D1, D1b and D7.
- The retired check's dependents and the amendment set: the recipes in D2 and D8.
- Scratch scripts used on 2026-11-07 are not part of the repository.
