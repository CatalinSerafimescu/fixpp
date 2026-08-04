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

TSan is picked because it is both the largest lane and by far the most stable. Using one statistic
throughout — **range ÷ mean** — TSan is **1.69%** (56 s spread on a 3319 s mean) against ASan's
**26.3%** (410 s on 1559 s). So a real effect on TSan will clear the noise floor unambiguously,
while the same effect on ASan would not be separable from run-to-run variance.

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
| `log_file_fsync` | 1.4 | **`RUN_SERIAL TRUE`** — added by this PR (Gate B F-1) |
| remaining 340 tests | 872 | |

Costing the two `RUN_SERIAL` tests as running alone (332 + 1.4 s), and treating
`codegen_determinism_test` as the critical path — `333 + max((3356−333)/2, 1132)`:

| `jobs` | modelled makespan | vs baseline |
|---|---|---|
| 1 | 3356 s | — |
| **2** | **~1845 s** | **−25 min** |
| 3 | ~1465 s | −32 min |
| ≥4 | ~1465 s | no further gain — bounded by `codegen_determinism_test` (1132 s) |

These are **ideal load-balance lower bounds**, not predicted wall-clock: they assume perfect packing
and no slowdown from contention. The real number will be higher. That is precisely why this PR
measures instead of asserting.

`jobs=2` captures the bulk of the available win and is the conservative first step.

## Why not go straight to 3 or 4

`ubuntu-latest` is 4 vCPU / 16 GB. Memory is the **suspected** constraint, not a demonstrated one:
TSan's 5–10× overhead is well documented, but no peak RSS or cgroup-memory figure has been captured
for this suite, and shadow *address space* is not resident memory. The probe must therefore measure
it rather than assume it — see acceptance criterion 4. What makes the caution non-theoretical is
that this repo has previously read an OOM kill (`exit 143`) as a flake.

The CPU side is better evidenced. Many tests assert wall-clock bounds, and oversubscription is
exactly the condition under which a real lost-wake was once dismissed as an "oversubscription
flake". Two Gate B sweeps (PR #227 rounds 1–2) inventoried them. **This list is not exhaustive** —
the suite has dozens of 2–12 s completion watchdogs whose normal path is sub-second or structurally
driven, and enumerating every one is not the useful thing. What matters is the ratio between the
asserted bound and the *intended* path:

| Test | ceiling | intended path | slack |
|---|---|---|---|
| `log_file_fsync` enqueue (`test_file_sink_async_fsync.cpp:211`) | 40 ms | ~0 | tight, no lower bound |
| `log_file_fsync` flush (`:255`) | 100 ms | 10 ms deadline | 10× |
| `log_file_fsync` close (`:274`) | 700 ms | 500 ms injected fsync | **200 ms** |
| `log_file_fsync` close (`:364`) | 1000 ms | 800 ms injected stall | **200 ms** |
| `otel_exporters` teardown (`tests/otel/test_engine_close_teardown.cpp:311`) | 400 ms | ~50 ms | 8× |
| plain-transport close (`tests/transport/test_asio_plain_transport_config.cpp:242`) | 500 ms | immediate (wrong path is 2 s) | wide |
| C-API close (`tests/capi/lifecycle_test.cpp:~310`) | 1 s | immediate | wide |
| session / interop stop watchdogs | 1.5–5 s | prompt | wide |

The four smallest margins in the whole suite are all inside `log_file_fsync`, and two of them are
an absolute 200 ms rather than a multiple — which is why that one test is pinned `RUN_SERIAL` in
`tests/log/CMakeLists.txt` (1.42 s, its entire runtime). Everything else has enough headroom that
2× CPU contention should not reach it; that expectation is what acceptance criterion 3 exists to
falsify.

The same sweeps found **no** cross-test fixed path, fixed listening port, Unix socket, or
process-global env/cwd writer; every listener binds `127.0.0.1:0`; `codegen_determinism_test` uses
PID-keyed `TempDir`s; and the only test that mutates the source tree is already `RUN_SERIAL`, so it
cannot overlap `codegen-build-graph-check`'s repo-global `git status --porcelain` assertion. No
registered CTest `TIMEOUT` is tight enough for a 2× slowdown to trip it — the closest are
`wire_dict_tests` (600 s registered vs 220 s measured) and `delimiter_census` (600 s vs 112 s).

## Acceptance for rolling this out further

1. The TSan `Test` step lands materially below the 3300–3356 s band, over **more than one** run.
2. No new failures, and no `exit 143` / OOM on the lane.
3. No test that was previously stable becomes intermittent.
4. **Peak memory captured, not assumed** — record peak RSS / cgroup `memory.peak` during the run,
   ideally while `codegen_determinism_test` (1132 s) overlaps `dictionary_pure_tests` (531 s), the
   worst-case pairing. Until that number exists, "two concurrent TSan processes fit in 16 GB" is
   untested, and widening to `jobs=3` would be compounding an unmeasured assumption.

Only then extend `execution.jobs` to `linux-clang-asan` / `linux-clang-ubsan` /
`linux-clang-coverage`, one at a time, re-measuring each.

If the lane goes red or flaky, revert this preset field — that is the entire blast radius of the
probe.
