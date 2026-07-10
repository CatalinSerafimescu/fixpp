# Measurements — 068-test-binary-grouping

## Pilot: `dictionary` (US1) — 2026-07-10

25 test `.cpp` → **2 grouped whole-binary `add_test` executables** (`dictionary_pure_tests`
= 15 TUs, `dictionary_reify_tests` = 4 TUs) + **6 standalone** (alloc/tsan/
concurrent/label-heterogeneous — FR-002). All 8 presets **315/315 green**
(315 gtest cases still run — see §(b) chosen-approach note; `gtest_discover_tests`
was the initially-spiked mechanism, rejected per §(b), superseded by whole-binary
`add_test`).

### (a) Disk — `inventory.sh` diff vs `baseline-2026-07-10.csv`

| preset | before bins | before | after bins | after | module × | grouped-portion × |
|---|---|---|---|---|---|---|
| linux-clang-debug | 25 | 154.3 MB | 8 | 60.9 MB | 2.5× | **6.0×** |
| linux-clang-asan | 25 | 262.4 MB | 8 | 93.6 MB | 2.8× | **6.7×** |
| linux-clang-tsan | 24 | 227.0 MB | 8 | 84.2 MB | 2.7× | — |
| linux-clang-ubsan | 25 | 285.0 MB | 8 | 111.3 MB | 2.6× | — |
| linux-clang-coverage | 25 | 188.5 MB | 8 | 77.9 MB | 2.4× | — |
| linux-gcc-release | 23 | 18.7 MB | 8 | 8.1 MB | 2.3× | — |
| linux-clang-release | 24 | 19.2 MB | 8 | 7.6 MB | 2.5× | — |
| linux-clang-libc++ | (no baseline row) | — | 8 | 60.0 MB | — | — |

- **Module-level ~2.3–2.8×**, diluted by the 6 irreducible standalone binaries
  (each still static-links the full fixpp stack).
- **Grouped-portion ~6–6.7×**: the 19 now-grouped tests dropped from ~112 MB
  (debug, 19 individual binaries) to 18.8 MB (2 shared binaries) — matches the
  2026-07-10 spike (7.1×/8.3× on a 14-test pure bucket). Meets SC-001 (recorded
  observation, several-fold on grouped portion).

### (b) ctest wall-time — `ctest -L dictionary`, before (26 entries) vs after

**gtest_discover_tests (rejected approach)** — after = 315 per-case entries:

| ctest `-j` | BEFORE | AFTER (rejected) | ratio | note |
|---|---|---|---|---|
| **-j1 (serial)** | 55.77 s | 321.71 s | **5.77×** | **CI-faithful** — CI passes no `-j`, sets no `CTEST_PARALLEL_LEVEL` → ctest serial |
| -j2 | 40.86 s | 167.77 s | 4.1× | local WSL2 compile-cap value (not a test constraint) |
| -j10 (`nproc`) | 41.11 s | 47.26 s | **1.15×** | parallel — grouping is wall-time-**neutral** |

**Mechanism (rejected approach).** `gtest_discover_tests` launches the 12 MB
binary once per case (315×) vs 26 whole-binary launches before. At serial that
launch overhead (~0.3 s × 315 ≈ 95 s) dominates. Under parallelism it amortizes
**and** per-case discovery parallelizes the old serial long-pole
(`PerCensusedCollision/CollisionMembershipGuards` — 69 params / 39.7 s, formerly
one un-parallelizable ctest entry). Hence 1.15× at -j10.

**SC-005 status (rejected approach).** ≤10% holds at -j10 (1.15×) but **fails
at CI-faithful serial (5.77×)**. This surfaced an FR-001 ↔ SC-005 tension
(FR-001 as originally written mandated per-case entries; SC-005 caps
wall-time).

**whole-binary `add_test` (chosen)** — after = 9 entries (2 grouped + 6
standalone + 1 mallocnesia), each grouped binary still running all its gtest
cases in-process (255 for `dictionary_pure_tests`, 53 for
`dictionary_reify_tests` — same case set as before, verified via
`--gtest_list_tests`):

| ctest `-j` | BEFORE | AFTER (chosen) | ratio |
|---|---|---|---|
| **-j1 (serial)** | 55.77 s | **53.04 s** | **0.95×** (faster than status quo) |

This resolves the SC-005 finding outright: whole-binary `add_test` is **at or
below** the pre-grouping serial baseline with **no `-j` dependency** — the
5.77× regression above was an artifact of per-case process-launch overhead
under `gtest_discover_tests`, which whole-binary registration eliminates by
construction (1 launch per bucket, same as before grouping). FR-001 amended
(see spec.md) to mandate whole-binary `add_test` instead of per-case
`gtest_discover_tests`; options (A)/(B)/(C) below are moot under the chosen
approach and retained only for historical record of the rejected path:
- **(A)** enable `ctest -j` in CI (`testPresets.execution.jobs` / `CTEST_PARALLEL_LEVEL`)
  → wall-time neutral (1.15×) under the rejected per-case approach; touches CI
  config (outside the `tests/**/CMakeLists.txt` edit surface).
- **(B)** accept the serial regression (disk is the goal; cost is `-j`-recoverable) — rejected.
- **(C)** reinterpret SC-005 as measured at parallel `-j` — rejected, superseded
  by the whole-binary result which needs no reinterpretation.

### (c) incremental relink blast-radius (T009c)

Touch one grouped `.cpp` (`lookup_test.cpp`) → rebuild `dictionary_pure_tests`
(recompile 1 TU + relink the 15-TU binary): **5.11 s** (debug, -j2). Pre-grouping
single-binary relink (recompile 1 TU + link 1 small binary) ≈ 2.5 s. Blast-radius
≈ **2×** — acceptable; well under the threshold that would force a bucket split
(T010: no split needed on relink grounds).

### Parallel-safety (bearing on option A)

- No `tests/**/CMakeLists.txt` declares `RESOURCE_LOCK` / `RUN_SERIAL` /
  `FIXTURES_*` / `PROCESSORS` — nothing relies on ctest **serial exclusivity**.
- **But** 28 files under `tests/{session,interop,tls}` bind fixed ports /
  `localhost` / `listen()`. CI's current serial execution masks any port
  collision; enabling global `ctest -j` could expose flakiness there. Dictionary
  itself is pure dict-parsing → `-j`-safe with certainty. ⇒ option (A) is a
  certain win for dictionary but **not proven** tree-wide without a
  network-module port audit (ephemeral vs fixed).

### Rollout lesson (US2) — superseded

(Rejected-approach lesson, kept for history.) `gtest_discover_tests` on a large
grouped binary needs `DISCOVERY_TIMEOUT 120` — the default 5 s times out
running `--gtest_list_tests` under TSan/coverage instrumentation (build fails
loudly). Moot under whole-binary `add_test` (chosen, §(b)): there is no
build-time discovery step, so no `DISCOVERY_TIMEOUT` is needed. Removed from
both dictionary buckets accordingly (see IMPLEMENTATION-PROCEDURE.md).
