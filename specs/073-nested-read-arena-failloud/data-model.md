# Data Model: 073 nested-read arena fail-loud

**Feature branch**: `073-nested-read-arena-failloud` | **Date**: 2026-07-13

Two new/changed data shapes. Both stay trivially copyable (zero-alloc wire-read discipline).

## `nested_slices_result` (NEW — `fixpp::wire`, `offset_table.hpp`)

The status-bearing return of both `OffsetTable::nested_group_slices` overloads (D1, D3).

| Field | Type | Meaning |
|-------|------|---------|
| `slices` | `std::span<group_slice const>` | The nested group's occurrence slices (borrowed from the parent per-message arena). Empty for absent / count-0 / failed-build. |
| `alloc_failed` | `bool` (default `false`) | `true` **iff** a sub-view allocation failed via **any of three** sub-modes (D2): (a) the sub-`OffsetTable` shell alloc failed (`build_nested_subview → nullptr`), (b) the sub-table built non-null but its own `group_slices()` slice materialization caught `bad_alloc`, OR (c) the sub-table built non-null but its ctor's own `build()` degraded specifically to `out_of_memory` (`build_status().error() == out_of_memory`; found at `/speckit-implement`). Distinguishes "empty because alloc failed" from "empty because absent/count-0". |

**Invariants**
- `alloc_failed == true` ⇒ `slices.empty()` (a failed build or a caught `bad_alloc` in slice materialization yields no slices).
- `alloc_failed == false` with `slices.empty()` ⇒ legitimately absent or count-0 (FR-007 disjointness).
- Trivially copyable (`static_assert(std::is_trivially_copyable_v<nested_slices_result>)`) — span + bool, no allocation.

**Set-condition (both empty-returning exits, D2 — three origins)**
`alloc_failed = (resolved sub-table pointer == nullptr) || (table->build_status().error() == out_of_memory) || table->group_slices_status(nested_no_tag).alloc_failed`, guarded by `slice_data != nullptr`, evaluated at:
- the cache-hit early return (`offset_table.cpp:769-786`) — a cached null `row.table` is a cached failed build (mode a); a cached non-null `row.table` with `build_status() == out_of_memory` is mode (c); a cached non-null `row.table` whose `group_slices_status()` re-throws is mode (b);
- the final return (`offset_table.cpp:804-813`).
The `slice_data == nullptr` guard (`:722`) returns `{ {}, false }` (absent). A **non-null, `build_status()`-ok** table whose `group_slices_status()` returns a count-0 span **without** throwing returns `{ empty, false }` (count-0). The mode-(c) term is scoped to `out_of_memory` only — a table degraded for a malformed-data reason (`offset_table_full`, `invalid_field_format`) is NOT arena exhaustion and stays `alloc_failed = false`.

**Internal `group_slices_result` (NEW — `fixpp::wire`, `offset_table.hpp`; not a public return)**
`group_slices_status(std::uint16_t no_tag) -> group_slices_result { std::span<group_slice const> slices; bool alloc_failed; }` is the status-bearing form of `OffsetTable::group_slices`: its `catch (std::bad_alloc)` at `offset_table.cpp:674-675` sets `alloc_failed = true`; a warm-cache hit or a genuine count-0 return sets `alloc_failed = false`. The **public `group_slices(no_tag)` stays a one-line span wrapper** returning `.slices` — every top-level caller is unchanged (FR-002 originate-at-failure; no ABI/behaviour change). Only `nested_group_slices` consumes the status (mode (b)).

## `group_view<GroupT>` status extension (CHANGED — `group_view.hpp`)

Adds the typed-path observable (D4). No other member semantics change.

| Field / member | Type | Meaning |
|----------------|------|---------|
| `alloc_failed_` (private) | `bool` (default `false`) | Set from `nested_slices_result::alloc_failed` at construction by the codegen-emitted nested accessor. |
| `alloc_failed()` (public) | `[[nodiscard]] bool () const noexcept` | Typed-caller query: `true` ⇒ this nested group's sub-view could not be allocated (present-but-truncated), distinct from an empty group (`size() == 0 && !alloc_failed()`). |

**Ctor change**: both existing `group_view` ctors gain a trailing `bool alloc_failed = false` parameter (back-compat: `MessageView::group<>()` and the dict-free ctor keep compiling unchanged). Stays trivially copyable (adds a `bool`).

**Relationship to `size()`**
| `alloc_failed()` | `size()` | Meaning |
|------------------|----------|---------|
| `false` | `> 0` | Present, read OK. |
| `false` | `0` | Legitimately absent / count-0. |
| `true` | `0` | Present but sub-view allocation failed (fail-loud). |

## Consumers

- **C-ABI** (`message_read.cpp`): binds the result; `result.alloc_failed` ⇒ return `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` **before** the presence probe; otherwise unchanged (`result.slices` drives the existing absent/count-0/present logic).
- **Typed** (emitter `emit_messages.cpp`): binds the result; passes `result.slices` as instances and `result.alloc_failed` into the `group_view` ctor.
- **Tests** (~20 sites): `auto r = ...nested_group_slices(...)` then `r.slices.empty()/.size()/[i]`.
