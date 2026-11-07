# Research: 102 dictionary index width and the MultiCharValue accessor type

Code read at `fa778a8e`. Symbols, not lines. Every inventory below is stated with the recipe that re-derives it; the
recipe, not a list or a count, is authoritative.

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
  `size_t` with `static_cast<std::uint32_t>` (D2). Re-derive:
  `grep -n 'static_cast<std::uint32_t>' src/dictionary/{xml,orchestra}_loader.cpp`.
- `emit_fields.cpp`'s `emit_field_ref` writes one positional initializer per `FieldRef` member and ends each row with a
  literal `, 0 },` for `_reserved`. `accessor_resolution_census_test.cpp`'s Fields parser requires nine numbers per
  row; `validator_shape_test.cpp` reads `_reserved` of a generated v44 row.
- 098's overlay check: `kSideTableMax`, `side_table_fits`, and the refusal arms in `overlay_merge.cpp` (one per format)
  refuse only when the base fits and the merged result does not; FIX50SP2's base already does not fit, so the check can
  never fire there. Overlay caps bound new components and groups (`kMaxNewComponents`, `kMaxNewGroups`).
- Public: the headers are installed; the C ABI is opaque (`fixpp_dict_t`; `src/capi` reads `FieldRef` members by
  name, `rule` and `type`, neither of them widened, re-derive with `grep -n 'fr\.' src/capi/*.cpp`); SWIG does not
  expose the records. `table_view`'s own stores hold tags and parent paths of tags, not indexes into the records.
- Tests touching the shapes or the wrap: `ref_shape_test.cpp`; `dictionary_equivalence.hpp`'s `census_excluded` wrap
  exclusions, its side-against-side `check` lambdas (16-bit parameters, so a 32-bit member would compare modulo 65,536,
  which is the wrap signature itself), and its `_reserved` comparisons and registry entries; the wrap probe in
  `overlay_equivalence_test.cpp`; the side-table refusal rows in `overlay_refusal_test.cpp`; readers in
  `lookup_test.cpp` (a `static_cast<std::uint16_t>(… + 1U)` comparison), `xml_loader_test.cpp`, `round_trip_test.cpp`.

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
  `git grep -nE 'uint16_t[^;,)]*(component|parent_comp_id|parent_of_|next_id)|static_cast<std::uint16_t>\((i|i \+ 1|[a-z_]+->second \+ 1|components_\.size\(\))\)' -- src/dictionary include/fixpp/dict tools/codegen tests/dictionary/dictionary_equivalence.hpp`
  (the class stops at `,` and `)`, so a parameter list with a 16-bit tag before a widened index does not match) is
  widened at tasks, and every clamp hit of `git grep -nE 'min<std::(size_t|uint32_t)>\([^)]*65535' -- src/dictionary`
  is removed. Both recipes print nothing on the head, outside comments; `fa778a8e` is their known positive. The
  surrogate-id keys are D1b's recipe, not this one.
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
  (`<fixr:component>`, `<fixr:group>`, `componentRef`, `groupRef`) are parsed to `std::uint32_t` and admitted up to the
  schema's `maxInclusive` (2147483647); above it, or malformed, they are `orchestra_parse_error` as today. That bound is
  the format's own rule, so the refusal above it is not a cap. 0 stays admitted (Survey).
- Every site that parses a surrogate id moves together. Most of them skip silently on a parse miss (`continue`,
  `std::nullopt`, no insert), so a site left 16-bit becomes a silent divergence, not an error:
  - the loader: `collect_components`, `collect_groups`, `expand_field_list`'s componentRef and groupRef arms, the
    parent-map pass's tolerant read, the `component_index_by_xml_id_` and `group_by_xml_id_` keys, and
    `collect_header_tags`' `id_of`, which reads all three kinds through one width and is split by kind;
  - `overlay_merge.cpp`: `orch_id_key_of`, `orch_child_key`, and the Orchestra graph build's `component_by_id`,
    `group_by_id` and `records_of`'s `has_id` (a `fieldRef`'s and a `numInGroup`'s id stay tag-parsed);
  - the code generator's `ir.cpp`: `try_parse_orchestra_uint16` at component and group sites, and the
    `OrchestraComponentIndex` and `OrchestraGroupIndex` keys. A miss there is "defensive only, continue" and would
    silently drop a component's members from the IR;
  - the equivalence fixture's Orchestra raw reader. The independent oracles that already key by `std::uint32_t`
    (`required_scope_oracle.hpp`) need no change.
- Enumerate: every hit of
  `git grep -nE 'try_parse_uint16|parse_orchestra_id|try_parse_orchestra_uint16|by_xml_id_|by_id\b|attribute\("id"\)\.as_u?int' -- src/dictionary tools/codegen tests/dictionary tests/codegen`
  is classified tag or surrogate at tasks. A surrogate hit left 16-bit on the head is a defect.
- Alternative: keep the 16-bit surrogate parse as a "format rule". Rejected: the schema says otherwise, and with the
  index widened it would be the one place a 65,537th Orchestra component is refused, which is the cap B86 rejected.

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
- `kSideTableMax`, `side_table_fits`, the two refusal arms and their `overlay_limit_error` texts are removed. The
  refusal rows in `overlay_refusal_test.cpp` that exercise the side-table arm are deleted, each named in the tasks.
  Nothing replaces them: after D1 no 16-bit value is left for an overlay to push.
- **Text for the orchestrator to add to `phases/phase-4/issue-batches.md` §B86** (102 does not edit that file):
  > - **From 102 (B84), Gate A round 1, 2026-11-07.** 102 widens the component and group side-table starts and
  >   counts, the component name offset and the whole component identity to 32 bits, and retires 098's side-table fit
  >   check. The ruling note's refusal order "cycle, budget, depth, side-table fit" therefore becomes **cycle, budget,
  >   depth**. B86 owns the 32-bit obligation in its place: its absolute expansion budget must imply that every
  >   expansion-derived run end (each side table, and each per-message `MsgFieldsRun`) is at most `UINT32_MAX`. It does
  >   by construction (a walk step emits at most one entry, and the budget is far below 2^32); B86's shipped text states
  >   that derivation, and B86 amends L-102-1 (a) when it lands. The name-pool leg, L-102-1 (b) (4 GiB or more of
  >   names), outlives B86, per the ruling's "no byte caps".
- The name pool: a pool past 2^32 bytes needs 4 GiB or more of host-chosen names; B86 rejected byte caps, so this
  residual outlives B86. Its effect is a wrong name, not an out-of-bounds read: a wrapped offset is smaller than the
  true one and a truncated length is shorter, so `dict_metadata_handle::name_at` stays inside the pool. Disclosed as
  L-102-1 (b); no guard.

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
    65535 merges it into that component (RED with a 16-bit `overlay_merge` id parse).
  - Codegen: an IR build (`build_ir`) over a small Orchestra fixture whose component and group ids pass 65535 gives the
    message the component's members and the group's order (RED with a 16-bit `ir.cpp` id parse).
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
- Mutation: each change reverted alone turns its own cell RED, in particular:
  - the side-table widening → the equivalence fixture at `RgstDistInstGrp`;
  - the name-offset widening → the name-offset oracle;
  - the XML component counter or index → the XML identity cell;
  - Orchestra's index, with the id parse kept wide → the Orchestra identity cell;
  - each surrogate-id site alone (loader, `overlay_merge`, `ir.cpp`) → its own cell;
  - one reader's `std::size_t` arithmetic → the reader cell;
  - `kind_of` → the decode cells and the census.

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
  and `_dispatch/`) with the `fa778a8e` codegen and with the head codegen, and diff the trees recursively. The head is
  accepted only if:
  - every `Fields.hpp` equals the base's after the row-trailer transform (each row's `, 0 },` becomes ` },`), and
    nothing else changes in it;
  - every other hunk is an accessor, an `Args` member or an entry setter of a tag in
    {18, 286, 291, 292, 529, 546, 1308, 1384};
  - every other file is byte-identical.
  The filter is shown able to report a stray hunk: a planted extra change in one non-MultiCharValue file is reported,
  then removed. The verify record holds the commands, the hunk classification and the planted positive.

### D6 — Performance (SC-004)

- The records grow (`ComponentRef` from 12 to 20 bytes, `GroupRef` from 12 to 16; `FieldRef` stays 16); loader work is
  unchanged.
- Timing: `bench/dictionary/xml_loader_bench` (a `paired` row in `bench/ci-suite.txt`), run locally paired against the
  merge-base `fa778a8e` under Article VIII §2: release build of both trees, A-B-A-B, min-per-tree, the +5% budget; above
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
changed.

### D8 — Records and amendments (FR-007)

- B&L:
  - L-098-4 and L-621-1 closed (moved to the closed file);
  - B-098-1 amended: the side-table fit refusal is gone, and its cause-list entry with it;
  - new **B-102-1**: the `ComponentRef`, `GroupRef` and `FieldRef` layouts, with the contract's migration;
  - new **B-102-2**: the MultiCharValue accessor and builder types;
  - new **B-102-3**: an Orchestra document whose component or group ids pass 65535 now loads; a QuickFIX XML or
    Orchestra base of more than 65,535 components loads with correct identities;
  - new **L-102-1**: the 32-bit residual. (a) Until B86, an unbounded base expansion can wrap a 32-bit run end; B86's
    budget closes it. (b) A name pool past 2^32 bytes gives wrong names, in bounds; it outlives B86;
  - L-098-3 gains a pointer to L-102-1 (a).
- Amended in place with an `Updated (102-dict-width-multichar-type)` note:
  - `.specify/2c-codegen.md` §4.1 and §4.2: the layouts, `alignof`, the `_reserved` discipline that no longer applies
    to `FieldRef` and `ComponentRef`, and the lost `FIXPP_DICT_COMPONENTREF_RESERVED_USED` slot;
  - `specs/002-dictionary-xml-loader/contracts/field_ref.hpp`, `component_ref.hpp`, `group_ref.hpp` and its
    `data-model.md`: a superseded-by note, not a rewrite;
  - `brain/components/dictionary.md`: 102 listed against 2c §4.1/§4.2, and the superseded 002 extracts flagged;
  - the three installed headers' banners and `_reserved` paragraphs;
  - `include/fixpp/dict/error.hpp` (`overlay_limit_error`'s "side table stop fitting 16 bits" cause),
    `include/fixpp/dict/xml_loader.hpp` (`load_with_overlay`'s cause list) and `orchestra_loader.hpp` (which refers to
    it);
  - the comment above `parse_orchestra_field_tag` (D1b's split; the schema's `positiveInteger`).
- Catalogue / coverage index: the D-007 and D-010 rows gain 102.

## Re-derivation recipes

- Side-table starts: expand each definition as `finalize` does (components in declaration order, each fully expanded;
  groups first seen in the message, then component, loops, then sorted by NumInGroup tag), accumulate the starts, and
  compare with 65535.
- Component name offsets: follow the name pool's interning order (field names, enum values and descriptions, message
  types and names, then component names).
- Component identity, surrogate ids, readers: the `git grep` recipes in D1, D1b and D7.
- Scratch scripts used on 2026-11-07 are not part of the repository.
