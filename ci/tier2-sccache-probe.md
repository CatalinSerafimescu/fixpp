# Tier 2 (MSVC) compiler cache — sccache probe

**Status:** WIRED, **not yet measured on CI**. Issue [#231]. Sibling of
[`ctest-parallelism-probe.md`](ctest-parallelism-probe.md), and deliberately the same shape: a change
that is kept only if a measurement says to keep it.

Tier 1 / Tier 3 keep `hendrikmuhs/ccache-action`. This is not a proposal to unify them — sccache is
here because ccache's MSVC support is still experimental while sccache has treated `cl.exe` as a
first-class compiler for years.

---

## What this addresses, and what it does not

Tier 2 had **no compiler cache at all**: every leg recompiled every TU on every run. Measured, run
[30880318695] (all three legs green):

| leg | Configure | **Build** | Test | job total |
|---|---|---|---|---|
| `windows-msvc-debug` | 1m17s | **80m13s** | 21m20s | 104m |
| `windows-msvc-release` | 1m05s | **92m15s** | (rest) | 104m |
| `windows-msvc-asan` | 3m53s | **84m43s** | 80m57s | 171m |

**AC5 — stated explicitly, because it is the easy thing to lose:** this changes the **Build** half
only. The **`ctest` half is not improved at all** — 81 minutes of the asan leg's 171, which sccache
does not touch. That is [#229]'s territory. In particular this is **not** a fix for the asan leg's
171m/180m proximity to `timeout-minutes`, which is test-dominated; the stale *"the MSVC matrix runs
~45 min cold"* comment at the `timeout-minutes` declaration was corrected in the same change, and
the backstop was deliberately **not** raised.

---

## Prerequisites — verified before wiring, not inferred

#231 flagged its own `/Zi` claim as *"an inference, not a measurement"*. It has been measured.

### 1. `/Zi` → `/Z7` — CONFIRMED, and the failure mode is worse than predicted

Baseline, from a real configured Debug tree's `build.ninja` (local MSVC sandbox, OTel-ON, toolset
`14.44.35207` — the same toolset the runner reports):

```
total FLAGS lines: 3235   distinct: 4
  2904  /DWIN32 /D_WINDOWS /EHsc /Ob0 /Od /RTC1 -std:c++latest -MDd -Zi
   325  /DWIN32 /D_WINDOWS /EHsc /Ob0 /Od /RTC1 -MDd -Zi
     4  /DWIN32 /D_WINDOWS /EHsc /Ob0 /Od /RTC1 -std:c++latest -MDd -Zi /bigobj
     2  /DWIN32 /D_WINDOWS /Ob0 /Od /RTC1 -MDd -Zi
```

**3235 of 3235 carry `-Zi`; zero carry `/Z7`.** CMP0141 is `NEW` (`cmake_minimum_required(3.28)`) and
nothing in `CMakeLists.txt` or `cmake/*.cmake` overrides `MSVC_DEBUG_INFORMATION_FORMAT`, so the
CMake default `$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>` applies, exactly as #231 predicted.

⚠️ **Correction to #231's expected failure shape.** The issue expected a missed `/Z7` to show up as
*"a near-zero hit rate with a large non-cacheable bucket"*. It does not. Measured on a 3-TU control
project (§3 below, step 7) — `sccache` + `/Zi`, everything else identical:

```
C:\temp\sccache-probe\b.cpp: fatal error C1041: cannot open program database
  '...\probe.pdb'; if multiple CL.EXE write to the same .PDB file, please use /FS
```

with `/FS` **already on the command line**. `Non-cacheable compilations 0`, `Compilation failures 2`.
So `/Zi` under sccache does not degrade to slow-but-green — **it goes red**. Better than a silent
false-green, but it means the `/Z7` prerequisite is load-bearing for the lane building at all, not
merely for it being fast. That is why the workflow asserts it (§4) instead of trusting it.

### 2. No PCH — confirmed

`grep -rn target_precompile_headers` over the CMake tree returns nothing. sccache's MSVC PCH handling
is a known rough edge; it is not in play here.

### 3. Launcher-vs-Conan ordering — made unrepresentable, not merely ordered

`tier1.yml:392-404` records the trap: a **job-level** `CMAKE_{C,CXX}_COMPILER_LAUNCHER` makes
`conan install --build=missing` run a dependency's compiler probe through the launcher, and a
launcher not yet on `PATH` fails that probe with **exit 127**. `abseil/20260107.1` made it concrete
on Tier 1; the same fix later landed on Tier 3 ([#177]).

Tier 2 passes the launcher with `-D` **on the Configure step only**, so Conan's dependency builds
never see it. Deps arrive prebuilt from GHCR on this lane by design, so caching them would buy
little, and `-D` scoping makes the exit-127 shape unrepresentable rather than ordered-around. The
`env:` block on the `windows` job carries a comment saying so, because the natural "fix" for a future
reader is to hoist the launcher into it.

---

## The mechanism, proven end-to-end locally before spending CI minutes

3-TU standalone project, MSVC `14.44.35207`, Ninja, the **exact quoted argument string** the
Configure step passes. Full run: `sccache v0.17.0`.

| step | result |
|---|---|
| Configure **Debug** | `... -MDd -Z7 ... /FdCMakeFiles\probe.dir\probe.pdb /FS -c ...` — genex → `Embedded` |
| Configure **Release** | `... /O2 /Ob2 /DNDEBUG -std:c++20 -MD ...` — **no** debug-info flag; genex left it alone |
| launcher on the line | `sccache "…\cl.exe" /nologo /TP …` |
| **cold** build | `Compile requests 3 · Cache hits 0 · Cache misses 3 · 0.00 %` |
| **warm** build (objects wiped, cache kept) | `Compile requests 3 · Cache hits 3 · Cache misses 0 · **100.00 %**` |
| **control**: same, `/Zi` (no override) | **build FAILS**, `C1041` ×2 — see §Prerequisites 1 |

Two things this settles that a CI run would have settled more expensively:

- **The `cmd` quoting survives.** `"-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"`
  reaches CMake intact; the surrounding double quotes are what stop `cmd` reading `<`/`>` as
  redirection. The value is CMake's own default genex with `ProgramDatabase` swapped for `Embedded`,
  **not** a bare `Embedded` — a bare value would put `/Z7` on `windows-msvc-release`, which emits no
  debug info today, and silently starting to would change what *ships*, not just how fast it builds.
- **`/Fd` and `/FS` stay on the command line under `/Z7`, and caching works anyway.** CMake emits
  them regardless of the debug-information format; with `/Z7` `cl` does not actually write the PDB,
  so there is no out-of-band side effect for sccache to fail to reproduce. Worth recording because
  seeing `/Fd…probe.pdb` on a cached compile looks wrong and is not.

### ⚠️ Ceiling: the module-dependency scan is NOT cached

`ninja -t commands` on a real fixpp `.obj` is **85 lines**, and the launcher is on exactly one of
them. The `-scanDependencies` pass — one `cl.exe` invocation per TU, emitting the `.ddi` that feeds
`cmake -E cmake_ninja_dyndep` — runs **without** the launcher:

```
"…\cl.exe" … -std:c++20 -MDd -Z7 a.cpp -nologo -TP -showIncludes -scanDependencies …a.cpp.obj.ddi -Fo…
sccache "…\cl.exe" /nologo /TP … -Z7 /showIncludes @…a.cpp.obj.modmap /Fo… /Fd… /FS -c a.cpp
```

CI build directories are not persisted, so **every** run pays a full 3235-TU dependency scan that
sccache cannot recover, warm or cold. This is a hard ceiling on the achievable Build-step saving and
must be read off the measured numbers rather than modelled. Whether that pass is needed at all
(`CMAKE_CXX_SCAN_FOR_MODULES` — fixpp ships no C++20 modules) is a **separate** question worth its
own issue; it is deliberately not bundled here.

---

## Design — storage backend

**GHCR/oras, mirroring the Conan package** (`ci/restore-sccache.sh`, `ci/seed-sccache.sh`,
`ci/sccache-cache-key.sh` — the same three-file split as the Conan trio, and for the same stated
reason: a key the two sides compute differently is silently a permanent MISS).

Explicitly **not** the GHA backend that `mozilla-actions/sccache-action` enables by default.
`tier2.yml:203-208` records that the `actions/cache` Conan step was *deliberately deleted* from this
lane: it wrote ~790 MB per PR into the repo-wide **10 GB** Actions-cache pool, under a
`refs/pull/<n>/merge` scope nothing can read once the PR closes. `SCCACHE_GHA_ENABLED=true` would put
this lane straight back into that pool with a larger payload.

#231 sanctioned a throwaway GHA spike to get a hit-rate number cheaply. It was **not** taken, because
`workflow_dispatch` bypasses the `filter` job's gate check outright (*"Event … is not a
pull_request — Tier 2 runs unconditionally"*), so the cold→warm A/B is reachable on the branch
against the **mergeable** design directly. The spike would have cost the same runner hours and
produced a number about a configuration that was never going to ship.

### Why the tag is coarse where the Conan tag is content-hashed

`sccache-<preset>-<VCToolsVersion>`, e.g. `sccache-windows-msvc-asan-14.44.35207`.

The Conan tag hashes `conanfile.py` + the profile because a Conan package is opaque and a wrong HIT
relinks against a foreign STL — the failure that forced the 2026-07-19 revert (main `327d7665`).
sccache has no such mode: its entry hash covers the compiler binary, the full argument list and the
**preprocessed source**, so a stale entry cannot be wrongly reused, only missed. Correctness is
sccache's job here, not the tag's — which inverts the goal, because a content-hashed tag would miss
on exactly the commits this cache exists to speed up.

The toolset is still in the tag, for **cost** rather than safety: after a VS image bump every entry
misses internally anyway, so restoring would download multiple GB to achieve nothing. Folding
`VCToolsVersion` in turns that slow useless HIT into a fast MISS.

### The honest consequence, stated up front

Seeding is gated to `push:main` / `workflow_dispatch` — never a PR. **PR legs therefore get a
read-only cache whose provenance is main**, and a header-churning PR gets partial benefit at best.
Same bargain the Conan package already makes (*"MISS is never fatal"*).

Publishing happens **after Build and before `ctest`**: the cache is a compilation artifact, and
withholding it because an unrelated test went red would make the next run pay a full cold build.

### Open item — package visibility

`ghcr.io/catalinserafimescu/fixpp-sccache` is created by the first seed and is **private** by
default, unlike `fixpp-conan-cache` which was made public by hand. The restore step therefore
`oras login`s first (`|| true`, so a fork PR's downgraded token falls back to an anonymous pull).
**If the package is left private, fork-PR legs will read as a permanent MISS** — visible as a 0% hit
rate in the stats, not as a failure. Making it public matches the Conan package and removes the
asymmetry.

---

## Acceptance criteria

Numbering follows #231.

| # | criterion | status |
|---|---|---|
| 1 | `sccache --show-stats` in the job summary on every leg — requests, hits, **and** the non-cacheable/failure breakdown | **WIRED** — `if: always()`, so a red build still reports them. Not yet measured on CI. |
| 2 | warm-vs-cold `Build` wall time, back-to-back **on the same PR in the same session** | **PENDING** — two `workflow_dispatch` runs on this branch, cold then warm. Not a cross-day A/B: on GitHub runners that measures host drift as much as code (`feedback_bench_ab_needs_same_session_control_host_drifts`). |
| 3 | one-TU `ninja -v` / `-t commands` excerpt confirming `/Z7` and the launcher | **WIRED as a GATE, not an excerpt** — see §4 below. Locally proven RED on an unfixed tree. |
| 4 | all three legs green, incl. the `Assert the packaging tier is registered` count check on `windows-msvc-release` | **PENDING** |
| 5 | explicit statement of what was **not** improved | **MET** — the `ctest` half, 81 min on the asan leg; see the top of this file. Cross-ref [#229]. |

### AC3 is a gate

`Assert the sccache prerequisites are on the cl command line` runs between Configure and Build and
fails the leg if the launcher is missing, if `/Z7` is missing on a Debug-config leg, if `/Zi` survived
alongside it, or if `windows-msvc-release` acquired a debug-info flag it does not have today.

This exists because every claim sccache rests on is invisible from the outside. Moving this lane to
the **Visual Studio generator** would silently drop the launcher — `CMAKE_<LANG>_COMPILER_LAUNCHER` is
honored by Ninja/Makefiles and *ignored* by the VS generator, which is the only reason this is
wireable here at all (`CMakePresets.json:9`). A preset change restoring `/Zi` would do the same. Both
turn the cache into a 0%-hit no-op on a lane that still reports green.

**Proven non-vacuous:** run against the *unfixed* tree (the local sandbox's `main` build), the three
checks report `ABSENT: sccache`, `ABSENT: -Z7`, `present: -Zi` — i.e. all three fire. A verification
grep never shown to be non-zero is a broken instrument, not a clean sweep
(`feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree`).

---

## Keep-or-revert

Keep only on a demonstrated hit rate on a warm re-run (AC2 + AC1 together). A large measured saving
on the two Debug-config legs with a poor one on `windows-msvc-release`, or an upload/download cost
that eats the saving, are both live outcomes — the seed step prints the archive and on-disk sizes
into the job summary so that trade is visible rather than assumed.

[#231]: https://github.com/CatalinSerafimescu/fixpp/issues/231
[#229]: https://github.com/CatalinSerafimescu/fixpp/issues/229
[#177]: https://github.com/CatalinSerafimescu/fixpp/issues/177
[30880318695]: https://github.com/CatalinSerafimescu/fixpp/actions/runs/30880318695
