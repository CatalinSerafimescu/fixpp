# Quickstart: Nested Group-Parse Correctness (063)

How to reproduce the defects, run the census that decides the fix option, and validate the fix. Run from the library submodule root.

## 0. Reproduce (pre-fix, RED)
- **Defect A** (membership): un-skip / add a witness that loads real `FIX44.xml` → `Dictionary::as_table_view()` → parses a MassQuote and reads `quote_sets()[s].quote_entries()`. Pre-fix: `.size() == 0` (first-seen-wins picks QuotCxlEntriesGrp; `group_member_fn(295,299)=false`).
- **Defect B** (extent): `tests/codegen/nested_group_read_test.cpp:353` `NestedQuoteEntriesPerInstancePrices` is currently `GTEST_SKIP()`'d — its MassQuote QuoteSet[0] has 2 QuoteEntries; pre-fix the outer slice truncates at the 2nd → `quote_entries().size()==1`.

## 1. Run the census (decides Option A vs B)
The loader-faithful (component-expanding) reused-tag census over all nine XMLs. Prototype: `scratchpad/census2.py` (already reproduces FIX44=12 colliding tags, all parent-ambiguous; 295 → parent-0 QuotCxlEntriesGrp vs parent-296 QuotEntryGrp). The implementation-grade census additionally emits, per OFFICIAL message + group, the **over-extension check**: does any group's trailing wire-neighbour ∈ its no_tag union? Empty ⇒ **Option B** (union membership) ships; non-empty on an in-scope message ⇒ **Option A** (per-context) or a per-tag exception.

## 2. Apply the fix
- **Defect A / Option B**: `src/dictionary/xml_loader.cpp:486` — replace first-seen-wins with union-accumulate of members per `(group_no_tag, no_tag)` (mirror `emit_messages.cpp:390-406`); `group_first` (delimiter) stays first-seen.
- **Defect B**: `src/wire/offset_table.cpp:402-482` — replace the flat `seen_in_instance` walk (`:450-459`) with a depth-bounded, **allocation-free** nested-count-aware boundary walk (on a member tag that is a `NumInGroup` count in this dict, read its count from `entries_` and consume its extent before resuming the outer boundary).

## 3. Validate (GREEN + non-regression)
```
# build + wire/dict/codegen suites (own-build gate)
cmake --build build/linux-clang-debug -j2
ctest --test-dir build/linux-clang-debug -R "dictionary|wire|codegen|nested_group|group_slice|alloc" --output-on-failure
# alloc discipline (boundary walk zero-alloc)
ctest --test-dir build/linux-clang-debug -R "alloc_guard|group_entry_alloc_gate" --output-on-failure
# C-ABI freeze unchanged
ctest --test-dir build/linux-clang-debug -R "abi_symbol_golden|capi_freeze" --output-on-failure
```
Expected: SC-001 real-dict MassQuote 2-entry witness GREEN; `nested_group_read_test.cpp:353` un-skipped GREEN; Defect-A and Defect-B guards mutation-proven RED on pre-fix (SC-003); single-entry / count-of-zero / flat / benign-reuse regressions GREEN; alloc gate GREEN; C-ABI golden + freeze unchanged.

## 4. Close-out
- Retire the two `L-062` rows in `spec/behaviors-and-limitations.md` with the 063 evidence PR; note the FR-002 census result (Option chosen + any documented over-extension limitation).
