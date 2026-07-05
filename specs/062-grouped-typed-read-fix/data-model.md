# Data Model: Grouped Typed-Read Path Fix (062)

Not a persistent data model — the runtime read-path types the fix touches.

## Entities

### group_slice (`include/fixpp/wire/view.hpp:77-80`)
One repeating-group occurrence's bytes: `{ std::byte const* data; std::size_t len; }`. Points into the parent frame buffer; owned/borrowed via the parent OffsetTable arena. **Constraint (fix)**: `len` must cover the occurrence including the trailing SOH of its last field, OR the entry scanner/sub-index build must tolerate a missing final SOH (research: trailing-SOH hazard).

### entry read context (NEW, wire)
Small trivially-copyable bundle the entry needs to read itself: `{ std::pmr::memory_resource* mr; void const* opaque_dict; OffsetTable::group_member_fn_t group_member_fn; }` — all borrowed from the parent OffsetTable/MessageView. Used by the entry's nested-group accessor only; scalar reads need only the slice span. Carried by `group_view`, handed to each entry by `operator[]`/`iter()`.

### group_view<GroupT> (`include/fixpp/wire/group_view.hpp`)
Enumerable view over a group's instance slices. **Change**: additionally carries the entry read context; `operator[]`/`iterator::operator*` construct `GroupT` from `{slice_span, context}` (was: from `span` alone, incompatible with the generated ctor). `size()` = occurrence count. Seam-#8: `operator[]` and `iter()` enumerate identical slices — unchanged.

### Entry flyweight `G_<no_tag>` (generated, `fixpp::<ns>::groups`)
**Change**: stores `{ std::span<const std::byte> bytes_; entry_context ctx_; }` (was `MessageView<Index> const* view_`). Read surface unchanged for callers:
- scalar `field() -> expected_t<T>` (string/char/int) and `field(mr) -> expected_t<decimal_t>`: implemented by **span-scanning `bytes_`** for the tag (via `field_iterator`) then `decode_field`/`decimal_t::parse`. No sub-index.
- `field_value(tag) -> expected_t<field_view>`: span-scan.
- nested `group<c, G_c>() -> group_view<G_c>`: builds a **dict-aware sub-view over `bytes_` lazily**, cached once (see below), returns its `group<c>()`.

### Nested sub-view cache (NEW, wire — parent OffsetTable arena)
Keyed by `(no_tag, instance_index)`. On first nested descent into an instance, build a sub-view (sub-OffsetTable over the entry slice, dict-aware via `ctx_`), store in the per-message arena, return it; subsequent descents reuse it. Lifetime = parent message. Ensures FR-004 (no per-access alloc on repeat nested reads) while entries remain by-value temporaries.

## Invariants
- **INV-G1**: entry borrows the parent parsed message; valid only while the parent MessageView is alive (documented, matches existing flyweight lifetime model).
- **INV-G2**: one-level scalar read builds no sub-index and allocates nothing (FR-004a).
- **INV-G3**: nested descent builds at most one sub-view per (no_tag, instance), cached (FR-004).
- **INV-G4**: `operator[]` and `iter()` enumerate identical entries in identical order (seam-#8).
- **INV-G5**: no change to top-level (non-group) message field reads or to the C-ABI/error enum (FR-007).
