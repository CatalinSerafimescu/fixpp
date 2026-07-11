# Quickstart: Grouping One Module

The repeatable procedure applied per module (FR-010, one at a time, descending disk order). Reference implementation: `tests/dictionary/CMakeLists.txt`. **Authoritative pattern: `IMPLEMENTATION-PROCEDURE.md`** — whole-binary `add_test`, not `gtest_discover_tests` (dropped after the pilot: per-case discovery regressed serial ctest 5.77×). The steps below are aligned to that pattern.

## 0. Preconditions
- Tree clean; all 8 presets built at the current baseline.
- `../research/test-grouping-baseline/baseline-2026-07-10.csv` present (the *before* reference). **All paths below are from the `library/` submodule cwd; the baseline lives in the PARENT repo (`../research/…`), never inside `library/` (gitignored + CI-guarded, Art XV §18).**

## 1. Census the module
For `tests/<module>/`, classify each `.cpp`:
- **Standalone** if it matches the FR-002/D3 taxonomy — grep signals:
  - `git grep -l 'operator new\|mallocnesia\|alloc_guard'` (allocation-counting)
  - OOM-injection / global new-handler
  - per-test-heterogeneous `ENVIRONMENT` / `TSAN_OPTIONS` / suppression files
  - top-level `abort()`/`_exit()` (NOT `EXPECT_DEATH` — those group, D3)
  - per-target `target_compile_definitions` variants of one `.cpp`
  - completeness gate with a precise `-L` feature label
  - genuinely-concurrent / global-singleton-freshness: spawns `std::thread`/`std::jthread`/`std::async`, or mutates a function-local `static`/process-global registry read by other `TEST`s in the file — no reliable grep signal; flag for manual review when suspected
  - selected by `ctest -R <target>` in any quickstart/tasks doc
- Else **groupable**.

## 2. Bucket the groupable set (D4)
Partition by key `(sorted link-libs, sorted labels)`. Each partition → one whole-binary target — **NO `gtest_discover_tests`, NO `include(GoogleTest)`** (per-case discovery regresses serial ctest, see above):
```cmake
add_executable(<module>_<bucketkey>_tests  a.cpp b.cpp c.cpp)
target_link_libraries(<module>_<bucketkey>_tests PRIVATE
  <union-of-member-link-libs>  GTest::gtest GTest::gtest_main)
target_include_directories(<module>_<bucketkey>_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
# homogeneous compile-defs / ENVIRONMENT only:
target_compile_definitions(<module>_<bucketkey>_tests PRIVATE <shared-defs>)
add_test(NAME <module>_<bucketkey>_tests COMMAND <module>_<bucketkey>_tests)
set_tests_properties(<module>_<bucketkey>_tests PROPERTIES
  LABELS "<shared-labels>"                       # the selector for ctest -L (FR-003)
  ENVIRONMENT "<shared-env-if-any>")              # only if homogeneous (D3)
```
Leave standalone tests exactly as they are (`add_executable` + `add_test(NAME ...)` + `set_tests_properties`).

## 3. Resolve ODR collisions (D5/FR-012)
If a bucket fails to link (duplicate symbol) or gtest reports a duplicate `Suite.Name`:
- duplicate **helper/global** → make it `static`/anon-namespace or rename in the test source → keep grouped.
- duplicate **`Suite.Name`** → cannot rename (audit key) → move that `.cpp` to standalone.
Record the action in the disposition ledger.

## 4. Build + verify (all 8 presets, serial, -j2)
> **Resource gate (Article XVII §7):** an AI agent MUST `AskUserQuestion` before running these builds.
```bash
for p in linux-clang-debug linux-clang-asan linux-clang-tsan linux-clang-ubsan \
         linux-clang-coverage linux-gcc-release linux-clang-release linux-clang-libc++; do
  cmake --build build/$p -j2 && ctest --test-dir build/$p --output-on-failure
done
```
PASS = every preset green, no new sanitizer finding (FR-006), and the built/run gtest-case set identical to before (FR-007).

## 5. Preservation checks (FR-004 / SC-003 / SC-004)
- Coverage-index + completeness audits: unchanged & green (they key on `.cpp` stem + `Suite.Name`).
- Selectability: run `ctest -L <label>` — same logical case set as before. Grouped binaries no longer have a per-`.cpp` target name, so `-L <label>` is the stable selector going forward; do not rely on `ctest -R <exe-name>` (only genuine standalone targets keep an exact-match name, per §1's `-R <target>` carve-out).

## 6. Measure & record (FR-008)
```bash
bash ../research/test-grouping-baseline/inventory.sh /tmp/after.csv
# diff this module's rows vs baseline-2026-07-10.csv → record delta
```
**Pilot (`dictionary`) additionally** (FR-009/SC-005):
- ctest wall-time: `time ctest --test-dir build/<p> -L dictionary` before vs after; assert ≤10%/preset.
- incremental relink: `touch` one grouped `.cpp`; `time cmake --build build/<p> --target <bucket> -j2`; compare to pre-grouping single-binary relink.

## 7. Disposition ledger (FR-011)
Every `.cpp` in the module recorded as `grouped:<bucket>` or `standalone:<reason>`. Sum == module's `.cpp` count. Then advance to the next module.

## Done-definition (per module)
8 presets green ∧ disk delta recorded ∧ gates+selectability preserved ∧ 100% dispositioned.
