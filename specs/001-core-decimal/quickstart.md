# quickstart.md — 001-core-decimal

> **Phase 1 output for `/speckit-plan`.** A getting-started for engineers picking up this feature: build, run unit tests, run the bench, verify the ABI golden, and switch the decimal alias for a wider-trait build. Mirrors the surface in [`spec.md`](./spec.md) §4 and the contracts in [`contracts/`](./contracts/).

## 0. Prerequisites

Working tree must already pass the Phase 3 baseline (Tier 1 CI green; library submodule pinned at `6e1edd0` or later on `main`). Per `[const §XVII §7]`, the local toolchain is **Clang 22**, which the user already has installed (matches the Conan profile pin).

```bash
# inside the library submodule
cd research/G19-fix-fpml-iso20022/library

# verify Conan profile is initialized (Phase 3 step — should be a no-op)
conan profile show linux-clang-debug
```

## 1. Build (default trait — `pod_decimal`)

> ⚠️ Per `[const §XVII §7]` resource gate: when an AI agent needs to run a local build, it MUST surface an `AskUserQuestion` first. The user approves the build before it runs. Engineers running interactively are unaffected.

```bash
# Conan install (debug profile)
conan install . --profile linux-clang-debug --build=missing -of build/linux-clang-debug

# CMake configure + build
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug

# unit tests
ctest --preset linux-clang-debug --output-on-failure -R 'decimal_'
```

Expected: every `decimal_*_test` passes (parse, format, compare, cross_traits, alias, layout, reserved). The Python oracle test runs separately in step 4.

## 2. Run the bench (NFR check)

```bash
# release-mode build needed for representative numbers
cmake --preset linux-clang-release
cmake --build --preset linux-clang-release --target decimal_bench

# run with a stable CPU pin
taskset -c 2 ./build/linux-clang-release/bench/core/decimal_bench \
    --benchmark_min_time=2s --benchmark_repetitions=3
```

NFR bars (default trait, x86_64, 5-digit mantissa, warm cache; per `spec.md §6` and `2a §10 Q4`):

| Op | Median target | Regression bar |
|---|---|---|
| `parse` | ≤ 50 ns | ±5 % vs `bench/baselines/decimal.json` |
| `to_chars` | ≤ 30 ns | ±5 % |
| `compare` | ≤ 20 ns | ±5 % |

If the first `/implement` close lands ≤ 2× the targets, that's a TODO not a blocker per `2a §10 Q4`; the bar may be revised after the first wire-layer integration.

## 3. Verify the ABI golden

```bash
# Tier 2 (manual / nightly) — only run after first /implement close
./tools/abidiff_golden.sh tests/abi/golden/fixpp_decimal_t.abidiff \
    build/linux-clang-release/lib/libfixpp.so
```

Expected: **clean diff**. Any drift is a Tier 2 hard-fail per `2a §9 seam #4`.

## 4. Run the Python `Decimal` oracle (Tier 1, seam #8)

```bash
# Conan-installed manylinux_2_28_x86_64 cp310 wheel (Phase 3 baseline already
# builds bindings/python/_fixpp.so). The oracle test compares fixpp_decimal_compare
# against Python's decimal.Decimal on every generated pair.
pytest bindings/python/tests/test_decimal_oracle.py -v
```

The test generates 10⁴ canonical-domain pairs and asserts the C-ABI compare agrees with the Python oracle.

## 5. Run the fuzz harness (seam #7)

```bash
# Conan profile linux-clang-asan + libFuzzer
cmake --preset linux-clang-asan
cmake --build --preset linux-clang-asan --target fuzz_decimal_parse

# 10-minute smoke run (Tier 1)
./build/linux-clang-asan/tests/fuzz/fuzz_decimal_parse \
    -max_total_time=600 -timeout=10 \
    tests/fuzz/corpus/decimal/
```

Expected: no ASan/UBSan finding. Long nightly runs in Tier 2.

## 6. Switch the decimal alias for a wider build (consumer scenario)

Consumers trading at venues that breach `pod_decimal`'s ±9.22 × 10¹⁰ ceiling at 8 fractional digits opt into a wider trait at build time:

```bash
# 1. Provide a header that defines my::decimal128 and a decimal_traits<my::decimal128>
#    specialization satisfying the surface in contracts/decimal_traits.hpp.

# 2. Configure with FIXPP_DECIMAL_T overridden + the user header pulled in:
cmake --preset linux-clang-release \
    -DFIXPP_DECIMAL_T=my::decimal128 \
    -DFIXPP_DECIMAL_USER_HEADER='"my_decimal.hpp"'
cmake --build --preset linux-clang-release
```

Two correctness checks happen automatically:

1. **Link-time guard (AC-B3, seam #9).** If two TUs are accidentally built with conflicting `FIXPP_DECIMAL_T`, the `decimal_alias_sentinel<T>::tag` symbol multiply-defines or unresolves at link time. **No silent ABI break.**
2. **C-ABI invariance (AC-B4).** `fixpp_decimal_t` stays the frozen 16-byte PoD regardless of the C++ alias. Python and C consumers see one shape per build.

## 7. Quick API tour (C++)

```cpp
#include <fixpp/core/decimal.hpp>

// Parse FIX FLOAT bytes
std::pmr::monotonic_buffer_resource arena{4096};
constexpr auto bytes = std::as_bytes(std::span{"123.45", 6});
auto v = fixpp::core::decimal_traits<fixpp::core::pod_decimal>
            ::from_chars(bytes, &arena);
// v == expected<pod_decimal{12345, -2}, _>

// Compare by value (1.50 == 1.5 == 1.500)
fixpp::decimal_t a{ fixpp::core::pod_decimal{150, -2} };
fixpp::decimal_t b{ fixpp::core::pod_decimal{15,  -1} };
auto cmp = fixpp::core::decimal_traits<fixpp::core::pod_decimal>
            ::compare(a.value(), b.value());
// cmp == std::strong_ordering::equal

// Cross-traits conversion (T == U: short-circuit; T != U: funnel via pod_decimal)
auto same = a.to<fixpp::core::pod_decimal>();   // decimal<pod_decimal>, no error possible
// auto wider = a.to<my::decimal128>();         // expected<decimal<decimal128>, decimal_error>
```

## 8. Quick API tour (C / Python via SWIG)

```c
#include <fix/c_api.h>

fixpp_decimal_t out;
fixpp_decimal_init(&out);                         // zero-init _reserved (recommended)
int rc = fixpp_decimal_parse("123.45", 6, &out);  // 0 on success

char buf[64];
size_t written;
rc = fixpp_decimal_format(&out, buf, sizeof buf, &written);

fixpp_decimal_t a = { 150, -2, {0,0,0,0,0,0,0} };
fixpp_decimal_t b = { 15,  -1, {0,0,0,0,0,0,0} };
int eq;
rc = fixpp_decimal_equal(&a, &b, &eq);            // eq == 1 (value equality)
```

## 9. CI evidence checklist (for PR open)

Per `[const §XVII §7]`, the PR description must include:

```
local build: green on linux-clang-debug @ <git-sha>
```

Plus the Tier 1 jobs (Linux/Clang Debug+Release, Linux/GCC Release, ASan, UBSan, TSan, Coverage, clang-tidy, IWYU, Python pytest) all green on the PR's last commit. Tier 2 (Windows + abidiff golden) is manual/nightly and not required for the merge.

## References

- Spec: [`spec.md`](./spec.md)
- Plan: [`plan.md`](./plan.md)
- Research: [`research.md`](./research.md)
- Data model: [`data-model.md`](./data-model.md)
- Contracts: [`contracts/c_api_decimal.h`](./contracts/c_api_decimal.h), [`contracts/decimal_traits.hpp`](./contracts/decimal_traits.hpp)
- Design doc (full *how*): `.specify/2a-decimal.md` v0.3
