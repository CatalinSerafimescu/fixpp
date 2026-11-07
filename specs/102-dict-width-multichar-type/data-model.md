# Data model: 102

## `ComponentRef` (after)

| Member | Type | Note |
|---|---|---|
| `component_id` | `std::uint16_t` | bounded by the D1 component cap |
| `parent_component_id` | `std::uint16_t` | 0 = top level; 1-based otherwise |
| `name_offset` | `std::uint32_t` | was clamped at 65535 |
| `first_field_index` | `std::uint32_t` | was wrapped modulo 65536 |
| `field_count` | `std::uint32_t` | was clamped at 65535 |

`sizeof == 16`, `alignof == 4`, standard layout, trivially copyable.

## `GroupRef` (after)

| Member | Type |
|---|---|
| `no_tag`, `first_field_tag`, `parent_group_no_tag`, `_reserved` | `std::uint16_t` |
| `first_field_index`, `field_count` | `std::uint32_t` |

`sizeof == 16`, `alignof == 4`.

## Loader refusal

A base or merged dictionary with more than 65535 components fails to load (QuickFIX XML and Orchestra), naming the cap.

## Generated type of a MultiCharValue field

| Surface | Before | After |
|---|---|---|
| view / owning accessor | `expected_t<char>` | `expected_t<std::string_view>` |
| builder `Args` member | `std::optional<char>` | `std::optional<std::string_view>` |
| group entry setter | `set_char` | `set_string` |
