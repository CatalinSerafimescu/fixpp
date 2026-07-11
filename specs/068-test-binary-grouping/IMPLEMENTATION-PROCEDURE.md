# Implementation Procedure — whole-binary `add_test` grouping (068)

**Authoritative pattern for the rollout.** Supersedes the `gtest_discover_tests`
per-case approach in quickstart.md (dropped after the pilot: per-case discovery
= 1 process launch **per case** → 5.77× serial ctest regression; whole-binary
`add_test` = 1 launch per bucket → *faster* than status quo + the disk win).
Decision recorded in measurements.md; FR-001 amended to per-**binary** entry.

## The pattern

For each module `tests/<mod>/`:

### 1. Census every `.cpp` by MECHANISM (not filename)

**STANDALONE** (keep its own `add_executable` + `add_test(NAME <tgt> COMMAND <tgt>)`
+ its exact current name/labels/properties) iff it matches any:
- **global** allocation-counting: in-TU global `operator new`/`set_new_handler`
  counter, `mallocnesia` `LD_PRELOAD` gate, `alloc_guard`. Grep `operator new` /
  `set_new_handler` / global `g_alloc`/`alloc_count`.
- **global** OOM injection: process-wide new-handler / injection toggle.
- TSan-specific target, or a **heterogeneous** per-test `ENVIRONMENT`/
  `TSAN_OPTIONS`/suppression (a *homogeneous* ENV shared by the whole bucket may
  ride the grouped binary).
- top-level `abort()`/`_exit()` / link-mode override (NOT gtest fork
  `EXPECT_DEATH` — that groups).
- genuinely concurrent / global-singleton-freshness: spawns
  `std::thread`/`std::jthread`/`std::async`, or mutates a file-scope
  `static`/process-global registry read by other `TEST`s.
- per-target `target_compile_definitions` variants of one `.cpp` (`_wide` vs `_portable`).
- exact-set completeness gate with a precise `-L` label.
- a **live** procedure selects it by exact `ctest -R <target-name>` (a
  `$<TARGET_FILE:x>` reference, e.g. a mallocnesia sidecar add_test → the target
  must keep its name).
- label-heterogeneous sole member of its label class (bucket-of-1 = no win).

**KEY DISCRIMINATOR:** a *local* `std::pmr::memory_resource` subclass (failing/
counting) constructed per-test and passed explicitly is **isolation-safe →
GROUPABLE**, even in a file named `*_oom*`/`*alloc*`. Only *global* mechanisms
force standalone. (Pilot proof: `oom_injection`, `reify_oom` group; `pmr_allocation`,
`group_context_lookup_alloc_gate` — global counters — stay standalone.)

Else **GROUPABLE**.

### 2. Bucket the groupable set by `(sorted link-libs, sorted labels)`

Each partition → one whole-binary target. Union member link-deps.

```cmake
add_executable(<mod>_<bucket>_tests  a.cpp b.cpp c.cpp ...)
target_link_libraries(<mod>_<bucket>_tests PRIVATE
  <union-of-member-link-libs>  GTest::gtest GTest::gtest_main)
target_include_directories(<mod>_<bucket>_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests" <others>)
target_compile_definitions(<mod>_<bucket>_tests PRIVATE <shared-defs-only>)   # homogeneous only
add_test(NAME <mod>_<bucket>_tests COMMAND <mod>_<bucket>_tests)
set_tests_properties(<mod>_<bucket>_tests PROPERTIES LABELS "<shared-label>")  # + ENVIRONMENT/TIMEOUT if homogeneous
```

- **NO `gtest_discover_tests`, NO `include(GoogleTest)`, NO `DISCOVERY_TIMEOUT`.**
- Bucket must be **label-homogeneous** (FR-003) and share link-deps.
- Preserve every member's TIMEOUT / ENVIRONMENT / special link dep on the bucket,
  else the member stays standalone (FR-005).
- Keep conditional blocks (e.g. `if(TARGET fixpp_codegen_generate)`) so the built
  set is unchanged (FR-007). Codegen-dependent tests form their own bucket inside
  the guard, with `add_dependencies(<bucket> fixpp_codegen_generate)` + the
  generated include dir.

### 3. ODR pre-check (before building — cheap greps)

- `grep -lE 'int main\s*\(' <grouped files>` → any own `main()` collides with `gtest_main`.
- duplicate `TEST(Suite,Name)` across the bucket → rename the case is NOT allowed
  (FR-004 — Suite.Name is an audit key); carve that `.cpp` to standalone.
- duplicate non-`static`/non-anon-namespace file-scope helper/global across the
  bucket → make it `static`/anon-ns **in test source** (FR-012), else carve out.
- shared support headers: fine if `#pragma once` + `inline`/class-only.

### 4. Disposition ledger (FR-011)

Append a `## Module: <mod>` section to `dispositions.md`: every `.cpp` as
`grouped:<bucket>` or `standalone:<reason>`; sum == module `.cpp` count. Note any
`-R`-by-name idiom → `-L <label>` replacement.

### 5. Orphan cleanup (after regrouping a module, per build dir)

`ninja -t cleandead` misses these; remove the now-dead per-`.cpp` targets:
```bash
valid="<space-separated current target names>"
for p in <presets>; do
  for d in build/$p/tests/<mod>/CMakeFiles/*.dir; do
    t=$(basename "$d" .dir)
    case " $valid " in *" $t "*) ;; *) rm -rf "$d"; rm -f "build/$p/bin/$t";; esac
  done
done
```

## Build / verify (WSL2)

- **-j2 max** for compiles (OOM cap). Use a **zsh array** for multi-target
  builds: `tgts=(a b c); cmake --build build/$p -j2 --target $tgts` (unquoted
  `$var` does NOT word-split in zsh).
- Reconfigure after CMakeLists change: `cmake -S . -B build/<preset>`.
- Green = every preset builds + `ctest -L <mod>` all pass, same gtest cases as
  before, no new sanitizer finding. Grouped binaries run all their cases in ONE
  process (whole-binary add_test), so `-L <mod>` shows fewer *entries* but the
  same gtest case set.
- 8 presets: linux-clang-{debug,asan,tsan,ubsan,coverage,release,libc++},
  linux-gcc-release. **libc++ local caveat:** binaries resolve the system
  `libc++.so.1` (too old for `std::pmr` RTTI) — export
  `LD_LIBRARY_PATH=/opt/llvm22/lib/x86_64-unknown-linux-gnu:$LD_LIBRARY_PATH`
  for BOTH build (post-build steps) and ctest. Pre-existing, grouping-independent,
  CI-green.

## Invariants (must hold)

- No `src/`/`include/`/ABI/runtime/codegen change — only `tests/**/CMakeLists.txt`
  (+ test source ONLY on an ODR rename).
- `ctest -L <label>` selects the same logical set. coverage-index + completeness
  audits key on `.cpp` stem + gtest `Suite.Name` — never rename either.
- Reference precedent already in-tree: `tests/core/CMakeLists.txt`.
