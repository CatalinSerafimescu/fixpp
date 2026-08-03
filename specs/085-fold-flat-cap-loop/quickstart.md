# Quickstart: validating 085-fold-flat-cap-loop

**Branch**: `085-fold-flat-cap-loop` · **Date**: 2026-08-03

This feature's deliverable is a **removal**, so "it works" cannot be shown by exercising a new capability. Validation is therefore three separate claims, each with its own procedure:

1. **Nothing changed** on the dictionary path (SC-001, SC-002, SC-005).
2. **The cap still fires**, on both paths, and the new pin is load-bearing (SC-003, SC-004, SC-004a).
3. **No regression** in performance or hygiene (SC-006 – SC-008).

Run them in that order. Claim 2 is the one that can silently false-pass, so it carries the mutation step.

---

## Prerequisites

```bash
cd research/G19-fix-fpml-iso20022/library     # Spec-Kit + build root
cmake --preset dev-clang                       # or the preset used locally
cmake --build --preset dev-clang -j2           # -j2: wider parallelism OOM-kills the session
```

> Build with **`-j2`**. Wide parallel C++ builds in this tree have OOM-killed the session before.

---

## 1. Nothing changed on the dictionary path

### 1a. Census and split pins (SC-001)

```bash
ctest --preset dev-clang -L dictionary -R DelimiterCensus
ctest --preset dev-clang -L wire       -R TypedReadSplitAgreement
```

**Expected**: `DelimiterCensus.RedCountsReconcileWithSpecBaseline` passes with the **same baseline counts as `main`** — no fixture edit, no baseline update. All **seven** `TypedReadSplitAgreement.*` tests pass.

**A fixture or baseline edit made to turn these green is a failure of SC-001, not a fix.** Their whole purpose is to be insensitive to this change.

### 1b. Whole-suite equivalence (SC-002)

```bash
ctest --preset dev-clang -L wire
ctest --preset dev-clang -L capi -R MessageReadGroup
```

**Expected**: identical pass set to `main`. In particular the ~16 dict-free `OffsetTable{frame, mr}` tests stay green — including `offset_table_test.cpp:150-157`, which calls `group(453)` dict-free and asserts its extent. Those are what make C-3's byte-identical claim checkable rather than asserted.

### 1c. One traversal (SC-005)

Discharged by **source inspection**, not a command:

```bash
git diff main -- src/wire/offset_table.cpp
```

**Expected shape**: the loop disappears from the dictionary branch and reappears **unmodified** inside the `else`. The dictionary branch gains the FR-002 comment. If the diff shows the loop *rewritten* rather than moved, FR-001a is violated even if every test is green.

---

## 2. The cap still fires — and the pin is load-bearing

### 2a. Dictionary path (SC-003)

```bash
ctest --preset dev-clang -L wire -R 'DoSCapPerInstance'
```

**Expected**: both pre-existing pins pass — `DoSCapPerInstanceRejectsOversizedSingleInstance` (breach → `wire_group_too_large`) and `DoSCapPerInstanceAllowsAggregateOverCap` (aggregate over cap, no single instance over it → success).

### 2b. Dict-free path (SC-004)

```bash
ctest --preset dev-clang -L wire -R 'DictFree.*Cap'
```

**Expected**: the new pin fails the over-cap frame with `wire_group_too_large`, and its bracketing companion succeeds on the **same frame** with `max_group_entries_per_instance` raised above it.

> The frame needs **both** dict-free construction **and** a tightened `Config` — e.g. `tight_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3}`. Under default `Config` this branch is arithmetically unreachable (`research.md` R-2), which is exactly why no such test existed before.

### 2c. Prove the pin RED (SC-004a, FR-005a(i))

**This step is mandatory and is the one that cannot be skipped.** A cap pin never observed RED proves nothing.

```bash
# 1. Delete the cap check inside the dict-free branch of OffsetTable::group()
#    (the `if (inst_count > cfg_.max_group_entries_per_instance) return ...`)
# 2. Rebuild and run ONLY the new pin:
cmake --build --preset dev-clang -j2
ctest --preset dev-clang -L wire -R 'DictFree.*Cap' --output-on-failure
# 3. Capture the failure output verbatim.
# 4. Restore the check; rebuild; confirm green.
```

**Expected**: step 2 fails with the group request unexpectedly succeeding. Record the mutation applied and the failure output in `.specify/decisions/085-fold-flat-cap-loop-verify.md` and cite it from the test's comment block.

**If the pin stays GREEN with the check deleted, stop.** The test is not exercising the branch — most likely the frame is not reaching the dict-free path, or `Config` was not tightened.

---

## 3. No regression

### 3a. Benchmark (SC-006)

```bash
cmake --build --preset bench-release -j2
./build/bench-release/bench/wire/typed_read_group_bench \
    --benchmark_filter='BM_TypedReadGroup_(Flat2|ModeC2|ModeC8)'
```

Run on `main` and on the branch, **same machine, same session**, and put both sets of numbers in the PR body.

**Expected**: within the ±5% budget (Article VIII §2). A null result is a **pass** — the bench exists to prove no regression, not to claim a win (A-004). Do not add a bench case tuned to make the removal look good, and do not update `bench/baselines/`: this is not an intentional perf change.

### 3b. Sanitizers and static analysis (SC-008)

```bash
ctest --preset asan -L wire
ctest --preset ubsan -L wire
ctest --preset tsan -L wire
```

**Expected**: clean. No new constructs are introduced, so any finding here is a signal that the relocation was not verbatim.

### 3c. ABI hygiene (SC-007)

**Expected**: zero change to exported symbols, public headers, error enum values and the C-ABI version. `include/fixpp/wire/offset_table.hpp` should be **untouched** by this feature.

---

## 4. Documentation obligations

Not runnable, but they gate close-out:

| Check | Requirement |
|---|---|
| L-063-4 records leg 2 DELIVERED, leg 1's descope evidence intact | FR-006, SC-009 |
| Both surviving flat sites named — **with their differing delimiter sources** (wire vs the per-context store) | FR-007, FR-007a |
| References anchored by function and role; line numbers stamped as-of the merge sha; historical brackets byte-unchanged | FR-007b |
| Limitation row cites **fixpp#220** and states the default-config unreachability | FR-003a, SC-010 |
| The stale waiver at `tests/wire/offset_table_error_path_test.cpp:10-14` repaired — its claim that `group()`'s `err_group_too_large` is "provably unreachable" is now false for `:577` and, after this feature, for the relocated branch too | research.md R-3 |

---

## Failure triage

| Symptom | Likely cause |
|---|---|
| `TypedReadSplitAgreement.*` red | The relocation altered the dictionary path. Check the loop was removed, not the `consume_group_extent` call |
| A dict-free test red | The relocation was not verbatim — re-read C-3 |
| New dict-free pin green under mutation | Frame is not reaching the dict-free branch, or `Config` was not tightened |
| `DelimiterCensus` red | Something re-pointed a delimiter. Nothing in this feature should touch delimiter resolution — see C-1 |
| Bench regressed >5% | Unexpected for a removal. Suspect a build/config difference between the two runs before suspecting the change |
