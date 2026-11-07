# Data model: 100 wire group-read integrity

All shapes are by-value and trivially copyable; nothing here allocates.

## `group_read_status` (new, `include/fixpp/wire/offset_table.hpp`)

`enum class group_read_status : std::uint8_t { ok, alloc_failed, limit_exceeded }` (research D5).

| Value | Set when |
|---|---|
| `ok` | the read completed: a group with its instances, a group with zero instances read, a not-a-group answer, or an absent tag |
| `alloc_failed` | a `bad_alloc` was caught: instance slices, the cache row of a group with instances (research D10), the nested sub-table shell or its cache row (mode a), the sub-table's own slices (mode b), the sub-build's `out_of_memory` (mode c) |
| `limit_exceeded` | `group()` returned `wire_group_too_large` (per-instance entry cap, nesting depth; inside a sub-table this is defensive, research D5), or the nested sub-build failed with `wire_offset_table_full` (research D7) |

A read ends at its first failure, so it carries exactly one status and needs no precedence rule. `group()` allocates
nothing, and a limit failure pushes no cache row (research D10), so `limit_exceeded` is never followed by an
allocation. A sub-build reports the one error that stopped it.

## `group_slices_result`

| Field | Today | After |
|---|---|---|
| `slices` | instance slices; empty for absent, not-a-group, zero count, failure | unchanged |
| `alloc_failed` (`bool`) | `bad_alloc` while materializing slices | **replaced** by `status` |
| `status` | — | **new**, `group_read_status` |
| `is_group` | — | **new**: the read completed and the registration predicate (research D2) held for this tag in this table's context, including zero instances read |

Invariants: `status != ok ⇒ slices.empty() && !is_group`; `!is_group ⇒ slices.empty()`; `status != ok ⇒ the count tag
is present and registered`, because `group()` asks registration before anything allocates or walks (research D2).

## `nested_slices_result`

Same three fields. A failure is one of (a) no sub-table, or its cache row did not fit → `alloc_failed`; (c) the
sub-build failed with `out_of_memory` → `alloc_failed`, or with `wire_offset_table_full` → `limit_exceeded`; (b) the
sub-table's `group_slices_status().status`. When one fires, `resolve_nested_result` keeps it only for a tag that is
registered in the call's `ctx` (asked through the caller's `hooks`) **and** present in the instance slice, in that
order (research D11); otherwise the result is `{empty, ok, is_group = false}`. Without a failure, `is_group` is the
sub-table's `group_slices_status().is_group`. A sub-build that failed for any other reason keeps 073's not-failed scope:
`{empty, ok, is_group = false}`. Both exits (cache hit and final) apply the same rule.

Invariant (the one the contract's single table rests on, stated here only): `status != ok ⇒ nested_no_tag is in the
instance slice AND registered in ctx`. "In the instance slice" means anywhere in it, nested instances included (research
D11 step 2).

## `OffsetTable::group_span` (cache row, private)

| Field | After |
|---|---|
| `data`, `count`, `no_tag` | unchanged |
| `is_group` | **new**, cached with the row so a repeat read answers the same as the first. Fits the existing padding; `sizeof(group_span) <= 16` stays asserted. |

Caching rule: research D10 (a failed read leaves no row; for a group with instances the row is pushed before the slices
and popped if they fail). Today the push sits in the slice allocation's `try` and runs for every outcome of `group()`,
including `wire_group_too_large`.

## `group_view<GroupT>` (`include/fixpp/wire/group_view.hpp`)

- `alloc_failed()`: `status == group_read_status::alloc_failed`; its documented meaning is unchanged.
- `read_failed()`: **new**, `noexcept`, `status != group_read_status::ok`; the predicate typed readers check.
- Both `bool alloc_failed = false` constructors are kept unchanged (`true` → `alloc_failed`). Two constructors taking
  `group_read_status` are added with **no** default argument, so `group_view{inst, ctx}` still selects a `bool`
  overload. `MessageView::group<>()` (research D6) and the generated nested accessors (research D5) call the status
  constructors.
- Overload selection, each selecting exactly one constructor:
  - `group_view{inst, ctx}` and `{inst, ctx, true}` select a `bool` constructor;
  - `{inst, ctx, group_read_status::…}` selects a status constructor. A scoped enum converts neither to `bool` nor
    from it.
  - an integer third argument selects a `bool` constructor, as today.

## `table_view` registration (`include/fixpp/dict/table_view.hpp`)

- **New** per-tag bitset "has a context key", beside `group_bits_`, set wherever a `group_ctx_` key is created (all
  three `_ctx` setters, `add_group_required_member_ctx` included); built once with the table.
- **New** arm-1 store, keyed `(msg_type, parent_path, no_tag)` and filled from the per-message delimiter records
  (`msg_group_ctx_delims_impl`), which arm 1 reads with the `group_ctx_` keys of a hand-built table (research D2). It creates
  no `group_ctx_` key. *(Added by Gate A round 4 hand-edit (owner, 2026-11-07).)* Each key holds its occurrence's
  record (below); one store answers arm 1 and the record lookup. *(Gate A round 5 → hand-edit 3 (owner, 2026-11-07).)*
- **New** MsgType-free store of `(parent_path, no_tag)` section-group keys, each holding its record (below), filled by a new
  `table_view_builder::add_section_group(parent_path, no_tag, record)` that `as_table_view` calls from a new handle store
  (`dict_metadata_handle`, beside `header_tags_`); the loaders' header and trailer walks fill that store at every
  depth, once per (definition, enclosing path) (research D2; Gate A round 4 hand-edit (owner, 2026-11-07)). Built once with the table; no arena impact, since `table_view` is per dictionary. Section records do not set
  the "has a context key" bitset. The section arm (research D2) reads this store, not `is_header_tag` or `group_bit`.
- **New** `noexcept` accessor answering the registration predicate; its arms are stated in research D2.
- Every store and record in this section is written only while the table is built, and is read-only afterwards. Concurrent
  readers of one published view need no synchronization (research D2, "Thread safety").
- **New** per-occurrence record (research D2, "Per-occurrence records"; Gate A round 5 → hand-edit 3 (owner,
  2026-11-07)): `{delimiter, direct members}`, the shape of `group_ctx_entry` without its required members. Arm-1
  records take the delimiter from the occurrence's `GroupCtxDelim::delimiter` and the members from a capture beside
  `DelimCapture` in both loaders, written by `flush_group_ctx_delims` to a handle store parallel to
  `group_ctx_delim_pool_`. Section records come from the loaders' section walks. Built once with the table; no arena
  impact.
- **New** `noexcept` wire-side membership and delimiter accessors, called only by `dict_hooks::for_table_view`'s two
  lambdas: the `group_bit` pre-filter, then the exact `group_ctx_` key, the arm-1 record, the section record, and the
  bare store, in that order. `group_member_tags(msg_type, parent_path, no_tag)` and
  `group_first_field(msg_type, parent_path, no_tag)`, which the validator calls, are unchanged.

## `dict_hooks` (`include/fixpp/wire/dict_hooks.hpp`)

- **New** `noexcept` callback `group_registered_fn_t = bool (*)(void const*, group_context const&, std::uint16_t)`,
  filled by `for_table_view` from the accessor above. Null on `none()`. A dict-aware bundle with a null registration
  callback declines group reads as a dict-free one does.
- Grows `sizeof(dict_hooks)` and every by-value holder of it; the holders and their recipe are in research.md's census.
- The membership and delimiter callbacks keep their types; only `for_table_view`'s lambdas change (the record lookup
  above), so the per-occurrence records add nothing to `sizeof(dict_hooks)`.

## `OffsetTable::build` status

A build whose total probe work exceeds `kMaxBuildProbe` × occurrences sets `status_` to `wire_offset_table_full` and
clears `entries_` and `overlay_` (research D1). `find()` on a failed table already returns the status error.

## C-ABI getter endings

`contracts/capi-group-getters.md` is the normative table; research D4 states how each getter evaluates it.
