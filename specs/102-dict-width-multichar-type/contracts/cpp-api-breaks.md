# Contract: C++ API changes in 102 (BREAKING, C++ only; C ABI and Python unchanged)

Pre-release, declared under Article XVII §1 (Gate A on a public C++ surface). The C ABI is unchanged: `fixpp_dict_t`
is opaque, `src/capi` reads `FieldRef::rule` and `type` (neither widened), and SWIG exposes none of these types
(Article X: not engaged).
B&L rows: B-102-1 (layouts), B-102-2 (MultiCharValue types), B-102-3 (loads that now succeed); B-098-1 amended.

## 1. `fixpp::dict::FieldRef`, `ComponentRef`, `GroupRef` change layout (data-model.md)

- `FieldRef`: `component_index` is `std::uint32_t`; `_reserved` is gone; `sizeof` stays 16, `alignof` goes from 2 to 4.
- `ComponentRef`: all five members are `std::uint32_t`; `_reserved` is gone; `sizeof` goes from 12 to 20, `alignof` 4.
- `GroupRef`: `first_field_index` and `field_count` are `std::uint32_t`; `sizeof` goes from 12 to 16, `alignof` 4.
- Declared member order is unchanged in all three.

What still compiles and keeps its meaning:

- code that reads or writes members by name, into a type at least as wide;
- designated initializers that do not name `_reserved`;
- positional initializers of `FieldRef` (eight values), `ComponentRef` (five values) and `GroupRef` (six values),
  because the order is unchanged.

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
- code that depends on `sizeof` 12 or `alignof(FieldRef) == 2`, or stores the records in a binary format.

Migration: widen the receiving variable to `std::uint32_t` (or `std::size_t`); drop `_reserved`; prefer designated
initializers.

## 2. Loads that now succeed (B-102-3)

- A QuickFIX XML or Orchestra base of more than 65,535 components loads, each component with its own id, parent id,
  members and name. Before, identities past that point wrapped silently. Identities, members and names are exact
  while every table stays below 2^32 entries (components, side-table entries, name-pool bytes; L-102-1 (a)–(c)).
- An Orchestra document whose component or group ids pass 65535 loads; before, it was `orchestra_parse_error`. Ids above
  2147483647 (the schema's `maxInclusive`) are still refused.

## 3. 098's side-table refusal is removed

`overlay_limit_error` for "the components/groups side table would stop fitting 16 bits" no longer exists. An overlay
that made a side table pass 65,535 entries now loads, with correct members.

## 4. MultiCharValue types in `fixpp::v50sp2` and `fixpp::vlatest`

Every MultiCharValue accessor (view and owning, top level and group entry) returns `expected_t<std::string_view>`; every
such builder `Args` member is `std::optional<std::string_view>`; group entries use `set_string`. A caller assigning a
`char` writes a one-character string literal instead (`args.exec_inst = "1";`). A caller comparing the result with a
`char` compares with a one-character string. The precompiled builder libraries' C++ ABI changes accordingly.

## 5. Generated `Fields.hpp`

Every `FieldRef` row has eight initializers instead of nine. Code that includes a regenerated `Fields.hpp` sees no value
change; a hand-written copy of a row with nine values fails to compile.
