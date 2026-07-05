# Phase 0 Research: Grouped Typed-Read Path Fix (062)

**Date**: 2026-07-05 | **Branch**: `062-grouped-typed-read-fix`

## The defect (source-verified)

Compile-time type mismatch, not a runtime wrong-value: `wire::group_view<GroupT>::operator[]` builds `GroupT{std::span<const std::byte>{s.data, s.len}}` (`include/fixpp/wire/group_view.hpp:34-37`, iterator `:42`), but every generated entry class `G_<n>` has only `G_n() = default` + `explicit G_n(MessageView<Index> const&)` (`tools/codegen/fixpp-codegen/emit_messages.cpp:212-217`) and reads fields via `view_->template get<TAG>()` (`emit_scalar`/`emit_field_value`, `:66-124`). So `operator[]` is ill-formed the instant it is ODR-instantiated on a generated flyweight. Masked because `tests/wire/repeating_group_equivalence_test.cpp` uses a hand-written span-ctor `TestLeg` and `tests/codegen/typed_accessor_test.cpp` only calls `size()`/default-constructs an entry.

## Feasibility facts

- **Indexing needs no envelope**: `OffsetTable::build` (`src/wire/offset_table.cpp:186-310`) scans `frame.bytes()` as a raw `tag=value\x01` stream; never looks for 8/9/35/10. `MessageView<Index>::get(tag)` reads purely via `table_.find(tag)` (`parser.hpp:212-219`). So an index CAN be built over a bare entry slice.
- **But input type is `frame_view const&`, whose populated ctor is protected** (mintable only by `Framer` / the `frame_view_access` friend, `framer.hpp:95-105`). There is a **production friend-seam precedent**: `src/capi/message_write.cpp:63-74,406` re-declares `frame_view_access` and mints a `frame_view` from arbitrary `{buf,len,body_off,body_len}`. So a `frame_view`-over-slice seam is a known, replicable move.
- **Group slices** (`OffsetTable::group_slices`, `offset_table.cpp:456-517`) materialize **lazily on first access**, one `{data,len}` per occurrence, into an **mr-backed, reserve-once, append-only** vector valid for the message lifetime (`offset_table.hpp:164-176`). The arena = the per-message PMR (`offset_table.hpp:140`), lifetime = parent message. A `group_slice` is just `{std::byte const* data, std::size_t len}` (`view.hpp:77-80`) — no sub-index. **Per-entry sub-views CAN be built + stored in this same arena.**
- **Group extent is dict-driven**: `OffsetTable::group()` (`offset_table.cpp:375-453`) uses the count field, the delimiter (first tag after count), and `group_member_fn_(opaque_dict, no_tag, tag)` (threaded from `Parser`, `parser.hpp:438-448`). This slicer exists ONLY over the indexed `entries_` array — there is **no dict-driven nested-group slicer over a bare span**. This is why a pure span-scan cannot do nested groups.
- **Trailing-SOH hazard**: materialized slice `len = end_off - fs` is computed pre-trailing-SOH (`offset_table.cpp:501-504`); `build`'s counted Length+Data path clears the whole table if `end < n && buf[end] != SOH` (`offset_table.cpp:264-268`). So any sub-index/scan over an entry whose LAST field is RawData/EncodedText/XmlData fails unless the slice includes the trailing SOH or the scanner tolerates its absence. **Blast radius note**: fixing the `build()` guard touches the whole-message parse path — needs regression coverage there.
- **Span-native scan already exists**: `MessageView::field_iterator` takes a raw `std::span<const std::byte>` (`parser.hpp:161`) and `advance()` (`:346-420`) walks `tag=value\x01` dict-free with correct Length+Data carry — **no frame_view needed**. This is the primitive for one-level scalar reads over an entry slice.
- **Entry storage & ripple**: entry stores `MessageView<Index> const* view_` (`emit_messages.cpp:258-261`); message-level flyweight stores `MessageView const&` by reference and is INDEPENDENT of entry storage. Changing the entry's stored member is localized to `emit_group_class` + the ptr=true branches of `emit_scalar`/`emit_field_value` — it does NOT ripple into the message flyweight.

## Reference-engine parity (why nested is in scope)

Verified 2026-07-05 in the cloned engines: **QuickFIX C++** (`reference-engines/quickfix-cpp/include/quickfix/fix44/MassQuote.h`) and **QuickFIX/J** (`quickfixj-messages-all/.../fix44/MassQuote.java`) both generate **fully recursive nested typed group classes** — `NoQuoteSets` → `NoQuoteEntries` (holding `BidPx`/`OfferPx`/`BidSize`/`OfferSize`), `NoPartyIDs` → `NoPartySubIDs`, `NoLegs` → `NoLegSecurityAltID`. MassQuote's core prices are NESTED and read per-instance-typed by both. `field_value(tag)` on the whole message returns only the first occurrence → cannot substitute. Hence FR-002 (nested typed read, P1) is a parity requirement, not a nice-to-have.

## Mechanism decision: HYBRID (span-scan scalars + lazy dict-aware nested sub-view)

Three candidates were evaluated; the two hard constraints — **FR-002 (nested-in-entry, P1)** and **FR-004a (one-level scalar reads build no per-entry sub-index)** — eliminate the pure forms and force a hybrid:

- **(a) per-entry by-value `MessageView`** — DROP. `operator[]` returns the entry by value every call, so a by-value OffsetTable = a sub-index build+alloc per `[i]` → violates FR-004/FR-004a.
- **(b) eager per-entry sub-views for ALL groups** — DROP as the sole mechanism. Satisfies nested but builds a sub-index for one-level groups too (MarketData `NoMDEntries`) → violates FR-004a (hot-path).
- **(c) pure span-scan entries** — DROP as the sole mechanism. Cheap one-level reads, but NO dict-driven nested slicer over a bare span → cannot satisfy FR-002.
- **(D) HYBRID — CHOSEN**:
  - The entry (`G_n`) holds its **slice span + a small context** `{mr, opaque_dict, group_member_fn}` (all borrowed from the parent OffsetTable; trivially copyable; no alloc). `group_view` carries this context per group and `operator[]` hands it to the entry.
  - **Scalar accessors** span-scan the entry slice via `field_iterator` — no sub-index, zero alloc (satisfies FR-004a + FR-004 for the one-level hot path).
  - **Nested-group accessor** (`entry.group<c,G_c>()`) builds a **dict-aware sub-view over the entry slice, lazily, on first descent, cached once into the parent arena** (keyed by (no_tag, instance) so repeated nested reads on the same instance don't re-allocate) — reuses the existing dict-driven slicer, so nesting works recursively (satisfies FR-002). Nested descent pays a bounded, cached-once sub-index; the one-level path never does.
  - **`operator[]`/`iter()` equivalence** (seam-#8) preserved — both still enumerate the same instance slices; only what they hand the entry changes.

### Cost reconciliation (FR-004 vs FR-004a)
Zero-per-access-alloc (FR-004) holds strictly for the one-level scalar path. Nested descent materializes a **cached-once** sub-view (bounded, arena-owned) — the intended, spec-sanctioned cost of nested support; it is NOT a per-access allocation on repeat reads of the same instance.

### Surface touched (Constitution Check → Wire format/parser + Codegen layout triggers)
- **Wire**: `group_view` (carry context; `operator[]`/iterator hand entry `{span, ctx}`), `MessageView::group<>()` (thread context into `group_view`), a `frame_view`-over-slice friend-seam, the nested sub-view materialization + arena cache (likely on `OffsetTable`), the trailing-SOH slice/scan fix, possibly the `group_slice`/context struct shape.
- **Codegen** (`emit_messages.cpp`): entry class stores `{span, ctx}`; scalar accessor bodies → span-scan; nested accessor → lazy dict-aware sub-view. Forced regen (rebuild tool + clear `_codegen` markers). Golden `v44_Messages.golden.hpp` updates.
- Both Appendix-A triggers apply → full mandatory controls (already run: `/clarify`; pending `/analyze`, Codex Gate A, user `/plan` sign-off) + full Gate B.

## Key risks (for /plan + Gate A)
1. **Trailing-SOH correctness**: entry slices ending in a counted Length+Data field. Fix must cover span-scan AND any sub-index build; the `build()` guard fix has whole-message blast radius → regression coverage on top-level parsing required.
2. **Context threading**: `{mr, opaque_dict, group_member_fn}` must reach the entry; confirm the parent MessageView exposes them (dict/`group_member_fn_` at `parser.hpp:438-448,497`).
3. **Nested cache lifetime**: cache keyed by (no_tag, instance) in the parent arena; entries are by-value temporaries, so the cache must live in the parent OffsetTable, not the entry.
4. **Regression guard** (FR-006): a test instantiating `operator[]`/`iter()` on a GENERATED flyweight so a revert re-breaks the build/test.
5. **Codegen determinism** (FR-005): golden update + forced-regen discipline (`Codegen.cmake` is blind to emitter edits — rebuild tool + clear markers).
