# Contract: C++ API changes in 102 (BREAKING, C++ only; C ABI and Python unchanged)

Pre-release, declared under Article XVII §1 (Gate A on a public C++ surface). The C ABI is unchanged: `fixpp_dict_t`
is opaque, `src/capi` reads `FieldRef::rule` and `type` (neither widened), and SWIG exposes none of these types
(Article X: not engaged).

Re-derive the claim with one recipe over the C ABI, its implementation and the SWIG interface. It names the record
types, their widened members and every `Dictionary` member that returns or spans a record:
`command git grep -nE 'FieldRef|ComponentRef|GroupRef|field_ref\(|[.>]component\(|[.>]group\(|component_fields|group_fields|message_fields|first_field_index|component_index|component_id|name_offset|parent_component_id' -- src/capi bindings include/fix`.
Its known positive on the parent base is `src/capi/message_write.cpp`'s `field_ref(` reads, each of which reads only
`rule` or `type`. Every hit is classified as reading a member 102 does not widen, or a defect. Whether the C-ABI
*results* that change with §2 and §3 are a breaking change under `[const §X.7]` awaits the owner
(`checklists/api-abi.md` CHK001 to CHK003).

B&L rows: B-102-1 (layouts), B-102-2 (MultiCharValue types), B-102-3 (loads that now succeed); B-098-1 amended.

Every C++ consumer of the `fixpp/dict` headers rebuilds against them. libfixpp's own C++ ABI changes wherever a
member returns, spans or takes a record: `Dictionary::component`, `group`, `component_fields`, `group_fields`,
`message_fields`, `field_ref`, and every other such signature.

## 1. `fixpp::dict::FieldRef`, `ComponentRef`, `GroupRef` change layout (data-model.md)

- `FieldRef`: `component_index` is `std::uint32_t`; `_reserved` is gone; `sizeof` stays 16, `alignof` goes from 2 to 4.
- `ComponentRef`: all five members are `std::uint32_t`; `_reserved` is gone; `sizeof` goes from 12 to 20, `alignof` 4.
- `GroupRef`: `first_field_index` and `field_count` are `std::uint32_t`; `sizeof` goes from 12 to 16, `alignof` 4.
  `GroupRef::_reserved` stays as the explicit tail pad: the record is 16 bytes at `alignof == 4` with it or without
  it. 2c §4.2's `_reserved` discipline (zero on emit, ignored on read) now applies to `GroupRef` alone.
- Declared member order is unchanged in all three.
- All three stay standard-layout and trivially copyable, asserted in each header and in
  `tests/dictionary/ref_shape_test.cpp` (data-model.md).
- The forward-compatibility macros `FIXPP_DICT_FIELDREF_RESERVED_USED` and `FIXPP_DICT_COMPONENTREF_RESERVED_USED`
  were never defined: outside `specs/` and `.specify/` only a header comment names one (re-derive:
  `command git grep -n RESERVED_USED -- ':!specs' ':!.specify'`). A consumer that tests them sees no change, but the
  slots they reserved are gone.

What still compiles and keeps its meaning:

- code that reads or writes members by name, into a type at least as wide;
- designated initializers that do not name `_reserved`;
- positional initializers of `FieldRef` (eight values), `ComponentRef` (five values) and `GroupRef` (six values),
  because the order is unchanged;
- a braced list whose elements are `std::uint16_t` variables, because widening is not narrowing;
- a positional initializer with fewer values than members: the trailing members are value-initialized (0), as before.

What fails to compile:

- any use of `FieldRef::_reserved` or `ComponentRef::_reserved` (for example a `_reserved == 0` check, or
  `cr._reserved = 0`);
- a positional initializer that still supplies `_reserved` to `FieldRef` (nine values) or `ComponentRef` (six values);
- a braced narrowing initialization from a widened member, for example `std::uint16_t x{cr.first_field_index}` or
  `std::uint16_t id{fr.component_index}`.

What compiles and changes silently:

- a non-braced narrowing, for example `std::uint16_t x = cr.component_id;` or `static_cast<std::uint16_t>(…)`,
  truncates on a dictionary of more than 65,535 components or a side table past 65,535 entries;
- arithmetic on a widened member: it was promoted to `int` and is now unsigned 32-bit, so a difference such as
  `a.first_field_index - b.first_field_index`, or `id - 1` on an id of 0, wraps where it used to go negative;
- code that depends on `sizeof` 12 or `alignof(FieldRef) == 2`, or stores the records in a binary format;
- code that depends on a member's offset or on the record's bytes: `offsetof`, `memcpy` into or out of a record, a
  reinterpretation of a record's storage, or a record inside a packed enclosing struct. Every `ComponentRef` member
  after the first moves, as do `GroupRef`'s members after `first_field_index` and `FieldRef`'s `enum_table_index` and
  `length_pair_data_tag` (compare data-model.md's tables with the parent base's headers);
- a type change seen through deduction: `auto x = cr.component_id;` or `decltype(…)` now names `std::uint32_t`,
  overload resolution between a `std::uint16_t` and a `std::uint32_t` overload picks the other one, and a template
  keyed on the member's type is instantiated for the other type.

Migration: widen the receiving variable to `std::uint32_t` (or `std::size_t`); drop `_reserved`; prefer designated
initializers.

## 2. Loads that now succeed (B-102-3)

- A QuickFIX XML or Orchestra base of more than 65,535 components loads, each component with its own id, parent id,
  members and name. Before, identities past that point wrapped silently. Identities, members and names are exact
  while every table stays below 2^32 entries (components, side-table entries, name-pool bytes; L-102-1 (a)–(c)).
  Past that bound a caller gets wrong answers, in bounds, with no error and no way to ask (L-102-1).
- An Orchestra document whose component or group ids pass 65535 loads; before, it was `orchestra_parse_error`. Ids above
  2147483647 (the schema's `maxInclusive`) are still refused. `orchestra_parse_error` thereby loses the cause "a
  component, group or ref id above 65535". No installed header documented it: `orchestra_loader.hpp`'s `Throws:`
  list says only "malformed".
- On the vendored FIX50SP2, `Dictionary::component_fields("RgstDistInstGrp")`, and that of every component whose
  side-table start passed 65535, now returns the component's own members; before, it returned another definition's,
  with no error (L-098-4, closed). B-102-3 records it.

## 3. 098's side-table refusal is removed

`overlay_limit_error` for "the components/groups side table would stop fitting 16 bits" no longer exists. An overlay
that made a side table pass 65,535 entries now loads, with correct members. The cause is documented in
`error.hpp`'s `overlay_limit_error` comment and in `xml_loader.hpp`'s `load_with_overlay` comment, both in research
D8's amendment set. `overlay_limit_error` itself stays, for its other causes: a cap, a DOCTYPE, the expansion budget
and the step counter. Code that catches it keeps its meaning, and at the C ABI it still maps to
`FIXPP_ERR_DICT_LIMIT_EXCEEDED`.

## 4. MultiCharValue types in `fixpp::v50sp2` and `fixpp::vlatest`

Every MultiCharValue accessor (view and owning, top level and group entry) returns `expected_t<std::string_view>`; every
such builder `Args` member is `std::optional<std::string_view>`; group entries use `set_string`. A caller assigning a
`char` writes a one-character string literal instead (`args.exec_inst = "1";`). A caller comparing the result with a
`char` compares with a one-character string. The precompiled builder libraries' C++ ABI changes accordingly.

The affected symbols are every accessor, `Args` member and group-entry setter generated for a MultiCharValue field in
`fixpp::v50sp2` and `fixpp::vlatest`. Re-derive them from the MultiCharValue tag set (research Survey "Codegen"; spec
FR-004's classification); SC-003's differential lists them. `fixpp::v42`, `fixpp::v44` and `fixpp::vt11` have no
MultiCharValue field, so their typed API changes only through §5's row.

What fails to compile (each checked by a scratch compile under GCC 13 and Clang 22, `-std=c++23`, with
`std::expected<std::string_view, E>` and `std::optional<std::string_view>`):

- `char c = *acc;`, `switch (*acc)`, `*acc == '1'` and `acc == '1'`;
- `args.exec_inst = '1';`, and assigning a `char` variable or a `std::optional<char>` to the member.

What compiles and changes silently:

- `auto v = *acc;` now deduces `std::string_view`. A later use that accepts both types changes meaning: overload
  resolution between a `char` and a `std::string_view` overload picks the other one, and a template keyed on the
  type is instantiated for `std::string_view`.
- Lifetime. The accessor used to return the `char` by value; it now returns a view into the message's buffer
  (`[[clang::lifetimebound]]`, as for every MultipleStringValue accessor). A caller that keeps the value after the
  message, its buffer or an owning (reified) object is moved or destroyed compiled and was correct before. It still
  compiles, and now dangles. Migration: copy the value into a `std::string` (or a `char` where one character is
  meant) before the source goes away.
- Lifetime, builder. `args.exec_inst = std::string{"1 2"};`, or any temporary `std::string`, compiles and leaves the
  member dangling before the build call reads it. Neither compiler warns at `-Wall -Wextra`. The caller's obligation
  is the one every MultipleStringValue `Args` member already carries: the viewed characters outlive the build call.

A consumer of a precompiled builder library rebuilds the library and its own code against the 102 headers; an
installed package ships both rebuilt. The libraries carry no version of their own (`cmake/Codegen.cmake` sets
none), so the change is recorded by B-102-2 and the PR description's BREAKING marker only.

## 5. Generated `Fields.hpp`

Every `FieldRef` row has eight initializers instead of nine. Code that includes a regenerated `Fields.hpp` sees no value
change; a hand-written copy of a row with nine values fails to compile. The package installs each version's generated
read-side headers, `Fields.hpp` among them (`fixpp_typed_tier_expected_children` in `cmake/FixppTypedTierRule.cmake`),
and, where the typed tier is installed (`FIXPP_INSTALL_TYPED_TIER`, #613), the builder libraries. So an installed
consumer receives the regenerated row from the new package. A consumer that vendored a generated header regenerates
it.
