# `ctest` parallelism — single-lane probe (TSan)

**Status:** MERGED (PR #227, squash `80ccb782`), **three measurements in — all green**.
`linux-clang-tsan` only. Do **not** copy `execution.jobs` to any other test preset yet: acceptance
criterion **4** (peak memory) is still unmet.

## MEASURED — 2026-08-04

Three independent runs: `30881578522` and `30885760893` (on `e3d3cecb`/`3c4a030a`), and
`30895037213` (on the rebased `b8a3481d`, i.e. the content that actually merged).

```
100% tests passed, 0 tests failed out of 346          (all three runs)
Total Test time (real) = 1935.04 / — / 1858.99 sec
346/346 Test #270: log_file_fsync ... Passed 1.42 sec  (all three — last, alone, identical)
```

| | value |
|---|---:|
| baseline (serial), 3 runs on `main` | 3356 / 3300 / 3302 — mean **3319**, range/mean **1.69%** |
| **measured, `jobs=2`** | 1935 / 1806 / 1859 — mean **1867**, range/mean **6.91%** |
| saving | **1453 s ≈ 24.2 min, −43.8%** |
| modelled ideal lower bound | 1845 — the three-run mean is **+1.2%** above it |

The third run matters more than as a tie-breaker: it is the only one taken on the exact content
that merged, and it moved the mean *toward* the ideal bound (+1.4% → +1.2%) rather than away, so the
model is not drifting as samples accumulate.

### Two further post-merge `push:main` runs — different basis, both correct

Runs `30908214440` (1946 s) and `30938621205` (1851 s), alongside the shared run `30881578522`
(1937 s), were previously carried in `tier1.yml`'s #229 comment as a second, uncited record of this
measurement; that comment now points here instead (PR #245 Gate B RC#4), and this section is where
the figures live going forward. **These are NOT the same measurement as the table above and are not
merged into it.** The table above is ctest's own
`Total Test time (real)` (the `:14` block); these three are the Actions **Test-step wall-clock**
duration, which additionally includes step setup/teardown overhead around the `ctest` invocation.
Only `30881578522` is common to both sets, and there the two bases agree as expected — 1935.04 s of
ctest inside a 1937 s step. Both sets are individually correct; they are not reconciled against each
other because they measure different things.

| | runs | basis | figures | mean |
|---|---|---|---|---:|
| measured, `jobs=2` (above) | `30881578522`, `30885760893`, `30895037213` | ctest `Total Test time (real)` | 1935 / 1806 / 1859 | 1867 |
| Actions Test-step duration | `30881578522`, `30908214440`, `30938621205` | GitHub Actions step wall-clock | 1937 / 1946 / 1851 | 1911 |

All three step-duration runs (and both `Total Test time` samples that overlap them) are green at
346/346, consistent with criteria 1–3 above.

### Acceptance status

| # | criterion | status |
|---|---|---|
| 1 | materially lower over more than one run | **MET** — 1935 / 1806 / 1859, all far below the 3300–3356 band |
| 2 | no failures, no `exit 143` / OOM | **MET** — 346/346 on all three runs |
| 3 | no previously-stable test turns intermittent | **MET** — identical 346 count and zero failures across three runs |
| 4 | peak RSS / cgroup `memory.peak` captured | **UNMET** — still not measured. Closes on the first successful post-merge `linux-clang-tsan` run of the `Capture peak memory (ctest --parallel evidence, #229)` step added in PR #245 (`tier1.yml`); record that run here (URL, source path, peak bytes/GiB, runner MemTotal, Test outcome) when it lands. |

**Criterion 4 is what blocks widening, and it is not a formality.** Three green runs say the lane
*did not* run out of memory; they say nothing about how close it came. Two concurrent TSan
processes on a 4 vCPU / 16 GB runner is precisely the configuration this repo has previously had an
`exit 143` OOM kill mistaken for a flake. Rolling `execution.jobs` out to `asan`/`ubsan`/`coverage`
without a headroom figure would repeat the modelled-for-measured substitution this probe exists to
prevent.

### `log_file_fsync` — the Gate B P1, closed by observation

Scheduled **last and alone at `346/346` for exactly 1.42 s in all three runs** — byte-identical to
each other and to its serial baseline. `RUN_SERIAL` is behaving deterministically, not
coincidentally, so the 40 ms / 100 ms / 200 ms-slack wall-clock assertions were never exposed to a
co-runner.

### A side effect worth recording

Run-to-run variance on this lane rose from **1.69% → 6.91%** (range/mean, three runs). That is expected —
makespan now depends on how tests happen to pack rather than on a fixed serial sum — but it has a
consequence: **this lane is a noticeably noisier baseline for any future A/B measurement.** The
1.7% stability that made TSan the right lane to probe *with* is partly spent by the probe itself.
Anything later that needs a tight TSan baseline should account for that, or use more runs.

Also note the two-run figures previously recorded here (mean 1871, −43.6%, +1.4% vs ideal) were
correct for two samples; they are superseded by the three-run figures above rather than corrected.

What the measurement establishes beyond the headline number:

- **Test count unchanged: 346, same as serial** — parallelism skipped or dropped nothing, which is
  the failure mode that would otherwise read as a saving.
- **The model was honest.** The mean sits *above* the ideal lower bound, the only direction it can
  legitimately sit; imperfect packing and contention account for the gap.

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
| `log_file_fsync` enqueue (`test_file_sink_async_fsync.cpp:212`) | 40 ms | ~0 | tight, no lower bound |
| `log_file_fsync` flush (`:259`) | 100 ms | 10 ms deadline | 10× |
| `log_file_fsync` close (`:274`) | 700 ms | 500 ms injected fsync | **200 ms** |
| `log_file_fsync` close (`:365`) | 1000 ms | 800 ms injected stall | **200 ms** |
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
   untested, and widening to `jobs=3` would be compounding an unmeasured assumption. **Still UNMET**:
   PR #245 installs the `Capture peak memory` step (`tier1.yml`) that takes this measurement;
   criterion 4 closes on the first successful post-merge `linux-clang-tsan` run after that PR merges,
   recorded in this document (run URL, source path, peak bytes/GiB, runner MemTotal, Test outcome) —
   not on the PR's own merge.

Only then extend `execution.jobs` to `linux-clang-asan` / `linux-clang-ubsan` /
`linux-clang-coverage`, one at a time, re-measuring each.

If the lane goes red or flaky, revert this preset field — that is the entire blast radius of the
probe.
