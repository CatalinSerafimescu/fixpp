# Research: Fail-loud on nested-read sub-table allocation failure (073, L-065-2, #184)

**Feature branch**: `073-nested-read-arena-failloud`
**Date**: 2026-07-13

This feature reopens 065 research **Decision 6 / FR-009**, which deliberately declined widening `OffsetTable::nested_group_slices` to a status-bearing result for the then-in-scope unreachable-overflow case. It carries its own Gate A per Article XVII.

## Verified seam (against source, 2026-07-13)

| Site | File:line | Role |
|------|-----------|------|
| Alloc-failure origin | `src/wire/offset_table.cpp:709-711` | `build_nested_subview` catches `std::bad_alloc` → returns `nullptr` (only nullptr source; a zero-len slice still builds a non-null empty table). |
| Primitive (7-arg) | `src/wire/offset_table.cpp:718-769` | Builds/caches the sub-table; returns `std::span<group_slice const>` — **empty** both for absent/count-0 AND alloc-fail. |
| Primitive (2-arg convenience) | `src/wire/offset_table.cpp:776-781` | Forwards to the 7-arg using this table's own dict/fn/token. |
| **Cache-hit early exit** | `src/wire/offset_table.cpp:748-750` | On a warm `(slice,no_tag)` hit, `row.table != nullptr ? group_slices : {}` — a **cached null row is a cached failed build** (:737-741 FIRST-wins). |
| Final exit | `src/wire/offset_table.cpp:768` | `table != nullptr ? group_slices : {}`. |
| C-ABI consumer | `src/capi/message_read.cpp:489-499` | `slices.empty()` → presence probe: absent → `TAG_NOT_FOUND`, present-but-empty → `OK`/nc=0. **Alloc-fail lands here as OK/nc=0 = silent truncation.** |
| Typed consumer | `tools/codegen/fixpp-codegen/emit_messages.cpp:256-286` | The **emitter** emits the generated nested accessor that calls the 7-arg overload and returns `group_view<G_c>{nested, child_ctx}`. `group_view::operator[]` does NOT call the primitive — the generated flyweight does. |

## Decisions

### D1 — Status-bearing RETURN type, not an out-parameter

**Decision**: Widen both `nested_group_slices` overloads to return a small trivially-copyable result struct carrying the slice span **and** an allocation-failure flag. Do **not** add an optional `bool* alloc_failed_out = nullptr` out-parameter.

**Rationale**: An out-parameter that defaults to `nullptr` leaves the *default* behavior of the primitive as "empty span, status silently dropped" — i.e. the exact silent-truncation this feature exists to eliminate remains the easy/default path, and any future caller who omits the out-param re-inherits the bug. That directly violates the repo's default-real / fail-closed stance (CLAUDE.md silent-loss preamble; [[feedback_fixed_buffer_build_failure_silent_success]]). A return struct makes the status **un-ignorable by construction** and is the shape that survives a hostile Gate A on "we fixed silent truncation with an opt-in flag that defaults to silent".

**Cost / alternatives**: ~20 test call sites currently bind `auto slices = ...nested_group_slices(...)` and use `slices.empty()/.size()/[i]`; each becomes `result.slices.…` — mechanical, greppable, one-pass, and any missed edit fails to compile (loud). Rejected: out-param (default-silent, above); throwing (both overloads + `build_nested_subview` are `noexcept` → a throw terminates — spec FR-004).

### D2 — Set the failure status at EVERY empty-returning exit (multi-exit gate)

**Decision**: The result's `alloc_failed` flag is set true when a build was attempted/served-from-cache and the resolved sub-table pointer is null, at **both** empty-returning exits:
- the cache-hit early return (`offset_table.cpp:748-750`) — a cached `row.table == nullptr` is a cached failed build (FIRST-wins, :737-741), and
- the final return (`:768`).

Formula: `alloc_failed = (slice_data != nullptr) && (resolved table == nullptr)`.

**Disjointness (spec FR-007)**: `slice_data == nullptr` (absent, `:722-724`) → `alloc_failed = false`; a **non-null** table whose `group_slices()` returns empty (legit count-0 / zero-len slice) → `alloc_failed = false`. Only a null table after a non-null `slice_data` is a failure.

**Rationale**: Instrumenting only the final return reintroduces the silent truncation on the **second** read of an exhausted group (served from the cached null row) — which the spec's own edge case forbids ("Repeated read after a failed build … MUST NOT serve a cached silent-empty result"). Same multi-exit-gate hazard as [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]. A **repeated-read witness** (read twice, assert failure both times) is the mutation-discriminating test for the cache exit.

### D3 — Result type shape

**Decision**: A new trivially-copyable struct in `fixpp::wire`, e.g.:
```cpp
struct nested_slices_result {
    std::span<group_slice const> slices{};
    bool alloc_failed = false;
};
```
Placed in `offset_table.hpp` (where the overloads are declared). Trivially copyable (span + bool) — no allocation, consistent with the zero-alloc wire-read discipline. Final name settled at implementation.

### D4 — Typed surfacing via a `group_view` status bit

**Decision**: `group_view<GroupT>` gains a private `bool alloc_failed_ = false` and a public `[[nodiscard]] bool alloc_failed() const noexcept` accessor. Both existing ctors gain a trailing defaulted `bool alloc_failed = false` parameter (back-compat: `MessageView::group<>()` and the dict-free ctor keep compiling). The **emitter** (`emit_messages.cpp`) threads `result.alloc_failed` into the ctor and `result.slices` into `instances`.

**Rationale**: The generated nested accessor's *only* observable is the returned `group_view` — there is no error channel on the typed path, so the status must live on `group_view` (FR-004: value/status, no throw). This is a public-surface addition to the wire read API; the shape oracle `specs/004-wire-codec/contracts/group_view.hpp` is updated to match (Article XVI consistency). `group_view` stays trivially copyable (adds a `bool`).

Note: `group_view` changes under **either** D1 choice, so it is not a discriminator for D1.

### D5 — C-ABI error = existing `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`

**Decision** (spec FR-008, clarified): the C-ABI nested read returns the existing `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (101) on `result.alloc_failed`, checked **before** the presence probe at `message_read.cpp:492`. The sibling C++ error `wire_offset_table_full` already maps to this code (`src/capi/error.cpp:70-74`); a parse-arena exhaustion building a nested offset sub-table is the same wire resource-limit family. No new C-ABI macro, no `core::error` enumerator, no `error_message()`/`test_020` churn (the feature-072 constraint does not apply — this return is a direct `fixpp_error_t`, not a mapped `core::error`).

### D6 — Faithful, deterministic exhaustion witness

**Decision**: Drive exhaustion by injecting a **tiny-capacity `std::pmr::monotonic_buffer_resource` over `std::pmr::null_memory_resource()`** as the parse arena — sized so a genuinely present nested group's sub-`OffsetTable` allocation (`build_nested_subview`'s `mr->allocate(sizeof(OffsetTable), …)`) throws `bad_alloc`. This reproduces the exact failure the fixed 16 KiB null-upstream arena exhibits at its cap. **Not** a hand-built 16 KiB-exhausting real message; **not** a post-hoc "alloc failed" flag ([[feedback_fault_injection_posthoc_flag_unfaithful]]).

Two symmetric witnesses (FR-005), each mutation-proven RED pre-fix, asserting the distinct signal **directly** (not an `nc==0` proxy, SC-002):
1. **C-ABI**: nested read of a present group under the tiny arena → `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (not `OK`/nc=0).
2. **Typed**: generated nested accessor under the tiny arena → `group_view.alloc_failed() == true`.
3. **Repeated-read** (D2 discriminator): read the same exhausted group twice → failure signalled **both** times (kills the cache-exit miss).
4. **Non-failure controls** (SC-003): absent group and count-0 group under an ample arena → **no** failure signal on either path.

### D7 — Golden regeneration (codegen)

**Decision**: The generated nested accessors change (they now bind a `nested_slices_result` and thread `.alloc_failed`/`.slices`). Regenerate the checked-in read goldens (`specs/003-dictionary-codegen/contracts/golden/v{44,50sp2,42,vt11}_Messages.golden.hpp`) and run the **full** ctest, not narrow targets — the `codegen_determinism_test` compares against these goldens and a stale golden hangs CI ([[feedback_codegen_golden_exists_narrow_verify_misses_it]], [[project_codegen_emitter_staleness]]).

### D8 — Scope

Fail-loud only (spec FR-009). The fixed inbound parse arena stays bounded / null-upstream — no sizing or growability change. No new arena, no new read path, no outbound/write change. L-065-1 context-path arithmetic is already fixed (072) and untouched.

## Blast radius (files)

| File | Change |
|------|--------|
| `include/fixpp/wire/offset_table.hpp` | Add `nested_slices_result`; change both overload return types. |
| `src/wire/offset_table.cpp` | Return the struct; set `alloc_failed` at the cache-hit exit + final exit (D2). |
| `include/fixpp/wire/group_view.hpp` | Add `alloc_failed_` member + accessor + defaulted ctor param (D4). |
| `tools/codegen/fixpp-codegen/emit_messages.cpp` | Emit `.slices`/`.alloc_failed` threading in the nested accessor. |
| `src/capi/message_read.cpp` | Consume `result.alloc_failed` → `WIRE_LIMIT_EXCEEDED` before the presence probe (D5). |
| ~20 test call sites (`tests/wire/`, `tests/fuzz/`, `tests/capi/`, `tests/alloc_guard/`) | `slices.…` → `result.slices.…` (mechanical). |
| `specs/003-dictionary-codegen/contracts/golden/*_Messages.golden.hpp` | Regenerate (D7). |
| `specs/004-wire-codec/contracts/group_view.hpp` | Shape-oracle: add the `alloc_failed()` accessor. |
| New: `tests/capi/*` + `tests/wire/*` witnesses | D6. |

## Constitution notes

- **Article X (ABI, C-ABI frozen 1.5.0)**: No C-ABI symbol/struct/signature/macro change — the nested read returns an **existing** error code (`WIRE_LIMIT_EXCEEDED`) in a formerly-silent edge. This is a behavioral bug-fix (silent-loss → fail-loud) on an extreme, normal-traffic-unreachable path, not an ABI break. The typed `group_view` change is C++ (link-ABI is C++-template, header-only for the wire read layer). Flag for Gate A confirmation.
- **Article VII (TDD)**: witnesses authored RED-first, mutation-proven (D6).
- **Article IX**: full sanitizer/coverage matrix over the touched diff (the arena/`bad_alloc` path is ASan/UBSan-relevant).
- **Article XV (banned patterns)**: fail-closed on the failure edge (no silent success); no `noexcept`-boundary throw.
