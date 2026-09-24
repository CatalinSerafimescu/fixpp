# Data model — 091 data-field-bytes

No persistent data. The "entities" are the builder's accumulator nodes and the generated Args
shapes. Decisions are cited as `R-n` (research.md).

## `wire::body_builder` (changed)

| Member | Change | Notes |
|---|---|---|
| ctor `body_builder(std::string_view msg_type, dict_hooks hooks = dict_hooks::none())` | **new defaulted parameter** | Existing one-argument callers compile unchanged (R-2). |
| `dict_hooks hooks_` | **new private member**, by value | Trivially copyable. The single pair lookup that `field_data`, `set_data` and commit all read (FR-009). |
| `field_data(std::uint16_t data_tag, std::span<const std::byte> value)` | **new public** | Atomic Length+Data append (R-1, R-3). |
| `field(tag, string_view)` and the other `field` overloads | unchanged | INV-2 intact (FR-007). |
| `commit(out)` | **adds the pair check** inside the INV-5 walk | R-4, FR-008. |
| `validate_group_grammar` | becomes the combined INV-5 + pair walk (a private static, which gains a `dict_hooks const&` parameter) | R-4. |

## `wire::entry_handle` (changed)

| Member | Change |
|---|---|
| `set_data(std::uint16_t data_tag, std::span<const std::byte> value)` | **new public**. The same checks as `set_string` for handle validity, then R-3. It reads the owning builder's `hooks_`. |

## Accumulator node shapes (unchanged)

`entry_node` and `group_instance` gain no field. A pair is simply two consecutive scalar
`entry_node`s: the Length (value = ASCII decimal count) and the Data (value = the verbatim octets).
Keeping them as ordinary nodes is what lets `compute_size` / `serialize_entries` stay untouched, and
lets the commit check treat pairs from `field_data` and pairs written by hand identically.

## Invariants

| ID | Statement | Enforced where |
|---|---|---|
| INV-2 (existing, unchanged) | `field`/`set_string`: no framing tag, every byte in `0x20–0x7E` | `append_string_field` |
| INV-4 (existing, extended) | Any failure leaves the container size unchanged; a failed commit leaves `out` untouched | `field_data`/`set_data` roll back **both** nodes (R-3) |
| INV-5 (existing) | Each group instance is non-empty and delimiter-first | commit walk. A Length-delimited entry passes because the Length node is appended first (R-6) |
| **INV-6 (new)** | In every container, each pair tag (per `hooks_`) forms a well-formed pair: Length immediately followed by its Data, Length a positive decimal equal to the Data's octet count, Data non-empty | commit walk, through `wire::length_data_checker` (R-4) |
| **INV-7 (new)** | `field_data`/`set_data` never append a node for a tag that `hooks_` does not name as a Data tag, and never for a framing tag on either half | refusal order, R-3 |

## Refusal → error (FR-004a)

| Condition | Error | Stage |
|---|---|---|
| handle has no owner / is not the innermost open entry (`set_data`) | `wire_invalid_field_format` | set |
| `data_tag` is a framing tag | `wire_field_value_out_of_range` | set |
| `hooks_.length_tag_for_data(data_tag) == 0` | `wire_unexpected_tag` | set |
| the derived Length tag is a framing tag (dictionary pair only) | `wire_field_value_out_of_range` | set |
| `value.empty()` | `wire_field_value_out_of_range` | set |
| arena exhaustion during either append | `wire_frame_too_large` (both nodes rolled back) | set |
| INV-6 violated in any container | `wire_invalid_field_format` | commit |
| serialized body over `kBodyCap` | `wire_frame_too_large` (existing) | commit |

## Generated Args (changed)

| Shape | Change |
|---|---|
| Coupled Length+Data member `std::optional<std::string_view> <data_accessor>` | **type unchanged** (FR-011). Emitted through `field_data`/`set_data` (R-7). |
| `std::optional<std::string_view> message_encoding` | **new**, top level only, **appended last**, only on messages that can carry an `Encoded*` field at any depth (R-8, FR-011a). Emitted first in the body as `347`. |

## Limitation ledger (FR-016, FR-008, FR-009a, FR-011a)

| Row | Action |
|---|---|
| L-067-2 | **moves** to `spec/behaviors-and-limitations-closed.md`, citing stage-one `68c8c769` + this PR |
| B-091-1 (new, behaviour) | `body_builder::commit` refuses a malformed hand-written Length/Data pair (`wire_invalid_field_format`) |
| B-091-2 (new, behaviour) | `MessageEncoding(347)` is available on generated Args but is not required when an `Encoded*` field is set |
| L-091-1 (new, limitation) | Interop: QuickFIX/C++ and QuickFIX/J find a custom pair's Length only when it is Data − 1; QuickFIX/n splits any data value other than XmlData(213) at SOH |

The row IDs are provisional; `/speckit-implement` assigns the final ones against the live file.
