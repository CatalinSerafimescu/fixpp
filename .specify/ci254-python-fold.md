# Design Doc CI-254 — Fold the Python bindings into the `linux` matrix legs

> **Status:** Draft v0.2 — Gate A round 1 converged (Phase A). Not implemented; no workflow, CMake or script file is modified by this doc.
> **Date:** 2026-08-09
> **Owner:** CI / `.github/workflows/tier1.yml` (the `linux` matrix job, the `python-bindings` job, `tier1-required`, `ci/test-tier1-python-policy.sh`) **and** `bindings/python/CMakeLists.txt` (the Python install control, §4.5).
> **Issue:** [#254](https://github.com/CatalinSerafimescu/fixpp/issues/254) — *fold python-bindings into the linux matrix legs*.
> **Branch:** `ci/fold-python-bindings-into-matrix` (worktree `~/Work/Programming/fixpp-parallel`).
> **Supersedes:** **#244 part 2** (outright — the work it caches is deleted); **#244 part 1**'s python-side *mechanism* (restore-only ccache + probes, deleted with the job it served); **shrinks #248** (**one** `run:` block — the preset→sanitizer derivation — is extracted into a tested `ci/` script, and the cross-job ccache probe steps stop existing — §5b.2, §6.2) and **#253** (`python_touched` collapses to the wheel jobs by deletion).
> **Convergence:** addresses the Codex Gate A review (P1=1 P2=6 P3=3) and the Opus adversarial review (P1=3 P2=7 P3=6; **4 root causes**). Per-finding resolution in **Appendix C**; round log in **Appendix B**.
> **Evidence pointer:** `research/G19-fix-fpml-iso20022/remaining-work/ci-compiler-cache-pass-plan.md` §"~~#244 part 2~~ — SUPERSEDED by #254", and the three comments on issue #254. Primary CI evidence is run **`31273945846`** (2026-08-08). Every quantitative claim is restated with provenance and an **M / D / I** tag in **Appendix A**, under the tagging rule fixed in §Appendix A's preamble.
>
> **Scope class:** **CI/workflow + one CMake install control.** This is *not* a CI-only change, and v0.1's "CI/workflow only" banner was false. The change edits `.github/workflows/tier1.yml`, adds `ci/derive-python-sanitizer.sh`, re-points `ci/test-tier1-python-policy.sh`, repairs comments in `ci/test-ccache-scripts.sh` and `.github/workflows/cache-cleanup.yml`, re-points the L-056-4 invariant in four files, **and adds one boolean option plus one `message(STATUS)` to `bindings/python/CMakeLists.txt`** (§4.5). No C++, no C ABI, no allocator, no hot path, no public C++ surface. The `/gate-a-ph2` library-subsystem sections (public C++ API, C ABI, PMR recap, latency ceilings, `[[clang::lifetimebound]]`, pure-virtual caps) remain **not applicable** and are deliberately absent rather than emitted as empty N/A stubs.
>
> **What the scope class means for gate labelling.** The prior CI-only PRs — **#227 / #245 / #247 / #250 / #251** — each took a **Gate-A waiver on the "CI-only, no product code" precedent**. That precedent does **not** extend to this PR: `bindings/python/CMakeLists.txt` is product build wiring that governs what `cmake --install` and CPack emit for *every* consumer, not just CI. Two mechanical consequences, stated separately because they have different causes:
> 1. **The workflow-level Gate-A trigger fires on the design doc, not on the CMake edit.** `TRIGGER_RE='^(include/|src/|codegen/|\.specify/[^/]+\.md$)'` (`tier1.yml:213`) does not list `bindings/`. Committing this file matches `^\.specify/[^/]+\.md$`, which is also **explicitly excluded** from the pure-doc auto-waive (`:197–202`). So `gate-a-done` / `gate-a-waived` is required either way.
> 2. **The CMake edit is what makes a *waiver* inappropriate.** The waiver rationale used on #227/#245/#247/#250/#251 was "no product code touched". That sentence is false here. This PR should carry **`gate-a-done`** (this doc, through `/gate-a-ph2`), not `gate-a-waived`.
>
> **Cites (process / constitutional — see §11 for the normative-reference disposition):** `[const §XVII.1]` (*"Any new design document under `.specify/` … qualifies by default"* — verified at `.specify/constitution.md:340`; why this doc exists at all), `[const §XVII.7]` (local pre-PR build gate, incl. `pytest bindings/python/tests/` **if the change touches `bindings/python/`** — `:354`; see §8 PG-9, which is now literally triggered), `[const §XVII.8]` (verify/label evidence rule — `:359`), `[const §IX.1]` (*"no uncovered error path without an explicit assessment — that is the enforced gate; the percentage is the target"* — `:200`), `[const §IX.2]` (Tier 1 must run ASan/UBSan/TSan on every PR — `:204`), `[const §IX.6]` (Tier 1 = *"…Python pytest…"*, i.e. pytest is a required Tier-1 constituent, not an optional lane — `:213`), `[const §VII.2]` (*"Python tests: pytest against the SWIG bindings"* — `:172`), `[const §VI.5]` (Normative References — `:164`, §11).
>
> **Numbering note:** the section list follows the Gate A brief, with two additions — **§5b**, the `ci/test-tier1-python-policy.sh` contract, which is **the edit the brief's four-edit list omits entirely** and is a required-check trap of the same class as §5, so it sits next to it rather than buried in §6; and **§11**, Normative References per `[const §VI.5]`. ⚠️ **Edit ordinals** are assigned in §4 and are the authoritative ones: Edit 1 = apt (§4.2), Edit 2 = derive + Configure (§4.3), Edit 3 = the two test steps (§4.4), Edit 4 = the Python install control (§4.5), **Edit 5 = `tier1-required` (§5), Edit 6 = the pin (§5b)** (§4.6). v0.1 called the pin "the fifth" against the brief's four-edit list; v0.2 adds the CMake control, so it is now the **sixth** — the ordinal moved, the point did not.

---

## 1. Goals

1. **Delete the `python-bindings` job** (`tier1.yml:1033–1587`, ~555 lines) and make the Python bindings one more thing each `linux` matrix leg builds and tests — not a job of its own. The payoff is **four runners** and, **on the event classes where the job actually ran**, up to **316 gross runner-min/run**. That number is *gross deleted job time*, not a net saving, and on a PR that does not match `PY_RE` the change is net **negative** runner-minutes. §3.3 states the saving per event class; do not quote a single headline (Appendix A, E-11/E-29/E-30).
2. **Normalise across all six `linux-*` legs**, including `linux-clang-release` and `linux-gcc-release`, which have never built the bindings. This is a *coverage gain*, not merely a move: the SWIG wrapper has never seen gcc-13, `-O2`, or `NDEBUG` (Appendix A, **E-17**).
3. **Derive the sanitizer identity from the preset, once, fail-closed, in a tested `ci/` script** — replacing today's third spelling (`FIXPP_PYTHON_SANITIZER=<san>` hand-passed alongside `FIXPP_ENABLE_<SAN>=ON` and the preset name) with a single derivation, executable as itself, consumed by every downstream step (§4.3).
4. **Keep the C++ release artifacts free of Python** while the bindings build on the Release legs — a **new** goal in v0.2, and a pre-merge blocker. `FIXPP_BUILD_PYTHON=ON` activates four unconditional `install()` rules that reach `fixpp-package`, the uploaded `packages-linux-{clang,gcc}-release` artifacts, and the all-six-legs consumer stage-install. §4.5 adds the control that prevents it; §7 R6 and §8 PG-3 carry the evidence.
5. **Move the required-check contracts together with the deletion** — `tier1-required` (§5) *and* the `ci/test-tier1-python-policy.sh` regression pin (§5b). Either one left behind turns a required check permanently red on **every** non-release run.
6. **Make free disk an observation at four points, with the instrument itself fail-closed** — per the #244-part-1 ccache-liveness precedent, the #229 peak-memory step's discipline, and the recipe issue #254 itself specifies (§4.7).
7. Leave the workflow's *reasons* in the workflow, and repair every live site the deletion falsifies — the dispositioned census in §4.8, not a prose count.

## 2. Non-goals

Each of these is load-bearing; a design that quietly does any of them is a different change.

| # | Non-goal | Why |
|---|---|---|
| NG-1 | **Do not remove the `free-disk-space` step.** | It is **measured margin**, not a vestige: `+26 GiB` reclaimed against a `linux-clang-ubsan` tree of **32.96 GiB**. The 2026-06-25 ENOSPC (run `28184395001`) died at edge **1651/1748**, ~94 % through, deep in the link phase — exactly as `bin/` fills. (Appendix A, E-6/E-7/E-10.) ⚠️ v0.1 also claimed the tree "is still growing (1748 → 3627 edges since June)". **That claim is withdrawn** — today's 3627 includes 1460 module-scan edges and the June denominator's graph composition is not shown to be comparable. NG-1 stands on the margin alone. |
| NG-2 | **Do not design *deliberate* packaging of Python artifacts.** | Issue §4 was **DROPPED by user decision, 2026-08-09**, in favour of **#255** (a per-package wheel, option (b) — link the shared C ABI). This doc designs no `packages-<preset>` **addition**, no artifact-name dimension, no abi3-vs-version-specific choice. ⚠️ **v0.1's split between *deliberate* and *accidental* packaging is retained as a distinction but no longer as a deferral.** #255 owns *deciding* to ship Python at package level. **#254 owns not shipping it by accident while the decision not to ship it is in force** — which is why §4.5 lands here and not in #255. |
| NG-3 | **Do not touch `python-wheel-build` / `python-wheel-test`.** | The wheel is built by cibuildwheel inside a **manylinux_2_28 container** with its own in-container Conan profile (`bindings/python/cibw-before-all.sh:26` runs `conan profile detect --force`) and its own cache key — zero object sharing with the clang matrix legs is possible, cached or not; and it is compiled against the limited API and audited with `abi3audit --strict`. `python-wheel-test`'s 4-leg 3.10–3.13 matrix **is the abi3 feasibility witness**: collapsing it deletes the only evidence that one wheel serves all four interpreters. ⚠️ It is also the **ON-side witness for §4.5** and it fires on this very PR — see §4.5.4. |
| NG-4 | **Do not narrow `PY_RE`.** | Merged, `python_touched` governs the wheel jobs alone — which is exactly the split **#253** asks for, arrived at by deletion. Narrowing the literal (e.g. dropping `conan/profiles/linux-clang-*`, which the wheel jobs never read) is **#253's** to take, together with the pin's `PY_RE_CASES` table (`ci/test-tier1-python-policy.sh:200–220`). Doing it here would put two changes through one review. |
| NG-5 | **Do not claim a wall-clock win.** | Tier 1's wall is bounded by `linux-clang-coverage` at **134.8 min**, already longer than the longest python leg (113.1 min); the run's end-to-end wall was **135.2 min**. This change frees runners, not minutes on the critical path. That lever is **#241**. |
| NG-6 | **Do not fold `coverage` in, or touch its cache posture.** | Out of scope, and it is the critical path (NG-5) — #241 owns it. |
| NG-7 | **Do not change `continue-on-error`, `fail-fast`, or the per-leg concurrency groups.** | The matrix already keys its concurrency group on `matrix.preset` (`tier1.yml:290–292`), which is correct for six legs that each grow a python tail. |
| NG-8 | **Do not change the default of the new install control, and do not gate it on `SKBUILD`.** | `FIXPP_INSTALL_PYTHON` defaults **ON** (§4.5). An `if(DEFINED SKBUILD)` guard is the tempting one-liner and it **regresses feature 056**: LAY-1 / D-4 / T006 deliberately fixed the **in-tree `cmake --install` path too**, not only the wheel path (`specs/056-python-wheel-packaging/{tasks.md:112,research.md:142,checklists/packaging.md CHK002}`). Only CI sets it OFF. |

## 3. Current state

### 3.1 The two shapes

| | `linux` matrix job (`tier1.yml:266–821`) | `python-bindings` job (`tier1.yml:1033–1587`) |
|---|---|---|
| legs | **6** presets: `linux-clang-{debug,release,asan,ubsan,tsan}`, `linux-gcc-release` | **4**: `none`, `asan`, `tsan`, `ubsan` |
| configure | `cmake --preset <preset>` (`:520–528`) | `cmake --preset linux-clang-debug` **+** `FIXPP_ENABLE_<SAN>=ON` **+** `BUILD_SHARED_LIBS=ON` **+** `FIXPP_PYTHON_SANITIZER=<san>` **+** `-B build/<san>-py` **+** a `CMAKE_TOOLCHAIN_FILE` override (`:1394–1404`) |
| build dir | `build/<preset>` (preset `binaryDir`, `CMakePresets.json:10`) | `build/linux-clang-debug` (`none`) / `build/linux-clang-<san>-py` (the three sanitizer legs) |
| Conan profile | `conan/profiles/<preset>` (`:488–493`) | `conan/profiles/<conan_profile>` — aligned per leg by #243/PR #251 (`:1377–1392`) |
| tests | **ctest**: 350 tests, 30.5 min on ubsan. **No pytest** for the bindings (pytest *is* installed — the `decimal_compare_oracle` CTest entry needs it, `:383–392`) | **pytest** only: 82 tests, **11.8 s**. No ctest |
| `cmake --install` | **yes, on all six legs** — `fixpp::consumer::install-witness` stage-installs (`:569–581`, `tests/consumer/run_consumer_witness.cmake:48–56`) | **never** — the deleted job runs no install and no CPack |
| CPack | `fixpp-package` on both Release legs (`:806–808`), uploaded (`:812–821`) | none |
| timeout | `240` (`:284`) | `180` (`:1040`) |
| gating | `proceed` (both gate labels) — **not** path-gated | `proceed` **and** `python_touched` (`:1046–1049`) |
| free-disk step | yes (`:339–348`) | yes (`:1195–1204`) — a duplicate |
| ccache | full install + restore + **save on push** (`:435–486`) | `none` leg only, **restore-only**, sharing the matrix debug leg's key (`:1303–1309`) + a restore-check probe (`:1340–1375`) + a stats/liveness step (`:1430–1549`) |

**The legs are complementary, not redundant in what they *test*** — matrix runs ctest and no pytest; python runs pytest and no ctest. Merged runs both. What *is* redundant is the ~1460-TU compile underneath, done twice on two runners.

⚠️ **The `cmake --install` / CPack row is the one that changes the scope class.** The deleted job never executed an install rule. The matrix legs execute install rules on **all six** legs and CPack on two. Folding the bindings in therefore does not merely move a build — it puts four previously-unreached `install()` rules onto a path that produces a shipped artifact. That is §4.5.

### 3.2 How close the two configurations already are — stated at the strength the evidence supports

⚠️ **v0.1's claim "the merged configuration already runs in CI, green" is RETRACTED.** It is not supported, and it had travelled into the #254 issue comment, the plan doc and a memory; the retraction must land in all four places, not only here. The defensible narrower claim is:

> On run `31273945846`, with the `-py` build-dir suffix normalised away, the ninja **edge set** of `python-bindings (ubsan)` (**3632**) was a **strict superset** of matrix `linux-clang-ubsan`'s (**3627**) by exactly **5** edges, with **zero** edges unique to the matrix leg. That establishes that the two **build graphs** merge into one tree with no second compile — **and nothing else.** No merged job has ever run: not the ctest→pytest ordering, not the Release configurations, not `cmake --install` with the bindings present, not CPack with them present.

Sharper, and the reason this matters twice: **the 5 extra edges are precisely the edges that carry the install rules.** The evidence that the merge is cheap and the evidence for R6 are the same five edges.

The rest of §3.2's measurements stand, retagged:

- **Command-level, not just graph-level, evidence exists and v0.1 omitted it.** #244 part 1 measured **1457/1457 compile commands byte-identical** between the matrix `linux-clang-debug` tree and the python tree once the build dirs are named the same, and established that `FIXPP_BUILD_PYTHON=ON` *"adds exactly one compile edge and perturbs nothing else — it only gates `add_subdirectory(bindings/python)"*. The gating is three lines at `CMakeLists.txt:333–335` (verified). This is stronger than the edge-set diff for the debug shape, and it is the **mechanism** behind §9's ccache claim: one ccache miss per leg per run (the SWIG wrapper edge), which #247 measured exactly (Appendix A, **E-31**).
- **The hand-assembled configuration is cache-variable-identical to the preset.** Flattening `CMakePresets.json`, `linux-clang-debug` + `FIXPP_ENABLE_UBSAN=ON` + `BUILD_SHARED_LIBS=ON` differs from the `linux-clang-ubsan` preset in **`CMAKE_TOOLCHAIN_FILE` alone**, which `tier1.yml:1403` already overrides. No `linux-*` preset carries an `environment` block or a top-level `toolchainFile`. ⇒ **§4.3's "use the preset" is a readability change, not a behavioural one.** Stated deliberately flat: it is not sold as a correctness fix. It establishes equality of *declared cache variables*, not of generated compiler/link commands, CMake policy state, discovered Python variables, or install scripts — E-5 is tagged **`D+I`** accordingly.
  (Verified in this worktree: `CMakePresets.json:43–82` — the `asan`/`ubsan`/`tsan` presets already carry `BUILD_SHARED_LIBS=ON`, so the python legs' `-DBUILD_SHARED_LIBS=ON` is and always was redundant — and was never applied to the other three presets.)
- **Disk is not the constraint it was.** The runner is plain `ubuntu-24.04`: **145 G disk, 87 G free before the reclaim step, 114 G after**. The ubsan tree is **32.96 GiB** (`bin/` 18 G = 330 executables, `lib/` 6.8 G, `CMakeFiles/` 6.4 G). The bindings add **~126 MiB**, of which `lib/_fixpp.so` is **120.83 MiB**. Everything ctest writes (`build/<dir>/Testing`) is **20 KB**.
  ⚠️ **Two derivation-hygiene corrections to v0.1.** (i) The `114 G` is a **CI** number measured *before the tree exists*; the `32.96 GiB` is a **local** number. Any "×N margin" quotient mixes them, so the operative margin is deliberately **not** stated as a headline quotient — it is **measured at four points, on the leg, every run** (§4.7), with the job-end point being the one that answers NG-1. (ii) The `+0.37 %` precision is dropped: the reconfigure that produced it rebuilt 3053 edges, so it is not a controlled five-edge delta. What survives is the part that is independently checkable with one `du`: **96 % of the delta is one named file**, `lib/_fixpp.so`, which §4.7 reports per leg.
- ⚠️ **Two workflow comments are wrong and one is only *dated*, not wrong.** The `~14 GB free-disk ceiling` claim (`tier1.yml:334–338` and `:1189–1194`) was **accurate when written** (2026-06-26): a 33 GiB tree against ~14 GB free dies during linking, and it did. What changed is the runner image's disk. The comment must be **updated to say the ceiling moved**, not deleted as if it had been wrong (§4.8). The `~1748` / `~1987` "instrumented executables" figures in those same two comments **contradict each other and are both wrong**: the tree is **3627 ninja edges** = 1460 compiles + 1460 module scans + 330 executable links. ⚠️ Note the `1748` in run `28184395001`'s `1651/1748` is a **ninja edge total**, not an executable count — v0.1 used the same number both ways in adjacent sentences. The two usages are now kept apart.

### 3.3 What the numbers say about the win — per event class, not one headline

| event class | deleted job cost | added cost on six merged legs | net |
|---|---:|---|---|
| `push:main` / `workflow_dispatch` / `release` | **316 runner-min** (asan 113.1 · ubsan 99.5 · tsan 95.8 · none 7.6) | apt `swig`+`python3-dev` × 6 · 5 ninja edges × 6 · pytest wall × 6 · 1 ccache miss × 6 | **≈ −(316 − added)**, added is small but **not zero and not yet measured on all six legs** |
| `pull_request` **matching** `PY_RE` | same 316 | same | same |
| `pull_request` **not matching** `PY_RE` | **0** — the job was skipped | same added cost | ⚠️ **net POSITIVE runner-minutes (a cost, not a saving)** |

**Measured PR-side match rate: 11 of the last 30 merged PRs match `PY_RE` (37 %)** — so on **19 of 30 (63 %)** of merged PRs this change *costs* runner-minutes rather than saving them, and buys the §5.2 strengthening instead (Appendix A, **E-30**). That trade is accepted deliberately: the bindings move from *conditionally required* to *unconditionally required*.

Wall-clock, for completeness:

| | measured | note |
|---|---:|---|
| matrix `linux-clang-ubsan` | **39.9 min** | of which ctest 30.5 min |
| merged ubsan (projection) | **≈40 min** | *derived*, not measured — 39.9 + ~12 s pytest + one SWIG TU; against `timeout-minutes: 240` |
| run wall, end-to-end | **135.2 min** | vs `linux-clang-coverage` **134.8 min** — coverage is the critical path |

⇒ **the win is runners and (on two of three event classes) runner-minutes, not wall-clock** (NG-5). **Tier 1 does not get shorter.**

⚠️ Run `31273945846` **predates #251**. Edge counts, the preset comparison and the `df` readings are unaffected by it, but **the runner-minute table must be restated on a warm post-#251 run at close-out** (§8 CO-1, §10 OQ-2).

---

## 4. The merged design

### 4.1 What each leg does, after

Every `linux-*` leg, all six, unconditionally:

```
apt: … + swig + python3-dev                                     (§4.2)
derive:    ci/derive-python-sanitizer.sh "<preset>"  → GITHUB_OUTPUT   (§4.3)
Configure: cmake --preset <preset> -DFIXPP_ARTIFACT_DIR=… \
           -DFIXPP_BUILD_PYTHON=ON \
           -DFIXPP_PYTHON_SANITIZER=<derived> \
           -DFIXPP_INSTALL_PYTHON=OFF                           (§4.3, §4.5)
           + df point 2 ; assert cmake_install.cmake has 0 install rules (§4.5.3)
Build:     cmake --build --preset <preset>       (unchanged; +5 ninja edges)
           + df point 3
ctest:     unchanged (350 tests; packaging tier still gated to linux-gcc-release)
pytest:    exactly one of the two named steps below              (§4.4)
job end:   measure disk (asserting) + report disk (summary)      (§4.7, df point 4)
```

No leg gets a conditional bindings build. A per-leg conditional would reintroduce precisely the branching this change deletes, and would leave the two Release legs in the no-evidence state Goal 2 exists to close.

### 4.2 Edit 1 — the apt line

The matrix job's existing install step (`tier1.yml:373–377`) gains **`swig`** and **`python3-dev`**:

```yaml
sudo apt-get install -y --no-install-recommends \
  gcc-13 g++-13 \
  ninja-build \
  swig \
  python3-dev \
  python3-pip \
  pipx
```

- `swig` — required by `find_package(SWIG 4.2 REQUIRED)` (`bindings/python/CMakeLists.txt:8`). ubuntu-24.04's apt `swig` has satisfied that pin on the deleted job since 053; this is the same package, not a new dependency.
- ⚠️ **`python3-dev` is a correction to the brief.** The brief states *"Only `swig` is new, on an existing apt line."* That is incomplete: the deleted job's apt line (`tier1.yml:1227–1232`) installs **`swig` *and* `python3-dev`**, and the matrix line has neither. The rest of the brief's claim holds and is verified — the matrix job **already** runs `actions/setup-python@v6` (`:379–381`) and `pip install pytest pyyaml` (`:383–392`) on every leg, because the `decimal_compare_oracle` CTest entry needs pytest. Design decision: **mirror the deleted job exactly.** It is the configuration CI has proven green; `python3-dev` costs seconds; and neither the CI evidence nor the local builds can discriminate whether `Development.Module` would have resolved against `actions/setup-python`'s headers alone, because both jobs and both local machines had the system headers present. Dropping it would be an *untested* narrowing bought for nothing. (§10 OQ-5 records the un-discriminated question rather than pretending it was answered.)
- ⚠️ **`python3-dev` also decides which `Python3_SITEARCH` branch fires** (§4.5.2). Mirroring the deleted job keeps that variable pinned to the configuration CI has run; the branch is *read and reported* rather than assumed.
- `python-bindings`' step also installed `pipx`/`python3-pip` and the Clang-22 block — already present on the matrix leg, verbatim.

### 4.3 Edit 2 — the sanitizer identity, derived once, fail-closed, **in a tested `ci/` script**

Today the sanitizer identity is spelled three times per python leg — the preset name, `FIXPP_ENABLE_<SAN>=ON`, and `FIXPP_PYTHON_SANITIZER=<san>` — plus a fourth time in `san_opts` and a fifth in the runtime basename. Two of those (`preset`, `FIXPP_ENABLE_*`) collapse for free: the preset already carries `FIXPP_ENABLE_<SAN>` and `BUILD_SHARED_LIBS` (`CMakePresets.json:43–82`). The remaining three are **derived from `matrix.preset` once** and consumed by expression, never re-spelled.

#### 4.3.1 Decision — an extracted `ci/` script, not an inline `run:` `case`

v0.1 put the derivation inline in a `run:` block and made `ci/test-tier1-python-policy.sh` extract and parse it. **v0.2 reverses that**, on three grounds (this is Codex's counter-proposal, which the adversarial review judged the winner over v0.1's §5b.2 option (b)):

1. **It is what #248 exists to ask for.** #248's thesis, in this repo's own words at `ci/test-ccache-scripts.sh:3–9`, is *"extracting TIER 1's in-workflow `run:` script into a tested `ci/` script"*. Teaching the PyYAML pin to extract a six-arm, quote-nested, `$GITHUB_WORKSPACE`-interpolating bash `case` out of a YAML `run:` block would create a second, larger instance of exactly the problem #248 exists to remove — **in the same change that §6.2 claims shrinks it.** v0.1 contained that contradiction; v0.2 resolves it in favour of shrinking (see §6.2).
2. **PR #247 Gate B round 5 already declined this.** F1 was **waived** for precisely this reason and filed as **#248**. Choosing the inline form would re-take a waiver the repo declined to live with.
3. **A `run:` block cannot be faithfully tested any other way.** `feedback_actions_runs_run_blocks_as_a_bash_script_file_not_bash_c` — Actions executes `run:` as a **bash script file** (`bash -e {0}`), and a probe that exercises it as `bash -c` is a false RED/GREEN generator. An executable script is testable **as itself**.

The existing pin already shows the ceiling: assertion 5 extracts `decide_run` and evaluates `PY_RE` *behaviourally* — but that works only because `PY_RE` is a **single-line literal**. The §4.3.2 mapping is not.

#### 4.3.2 `ci/derive-python-sanitizer.sh` — contract

```
usage: ci/derive-python-sanitizer.sh <preset>
stdout (exactly three KEY=VALUE lines, GITHUB_OUTPUT-shaped):
  sanitizer=<none|asan|ubsan|tsan>
  rt_base=<''|asan|ubsan_standalone|tsan>
  san_opts=<''|ASAN_OPTIONS=…|UBSAN_OPTIONS=…|TSAN_OPTIONS=…>
exit 0 on a known preset; on anything else: `::error::…` on stderr and exit 1.
```

**The mapping, exhaustive over the matrix:**

| `matrix.preset` | `sanitizer` | `rt_base` (`libclang_rt`) | `san_opts` |
|---|---|---|---|
| `linux-clang-debug` | `none` | — | — |
| `linux-clang-release` | `none` | — | — |
| `linux-gcc-release` | `none` | — | — |
| `linux-clang-asan` | `asan` | `asan` | `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` |
| `linux-clang-ubsan` | `ubsan` | **`ubsan_standalone`** | `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1` |
| `linux-clang-tsan` | `tsan` | `tsan` | `TSAN_OPTIONS=suppressions=$GITHUB_WORKSPACE/bindings/python/tests/tsan_suppressions.txt:halt_on_error=1` |
| anything else | **fatal — `exit 1`** | — | — |

Four properties are load-bearing, and each names the false-green it prevents:

1. **Unknown preset ⇒ `::error::` + `exit 1`, never a silent `none`.** A defaulted `none` on a future sanitizer leg builds an uninstrumented `_fixpp.so` and reports green — the **#251 class exactly** (an uninstrumented thing reporting green for a whole run). `[const §IX.2]` makes the sanitizer legs a *required* signal, so a leg that silently stops being instrumented is a constitutional false-green, not a cosmetic bug.
2. **`san_opts` derives from the same discriminant in the same call.** A sanitizer leg that loses `halt_on_error=1` runs, finds, prints — and exits 0. One discriminant, one place.
3. **No `contains(matrix.preset, 'san')` sniffing** anywhere in the `if:` conditions. Substring sniffing is the third spelling under a different name, and it also matches `ubsan`/`asan` inside hypothetical future preset names.
4. **`FIXPP_ENABLE_<SAN>` and `BUILD_SHARED_LIBS` are *not* passed** — the preset owns them. Passing them again is how the current job ended up reconstructing a preset by hand.

The workflow step is then two lines:

```yaml
- name: Derive the python sanitizer identity from the preset
  id: pysan
  run: ci/derive-python-sanitizer.sh "${{ matrix.preset }}" >> "$GITHUB_OUTPUT"
```

#### 4.3.3 The Configure step

Becomes (`tier1.yml:520–528` + three lines):

```yaml
- name: Configure
  run: >
    cmake --preset ${{ matrix.preset }}
    -DFIXPP_ARTIFACT_DIR=${{ github.workspace }}/_artifacts
    -DFIXPP_BUILD_PYTHON=ON
    -DFIXPP_INSTALL_PYTHON=OFF
    -DFIXPP_PYTHON_SANITIZER=${{ steps.pysan.outputs.sanitizer }}
```

No `-B` and no `CMAKE_TOOLCHAIN_FILE` override: the preset's `binaryDir` is `build/<preset>` (`CMakePresets.json:10`) and each preset's `CMAKE_TOOLCHAIN_FILE` already points at `build/<preset>/conan_toolchain.cmake`, which is exactly where `conan install … -of build/${{ matrix.preset }}` (`tier1.yml:488–493`) puts it. Both overrides existed only to service the `-py` build dirs, which cease to exist.

### 4.4 Edit 3 — the two test steps, moved in and kept separately named

```yaml
- name: Run Python tests                      # if: steps.pysan.outputs.sanitizer == 'none'
- name: Run Python tests under sanitizer      # if: steps.pysan.outputs.sanitizer != 'none'
```

- Carried over **verbatim** from `tier1.yml:1552–1556` and `:1572–1587`, except: `PYTHONPATH` becomes `${{ github.workspace }}/build/${{ matrix.preset }}/lib`, and the three matrix-driven values come from `steps.pysan.outputs.*` instead of `matrix.*`.
- The `libclang_rt` resolution moves unchanged, including both naming schemes (`libclang_rt.<base>-x86_64.so`, then `$(clang -print-runtime-dir)/libclang_rt.<base>.so`) and the **`ubsan` → `ubsan_standalone`** mapping. (Note for the reader of the diff: `bindings/python/CMakeLists.txt:104–137` already links the asan/ubsan module against a *shared* sanitizer runtime with an rpath, so `LD_PRELOAD` is belt-and-braces for `import` and load-bearing for the *subprocess children* of the watchdog/canary tests. It is preserved as-is; this change is not the place to test that hypothesis — §10 OQ-6.)
- ⚠️ **Sanitizer options stay per-invocation.** `tier1.yml:1580–1587` scopes them as `env <opts> LD_PRELOAD="$RT" pytest`, and `PYTHONPATH` is set on the step, not the job. **`san_opts` must NOT be promoted to a job-level `env:`** during implementation — that would silently change the C++ ctest run in the same job. Verified: nothing set for pytest reaches ctest or vice versa today.
- **Two steps, not one.** This is the mitigation for R2 (§7): a python failure is attributable to a named step from the checks UI without reading the job log. `continue-on-error` stays **off** on both.
- **Placement:** immediately after the two ctest steps (`:600–603`, `:778–780`) and **before** the packaging block (`:806–821`). Rationale: (a) the C++ ctest signal is produced before any python step can stop the job, so a python failure never preempts it; (b) a red python leg then uploads no packages, which matches the existing intent recorded at `:783` — *"Runs only after a green test step, so a red lane uploads nothing."*
- On the three `none`-mapped legs the sanitizer step is skipped and vice versa; the two are jointly exhaustive over the six legs by construction of the §4.3.2 table (the same "mutually exclusive by `if:`, jointly covering" shape the workflow already uses at `:560–563`).

### 4.5 Edit 4 — **the Python install control** (`bindings/python/CMakeLists.txt`)

**This is the edit that makes #254 not a CI-only change, and it is a pre-merge blocker.** It is stated as a design decision, not an open question — v0.1's R6 posture ("this doc deliberately asserts neither 'it breaks' nor 'it's fine'") was defensible before the measurement and is not defensible against a standing user decision.

#### 4.5.1 Why it is #254's, and pre-merge

The warrant is not R6's own risk analysis; it is **the user's standing decision, which names an outcome, not an intent.** Issue #254's §4-dropped comment (2026-08-09): *"The Release legs still build and test the bindings under §1's normalisation … **they just do not package them.**"* Corroboration, and the reason this is not deferrable:

- `tier1.yml:806–808` runs `cmake --build --target fixpp-package` on **both** `linux-gcc-release` and `linux-clang-release`; `:812–821` uploads `packages-linux-{clang,gcc}-release` with `if-no-files-found: error`. These are the **shipped C++ deliverable**.
- `tests/consumer/run_consumer_witness.cmake:48–56` runs `cmake --install "${FIXPP_MAIN_BUILD_DIR}" --prefix "${_stage}"` and is registered on **all six** legs (`tier1.yml:569–581`, asserted `expected=1`). So *every* leg executes the install rules after this change, not only the Release ones.
- **L-056-4** (`spec/behaviors-and-limitations.md:1649`) records as delivered behaviour: *"the `packages-linux-{clang,gcc}-release` artifacts are the **C++** consumer deliverable and contain no Python."* Repeated **verbatim in three more files** — `tier1.yml:1599–1610`, `bindings/python/pyproject.toml:11–14`, `bindings/python/cibw-before-all.sh:23–26` — one of which is the very note §4.8 promises to *keep and re-point*. Shipping the accidental payload would make a note this change deliberately preserves **false**.
- **The existing witnesses cannot see it.** `tests/packaging/run_package_contents_witness.cmake` is a MUST-BE-PRESENT list plus a **7-pattern denylist** (`:637`), an archive-format extension set (`:100–110`), an archive count derived from the shipped `fixppTargets.cmake`, and a raw-listing `usr/` prefix check (`:330–352`). A root-level `_fixpp.so` / `fixpp.py` / `_fixpp_data/` matches **none of the seven**. The consumer witness checks for *specific* leaks (`fixpp/_dispatch/`, `fixpp/vt11/`) and for *required* content — there is no allowlist. **Both are structurally blind to added root-level files.** So a `ctest -L 'packaging|consumer'` 9/9 green with bindings ON is a **false-green**, and v0.1's AC-3 could not have discharged R6.
- **There is no workflow-only lever.** Every lane that fires the rules is unavoidable. Suppressing an `install()` rule requires a CMake change.

**#255 owns *deliberate* package-level Python shipping** — which shape, which ABI, which audience. **#254 owns not shipping it by accident while the decision not to ship it is in force.**

#### 4.5.2 The control

```cmake
option(FIXPP_INSTALL_PYTHON
       "Install the Python binding artifacts (module, pure-Python modules, _fixpp_data)" ON)
```

guarding the four `install()` calls at `bindings/python/CMakeLists.txt:196 / 197 / 205 / 216`, plus one reporting line:

```cmake
message(STATUS "fixpp: FIXPP_INSTALL_PYTHON=${FIXPP_INSTALL_PYTHON}; "
               "Python3_SITEARCH='${Python3_SITEARCH}'; "
               "FIXPP_PY_INSTALL_DIR='${FIXPP_PY_INSTALL_DIR}'")
```

⚠️ **Default ON is load-bearing (NG-8).** It preserves feature 056's LAY-1 / D-4 / T006 decision that the in-tree `cmake --install` path is correct. **Do not substitute `if(DEFINED SKBUILD)`** — that gates the rules on the *wheel* build and silently regresses a delivered 056 requirement. Only the six `linux` matrix legs set it OFF.

⚠️ **OFF does not affect the tests.** In-tree pytest runs from `PYTHONPATH=build/<preset>/lib`, staged by the `POST_BUILD` copy, **not** by `install()` — stated in the file's own comment at `:188–189`. Turning the install rules off on the matrix legs therefore removes nothing the pytest step depends on.

**The `FIXPP_PY_INSTALL_DIR` branch — three reachable outcomes, and the design must name which one CI is in.** The current logic (`:190–194`) is:

```cmake
if(DEFINED SKBUILD OR NOT Python3_SITEARCH)   set(FIXPP_PY_INSTALL_DIR ".")
else()                                        set(FIXPP_PY_INSTALL_DIR "${Python3_SITEARCH}")
endif()
```

| branch | what happens with the control ON | what an install-prefix `find` diff sees |
|---|---|---|
| `"."` (SITEARCH unset) | payload lands at the **install-prefix root** → package pollution | **sees it** |
| absolute SITEARCH, **writable** (e.g. `actions/setup-python`'s tool-cache `site-packages`) | `cmake --install --prefix X` **ignores the prefix** for an absolute DESTINATION → files land outside **both** staging prefixes, silently polluting the runner's interpreter | **BLIND — both prefixes identical, delta empty, a prefix-diff criterion reads "no change" and PASSES** |
| absolute SITEARCH, **not writable** (e.g. `/usr/lib/python3/dist-packages` from `python3-dev`) | `cmake --install` fails → `run_consumer_witness.cmake:55–56` `FATAL_ERROR` → **all six legs RED at ctest** | never reached |

⚠️ **Measured locally (2026-08-09, `build/linux-clang-tsan-py/bindings/python/cmake_install.cmake`): the `"."` branch fires** — the generated file carries **4** `file(INSTALL …)` directives, with destinations `${CMAKE_INSTALL_PREFIX}/.` and `${CMAKE_INSTALL_PREFIX}/./_fixpp_data`, i.e. `_fixpp.so`, `fixpp.py`, `fixpp_oo.py`, `fixpp_dict_data.py` and `_fixpp_data/__init__.py` + four XMLs at the install-prefix root.
⚠️ **The local probe does not speak for CI.** On CI, `actions/setup-python@v6` runs before Configure, so `Python3_SITEARCH` may well be populated and a *different* branch may fire. **Which branch fires on CI is not established by any measurement this doc has**, and that is why the branch is *reported* (the `message(STATUS)` above, visible in the Configure log on every leg every run) rather than assumed.

**Once the control is OFF on the six legs, all three branches are harmless there** — no python install rule executes at all, so the pollution branch, the prefix-blind branch **and the all-six-legs-red branch are closed together.** The branch read is kept anyway because the wheel path and any in-tree `cmake --install` by a real consumer still depend on it, and because a future change that flips the control back must be able to see which outcome it is walking into.

#### 4.5.3 The OFF-side assertion — a step that exits non-zero, not a PR-body diff

Immediately after Configure, on every leg:

```yaml
- name: Assert the Python install rules are OFF (#254 / L-056-4)
  run: |
    set -euo pipefail
    f="build/${{ matrix.preset }}/bindings/python/cmake_install.cmake"
    [ -f "$f" ] || { echo "::error::$f absent — FIXPP_BUILD_PYTHON=ON should have generated it."; exit 1; }
    n=$(grep -c '^[[:space:]]*file(INSTALL' "$f" || true)
    echo "python install directives in $f: $n (expected 0)"
    if [ "$n" != "0" ]; then
      echo "::error::FIXPP_INSTALL_PYTHON=OFF but $n install directive(s) remain."
      echo "::error::These would enter fixpp-package and the packages-linux-* artifacts, falsifying L-056-4."
      exit 1
    fi
```

Three properties, each deliberate:

1. **The instrument is proven non-zero on the unfixed tree — and the fixed side is now measured too, so E-32 is a PAIR, not a half.** An absence-check never shown to return non-zero is a broken instrument, not evidence (`feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree`); a zero never shown to be *reachable* is an untested claim. Both sides, 2026-08-09:

| tree | control | `grep -c '^[[:space:]]*file(INSTALL' …/bindings/python/cmake_install.cmake` | file present |
|---|---|---:|---|
| `build/linux-clang-tsan-py` (configured **before** the option existed) | unfixed | **4** | yes |
| `build/probe-none-B` (reconfigured, 19 s, **no rebuild**) | **OFF** | **0** | yes — 44 lines, and **zero** `_fixpp` / `fixpp*.py` / `_fixpp_data` references anywhere in it |

   The OFF row closes the *"NOT YET MEASURED"* residual v0.2 carried. It cost one reconfigure of an existing tree: regenerating `cmake_install.cmake` does not require a build, which is why the earlier "needs a fresh tree, too expensive" disposition was wrong. The same reconfigure printed `fixpp: FIXPP_INSTALL_PYTHON=OFF; Python3_SITEARCH=''; FIXPP_PY_INSTALL_DIR='.'` — the `message(STATUS)` of §4.5.2 firing, and independent confirmation that **locally** the `"."` branch is the one in play. **CI's branch remains unestablished** (§4.5.2) — reported there, not assumed.
2. **It is a positive existence check first.** If the generated file is missing entirely the step fails, so a `0` cannot be produced by the file having silently stopped being generated (which would also pass a naive count).
3. **It is self-contained.** It needs no pre-change baseline artifact, so it runs identically on the first CI run and forever after — unlike "compare the manifest with the pre-change one", which has no baseline in CI.

The **local** two-prefix manifest diff (configure with and without `-DFIXPP_BUILD_PYTHON=ON`, `cmake --install … --prefix /tmp/p{on,off}`, `diff` the `find` listings) is retained as **§8 PG-3's local half** — it is the instrument that names the *content* of the delta on the `"."` branch. It is explicitly **not** the CI gate, because on the absolute-SITEARCH branch it is structurally blind (§4.5.2 table, row 2).

#### 4.5.4 The ON-side witness already exists and fires on this PR

No new ON-side assertion is needed. `python-wheel-test` installs the **shipped wheel** and imports it across 3.10–3.13, exercising the install rules with the control at its default ON. Because `PY_RE` contains `\.github/workflows/tier1\.yml$` (`tier1.yml:161`) — and, after this edit, `^bindings/python/` as well — **`python_touched=true` on the #254 PR itself**, so that witness fires on this very PR. **This is what makes R6 dischargeable inside #254 rather than deferrable to #255.**

⚠️ **The two halves get signal at opposite ends of the PR's life**, and the implementer should expect that: the OFF-side signal arrives with `ci-script-pins`, which is **ungated** and fires before any gate label exists (§9 item 5); the ON-side wheel witness is gated on `proceed` and lands only once `gate-a-*` and `gate-b-*` are applied.

#### 4.5.5 The two **semantic** install witnesses — R2-P2-1 and R2-P2-2, as implemented

`bindings/python/run_python_install_witness.cmake`, registered from
`bindings/python/CMakeLists.txt` as **exactly one** `ctest` test per build, decided by the option:

| `FIXPP_INSTALL_PYTHON` | registered test | asserts |
|---|---|---|
| **ON** (default — every ordinary build) | `fixpp::python::install-present-witness` | module + `fixpp.py` + `fixpp_oo.py` + `fixpp_dict_data.py` + `_fixpp_data/__init__.py` + the 4 XMLs **are** staged — feature 056 LAY-1 / D-4 / T006 (**R2-P2-2**) |
| **OFF** (tier 1's six `linux` legs) | `fixpp::python::install-absent-witness` | **no** `_fixpp*`, `fixpp.py`, `fixpp_oo.py`, `fixpp_dict_data.py`, `_fixpp_data` anywhere in the staged tree — L-056-4 (**R2-P2-1**) |

⚠️ **`DESTDIR`, not `--prefix`, and that is a correctness requirement, not a style choice.** §4.5.2's
own table row 2 says why: on the absolute-`Python3_SITEARCH` branch the DESTINATION is absolute, and
`cmake --install --prefix X` **ignores the prefix** for an absolute destination — so a prefix-staged
scan sees an empty delta and **PASSES** while the payload lands on the real interpreter. `DESTDIR`
prepends to absolute destinations, so it captures both branches; the scan therefore walks the **whole**
staging root, not `${stage}${prefix}`. ⚠️ **Do not copy `tests/consumer/run_consumer_witness.cmake:48–56`,
which uses `--prefix`** — it answers a different question (a relocatable C++ package whose destinations
are all prefix-relative). This blindness would **not** reproduce locally, where `Python3_SITEARCH` is
empty and the `"."` branch fires; that is exactly what makes it worth spelling out.

**Proven RED with no synthetic self-test.** Each mode's negative evidence is the *other* configuration
of the same tree — the four cells are measured, not asserted (Appendix A, **E-35**). A planted-file
self-test was considered and rejected: it would test the scanner, not the install.

**And proven on the REAL invocation path, not only via `cmake -P`.** The four cells drive the script
directly; CI drives it through `ctest`, which supplies a different working directory and environment.
Both halves are now closed on the same tree: `ctest --test-dir build/probe-none-B -L python` with
`-DFIXPP_BUILD_TESTS=ON -DFIXPP_INSTALL_PYTHON=OFF` runs **`fixpp::python::install-absent-witness`,
1/1 Passed in 1.93 s** (E-35). Registration was separately confirmed both ways with `ctest -N`.
Verifying registration and execution *separately* would have left exactly the seam this closes.

⚠️ **`absent` registers NOWHERE until §4.3.3 passes `-DFIXPP_INSTALL_PYTHON=OFF`.** It is inert by
construction until then, which is correct — but it means a typo in that flag on the six legs would
leave the witness silently not running and the §4.5.3 grep (the instrument R2-P2-1 judged
*insufficient*) as the only survivor. **The workflow change must assert the test RAN** (a `ctest -N`
count), not merely that ctest was green; `tier1.yml:569–581`'s `expected=1` on the consumer witness is
the precedent. `feedback_ci_gate_observes_not_asserts_witness_skips_into_green`.

**Residual, narrowed at Gate B round 1 (F6).** Round 1 was right that a current-**basename** denylist
does not durably support the broad L-056-4 claim, and it named the escapes: a later
`fixpp_helpers.py`, a `.pyi` stub, a stray `.pyc`, an SOABI-tagged module renamed off the `^_fixpp`
prefix. The reject set is now **extension-based as well as name-based** (`\.pyi?$`, `\.pyc$`,
`\.cpython-*.(so|pyd)$`), which follows provenance across renames and new files. ⏱ Validated safe:
the real OFF-side install stages 258 entries with **zero** `.py`/`.pyi`/`.pyc`. Counter-test: a staged
`fixpp_helpers.py` **now reds** `absent` (it passed before).

⚠️ **What still escapes, stated rather than left to be found.** The four bundled XMLs are matched only
*via the `_fixpp_data` directory entry* — deliberately, since `FIX42.xml` etc. also exist in the C++
install and in `dictionaries/`, so a name match would false-positive. So an XML **moved out of
`_fixpp_data` to the prefix root** would slip past, as would a payload behind a neutrally-named
symlinked directory. Component tagging (`install(... COMPONENT python)` + a component-only install)
was considered and **rejected**: it is the durable answer, but it obliges every future Python install
rule to carry the tag, and #257 may well move this decision wholesale. Recorded as the known limit of
this instrument.

Registration is `UNIX`-only (CMake does not support `DESTDIR` on Windows) and skipped under `SKBUILD`
(the wheel build has no test tree, and R2-P2-2's question is specifically about the **non-**`SKBUILD`
path `python-wheel-test` cannot see).

### 4.6 Edits 5 + 6 — the two required-check contracts

`tier1-required` (§5) and `ci/test-tier1-python-policy.sh` (§5b). **Both must land in the same commit as the deletion.** Each is independently sufficient to turn a required check permanently red.

### 4.7 New — the disk observation, at **four** points, with the instrument fail-closed

Issue #254's own gate comment specifies four `df` points — after `free-disk-space`, after Configure, after Build, and after Test — plus `du -sh build/linux-clang-ubsan` before/after. v0.1 shipped **one**, placed after Build, which is **not** the job's high-water mark: the consumer install-witness stage-installs and configures+builds a sub-project on all six legs; on `linux-gcc-release` the packaging tier additionally builds sub-projects into `${CMAKE_BINARY_DIR}/_packaging_tests` (`tests/packaging/CMakeLists.txt:10`) and `fixpp-package` stages CPack into `_artifacts`. All of that lands after Build.

**Four points, minimum machinery:**

| # | where | what | shape |
|---|---|---|---|
| 1 | the existing `Free up disk space` step (`:339–348`) | `df` before/after the reclaim | **already there** — no change |
| 2 | tail of the existing **Configure** step | one line: `df -h "$GITHUB_WORKSPACE" \| tail -1` | +1 line, no new step |
| 3 | tail of the existing **Build** step | same one line | +1 line, no new step |
| 4 | **job end**, after `Upload packages` (`:814`), `if: always()` | the operative high-water mark — two steps, below | +2 steps |

**Point 4 is the number NG-1 is actually about.** Points 2 and 3 are trend diagnostics; they are one line each precisely so the recipe is satisfied without four new steps or a new script.

**The job-end pair — measurement asserts, reporting does not:**

```yaml
- name: Measure disk at job end (#254)          # if: always()   — NO continue-on-error
  run: |
    set -euo pipefail
    P="${{ matrix.preset }}"
    if [ ! -d "build/$P" ]; then
      echo "build/$P absent — the job did not reach a configured tree; nothing to measure."; exit 0
    fi
    df -h "$GITHUB_WORKSPACE" > /tmp/disk.txt || { echo "::error::df failed — instrument unavailable."; exit 1; }
    du -sh "build/$P"          >> /tmp/disk.txt || { echo "::error::du on build/$P failed."; exit 1; }
    du -h "build/$P/lib/_fixpp.so" >> /tmp/disk.txt 2>/dev/null || echo "_fixpp.so absent (expected only if the build did not reach it)" >> /tmp/disk.txt

- name: Report disk to the job summary (#254)   # if: always()   — continue-on-error: true
  continue-on-error: true
  run: cat /tmp/disk.txt >> "$GITHUB_STEP_SUMMARY"
```

**Why the split, explicitly.** v0.1 put `continue-on-error: true` on the whole thing and then wrote an acceptance criterion saying *"a run where the step reports NOT MEASURED does not discharge this criterion"* — with nothing that fails. **That criterion was self-waiving**, the exact `feedback_ci_gate_observes_not_asserts_witness_skips_into_green` shape the same section cites. The split makes the **instrument** fail closed (a `df`/`du` that cannot run reds the leg) while leaving the **cosmetic** summary write non-fatal. The absent-tree early-exit is deliberate and is not a hole: if the tree is absent the Build step is already red.

**What is still NOT asserted: the floor.** This is stated plainly rather than argued into comfort:

1. **The failure mode is already self-asserting.** Disk exhaustion does not pass silently — `ld: final link failed: No space left on device` reds the **Build** step. That is how it was found in June. The observation is a *trend instrument*, not the detector.
2. **There is no measured floor to assert against**, and the two numbers that would form a quotient are one CI and one local (§3.2). Any threshold this doc named would be invented, and an invented threshold either never fires (decoration) or fires spuriously. `[const §IX.1]`'s framing applies by analogy: *the assessment is the gate, the number is the target.*
3. The honest disposition: **measure every run at four points, assert that the measurement happened, do not assert a floor, and re-open when the job-end point itself shows free space below ~2× the tree on any leg** (§10 OQ-7).

**The issue's own fallback, recorded here because v0.1 omitted it entirely:** if the ubsan leg's job-end headroom is **not comfortably positive**, #254 is **scoped down to the legs that fit** — the fold lands on the legs with margin and the remainder stays as a job or is deferred. This is R1's fallback and is not a new decision; it is the issue's stated recipe.

### 4.8 The dispositioned census — every live site

v0.1 enumerated **four** comment sites and §9 asserted a **two-file** blast radius. The repository disagrees. The census below is derived mechanically over **two axes** — `python-bindings` (the job name) **and** the L-056-4 sentence (`no Python` / `TEST VEHICLE` / `not a byte of them ships`), because L-056-4's own home file does **not** contain the string `python-bindings` and is invisible to the first axis. Frozen `specs/<feature>/` bundles are excluded by rule.

⚠️ **Every row was dispositioned by reading the line**, not by counting hits: some hits are about the *job* (`python-bindings`, hyphenated) and some about the *path* (`bindings/python`). ⚠️ **Re-derive this table after the last fix commit** — `feedback_a_census_taken_before_the_fix_does_not_cover_what_the_fix_adds`; the R6 edit and the new `ci/derive-python-sanitizer.sh` add sites this table cannot contain.

**A. Load-bearing — must change with the deletion**

| # | site | what it says / what breaks | disposition |
|---|---|---|---|
| 1 | `.github/workflows/tier1.yml:1033–1587` | the job itself | **DELETE** |
| 2 | `tier1.yml:124` | `gate-precheck`'s `PY_RE` comment names `python-bindings` as one of the gated jobs, and the four clang profiles as its build recipe | **REWRITE** — the gated set is now the wheel jobs alone; `.specify/` note that #253 owns the literal (NG-4) |
| 3 | `tier1.yml:334–338` | *"~1748 instrumented executables … the runner's ~14 GB free-disk ceiling"* | **REWRITE** — the **ceiling moved** (145 G / 87 G pre / 114 G post, 2026-08-08); the 14 GB figure was **accurate when written** (run `28184395001` died at edge 1651/1748); correct the count to **330 executables / 3627 ninja edges (1460 compiles + 1460 scans + 330 links)** |
| 4 | `tier1.yml:389`, `:901`, `:2009` | *"pytest … is registered in every preset, **not just the python-bindings build**"* — three copies; after the fold the bindings **are** built in every preset, so the sentence inverts | **REWRITE ×3** |
| 5 | `tier1.yml:1183` | the deleted job's concurrency group | **DELETE** with the job |
| 6 | `tier1.yml:1189–1194` | duplicate of row 3 with `~1987` | **DELETE** with the job |
| 7 | `tier1.yml:1547` | `ccache-hitrate … (python-bindings none)` annotation text | **DELETE** with the job |
| 8 | `tier1.yml:1590`, `:1599–1610` | the wheel-vs-legs note: *"ADDITIVE (CI-8): does NOT touch the python-bindings sanitizer matrix"* + *"the `python-bindings` legs above are a TEST VEHICLE that ships nothing"* + *"`packages-linux-{clang,gcc}-release` … carries no Python"* | **KEEP and RE-POINT** — the test vehicle becomes the six `linux` matrix legs. ⚠️ The *second* half (**no Python in the packages**) stays **TRUE** — that is what §4.5 preserves. This paragraph is the NG-3 record that stops the wheel collapse being re-proposed; it must survive the deletion of the job it points at |
| 9 | `tier1.yml:1899` | the pin's step name — *"Regression pin — tier1.yml python-bindings policy (#251)"* | **REWRITE** with the pin (§5b) |
| 10 | `tier1.yml:2092` | `tier1-required`'s `needs:` list | **REWRITE** — 8 names → 7 (§5) |
| 11 | `tier1.yml:2145` | `tier1-required`'s comment enumerating `python-bindings` as an independently gating job | **REWRITE** — §5.1's site list is **six**, not five; this is the sixth |
| 12 | `tier1.yml:2162–2167` | latency note citing 113.1 / 134.8 for `python-bindings (asan)` | **DELETE** with the assertions (§5) |
| 13 | `tier1.yml:2169`, `:2172`, `:2175`, `:2184` | the `pb=` capture, the diagnostic `echo`, and both `::error::` branches | **DELETE** (§5.1) |
| 14 | `ci/test-tier1-python-policy.sh` (7 sites: `:3`, `:9`, `:23`, `:77`, `:171`, `:172`, `:251`) | the whole pin — header, extraction, three assertions, `EXPECTED_NEEDS` | **REWRITE** (§5b) |
| 15 | `ci/test-ccache-scripts.sh:3–9` | *"#248 is specifically about extracting TIER 1's in-workflow ccache probes (the ~170 lines of `run:` script around `python-bindings`) … those `run:` blocks are UNCHANGED by this PR"* — this is **#248's scope definition living in a script header**, and #254 deletes those blocks | **REWRITE** — a live operational instruction, not a historical note |
| 16 | `.github/workflows/cache-cleanup.yml:25` | *"tier1's third site, python-bindings `none`, is `save: false` and never writes"* — this sentence **is** the rationale for the whole state-based reclaim design; after #254 there is no third site | **REWRITE** — live operational rationale |
| 17 | `bindings/python/pyproject.toml:11–14` | L-056-4 verbatim, second axis | **RE-POINT** the vehicle noun; the no-Python half stays true |
| 18 | `bindings/python/cibw-before-all.sh:23–26` | L-056-4 verbatim, second axis | **RE-POINT** likewise |
| 19 | `spec/behaviors-and-limitations.md:1649` (**L-056-4 itself**) | *"Those bindings are a **test vehicle only — not a byte of them ships**; likewise the `packages-linux-{clang,gcc}-release` artifacts are the **C++** consumer deliverable and contain no Python."* | **RE-POINT** — the vehicle is now the six matrix legs; add a `#254` note that the property is **preserved by `FIXPP_INSTALL_PYTHON=OFF`**, i.e. it stayed true by construction rather than by accident |

| 20 | `bindings/python/tests/conftest.py:6` | describes the **current CI vehicle** for these tests (the `python-bindings` matrix) | **RE-POINT** — the vehicle is the six `linux` legs. Promoted out of table B by **Appendix D P3(3)** |
| 21 | `bindings/python/tests/test_gil_release_canary.py:16` | same, **and** already stale on its own terms — it names the matrix as none/asan/tsan; the ubsan lane landed at #159 | **RE-POINT** — two fixes in one line; label them separately in the diff so the pre-existing staleness is not read as introduced by #254 |

⚠️ **Rows 20/21 are NOT safe to land before the deletion.** Together with rows 15 (`ci/test-ccache-scripts.sh:3–9`) and 16 (`cache-cleanup.yml:25`), every one of these four re-points asserts a **post-deletion** state — *"there is no third ccache site"*, *"the vehicle is the six matrix legs"*. Landing them while the `python-bindings` job still exists makes the repo assert something **false**, which is strictly worse than stale (`feedback_stale_anchor_repoint_to_a_plausible_twin_is_worse_than_stale`). **They ship in the same commit as the deletion, not before it.**

⚠️ **Rows 8 / 17 / 18 / 19 are one invariant in four files** — the `feedback_subset_check_cannot_see_symmetric_omission` shape. v0.1 re-pointed **one** of the four. Fixing the copy you happen to be editing would leave three copies asserting the opposite.

**B. Descriptive — disposition in one line each; do not rewrite**

| site | disposition |
|---|---|
| `bindings/python/tests/conftest.py:6` | ⚠️ **SUPERSEDED by Appendix D P3(3) → RE-POINT** (moved to table A as row 20). Round 2 was right: this does not merely *incidentally* name the matrix, it describes the **current CI vehicle**, and after the fold that description is false |
| `bindings/python/tests/test_gil_release_canary.py:16` | ⚠️ **SUPERSEDED by Appendix D P3(3) → RE-POINT** (table A row 21). It is *also* already stale on its own terms — it says the matrix is none/asan/tsan and the ubsan lane landed at #159 — so the re-point fixes two things; say which is which in the diff so the pre-existing staleness is not read as introduced here |
| `CMakeLists.txt:303` | **LEAVE** — an MSVC `/bigobj` comment mentioning the matrix as context |
| `spec/coverage-index.md:656,658` · `spec/feature-catalogue.md:274–275` · `spec/behaviors-and-limitations-closed.md:138–142` · `spec/behaviors-and-limitations.md:1851` | **LEAVE — historical.** These record what the PY-001/002/003 features delivered *at the time*, keyed to closed L-rows. Rewriting them would falsify a record |
| `CLAUDE.md:10,12` · `CLAUDE-history.md:40` | **LEAVE — historical.** Merged-PR narratives for #251/#247/#159 |
| `specs/<frozen>/**` | **LEAVE — frozen bundles**, out of scope by rule |

**C. Added by this change (not in the census above — the reason it must be re-derived)**

`ci/derive-python-sanitizer.sh` (new, §4.3), `bindings/python/CMakeLists.txt` (the option + the `message(STATUS)`, §4.5), and the new pin assertions/mutants in `ci/test-tier1-python-policy.sh` (§5b).

---

## 5. The `tier1-required` contract

`tier1-required` (`tier1.yml:2088–2205`) is the required check. It lists `python-bindings` in `needs:` (`:2091–2092`) **and** asserts on `needs.python-bindings.result` in both branches of the `python_touched` split (`:2173–2191`).

### 5.1 Before / after truth table

`E` = the empty string a context property of a non-existent job evaluates to.

| event / state | job | **before** (asserted) | **after** (asserted) | **after, if the assertion is left behind** |
|---|---|---|---|---|
| `release` | — | early exit at `:2115`, gate n/a | unchanged | unchanged (early exit precedes everything) |
| any non-release | `gate-precheck`, `linux`, `coverage`, `check-layers`, `ci-script-pins` | `== success` | `== success` (**unchanged**) | unchanged |
| `python_touched=true` | `python-bindings` | `== success` | **assertion deleted** | `E != success` ⇒ `::error::` **RED** |
| `python_touched=true` | `python-wheel-build` | `== success` | `== success` (unchanged) | unchanged |
| `python_touched=false` | `python-bindings` | `== skipped` (**exactly**) | **assertion deleted** | `E != skipped` ⇒ `::error::` **RED** |
| `python_touched=false` | `python-wheel-build` | `== skipped` (exactly) | unchanged | unchanged |
| any | `python-wheel-test` | `success` iff `python-wheel-build == success`, else exactly `skipped` | unchanged | unchanged |
| — | `needs:` set (sorted) | `check-layers, ci-script-pins, coverage, gate-precheck, linux, python-bindings, python-wheel-build, python-wheel-test` (**8**) | same minus `python-bindings` (**7**) | — |

**The six sites that move as one unit.** "Assertion deleted" above is **six** edits inside `tier1-required` (v0.1 said five and missed the comment at `:2145`), and a partial application is worse than none: the `needs:` entry (`:2092`), the `pb='${{ needs.python-bindings.result }}'` capture (`:2169`), the diagnostic `echo` that prints `python-bindings=$pb` (`:2172`), the two branch conjuncts (`:2174–2177` and `:2183–2186`), and the explanatory comment enumerating `python-bindings` as an independently gating job (`:2145`). Drop the conjuncts but keep the capture and the echo, and the log names a job that no longer exists; drop the capture but keep the echo, and the diagnostic prints an empty interpolation. The adjacent latency comment (`:2162–2167`) goes with them (§4.8 row 12).

⚠️ **Correction to the brief.** The brief (and the plan record) state that leaving the assertion behind reds *"the `python_touched=false` branch"*. It is **both branches**: `pb=E` fails `!= "success"` when `python_touched=true` **and** `!= "skipped"` when false. There is no run shape that survives it — the required check is red on **every** non-release run, PR and push alike. The conclusion the brief draws (the two must move together) is right; the blast radius is larger than stated.

### 5.2 What still guards the python signal

Once `needs.python-bindings.result` no longer exists, the guard is **stronger, not weaker**:

- The python build and pytest now ride **inside `linux`**, which `tier1-required` asserts `== success` **unconditionally** for every non-release event (`:2129–2134`).
- `linux` has **no `python_touched` gate** — only `proceed` (`:289`). So the bindings' signal moves from *conditionally required* (required only when the diff touched a python-relevant path) to *unconditionally required*.
- Concretely: the old contract accepted `python-bindings == skipped` as green on a C++-only PR. The new one has no such branch — the bindings are built and pytest is run on that PR too, and a failure reds `linux`, which is asserted. **The `python_touched=false → exactly-skipped` acceptance path disappears because there is nothing left that can legitimately skip.** This is what the §3.3 net-negative runner-minutes on non-`PY_RE` PRs buys.
- `python_touched` itself stays alive and asserted — the `python-wheel-{build,test}` branches still read it (`:2173`, `:2182`, `:2192–2202`). It simply governs the wheel jobs alone, which is #253's requested split arrived at by deletion (NG-4).

---

## 5b. The `ci/test-tier1-python-policy.sh` contract — the **sixth** mandatory edit

**This is not in the brief's four-edit list, and a PR that ships the four edits alone is red before it is reviewed.**

`ci/test-tier1-python-policy.sh` (456 lines, landed by PR #251) parses `tier1.yml` with PyYAML and pins the python policy. It is invoked by the **`ci-script-pins`** job (`tier1.yml:1899–1902`), which is **ungated** — it has no `if:` at all (`:1873–1876`) — and whose result `tier1-required` asserts `== success` (`:2126`, `:2129`). So this red fires on the #254 PR itself, on every event, **before** and independently of the §5 trap.

### 5b.1 Every place the deletion breaks it

| # | site | what breaks |
|---|---|---|
| 1 | `extract_json`, `:77` — `pb_job = jobs["python-bindings"]` | **KeyError** ⇒ python exits non-zero ⇒ `run_full_pin` dies under `set -euo pipefail` on the **real** workflow, at `:269`, before any mutant runs |
| 2 | `assert_tier1_required_needs`, `:249–256` | `EXPECTED_NEEDS` is the exact 8-name sorted set **including `python-bindings`**. Must be re-based to the 7-name set |
| 3 | `assert_matrix_policy`, `:111–146` | Reads `python-bindings`' `strategy.matrix.include`; `EXPECTED_MATRIX` names all four sanitizer→profile pairs |
| 4 | `assert_step_parameterisation`, `:164–195` | Requires the python job's `Restore Conan cache from GHCR` and `Conan install` steps to exist with exact normalised run text (`:171–172`) |
| 5 | mutants **A** (`:287`), **C1** (`:344`), **C2** (`:370`), **D** (`:399`), **E** (`:430`) | Each does a literal text replacement inside the python-bindings job under `assert t.count(old) == 1` ⇒ **AssertionError** once the text is gone. The `cmp`-based no-op guards then fire too |
| 6 | mutant **B** (`:317`) | Targets `PY_RE` in `gate-precheck` — **survives untouched** (NG-4) |

### 5b.2 Decision — **re-point the pin onto the extracted script and its call site**

v0.1 framed this as a two-way choice between **(a) narrow** and **(b) re-point by teaching the PyYAML pin to parse the inline `case`**, and chose (b). **v0.2 chooses neither as stated**, because §4.3.1 moved the derivation out of YAML. The decision is now:

> **Do all of (a)'s re-basing work, and re-point the deleted half onto `ci/derive-python-sanitizer.sh` — driving the real script, plus a single-line call-site assertion in `tier1.yml`.**

Why this supersedes v0.1's (b):

- **It removes the contradiction v0.1 carried.** §6.2 claimed #254 *shrinks* #248 while §5b.2 chose the option that *deepens* it. Under this decision #254 **delivers a down payment on #248** and §6.2 becomes true.
- **It is strictly cheaper than (b)** — v0.1's own cost note called (b) *"the largest single piece of work in #254 — larger than the workflow diff"*. Extracting the script and testing it directly is smaller *and* stronger.
- **It keeps PyYAML doing what it is good at**: structure and **single-line literals** (which is exactly why assertion 5's behavioural `PY_RE` extraction works today). It never asks PyYAML to parse shell.
- **It does not delete the re-basing work.** Assertions 1/2/3/4, `EXPECTED_NEEDS`, and the mutant re-bases are all still required; only the *re-point target* changed.

**Required content:**

1. **Re-based `EXPECTED_NEEDS`** — `check-layers,ci-script-pins,coverage,gate-precheck,linux,python-wheel-build,python-wheel-test` (7 names).
2. **Delete assertions/mutants keyed on the vanished job** — 1, 3, 4 and mutants A/C1/C2/D/E. **Keep** the `PY_RE` case table and mutant **B** untouched (NG-4); they move with **#253**.
3. **Drive the real script** — `ci/derive-python-sanitizer.sh` invoked over **exactly the six presets in `tier1.yml`'s `linux` `strategy.matrix.preset`** (read from the YAML, so the census is *exact-set*, not subset — `feedback_completeness_gate_exact_set_not_subset`, the lesson `EXPECTED_MATRIX` encodes today), asserting the §4.3.2 values, **plus** one unknown preset asserting **exit 1**.
4. **Assert the call site.** ⚠️ *A tested script the workflow no longer invokes is the dead-call-site shape.* Two assertions, not one: (i) the `linux` job has a step whose `run:` invokes `ci/derive-python-sanitizer.sh` with `matrix.preset`, and (ii) the Configure step consumes `steps.pysan.outputs.sanitizer` — the same "dead interpolation" trap assertion 4 was written for (`ci/test-tier1-python-policy.sh:23–33`). Both are single-line literal checks over the YAML.
5. **`ubsan → ubsan_standalone`** asserted explicitly. It is the one non-identity entry in the table and therefore the one a future edit "normalises" away.
6. **Assert the `FIXPP_INSTALL_PYTHON=OFF` flag is on the Configure line** of the `linux` job — a single-line literal check. Without it the §4.5.3 step could be deleted and the pin would not notice.
7. **Mutants, per `feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree`** — an assertion never shown to fail is not evidence. At minimum:
   - **M1**: `linux-clang-tsan → none` **in the script** ⇒ must fail the value check with a message naming the preset. (Heir of mutant A.)
   - **M2**: delete `linux-gcc-release` from the script's `case` ⇒ must fail the **exhaustiveness** check, not merely the value check. (Heir of mutant D.)
   - **M3**: `ubsan_standalone → ubsan` ⇒ must fail.
   - **M4**: drop one entry from `tier1-required`'s `needs:` ⇒ must fail the needs census. ⚠️ **This mutant does not exist today** — assertion 6 has never been proven RED. Re-basing it without adding a witness carries a never-tested assertion forward under a new number.
   - **M5**: remove `-DFIXPP_INSTALL_PYTHON=OFF` from the Configure line ⇒ must fail (item 6).
   - **M6**: rename the derive step so nothing invokes the script ⇒ must fail the call-site assertion (item 4).
   - Each mutant keeps the existing `cmp -s`-based **no-op guard** (`:297–299`) so a replacement that fails to apply cannot read as a pass, and each keeps its `grep`-the-reason check so a mutant cannot pass the pin for the wrong reason.
8. **The pin asserts its own declared-vs-run mutant count** and exits non-zero on a mismatch. ⚠️ This is not decoration: **the r3 lesson of PR #251's own review loop was a summary that counted five mutants where six ran**, caught by orchestrator verification *after* the fixer reported done. v0.1's AC-8 proposed a **human eyeball** as the remedy for that exact failure. The count check is machine, or it is not a check.
9. The pin's header comment (`:1–50`) is rewritten to describe what it now pins. A pin whose self-description is stale is the r3 failure mode of PR #251's own review loop.

---

## 6. What is deleted, and what that supersedes

### 6.1 Deleted

The whole `python-bindings` job, `tier1.yml:1033–1587` (~555 lines).

**Duplicated infrastructure** (present verbatim on the matrix legs): `checkout`, `Free up disk space`, the Clang-22 install block, `actions/setup-python`, `Install Conan + pytest via pipx`, `Initialize Conan default profile`, `Teach Conan about Clang 22`, `Set up oras`, `Restore Conan cache from GHCR`, `Conan install`, `Configure with Python bindings`, `Build`, plus the job-level `env`, `strategy.matrix.include` and `concurrency` blocks.

⚠️ **Three steps are NOT duplicates — they are deliberate observability deletions**, and v0.1 mislabelled them:

| step | site | signal lost |
|---|---|---|
| `Conan restore disposition (observation, not a gate)` | `:1280–1282` | the Job-Summary disposition line **for the four deleted legs only** |
| `ccache restore check (did the matrix debug leg's cache land?)` | `:1340–1375` | the cross-job restore probe |
| `ccache stats (none leg — restore-only)` | `:1430–1549` | the `none` leg's hit-rate annotation |

**The loss is nil.** All three described *the deleted legs*. The surviving matrix legs never had the disposition line (their `restore-conan-cache.sh` emits its own annotations per #222/#245), and they own their ccache key with a real save path. The signals disappear with the legs they described — that is a correct outcome, not a regression, but it is a **deletion of observability**, not a de-duplication, and the PR body should say so.

**Including all of #244 part 1's python-side machinery**: the restore-only `ccache-action` (`:1303–1309`), the restore-check probe, and the stats/liveness step.

One consequence worth a line in the PR body: this retires **the repo's only cross-job ccache key share** (`:1303–1309`; its own note at `:1332–1334` calls it *"the first observation in this repo of ccache-action sharing a key ACROSS jobs"*). After #254 every ccache call site owns its own key again — a simplification of the cache model, not merely a deletion.

### 6.2 What it supersedes

| issue | disposition | why |
|---|---|---|
| **#244 part 2** | **Superseded outright. Do not start.** | Part 2 would *cache* the ~308 min the three sanitizer legs spend rebuilding. #254 **deletes those job-minutes outright, plus the four runners**. Part 2 is now work that would be deleted. The disk gate the sequencing decision was conditioned on came back clear (§3.2, and §4.7 measures it every run). |
| **#244 part 1** | **Merged and delivered; its *mechanism* is absorbed.** | Part 1's measured result stands (7.6 min vs ~90 on the `none` leg) — not retracted. But the restore-only ccache, the probe and the stats step existed to make a duplicate build cheap; the duplicate build is gone. **#244 ends up fully absorbed.** |
| **#248** | **Shrinks — and #254 pays a down payment on it.** | ⚠️ **Corrected in v0.2.** v0.1 claimed "shrinks" while choosing an option that would have *deepened* it (§5b.2). Under the v0.2 decision: (i) the cross-job ccache probe steps #248 was opened to extract **stop existing**, and (ii) the preset→sanitizer derivation is **extracted into `ci/derive-python-sanitizer.sh` and tested as itself** — the first application of #248's own thesis to a tier-1 `run:` block. `ci/test-ccache-scripts.sh:3–9` must be rewritten accordingly (§4.8 row 15). |
| **#253** | **Shrinks, by deletion.** | `python_touched` collapses to the wheel jobs alone — the split #253 asks for. The `PY_RE` literal itself is **not** narrowed here (NG-4); #253 still owns that plus the pin's case table. |
| **#252** | **Untouched.** | The C-language instrumentation gap in `conan/profiles/` is pre-existing and equal on the matrix legs; merging neither fixes nor worsens it. Worth one sentence in the PR body so it is not read as fixed. |
| **#255** | **Boundary sharpened, not moved.** | #255 owns *deliberate* package-level Python shipping. §4.5 does not pre-empt it — it holds the current (no-Python) contract in place until #255 decides otherwise, and does so with a boolean #255 can flip. |

---

## 7. Risk register

Weight order. Each risk carries its **measured disposition** — or, where it has none, says so plainly rather than reasoning its way to comfort.

| # | risk | disposition | residual |
|---|---|---|---|
| **R6** | ⚠️ **`install()` rules reach the packaging and consumer tiers**, adding a Python payload to `packages-linux-{clang,gcc}-release` and to the all-six-legs consumer stage-install — falsifying **L-056-4** and violating the user's standing #254 decision. **Promoted to top weight in v0.2.** | **DESIGNED OUT, not left open.** §4.5: `FIXPP_INSTALL_PYTHON` defaults ON (056 LAY-1 preserved), set **OFF** on the six legs; the resolved branch is **reported** at Configure (`message(STATUS)`); an **asserting step** (§4.5.3) reds the leg if any install directive survives, with the instrument proven non-zero (**4**) on the unfixed tree; the ON-side is witnessed by `python-wheel-test`, which fires on this PR (§4.5.4). ⚠️ The existing packaging/consumer witnesses are **structurally blind** to added root-level files — a 9/9 green there is **not** evidence and must not be cited as such. | Low. Three residuals, all named: (i) which `Python3_SITEARCH` branch fires on CI is **unmeasured** — reported, not assumed; (ii) the OFF assertion is a *generated-file* check, so a future CMake refactor that emits install rules elsewhere would evade it; (iii) `python-wheel-test` is `proceed`-gated, so ON-side signal arrives only after the gate labels. |
| **R1** | **Disk.** The bindings land on the leg that already died of ENOSPC. | **Measured, and now instrumented at four points.** 114 G free post-reclaim (CI, pre-build) vs a 32.96 GiB tree (local); bindings add ~126 MiB, `_fixpp.so` 120.83 MiB of it (96 %); ctest's `Testing/` output is 20 KB. ⚠️ **No "×N margin" headline** — the two terms are CI and local respectively (§3.2). **NG-1 keeps the reclaim step** — the margin *is* the reclaim. | Low. The **job-end** measurement (§4.7 point 4) is the operative number and it fails closed if the instrument cannot run. Self-asserting if it ever bites (Build reds). ⚠️ **Fallback, from the issue itself:** if the ubsan leg's job-end headroom is not comfortably positive, **#254 is scoped down to the legs that fit.** |
| **R2** | **Signal granularity.** A python flake and a C++ regression become the same red X on the same job. | **Partially mitigated.** Two separately-named steps + `continue-on-error` off (§4.4) ⇒ the failing step is identifiable from the checks UI without reading the log. | ⚠️ **Real and not fully mitigated.** Named steps buy *attributability*, not *blast radius*: a pytest flake now reds a required C++ leg on a PR that touched no Python, and `fail-fast: false` means it reds only that leg but reds it fully. This is the price of the merge, accepted knowingly. |
| **R3** | **Path gating.** Merged, `python_touched` no longer skips the bindings work on PRs. | **Quantified, and the cost is real.** On `push:main` it costs nothing — but ⚠️ **that is a tautology, not a measurement**: `tier1.yml:164–172` sets `PYTHON_TOUCHED=true` unconditionally for every non-PR event (E-19, retagged **I**). The decision-relevant rate is the **PR-side** one: **11 of the last 30 merged PRs match `PY_RE` (37 %)** (E-30). On the other **63 %** the merged legs pay `+(swig+python3-dev apt) + 5 edges + pytest wall + 1 ccache miss`, × 6, against a deleted job that cost **0**. PR-side pytest wall: **11.61 s** (ubsan) / **5.51 s** (gcc-release) / **5.31 s** (clang-release) — ⚠️ **three of six configurations only**, no debug, asan or **tsan** number, and tsan's subprocess watchdog/canary tests are exactly where extrapolating is unsafe. | Low in absolute minutes, **net negative on 63 % of PRs**, and it buys the §5.2 strengthening (the bindings become unconditionally required). Stated as a trade, not a saving. |
| **R4** | **Timeouts.** Matrix legs sit at 240, python at 180. | **Settled.** Merged ubsan ≈ **40 min** (*derived*: 39.9 measured + ~12 s pytest) against `timeout-minutes: 240`. 6× headroom. | Nil. No timeout change proposed. |
| **R5** | **Toolchain setup on legs that do not need it.** | **Nearly nil, and smaller than the issue assumed.** The matrix job **already** runs `actions/setup-python@v6` and `pip install pytest pyyaml` on every leg (`:379–392`) — pytest is required by the `decimal_compare_oracle` CTest entry. New: `swig` + `python3-dev` on an existing apt line (§4.2). | Seconds per leg. |
| **R7** | **The regression pin.** | **Certain, fully characterised** — §5b. Six distinct break sites; `ci-script-pins` is ungated and asserted, so it reds the #254 PR itself. | Nil once §5b lands in the same commit; §8 PG-6 asserts it. |
| **R8** | **`-Werror` on the SWIG wrapper**, newly compiled on `linux-clang-release` (which inherits `FIXPP_WERROR=ON` from `_base`, `CMakePresets.json:14`; `linux-gcc-release`/`linux-gcc-debug` set it OFF). | **Nil, by inspection, re-verified.** `fixpp_maybe_werror` (`cmake/Helpers.cmake:31–39`) is applied **per target**, and `bindings/python/CMakeLists.txt` never calls it — `fixpp_py` gets no `-Werror` under any preset. Verified by grep over that file: zero occurrences. | Nil. Recorded so a reviewer does not have to re-derive it. |
| **R9** | **A silently mis-derived sanitizer identity** — the new failure mode this change introduces. | **Designed out, fail-closed** (§4.3): unknown preset is fatal; `san_opts` shares the discriminant; no substring sniffing; the derivation lives in a script driven directly by the pin over the exact six presets plus an unknown one, with M1–M3 proven RED (§5b.2). | Low, and instrumented rather than argued. |
| **R10** | **The Release legs' bindings are new code paths in CI.** `-O2`/`NDEBUG`/gcc-13 have never seen the SWIG wrapper, and `bindings/python/CMakeLists.txt:139` adds `-static-libstdc++ -static-libgcc` on exactly the non-sanitized legs. | **Proven locally, not yet in CI.** `linux-gcc-release` **82 passed** 5.51 s (module 20 MiB); `linux-clang-release` **82 passed** 5.31 s (module 14 MiB); local gcc **13.3.0** = CI's gcc-13, local clang 22.1.2 vs CI 22.1.8. | Low. First CI run is the confirmation (PG-2). |
| **R11** | **Job-level `env:` promotion during implementation.** If `san_opts` is lifted to the job's `env:` block instead of staying per-invocation, it silently changes the **C++ ctest** run in the same job. | **Named, not measured — a implementation-time hazard.** Today's scoping is per-invocation (`:1580–1587`) and §4.4 requires it to stay that way. | Low, but it is the kind of change a reviewer would wave through; it is written down so they do not. |

**Checked and clear — dispositioned, so round 2 does not re-litigate them:**

| topic | disposition |
|---|---|
| Sanitizer env leakage between ctest and pytest | **None.** Options are scoped per-invocation; `PYTHONPATH` is on the step, not the job. Preserved by §4.4; guarded by R11 |
| Concurrency-group collisions | **None.** The matrix keys on `tier1-<pr\|ref>-${{ matrix.preset }}` (`:290–292`); the deleted job keyed on `…-python-bindings-<san>` (`:1183`). Deletion removes a group |
| Cache-key collisions | **None.** The deleted site was **restore-only** on the matrix debug leg's key (`:1303–1309`, `CCACHE_DIR=/tmp/fixpp-ccache-linux-clang-debug`). Deleting it removes a **reader**, never a writer |
| `BUILD_SHARED_LIBS` | `linux-clang-{asan,ubsan,tsan}` carry it ON; `linux-clang-{debug,release}` and `linux-gcc-release` do not. The python legs' `-DBUILD_SHARED_LIBS=ON` was redundant on three legs and never applied to the other three (E-22) |
| New ctest registrations | **None.** `bindings/python/CMakeLists.txt` contains no `add_test`, so `FIXPP_BUILD_PYTHON=ON` cannot perturb the `expected=1` consumer or `expected=8` packaging count gates |
| `linux-clang-coverage`, tier 2, tier 3 | **Untouched.** `coverage` is a separate job with its own preset; `cmake/Codegen.cmake:201` already passes `-DFIXPP_BUILD_PYTHON=OFF` to the bootstrap codegen build |
| Required status-check contexts | **Audited (E-28).** No `python-bindings (*)` context is required — deleting the job strands nothing. ⚠️ Mutable external state; **re-verify at merge** (PG-7) |

---

## 8. Acceptance criteria

⚠️ **v0.1's acceptance layer specified numbers for a human to read, not steps that exit non-zero.** Every criterion below either **names a command that exits non-zero and says who runs it**, or is explicitly relabelled a **close-out observation**. The two are separated because v0.1 mixed them — AC-6 required a `push:main` run that only exists after merge, and AC-4's own instrument was `continue-on-error: true`.

### 8.1 Pre-merge gates — each exits non-zero

| # | criterion | the instrument that fails |
|---|---|---|
| **PG-1** | **No `python-bindings` job, and all six legs run pytest under their own preset.** | ① `ci/test-tier1-python-policy.sh` — `EXPECTED_NEEDS` is the 7-name **exact set** and the six-preset script census is exact-set; a leftover job or a missing leg exits non-zero in `ci-script-pins`. ② The negative grep (`grep -c '^  python-bindings:' .github/workflows/tier1.yml` = 0) is admissible **only** because it is first shown to return **1** on the pre-change tree; a `0` also passes if the job were merely *renamed*, which is why ① carries the weight. |
| **PG-2** | **Every `linux-*` leg — including both Release legs — runs pytest with a non-vacuous test count.** | The pytest step itself, on all six legs, with `continue-on-error` off. ⚠️ **RESTATED at Gate B round 1 (N1 / Appendix D P3(2)) — the previous rule was wrong on BOTH counts.** (a) *"an empty pytest selection exits 0"* is **false**: pytest exits **5** on an empty collection, so this is not `ctest -L`'s vacuous-success trap and no count assertion is needed to close it. (b) *"assert `N passed` with N ≥ collected"* **fails on any legitimate skip or xfail**, turning a normal `@pytest.mark.skipif` into a red CI criterion. **The rule is therefore: trust pytest's own exit status** — it already covers empty collection (5) and failures (1) — and, if a vacuity pin is still wanted, assert `pytest --collect-only -q` reports **> 0**. No hardcoded `82`: the repo adds pytests regularly (053 → 054 → 055 → 056). |
| **PG-3** | **The C++ package/install content is unchanged: zero Python install directives on the matrix legs.** | ① **In CI, every leg:** the §4.5.3 step — `grep -c '^[[:space:]]*file(INSTALL' build/<preset>/bindings/python/cmake_install.cmake` must be **0**, instrument proven **4** on the unfixed tree (E-32); the step also fails if the generated file is absent. ② **Locally, once, recorded in the PR body:** configure `linux-gcc-release` with and without `-DFIXPP_BUILD_PYTHON=ON`, `cmake --install --prefix /tmp/p{on,off}`, `diff <(find /tmp/pon -type f\|sort) <(find /tmp/poff -type f\|sort)` — this names the *content* of the delta on the `"."` branch. ⚠️ ② is **not** the gate: on the absolute-SITEARCH branch a prefix diff is structurally blind (§4.5.2). ③ `ctest --preset linux-gcc-release -L packaging` (8) and `-L consumer` (1) green — ⚠️ **necessary, not sufficient**; both witnesses are blind to added root-level files, so a green here is **not** evidence for PG-3 and must not be quoted as one. |
| **PG-4** | **The install branch is named, not assumed.** | The `message(STATUS)` from §4.5.2 appears in every leg's Configure log with a concrete `Python3_SITEARCH` and `FIXPP_PY_INSTALL_DIR` value. This is a **reporting** requirement whose *failure to appear* is caught by PG-3 ①'s absent-file check. Its value is recorded in the PR body; it does not gate on which branch fired, because PG-3 makes the branch irrelevant on the matrix legs. |
| **PG-5** | **Free disk is measured at four points, and an unmeasurable instrument reds the leg.** | The §4.7 job-end **measurement** step — `set -euo pipefail`, no `continue-on-error`, `df`/`du` failure ⇒ `exit 1`. ⚠️ **What this asserts is that the measurement happened, not that the margin is adequate** — no floor is invented (§4.7, `[const §IX.1]` by analogy). The three earlier `df` points are one-line diagnostics inside existing steps. |
| **PG-6** | **The regression pin passes and is still able to fail.** | `bash ci/test-tier1-python-policy.sh` exits 0 locally and in `ci-script-pins`, printing `RED (expected):` for **every** mutant including M1–M6 — **and the script asserts its own declared-vs-run mutant count and exits non-zero on a mismatch** (§5b.2 item 8). v0.1 proposed a human reading the output; PR #251's r3 failure was precisely a count that a human read as fine. |
| **PG-7** | **No required status-check context is stranded.** | `gh api repos/CatalinSerafimescu/fixpp/branches/main/protection -q '.required_status_checks.contexts[]'` and `gh api …/rulesets`, **re-run within the merge window**, must contain no `python-bindings (*)` context. ⚠️ E-28 is the **only** evidence row measuring mutable state **outside the repository**; every other row is re-derivable from tracked files. It does not carry forward from a prior reading. |
| **PG-8** | **`python-wheel-build` / `python-wheel-test` untouched, and both fire on this PR.** | `git diff` touches no line in `tier1.yml:1611–1872`. `python_touched=true` on this PR (`PY_RE` matches `tier1.yml` and now `bindings/python/`), so `python-wheel-test`'s 3.10–3.13 install+import runs — the **ON-side witness for §4.5** (§4.5.4). It is `proceed`-gated, so it appears only after the gate labels land. |
| **PG-9** | **Local pre-PR build gate satisfied.** | `[const §XVII.7]` — ⚠️ **now literally triggered.** v0.1 argued the clause applied because the change touched "`bindings/python/`'s *build wiring*", which is an interpretation, not the clause; the clause is path-based (*"if the change touches `bindings/python/`"*, `.specify/constitution.md:354`). **The §4.5 edit puts `bindings/python/CMakeLists.txt` in the diff**, so the condition is met on its own terms. PR body carries `local build: green on linux-clang-debug @ <sha>` **and** a `pytest bindings/python/tests/` result. |

### 8.2 Truth-table coverage — what one PR can and cannot observe

⚠️ **v0.1's AC-7 claimed four "observed green" shapes that a single PR cannot produce.** The honest split:

| shape | how it is covered | why |
|---|---|---|
| `pull_request`, `python_touched=true` | **Observed on this PR.** | `PY_RE` matches `tier1.yml` and `bindings/python/` — this PR *is* that shape |
| `pull_request`, `python_touched=false` | **NOT observable on this PR.** Covered by **the pin** (§5b), which drives `decide_run` behaviourally over the `PY_RE` case table with synthetic inputs, plus the §5.1 truth table by inspection | Any PR that edits `tier1.yml` matches `PY_RE`; the shape is unreachable from #254 by construction |
| `push:main` | **Close-out (CO-2).** | The event exists only after merge |
| `release` | **By inspection**, unchanged: the `:2115` early exit precedes every python assertion | No release is being cut for this PR |

**The single mechanism produces both consequences** — `PY_RE` matching `tier1.yml` is *why* the ON-side wheel witness is free (§4.5.4) and *why* the `python_touched=false` branch cannot be exercised here. Stated once so neither reads as an oversight.

### 8.3 Close-out observations — post-merge, not merge gates

| # | observation | how |
|---|---|---|
| **CO-1** | **Before/after tier-1 runner-minutes and wall-clock, with the arithmetic, per event class.** | `gh run view <id> --json jobs` on a **warm post-#251 `push:main`** run, before and after. Before-figures from run `31273945846` are **stale for this purpose** (they predate #251) and must be re-taken (§10 OQ-2). The claim to test is **§3.3's per-class table**, not "−316". **Tolerance:** wall-clock within **±10 %** of 135.2 min counts as "≈ unchanged" (NG-5); a wall-clock *improvement* outside that band is a finding to explain, not a bonus. |
| **CO-2** | **`tier1-required` green on `push:main`.** | The first post-merge run. Together with PG-1 and §8.2 this closes the truth table. |
| **CO-3** | **Per-leg pytest wall on all six legs**, replacing the three-of-six sample (E-18). | Read from the first merged run's step timings — closes R3's extrapolation gap, in particular **tsan**, whose subprocess watchdog tests are where extrapolation is least safe. |
| **CO-4** | **Job-end free disk on all six legs**, from the §4.7 step. | Establishes the first real post-build margin and the trigger threshold in OQ-7. |

---

## 9. Rollback and blast radius

**Rollback is semantic, not byte-for-byte.** ⚠️ v0.1 said a revert restores the `python-bindings` job "byte-for-byte"; that is false once the comment repairs, the L-056-4 re-points and the §4.5 CMake control land. A `git revert` of the single commit restores the old job **and** removes the install control, the derive script and the comment repairs together — which is the correct all-or-nothing coupling, but it is a *semantic* restoration of the prior contract, not a byte-identical one. In particular, reverting re-exposes nothing dangerous: with the job restored, `FIXPP_BUILD_PYTHON` is again OFF on the matrix legs, so the install rules go back to being unreachable from any packaging path.

**Files this change touches** — ⚠️ v0.1's "confined to two files; touches no C++, no CMake, no Conan profile" was **false on three independent counts** (the two live operational files, the four-file L-056-4 invariant, and `bindings/python/CMakeLists.txt`):

| file | why |
|---|---|
| `.github/workflows/tier1.yml` | the deletion, the six edits, the census rows in §4.8 A |
| `ci/derive-python-sanitizer.sh` | **new** (§4.3) |
| `bindings/python/CMakeLists.txt` | **the install control + the branch report** (§4.5) |
| `ci/test-tier1-python-policy.sh` | the pin (§5b) |
| `ci/test-ccache-scripts.sh` | #248's scope definition in its header (§4.8 row 15) |
| `.github/workflows/cache-cleanup.yml` | the state-based-reclaim rationale (§4.8 row 16) |
| `bindings/python/pyproject.toml`, `bindings/python/cibw-before-all.sh`, `spec/behaviors-and-limitations.md` | the L-056-4 invariant, three of its four copies (§4.8 rows 17–19) |
| `.specify/ci254-python-fold.md` | this doc |

**Blast radius, in order** — ⚠️ **reordered in v0.2.** v0.1 put the install path 4th; the unwritable-SITEARCH branch reds **all six legs at ctest**, which makes it a first-order item, not a Release-only one:

1. **The install path, on all six legs.** `fixpp::consumer::install-witness` stage-installs on every leg; with the bindings ON and the control absent, an absolute-unwritable `Python3_SITEARCH` would `FATAL_ERROR` in `run_consumer_witness.cmake:55` and red **six** legs at ctest, while an absolute-writable one would silently pollute the runner's interpreter and evade any prefix diff. §4.5 closes all three branches; §4.5.3 asserts it per leg.
2. **The two Release legs' uploaded artifacts.** `packages-linux-{clang,gcc}-release` are the shipped C++ deliverable and `linux-clang-release` runs **no packaging ctest tier at all** (`-L packaging` is gated to `linux-gcc-release`, `:761–773`) — so that artifact has no content witness of any kind. Bounded by §4.5, not by the existing witnesses.
3. **All six `linux` legs' Configure line.** Every leg's Configure changes. A defect in the §4.3 derivation is a six-leg failure — which is why the default arm is fatal (R9).
4. **The required check.** Two independent traps (§5, §5b), both of which red *every* non-release run if half-landed. Neither is deferrable to a follow-up PR.
5. **`ci-script-pins` fires pre-gate.** It is ungated, so the pin's red appears on the #254 PR before Gate A/B labels exist and before the heavy matrix runs. Expect it first; it is the earliest signal that §5b was under-done.
6. **Conan/GHCR caches: unaffected.** The matrix legs keep `-pr conan/profiles/<preset>` and their own seeds; deleting a **restore-only** consumer (`:1268–1270`) removes a reader, never a writer, so no seeded package is invalidated and nothing needs re-seeding.
7. **Actions-cache pool: the WRITER SET is unchanged, one READER is removed, and the CONTENTS grow by at least the wrapper object.** ⚠️ **Restated in v0.3 — the earlier wording ("strictly reduced" / "no effect on the 10 GB budget in either direction") was FALSE**, and round 2 was right to call it (R2-P2-4). The deleted ccache call site was `save: false`, so removing it removes a *reader* and changes no *writer*. But enabling the bindings adds **one cacheable SWIG wrapper object to each of the six writers**, so what each surviving archive *contains* grows. "Strictly reduced" conflated the number of call sites with the size of the pool, and "no effect in either direction" asserted the one thing the change definitely does. ⚠️ **The per-leg runtime cost is ONE ADDITIONAL CACHEABLE EDGE — one miss on the first compatible run, hits thereafter. NOT "one miss per run"; corrected at Gate B round 1 (F7).** #247's 1461-hits/1-miss measurement was taken when the deleted *restore-only reader* consumed a C++ matrix cache that **could not contain** the SWIG wrapper, so a miss was structural there. After this change each surviving **writer** builds and saves that wrapper itself, so later runs can hit it. The measurement supports "one new cacheable compile edge", and plausibly "one miss on the first compatible run" — it does **not** support a recurring per-run subtraction, and that term must not be carried into the net arithmetic. **The effect on the 10 GB budget #240/#241 are measured against is therefore small and POSITIVE, not nil, and it is UNMEASURED: record archive sizes and any eviction effect at close-out** rather than predicting them here.
8. **This PR's own gate posture.** See the header's *"What the scope class means for gate labelling"*: the workflow trigger fires on `^\.specify/[^/]+\.md$` (`:213`), which is also excluded from the pure-doc auto-waive (`:197–202`); and the CMake edit makes the "CI-only, no product code" waiver rationale used on #227/#245/#247/#250/#251 **inapplicable**. `.specify/**` sits in `paths-ignore` for **`push:`** only (`:15–23`), never `pull_request` — so the doc suppresses no PR-side signal.

---

## 10. Open questions

| # | question | owner / disposition |
|---|---|---|
| **OQ-1** | ~~Does `FIXPP_BUILD_PYTHON=ON` change what `fixpp-package` ships?~~ | **CLOSED in v0.2.** It does — and §4.5 prevents it rather than probing whether it matters. The question that *remains* open is narrower and is recorded as **OQ-1b**. |
| **OQ-1b** | **Which `Python3_SITEARCH` branch fires on the CI runner** (`"."`, absolute-writable, or absolute-unwritable)? | **Reported, not gated.** The local probe fires `"."` (E-32); CI has `actions/setup-python@v6` present and is **not** established by it. The §4.5.2 `message(STATUS)` names it on the first CI run. With the control OFF the answer is irrelevant on the matrix legs — but it matters to **#255**, and this is where #255 should read it from. |
| **OQ-2** | The runner-minute table is from run `31273945846`, which **predates #251**. | **Close-out (CO-1).** Edge counts, the preset comparison and the `df` readings are unaffected and need no re-taking. |
| **OQ-3** | ~~Pin option (a) narrow vs (b) re-point onto an inline `case`~~ | **CLOSED in v0.2** — §5b.2 takes neither as stated: the derivation moves into `ci/derive-python-sanitizer.sh` and the pin re-points onto the **script plus its call site**. This resolves the §5b.2-vs-§6.2 contradiction v0.1 carried, is cheaper than v0.1's chosen option, and is what **#248** exists to ask for. |
| **OQ-4** | Narrowing `PY_RE` (the four `conan/profiles/linux-clang-*` terms and `tier1.yml` itself are no longer any surviving python job's build recipe) and the pin's `PY_RE_CASES` table with it. | **#253.** Explicitly NOT in #254 (NG-4). #254 should post a comment on #253 noting that its premise is now half-satisfied by deletion. ⚠️ Note the interaction: narrowing `PY_RE` would also remove the mechanism that makes §4.5.4's ON-side witness free on a `tier1.yml`-only PR. #253 must weigh that. |
| **OQ-5** | Is `python3-dev` actually required once `actions/setup-python@v6` is on PATH, or does `Development.Module` resolve against the tool-cache headers alone? | **Unresolved and deliberately not resolved here** (§4.2). Neither the CI evidence nor the local builds discriminate — every environment tested had both. #254 mirrors the deleted job. A later cleanup may test the narrowing; it must do so with a run that *removes* the package, not by reasoning. ⚠️ It also moves OQ-1b's answer. |
| **OQ-6** | Does the `LD_PRELOAD` in the sanitizer test step remain necessary, given `bindings/python/CMakeLists.txt:104–137` already links asan/ubsan modules against a shared runtime with an rpath? | **Deferred, not #254.** Carried over verbatim (§4.4). It is load-bearing for the watchdog/canary tests' *subprocess children* regardless; testing the narrowing belongs in a change that can afford a red. |
| **OQ-7** | The §4.7 disk step asserts the instrument but no floor. When should it assert a floor? | **#254 records the trigger, not a threshold:** re-open if the **job-end** point shows free space below ~2× the build tree on any leg (CO-4 supplies the first real reading). No floor is invented today (§4.7). |
| **OQ-8** | #248's residue after this lands. | **#248.** Post a comment from the #254 PR enumerating what stopped existing (the cross-job ccache probe steps) **and what #254 delivered on its thesis** (`ci/derive-python-sanitizer.sh`, extracted and tested as itself) so #248 is re-scoped by evidence rather than left nominally open. ⚠️ Ref the *"a commit saying a PR does **not** close an issue is what closed it"* trap — word the comment so no keyword scanner closes #248 by accident. |
| **OQ-9** | The `PY_RE` PR-side match rate (E-30) is computed over the **last 30 merged PRs** using **today's** `PY_RE`, which gained `\.github/workflows/tier1\.yml$` only at #247. | **Accepted as-is.** The rate is a decision input for §3.3, not a gate; a recomputation on the historically-correct regex per PR would move it downward (fewer matches), strengthening rather than weakening the "net negative on most PRs" conclusion. Recorded so the number is not over-read. |

---

## 11. Normative References

`[const §VI.5]` (`.specify/constitution.md:164`) requires *"Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries **from the coverage index** that inform the spec."* Every sibling Phase-2 design doc under `.specify/` carries one (`2a`–`2m`, `architecture.md`, `api-contract.md`; `2d-threading.md:1400` cites §VI.5 by name), so repo precedent treats a `.specify/` design doc as in scope. v0.1's "deliberately absent rather than emitted as empty N/A stubs" note did not cover the one section the constitution names. This section closes that.

**Normative FIX references informing this design: NONE.**

Stated explicitly rather than omitted, because §VI.5's object is *coverage-index* entries — FIX normative sources. This change alters no wire behaviour, no session FSM, no dictionary handling, no message construction, and no public surface. It moves a CI job and adds a CMake install control. **No `[DocAbbrev §X.Y.Z]` entry from `spec/coverage-index.md` informs it**, and inventing one would be worse than none.

**Process / constitutional references** (distinct from normative FIX references, and enumerated here rather than left in the header's `Cites:` line alone) — each verified to resolve to the text claimed:

| citation | line | what it says, as used here |
|---|---|---|
| `[const §VI.5]` | `:164` | this section's own requirement |
| `[const §VII.2]` | `:172` | *"Python tests: pytest against the SWIG bindings"* — the binding test framework |
| `[const §IX.1]` | `:200` | *"…that is the enforced gate; the percentage is the target"* — used **by analogy** in §4.7 for the disk floor, not as a coverage claim |
| `[const §IX.2]` | `:204` | *"Sanitizers — Tier 1 (every PR, Linux/Clang): ASan, UBSan, TSan must all run and pass"* — why a silently-uninstrumented leg is a constitutional false-green (§4.3.2) |
| `[const §IX.6]` | `:213` | Tier 1 enumerates *"…Python pytest…"* — pytest is a required Tier-1 constituent, not an optional lane |
| `[const §XVII.1]` | `:340` | *"Any new design document under `.specify/` … qualifies by default"* — why this doc goes through Gate A |
| `[const §XVII.7]` | `:354` | local pre-PR build gate; **path-conditioned** on `bindings/python/` — met literally once §4.5 lands (PG-9) |
| `[const §XVII.8]` | `:359` | `/speckit-verify` record requirement; noted for the Gate B precondition, not exercised by this doc |

⚠️ **All eight line numbers were re-verified by grep on 2026-08-09, and six of them were off by one to three lines in the v0.2 draft before this check.** Re-verify before citing; a citation carried forward without re-resolution is the `feedback_stale_anchor_repoint_to_a_plausible_twin_is_worse_than_stale` shape.

**Design-document references:** none. `2m-pybind.md` specifies the Python *binding surface*; this change alters no binding surface, only where it is built and whether its artifacts install.

---

## Appendix A — Evidence table

**Tagging rule (fixed in v0.2).** The tag records **how the number was obtained**, not how confident the author is.

- **M** = *measured* — a run or a command whose output **is** the number.
- **D** = *derived* — computed from M rows.
- **I** = *inspection* — read from a tracked file at a cited line, **or** true by construction of the code being described (in which case the mechanism is named).

Line numbers verified in this worktree on **2026-08-09** against a 2205-line `tier1.yml` on branch `ci/fold-python-bindings-into-matrix`.

| id | claim | value | kind | provenance |
|---|---|---:|:--:|---|
| E-1 | `python-bindings (ubsan)` ninja edges (build-dir suffix normalised) | **3632** | M | CI run `31273945846` (2026-08-08), `.ninja_log` edge-set diff |
| E-2 | matrix `linux-clang-ubsan` ninja edges | **3627** | M | same run; **strict superset**: 5 extra, 0 unique to the matrix leg. ⚠️ Establishes **build-graph topology only** — see §3.2's retraction |
| E-3 | the 5 extra edges | SWIG compile · `wrap.cxx` scan · dyndep · `fixppPYTHON_wrap.cxx.o` · link `lib/_fixpp.so` | M | same diff. ⚠️ These are also the edges that carry the install rules (R6) |
| E-4 | edge composition of the tree | **1460 compiles + 1460 scans + 330 links = 3627** | M | same run. ⇒ the `~1748` (`tier1.yml:334`) and `~1987` (`:1189`) comments contradict each other and are **both wrong** |
| E-5 | cache-variable delta, hand-assembled ubsan vs the `linux-clang-ubsan` preset | **`CMAKE_TOOLCHAIN_FILE` only** | **D+I** | flattened `CMakePresets.json` (**derivation** over **inspected** tracked JSON — retagged from `M+I` in v0.1); the override is at `tier1.yml:1403`. ⚠️ Covers *declared cache variables*, not generated compiler/link commands, policy state, discovered Python variables, or install scripts |
| E-6 | runner disk | **145 G total; 87 G free pre-reclaim; 114 G post** | M | `df` in the `free-disk-space` step, run `31273945846`. ⚠️ **CI**, measured **before the build tree exists** |
| E-7 | `linux-clang-ubsan` build tree | **32.96 GiB** (`bin/` 18 G = 330 exes, `lib/` 6.8 G, `CMakeFiles/` 6.4 G) | M | converged **local** tree. ⚠️ Local; do **not** form a quotient with E-6 (§3.2) |
| E-8 | bindings' disk cost | **~126 MiB**, of which `lib/_fixpp.so` **120.83 MiB (96 %)** | M | 35,391,876,429 → 35,523,700,208 B. ⚠️ The reconfigure rebuilt 3053 edges, so this is **not** a controlled 5-edge delta; the `+0.37 %` precision v0.1 quoted is **withdrawn**. The 96 %-is-one-named-file part is independently checkable with one `du`, which §4.7 runs per leg |
| E-9 | ctest's own output | **20 KB** (`build/<dir>/Testing`) | M | local measurement |
| E-10 | June ENOSPC | run `28184395001`, died at edge **1651/1748**, ~94 % through, in the link phase | M | ⇒ the `~14 GB` comment was **accurate when written**. ⚠️ `1748` here is a **ninja edge total**, not an executable count; v0.1 used it both ways |
| E-11 | `python-bindings` cost | **316 runner-min/run** — asan 113.1 · ubsan 99.5 · tsan 95.8 · none 7.6 | M | run `31273945846` job durations. ⚠️ **Gross deleted job time**, on the event classes where the job ran — **not** a net saving (E-29) |
| E-12 | matrix `linux-clang-ubsan` | **39.9 min**, of which ctest **30.5 min** (350 tests) | M | same run |
| E-13 | merged ubsan projection | **≈40 min** vs `timeout-minutes: 240` | **D** | E-12 + ~12 s pytest + 5 ninja edges. **Not a measurement** — CO-1 replaces it |
| E-14 | run wall vs critical path | wall **135.2 min**; `linux-clang-coverage` **134.8 min**; longest python leg 113.1 min | M | ⇒ NG-5: runners, not wall-clock |
| E-15 | pytest, CI | `python-bindings (ubsan)` **82 passed / 11.83 s** | M | run `31273945846` |
| E-16 | pytest, local, merged config **from the preset** | `linux-clang-ubsan` **82 passed** 11.61 s, module 120.8 MiB | M | local; exact match to E-15 |
| E-17 | pytest, local, on the two **never-built-before** Release legs | `linux-gcc-release` **82 passed** 5.51 s (20 MiB) · `linux-clang-release` **82 passed** 5.31 s (14 MiB) | M | local gcc **13.3.0** = CI's gcc-13; local clang 22.1.2 vs CI 22.1.8. ⚠️ RTK's pytest summariser reported *"81 passed, 1 skipped"* for a run whose raw output was **"82 passed"** — a fabricated skip; read the raw file. **⇐ this is Goal 2's evidence row** (v0.1 cited E-13) |
| E-18 | PR-side pytest cost | **5.31 / 5.51 / 11.61 s** on **3 of 6** configurations | M | E-16/E-17. ⚠️ **A three-of-six sample, not a per-leg measurement.** No debug, asan or **tsan** number; tsan's subprocess watchdog/canary tests are exactly where extrapolating is unsafe. CO-3 closes it |
| E-19 | `python_touched` on non-PR events | **`true` unconditionally** — 12 of the last 12 runs, **by construction** | **I** | `tier1.yml:164–172`: `if [ "$EV" = "pull_request" ]; then … else PYTHON_TOUCHED=true; fi`. ⚠️ **Retagged M→I in v0.2.** The "12 of 12" is the workflow's `else` branch restated as data, not a measurement of anything |
| E-20 | R5 — setup already present | `actions/setup-python@v6` + `pip install pytest pyyaml` on **every** matrix leg | I | `tier1.yml:379–392`; pytest required by `decimal_compare_oracle` |
| E-21 | apt delta | **`swig` + `python3-dev`** | I | matrix `:373–377` vs python `:1227–1232`. ⚠️ corrects the brief's *"only `swig` is new"* |
| E-22 | `BUILD_SHARED_LIBS` across presets | ON for `linux-clang-{asan,ubsan,tsan}`; **not set** for `linux-clang-{debug,release}`, `linux-gcc-release` | I | `CMakePresets.json:51,65,79` ⇒ the python legs' `-DBUILD_SHARED_LIBS=ON` was redundant on three legs and **never applied to the other three** |
| E-23 | `tier1-required` shape | `needs:` 8 names at `:2091–2092`; `python-bindings` asserted at `:2174–2177` (`success`) and `:2183–2186` (exactly `skipped`); enumerating comment at `:2145` | I | ⇒ §5. **Both** branches red if left behind — corrects the brief's "the false branch". **Six** sites, not five |
| E-24 | the regression pin | `ci/test-tier1-python-policy.sh` — `jobs["python-bindings"]` at `:77`; needs-census at `:249–256`; step-parameterisation at `:164–195` (`:171–172`); mutants A/C1/C2/D/E at `:287/344/370/399/430`; invoked from `ci-script-pins` at `tier1.yml:1899–1902`; `ci-script-pins` **ungated** (`:1873–1876`) and asserted at `:2126/2129` | I | ⇒ §5b. **Absent from the brief's edit list** |
| E-25 | R6 — install rules | four `install()` calls at `:196/197/205/216`, **unconditional** under `FIXPP_BUILD_PYTHON=ON`; destination `${Python3_SITEARCH}` **or** `"."` chosen at `:190–194`; file ends at 224 | I | `bindings/python/CMakeLists.txt`. Reached by: consumer stage-install on **all six** legs (`tier1.yml:569–581`, `tests/consumer/run_consumer_witness.cmake:48–56`), `fixpp-package` on both Release legs (`:806–808`), upload (`:812–821`); packaging tier gated to `linux-gcc-release` (`:761–773`) |
| E-26 | R8 — no `-Werror` on the wrapper | `fixpp_maybe_werror` is per-target (`cmake/Helpers.cmake:31–39`) and **never called** in `bindings/python/CMakeLists.txt` | I | grep over that file: 0 occurrences. `linux-clang-release` inherits `FIXPP_WERROR=ON` (`CMakePresets.json:14`); `linux-gcc-release` sets it OFF (`:105`) — neither reaches `fixpp_py` |
| E-27 | test-step source | `Run Python tests` `tier1.yml:1552–1556`; `Run Python tests under sanitizer` `:1572–1587` (runtime resolution, `ubsan → ubsan_standalone`, `LD_PRELOAD`, `PYTHONPATH`, per-invocation `san_opts`) | I | brief's line reference for the sanitizer step (**1572**) confirmed |
| **E-28** | required status-check contexts on `main` | exactly `Gate A — design review label check`, `Gate B — PR review label check`, `tier1-required`, `tier2-required`, `tier3-required`; `rulesets` = **`[]`**; **no `python-bindings (*)` context required** | M | `gh api repos/CatalinSerafimescu/fixpp/branches/main/protection` + `…/rulesets`, **2026-08-09**. ⚠️ **The only row measuring mutable state OUTSIDE the repository.** Every other row is re-derivable from tracked files or a recorded run. **Must be re-verified inside the merge window** — PG-7 |
| **E-29** | net runner-minute arithmetic | `net = 316 − (apt swig+python3-dev × 6) − (5 ninja edges × 6) − (pytest wall × 6) − (1 ccache miss × 6)` | **D** | E-11 minus E-18/E-31 terms. ⚠️ Every subtrahend is small; **the label is what was wrong in v0.1, not the magnitude**. And the whole expression is conditional on the event class — E-30 |
| **E-30** | PR-side `PY_RE` match rate | **11 of the last 30 merged PRs (37 %)** | M | `gh pr list --state merged --limit 30` × `gh pr view --json files`, each file list tested against `PY_RE` (`tier1.yml:161`), 2026-08-09. ⇒ on **19/30 (63 %)** of merged PRs the deleted job cost **0** and #254 is **net positive** runner-minutes. ⚠️ Computed with **today's** `PY_RE` — see OQ-9 |
| **E-31** | command-level merge evidence + the ccache prediction | **1457/1457 compile commands byte-identical**; `FIXPP_BUILD_PYTHON=ON` *"adds exactly one compile edge and perturbs nothing else"*; measured on run `31256807981`: **1461 hits / 1 miss over 1462 cacheable calls** | M | #244 part 1 / PR #247 (`CLAUDE.md:12`, issue #254 body). Gating mechanism verified at `CMakeLists.txt:333–335` (`if(FIXPP_BUILD_PYTHON) add_subdirectory(bindings/python) endif()`) — **I**. ⇒ this is the **mechanism** behind §9.7's cache claim, which v0.1 asserted without one |
| **E-32** | R6 instrument, **proven non-zero on the unfixed tree** | `grep -c '^[[:space:]]*file(INSTALL' build/linux-clang-tsan-py/bindings/python/cmake_install.cmake` = **4**; destinations `${CMAKE_INSTALL_PREFIX}/.` and `${CMAKE_INSTALL_PREFIX}/./_fixpp_data` | M | local, 2026-08-09. ⇒ the **`"."` branch fires locally**, and §4.5.3's `== 0` assertion is a working instrument, not a broken one. ⚠️ Measured in a `-py`-suffixed build dir (the **deleted job's** naming) while §4.5.3 will read `build/<preset>/…`; the grep pattern matches CMake's generator output, which is **independent of the build-dir name**, so the instrument is proven on the same population. ⚠️ **Does not speak for CI**, where `actions/setup-python@v6` precedes Configure (OQ-1b) |
| **E-33** | L-056-4 invariant site count | **4** — `spec/behaviors-and-limitations.md:1649`, `tier1.yml:1599–1610`, `bindings/python/pyproject.toml:11–14`, `bindings/python/cibw-before-all.sh:23–26` | M+I | two-axis census, 2026-08-09: `grep -rn 'python-bindings'` **and** `grep -rn 'no Python\|TEST VEHICLE\|not a byte of them ships'`, excluding `specs/`. ⚠️ **The second axis is not optional**: L-056-4's own home file does not contain the string `python-bindings` and is invisible to the first |

| **E-34** | R6 instrument, **the OFF side, now measured** — E-32's missing half | `grep -c '^[[:space:]]*file(INSTALL' build/probe-none-B/bindings/python/cmake_install.cmake` = **0**, file present (44 lines), **zero** `_fixpp`/`fixpp*.py`/`_fixpp_data` references | M | local, 2026-08-09. Obtained by **reconfiguring an existing tree** with `-DFIXPP_INSTALL_PYTHON=OFF` — 19 s, **no rebuild**, because `cmake_install.cmake` is regenerated at generate time. ⇒ v0.2's *"could not test OFF locally without configuring a fresh tree (expensive)"* disposition was simply **wrong**, and E-32 is now a pair rather than a half. Same run printed `FIXPP_INSTALL_PYTHON=OFF; Python3_SITEARCH=''; FIXPP_PY_INSTALL_DIR='.'` — §4.5.2's `message(STATUS)` firing |
| **E-35** | §4.5.5's two semantic witnesses, **all four cells measured** — each mode's RED is the other configuration, no synthetic self-test | tree `build/probe-none-B`, `-DFIXPP_BUILD_TESTS=OFF` (1843 edges; the install closure without the 330 test executables), `DESTDIR`-staged:<br>· **ON / present → GREEN**, 268 staged entries<br>· **ON / absent → RED**, 5 payload entries at the prefix root (`_fixpp.so`, `_fixpp_data`, `fixpp.py`, `fixpp_oo.py`, `fixpp_dict_data.py`)<br>· **OFF / absent → GREEN**, 0 payload in 258 staged<br>· **OFF / present → RED**, all 9 required entries missing | M | local, 2026-08-09. ⚠️ **268 − 258 = 10** — module + 3 `.py` + `_fixpp_data/` + `__init__.py` + 4 XMLs, which corroborates **what the ON side stages**. ⚠️ **RETRACTED at Gate B round 1 (F6b): this does NOT show the scan "neither over- nor under-matches".** A count identity survives compensating misses and false hits, and **no code asserts the arithmetic at all** — it was an inference a human drew from two numbers. The scan's correctness rests on the four measured cells and on the two counter-tests (a staged `fixpp_helpers.py` now reds `absent`; a staged `fixppXpy` now reds `present`), not on this subtraction. ⚠️ Verified `FIXPP_BUILD_TESTS=OFF` does not change install content: **no `install()` rule sits inside the `if(FIXPP_BUILD_TESTS)` block** (`CMakeLists.txt:349–424`; all 9 top-level rules are outside it) |

**Figures deliberately NOT restated as current:** the `~7.7 GB` / `5.82 GiB` Actions-cache pool numbers (`tier1.yml:459–471`) — unaffected by this change in either direction (§9.7), and conditional on a manual `gh cache delete` that this doc does not verify.

**Withdrawn in v0.2:** *"the tree is still growing (1748 → 3627 edges since June)"* — the two counts are not shown to have comparable graph composition (today's 3627 includes 1460 module-scan edges). NG-1 stands on the margin alone. And *"the merged configuration already runs in CI, green"* — see §3.2; the retraction must also land in the #254 issue comment, the plan doc, and the memory that carry it.

---

## Appendix B — Convergence log

| round | source | findings | disposition |
|---|---|---|---|
| — | — | — | **v0.1** is the pre-review draft (2026-08-09). |
| 1 | Codex Gate A review — `research/reviews/codex_ci254_python-fold_review.md` | **P1=1 P2=6 P3=3** | Judged by the Opus adversarial pass; per-finding resolution in Appendix C. |
| 1 | Opus adversarial review — `research/reviews/opus_ci254_python-fold_adversarial_review.md` | **P1=3 P2=7 P3=6**, **4 root causes** | Verdict: *"v0.2 can ship after a SINGLE convergence pass — not a rewrite. Nothing in 16 findings touches the design's spine."* Applied in full except where Appendix C records a deliberate non-application. → **v0.2** (this document). |

---

## Appendix C — v0.1 → v0.2 resolution

### C.1 Root causes (the reshaping work)

| # | root cause | how v0.2 closes it |
|---|---|---|
| **RC-1** | **An install side-effect treated as an open question when it violates a standing decision.** Collapses Codex #1, N1, N2, and the header's "Scope class: CI/workflow only" / §9 "touches no CMake" self-description. | **§4.5 is new and is a design decision, not a probe.** `FIXPP_INSTALL_PYTHON`, default **ON** (NG-8 forbids the `if(DEFINED SKBUILD)` shortcut that would regress 056 LAY-1), OFF on the six legs; the branch is **reported** at Configure; §4.5.3 is an **asserting** step with the instrument proven non-zero (E-32); §4.5.4 records that the ON-side witness already fires on this PR. **The scope class is rewritten, not patched** — header, §9's file table, NG-2, R6, OQ-1, and the Gate-A/B labelling precedent all restated. R6 is promoted to top weight in §7 and to **item 1** in §9's blast radius (the unwritable branch reds six legs, not two). |
| **RC-2** | **A topology measurement promoted to a behavioural claim**, propagated into four documents, with M/D tag drift. | **§3.2 retracts** *"the merged configuration already runs in CI, green"* and replaces it with the exact narrower statement (edge-set superset ⇒ build-graph topology, nothing else), naming the four places the retraction must land. **Appendix A's preamble fixes the tagging rule** (the tag records *how the number was obtained*): **E-5 `M+I` → `D+I`**, **E-19 `M` → `I`** with the tautology mechanism quoted from `tier1.yml:164–172`, **E-18** relabelled a three-of-six **sample** with CO-3 to close it, **E-11** relabelled **gross**. |
| **RC-3** | **The acceptance layer specifies numbers for a human to read, not steps that exit non-zero.** | **§8 is restructured into PG-1…PG-9 (pre-merge gates, each naming a command that exits non-zero and who runs it), §8.2 (truth-table coverage — what one PR can and cannot observe), and CO-1…CO-4 (close-out observations).** Specifically: the `continue-on-error` self-waiving disk step is **split** into an asserting measurement + a non-fatal report (§4.7); AC-3's PR-body diff becomes the §4.5.3 CI assertion with the local diff demoted to a *recorded* half; AC-6 moves to CO-1 **with a ±10 % tolerance**; AC-8's human eyeball becomes a machine **declared-vs-run mutant count** (§5b.2 item 8); AC-1/AC-2's two rules for one instrument become **one rule** (`N ≥ collect-only count`, no hardcoded 82). |
| **RC-4** | **The scope census was drawn from the intended diff, not the repository.** | **§4.8 replaces the four-row comment table with a dispositioned census derived over TWO axes** — `python-bindings` **and** the L-056-4 sentence, because L-056-4's home file is invisible to the first axis (E-33). 19 load-bearing rows + a descriptive/historical table + a "**C. Added by this change**" section, with an explicit instruction to **re-derive after the last fix commit**. §9's "two-file" claim becomes an eight-file table; rollback is described **semantically**. |

### C.2 Codex findings

| # | sev (post-judging) | Opus judgement | resolution in v0.2 |
|---|---|---|---|
| **#1** | **P1** | **CONFIRM** — worse than "unverified"; both existing witnesses structurally blind | **Applied** — §4.5 (the whole section), R6, PG-3, §9 item 1. Prescription (`FIXPP_INSTALL_PYTHON` default ON, OFF on the matrix) taken **as corrected**: the `if(DEFINED SKBUILD)` variant is explicitly **forbidden** (NG-8). Codex's *two* new assertions reduced to **one**: the OFF-side. The ON-side already exists (§4.5.4) |
| **#2** | **P2** | **CONFIRM** — the claim also travelled to 3 other artifacts | **Applied** — §3.2 retraction with the exact replacement wording + the four-place instruction; E-5 → `D+I` |
| **#3(a)** | P3 | CONFIRM (downgraded) | **Applied** — §3.2 states E-6 is CI/pre-build and E-7 is local; **no "×N margin" quotient** is stated anywhere in v0.2 |
| **#3(b)** | P3 | **premise accepted, prescription REJECTED on measurement** | **Premise applied, prescription NOT applied.** The `+0.37 %` precision is withdrawn and the 3053-edge caveat kept (E-8). Codex's remedy — build **two clean CI-equivalent UBSan trees** — is **deliberately not adopted**: it costs two full sanitizer builds to refine a number that cannot change any decision, when 96 % of the delta is **one named file** checkable with one `du`. §4.7 reports `du -h build/<preset>/lib/_fixpp.so` per leg instead |
| **#3(c)** | P3 | CONFIRM | **Applied** — the `1748 → 3627` growth claim is **withdrawn** (NG-1, Appendix A "Withdrawn"); the two uses of `1748` (executable count vs ninja edge total) are kept apart (E-10) |
| **#3(d)** | **P2** | CONFIRM | **Applied** — §4.7 delivers the issue's **four `df` points**, the operative one at **job end** (after `Upload packages`, `:814`) rather than after Build, because the consumer install-witness, the packaging sub-project builds and CPack all land later. The issue's **fallback** ("scope down to the legs that fit") is recorded under **R1**, which v0.1 omitted entirely |
| **#4** | **P2** | CONFIRM, plus N3 | **Applied** — Goal 1 and §3.3 restate the saving **per event class**; E-11 relabelled **gross**; E-29 carries the net arithmetic; E-18 relabelled a sample |
| **#5** | **P2** | CONFIRM (RC-3) | **Applied** — see RC-3. All four sub-claims (AC-3 blind, AC-4 self-waiving, AC-6 post-merge, AC-7 impossible shapes) resolved individually |
| **#6** | — | **DISCHARGED BY EVIDENCE** | **Result recorded as E-28** with the mutable-external-state caveat and a **named re-verification at merge (PG-7)**. Codex's reasoning was right; its conclusion is discharged |
| **#7** | P3 | **DOWNGRADE P2 → P3**, confirm as P3 | **Applied at P3 weight** — **§11 Normative References** added, stating explicitly that **no FIX normative source informs this change** and separating process/constitutional citations (each verified to its line) from normative FIX references. Not treated as structural |
| **#8** | **P2** | **ESCALATE P3 → P2** (RC-4) | **Applied and extended** — §4.8's two-axis census covers Codex's three sites plus the six further `tier1.yml` comment sites and the **four-file L-056-4 invariant** Codex did not reach |
| **#9** | P3 | CONFIRM, all three verified | **Applied** — Goal 2 now cites **E-17** (was E-13); §3.2's preset argument credits **§4.3** (was §4.2); `[const §XVII.7]` is restated as **path-conditioned** and PG-9 explains that §4.5 makes it apply **literally**, rather than keeping an interpretation that happens to become true |
| **#10** | P3 | CONFIRM, narrowed | **Applied as narrowed** — §6.1 relabels the three non-duplicate steps as **deliberate observability deletions**, lists them in a table, and states the loss is **nil** with the reason (they described the deleted legs; the survivors have their own annotations). Rollback described **semantically** in §9 |

### C.3 Opus-only findings

| # | sev | resolution in v0.2 |
|---|---|---|
| **N1** | **P1** | **Applied** — §4.5.1 leads with the uploaded-artifact consequence and the user's #254 comment as the *decisive* warrant; §4.8 row 8 reconciles the note §4.7-of-v0.1 promised to preserve, stating that its **no-Python half stays TRUE by construction** under `FIXPP_INSTALL_PYTHON=OFF`. E-33 pins the four sites |
| **N2** | **P1** | **Applied** — §4.5.2's three-outcome table is in the doc verbatim in substance, including the **prefix-blind** middle branch that would have passed v0.1's AC-3 and the **unwritable** branch that reds six legs. The **cheap discriminator** is taken: a configure-time `message(STATUS)` reading `Python3_SITEARCH` (no build, no install). §9's blast radius **reordered** so the six-leg branch is item 1. ⚠️ **One refinement beyond the review:** the review offered "read the generated `cmake_install.cmake`" as an *alternative* discriminator; v0.2 uses it for a **different** purpose — as the OFF-side **assertion instrument** (§4.5.3), because with the control OFF that file has no install directives and so cannot name the branch. Both are kept, each for the job it can do |
| **N3** | **P2** | **Applied, and measured rather than deferred.** E-19 retagged **I** with the mechanism quoted. The decision-relevant PR-side rate the review said "is never measured" **is now measured**: **11/30 merged PRs match `PY_RE`** (E-30, a `gh` query as the review suggested). §3.3's per-event-class table replaces the single headline, and Goal 1 no longer quotes −316 unqualified |
| **N4** | **P2** | **Applied — counter-proposal taken.** §4.3.1 moves the derivation into `ci/derive-python-sanitizer.sh`; §5b.2 re-points the pin onto the **script + its call site** instead of v0.1's option (b); §6.2's "#248 shrinks" is now **true** and #254 delivers a down payment on it. **The review's addition is taken too**: the pin asserts the **call site** (item 4), not just the script, so a tested-but-uninvoked script cannot read as a pass. ⚠️ Note what was **not** dropped: (a)'s re-basing work — `EXPECTED_NEEDS`→7, the deleted assertions, and mutant M4 for the never-tested needs census — all survive; only the re-point *target* changed |
| **N5** | P3 | **Applied** — PG-2 carries **one rule for all six legs** (`N passed` with N ≥ the same step's `pytest --collect-only -q` count). The hardcoded `82` is gone, with the reason stated (053→056 each added pytests) |
| **N6** | P3 | **Applied** — §3.2 adds the **1457/1457 byte-identical compile-command** evidence and the `CMakeLists.txt:333–335` `add_subdirectory` gating as **E-31**, and §9 item 7 now carries the **mechanism** (exactly one ccache miss per leg per run, measured 1461/1 by #247) that v0.1 asserted without one |

### C.4 Deliberately not applied

| what | why |
|---|---|
| **Codex #3(b)'s prescription** — build two clean CI-equivalent UBSan trees (Python OFF and ON) and report `du -sb` deltas | **Rejected on measurement**, per the adversarial review. Two full sanitizer builds to refine a number that cannot change any decision; **96 % of the delta is one named file**, and §4.7 reports it per leg every run. The premise (E-8 is not a controlled delta) **is** accepted — the precision is withdrawn |
| **Codex #1's second assertion** — "the default/ON wheel install still contains the Python payload" as a *new* check | **Unnecessary, not wrong.** `python-wheel-test` already installs the shipped wheel and imports it on 3.10–3.13, and `PY_RE` guarantees it fires on this PR (§4.5.4). Adding a second ON-side witness would duplicate a working one |
| **Codex #5's "labelled `workflow_dispatch` / temporary fixture strategy"** for event-truth-table testing | **Not adopted as a workflow change.** §8.2 instead states plainly which shapes this PR can observe, covers the unreachable `python_touched=false` shape through the **pin's behavioural `decide_run` drive with synthetic inputs** (which already exists and is cheaper), and moves `push:main` to CO-2. Adding dispatch fixtures to `tier1.yml` for one PR's benefit would be permanent machinery for a transient need |
| **Codex #7's weight (P2, "violates §VI.5")** | **Downgraded to P3** per the adversarial judgement and shipped as a one-section fix. §VI.5's object is *coverage-index* FIX entries; a CI/CMake change has none, and the absence is now stated explicitly rather than left to inference. It cannot make CI behave wrongly |
| **v0.1's own §5b.2 option (b)** (teach the PyYAML pin to parse the inline `case`) | **Superseded by N4.** Kept in the record because the *cost note* attached to it was correct and is the reason the counter-proposal wins |
| **Rewriting the historical `python-bindings` references** in `spec/coverage-index.md`, `spec/feature-catalogue.md`, `spec/behaviors-and-limitations-closed.md`, `CLAUDE.md`, `CLAUDE-history.md` | **Deliberate LEAVE** (§4.8 B). These record what PY-001/002/003 delivered *at the time*, keyed to closed L-rows and merged PRs. Rewriting them would falsify a record. The review agreed ("descriptive only — do not rewrite"); it is stated here so the omission is not read as an oversight |

### C.5 Not closed

| item | why it stays open |
|---|---|
| **Which `Python3_SITEARCH` branch fires on CI** (OQ-1b) | Cannot be closed without a CI run. The local probe fires `"."` (E-32); CI has `actions/setup-python@v6` in front of Configure and no run has ever executed `cmake --install` there. **Mitigated, not resolved:** §4.5's control makes the answer irrelevant on the matrix legs, and §4.5.2 reports it on the first run. **#255 should read it from there** |
| **Per-leg pytest wall on debug / asan / tsan** (E-18, CO-3) | Requires a merged run. The three-of-six sample is labelled as such and R3's conclusion is stated at that strength |
| **Net runner-minutes** (E-29, CO-1) | The subtrahends are not measured on all six legs, and the before-figures predate #251. The doc states the arithmetic and the event-class conditionality rather than a number |
| **E-28's durability** (PG-7) | Branch protection is mutable state outside the repository. It cannot be *closed* by this doc — only re-verified inside the merge window, which PG-7 requires |
| **The retraction in three external artifacts** (§3.2) | The #254 issue comment, the plan doc and the memory carrying *"the merged configuration is already running, green"* are outside this file. The doc names them; the PR must actually edit them |

### C.6 Net effect — what the document is now that v0.1 was not

Five changes at the level of the document, distinct from the per-finding rows above:

1. **The scope class changed.** #254 is no longer a CI-only change. It carries **one mandatory CMake edit** (`bindings/python/CMakeLists.txt` — the `FIXPP_INSTALL_PYTHON` option + the branch report) and **one new extracted script** (`ci/derive-python-sanitizer.sh`). The Gate-A **waiver precedent** of #227/#245/#247/#250/#251 no longer applies; this PR should carry `gate-a-done`.
2. **The blocking risk moved from open to designed-out.** R6 went from *"this doc deliberately asserts neither 'it breaks' nor 'it's fine'"*, ranked 4th in the blast radius, to **top-weight, item 1, with a control, a reported branch, and an assertion whose instrument is proven non-zero (E-32)**. The single strongest consequence: with the control OFF, **all three of N2's `Python3_SITEARCH` outcomes are closed at once** — the pollution branch, the prefix-blind branch, and the six-legs-red branch.
3. **Acceptance became falsifiable.** Ten mixed criteria became **9 pre-merge gates each naming a command that exits non-zero**, **4 close-out observations**, and an explicit **§8.2 truth-table-coverage section** stating which run shapes a single PR can and cannot produce. The self-waiving `continue-on-error` disk step, the PR-body diff, the hardcoded `82`, and the human-eyeballed mutant count are all gone.
4. **The census went from a claim to a derivation.** A 4-row comment table and a "two-file scope" became a **two-axis, 19-row load-bearing census + 6 descriptive rows across 8 files**, with a *"C. Added by this change"* section and a standing instruction to re-derive after the last fix commit. The second axis is not optional: the L-056-4 invariant lives in 4 files and one of them is invisible to the first axis (E-33).
5. **The evidence base was retagged and extended.** **3 rows retagged** (E-5 `M+I`→`D+I`, E-19 `M`→`I`, E-18 relabelled a 3-of-6 sample), **2 claims withdrawn** (the merged-green behavioural claim; the 1748→3627 growth trend), **6 rows added** — E-28 (branch protection, the only mutable-external-state row), E-29 (net arithmetic), E-30 (**PR-side `PY_RE` match rate, 11/30 — measured, not deferred**), E-31 (1457/1457 command-level evidence + the ccache mechanism), E-32 (the R6 instrument proven non-zero at 4), E-33 (the four-site invariant census).

**What did not move: the spine.** Fold the bindings into all six `linux` legs, delete the ~555-line job, derive the sanitizer identity once and fail closed, and move the `tier1-required` and pin contracts in the same commit. Sixteen findings across two reviews landed on the install side-effect, the evidence tags, the acceptance layer and the census — none on the thesis.

---

## Appendix D — Gate A round 2 outcome: **P1=0 P2=5 P3=4. NOT converged to the P2=0 bar.**

Round 2 (Codex, `research/reviews/codex_ci254_2_python-fold_review.md`) audited v0.2's claimed
closures and returned **P1=0 / P2=5 / P3=4**, with the verdict: *"v0.2 is implementable in principle,
and the default-ON/OFF-on-six-legs control is the right core design. It is not yet Gate-A-ready
because the install and sanitizer acceptance layers remain capable of certifying less than their
stated contracts."*

⚠️ **Process deviation, recorded rather than hidden.** The Gate A bar is `P1==0 AND P2==0`. It was
**not** met. Round 2's findings were triaged **inline by the orchestrator** instead of by an
independent Opus adversarial fork, and this appendix was written by the orchestrator rather than a
rewriter fork — both deviations from `/gate-a-ph2`'s orchestrator-only rule, taken under an explicit
user decision after the Claude weekly budget hit its 95 % threshold mid-loop. **Consequence: no
independent reviewer has re-checked the dispositions below.** Gate B must treat every row as
unverified and check it against the implemented diff.

All five P2s are **acceptance-layer requirements on the implementation**, not design changes. They are
binding on the PR:

| # | Finding | What the implementation MUST do |
|---|---|---|
| **R2-P2-1** | `grep -c 'file(INSTALL'` is an implementation-**shape** pin, not a package-**content** gate. It can pass if the payload later moves to `install(CODE)`/another directory, and it never opens either Release artifact. | Keep the grep as a cheap structural pin **and add a semantic witness**: `cmake --install` under a temporary `DESTDIR`, then reject `_fixpp*`, `fixpp.py`, `fixpp_oo.py`, `fixpp_dict_data.py`, `_fixpp_data` anywhere in the staged tree. This is the instrument that actually caught R6 (orchestrator probe, 2026-08-09). |
| **R2-P2-2** | The ON-side witness covers only the `SKBUILD` wheel path. `if(SKBUILD AND FIXPP_INSTALL_PYTHON)` would pass `python-wheel-test` **and** all six OFF-side checks while silently regressing feature 056's LAY-1 non-`SKBUILD` in-tree install. | Add a **non-`SKBUILD` default-option install witness**: configure `FIXPP_BUILD_PYTHON=ON` with no `FIXPP_INSTALL_PYTHON` override, install under `DESTDIR`, assert the module + 3 `.py` + `_fixpp_data/__init__.py` + 4 XMLs are present. Does not need a second full build. |
| **R2-P2-3** | The extracted script emits `sanitizer`, `rt_base`, **and** `san_opts`, but only `sanitizer`'s consumption is pinned. UBSan's `halt_on_error=1` can go **dead** and pytest then exits green after a recoverable UBSan report — the exact false-green the extraction exists to prevent. | Pin **all three** consumers in the workflow; add a mutant per output (`rt_base`, `san_opts`) each proven RED for its own reason; assert the resolved runtime file **exists** before invoking pytest. |
| **R2-P2-4** | "Actions-cache pool strictly reduced" / "no effect on the 10 GB budget" are **false**: deleting the restore-only job removes a *reader*, but enabling the binding adds one cacheable SWIG wrapper object to each of the six *writers*. | Restate as: writer set unchanged, one reader removed, **contents change by ≥ the wrapper object**. Drop "strictly reduced" and "no effect". Record archive sizes / eviction effects at close-out.  ✅ **DONE** — §9 item 7 restated in v0.3; the two false phrases are gone and the residual is named as UNMEASURED-at-close-out rather than predicted. |

| **R2-P2-5** | The retraction of *"the merged configuration already runs in CI, green"* lived outside the executable scope. | ✅ **Done by the orchestrator before implementation** — `ci-compiler-cache-pass-plan.md` corrected in place; the memory corrected and a dedicated lesson recorded; a corrective comment posted to #254. The doc's §9 table must still list the repo-resident corrections so their absence fails review. |

P3s, also binding but cheap — **all four CLOSED at Gate B round 1**: **(1)** ✅ *done* — the `~126 MiB`
total is gone from `tier1.yml` (the only place it appeared outside this doc; the "96 %" was never in
the workflow, contrary to round 1's F2), and the surviving in-place derivation-hygiene caveats at §3.2
and §9 are deliberately KEPT, since they are what annotate the hazard at the point of use. Drop the
`~126 MiB` total and the derived "96 %" — the uncontrolled 3053-edge reconfigure cannot support them;
keep only the independently measured `_fixpp.so` = 120.83 MiB. **(2)** ✅ *done* — PG-2 restated in §8. PG-2's pytest rule was wrong on two counts — pytest exits
**5** when it collects nothing (it is not `ctest -L`'s vacuous success), and "passed ≥ collected"
fails on any legitimate skip/xfail; use `--collect-only` > 0, then trust pytest's own status.
**(3)** ✅ *done* — both files were re-pointed as §4.8 table A rows 20–21, in the deletion commit. The
touched-file count is **9** excluding this doc, not 8; and `bindings/python/tests/conftest.py:6` +
`test_gil_release_canary.py:16` describe the **current** CI vehicle (the latter already stale — it
omits UBSan), so they are re-point targets, not history. **(4)** ✅ *done* — E-30 records its counting
command and its PR-number population. E-30 must record the PR-number
population or the exact counting command, or be downgraded from **M**.

⚠️ **Round-1 note (N3):** the PR body's *"Appendix D P3s (1), (2) and (4) are unaddressed"* was already
wrong about **(4)** when it was written — E-30 had recorded its population. All four P3s are now
closed, and the PR body is corrected.
