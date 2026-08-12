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
ctest run, and is **strictly stronger**: it *implies* `DA:`/`BRDA:` equality rather than sampling it.

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
and are byte-identical to their cold-compiled counterparts** — 99.87 % of the population, versus the
~15 inert lines the 99 % band would have allowed. The proof is not resting on self-comparison.

**§3b is discharged without running it.** A 99.87 % hit rate under `CCACHE_COMPILERCHECK=content`
*proves* the compiler binary did not move overnight; the version string matching
(`22.1.8 ++20260714014902+ca7933e47d3a…` in both logs) is corroboration, not the evidence.

Note the hit *kind* also shifted as theory predicts: cold `direct=9 preprocessed=5`, warm
`direct=1487 preprocessed=0` — a fully populated direct-mode cache, not preprocessor fallback.

### Measured saving

| | cold | warm | Δ |
|---|---|---|---|
| `Build` step | 107.8 min | **7.5 min** | **−100.3** |
| whole job | 132.8 min | **33.4 min** | **−99.4** |

Tier-1 runner-minutes: **376 → ~277** (−26 %). ⚠️ **Quote the right latency axis** — coverage was
100 % of *tier-1's* wall clock, so tier-1 wall drops sharply, but Tier 2 ran 100 min and Tier 3
83 min on the same push, so **all-tier latency stays ~100 min (Tier 2-bound)**. The −99 is
tier-1-local.

## 6. Coverage needs its OWN ccache namespace

It is Debug like the matrix debug leg, but the coverage profile folds
`-fprofile-instr-generate` / `-fcoverage-mapping` into `tools.build:cxxflags` **and**
`tools.info.package_id:confs`, so the whole dependency closure resolves to different
`~/.conan2/p/<hash>` paths ⇒ different `-isystem` ⇒ a different hash on every dependency-touching
TU. Sharing the debug leg's key would be a permanent ~0 % hit rate, not a saving.

## 7. Revert checklist (only after §3 reads PASS)

Do **not** revert before the proof is in hand: a §3b artifact verdict requires re-running cold+warm
same-day, which requires probe mode still ON.

1. Revert the five `PROBE MODE (#241)` markers. **Re-derive the count from the tree** rather than
   trusting this line.
2. Confirm the reverted `save:` predicate is **character-identical** to the matrix legs' — #241 adds
   the **seventh** copy of that publish predicate.
3. **Gate the revert on `tier1-required` going GREEN**, not on re-reading the five markers. Its RED
   is the tripwire's positive state; its GREEN is the dual, and only that proves probe mode is off.
4. Drop the manifest step (probe scaffolding). **Keep `ci/ccache-stats.sh`** — it is standing
   observability, and it moves tier1 the direction #248 wants. Confirm the matrix legs already call
   it, so keeping it makes coverage *consistent* rather than adding a step.
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
- **that the first post-merge coverage run will be fast.** Once `save:` is back to `push:main` only,
  `main` has no coverage cache until the first post-merge push builds one. **That run is still
  ~133 min.** Anyone measuring immediately after merge will otherwise read it as the change not
  working;
- any change to `DA:`/`BRDA:` coverage numbers, which are not what this instrument measures.
