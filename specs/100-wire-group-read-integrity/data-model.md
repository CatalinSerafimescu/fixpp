# Data model: 100 wire group-read integrity

All shapes are by-value and trivially copyable; nothing here allocates.

## `group_read_status` (new, `include/fixpp/wire/offset_table.hpp`)

`enum class group_read_status : std::uint8_t { ok, alloc_failed, limit_exceeded }` (research D5).

| Value | Set when |
|---|---|
| `ok` | the read completed: a group with its instances, a group with zero instances read, a not-a-group answer, or an absent tag |
| `alloc_failed` | a `bad_alloc` was caught: instance slices, the nested sub-table shell (mode a), the sub-table's own slices (mode b), the sub-build's `out_of_memory` (mode c) |
| `limit_exceeded` | `group()` returned `wire_group_too_large` (per-instance entry cap, nesting depth), or the nested sub-build failed with `wire_offset_table_full` (research D7) |

## `group_slices_result`

| Field | Today | After |
|---|---|---|
| `slices` | instance slices; empty for absent, not-a-group, zero count, failure | unchanged |
| `alloc_failed` (`bool`) | `bad_alloc` while materializing slices | **replaced** by `status` |
| `status` | — | **new**, `group_read_status` |
| `is_group` | — | **new**: the registration predicate held for this tag in this table's context (research D2), including zero instances read |

Invariants: `status != ok ⇒ slices.empty() && !is_group`; `!is_group ⇒ slices.empty()`.

## `nested_slices_result`

Same three fields. `status` is the first failure among (a) no sub-table → `alloc_failed`, (c) the sub-build failed with
`out_of_memory` → `alloc_failed` or with `wire_offset_table_full` → `limit_exceeded`, (b) the sub-table's
`group_slices_status().status`; but only when the instance slice carries `nested_no_tag` (research D11). When it does not,
the result is `{empty, ok, is_group = false}` whatever failed. Otherwise `is_group` is the sub-table's
`group_slices_status().is_group`. A sub-build that failed for any other reason keeps 073's not-failed scope:
`{empty, ok, is_group = false}`.

Invariant (the one the contract's single table rests on): `status != ok ⇒ nested_no_tag is in the instance slice`.

## `OffsetTable::group_span` (cache row, private)

| Field | After |
|---|---|
| `data`, `count`, `no_tag` | unchanged |
| `is_group` | **new**, cached with the row so a repeat read answers the same as the first. Fits the existing padding; `sizeof(group_span) <= 16` stays asserted. |

Caching rule (research D10): a row is pushed only for a completed read (`status == ok`): a registered group with its
slices materialized (including zero instances), or a not-a-group / absent answer. A failed read pushes nothing, so a
repeat read recomputes and fails with the same status. The push has its own `try`; if it throws, the computed result is
returned uncached and unchanged. (Today the push sits in the slice allocation's `try` and runs for every outcome of
`group()`, including `wire_group_too_large`.)

## `group_view<GroupT>` (`include/fixpp/wire/group_view.hpp`)

- `alloc_failed()`: `status == group_read_status::alloc_failed`; its documented meaning is unchanged.
- `read_failed()`: **new**, `status != group_read_status::ok`; the predicate typed readers check.
- Both constructors' trailing parameter changes from `bool alloc_failed = false` to
  `group_read_status status = group_read_status::ok`. Callers: `MessageView::group<>()` (research D6) and the generated
  nested accessors (research D5).

## `table_view` registration (`include/fixpp/dict/table_view.hpp`)

- **New** per-tag bitset "has a context record", beside `group_bits_`, set only by `set_group_first_ctx` and
  `add_group_member_ctx`; built once with the table.
- **New** `noexcept` accessor answering the registration predicate (research D2): a context record for
  `(msg_type, parent_path, no_tag)`, or no context record for `no_tag` anywhere and `group_bit(no_tag)` set.

## `dict_hooks` (`include/fixpp/wire/dict_hooks.hpp`)

- **New** `noexcept` callback `group_registered_fn_t = bool (*)(void const*, group_context const&, std::uint16_t)`,
  filled by `for_table_view` from the accessor above. Null on `none()`. A dict-aware bundle with a null registration
  callback declines group reads as a dict-free one does.
- Grows `sizeof(dict_hooks)` and every by-value holder (`OffsetTable`, `MessageView`, `entry_context`,
  `field_iterator`); census in research.md.

## `OffsetTable::build` status

A build whose total probe work exceeds `kMaxBuildProbe` × occurrences sets `status_` to `wire_offset_table_full` and
clears `entries_` and `overlay_` (research D1). `find()` on a failed table already returns the status error.

## C-ABI getter endings

`contracts/capi-group-getters.md` is the normative table; research D4 states how each getter evaluates it.
