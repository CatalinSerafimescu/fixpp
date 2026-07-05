# Implementation Plan: Grouped Typed-Read Path Fix (062)

**Branch**: `062-grouped-typed-read-fix` | **Date**: 2026-07-05 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification + [research.md](./research.md) (Phase-0)

## Summary

Make typed field access on repeating-group **entries** compile and return correct per-instance values — scalars AND nested groups — closing the `group_view`/codegen contract mismatch that currently makes `group[i].field()` ill-formed. Delivers QuickFIX C++/J parity for nested groups (MassQuote quote-entry prices). Chosen mechanism (research §Mechanism): a **hybrid** — the entry span-scans its own slice for scalar fields (no per-entry sub-index, protecting the one-level MarketData hot path per FR-004a), and builds a **dict-aware sub-view lazily, cached once in the parent arena, only on nested descent** (satisfying FR-002 nested + FR-004 zero one-level alloc). Prerequisite for feature 061.

## Technical Context

**Language/Version**: C++23. **Primary surface**: `include/fixpp/wire/{group_view,parser,offset_table,view,framer}.hpp` + `src/wire/offset_table.cpp` (wire); `tools/codegen/fixpp-codegen/emit_messages.cpp` + regenerated `_codegen` headers + golden `specs/003-dictionary-codegen/contracts/golden/v44_Messages.golden.hpp` (codegen). **Testing**: GoogleTest under `tests/wire/` + `tests/codegen/`; sanitizer matrix (ASan/UBSan/TSan) + alloc gate. **Target Platform**: Linux Tier-1 + Windows Tier-2. **Project Type**: single library. **Scale/Scope**: focused wire+codegen fix; no new message families, no builders, no C-ABI change.

## Constitution Check

*GATE: must pass before Phase 0 (passed — research complete) and re-checked post-design.*

- **Appendix A mandatory triggers**: **Wire format/parser** (group entry read path, `OffsetTable`/`group_view` changes, `frame_view`-over-slice seam) AND **Codegen layout** (entry-class emission + forced regen) → run ALL four mandatory controls: `/clarify` (done, 2 sessions), `/analyze` (pending), Codex **Gate A** (pending), user **`/plan` sign-off** (pending). Full **Gate B** before merge.
- **Error semantics**: no new `fixpp_error_t` value; entry accessors reuse existing field-not-found/decode errors (FR-007).
- **C-ABI**: unchanged (FR-007).
- **Zero-alloc discipline** (constitution hot-path): honored on the one-level scalar path (FR-004a); nested descent materializes a bounded, cached-once sub-view (FR-004 reconciliation, research §Cost reconciliation) — surfaced at `/plan` sign-off as the intended nested cost.
- **Sanitizers as real defects**: lifetime correctness (entry borrows parent; nested sub-view arena-owned) validated under the full sanitizer matrix, not asserted.

**Post-design re-check**: no new violations; the eager-materialization hot-path risk that would have breached zero-alloc discipline is designed out by the hybrid (one-level never builds a sub-index). No Complexity-Tracking waiver required.

## Project Structure

### Source Code (repository root)

```text
include/fixpp/wire/group_view.hpp     # carry per-group context {mr, dict, group_member_fn}; operator[]/iterator hand entry {span, ctx}
include/fixpp/wire/parser.hpp         # MessageView::group<>() threads context into group_view; expose dict/group_member_fn to it
include/fixpp/wire/offset_table.hpp   # nested sub-view cache (keyed by (no_tag, instance)) in the per-message arena; API to build/fetch a sub-view over a slice
src/wire/offset_table.cpp             # sub-view materialization over a slice; trailing-SOH slice/scan fix (+ whole-message-path regression guard)
include/fixpp/wire/framer.hpp / view.hpp   # frame_view-over-slice friend-seam (mirror src/capi/message_write.cpp:63-74 precedent); group_slice/context struct shape if needed
tools/codegen/fixpp-codegen/emit_messages.cpp  # entry class: store {span, ctx}; scalar accessors -> span-scan; nested accessor -> lazy dict-aware sub-view
# regenerated: build/<preset>/_codegen/include/fixpp/**/Messages.hpp ; golden: specs/003-dictionary-codegen/contracts/golden/v44_Messages.golden.hpp
tests/wire/  tests/codegen/          # discriminating witnesses over GENERATED flyweights: one-level scalar+decimal, nested (MassQuote NoQuoteSets->NoQuoteEntries), empty group, operator[]==iter(); + a regression guard (instantiate operator[] on a generated entry)
```

**Structure Decision**: wire-layer feature with a confined codegen entry-class change; no new modules. Design artifacts: [data-model.md](./data-model.md), [contracts/](./contracts/), [quickstart.md](./quickstart.md).

## Implementation Sequencing

1. **Enabling seams (wire) FIRST**: `frame_view`-over-slice friend-seam + a sub-view-over-slice builder + the trailing-SOH fix (with a whole-message-parse regression test proving no top-level regression). Verify: an internal test builds a working typed reader over a hand-cut slice.
2. **group_view context threading**: `group_view` carries `{mr, dict, group_member_fn}`; `MessageView::group<>()` passes it; `operator[]`/`iter()` hand the entry `{span, ctx}`; preserve seam-#8. Verify: `repeating_group_equivalence_test` extended to a GENERATED flyweight compiles + passes.
3. **Codegen entry emission**: entry stores `{span, ctx}`; scalar accessors span-scan; nested accessor lazy sub-view. Rebuild tool, clear `_codegen` markers, regen, update golden. Verify: golden diff is intentional; `typed_accessor_test` reads real entry fields.
4. **Discriminating witnesses + regression guard** (FR-006): one-level (scalar+decimal), nested (MassQuote), empty group, equivalence; a guard that re-breaks on revert.
5. **Sanitizer matrix + alloc gate** (FR-004/FR-004a): prove one-level scalar read = zero sub-index/zero alloc; nested descent = bounded cached-once; ASan/UBSan/TSan clean.

## Complexity Tracking

| Decision | Why | Simpler alternative rejected because |
|----------|-----|--------------------------------------|
| Hybrid (span-scan scalars + lazy nested sub-view) | Satisfies FR-002 (nested) and FR-004a (one-level cheap) simultaneously | Pure (b) builds sub-indices for one-level groups -> hot-path regression (FR-004a); pure (c) has no nested slicer -> fails FR-002 |
| `frame_view`-over-slice friend-seam | No public span->frame_view path; sub-view build needs a frame_view | Making `frame_view`'s ctor public widens surface for no benefit; the friend-seam precedent (`message_write.cpp:63-74`) is the established pattern |
| Nested sub-view cache in the parent OffsetTable arena | Entries are by-value temporaries; cache must outlive them and dedupe repeat descents | Per-entry-instance cache impossible (temporary); rebuild-per-call violates FR-004 for nested |
| Touch `OffsetTable::build()` trailing-SOH guard | Counted Length+Data last field in a slice fails the guard | Slice-only widening insufficient if the guard rejects; must fix with whole-message-path regression coverage |
