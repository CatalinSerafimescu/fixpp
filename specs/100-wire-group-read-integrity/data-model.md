# Data model: 100 wire group-read integrity

All shapes are by-value and trivially copyable; nothing here allocates.

## `group_slices_result` (`include/fixpp/wire/offset_table.hpp`)

| Field | Today | After |
|---|---|---|
| `slices` | instance slices; empty for absent, not-a-group, zero count, failure | unchanged |
| `alloc_failed` | `bad_alloc` while materializing slices | the read did not complete: `bad_alloc`, or `wire_group_too_large` from `group()` (research D5) |
| `is_group` | — | **new**: `group()` succeeded for this tag in this table's context, including present-and-empty (D3) |

Invariants: `alloc_failed ⇒ slices.empty()`; `!is_group ⇒ slices.empty()`; `alloc_failed` and `is_group` are not both true.

## `nested_slices_result`

Same three fields. `alloc_failed` ORs (a) no sub-table, (b) the sub-table's `group_slices_status().alloc_failed`, (c) the sub-table's build failed with `out_of_memory` or `wire_offset_table_full` (research D7). `is_group` is the sub-table's `group_slices_status().is_group` (false when (a)).

## `OffsetTable::group_span` (cache row, private)

| Field | After |
|---|---|
| `data`, `count`, `no_tag` | unchanged |
| `is_group` | **new**, cached with the row so a repeat read answers the same as the first. Fits the existing padding; `sizeof(group_span) <= 16` stays asserted. |

A failed read caches nothing (today's behaviour), so a repeat read on an exhausted arena fails again, with the same code.

## `group_view<GroupT>` (`include/fixpp/wire/group_view.hpp`)

No new member. `alloc_failed()`'s documented meaning widens to "the read did not complete" (D5). The top-level `MessageView::group<>()` now passes the flag (D6).

## `OffsetTable::build` status

A probe-cap reach sets `status_` to `wire_offset_table_full` and clears `entries_` and `overlay_` (D1). `find()` on a failed table already returns the status error.

## C-ABI getter endings

The table in research D4 is normative; `contracts/capi-group-getters.md` restates it as the public contract.
