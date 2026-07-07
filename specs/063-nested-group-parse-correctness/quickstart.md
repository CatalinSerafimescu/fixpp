# Quickstart: Nested Group-Parse Correctness (063)

How to reproduce the defects, run the census that decides the fix option, and validate the fix. Run from the library submodule root.

## 0. Reproduce (pre-fix, RED)
- **Defect A** (membership): un-skip / add a witness that loads real `FIX44.xml` → `Dictionary::as_table_view()` → parses a MassQuote and reads `quote_sets()[s].quote_entries()`. Pre-fix: `.size() == 0` (first-seen-wins picks QuotCxlEntriesGrp; `group_member_fn(295,299)=false`).
- **Defect B** (extent): `tests/codegen/nested_group_read_test.cpp:353` `NestedQuoteEntriesPerInstancePrices` is currently `GTEST_SKIP()`'d — its MassQuote QuoteSet[0] has 2 QuoteEntries; pre-fix the outer slice truncates at the 2nd → `quote_entries().size()==1`.

## 1. Run the census (FR-002 completeness — enumerates + guards every collision)
The loader-faithful (component-expanding) reused-tag census over all nine XMLs **incl. FIXT.1.1**. Prototype: `scratchpad/census2.py` (already reproduces FIX44=12 colliding tags, all parent-ambiguous; 295 → parent-0 QuotCxlEntriesGrp vs parent-296 QuotEntryGrp). The implementation-grade census enumerates every reused-with-differing-membership tag so each gets a discriminating guard. It is a **completeness aid, NOT an option gate**: Gate-A Round 1 resolved the design to **Option A (exact context-scoped membership)**; a declaration-order census cannot bound the order-independent wire (B-004-1), so it never adjudicated soundness.

## 2. Apply the fix (Option A)
- **Defect A / Option A**: `src/dictionary/xml_loader.cpp:486` — replace first-seen-wins with membership registered under the context key `(msg_type, bounded parent-no_tag-path, no_tag)`: expand shared `<component>`s per referencing message + accumulate the full parent-path; re-key `table_view` (`:215,218`) accordingly; thread the context arg through `group_member_fn_t` (`offset_table.hpp:29`) → `parser.hpp:271,484-494` → `entry_context` (`group_view.hpp:31-47`) → `validator.hpp:219`. `group_first` (delimiter) resolved under the same context key.
- **Defect B**: `src/wire/offset_table.cpp:402-482` — replace the flat `seen_in_instance` walk (`:450-459`) with a depth-bounded (private `K=16`), **allocation-free** nested-count-aware boundary walk driven by the exact Option-A membership (on a member tag that is a `NumInGroup` count present as a nested group in this context, read its count from `entries_`, consume exactly `declared` instances or fail closed, then resume the outer boundary). 062's nested descent (`:552`/`:582`) now carries the context field.

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
Expected: SC-001b real-dict MassQuote 2-entry witness GREEN; SC-001a `nested_group_read_test.cpp:353` un-skipped GREEN; Defect-A and Defect-B guards mutation-proven RED on pre-fix (SC-003); single-entry / count-of-zero / flat / benign-reuse regressions GREEN; alloc gate GREEN; C-ABI golden + freeze unchanged.

## 4. Close-out
- Retire the two `L-062` rows in `spec/behaviors-and-limitations.md` with the 063 evidence PR; note the FR-002 census result (the enumerated collision set + per-collision guard coverage under Option A).
