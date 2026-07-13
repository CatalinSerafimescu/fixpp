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
- `alloc_failed == false, slices == <occurrences>` — present group, sub-view built (or served from a warm non-null cache row).
- `alloc_failed == false, slices.empty()` — group absent (`slice_data == nullptr`) OR count-0 / zero-len (non-null table, empty).
- `alloc_failed == true, slices.empty()` — present group whose sub-`OffsetTable` allocation failed (arena exhaustion), at first read AND every subsequent read (cache-null-row served → still `alloc_failed`).
- `noexcept` preserved (no throw across the boundary).

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
