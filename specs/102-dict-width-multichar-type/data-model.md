# Data model: 102

Layouts are asserted with `static_assert` in each header and again in `tests/dictionary/ref_shape_test.cpp`
(`sizeof`, `alignof`, `offsetof` of every member, standard layout, trivially copyable).

## `FieldRef` (after)

Declared order unchanged; `component_index` widened in place; `_reserved` removed.

| Offset | Member | Type | Note |
|---|---|---|---|
| 0 | `tag` | `std::uint16_t` | FIX tag |
| 2 | `type` | `field_data_type` (`std::uint8_t`) | |
| 3 | `rule` | `field_presence` (`std::uint8_t`) | |
| 4 | `condition_index` | `std::uint16_t` | never written (0) |
| 6 | `group_no_tag` | `std::uint16_t` | FIX tag |
| 8 | `component_index` | `std::uint32_t` | was `std::uint16_t`; 0 = not in a component, else the component's index + 1 |
| 12 | `enum_table_index` | `std::uint16_t` | never written (0) |
| 14 | `length_pair_data_tag` | `std::uint16_t` | FIX tag |

`sizeof == 16` (unchanged), `alignof == 4` (was 2).

## `ComponentRef` (after)

Declared order unchanged; every member `std::uint32_t`; `_reserved` removed.

| Offset | Member | Type | Note |
|---|---|---|---|
| 0 | `component_id` | `std::uint32_t` | 0-based declaration index; was 16-bit and wrapped past 65,535 components |
| 4 | `name_offset` | `std::uint32_t` | was clamped at 65535 |
| 8 | `first_field_index` | `std::uint32_t` | was wrapped modulo 65,536 |
| 12 | `field_count` | `std::uint32_t` | was clamped at 65535 |
| 16 | `parent_component_id` | `std::uint32_t` | 0 = top level; otherwise the enclosing component's `component_id` + 1 |

`sizeof == 20` (was 12), `alignof == 4`.

## `GroupRef` (after)

Declared order unchanged; the two side-table values widened.

| Offset | Member | Type |
|---|---|---|
| 0 | `no_tag` | `std::uint16_t` |
| 2 | `first_field_tag` | `std::uint16_t` |
| 4 | `first_field_index` | `std::uint32_t` |
| 8 | `field_count` | `std::uint32_t` |
| 12 | `parent_group_no_tag` | `std::uint16_t` |
| 14 | `_reserved` | `std::uint16_t` (0) |

`sizeof == 16` (was 12), `alignof == 4`. The group index is not stored in a record; inside the loaders it stays 16-bit,
bounded by the NumInGroup tag domain (research Survey).

## Loader behaviour

- No refusal is added. A QuickFIX XML or Orchestra base of any number of components loads with each component's own
  id, parent id, members and name.
- Orchestra component and group ids (and their refs) are read in the schema's `id_t` domain, up to 2147483647; above it,
  or malformed, `orchestra_parse_error` as today. Field ids, `lengthId` and `numInGroup` ids stay FIX tags, 1..65535.
- 098's side-table fit refusal is removed (research D2).

## Generated type of a MultiCharValue field

| Surface | Before | After |
|---|---|---|
| view / owning accessor | `expected_t<char>` | `expected_t<std::string_view>` |
| builder `Args` member | `std::optional<char>` | `std::optional<std::string_view>` |
| group entry setter | `set_char` | `set_string` |

## Generated `Fields.hpp` row

Before: `{ tag, type, rule, condition_index, group_no_tag, component_index, enum_table_index, length_pair_data_tag, 0 },`
After: the same values without the trailing `0` (eight initializers). No value changes on a vendored base.
