# Quickstart — 004-wire-codec

How to build, test, fuzz, bench, run the footprint spike, verify, and gate the wire module. Anchored to `.specify/2b-wire.md` v0.2 and the constitution Tier-1 matrix (`[const §IX.6]`).

## 1. Build (local pre-PR gate, `[const §XVII.7]`)

> Resource gate: local Conan/CMake builds are heavy. The agent surfaces `AskUserQuestion` before running them; the user approves first. The contributor adds `local build: green on linux-clang-debug @ <git-sha>` to the PR body.

```sh
conan install . -pr conan/profiles/linux-clang-debug --build=missing
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug --target fixpp_wire
ctest --preset linux-clang-debug -R '^wire_'
```

## 2. Test (TDD red-green-refactor, `[const §VII.3]`)

```sh
ctest --preset linux-clang-debug -R '^wire_'          # all wire seams #1..#14
ctest --preset linux-clang-debug -R 'wire_cutover'    # SC-006: 001 FLOAT + 003 reify GREEN
ctest --preset linux-clang-debug -R 'wire_validator_per_version'  # SC-005
```

Seam → file map is in `plan.md` (Test-seam mapping). Conformance corpus: `tests/wire/conformance/w0NN_*.csv` (oracle `[FIX50SP2 §3]`).

## 3. Tier-1 sanitizer / coverage matrix (serial, `[const §IX.2]`/`[const §XVII.8]`)

Run **one preset at a time** (never parallel — `[const §XVII.8]` serial matrix):

```sh
for p in linux-clang-asan linux-clang-ubsan linux-clang-tsan; do
  conan install . -pr conan/profiles/$p --build=missing
  cmake --preset $p && cmake --build --preset $p --target fixpp_wire
  ctest --preset $p -R '^wire_'
done
cmake --preset linux-clang-coverage && cmake --build --preset linux-clang-coverage
ctest --preset linux-clang-coverage -R '^wire_'   # >=90% line / >=80% branch on wire/
cmake --preset linux-gcc-release && cmake --build --preset linux-gcc-release --target fixpp_wire  # GCC sanity
```

## 4. Fuzz (parser-touching, `[const §VII.7]`, seam #11)

```sh
cmake --preset linux-clang-asan --target fuzz_wire_framer fuzz_wire_parser fuzz_wire_validator
./build/.../fuzz_wire_parser -max_total_time=600    # >=10 min Tier-1
```

## 5. Bench + ±5% regression gate (`[const §VIII.1]`/`[const §VIII.2]`, seam #5)

```sh
cmake --preset linux-clang-release --target wire_benches
./build/.../parser_bench --benchmark_out=bench/results/wire_parser.json
python tools/bench_compare.py bench/baselines/wire/ bench/results/   # fail >5% vs [2b §6.6] ceilings
```

## 6. Offset-table footprint spike (`[arch §11 row 1]` / `[2b §10 Q1]`, SC-008, seam #6)

In-PR decision artifact (`/clarify` Q3):

```sh
cmake --preset linux-clang-release --target offset_table_footprint_bench
./build/.../offset_table_footprint_bench --benchmark_out=bench/results/wire_footprint.json
```

Measures raw `entry[]` + hash overlay **separately**, occurrence space, over Logon / NewOrderSingle / NewOrderList×{1,10,100} / MDIR×{10,100,1000} / SecurityList×{1000,3000,5000}. Record the table + the hybrid-confirmed verdict in `.specify/decisions/004-wire-codec-verify.md` (closes `[arch §11 row 1]`).

## 7. Verify (mandatory after `/speckit-implement`, `[const §XVII.8]`)

```
/speckit-verify 004-wire-codec
```

Produces `.specify/decisions/004-wire-codec-verify.md` (GREEN / YELLOW / RED). Each polish task (sanitizer presets, coverage, clang-tidy/cppcheck/IWYU, alloc guard, fuzz smoke, bench regression, footprint spike) is executed serially. `GREEN` is the `gate-b-done` precondition.

## 8. Gate A / Gate B

```
/gate-a 004-wire-codec     # BEFORE /tasks ([const §XVII.1]); Codex + Opus adversarial; both Codex passes
/gate-b <PR#>              # BEFORE merge ([const §XVII.2]); /speckit-verify GREEN/YELLOW precondition
```

Gate-label evidence rule (`[const §XVII.8]`): `gate-{a,b}-done` requires `/speckit-verify` GREEN **and** the Codex convergence record `.specify/decisions/004-wire-codec-gate{a,b}.md`. Parent-repo tracked record = the Phase-4 doc + `research/reviews/` (the `.specify/decisions/` path is gitignored/local-only per project memory).

## 9. Cutover sanity (`/clarify` Q1, SC-006)

```sh
grep -rn 'message_view_contract' include/fixpp/dict tests/   # expect: only the kept re-export shim
ctest --preset linux-clang-debug -R 'wire_cutover|dict_reify|core_decimal'  # 001+003 GREEN on real MessageView
```

Zero references to the frozen R6 stub body must remain (the file is kept as a thin re-export of `parser.hpp`'s `MessageView`).
