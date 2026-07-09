# Data Model: Membership-aware C-ABI nested repeating-group read (065)

No new persisted or wire data. This feature adds one field to an existing internal in-memory cursor and reuses existing extent/membership entities. Entities below are the ones the fix reads, threads, or extends.

## Entity: nested-descent context (threaded, extended)

`fixpp::wire::group_context` (`include/fixpp/wire/group_view.hpp:41`) — already exists (063). Trivially copyable:
- `msg_type: string_view` — aliases the message wire buffer (one-parse lifetime).
- `parent_path: array<uint16_t,16>` — bounded parent-no_tag chain.
- `depth: uint8_t` — valid prefix length.
- `pushed(no_tag) -> group_context` — returns a copy with `no_tag` appended (clamps at K=16).

**Meaning for this feature**: the membership context under which a group's members are registered. A group `G` reached along path `[O₁,…,Oₙ]` (its enclosing groups) resolves its members under `{msg_type, parent_path=[O₁,…,Oₙ]}`. The nested-descent call to `nested_group_slices` for a child no_tag `C` passes the *parent entry's own* context (= container path pushed with the parent's own no_tag), matching the typed path's `entry_context::group_ctx`.

## Entity: C-ABI group cursor (extended, internal)

`struct fixpp_group` (`src/capi/capi_internal.hpp:291`) — internal, opaque to consumers (public header forward-declares only). Existing fields:
- `slices: span<group_slice const>` — the group's instance slices (borrowed from the parent OffsetTable arena).
- `parent_view: const MessageView<Index>*` — owning parsed view (source of the dictionary-bearing `OffsetTable`).
- `arena: memory_resource*` — scratch (unused for reads; stays as-is).

**Added field**:
- `group_ctx: group_context` — the nested-descent context for *this* cursor = the context of an entry of this group (container path pushed with this group's own no_tag). Default `{}` (msg_type empty, depth 0) for any cursor minted without a context (degrades safely, research Decision 3).

**Invariant (context propagation)**:
- Top-level cursor (`fixpp_msg_get_group(group_tag)`): `group_ctx = parent_view->offsets().stored_group_context().pushed(group_tag)` = `{msg_type, [group_tag]}`.
- Nested cursor (`fixpp_group_get_nested_group(parent, i, nested_tag)`): descent passes `parent->group_ctx` as the `ctx` arg to `nested_group_slices`; the returned cursor's `group_ctx = parent->group_ctx.pushed(nested_tag)`.
- At depth-1 this is identical to how the generated typed accessor threads context (`emit_messages.cpp:263-268` passes `ctx_.group_ctx`), so C-ABI and C++ agree by construction on the represented layout. **Depth scope**: the pushed path is the *arithmetically-correct* full path; at depth ≥ 2 the typed path has a pre-existing unpushed-context gap (research Decision 7), so depth-≥2 read correctness/equivalence is out of scope and unasserted here.

## Entity: nested-group instance slice (bounding corrected — the defect)

`fixpp::wire::group_slice` `{ data: const byte*, len: size_t }` — one nested-group instance. Unchanged shape. **What changes is how the LAST instance's `len` is computed**:
- *Before*: positional scan closes it at the end of the outer entry's slice → absorbs a trailing outer member.
- *After*: `consume_group_extent` (via `nested_group_slices`) bounds it at the first non-member tag under the nested group's context → excludes the trailing outer member.

## Reused, unchanged

- `OffsetTable::nested_group_slices` (7-arg), `build_nested_subview`, `consume_group_extent`, the ROOT-owned `(slice_data, nested_no_tag)` cache — algorithms and keying untouched (FR-005). A new 4-arg overload forwards to the 7-arg one with the table's own `opaque_dict_`/`group_member_fn_` and a **build-mode-safe** generation token via a private helper `token_for_nested_cache() const noexcept` (`#ifndef NDEBUG return gen_; #else return {};`) — because `gen_` exists only under `#ifndef NDEBUG` (`offset_table.hpp:262-264`), forwarding `gen_` directly would not compile in release. With the helper the overload compiles in both build modes (research Decision 4).
- `OffsetTable::stored_group_context()` — read to seed the top-level cursor.

## State transitions

None. Reads are pure over an immutable parsed view; the only mutable state touched is the pre-existing `mutable` nested-view cache inside the const `OffsetTable`, exactly as the typed path already mutates it.
