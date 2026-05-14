---
id: 002-dictionary-xml-loader
title: Quickstart — Build, test, bench, TSan, coverage for the dictionary loader
spec_kit_step: /plan Phase 1
last_updated: 2026-05-14
status: drafted (round 1)
---

# Quickstart — 002-dictionary-xml-loader

Reproducible recipes for working on `fixpp::dict::XmlLoader` end-to-end. All paths are relative to the **library/** submodule root (`research/G19-fix-fpml-iso20022/library/` from the parent repo); all commands assume cwd is the library submodule. Toolchain is inherited from Phase 3 (Conan + Clang 22 + Ninja + CMake ≥ 3.28 per `[const §II.1]` / `[const §III.1]` / `[const §III.2]`).

The pugixml dependency is **new in this PR** — first time you build after pulling 002, you'll see Conan fetch `pugixml/1.14` per research.md D-1.

## 1. Build and run the unit tests (default-traits path)

The dictionary loader lives in `dictionary/`; the relevant CMake target is `fixpp_dictionary` for the library and the various `dictionary_*_test` executables for the tests. Tier-1 entry point is the `linux-clang-debug` preset.

```bash
# Configure once (first time after pulling 002 — Conan fetches pugixml)
conan install . --build=missing --profile=linux-clang-debug
cmake --preset linux-clang-debug

# Build + test the dictionary slice
cmake --build --preset linux-clang-debug --target fixpp_dictionary
cmake --build --preset linux-clang-debug --target $(seq 1 100 | awk '{printf "dictionary_xml_loader_test dictionary_lookup_test dictionary_ref_shape_test dictionary_negative_paths_test dictionary_round_trip_test dictionary_determinism_test dictionary_parser_error_test dictionary_pmr_allocation_test dictionary_oom_injection_test dictionary_concurrent_readers_test"}' | tr '\n' ' ')
ctest --preset linux-clang-debug -R '^dictionary_'
```

Expected: every `dictionary_*_test` target (per `plan.md §Project Structure > tests/dictionary/`) is green. Coverage threshold for the touched files is **≥ 90 % line / ≥ 80 % branch** per `[const §IX.1]`; measured under the `linux-clang-coverage` preset (recipe in §6).

## 2. Run all 10 test seams (seam → file map per plan.md)

The seam table in `plan.md` lists every named seam-or-AC test file. Run them as a group:

```bash
ctest --preset linux-clang-debug -L dictionary
```

Each seam-or-AC file labels itself `dictionary` via `set_tests_properties(... LABELS dictionary)` in `tests/dictionary/CMakeLists.txt`. The label-driven invocation guarantees no seam silently drops out of CI when a new test target lands.

Per-seam quickreference:

| Seam # | What runs | Test file |
|---|---|---|
| 1 | `load_from_string` as the mock `XmlSource` | (no dedicated file; exercised by seam #7) |
| 2 | PMR allocation tracking — AC-P1, AC-P2 | `tests/dictionary/pmr_allocation_test.cpp` |
| 3 | Clock seam — N/A | (none) |
| 4 | `FieldRef`/`ComponentRef`/`GroupRef` shape static_asserts — AC-F1..F5 | `tests/dictionary/ref_shape_test.cpp` |
| 5 | Determinism oracle — NFR-002-4 | `tests/dictionary/determinism_test.cpp` |
| 6 | TSan concurrent readers — AC-T1, AC-T2, NFR-002-3 | `tests/dictionary/concurrent_readers_test.cpp` |
| 7 | Negative-path XML samples — AC-L2..L8, L10 | `tests/dictionary/negative_paths_test.cpp` |
| 8 | Round-trip — AC-D1, AC-D2, AC-D5 | `tests/dictionary/round_trip_test.cpp` |
| 9 | OOM injection — AC-L9, AC-P2 | `tests/dictionary/oom_injection_test.cpp` |
| 10 | XML-parser-error injection — AC-L3 translation | `tests/dictionary/parser_error_test.cpp` |

## 3. Run sanitizer presets (ASan, UBSan, TSan)

Tier-1 sanitizer matrix per `[const §IX.2]`:

```bash
# ASan — every dictionary_*_test target
conan install . --build=missing --profile=linux-clang-asan
cmake --preset linux-clang-asan
cmake --build --preset linux-clang-asan --target fixpp_dictionary
ctest --preset linux-clang-asan -L dictionary

# UBSan — every dictionary_*_test target
conan install . --build=missing --profile=linux-clang-ubsan
cmake --preset linux-clang-ubsan
cmake --build --preset linux-clang-ubsan --target fixpp_dictionary
ctest --preset linux-clang-ubsan -L dictionary

# TSan — concurrent_readers_test specifically (AC-T2 verification)
conan install . --build=missing --profile=linux-clang-tsan
cmake --preset linux-clang-tsan
cmake --build --preset linux-clang-tsan --target dictionary_concurrent_readers_test
ctest --preset linux-clang-tsan -R '^dictionary_concurrent_readers_test$'
```

Expected: every preset green. Any data-race / use-after-free / undefined behaviour from the loader path lights up here.

## 4. Run the bench harness (NFR-002-1 verification)

The bench harness lives at `bench/dictionary/xml_loader_bench.cpp` (NEW in this PR, research.md D-18). It uses Google Benchmark per `[const §VIII.1]`.

```bash
# Release-preset build with bench targets enabled
conan install . --build=missing --profile=linux-clang-release
cmake --preset linux-clang-release -DFIXPP_BUILD_BENCH=ON
cmake --build --preset linux-clang-release --target xml_loader_bench
./build/linux-clang-release/bench/dictionary/xml_loader_bench \
    --benchmark_repetitions=100 \
    --benchmark_report_aggregates_only=true \
    --benchmark_format=json \
    --benchmark_out=bench-out.json
```

Verify the **median load time** for FIX44 ≤ 500 ms (user-facing target per NFR-002-1) and ≤ 1 s for FIX50SP2 (CI regression gate). The first run after a fresh build seeds `bench/baselines/dictionary/xml_loader.json`; subsequent runs diff against it per `[const §VIII.2]` (±5 % regression budget).

## 5. Run the bench-against-baseline regression diff

After the baseline is seeded (one-time, first green CI run):

```bash
python tools/bench_compare.py \
    bench/baselines/dictionary/xml_loader.json \
    bench-out.json \
    --tolerance 0.05
```

Exit status 0 = within ±5 %; exit status 1 = regression > 5 %. CI fails the PR on non-zero exit.

## 6. Run the coverage preset (≥ 90 % line / ≥ 80 % branch per `[const §IX.1]`)

```bash
conan install . --build=missing --profile=linux-clang-coverage
cmake --preset linux-clang-coverage
cmake --build --preset linux-clang-coverage --target fixpp_dictionary
ctest --preset linux-clang-coverage -L dictionary

# Generate coverage report scoped to the dictionary module
llvm-profdata merge -sparse build/linux-clang-coverage/tests/**/default.profraw \
    -o build/linux-clang-coverage/dict.profdata
llvm-cov report \
    --instr-profile=build/linux-clang-coverage/dict.profdata \
    build/linux-clang-coverage/src/dictionary/libfixpp_dictionary.a \
    -object build/linux-clang-coverage/tests/dictionary/dictionary_xml_loader_test \
    [other dictionary_*_test object files...] \
    --sources=src/dictionary include/fixpp/dict
```

Expected: line coverage ≥ 90 %, branch coverage ≥ 80 % on the new files in `src/dictionary/` and `include/fixpp/dict/`. The Codecov upload in CI (per Phase 3 baseline) carries the per-module breakdown so reviewers see the numbers in the PR.

## 7. Run static analysis (clang-tidy, clang-format, cppcheck, IWYU)

Tier-1 static analysis matrix per `[const §IX.4]`:

```bash
# clang-tidy (project ruleset)
cmake --build --preset linux-clang-debug --target fixpp_dictionary 2>&1 | \
    tools/run_clang_tidy.sh src/dictionary include/fixpp/dict

# clang-format check (no diff allowed)
clang-format --dry-run --Werror \
    src/dictionary/*.cpp include/fixpp/dict/*.hpp tests/dictionary/*.cpp

# cppcheck
cppcheck --enable=warning,style,performance --suppress=missingIncludeSystem \
    src/dictionary include/fixpp/dict 2>&1 | tools/parse_cppcheck.sh

# include-what-you-use
iwyu_tool.py -p build/linux-clang-debug src/dictionary include/fixpp/dict
```

Expected: all four clean. Local pre-commit hook runs the first two automatically.

## 8. Layer-edge lint (`tools/check_layers.py`)

The only new edge introduced by this feature is `dictionary → core`, which is **already present** in `src/dictionary/CMakeLists.txt` from prior phase scaffolding. Verification:

```bash
python tools/check_layers.py
```

Expected exit status 0. A regression (e.g., an accidental `#include <fixpp/wire/...>` in a dictionary header) fails this check and the Tier-1 CI job.

## 9. Sourcing the QuickFIX XML files (one-time, before first build)

Per research.md D-2, the four XML data files come from the upstream QuickFIX C++ repository at a pinned commit. The files are checked into `dictionaries/` in this repo; you don't need to pull them yourself. If you ever need to refresh (e.g., to track a newer upstream tag):

```bash
# From a temporary directory, NOT this repo:
git clone https://github.com/quickfix/quickfix.git /tmp/quickfix
cd /tmp/quickfix
git checkout <new-tag>
cp spec/FIX42.xml spec/FIX44.xml spec/FIX50SP2.xml spec/FIXT11.xml \
    <library-submodule>/dictionaries/

# Update the pin record
echo "quickfix/quickfix @ $(git rev-parse HEAD) tag=<new-tag> date=$(date -I)" \
    > <library-submodule>/dictionaries/UPSTREAM.txt
```

The four XML files plus `dictionaries/README.md` (which explains the pin) plus `dictionaries/UPSTREAM.txt` (the SHA pin record) make `dictionaries/` self-documenting.

## 10. End-to-end "first load" smoke (3 lines)

```cpp
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>

int main() {
    std::pmr::monotonic_buffer_resource arena;
    auto dict = fixpp::dict::XmlLoader{}.load("dictionaries/FIX44.xml", &arena);
    auto fr = dict.field_ref("D", 11);  // ClOrdID on NewOrderSingle
    return fr.rule == fixpp::dict::field_presence::Required ? 0 : 1;
}
```

Builds against `fixpp::dictionary` + `fixpp::core` (and `fixpp::dict::error` for exception types). Returns 0 if FIX44.xml is well-formed and `ClOrdID(11)` is Required on `NewOrderSingle`.

This snippet is **the** load-bearing AC-L1 verification — every other AC piggy-backs on a working `load` call.

## 11. `/speckit-verify` invocation (mandatory after `/speckit-implement` per `[const §XVII.8]`)

After `/speckit-implement` marks every `tasks.md` row `[X]`:

```bash
/speckit-verify 002-dictionary-xml-loader
```

Runs the Tier-1 preset matrix serially per `[const §XVII.8]` and writes the decision record at `.specify/decisions/002-dictionary-xml-loader-verify.md`. Verdict must be `GREEN` (every check PASS or SKIPPED-with-reason) to apply the `gate-b-done` label per `[const §XVII.8]`. `YELLOW` requires a waiver in both the verify record and the PR body.

## 12. `/gate-a` / `/gate-b` invocation (after verify passes)

```bash
# Gate A — design review (mandatory per [const §XVII.1])
/gate-a 002-dictionary-xml-loader

# Gate B — PR review (mandatory per [const §XVII.2])
/gate-b 002-dictionary-xml-loader
```

Both gates use a Codex review pass + an Opus adversarial pass per `feedback_gate_a_codex_dual_pass.md` (auto-memory). The independence rule per `[const §XVII.3]` is enforced — the implementer and the reviewer run in separate sessions.

## 13. Pre-PR confirmation line (per `[const §XVII.7]`)

Before opening the PR, include this line in the PR body:

```
local build: green on linux-clang-debug @ <git-sha>
```

`<git-sha>` is `git rev-parse HEAD`. PRs without this line, or with a known-red local build, are rejected at review.

## 14. Stuck-state checklist

If something doesn't build / test:

- **`pugixml.hpp` not found** → re-run `conan install . --build=missing --profile=linux-clang-debug` (the new dependency landed in `conanfile.py` with this PR).
- **`dictionaries/FIX44.xml` not found** → verify the four XML files are in `dictionaries/` (they should be present after `git pull` of this branch).
- **Tier-1 sanitizer red** → check ASan/UBSan/TSan output for the first leak/UB/race; the loader is single-pass over the XML (research.md A4) and frozen-after-handoff (D-7), so most reports point at the pugixml integration in `src/dictionary/xml_loader.cpp` or at the PMR-routing in the metadata-handle builder.
- **`AC-Lx` fails on FIX42 but passes on FIX44** → check the per-version headline tests in `dictionary_lookup_test.cpp`; FIX42 has a simpler `Instrument` component and no `Parties` (spec.md AC-D6 FIX42 sub-bullet).
- **`/speckit-verify` `RED`** → at least one unwaived Tier-1 failure; fix the root cause, do **not** waive without rationale. `YELLOW` requires `--waive=<task-id>:<rationale>` per `[const §XVII.8]`.
