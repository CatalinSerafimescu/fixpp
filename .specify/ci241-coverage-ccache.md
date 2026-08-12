# #241 — ccache the `linux-clang-coverage` lane

## 1. What is actually under test

`tier1.yml` did not *omit* ccache from the coverage lane. It recorded a **decision**:

> deliberately left UNCACHED to protect coverage-mapping integrity

So #241 is not "add a step". It is an **equivalence proof that refutes a recorded decision**. The
burden is on this change to show that an object served from ccache is the *same object* a fresh
compile produced — because identical bytes cannot carry a different `__llvm_covmap` /
`__llvm_covfun` / `__llvm_prf_*` section.

## 2. The instrument

Two `workflow_dispatch` runs on `ci/coverage-ccache`, probe mode ON:

| | run | role |
|---|---|---|
| cold | `31519511922` (2026-08-11 17:49→20:02 UTC) | populates the cache, hashes every `.o` |
| warm | `31568303088` (2026-08-12 05:58 UTC) | restores the cache, hashes every `.o` again |

`sha256sum` over `find . -name '*.o'` in the build dir, sorted, uploaded as an artifact.
The manifest carries a non-circularity gate: **`::error::` below 1000 objects**, because an empty
manifest would "match" the other run's and read as a clean proof.

### ⚠️ The manifest is evidence ONLY when paired with the hit rate

A cold-vs-cold pair is trivially identical and proves nothing. The warm run must *independently*
report a high hit rate, or the "comparison" is between two fresh compiles.

### ⚠️ Why NOT the `DA:`/`BRDA:` diff §3 originally asked for

Those are **execution counts**, produced by `llvm-cov` from profdata merged across ~359 test
binaries in a codebase with async/threaded paths. Two byte-identical builds produce different
counts, so that diff measures **test nondeterminism**, not cache fidelity — it would need a
two-cold-run control just to find its own noise floor. Object identity is deterministic, needs no
ctest run, and is **strictly stronger**: it *implies equality of the mapped line/function/branch
inventory after execution counts are stripped* (which is what §5c actually measures) rather than
sampling it.

## 3. Acceptance criteria — FIXED 2026-08-12 06:05 UTC, BEFORE the warm run reported

Recorded ahead of the measurement so the band cannot be fitted to the number it is meant to judge
(the failure mode in `feedback_timing_band_witness_range_admits_the_mutant_it_claims_to_kill`).

| manifest diff | warm hit rate | verdict |
|---|---|---|
| **empty** | **≥ 99.0 %** | **PASS — the recorded decision is refuted.** Proceed to revert + PR. |
| empty | 90.0 – 99.0 % | **PARTIAL — record, do not claim proof.** See §3a. |
| empty | < 90.0 % | **ARTIFACT SUSPECTED** — go to §3b before concluding anything. |
| **non-empty** | any | **INVESTIGATE** — enumerate the differing objects and cluster them by target before calling it a refutation of *this change*. A handful in one target is a lead; a spread across all targets is the recorded decision being right. |

### 3a. Why a high hit rate is load-bearing, not decoration

The hit rate and the manifest measure **different populations**, and the aggregate cannot say
*which* objects missed. Any object that missed in the warm run was **freshly compiled in both runs**,
so its manifest line is a cold-vs-cold comparison contributing zero evidence. At a 99 % hit rate,
~15 of the 1486 lines are inert; at 80 %, ~300 are, and the proof is mostly self-comparison.

What survives this caveat is the shape of the claim being refuted. "ccache corrupts coverage
mapping" is a **systematic** claim: if true, it would show up across essentially every cache-served
object, not in fifteen of them. Zero differences across ~1470 cache-served objects refutes it. The
honest statement of the result is therefore *"N objects were cache-served and are byte-identical to
their cold-compiled counterparts"* — not *"the cache is proven correct for all 1486"*.

### 3b. The overnight-drift discriminator (only run this on the low-hit branch)

`CCACHE_COMPILERCHECK=content` rehashes the **compiler binary**, and clang-22 comes from
`apt.llvm.org`. If the package moved between the two runs, the warm run legitimately hits ~0 % and
buys nothing. **That is a measurement artifact, not a refutation, and not grounds to close #241
negative.**

The implication runs one way for free: **a high hit rate proves the compiler did not move**, so no
check is needed on the PASS branch. Reserve the comparison for the low-hit branch, where it is the
discriminator between artifact and real result.

Cold run recorded `Ubuntu clang version 22.1.8 (++20260714014902+ca7933e47d3a-1~exp1~20260714135019.80)`
— a **2026-07-14** snapshot, i.e. clang-22 is on a release branch and is *not* rebuilt daily. The
drift risk is materially smaller than first assumed. Note that a matching version *string* does not
prove the *binary* is unchanged; only the hit rate does.

## 4. Population accounting — 1461 vs 1486 vs 1489

Three different numbers appear in the cold run. All three are explained; none is a discrepancy.

| number | source | what it counts |
|---|---|---|
| **1461** | outer ninja (`1459` CXX + `2` C `Building … object` lines) | compile edges the top-level build reported |
| **1486** | the manifest | `.o` files on disk under `build/linux-clang-coverage` |
| **1489** | `ccache --print-stats` `cacheable_calls` | every compiler invocation through the launcher |

- **1486 − 1461 = 25** — the **`_codegen_bootstrap/` nested sub-build** (`fixpp_core`,
  `fixpp_dictionary`, `fixpp_wire`, `fixpp-codegen`). It is a nested CMake/ninja invocation, so its
  edges are not echoed with the outer ninja's `[n/m]` prefix, but its objects are on disk and every
  one of them is under a `.dir/` — verified, zero manifest entries fall outside a `.dir/`.
- **1489 − 1486 = 3** — compiler invocations that leave no object in the build tree
  (configure-time `try_compile`-class probes, which inherit `CMAKE_CXX_COMPILER_LAUNCHER` and whose
  scratch dirs are deleted).

Consequence for §3: the hit-rate denominator (1489) is 3 larger than the manifest population
(1486). At 0.2 % this does not move any threshold, but it is why the criteria are stated as a
percentage rather than an exact miss count.

## 5. Cold-half result (recorded 2026-08-12)

Run `31519511922`, job `linux-clang-coverage`: **success, 132.8 min** (17:49:24 → 20:02:10).

```
coverage-objects: 1486 objects hashed
ccache: hits=14 (direct=9 preprocessed=5) misses=1475 cacheable_calls=1489
ccache: size=922 MiB cap=1907 MiB (48% full) cleanups_this_run=0
ccache-hitrate 0% over 1489 cacheable calls (linux-clang-coverage), restore=`n/a`
```

- **0 % is correct on a cold run** and is not a finding.
- `cleanups_this_run=0` at 48 % of a 2 GB cap ⇒ **`max-size: 2G` is not binding**; no object was
  evicted mid-run, so the warm run's misses cannot be blamed on cache pressure.
- Saved as `ccache-tier1-linux-clang-coverage-2026-08-11T20:02:00.630Z`, **901 MiB**, on
  `refs/heads/ci/coverage-ccache`.
- `tier1-required` went RED **by design** — it `needs:` a job probe mode skips. That RED is the
  tripwire proving probe mode is still on.

## 5b. Warm-half result and VERDICT (recorded 2026-08-12 06:35 UTC)

Run `31568303088`, job `linux-clang-coverage`: **success, 33.4 min** (05:58:33 → 06:31:56).

```
coverage-objects: 1486 objects hashed
ccache: hits=1487 (direct=1487 preprocessed=0) misses=2 cacheable_calls=1489
ccache: size=925 MiB cap=1907 MiB (48% full) cleanups_this_run=0
ccache-hitrate 99% over 1489 cacheable calls (linux-clang-coverage), restore=`n/a`
```

### ✅ VERDICT: PASS — the "deliberately left UNCACHED" decision is REFUTED

Against the bands fixed in §3 *before* this run reported:

| criterion | required | measured |
|---|---|---|
| manifest diff | empty | **empty** — 1486 vs 1486 lines, `diff` silent, and the two manifests share one whole-file sha256 `dcf17d78…21408f` |
| warm hit rate | ≥ 99.0 % | **99.87 %** (1487 / 1489) |

**The §3a residual is far tighter than budgeted.** Only **2** of 1489 calls missed. Since 3 calls
produce no object at all (§4), **at least 1484 of the 1486 manifest objects were served from cache
and are byte-identical to their cold-compiled counterparts** — which holds because all 1486
manifest entries are CMake target objects under a `.dir/` (verified per-file against the artifact)
and both compiler launchers are set as job-level env vars, so the manifest population is a subset
of `cacheable_calls`; `1489 − 1461 − 25 = 3` is the residual. That is 99.87 % of the population,
versus the ~15 inert lines the 99 % band would have allowed. The proof is not resting on
self-comparison.

**Independent bound, from wall clock alone.** Cold `Build` step 17:53:52→19:39:45 = 6353 s; warm
`Build` 06:02:24→06:09:52 = 448 s. The ratio **14.2×** is core-count independent. The warm 448 s
must also absorb 1487 cache-hit retrievals, the uncacheable C++20 module scan, the codegen
bootstrap, and the link of ~350 test binaries — fresh compiles in the warm run are therefore
bounded at **O(10²), not O(10³)**, even under the most hostile reading. Against a claim ("ccache
corrupts coverage mapping") that is systematic by construction, that is refutation with three
orders of magnitude of margin; it does not rescue the literal number 1484, but it rescues the
conclusion.

**§3b is discharged without running it.** A 99.87 % hit rate under `CCACHE_COMPILERCHECK=content`
*proves* the compiler binary did not move overnight; the version string matching
(`22.1.8 ++20260714014902+ca7933e47d3a…` in both logs) is corroboration, not the evidence.

Note the hit *kind* also shifted as theory predicts: cold `direct=9 preprocessed=5`, warm
`direct=1487 preprocessed=0` — a fully populated direct-mode cache, not preprocessor fallback.

### How to re-run this comparison

`tier1.yml` quotes the manifest digest truncated (`dcf17d78…21408f`). The **full** digest, identical
for both runs, is:

```
dcf17d7820fa6fff2390ff5eb297b07f28a1dffff0e3c1f18baeb9942d21408f
```

The manifests themselves are uploaded as run artifacts, live as of this writing:

| run | artifact | expires |
|---|---|---|
| cold `31519511922` | `coverage-objects-sha256-31519511922-1` | 2026-11-09 |
| warm `31568303088` | `coverage-objects-sha256-31568303088-1` | 2026-11-10 |

To re-derive §5b's verdict independently, from a clone of `main` with `gh` authenticated:

```bash
gh api repos/<owner>/<repo>/actions/runs/31519511922/artifacts   # -> artifact id
gh api repos/<owner>/<repo>/actions/artifacts/<id>/zip > cold.zip && unzip cold.zip -d cold
gh api repos/<owner>/<repo>/actions/runs/31568303088/artifacts   # -> artifact id
gh api repos/<owner>/<repo>/actions/artifacts/<id>/zip > warm.zip && unzip warm.zip -d warm
diff cold/coverage-objects.sha256 warm/coverage-objects.sha256   # expect: empty
sha256sum cold/coverage-objects.sha256 warm/coverage-objects.sha256   # expect: both dcf17d78…21408f
```

⚠️ **90-day horizon.** Both artifacts expire on the dates above. After expiry this section and the
checked-in `.specify/ci241-coverage-objects.sha256.gz` (one copy — the two runs' manifests are
byte-identical) are the only surviving evidence; re-download and archive before expiry if the
question ever needs reopening. The manifest-generation step itself was reverted after the proof —
recovered here verbatim from `git show 4343fb20 -- .github/workflows/tier1.yml` (⚠️ inline, not
cited by SHA: squash-merge is enabled on this repo, so `4343fb20` may not be reachable from a clone
of `main` after merge):

```yaml
- name: Coverage-object manifest (#241 equivalence proof)
  run: |
    set -euo pipefail
    cd build/linux-clang-coverage
    # Sorted for a stable diff; paths are relative to the build dir so the
    # two runs' manifests are directly comparable.
    find . -name '*.o' -type f -print0 \
      | sort -z \
      | xargs -0 sha256sum \
      > /tmp/coverage-objects.sha256
    n=$(wc -l < /tmp/coverage-objects.sha256)
    echo "coverage-objects: $n objects hashed"
    # Non-circularity: an empty or near-empty manifest would "match" the
    # other run's and read as a clean proof. Assert the tree is really there.
    if [ "$n" -lt 1000 ]; then
      echo "::error::only $n objects found (expected ~1460) — manifest is not a valid basis for the equivalence proof"
      exit 1
    fi
    echo "### #241 coverage-object manifest: $n objects" >> "$GITHUB_STEP_SUMMARY"
```

### Measured saving

| | cold | warm | Δ |
|---|---|---|---|
| `Build` step | 105.9 min | **7.5 min** | **−98.4** |
| whole job | 132.8 min | **33.4 min** | **−99.4** |

`Build` step is 17:53:52 → 19:39:45 (cold). The previously recorded 107.8 min was the
Configure-start → manifest-end span, not the `Build` step alone; corrected on remeasurement.

Tier-1 runner-minutes: **376 → ~277** (−26 %). ⚠️ **Quote the right latency axis** — coverage was
100 % of *tier-1's* wall clock, so tier-1 wall drops sharply, but Tier 2 ran 100 min and Tier 3
83 min on the same push, so **all-tier latency stays ~100 min (Tier 2-bound)**. The −99 is
tier-1-local.

## 5c. End-to-end corroboration on the LCOV report — and the near-miss it exposes

The object diff is the proof, but the sentence being refuted was written about **coverage-mapping
integrity**, so the downstream artifact deserves a look. Both runs upload `coverage-lcov`. Comparing
them costs zero runner-minutes and answers the obvious Gate B question — *"you diffed `.o` files;
did the coverage output actually match?"*

**Raw bytes differ** (`8fc33bd4…` vs `9b47deac…`, 1716128 vs 1716283 bytes), and that is not a
finding. Two independent reasons, both benign, both measured:

1. **`SF:` block ordering is nondeterministic.** Cold leads with `include/fix/c_api.h`, warm with
   `include/fixpp/tls/cipher_policy.hpp`. The *set* of 146 source files is identical.
2. **Execution counts differ**, as predicted.

Parsed per-`SF:` and compared order-insensitively:

| | result |
|---|---|
| `SF:` file sets | **identical**, 146 = 146 |
| **structural inventory** — `FN:` entries, `DA:` line numbers, `BRDA:` (line, block, branch) tuples, counts stripped | **identical for 146/146 files** |
| full records *including* counts | matches for only **99/146**; **47 files differ in counts alone** |

The structural inventory is exactly what the coverage *mapping* determines — *what* is instrumented,
not *how often it ran*. It is identical across the pair, at every file. That is end-to-end
corroboration at the level the original sentence was written at.

### ⚠️ The 47 files are the near-miss, and they justify §3's instrument choice by measurement

Had `DA:`/`BRDA:` counts been the acceptance instrument — as the original §3 plan proposed — this run
pair would have reported **47 of 146 files "differing"** and read as a **refutation**. On a pair whose
1486 objects are provably byte-identical. It would have been a **false negative that killed a correct
change.**

And the 47 are not random: `wire/parser.hpp`, `wire/tag_scan.hpp`, `wire/framer.hpp`,
`session/engine.hpp`, `tls/pinset.hpp` — the parsing- and concurrency-heavy headers, i.e. precisely
where run-to-run execution counts should be expected to move.

This was argued a priori in §3 ("that diff measures test nondeterminism, not cache fidelity").
It is now **measured**: the divergence observed in this pair is 32 % of files; n=1, so this is not
a floor — §3 notes a floor would need a two-cold-run control.
See `feedback_an_acceptance_instrument_built_on_execution_counts_measures_test_nondeterminism`.

## 6. Coverage needs its OWN ccache namespace

It is Debug like the matrix debug leg, but the coverage profile folds
`-fprofile-instr-generate` / `-fcoverage-mapping` into `tools.build:cxxflags` **and**
`tools.info.package_id:confs`, so the whole dependency closure resolves to different
`~/.conan2/p/<hash>` paths ⇒ different `-isystem` ⇒ a different hash on every dependency-touching
TU. Sharing the debug leg's key would be a permanent ~0 % hit rate, not a saving.

⚠️ `cmake/Codegen.cmake:190-209` does **not** forward `CMAKE_{C,CXX}_COMPILER_LAUNCHER` to the
`_codegen_bootstrap/` nested sub-configure — it forwards `-DCMAKE_CXX_COMPILER` / `-DCMAKE_C_COMPILER`
only. The 25 bootstrap objects (§4) reach ccache purely by process-env inheritance from the
job-level `env:` block. Moving the launcher from job-level `env:` to a `-D` on the top-level
configure would silently drop those 25 objects out of the cache.

## 7. Revert checklist (only after §3 reads PASS)

Do **not** revert before the proof is in hand: a §3b artifact verdict requires re-running cold+warm
same-day, which requires probe mode still ON.

1. Revert the five `PROBE MODE (#241)` markers. **Re-derive the count from the tree** rather than
   trusting this line.
2. Confirm the reverted `save:` predicate is **character-identical** to the matrix legs' — #241 adds
   the **seventh** copy of that publish predicate.
3. **Gate the revert on `tier1-required` going GREEN**, not on re-reading the five markers. Its RED
   is the tripwire's positive state; its GREEN is the dual, and only that proves probe mode is off.
   ⚠️ **That dual cannot be checked cheaply now** — `push:` is `main`-only and a PR skips the
   matrices until both gate labels land, so the only way to force it today is a full
   `workflow_dispatch` (~277 runner-min). It is therefore checked **at the merge gate**: the PR's own
   post-label run must show `tier1-required` GREEN. That run has to happen anyway, so this costs
   nothing extra — but it means **the revert is not fully verified until then**, and that is
   deliberately not claimed before it.
4. Drop the manifest step (probe scaffolding). **Keep `ci/ccache-stats.sh`.**
   ⚠️ **Corrected on measurement:** the assumption that "the matrix legs already call it, so keeping
   it is consistency" is **false** — `grep -n "ci/ccache-stats.sh"` returns exactly **one** caller,
   the coverage lane itself. Keeping it makes coverage the **first** lane with stats. Still the right
   call (a silent fall to ~0 % hits is otherwise invisible — the lane just gets slow — and it is the
   direction **#248** wants), but it must be pitched in the PR as **new surface**, not as matching
   existing practice.
5. `gh cache delete ccache-tier1-linux-clang-coverage-<stamp>` — the probe entry is ~900 MB of the
   shared 10 GB pool and can LRU-evict main's caches.
6. Rewrite tier1.yml's "deliberately left UNCACHED" paragraph with the proof, citing both run IDs.

## 8. What the PR promises — and what it does not

Stated narrowly and up front, because an unbounded promise generates unbounded review findings
(the mechanism that ran PR #258 to nine rounds against a cap of four).

**Promises:** coverage gets its own ccache namespace; object identity measured on the run pair
`31519511922` / `31568303088`; probe scaffolding reverted.

**Does not promise:**
- that ccache is safe for *every future* coverage configuration — the proof is against this
  compiler, this profile, this flag set;
- **that the win appears immediately. It does not — the first TWO coverage runs are both cold.**
  Actions cache scoping plus a push-only `save:` makes the timeline:

  | run | reads | cost | writes? |
  |---|---|---|---|
  | this PR's own CI | main's scope + the PR's — **neither holds a coverage entry** | **~133 min, cold** | no (`save:` is push-only) |
  | first push to `main` after merge | main's scope — still empty | **~133 min, cold** | **yes** |
  | every `main` run after that | main's fresh entry | **~33 min** | yes |

  So **the first warm coverage run is the second post-merge run.** The ~900 MiB probe entry on
  `refs/heads/ci/coverage-ccache` does *not* shorten the PR run — a feature branch's cache is
  unreachable from a `refs/pull/N/merge` scope — which is why it is deleted rather than kept
  (`feedback_pr_scoped_actions_caches_unreachable_but_evict_main`: it cannot help, and it can
  LRU-evict main's caches).

  Anyone measuring the PR run, or the merge commit's run, will read a cold 133 min and conclude the
  change does not work. It is stated here and in tier1.yml so that conclusion is pre-empted;
- any change to `DA:`/`BRDA:` coverage numbers, which are not what this instrument measures.

## 9. Pool budget — what this permanently costs

The Actions cache pool is **10 GB per repository**, shared by every lane.

⚠️ **Methodology, corrected on measurement.** `gh api …/actions/cache/usage` is *not* the
authority — it is eventually consistent and lags deletes, as this repo already recorded at
`.github/workflows/cache-cleanup.yml:207-209` ("verify by RE-LISTING, not by reading
`actions/cache/usage`… lagged several minutes behind confirmed deletes during the 2026-08-02
sweep"). This section previously cited the API figure (6.92 GiB) as authoritative against a
per-entry `gh cache list` sum of 6.04 GiB and called the list "under-reporting" — backwards. The
6.92 GiB reading was an accurate read of a **lagging** endpoint taken just after the 901 MiB probe
entry was deleted: `6.04 + 0.88 = 6.92` GiB exactly. Verify by re-listing.

Re-measured 2026-08-12 by re-listing (`gh cache list`), corroborated against a fresh
`actions/cache/usage` call — the two now **agree**:

⚠️ **Units — the trap this table originally fell into.** GitHub's cap is **10 GB decimal**
(10,000,000,000 bytes = 9.31 GiB). Percentages must be taken against that, not against 10 GiB.
Mixing the two understated the projection by 6 points on the first draft of this section.

| | GiB | GB | % of the 10 GB cap |
|---|---|---|---|
| current pool (9 active entries) | 6.04 | 6.49 | **64.9 %** |
| what #241 adds, permanently | 0.90 | 0.97 | +9.7 pts |
| **projected steady state** | **6.94** | **7.46** | **74.6 %** |
| remaining headroom | 2.37 | **2.54** | 25.4 % |

The four sanitizer/debug legs alone are 5.4 GiB of the current total (ubsan 1774 + asan 1524 +
tsan 1462 + debug 740 MiB). #241's own entry measured 901 MiB cold / 925 MiB warm; the steady-state
row uses the larger, warm figure.

**74.6 % is affordable and has real headroom**, and it is not reversible without reverting the
feature. ~2.5 GB is what absorbs the next cached lane — note that **#259** (`python-wheel-build`,
also uncached, 67.4 min) is queued behind this and will want its own namespace too. Re-derive this
table by re-listing (`gh cache list`) rather than trusting the API's `usage` endpoint alone; these
entries grow with the tree.

### The per-cycle transient — pre-existing, all-lane, not introduced by #241

`hendrikmuhs/ccache-action` writes `<key>-<ISO8601 stamp>` and never deletes its predecessor; the
keep-newest sweep that collapses old stamps to one per namespace fires on `workflow_run: completed`
for the tier workflows (`.github/workflows/cache-cleanup.yml:96-105`), i.e. *after* every save. So
each `main` Tier-1 cycle asks the pool to absorb a **second full generation** of ccache writes
before the sweep runs — measured today at **~6.0 GB**, projected **~7.0 GB post-#241**, against the
10 GB cap. This is *not* a peak-pool-size claim: GitHub evicts on write, so the 12.5 / 14.4 GB
double-generation total is never actually reached. All 9 entries from the 2026-08-11 main cycle
survived — one per namespace, none lost — because LRU orders by last-access and the superseded
generation is read at job *start* while the new one is written at job *end*, so eviction
preferentially clears the old generation before it can crowd out a live one. The mechanism predates
this PR and applies to all seven ccache lanes; #241 adds roughly 1 GB to a condition that already
existed and has held.

Both probe entries were deleted once the proof was recorded:
`…-2026-08-11T20:02:00.630Z` (cold, 901 MiB) and `…-2026-08-12T06:31:49.527Z` (warm, 925 MiB).
