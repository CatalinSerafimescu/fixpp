# Data Model: Nested Group-Parse Correctness (063)

**Date**: 2026-07-07 · Companion to [plan.md](./plan.md) / [research.md](./research.md). No new persisted/wire entities — this is a correctness fix on existing dictionary-metadata and wire-parse structures.

## Entities (existing, semantics corrected)

### GroupMembership (dictionary metadata)
- **Today**: `table_view.group_members_` / loader `group_index_by_no_tag_` map **`no_tag → members`**, first-XML-occurrence-wins (`xml_loader.cpp:486`). Single variant per tag → collision (Defect A).
- **Option B (recommended)**: `no_tag → UNION(members across all uses)` — accumulate, not first-wins; keyed to match codegen (`(group_no_tag, no_tag)`, `emit_messages.cpp:390-406`). `group_first_field(no_tag)` (the delimiter) stays first-seen. Invariant: `member ∈ union(no_tag)` iff `member` appears under `no_tag` in *some* message. No signature change.
- **Option A (fallback)**: `(context, no_tag) → members`, where `context` = enclosing-group path / message. Requires `group_member_fn_t` + accessors to carry `context`. (Selected only if the over-extension analysis fails.)
- **Consumers** (must observe the corrected membership consistently): `group_member_fn` (`parser.hpp:484-494`), `OffsetTable::group()` (`offset_table.cpp:433,447`), `validator.hpp:219`, `Dictionary::as_table_view()` (`dictionary.cpp:295-357`).

### GroupInstanceExtent (wire parse)
- The byte range of one outer group occurrence, computed by `OffsetTable::group()` (`offset_table.cpp:402-482`) → `group_index{no_tag, first, entry_count}` → `group_slices()`.
- **Today**: flat `seen_in_instance` heuristic (`:450-459`) truncates when a nested group repeats within one outer instance (Defect B).
- **Corrected**: nesting-aware — on a member tag that is a `NumInGroup` count in this dict, consume its nested extent (count × per-entry, honoring its delimiter) before resuming outer-boundary detection. **Allocation-free** (stack-only, depth-bounded index recursion over `entries_`).

### ReusedTagCensus (analysis artifact, FR-002)
- Enumeration over all nine runtime XMLs of every `NumInGroup` tag reused with differing membership, produced by a **loader-faithful (component-expanding)** walk. Fields per row: `dict, no_tag, variant_count, member_sets, contexts`.
- Second output (Option-B gate): per OFFICIAL message + group, `trailing_wire_neighbour ∈ union(no_tag)?` → the over-extension check. Empty hit-set ⇒ Option B ships.

### entry_context (062, UNCHANGED — FR-005)
- `group_view.hpp:31-47` (trivially-copyable): span, mr, opaque_dict, group_member_fn, gen, parent_cache_owner, outer_occurrence_id. 063 does not modify it under Option B. (Option A would add a `context` field, preserving trivial-copyability.)

## Relationships
```
XML (9 dicts) --load--> GroupMembership(no_tag→union)   [Defect A fix site]
                                   │
                        group_member_fn (predicate)
                                   ▼
OffsetTable::group() --uses membership--> GroupInstanceExtent   [Defect B fix site]
                                   ▼
group_slices() → (062) build_nested_subview/nested_group_slices → generated G_<n> superset accessors
                                   ▼
                    typed read == exact wire values  (SC-001)
```

## Validation / invariants
- **INV-A**: for MassQuote context, `group_member_tags(295)` ⊇ {299,132,133}; `group_member_fn(295,299)=true` (fixes the acceptance failure).
- **INV-B**: an outer instance containing a nested group of N entries has extent enclosing all N (multi-entry nested read `size()==N`).
- **INV-preserve**: single-entry nested, count-of-zero (`B-004-7`), flat groups, benign same-membership reuse unchanged.
- **INV-alloc**: `group()` extent computation performs zero heap allocation.
- **INV-abi**: C-ABI exported symbols + header freeze unchanged.
