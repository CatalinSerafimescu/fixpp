# Quickstart: verify the 065 C-ABI nested-read fix

All commands run with cwd inside the library submodule:
`research/G19-fix-fpml-iso20022/library`.

## 1. Reproduce the defect (pre-fix, RED)

The witness already exists, `GTEST_SKIP`'d, at
`tests/capi/message_read_test.cpp::MessageReadGroup.NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember`.
It builds an outer group `453` entry with a multi-entry nested group `539` followed by a trailing outer member `999`, and asserts:

- `fixpp_group_get_field_string(nested, 1, 999, …)` → `FIXPP_ERR_TAG_NOT_FOUND` (the discriminator).
- `fixpp_group_get_field_string(outer, 0, 999, …)` → `FIXPP_ERR_OK` / `"TRAIL"` (trailing member still reachable at the outer index).
- `nested_count == 2`; each nested instance's own `524`/`525` values correct.

**Mutation-proof it is RED on pre-fix code**: temporarily remove the `GTEST_SKIP` line only (no source fix), build, run — the `999`-on-`nested[1]` assertion must FAIL (returns `FIXPP_ERR_OK`/`"TRAIL"`), proving the witness discriminates the defect.

## 2. Apply the fix, then un-skip (GREEN)

After the implementation (delete positional scanner → `nested_group_slices`; cursor `group_ctx`; presence probe), delete the `GTEST_SKIP` + escalation comment. Then:

```bash
cmake --preset linux-clang-debug
cmake --build build/linux-clang-debug -j2 --target message_read_test
ctest --test-dir build/linux-clang-debug -R 'MessageReadGroup' --output-on-failure
```

Expect: `NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember` PASS, and every other `MessageReadGroup.*` / `MessageRead.*` still PASS (SC-002), including `NestedGroupAbsentTag` (→ `TAG_NOT_FOUND`) and `NestedGroupEmptyGroupCountLastField` (→ `OK`, nc=0).

## 3. Full C-ABI suite

```bash
ctest --test-dir build/linux-clang-debug -R 'capi|message_read' --output-on-failure
```

## 4. C-ABI freeze unchanged (SC-003)

```bash
ctest --test-dir build/linux-clang-debug -R 'abi|freeze' --output-on-failure
# and/or the repo's symbol/freeze check:
sha256sum -c tools/capi_freeze.sha256    # or the project's freeze-verify target
```

Expect: no delta in `tests/abi/golden/fixpp_capi_symbols.txt`; `capi_freeze.sha256` verifies.

## 5. Sanitizers + alloc gate (Tier-1 mirror — done by /speckit-verify)

```bash
# ASan/UBSan/TSan matrix over the C-ABI + wire tests; alloc-discipline gate.
# Confirms the reused nested sub-table lifetime + zero-global-heap (SC-004).
```

`/speckit-verify 065-cabi-nested-group-membership` runs the feature-appropriate slice of this and writes `.specify/decisions/065-cabi-nested-group-membership-verify.md`.

## 6. C++ ≡ C-ABI equivalence (SC-005) — MANDATORY (FR-011)

This witness is **mandatory**, not optional. Add a new test to `tests/capi/message_read_test.cpp` that:

- Builds the dictionary from the **real FIX44 dictionary** via `Dictionary::as_table_view()` (NOT a hand-built single-`msg_type` `table_view`) — this is what pins that real msg_type/parent-path context is threaded correctly on the C-ABI path (the empty-`msg_type` class that regressed in 063 Gate-B RC#1).
- Parses a FIX44 `ExecutionReport` carrying `NoLegs(555)` → `NoLegSecurityAltID(604) ×2` (multi-entry nested group) → trailing `LegQty(687)`.
- Asserts the C-ABI reads `FIXPP_ERR_TAG_NOT_FOUND` for `687` on the **last** nested instance (the discriminator), and `FIXPP_ERR_OK` + the correct value for `687` at the **outer** index.
- Asserts **C-ABI ≡ typed** equivalence.

**Writability caveat (the typed path cannot be asked for a non-member tag):** the generated typed nested flyweight has **no accessor for `687`** (it is not a member of the nested group `G_604`), so "C-ABI == typed for the trailing tag" is not directly expressible. Express the equivalence instead as: (a) the genuine nested member values + the nested count/extent agree between the two paths, AND (b) the trailing tag is **absent** from the typed nested entry's corrected extent — probe it via the `field_value()` escape hatch (post-fix, L-062-3's whole-slice first-occurrence scan over the corrected entry slice excludes `687`, so `field_value(687)` on the typed nested entry is absent, matching the C-ABI `TAG_NOT_FOUND`). Do NOT assert equivalence by asking the typed nested accessor for `687` directly.

(Precedent for a real-XML C-ABI witness in this file: `TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent`, `message_read_test.cpp:1735`.)
