# Data Model: Nested Group-Parse Correctness (063)

**Date**: 2026-07-07 · Companion to [plan.md](./plan.md) / [research.md](./research.md). No new persisted/wire entities — this is a correctness fix on existing dictionary-metadata and wire-parse structures.

## Entities (existing, semantics corrected)

### GroupMembership (dictionary metadata) — Option A, ONE context key
- **Today**: `table_view.group_members_` / `group_first_` (`table_view.hpp:215,218`) / loader `group_index_by_no_tag_` map **`no_tag → members`**, first-XML-occurrence-wins (`xml_loader.cpp:486`). Single variant per tag → collision (Defect A).
- **Corrected (Option A — ADOPTED)**: **one** precise runtime key — **`(msg_type, bounded parent-no_tag-path, no_tag) → members`**. Unique by FIX structure (a message cannot head two same-count-tag groups under the same parent-path). **msg_type is MANDATORY**: MassQuote vs MassQuoteAck share the entire path `root→296→295` and differ only by message type (census-proven; a `(parent, no_tag)` key is insufficient — every collision is parent-ambiguous). `group_first_field(...)` (the delimiter) is resolved under the same context key.
  - **Load-time vs parse-time reconciliation**: the loader registers per **referencing message**, expanding shared `<component>`s (the loader's `component_index` `xml_loader.cpp:479` is invisible on the wire) and accumulating the **full** parent-no_tag path (`expand_field_list` today threads only the immediate `enclosing_group_no_tag` `:529`). The parser recomputes the identical key from `(msg_type, wire parent-no_tag chain, no_tag)` at `MessageView::group<>` (`parser.hpp:271`); `msg_type()` is a `MessageView` member (decl `:149`), so `group<>` can compute it. The two ends agree exactly on the parent-no_tag chain + msg_type + no_tag.
  - **Invariant**: `member ∈ members(msg_type, path, no_tag)` iff `member` appears under `no_tag` in *that* message-context. (Not a union — the codegen union `G_<n>` remains a separate **post-slice accessor** superset, `emit_messages.cpp:390-406`; it is NOT the slicing key.)
  - **Surface**: `group_member_fn_t` (`offset_table.hpp:29`) + `group_member_tags`/`group_first_field` accessors gain the context args (public C++, non-ABI; clarify-sanctioned).
  - **Context-propagation mechanism (Round 2 — OffsetTable carries the context)**: the context rides on the **`OffsetTable` as construction state (a `group_context`)**, NOT on the call args. The **ROOT** table is built with `{msg_type, path=[]}` at `MessageView::group<>` (`parser.hpp:271`; `msg_type()` reachable at `:149`); a **NESTED** sub-table built by `build_nested_subview` (`offset_table.cpp:552`) is seeded with `{msg_type, outer-path + outer-group-no_tag}`. `OffsetTable::group()` reads its stored `group_context` and passes it into the membership predicate. So `group_slices(no_tag)` + the C-ABI signatures stay **stable** — only `group_member_fn_t` gains the context param, and the nested-descent call site (`emit_messages.cpp:260-270`) gains the context argument (→ FR-005 emitter edit + forced golden regen).
- **~~Option B (rejected)~~**: `no_tag → UNION` was rejected — the union false-positive corrupts the slice boundary at `offset_table.cpp:447` and the census cannot bound the risk (B-004-1). Kept out of the runtime key.
- **Consumers** (must observe the context-scoped membership consistently): `group_member_fn` (`parser.hpp:484-494`), `OffsetTable::group()` (`offset_table.cpp:433,447`) + nested descent `build_nested_subview`/`nested_group_slices` (`:552`/`:582`), `validator.hpp:219`, `Dictionary::as_table_view()` (`dictionary.cpp:295-357`).

### GroupInstanceExtent (wire parse)
- The byte range of one outer group occurrence, computed by `OffsetTable::group()` (`offset_table.cpp:402-482`) → `group_index{no_tag, first, entry_count}` → `group_slices()`.
- **Today**: flat `seen_in_instance` heuristic (`:450-459`) truncates when a nested group repeats within one outer instance (Defect B).
- **Corrected**: nesting-aware — on a member tag that is a `NumInGroup` count present as a nested group **in this context** (exact Option-A membership), consume its nested extent before resuming outer-boundary detection. Per-level fail-closed count semantics: parse the count, consume exactly `declared` instances or fail closed (malformed/short/overflow); zero-count consumes no extent (B-004-7); apply `cfg_.max_group_entries_per_instance` (`offset_table.cpp:476`) per level. **Allocation-free** (stack-only, depth-bounded index recursion over `entries_`; depth cap = private compile-time `K=16`, mirroring codegen `kMaxGroupDepth`).

### ReusedTagCensus (analysis artifact, FR-002 — COMPLETENESS aid, not a soundness gate)
- Enumeration over all nine runtime XMLs **incl. FIXT.1.1** of every `NumInGroup` tag reused with differing membership, produced by a **loader-faithful (component-expanding)** walk. Fields per row: `dict, no_tag, variant_count, member_sets, contexts`. Each collision gets a discriminating regression guard (FR-002 coverage).
- **No over-extension "gate"**: under Option A membership is exact, so over-extension does not arise. A declaration-order census could never have gated Option B's soundness anyway — the wire is order-independent (B-004-1). The census's role is purely **completeness** (enumerate + guard every collision), not safety adjudication.

### entry_context (062 — gains a named trivially-copyable `group_context` field under Option A)
- `group_view.hpp:31-47` (trivially-copyable, `static_assert` `:47`): span, mr, opaque_dict, group_member_fn, gen, parent_cache_owner, outer_occurrence_id. **063 adds a named POD `group_context` field** (a **named contract**, Round 2):
  - `std::string_view msg_type` — points into the **MESSAGE WIRE BUFFER** (NOT dictionary scratch); owned by the parent/cloned message, so it **outlives every nested entry** — the same one-parse, ROOT-owned lifetime as the existing `span` field (`group_view.hpp:32`). `msg_type` is `MessageView::msg_type()` (`parser.hpp:149`, `[[clang::lifetimebound]]`), aliasing the wire frame.
  - `std::array<std::uint16_t, K=16>` bounded parent-no_tag path + `std::uint8_t depth` (length).
  - `static_assert(std::is_trivially_copyable_v<group_context>)` on the new POD, AND preserve `entry_context`'s existing `is_trivially_copyable_v<entry_context>` static_assert (`group_view.hpp:47`) — a heap/vector path would break both and the alloc-free budget.
- This is the plumbing 062's nested descent (`build_nested_subview`/`nested_group_slices`) reads to recompute exact membership for nested slices; 062's slicing/caching **algorithm** and the `(slice_data, nested_no_tag)` cache key are unchanged (FR-005) — context is constant within one message parse and the ROOT-owned cache lives exactly one parse. The nested-descent **call site** (`emit_messages.cpp:260-270`) gains the context argument → emitter edit + forced golden regen.

## Relationships
```
XML (9 dicts) --load--> GroupMembership((msg_type,parent-path,no_tag)→members)   [Defect A fix site]
                                   │
                        group_member_fn (predicate)
                                   ▼
OffsetTable::group() --uses membership--> GroupInstanceExtent   [Defect B fix site]
                                   ▼
group_slices() → (062) build_nested_subview/nested_group_slices → generated G_<n> superset accessors
                                   ▼
                    typed read == exact wire values  (SC-001b)
```

## Validation / invariants
- **INV-A**: for MassQuote context, `group_member_tags(295)` ⊇ {299,132,133}; `group_member_fn(295,299)=true` (fixes the acceptance failure).
- **INV-B**: an outer instance containing a nested group of N entries has extent enclosing all N (multi-entry nested read `size()==N`).
- **INV-preserve**: single-entry nested, count-of-zero (`B-004-7`), flat groups, benign same-membership reuse unchanged.
- **INV-alloc**: `group()` extent computation performs zero heap allocation.
- **INV-abi**: C-ABI exported symbols + header freeze unchanged.
