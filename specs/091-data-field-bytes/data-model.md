# Data model — 091 data-field-bytes

No persistent data. The "entities" are the builder's accumulator nodes and the generated Args
shapes. Decisions are cited as `R-n` (research.md).

## `wire::body_builder` (changed)

| Member | Change | Notes |
|---|---|---|
| ctor `body_builder(std::string_view msg_type, dict_hooks hooks = dict_hooks::none())` | **new defaulted parameter** | Existing one-argument callers compile unchanged (R-2). Precondition: the dictionary behind `hooks` outlives the builder (C-1). |
| `dict_hooks hooks_` | **new private member**, by value | Trivially copyable. Read by the commit check only (FR-009); the set-time operation uses the standard table. |
| `field_data(std::uint16_t data_tag, std::span<const std::byte> value)` | **new public** | Atomic Length+Data append, standard pairs only (R-1, R-2, R-3). |
| `field(tag, string_view)` and the other `field` overloads | unchanged | INV-2 intact (FR-007). |
| `commit(out)` | **adds the pair check** inside the INV-5 walk | R-4, FR-008. |
| `validate_group_grammar` | becomes the combined INV-5 + pair walk (a private static, which gains a `dict_hooks const&` parameter) | R-4. |

## `wire::entry_handle` (changed)

| Member | Change |
|---|---|
| `set_data(std::uint16_t data_tag, std::span<const std::byte> value)` | **new public**. The same checks as `set_string` for handle validity, then R-3 (standard table only). Preferred implementation: `field_data` and `set_data` forward to one shared static `append_data_field(into, data_tag, value)`, in the file's `append_*_field` style (R-3). The two-surface tests (C-1.4, C-1.5 nested twin, C-1.9) are required either way. |

## Accumulator node shapes (unchanged)

`entry_node` and `group_instance` gain no field. A pair is simply two consecutive scalar
`entry_node`s: the Length (value = ASCII decimal count) and the Data (value = the verbatim octets).
Keeping them as ordinary nodes is what lets `compute_size` / `serialize_entries` stay untouched, and
lets the commit check treat pairs from `field_data` and pairs written by hand identically.

## Invariants

| ID | Statement | Enforced where |
|---|---|---|
| INV-2 (existing, unchanged) | `field`/`set_string`: no framing tag, every byte in `0x20–0x7E` | `append_string_field` |
| INV-4 (existing, extended) | Any failed append leaves the container at its pre-call size, so a later commit serializes exactly what it would have without the call; a failed commit leaves `out` and the accumulator untouched. Arena capacity consumed by a failed call is not reclaimed (as for every existing append) | `field_data`/`set_data` roll back **both** nodes (R-3); observed by commit-and-byte-compare (C-1) |
| INV-5 (existing) | Each group instance is non-empty and delimiter-first | commit walk. A Length-delimited entry passes because the Length node is appended first (R-6) |
| **INV-6 (new)** | In every container, each pair tag (per `hooks_`: standard pairs, plus the dictionary's own pairs when the hooks carry one) forms a well-formed pair: Length immediately followed by its Data, Length a positive decimal equal to the Data's octet count, Data non-empty | commit walk, through `wire::length_data_checker` (R-4) |
| **INV-7 (new)** | `field_data`/`set_data` never append a node for a tag that the **standard table** does not name as a Data tag (whatever `hooks_` holds), and never for a framing tag on either half | refusal order, R-3; the Length half by a `static_assert` over the standard table |
| **INV-8 (new)** | Set-time pairs ⊆ commit-time pairs, so nothing the set-time operation appends is refused at commit | every `dict_hooks` answers standard tags from the standard table first (`dict_hooks.hpp`) |

## Refusal → error (FR-004a)

| Condition | Error | Stage |
|---|---|---|
| handle has no owner / is not the innermost open entry (`set_data`; checked before any resolution, C-1.4b) | `wire_invalid_field_format` | set |
| `data_tag` is a framing tag | `wire_field_value_out_of_range` | set |
| `dict_hooks::none().length_tag_for_data(data_tag) == 0` (includes a dictionary-only Data tag) | `wire_unexpected_tag` | set |
| `value.empty()` | `wire_field_value_out_of_range` | set |
| arena exhaustion during either append | `wire_frame_too_large` (both nodes rolled back) | set |
| INV-6 violated in any container | `wire_invalid_field_format` | commit |
| serialized body over `kBodyCap` | `wire_frame_too_large` (existing) | commit |

## Generated Args (changed)

| Shape | Change |
|---|---|
| Coupled Length+Data member `std::optional<std::string_view> <data_accessor>` | **type unchanged** (FR-011). Emitted through `field_data`/`set_data` (R-7). |
| v50sp2 caller-set Length member `std::optional<std::int64_t>` of the five pairs R-11 newly couples | **deleted, no alias** (FR-011 carve-out, owner-ratified). Its Data member becomes the coupled member. Owners listed (dated) in C-2.1. |
| `std::optional<std::string_view> message_encoding` | **new**, top level only, **appended last**, only on messages that can carry an `Encoded*` field at any depth (R-8, FR-011a). Emitted first in the body as `347`. |

## Limitation ledger (FR-016, FR-008, FR-009, FR-009a, FR-011, FR-011a, FR-017, FR-019)

| Row | Action |
|---|---|
| L-067-2 | **moves** to `spec/behaviors-and-limitations-closed.md`, citing stage-one `68c8c769` + this PR |
| "L-067-2 is unchanged … (fixpp#418)" bullet in the live #426/#428 limitations block | **deleted** (the gap it restates is closed) |
| `tests/interop/conversation/support/conv_wire.hpp` hand-built-frame comment; `tests/interop/conversation/conv_cell_test.cpp` B-05 comment | reworded: B-05 stays on its hook because moving it needs a counterparty republish (spec Assumptions), not because #418 is open |
| `include/fixpp/wire/length_data_check.hpp` header comment; `brain/components/wire.md` Length+Data line | "#418 is meant to be / must reuse" → past tense (the builder is the checker's second caller) |
| W-008 (`spec/feature-catalogue.md`) | evidence column gains this feature |
| B-091-1 (new, behaviour) | `body_builder::commit` refuses a malformed hand-written Length/Data pair (`wire_invalid_field_format`) |
| B-091-2 (new, behaviour) | `MessageEncoding(347)` is available on generated Args but is not required when an `Encoded*` field is set |
| B-091-3 (new, behaviour) | C++ set-time vs C-ABI divergences: `field_data`/`set_data` refuse a dictionary-only Data tag that `fixpp_msg_set_data` accepts (follow-up *verifiable session binding for dictionary pairs, option (b)*, fixpp#505); a repeated call appends where the C-ABI upserts |
| B-091-4 (new, behaviour, source break; **BREAKING — C-ABI 1.9**, `[const §X.7]`, FR-019) | FIX 5.0 SP2: the QuickFIX XML loader now pairs 2494→2493, 2815→2814, 43109→42684, 43110→42486, 43111→42982, so `Dictionary`/`table_view`/`FieldRef` report them and the generated v50sp2 builders couple them; the caller-set Length members are deleted — a designated-initializer or member-access caller gets a compile error, a positional aggregate-initializer caller may silently shift into the next member. **User-loaded dictionaries:** a non-standard pair declared adjacently only inside a component or group becomes a dictionary pair, so `table_view::has_nonstandard_pair()` can flip, inbound scanners read its Data by count, `fixpp_msg_set_data` accepts it, and a malformed hand-written pair of that kind, which committed before, is now refused at commit through both the C-ABI and `body_builder`. Two effects are **BREAKING (1.9)**, each a success turned into a failure: through the C-ABI, `fixpp_msg_commit` returned `FIXPP_ERR_OK` for that malformed pair and now returns `FIXPP_ERR_WIRE_CONFORMANCE`; and inbound scanners now read such a Data by count, so an inbound message carrying a malformed pair of that kind, which parsed before as two plain fields, is now refused. The **additive** widenings that ride with them, each a failure turned into a success: `fixpp_msg_set_data`/`fixpp_entry_set_data` now accept such a pair; `fixpp_msg_set_string`/`fixpp_entry_set_string` no longer refuse an SOH-bearing value for its Data tag; and a correctly Length-prefixed, SOH-bearing Data of that pair, written through any setter, which failed commit with `FIXPP_ERR_WIRE_CONFORMANCE` before, now commits. For the shipped dictionaries inbound parsing is unaffected (C-2.5a) |
| `brain/components/c-api.md` | gains a C-ABI 1.9 entry beside the C-ABI 1.8 one (FR-019), naming the BREAKING commit refusal and the inbound-scanner change |
| CA-011 (`spec/feature-catalogue.md`, the `fixpp_dict_load_from_xml` row) | gains a C-ABI 1.9 BREAKING note in the style of its 1.8 one: a custom pair declared only inside a component or group is now a dictionary pair (FR-017, FR-019) |
| L-426-3 (existing) | **extended** with the send-side consequence (FR-009a); no L-091-1 near-copy |

The row IDs are provisional; `/speckit-implement` assigns the final ones against the live file.
