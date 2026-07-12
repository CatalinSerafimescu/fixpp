# Phase 1 Data Model: 072-nested-group-hardening

No new persisted data. The "entities" are the in-memory dictionary/context structures the feature reads and the one new error type it adds.

## New

### `dict::group_delimiter_collision_error` (new C++ type)
- **Kind**: exception class, `namespace fixpp::dict`, in `include/fixpp/dict/error.hpp`.
- **Derives from**: `dict::xml_parse_error` (so existing `catch(xml_parse_error&)` handlers catch it) — see research D-A1.
- **Carries**: a human-readable message naming the offending `no_tag`, its delimiter, and the parent's delimiter.
- **`code()`**: a new appended `fixpp::core::error` variant (e.g. `dict_nested_delimiter_collision`) in `include/fixpp/core/error.hpp` (append-only, Art. X §4). **Not** a C-ABI `fixpp_error_t` value — no frozen-ABI impact.
- **Thrown by**: `LoaderState::finalize()` in `src/dictionary/xml_loader.cpp`, inside the `trap_throw_or_throw` window (propagates unchanged).

## Read / walked (existing, unchanged shape)

### `GroupDef` (loader-internal, `src/dictionary/xml_loader.cpp:207`)
- Fields used: `no_tag`, `first_field_tag` (delimiter), `parent_group_no_tag`. Post-component-expansion at the point `groups_` is finalized.
- **Guard walk**: for each with `parent_group_no_tag != 0`, compare `first_field_tag` to the parent's (via `group_index_by_no_tag_`). Global first-seen-per-`no_tag` (dedup residual, FR-005b).

### `group_context` (`include/fixpp/wire/group_view.hpp:56` `pushed(no_tag)`)
- The `(msg_type, parent-no_tag path)` under which a group's members are resolved. `pushed(no_tag)` appends and `++depth` (clamp K=16).
- **Part B fix**: the emitter must store `ctx_.group_ctx.pushed(nested_no_tag)` on the returned child view (once, at mint).

### `table_view` membership stores (`include/fixpp/dict/table_view.hpp`)
- **Context store**: `add_group_member_ctx(msg_type, path, no_tag, member)` (`:359`) / `set_group_first_ctx`. Populated by `as_table_view()`.
- **Legacy bare store**: `add_group_member(no_tag, member)` (`:316`). Context lookups fall back to it on a miss (`group_first_field:268`, `group_member_tags:278`).
- **Part B witness**: hand-build a `table_view` where the context store and bare store disagree for grandchild group `555`, so the pre-fix bare-fallback is observably wrong.

## State transitions

None. Dictionary load is a one-shot parse→validate→build; the guard adds one validation step (reject-or-continue). No FSM.
