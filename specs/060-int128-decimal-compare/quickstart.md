# Quickstart — build, verify, benchmark C1 / int128 decimal compare

All commands run from the library submodule root:
`research/G19-fix-fpml-iso20022/library/`. Build infra per `[feedback_conan_preset_build_infra_gotchas]`
(`conan install . -of build/<P> --build=missing`, `-j2` max per `[feedback_build_resource_cap_oom]`).

## 1. Differential oracle (the decisive gate — Tier 1)

```bash
# Deterministic C++ differential corpus + witness matrix + antisymmetry/transitivity
ctest --test-dir build/<preset> -R decimal_compare_diff_oracle --output-on-failure

# Existing unit regressions (AC-C1..C5) must stay green
ctest --test-dir build/<preset> -R decimal_compare_test --output-on-failure

# Extended Python-Decimal oracle (seed=42 + cross-exponent pairs)
ctest --test-dir build/<preset> -R decimal_compare_oracle --output-on-failure
```
Pass = 100% agreement between the new comparator and the retained digit-string reference (SC-001).

## 2. Sanitizers (Tier 1 — UBSan is the soundness guard)

```bash
for san in asan ubsan tsan; do
  ctest --test-dir build/<clang-$san-preset> -R 'decimal_compare' --output-on-failure
done
```
UBSan must be clean on the widening multiply / shifts.

## 3. Portable `#else` fallback coverage (Linux)

```bash
# Force-compile the 32-bit-limb schoolbook path and run the same oracle against it
cmake -B build/portable-mul -DFIXPP_DECIMAL_FORCE_PORTABLE_MUL=ON ...
ctest --test-dir build/portable-mul -R decimal_compare_diff_oracle --output-on-failure
```

## 4. Differential fuzzer (continuous coverage; closes the no-assert gap)

```bash
cmake -B build/fuzz -DFIXPP_BUILD_FUZZ=ON ...   # Clang only
cmake --build build/fuzz --target fuzz_decimal_compare -j2
./build/fuzz/bin/fuzz_decimal_compare tests/fuzz/corpus/decimal_compare/ -max_total_time=600
# Mutation check: introduce a deliberate compare mutant → the fuzzer MUST trap (SC-006)
```

## 5. Benchmark + baseline (Article VIII)

```bash
# Primary win (diff-exp) + no-regression (same-exp, diff-bucket)
./build/<release>/bin/decimal_bench --benchmark_filter='BM_decimal_compare'
# Deterministic instruction count (the real gate; WSL2 ns is shape-only):
valgrind --tool=callgrind --callgrind-out-file=cg.out \
  ./build/<release>/bin/decimal_bench --benchmark_filter='BM_decimal_compare_diff_exp'
# Update bench/baselines/decimal_baseline.json in this PR with rationale ([const §VIII.2/3])
```

## 6. MSVC lane (Tier 2 — the L-049-3 guardrail)

- Trigger the `run-tier2` PR label → `tier2.yml` builds `windows-msvc-{debug,release,asan}` on
  `windows-2022` and runs the oracle there.
- **Before trusting the MSVC branch**: confirm `_umul128` (x64) / `__umulh` (ARM64) signatures + arch
  availability against current Microsoft `<intrin.h>` docs (research.md R2 obligation).

## 7. Surface-invariance checks

```bash
# Header untouched + no ABI change
git diff --stat include/fixpp/core/decimal.hpp   # expect: empty
# abidiff runs in CI (abi-golden.yml / [const §IX.5]); decimal PoD must be byte-identical
```

## Done criteria (maps to Success Criteria)

- SC-001 oracle 100% agreement (deterministic + Python) · SC-002 witnesses pass + mutation-proven ·
  SC-003 diff-exp Ir decreases, no same-exp/diff-bucket regression · SC-004 all toolchains incl.
  `windows-msvc` + forced-portable · SC-005 amendment in all 5 files · SC-006 fuzzer traps on a mutant.
