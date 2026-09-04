# `ctest` parallelism — single-lane probe (TSan)

**Status:** MERGED (PR #227, squash `80ccb782`), **three measurements in — all green**.

⚠️ **This document has two layers and they are dated differently.** Everything below the
*"MEASURED — 2026-08-04"* heading is the original single-lane probe record and is correct for the
runs it cites. The **#266 section inside the acceptance table** carries the live mechanism: the
cgroup instrument that criterion 4 depended on never produced a reading, so both the source and the
closure condition were replaced. Read that section before acting on criterion 4 — the sentences it
supersedes are deliberately left in place as a record of a falsified assumption, not as instructions.

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
| 4 | peak RSS captured, per lane | **see the #266 section below** — the source changed; the closure condition is restated there because the one written here was falsified. |

### ⚠️ Criterion 4's old closure condition was FALSIFIED — #266

It read: *"Closes on the first successful post-merge `linux-clang-tsan` run of the
`Capture peak memory (ctest --parallel evidence, #229)` step added in PR #245."*

**That run does not exist and never would have.** The step read cgroup v2's
`memory.peak`, which exists only on **non-root** cgroup v2 nodes; a GitHub-hosted
job sits in the root cgroup, so the file is absent. It failed on **8 of 8**
post-merge runs over five days — a census, not a sample — each time emitting its
loud `NOT MEASURED` warning rather than a silent nothing or a fabricated number.
The probe's *refusal* was correct; its *source* did not exist on the platform it
ran on.

The sentence above is kept rather than deleted: an assumption that a criterion
was written on, and that turned out to be false, is part of the record.

#### The replacement, and what it closes on

Source is now `ci/measure-peak-rss.py` — a `/proc` sampler that sums the RSS of
`ctest`'s whole process tree at 250 ms, wrapped **around** the ctest invocation
so the instrument's lifetime is structurally tied to the thing it measures.
`/usr/bin/time -v` was considered and rejected: `getrusage(RUSAGE_CHILDREN)`
reports the largest **single** child, not the concurrent **sum**, and #229's
local sweep measures that error at **+37 %** on this very lane (1.04 GiB largest
single ⇒ a naive ~4 GiB projection at j=4, against 2.53 GiB measured).

**Criterion 4 now closes PER LANE, on a `pull_request`- or `push:main`-context
run whose `Peak memory (ctest --parallel evidence, #266 / #229)` step renders the
`evidence` heading — not the `DIAGNOSTIC ONLY` one — for that lane, recorded in
the table below.** The heading is conditional on the Test step having succeeded
*and* the run having executed exactly the count recorded for that lane in
`ci/expected-eligible-tests.txt`, so a figure can never be labelled evidence over
a workload that is not the recorded one.

Three consequences worth stating, because each replaces something the old
condition assumed:

* **It is no longer "the first post-merge run".** That phrasing is what made the
  criterion unfalsifiable for five days — there was no run to wait for. The
  condition is now a *rendering* the job either produces or does not, visible on
  the run's own summary page.
* **It is per lane, not one number.** #267 widens `--parallel` lane by lane and
  requires a reading for each; the pin moved out of `tier1.yml`'s single
  `expected_eligible=350` into a per-lane file for the same reason.
* **It measures the TEST PHASE only.** `memory.peak` was a job-wide high-water
  mark covering `Build`, reported as a deliberate ceiling because the runner user
  cannot reset it. Sampling around `ctest` needs no such concession — but it also
  means the new figures are **not** comparable to a cgroup one, had any existed.

#### The eligible-count basis, re-derived (#266 acceptance item 5)

The old pin was `expected_eligible=350`, derived **locally** at `9e444ef5`.
⚠️ **Local and CI trees do not agree**, which that derivation could not see: on
`main` @ `0b51b1da` the #229 local sweep counted **376** eligible tests on
`linux-clang-tsan` and **377** on `linux-clang-debug`, while CI on the same
content ran **361** and **362**. Fifteen tests are registered on a workstation
that are not registered on a runner. The per-lane values now in
`ci/expected-eligible-tests.txt` are therefore read off **CI job logs** (run
`31737273371`, commit `5c56a17e`), and the first CI run of this change is their
confirmation; any lane that renders `DIAGNOSTIC ONLY` gets its line corrected
rather than argued with.

#### MEASURED — run `32003367497`, 2026-08-17

`workflow_dispatch` on `probe/266-run-a` (branch `ci/266-peak-rss-instrument` plus
a throwaway commit disabling the wheel jobs, which measure nothing here and cost
71 runner-min). Runner `MemTotal` **15.61 GiB** on every lane.

| lane | ctest jobs | peak concurrent RSS | % of MemTotal | achieved concurrency | tests executed | sanitizer reports |
|---|---:|---:|---:|---:|---:|---:|
| `linux-clang-release` | 1 | 0.37 GiB | 2.4 % | 1.00× | 362 / 362 ✅ | 0 |
| `linux-clang-debug` | 1 | 0.59 GiB | 3.7 % | 1.00× | 362 / 362 ✅ | 0 |
| `linux-clang-ubsan` | 1 | 0.68 GiB | 4.3 % | 1.00× | 361 / 361 ✅ | **1** ⚠️ |
| `linux-clang-coverage` | 1 | 0.85 GiB | 5.5 % | 1.00× | 369 (basis unrecorded) | 0 |
| `linux-clang-tsan` | **2** | **1.41 GiB** | **9.1 %** | **1.83×** | 361 / 361 ✅ | 0 |
| `linux-clang-asan` | 1 | 1.84 GiB | 11.8 % | 1.00× | 361 / 361 ✅ | **1** ⚠️ |

**#266 is discharged by the first row alone** — a real number, on a hosted
runner, from an instrument that had produced none in 8 attempts.

Five of six lanes matched their pin on first contact. `linux-clang-coverage` had
no pin (#229's lane table does not cover it), rendered `DIAGNOSTIC ONLY` as
designed, and its basis (369 — it runs the packaging tier the other five exclude)
is now recorded from this run rather than guessed from a sibling.

##### ⛔ REPEATABILITY — MEMORY IS SOLID, TIMING IS NOT, AND THE DIFFERENCE DECIDES WHAT THIS DOCUMENT MAY CLAIM

Runs A/B/C re-ran several lanes at **unchanged configuration** on the same C++
tree. Each matrix leg gets its **own VM**, so this spread is what any comparison
of two separate runs must clear:

| lane (unchanged config) | samples | wall spread | peak RSS spread |
|---|---:|---:|---:|
| `linux-gcc-release` (305–308 s) | 2 | **0.9 %** | 0.08 % |
| `linux-clang-release` (251–258 s) | 2 | **2.9 %** | 2.8 % |
| `linux-clang-asan` (1553–1980 s) | 2 | **27.5 %** | 1.7 % |
| `linux-clang-tsan` @ j=2 (1376–2160 s) | 3 | **43.3 %** | 0.7 % |

**Two different conclusions, and they must not be merged:**

* **Peak RSS reproduces to under 2 % everywhere**, on light and heavy lanes
  alike. #266's instrument is sound and its figures are usable as recorded.
* **Wall-clock on the heavy lanes does not reproduce at all** — 27–43 % between
  VMs. On those lanes a comparison of two separate CI runs measures the runner,
  not the change.

⚠️ **An earlier revision of this section claimed a "~1–3 % serial noise floor"
and is CORRECTED, not merely extended.** That figure was taken from the two
lightest lanes in the matrix (250–310 s) and then reasoned about as though it
applied to serial lanes generally. `linux-clang-asan` is also serial and spreads
**27.5 %**. Variance tracks lane WEIGHT, not the serial/parallel distinction the
old text used to organise it — the light lanes are simply not stressed enough to
expose the VM.

⚠️ **The 6.91 % figure recorded further down this document is likewise not a
usable floor.** It came from three runs in one sitting and is an order of
magnitude below what three later samples of the same lane and the same `jobs`
setting show (1903 / 2160 / 1376 s). Treat it as a historical measurement of
three particular runs, never as a bound.

##### Consequence: every UNPAIRED throughput comparison in this document is withdrawn

Any A/B that compares one lane in run X against the same lane in run Y is
confounded by the VM. The paired design that replaces it runs **both
configurations back-to-back in ONE job on ONE VM**, driven by `--parallel N` on
the command line (which overrides a preset's `execution.jobs` — verified by
explicit local probe, recorded under "Two guards worth recording" in #229).

⚠️ **Pass order is load-bearing: j=4 first, j=1 second.** The second pass runs
against a warm OS page cache, so this order biases toward the SERIAL baseline and
any surviving j=4 advantage is a **lower bound**. The opposite order would
flatter the parallel pass — i.e. manufacture the result being tested for.

##### Two independent cross-validations of the instrument

Neither was arranged; both are checks against numbers measured by something else.

* **Achieved concurrency 1.83× against #229's 1.84×.** #229 derived the tsan
  lane's production efficiency at `jobs: 2` from a *different* run
  (`31737273371`) by summing per-test durations against `Total Test time (real)`.
  This instrument computed 1.83× on run `32003367497` without reference to it.
  **0.5 % apart.**
* **CI peak 1.41 GiB against the local sweep's 1.52 GiB** at the same `j`, same
  lane. **7.2 % apart** — so the local host reproduces the effect, and its
  *ratios* transfer even though its absolutes do not (it is ~1.38× faster, has
  23 GB against 15.61, and registers 376 tests against CI's 361).

##### ⚠️ Two sanitizer reports, UNATTRIBUTED as of this run

`linux-clang-asan` and `linux-clang-ubsan` each carry **1** report in
`LastTest.log`, on runs whose ctest was **green**. That is not a contradiction —
a sanitizer report does not necessarily fail the test that emitted it, which is
why this count is taken separately from the exit code.

What is known: the three non-sanitizer lanes read 0, so it is sanitizer-lane
specific; and `linux-clang-asan` does **not** enable UBSan (checked in
`cmake/Sanitizers.cmake` — `FIXPP_ENABLE_ASAN` and `FIXPP_ENABLE_UBSAN` are
independent options and the presets set one each), so these are two independent
matches rather than one shared cause.

What is **not** known: which lines matched. The instrument reported a bare count
on this run — a defect since fixed; it now prints the matched lines. Per this
repo's standing rule these are **real defects until disproven**, and they are not
disproven. Pending re-run.

#### Phase 1 — `linux-clang-tsan` `jobs: 2 → 4` (#267)

The arithmetic, done from the **CI** number rather than the local one, because
that is the constraint being reasoned about:

| step | value |
|---|---:|
| CI peak at `j=2` (measured, above) | **1.41 GiB** |
| local `j=2 → j=4` factor (2.53 / 1.52 GiB, #229 sweep) | ×1.664 |
| ⇒ projected CI peak at `j=4` | **~2.35 GiB** |
| of a 15.61 GiB runner | **~15 %** — ~6.6× headroom |

Cross-checked a second way: the local **absolute** at `j=4` is 2.53 GiB, i.e.
16.2 % of this runner. Ratio-method and absolute-method land within 1.2
percentage points of each other, so the conclusion does not depend on which is
used.

⚠️ **A single-process figure would have said something else**, and that is the
whole reason this is a concurrent-sum instrument: the serial run's largest single
process is 1.04 GiB locally, which projects naively to ~4 GiB at `j=4` — **37 %
high** against the 2.53 GiB measured.

**Acceptance for phase 1 is the PAIR, not the peak alone.** `execution.jobs` is
an intention; the achieved-concurrency figure is what says it took effect. The
expected band at `j=4` is **2.2–2.6×** (local measured 2.39× and 2.32× on two
runs; the greedy-LPT model's 3.16× is a ceiling and ~⅓ optimistic — do not accept
against it). **A post-widening run reporting ~1.8× means the widening did not
take effect, and its peak would then be a `j=2` reading mislabelled as `j=4`
evidence** — precisely the class this PR exists to close.

#### ⛔ PHASE 1 RESULT — j=4 MEASURED, REFUTED, AND REVERTED

Run `32007171995`, same branch content as run A modulo CI files, same 361 tests,
adjacent in time. **This is the controlled A/B.**

| | j=2 (run `32003367497`) | j=4 (run `32007171995`) | delta |
|---|---:|---:|---:|
| wall — ctest `Total Test time (real)` | 2160.5 s | 2085.4 s | **−3.5 %** |
| achieved concurrency | 1.83× | **2.73×** | the widening DID take effect |
| summed per-test wall-time | 3954 s | **5693 s** | **+44.0 %** |
| peak concurrent RSS | 1.41 GiB | 1.92 GiB | **+35.5 %** |
| parallel efficiency | 92 % of j=2 | **68 % of j=4** | collapsed |
| tests executed | 361 / 361 | 361 / 361 | unchanged |
| sanitizer reports | 0 | 0 | unchanged |

**The widening worked and was not worth having.** Concurrency rose exactly as
intended — 2.73× is comfortably above the 2.2–2.6× acceptance band, so this is
not a case of the field failing to take effect. What did not happen is the
saving.

⚠️ **THE ORIGINAL WORDING HERE WAS "−3.5 % sits inside this lane's own 6.91 %
run-to-run variance". THAT IS WITHDRAWN — the true figure is far worse, and the
conclusion survives only because it got worse.** Three samples of THIS lane at
THIS `jobs: 2` setting, on the same tree, are **1903 / 2160 / 1376 s** —
range/mean **43.3 %**. The two runs compared above are two different VMs. A
−3.5 % delta against a 43 % floor does not measure anything at all; the honest
statement is not "inside the noise band" but **"this comparison could not have
detected an effect of any size a widening would plausibly produce"**.

The costs, by contrast, are stable across VMs and remain trustworthy — peak RSS
reproduces to 0.7 % on this lane, so **+36 % peak memory** is real. The **+44 %
summed per-test wall-time** is an intra-run ratio, also unaffected by VM speed.

So the disposition is unchanged and its warrant is stronger: measurable cost,
**unmeasurable** benefit. Reverted to `jobs: 2`.

Anything that wants to overturn this must use the paired same-VM design
described above, not another pair of separate runs.

##### The premise that failed, and it is not the memory one

Every projection in #267 and #229 — the greedy-LPT model *and* the local sweep —
assumes **per-test durations are invariant under concurrency**. Measured on a
runner, they are not: they inflate 44 % going from 2 to 4 concurrent TSan
processes. The memory reasoning was sound and even conservative (projected
2.35 GiB, measured 1.92); the *throughput* reasoning was not.

⚠️ **The local ratio did not transfer, and "ratios transfer, absolutes do not"
is exactly what #229 said would hold.** It did not:

| | j=2 → j=4 wall improvement |
|---|---:|
| local sweep (`taskset -c 0-3`, 10-CPU / 23 GB host) | 1457.4 → 1057.2 s = **1.38×** |
| CI (4-vCPU runner) | 2160.5 → 2085.4 s = **1.04× (noise)** |

The likely mechanism, stated as a hypothesis and not a measurement: `taskset`
restricts *scheduling* to four CPUs but leaves the host's full memory bandwidth,
L3 and idle neighbours available. TSan is shadow-memory-bound, so four concurrent
TSan processes saturate a small runner's bandwidth in a way they never do on the
workstation. **Pinning core count is not pinning a runner.** Nothing here
measures bandwidth, so this explains rather than proves.

##### What this means for the remaining phases

Efficiency is 92 % at j=2 and 68 % at j=4 on this lane — the curve is already
flat by 2. j=3 is **not** proposed: interpolating the same contention gives
≈2063 s, i.e. the same wall time again, so it would cost a 35-minute run to
re-measure noise.

Phases 2–3 are **not** cancelled by this, but their justification no longer
carries over from the local sweep and must be re-earned per lane. The lanes most
likely to behave differently are the ones that are **not** shadow-memory-bound:
`linux-clang-debug` above all (no sanitizer, local j=1→j=4 2.54×). The sanitizer
lanes should be assumed to saturate near j=2 until a lane shows otherwise.

⚠️ **One run per configuration.** The wall-clock conclusion is
"no measurable improvement", NOT "it got worse" — the deltas that are large
enough to act on are the cost ones. The decision is asymmetric on purpose:
measurable cost against unmeasurable benefit is a revert, and buying more samples
to defend a change with no demonstrated upside is not a good use of a 35-minute
lane.

#### ✅ PHASE 2 — `linux-clang-ubsan` `jobs: 1 → 4`, CONFIRMED

Measured **paired, both passes in ONE job on ONE VM** (run `32015364279`), which
is the only design that survives 27–43 % between-VM variance:

| pass | order | wall | achieved concurrency | peak RSS | tests |
|---|---|---:|---:|---:|---:|
| `--parallel 4` | first | **1114.46 s** | 2.68× | 0.86 GiB | 361/361 |
| `--parallel 1` | second | **1993.08 s** | 1.00× | 0.65 GiB | 361/361 |

**1.79×**, i.e. ~880 s ≈ **14.6 min per run** off the largest Tier-1 lane.

##### Why this is believed, when the earlier ubsan claim was withdrawn

The withdrawn claim was **one unpaired comparison between two VMs**, which VM
luck alone explains. This one is corroborated four ways across three VMs:

| configuration | independent measurements | agreement |
|---|---|---:|
| ubsan `j=4` | 1136.04 s (run C, VM X) / 1114.46 s (run D, VM Y) | **1.9 %** |
| ubsan `j=1` | 1912.90 s (run A, VM Z) / 1993.08 s (run D, VM Y) | **4.2 %** |

⚠️ **The second row is the one that matters, and it kills the rival hypothesis.**
Run D's `j=1` was a **second** pass. If second passes ran degraded — the effect
that makes the `debug` numbers ambiguous below — it would have read ≈3800 s. It
read 1993 s, within 4.2 % of an independent **first**-pass serial measurement on
a different VM. So on this lane there is no meaningful position effect, and the
pairing is trustworthy.

Memory cost is modest and consistent with every other lane: 0.65 → 0.86 GiB
(+32 %), **5.5 % of a 15.61 GiB runner**. Summed per-test wall-time inflates
1993 → 2987 s (+50 %) — real contention, but far less than it buys.

##### ✅ `linux-clang-debug` `jobs: 1 → 4`, CONFIRMED by A-B-A

The 97 % disagreement between `debug`'s two serial numbers was settled by running
**three passes on one VM**, serial-parallel-serial, with an independent machine
witness between each (run `32018795574`):

| step | wall | 1-proc calibration | 4-proc calibration | cumulative steal |
|---|---:|---:|---:|---:|
| — | — | 2.73 s | 5.21 s | 0 |
| `--parallel 1` | **1142.14 s** | 2.85 s | 5.16 s | 0 |
| `--parallel 4` | **541.53 s** | 2.82 s | 5.11 s | 0 |
| `--parallel 1` | **1135.28 s** | 2.94 s | 5.08 s | 0 |

**The two serial passes agree to 0.6 %.** There is no pass-order effect, the
calibration drifts under 8 %, and `/proc/stat` steal is **0** throughout — the
machine did not change under the experiment. **2.10×**, ≈597 s ≈ **10 min/run**.

⚠️ **The outlier was run A, not run D.** `debug` serial reads 1142 / 1135 / 1151 s
on three VMs and **583 s** on run A's — that one VM was roughly twice as fast.
Every "no gain" reading for this lane came from comparing against it.

##### ⚠️ `linux-clang-tsan` — the j=2-vs-j=4 question is UNRESOLVED, not settled

The phase-1 comparison (run A `j=2`, run B `j=4`) was **also unpaired**, on the
lane with the worst measured spread of all (**43.3 %**). It resolves nothing, and
the earlier wording *"measured no gain at j=4"* is **withdrawn** — the same error
as the withdrawn `debug` and `ubsan` claims, and it survived longer because its
conclusion happened to be the conservative one.

What is still solid, because it does not depend on VM speed:

* **+36 % peak memory at j=4 is real** — peak RSS reproduces to 0.7 % on this lane.
* Achieved concurrency 1.83× at j=2 and 2.73× at j=4 are intra-run ratios.

So `jobs: 2` stays as a **conservative default with a real known cost on the
alternative**, not as a measured optimum. Given `debug` (2.10×) and `ubsan`
(1.79×) both gained once measured properly, this lane deserves the same A-B-A
treatment before anyone concludes anything about it.

Not widened here, and each for its own reason: `asan` is phase 2
and are held until their two unattributed sanitizer reports are resolved;
`coverage` needs #267 acceptance item 4 (merged coverage shown identical before
and after) discharged first; the four `libc++` lanes are phase 3 and have no
local sweep at all; `linux-gcc-release` is in no phase; `windows-msvc-asan` is a
different platform and sanitizer runtime with no measurement of any kind.

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

⚠️ **THE 6.91 % FIGURE BELOW IS NOT A NOISE FLOOR — see the #266 section
above.** Three later samples of this same lane at this same `jobs: 2` setting
measured 1903 / 2160 / 1376 s, i.e. **43.3 %** range/mean. 6.91 % describes three
particular runs in one sitting; it is not a bound and must not be used to accept
or reject a delta.

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
4. **Peak memory captured, not assumed** — record the peak concurrent RSS during the run, ideally
   while `codegen_determinism_test` (1132 s) overlaps `dictionary_pure_tests` (531 s), the
   worst-case pairing. Until that number exists, "two concurrent TSan processes fit in 16 GB" is
   untested, and widening to `jobs=3` would be compounding an unmeasured assumption.

   ⚠️ **This item's mechanism was rewritten by #266 and its closure condition now lives in the
   *"Criterion 4's old closure condition was FALSIFIED"* section above.** The paragraphs that stood
   here — the `Capture peak memory` step, the cgroup source, the single `expected_eligible` in
   `tier1.yml`, and "closes on the first successful post-merge run" — described an instrument that
   produced a reading on 0 of 8 runs. They are superseded there rather than patched here, so there
   is one description of the live mechanism and not two that can drift apart.

   The one thing worth carrying forward verbatim, because it is a design property and not an
   implementation detail: **a mismatch between the executed test count and the recorded basis is a
   DESIGNED PROMPT, not a bug.** It degrades the run to `DIAGNOSTIC ONLY`, i.e. toward "not
   evidence" — never toward a false acceptance. Re-record the basis and update
   `ci/expected-eligible-tests.txt` in the same commit; the criterion closes on the run **after**
   that reconciliation, not on the mismatched one.

   For the record of what the pin was before: `expected_eligible` was re-derived for PR #245 Gate B
   round 2 at `9e444ef5` (configure-only `cmake --preset linux-clang-tsan` +
   `ctest --preset linux-clang-tsan -N -LE packaging`) and came back **350**, not the 346 this
   document's criteria 2 and 3 above were discharged at — #239 landed on `main` after the three runs
   cited above and added test files under `tests/session/` and `tests/transport/`. The 346/346
   figures in criteria 2 and 3 are correctly measured for the runs they cite and are left as-is
   (add, do not correct). ⚠️ That 350 was derived **locally**, which is why it drifted from CI
   without anyone noticing — see the re-derivation note in the #266 section above.

Only then extend `execution.jobs` to `linux-clang-asan` / `linux-clang-ubsan` /
`linux-clang-coverage`, one at a time, re-measuring each.

If the lane goes red or flaky, revert this preset field — that is the entire blast radius of the
probe.

---

## The measurement apparatus — `.github/workflows/parallelism-measure.yml` (#267)

Everything above this line was measured by hand, once, with an ad-hoc script that was never
committed. That is why the `debug` A-B-A — the one result in this document nobody has had to
withdraw — could not simply be repeated on the next lane. This section describes the committed
version of that design.

### What runs

`ci/run-parallelism-aba.sh` runs one lane's sample **in one job, on one VM**:

```
witness → ctest --parallel 1 → witness → ctest --parallel N → witness → ctest --parallel 1 → witness
```

`ci/parallelism-verdict.py` then judges it and exits **0 VALID / 1 DEFECT / 2 INSTRUMENT FAILURE /
3 VOID**. A red job means one of the last three — read the summary, not the tick.

Dispatch it from the Actions tab with a comma-separated lane list per platform. Nothing else
triggers it, and `ci/assert-ci-lane-policy.py` asserts that (`ci/test-ci-lane-policy.sh` cells T7,
T8, T8b prove the assertion can fail): each lane runs its suite **three times**, so an automatic
trigger would multiply the CI bill with every run still green.

### The four things that make a sample mean something

1. **A vs A′ must agree** (default 5 %). The primary gate, and the reason for the whole shape —
   `debug`'s trusted 2.10× had its serial passes agree to 0.6 %.
2. **The achieved parallel level is observed, not assumed.** The verdict counts `Start`/complete
   pairings in the ctest log and requires pass A to have run **1** test in flight and pass B to have
   run more.

   ⚠️ **This is the one failure A-vs-A′ cannot catch**, and it is not hypothetical. If the requested
   level fails to take effect, all three passes are identical, the agreement check passes
   *perfectly*, and the apparatus reports a clean sample saying "no gain on this lane" having tested
   one configuration three times. Measured, because the obvious mitigation is the wrong one:
   `ctest --preset P --parallel 1` **does** override a preset's `execution: {jobs: 4}` (8.03 s vs
   2.01 s on an 8×1 s synthetic suite), but `CTEST_PARALLEL_LEVEL=1 ctest --preset P` **does not**
   (2.01 s — the preset wins). `linux-clang-debug` and `linux-clang-ubsan` already carry `jobs: 4`,
   so the env-var form would have corrupted exactly the lanes a campaign starts from.
3. **The workload is checked against the lane's production basis** in `ci/expected-eligible-tests.txt`.
   Three passes agreeing with each other says nothing about agreeing with what ships. A mismatch
   VOIDs — the disposition this document already designed for a basis mismatch: *degrades toward
   "not evidence", never toward a false acceptance*.
4. **The machine is observed independently**, by `ci/machine-witness.py`, between passes.

### What is equalised between passes, and why each one matters

* **`CTestCostData.txt` is deleted before every pass.** CTest schedules longest-first from it on the
  *next* run. Left alone, pass B inherits pass A's cost data and packs better than any production
  run can — production always starts from a fresh checkout and never has cost data.
* **`LastTest.log` is copied out after every pass.** CTest overwrites it, so pass A′ would destroy
  pass B's — and pass B's is the only interesting one, since acceptance item 5 is precisely *did a
  report appear at higher concurrency*.
* **Coverage profiles go to a per-pass directory.** Three passes writing into one `profiles/` dir
  merge into one report covering all three, and "coverage identical before and after" becomes
  vacuously true.

### Acceptance item 4 — how the coverage comparison is actually attributable

Two comparisons, and only their difference attributes anything. **A vs A′** is the suite's own
run-to-run coverage determinism, measured on the same VM at unchanged concurrency. **A vs B** is the
parallelism effect. B differing while A and A′ agree is attributable to `--parallel`; all three
differing is a nondeterministic suite — a finding of its own, and explicitly *not* evidence about
parallelism either way. The digest is a sha256 of the **sorted** lcov `.info`, because
`llvm-cov export` emits per-object sections whose order follows the object list and the filesystem,
neither of which is a coverage fact.

### ⚠️ What this apparatus does NOT establish

* **The Windows lanes get no peak-RSS figure.** `ci/measure-peak-rss.py` samples `/proc`, which
  Windows does not have, so **acceptance item 1 cannot be discharged for `windows-msvc-asan` by this
  apparatus.** The driver writes `status=no-procfs-platform` and the verdict renders NOT MEASURED,
  so the gap is on the page rather than showing as a blank cell or a zero. Everything else transfers.
  A widening proposed on that lane would be proposed without the memory reading the Linux lanes are
  held to; say so in the PR.
* **A uniformly degraded VM is invisible to every check here.** A-vs-A′ and the calibration drift
  both detect the machine *changing*; a machine that was slow for the whole experiment produces zero
  drift and perfect agreement. Catching that needs the **absolute** calibration against a known-good
  band, and one CI observation is not a band. The absolute figures are recorded on every run so the
  corpus accumulates; until it does, this is an open gap and not a closed one.
* **The calibration tolerance is loose (25 %) because that is what the signal can carry.** Six
  back-to-back witnesses on a contended host spread **16.0 %** on the 1-proc arm and 8.4 % on the
  4-proc arm using a min-of-5 estimator; a single timing spreads 12–15 % and does *not* tighten with
  more iterations, because it is contention rather than sampling error. The only CI observation
  available is `debug`'s trusted A-B-A, whose 1-proc calibration moved 2.73 → 2.94 s = **7.4 %**
  across an experiment everyone agrees was clean. A 10 % gate would void honest samples routinely,
  and a gate that cries wolf gets widened until it detects nothing.

### The instruments are themselves instruments

`ci/test-parallelism-verdict.sh` (27 cells) proves every gate the verdict applies can fire, each
requiring its **named** diagnostic rather than merely a non-zero exit. `ci/test-parallelism-aba-seam.sh`
(4 cells) covers what that harness structurally cannot see — that the driver writes files of the
shape the verdict reads — by running the real driver against a 12-test synthetic ctest project and
feeding its real output to the real verdict, including a negative arm so the accept is a judgement
rather than a default. Both run in tier 1's `ci-script-pins`, and the seam check runs again as the
campaign's own pre-flight, where 40 s buys back three suite runs.
