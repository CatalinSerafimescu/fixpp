---
id: 001-core-decimal
title: Quickstart — Build, test, bench, ABI golden, alias switch
spec_kit_step: /plan Phase 1
last_updated: 2026-05-12
status: drafted (round-2 redraft)
---

# Quickstart — 001-core-decimal

Reproducible recipes for working on the decimal primitive end-to-end. All paths are relative to the `library/` submodule root; all commands assume cwd is the library submodule. Inherited toolchain from Phase 3 (Conan default profile + Clang 22 + Ninja + CMake ≥ 3.28 per `[const §II.1]`, `[const §III.1]`, `[const §III.2]`).

## 1. Build and run the unit tests (default-traits path)

The decimal primitive lives in `core/`; the relevant CMake target is `fixpp_core` for the library and `fixpp_core_tests` for the tests. Tier 1 entry point is the `linux-clang-debug` preset.

```bash
# Configure once (Phase 3 baseline; first-time only)
conan install . --build=missing --profile=linux-clang-debug
cmake --preset linux-clang-debug

# Build + test the decimal slice
cmake --build --preset linux-clang-debug --target fixpp_core fixpp_core_tests
ctest --preset linux-clang-debug -R '^decimal_'
```

Expected: every `decimal_*_test` target (per `plan.md §Project Structure > tests/core/`) is green. Coverage threshold for the touched files is `≥90 % line / ≥80 % branch` per `[const §IX.1]`; measured under the `linux-clang-coverage` preset.

## 2. Run all 10 test seams (seam → file map per plan.md)

Each seam has a dedicated entry point. None of them maps to "the existing `decimal_*_test.cpp`s collectively" — that was the round-1 defect closed here.

| Seam # | Entry point | Command |
|---|---|---|
| 1 | `tests/support/mock_decimal_traits.hpp` (header-only; exercised by seams #3 and downstream **2b**) | (no standalone command — exercised by `decimal_cross_traits_test`) |
| 2 | `tests/core/decimal_roundtrip_property_test.cpp` | `ctest --preset linux-clang-debug -R '^decimal_roundtrip_property_test$'` |
| 3 | `tests/core/decimal_cross_traits_test.cpp` | `ctest --preset linux-clang-debug -R '^decimal_cross_traits_test$'` |
| 4 | `tests/abi/golden/fixpp_decimal_t.abidiff` + `src/capi/decimal_assert.cpp` | Tier 2 abidiff (see §3) + compile-time static_asserts (run on every build) |
| 5 | `bench/core/decimal_bench.cpp` | `cmake --build --preset linux-clang-debug --target decimal_bench && ./build/linux-clang-debug/bench/core/decimal_bench --benchmark_filter=Decimal` |
| 6 | `tools/check_alloc.py` + `tests/alloc_guard/decimal_alloc_guard_test.cpp` | `python tools/check_alloc.py --target decimal_alloc_guard_test` (wraps `mallocnesia`) |
| 7 | `tests/fuzz/fuzz_decimal_parse.cpp` | `cmake --build --preset linux-clang-debug --target fuzz_decimal_parse && ./build/linux-clang-debug/tests/fuzz/fuzz_decimal_parse -max_total_time=600` |
| 8 | `tests/oracle/decimal_compare_oracle_test.py` | `pytest tests/oracle/decimal_compare_oracle_test.py -q` |
| 9 | `tests/link/decimal_alias_mismatch_test.cmake` | `cmake -S tests/link -B build/link_test && cmake --build build/link_test --target decimal_alias_mismatch_test`. **Expected: link failure with `decimal_alias_sentinel<...>::tag` unresolved.** Suite parser asserts the failure is the expected one (not a stray compile error). |
| 10 | `tests/core/decimal_reserved_tolerance_test.cpp` | `ctest --preset linux-clang-debug -R '^decimal_reserved_tolerance_test$'` |

## 3. Verify the C-ABI layout golden (Tier 2)

The `fixpp_decimal_t` shape is frozen for `FIXPP_C_ABI_VERSION_MAJOR == 1` per `[const §X.1]`. The Tier 2 abidiff golden lives at `tests/abi/golden/fixpp_decimal_t.abidiff`. Run on demand (typically nightly or on the `windows` PR label per `[const §IX.6]`):

```bash
# Regenerate the abidiff dump from the currently-built libfixpp_core.so
abidiff \
  --leaf-changes-only \
  --suppressions tests/abi/abidiff.suppr \
  tests/abi/baseline/libfixpp_core.so \
  build/linux-clang-release/lib/libfixpp_core.so \
  > tests/abi/golden/fixpp_decimal_t.abidiff
```

Drift in this golden is a Tier 2 hard-fail per `[const §IX.5]` — every change must be paired with an explicit MAJOR bump on `FIXPP_C_ABI_VERSION_MAJOR`. The compile-time `static_asserts` in `src/capi/decimal_assert.cpp` catch any layout drift at build time, before abidiff runs.

## 4. Benchmark and verify the latency NFRs

Targets (per spec.md §6 / 2a §6.5, Linux/Clang/x86_64, warm cache, 5-digit mantissa):
- `parse` ≤ 50 ns median
- `format` ≤ 30 ns median
- `compare` ≤ 20 ns median

```bash
cmake --build --preset linux-clang-release --target decimal_bench
./build/linux-clang-release/bench/core/decimal_bench \
  --benchmark_filter='^(BM_decimal_(parse|format|compare))$' \
  --benchmark_repetitions=10 \
  --benchmark_format=json \
  --benchmark_out=bench/results/decimal.json

# Compare against bench/baselines/decimal_baseline.json (±5 % per [const §VIII.2])
python tools/bench_compare.py \
  --baseline bench/baselines/decimal_baseline.json \
  --current  bench/results/decimal.json
```

## 5. Sanitizers + property oracle (Tier 1, every PR)

```bash
# ASan + UBSan + TSan — every PR per [const §IX.2]
ctest --preset linux-clang-asan   -R '^decimal_'
ctest --preset linux-clang-ubsan  -R '^decimal_'
ctest --preset linux-clang-tsan   -R '^decimal_'

# Python Decimal property oracle (Tier 1 — promoted from Tier 2 per 2a §9 seam 8)
pytest tests/oracle/decimal_compare_oracle_test.py -q
```

## 6. Swap the engine-wide decimal alias (D-7 build-time customization)

For consumers who need a wider mantissa (`decimal128`, `boost::multiprecision`, custom). The library MUST be rebuilt with the chosen alias — link-time guard catches mismatches per AC-B3.

1. Author a user-side header that declares the chosen `T` and any `decimal_traits<T>` specialization:

   ```cpp
   // my_project/fixpp_decimal.hpp
   #include <boost/multiprecision/cpp_dec_float.hpp>
   namespace my { using decimal128 = boost::multiprecision::cpp_dec_float_50; }
   namespace fixpp::core {
       template <> struct decimal_traits<my::decimal128> { /* ... */ };
   }
   ```

2. Configure CMake with the user header + alias macro:

   ```bash
   cmake --preset linux-clang-release \
     -DCMAKE_CXX_FLAGS="\
       -DFIXPP_DECIMAL_USER_HEADER=\\\"my_project/fixpp_decimal.hpp\\\" \
       -DFIXPP_DECIMAL_T=my::decimal128"
   ```

   **Shell quoting note:** the inner `"..."` around the header path is required by the C preprocessor (it expands to `#include "my_project/fixpp_decimal.hpp"`). On bash, the escape pattern is `\\\"` (backslash-quote, backslash-quote) — the shell strips one level, leaving `\"` for CMake's `-D`, which strips to `"`. On zsh and Windows pwsh, prefer the explicit-flag-file form (`@flags.txt` with one quoted directive per line) to avoid per-shell quoting drift.

3. Build, test, and verify the link-time guard didn't fire:

   ```bash
   cmake --build --preset linux-clang-release
   ctest  --preset linux-clang-release -R '^decimal_alias_test$'
   ```

4. Verify the C-ABI shape **did not change** (it stays PoD `(int64, int8, int8[7])` per `[const §X.3]` regardless of the C++ alias):

   ```bash
   abidiff \
     tests/abi/golden/fixpp_decimal_t.abidiff \
     <(abidump build/linux-clang-release/lib/libfixpp_core.so)
   ```

   Expected: identical. The C-ABI is alias-agnostic by design.

## 7. Allocation discipline (Linux)

`tools/check_alloc.py` wraps `mallocnesia` to instrument the parse-format loop. Any allocation between `decimal<T>::parse(...)` and `decimal<T>::format(...)` fails CI per `[const §VIII.5]` and `[const §XV.1]`.

```bash
# Configure + build the alloc_guard test
cmake --build --preset linux-clang-debug --target decimal_alloc_guard_test

# Run under mallocnesia interceptor
python tools/check_alloc.py \
  --target decimal_alloc_guard_test \
  --binary build/linux-clang-debug/tests/alloc_guard/decimal_alloc_guard_test \
  --max-allocs 0
```

**Windows gap** documented per `[const §IX.6]`: no `mallocnesia` equivalent of comparable fidelity. Windows alloc-discipline coverage is a Tier 2 v1.x deferred item (research.md D-10).

## 8. End-to-end Gate A round 2 dry run

Before submitting to Gate A round 2 (`/gate-a 001-core-decimal`), verify locally:

```bash
# 1. All Tier 1 presets pass
for preset in linux-clang-debug linux-clang-release linux-clang-asan linux-clang-ubsan linux-clang-tsan linux-clang-coverage; do
  cmake --build --preset "$preset" --target fixpp_core fixpp_core_tests || exit 1
  ctest --preset "$preset" -R '^decimal_' || exit 1
done

# 2. Static analysis Tier 1 ([const §IX.4])
clang-tidy --config-file=.clang-tidy include/fixpp/core/decimal*.hpp src/core/decimal.cpp src/capi/decimal*.cpp
cppcheck --enable=all --suppress=missingIncludeSystem include/fixpp/core/ src/core/ src/capi/
iwyu_tool.py -p build/linux-clang-debug include/fixpp/core/ src/core/

# 3. Property oracle (Tier 1)
pytest tests/oracle/decimal_compare_oracle_test.py -q

# 4. Fuzzer smoke (Tier 1 — ≥10 min on the PR, longer overnight on main)
./build/linux-clang-debug/tests/fuzz/fuzz_decimal_parse -max_total_time=600

# 5. Bench against baselines (Tier 1)
./build/linux-clang-release/bench/core/decimal_bench --benchmark_format=json > bench/results/decimal.json
python tools/bench_compare.py --baseline bench/baselines/decimal_baseline.json --current bench/results/decimal.json
```

Then submit:

```bash
# From the parent repo:
/gate-a 001-core-decimal
```

Per `gate-a.md` skill: Codex review → Opus adversarial → up to 2 rewrites. Reviews land at `research/G19-fix-fpml-iso20022/research/reviews/codex_001-core-decimal_gate_a_review.md` (round 2; round-1 was renamed to `..._v1_review.md`).
