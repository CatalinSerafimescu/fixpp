# Data Model: Dictionary-backed inbound receive parse (066)

No new wire/persisted data. This feature adds owned metadata members and threads a dictionary handle so inbound-derived views carry membership. Entities:

## Entity: Session inbound `table_view` (new, owned)

- **What**: the once-built dictionary metadata (membership predicate + field classification) the inbound `Parser` binds to. Built at `open()` from `cfg_.dictionary->as_table_view()`.
- **Lifetime/ownership**: a `Session` member with a **stable address** for the session lifetime (the dict-backed `Parser` ctor stores `std::addressof` of it — `parser.hpp:506`). Suggested shape: `std::optional<fixpp::dict::table_view> inbound_tv_`. Mirrors the owned `table_view` the strict validator already holds (`session.cpp:1173`); the two builds may later be shared, out of scope here.
- **Invariant**: built exactly once (at `open()`, dictionary guaranteed non-null); never rebuilt per message (FR-002/FR-004).

## Entity: Inbound `MessageView` / root `OffsetTable` (behavior change)

- Gains a non-null `opaque_dict_` (→ the session `table_view`) + `group_member_fn_` + a root `group_context` (`{msg_type, path=[]}` seeded in the dict-backed ctor). Consequence: `OffsetTable::group()` uses membership-bounded extents (not `entries_.size()`), and the scalar-as-group delimiter check is live. Both the C-ABI reads and the C++ typed flyweights (which read membership from this same table via `entry_context`) become correct.

## Entity: Clone-owned `table_view` (new, owned) + inbound `fixpp_msg.dict_`

- The inbound `fixpp_msg` (stack handle, `engine.cpp`) carries the session's `shared_ptr<const Dictionary>` (was null for inbound) so `fixpp_msg_clone` can propagate.
- `fixpp_msg_clone` gains a **clone-owned** `table_view` member (alongside `owned_frame_`/`owned_view_`, `capi_internal.hpp:271-273`), built from the copied `dict_`, with the clone's `MessageView` bound to it — so the clone reads groups identically to its source (FR-007). The clone's arena already uses `new_delete` upstream (owning handle), so this owned build is within contract.

## Entity: Reify owning handle dictionary (new)

- `owning_message_handle` (`reify.cpp` pimpl) carries the dictionary so its lazily-re-framed `view_cache_` is dict-backed (FR-007), rather than the current 2-arg dict-free re-frame.

## Reused, unchanged (FR-005)
- `Parser` dict-backed ctor; `OffsetTable::group()/consume_group_extent/nested_group_slices/build_nested_subview`; `as_table_view()`; generated flyweights; `Dictionary`. Algorithms and signatures unchanged — 066 only changes which parser the session/clone/reify use.

## State transitions
None new. `inbound_tv_` is built at `open()` and read (address-bound) by every subsequent parse; destroyed with the Session. Per-message membership state remains the existing per-message-arena `mutable` caches inside the (now dict-backed) `OffsetTable`.
