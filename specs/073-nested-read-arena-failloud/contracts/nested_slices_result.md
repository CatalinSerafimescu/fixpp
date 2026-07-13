# Contract: widened nested-read primitive + typed status accessor (073)

## Primitive — `OffsetTable::nested_group_slices` (both overloads)

**Before**
```cpp
[[nodiscard]] std::span<group_slice const> nested_group_slices(
    std::byte const* slice_data, std::size_t slice_len, std::uint16_t nested_no_tag,
    void const* opaque_dict, group_member_fn_t group_member_fn,
    detail::generation_token gen, group_context const& ctx) const noexcept;

[[nodiscard]] std::span<group_slice const> nested_group_slices(
    std::byte const* slice_data, std::size_t slice_len, std::uint16_t nested_no_tag,
    group_context const& ctx) const noexcept;               // convenience
```

**After**
```cpp
struct nested_slices_result {           // trivially copyable
    std::span<group_slice const> slices{};
    bool alloc_failed = false;
};

[[nodiscard]] nested_slices_result nested_group_slices( /* same 7 args */ ) const noexcept;
[[nodiscard]] nested_slices_result nested_group_slices( /* same 2 args */ ) const noexcept;
```

**Postconditions**
- `alloc_failed == false, slices == <occurrences>` — present group, sub-view built non-null AND its `group_slices_status()` returned occurrences without throwing (or served from a warm non-null cache row with a materialized span).
- `alloc_failed == false, slices.empty()` — group absent (`slice_data == nullptr`) OR count-0 / zero-len (non-null, `build_status()`-ok table, `group_slices_status()` returned empty **without** throwing).
- `alloc_failed == true, slices.empty()` — present group whose sub-view allocation failed via **any of three** sub-modes: (a) the sub-`OffsetTable` shell alloc failed (`build_nested_subview → nullptr`), (b) the sub-table built non-null but its own `group_slices()` slice materialization caught `bad_alloc` (`offset_table.cpp:674-675`), OR (c) the sub-table built non-null but its ctor's own `build()` degraded to `out_of_memory` (`offset_table.cpp:366-370`; `build_status().error() == out_of_memory`) — found at `/speckit-implement`. Holds at first read AND every subsequent read — mode (a) serves the cached null row; mode (b) re-materializes from the cached non-null row and re-throws; mode (c) rides the persistent `build_status() == out_of_memory` on the cached count-0 row (no re-throw).
- `noexcept` preserved (no throw across the boundary).

## Internal status split — `OffsetTable::group_slices` (D2 mode (b))

To make mode (b) originate at the point of failure (FR-002), `group_slices` is split into a status-bearing internal form and a public span wrapper — the public signature is **unchanged**, so every top-level caller keeps compiling and behaving identically:

```cpp
struct group_slices_result {            // internal, trivially copyable
    std::span<group_slice const> slices{};
    bool alloc_failed = false;
};

// internal — sets alloc_failed=true in the catch(bad_alloc) at offset_table.cpp:674-675
[[nodiscard]] group_slices_result group_slices_status(std::uint16_t no_tag) const noexcept;

// public — UNCHANGED signature; one-line wrapper: return group_slices_status(no_tag).slices;
[[nodiscard]] std::span<group_slice const> group_slices(std::uint16_t no_tag) const noexcept;
```

`nested_group_slices` (the only mode-(b) consumer) calls `group_slices_status(nested_no_tag)` at both empty-returning exits and OR-s its `alloc_failed` with the null-table build-fail predicate. Top-level group getters (C-ABI + `MessageView::group<>()`) keep calling the public span `group_slices()` and remain silent on exhaustion (L-073-1, deferred).

## Typed accessor — `group_view<GroupT>`

**Added**
```cpp
[[nodiscard]] bool alloc_failed() const noexcept;   // true ⇒ present-but-sub-view-alloc-failed
```
Ctors gain a trailing `bool alloc_failed = false` (defaulted → source-compatible for existing callers).

**Generated nested accessor (emitter output, shape)**
```cpp
[[nodiscard]] inline group_view<G_c> acc() const noexcept [[clang::lifetimebound]] {
    if (ctx_.parent_cache_owner == nullptr) return {};
    auto const r = ctx_.parent_cache_owner->nested_group_slices(
        ctx_.outer_occurrence_id, ctx_.span.size(), c,
        ctx_.opaque_dict, ctx_.group_member_fn, ctx_.gen, ctx_.group_ctx);
    entry_context child_ctx = ctx_;
    child_ctx.group_ctx = ctx_.group_ctx.pushed(c);
    return group_view<G_c>{r.slices, child_ctx, r.alloc_failed};
}
```

## C-ABI consumer — `fixpp_group_get_nested_group` (`message_read.cpp`)

**Added arm, before the existing presence probe**
```cpp
auto r = ...nested_group_slices(sl->data, sl->len, nested_tag, parent_grp->group_ctx);
if (r.alloc_failed) return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;   // fail-loud (D5)
auto slices = r.slices;
if (slices.empty()) { /* unchanged: absent → TAG_NOT_FOUND; present-count-0 → OK/nc=0 */ }
```
No C-ABI symbol/struct/signature/macro change — `WIRE_LIMIT_EXCEEDED` already exists.

## Shape-oracle sync

`specs/004-wire-codec/contracts/group_view.hpp` gains the `alloc_failed()` accessor so the oracle matches the shipped `group_view` (Article XVI).
