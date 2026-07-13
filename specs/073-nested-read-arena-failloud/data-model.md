# Data Model: 073 nested-read arena fail-loud

**Feature branch**: `073-nested-read-arena-failloud` | **Date**: 2026-07-13

Two new/changed data shapes. Both stay trivially copyable (zero-alloc wire-read discipline).

## `nested_slices_result` (NEW — `fixpp::wire`, `offset_table.hpp`)

The status-bearing return of both `OffsetTable::nested_group_slices` overloads (D1, D3).

| Field | Type | Meaning |
|-------|------|---------|
| `slices` | `std::span<group_slice const>` | The nested group's occurrence slices (borrowed from the parent per-message arena). Empty for absent / count-0 / failed-build. |
| `alloc_failed` | `bool` (default `false`) | `true` **iff** a sub-`OffsetTable` build was needed but its allocation failed (D2). Distinguishes "empty because alloc failed" from "empty because absent/count-0". |

**Invariants**
- `alloc_failed == true` ⇒ `slices.empty()` (a failed build yields no slices).
- `alloc_failed == false` with `slices.empty()` ⇒ legitimately absent or count-0 (FR-007 disjointness).
- Trivially copyable (`static_assert(std::is_trivially_copyable_v<nested_slices_result>)`) — span + bool, no allocation.

**Set-condition (both empty-returning exits, D2)**
`alloc_failed = (slice_data != nullptr) && (resolved sub-table pointer == nullptr)`, evaluated at:
- the cache-hit early return (`offset_table.cpp:748-750`) — a cached null `row.table` is a cached failed build;
- the final return (`offset_table.cpp:768`).
The `slice_data == nullptr` guard (`:722`) returns `{ {}, false }` (absent). A non-null table with an empty `group_slices()` returns `{ empty, false }` (count-0).

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
