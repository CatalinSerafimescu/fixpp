# Research: 102 dictionary index width and the MultiCharValue accessor type

Code read at `fa778a8e`. Symbols, not lines. Measurements re-derivable with the recipe at the end.

## Survey

### Dictionary records (#654)

- `ComponentRef` (`include/fixpp/dict/component_ref.hpp`) and `GroupRef` (`group_ref.hpp`): six `std::uint16_t` each,
  `sizeof == 12` asserted in both headers and in `tests/dictionary/ref_shape_test.cpp`. `FieldRef` (`field_ref.hpp`)
  is 16 bytes with 16-bit `condition_index`, `group_no_tag`, `component_index`, `enum_table_index`,
  `length_pair_data_tag`.
- Internal indexes are already 32-bit (`NameSlice`, `MsgFieldsRun`, `NamedIndex`, `EnumRun` in
  `src/dictionary/dictionary_internal.hpp`).
- Each loader's `finalize` (`XmlLoader` in `src/dictionary/xml_loader.cpp`, `OrchestraLoader` in
  `src/dictionary/orchestra_loader.cpp`) narrows, per component and per group:
  - the side-table start, `static_cast<std::uint16_t>(…size())` → **wraps on FIX50SP2's component table**
    (first at `RgstDistInstGrp`; the wrong slice still passes `component_fields_impl`'s bounds check);
  - the member count, `std::min<std::size_t>(…, 65535U)` → saturates; no vendored count reaches it;
  - the component name offset, `std::min<std::uint32_t>(ns.offset, 65535)` → **clamped for every component of FIX50SP1,
    FIX50SP2 and FIX Latest**; no `src/` reader (component lookup uses the 32-bit `NamedIndex`); the only reader is the
    equivalence fixture, which agrees because both sides saturate;
  - the component index (`component_id`, `parent_component_id`, `FieldRef::component_index`, the XML loader's
    `next_id` counter, an implicit wrap): bounded by the component count, which QuickFIX XML does not bound and Orchestra
    ids bound at 65535; no vendored base comes near.
- Group starts and counts do not exceed 16 bits on any vendored base (FIX Latest's group table is the largest, about
  half the range).
- `FieldRef::condition_index` and `enum_table_index` are never written (always 0).
- Readers: `Dictionary::component_fields` / `component_fields_impl`, `group_fields` / `group_fields_impl`
  (`src/dictionary/dictionary.cpp`); `build_table_view` reads `group_fields` to fill the context-free group store;
  `component_fields` has no production caller. `emit_fields.cpp` writes `FieldRef` columns into generated `Fields.hpp`.
- 098's overlay check: `kSideTableMax`, `side_table_fits`, and the refusal arms in `overlay_merge.cpp` (one per format)
  refuse only when the base fits and the merged result does not; FIX50SP2's base already does not fit, so the check can
  never fire there. Overlay caps bound new components and groups (`kMaxNewComponents`, `kMaxNewGroups`).
- Public: the headers are installed; the C ABI is opaque (`fixpp_dict_t`); SWIG does not expose them.
- Tests touching the shapes or the wrap: `ref_shape_test.cpp`; `dictionary_equivalence.hpp`'s `census_excluded` wrap
  exclusions; the wrap probe in `overlay_equivalence_test.cpp`; the side-table refusal rows in
  `overlay_refusal_test.cpp`; readers in `lookup_test.cpp`, `xml_loader_test.cpp`.

### Codegen (#621)

- `kind_of` (`tools/codegen/fixpp-codegen/gen_util.hpp`) maps `Char` and `MultiCharValue` to `TypeKind::Char`;
  `MultiStringValue` to `TypeKind::String`.
- Consumers of the kind: `kind_cpp_type` / `emit_scalar` (`emit_messages.cpp`, view accessors at top level and in group
  entries); the owning accessors in `emit_reify.cpp`; `args_cpp_type` (`emit_builders.cpp`, `Args` members);
  `builder_call_kind` / `entry_set_name` (`gen_util.hpp`, `set_char` vs `set_string` in group entries). The top-level
  `bb.field(tag, …)` text is the same for both kinds (overload by type). Validators, manifest, dispatch emitters do not
  read the kind; runtime multi-value enum checking keys on `field_data_type`, not the kind.
- MultiCharValue fields: none in v42, v44, FIXT.1.1; 8 in FIX50SP2 and in FIX Latest (tags 18, 286, 291, 292, 529, 546,
  1308, 1384). FIX50/SP1 are not codegen targets.
- Checked-in oracles that move: `specs/003-dictionary-codegen/contracts/golden/v50sp2_Messages.golden.hpp`;
  `specs/076-fix-latest-typed-codegen/contracts/golden/vlatest_Messages.golden.hpp`; the v50sp2 and vlatest trees under
  `specs/078-precompiled-builder-libs/contracts/golden/` (group `Args`, message `Args`, builder `.cpp`/`.inl` of the
  messages carrying these fields); `read_tier_byte_diff_test.cmake`'s v50sp2 Messages and Reify hashes (the Messages
  hash equals the 003 golden's); the census table `mapped_kind` in `tests/codegen/accessor_resolution_census_test.cpp`.
- Test sources that assign a `char` to these `Args` members: `tests/session/test_077_allversions_builder_roundtrip.cpp`
  and its vlatest twin.

## Decisions

### D1 — Widen what can exceed 16 bits; refuse what the format bounds (FR-001)

- `ComponentRef`: `first_field_index`, `field_count` and `name_offset` become `std::uint32_t`; `component_id` and
  `parent_component_id` stay 16-bit. Members are ordered for alignment (16-bit pair first, then the three 32-bit
  values); `_reserved` goes; `sizeof == 16`, standard layout, trivially copyable, all asserted.
- `GroupRef`: `first_field_index` and `field_count` become `std::uint32_t`; `no_tag`, `first_field_tag`,
  `parent_group_no_tag` and `_reserved` stay 16-bit; `sizeof == 16`.
- Both loaders' `finalize` write the 32-bit values with no cast or clamp.
- Component index: both loaders refuse, at load, a dictionary with more than 65535 components (a `dict_xml_parse_failed`
  -class error naming the cap), so `component_id`, `parent_component_id`, `FieldRef::component_index` and the XML
  loader's counter cannot wrap. Orchestra ids are already 16-bit; QuickFIX XML gains the cap. No vendored base is near.
- Rationale: one record per definition; the side-table values are the only ones a real base exceeds; a 65535-component
  dictionary is not a FIX dictionary any vendor ships, and a refusal is an observable ending where a wrap is not.
- Alternatives: widen every field (FieldRef grows, every generated `Fields.hpp` row changes, for no reachable value);
  refuse a side table past 65535 (refuses the vendored FIX50SP2).

### D2 — Retire 098's side-table fit check (FR-003)

With 32-bit starts and counts, a side table would need 2^32 entries; the overlay caps bound what an overlay adds, and
the base's own size is bounded by the input. `kSideTableMax`, `side_table_fits`, the two refusal arms and their
`overlay_limit_error` texts are removed; the D1 component cap covers the one 16-bit value an overlay can push. The
refusal rows in `overlay_refusal_test.cpp` that exercise the side-table arm are deleted or re-pointed at the component
cap, each named.

### D3 — MultiCharValue is a string kind (FR-004, FR-005)

`kind_of` maps `MultiCharValue` to `TypeKind::String`. Every consumer then emits what it emits for MultiStringValue:
`expected_t<std::string_view>` accessors (lifetimebound), `std::optional<std::string_view>` `Args` members,
`set_string` in group entries. No other field's text changes. `field_traits<char>` is unchanged (it stays the decoder of
`Char` fields).

### D4 — Witnesses

- #654: the equivalence fixture's wrap exclusions are removed, so it compares every component's and group's members
  (and name) with an independent expansion of the source file, for every vendored base, with and without an overlay.
  RED on `fa778a8e` from `RgstDistInstGrp`. A loader cell for the component cap (a generated dictionary of 65536
  components) and its control at 65535.
- #621: a v50sp2 and a vlatest decode cell on `18=1 2` and `18=1`; a builder round-trip of `exec_inst = "1 2"` and of
  a group entry's `LegExecInst(1384)`.
- Mutation: each change reverted alone turns its cell RED.

### D5 — Republishing

Regenerate the 003 v50sp2 and 076 vlatest goldens and the 078 v50sp2 and vlatest trees with the recipes in
`tests/codegen/determinism_test.cpp`'s failure messages; recompute the two read-tier hashes; move `mapped_kind`'s
MultiCharValue case to the string kind; change the `077` round-trip assignments to string literals. Every other golden
and pin must stay byte-identical (SC-003), checked by the same tests.

### D6 — Performance (SC-004)

The records grow from 12 to 16 bytes; loader work is unchanged. `XmlLoader::load` on FIX50SP2 and `as_table_view` are
measured paired against the merge-base (Article VIII §2's local A-B-A-B protocol); the accessor change moves no hot
path (a `string_view` decode is cheaper than the `char` decode's length check plus copy).

## Re-derivation recipe for the measurements

Expand each definition as `finalize` does (components in declaration order, each fully expanded; groups first seen in
the message, then component, loops, then sorted by NumInGroup tag), accumulate the starts, and compare with 65535;
the component name offsets follow the name pool's interning order (field names, enum values and descriptions, message
types and names, then component names). Scratch scripts used on 2026-11-07 are not part of the repository.
