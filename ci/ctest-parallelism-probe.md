# `ctest` parallelism — single-lane probe (TSan)

**Status:** PROBE. `linux-clang-tsan` only. Do **not** copy `execution.jobs` to any other test
preset until this lane has been re-measured green over several runs.

Every `testPreset` in `CMakePresets.json` ran serial (no `execution.jobs`) up to this change, and
no CI workflow passes `-j` to `ctest`. This probe sets `jobs: 2` on exactly one lane so the effect
can be measured against a stable baseline before anything is rolled out.

## Baseline (measured, not modelled)

`Test` step wall-clock from three consecutive `tier1.yml` runs on `main`:

| Lane | run 30862600825 | 30771094189 | 30748414724 |
|---|---|---|---|
| `linux-clang-tsan` | **3356 s** | 3300 s | 3302 s |
| `linux-clang-ubsan` | 1740 s | 1810 s | 1497 s |
| `linux-clang-asan` | 1758 s | 1348 s | 1570 s |
| `linux-clang-coverage` | 1171 s | 1070 s | 1147 s |

TSan is picked because it is both the largest lane and by far the most stable (±1.7% vs ±15% on
asan), so a real effect will clear the noise floor unambiguously.

## Where the 3356 s goes

Per-test times parsed from run `30862600825`'s TSan job log — 346 tests, serial sum **3356 s**,
which matches the observed step time exactly (so the step is test execution, not fixture overhead):

| Test | sec | Notes |
|---|---|---|
| `codegen_determinism_test` | 1132 | no `RUN_SERIAL` — regenerates into a `TempDir` |
| `dictionary_pure_tests` | 531 | |
| `fixpp::dict::codegen-source-staleness-check` | 332 | **`RUN_SERIAL TRUE`** + `RESOURCE_LOCK codegen_source_tree` |
| `required_scope_census` | 268 | |
| `wire_dict_tests` | 220 | |
| `fixpp::dict::codegen-build-graph-check` | (in tail) | `RESOURCE_LOCK codegen_source_tree`, no `RUN_SERIAL` |
| remaining 341 tests | 873 | |

Costing the `RUN_SERIAL` test as running alone, and treating `codegen_determinism_test` as the
critical path:

| `jobs` | modelled makespan | vs baseline |
|---|---|---|
| 1 | 3356 s | — |
| **2** | **~1844 s** | **−25 min** |
| 3 | ~1464 s | −32 min |
| ≥4 | ~1464 s | no further gain — bounded by `codegen_determinism_test` (1132 s) |

`jobs=2` captures the bulk of the available win and is the conservative first step.

## Why not go straight to 3 or 4

`ubuntu-latest` is 4 vCPU / 16 GB. Under TSan the binding constraint is shadow memory, not CPU:
two concurrent TSan processes multiply an already 5–10× footprint, and this repo has previously
read an OOM kill as a flake (exit 143). Several tests in the session/transport buckets are also
timing-sensitive, and oversubscription is exactly the condition under which a real lost-wake was
once dismissed as an "oversubscription flake".

## Acceptance for rolling this out further

1. The TSan `Test` step lands materially below the 3300–3356 s band, over **more than one** run.
2. No new failures, and no `exit 143` / OOM on the lane.
3. No test that was previously stable becomes intermittent.

Only then extend `execution.jobs` to `linux-clang-asan` / `linux-clang-ubsan` /
`linux-clang-coverage`, one at a time, re-measuring each.

If the lane goes red or flaky, revert this preset field — that is the entire blast radius of the
probe.
