# Quickstart — validating 087 system include binding

**Feature**: 087-system-include-binding · **Date**: 2026-08-04 · **Issue**: #234

Every step produces evidence for a named requirement, so the `/speckit-verify` record can cite it by id.

> **Worktree.** Run from `~/Work/Programming/fixpp-parallel`, not `research/G19-fix-fpml-iso20022/library`
> (which holds another session's branch — never `git checkout` there). `/speckit-verify` and `/gate-b`
> hardcode the main checkout; substitute this path.

## 0. Prerequisites

```bash
cd ~/Work/Programming/fixpp-parallel
export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache          # unset by default; silently uses ~/.cache/ccache
export FIXPP_087_EVIDENCE=~/fixpp-087-evidence        # DURABLE, not /tmp
mkdir -p "$FIXPP_087_EVIDENCE"
df -h /mnt/wsl/fixppbuild                             # Debug trees are 22-31 GB; check headroom
```

Build with **`-j2` maximum** — wider parallelism OOM-kills the host. One build owner per build directory.

> ### ⚠️ Every `ctest` invocation below carries `--no-tests=error`
>
> `fixpp::consumer::install-witness` is registered only inside `if(FIXPP_BUILD_CODEGEN_TOOL)` nested in
> `if(FIXPP_BUILD_TESTS)`, and **`ctest` exits 0 when a filter matches nothing**. There is no project-wide
> `--no-tests=error`. A wrong preset therefore reports green having asserted **nothing**. Capture the selected
> **count**, not just the exit code.
>
> **This banner binds a human; FR-014 binds CI.** The same hazard on a CI lane is closed by the
> `consumer`-label registration-count assertion prescribed in contract §6 — modelled on the ones
> `.github/workflows/tier1.yml:528-540` and `tier2.yml:371-384` already carry for `packaging`, and required
> on **all three** workflows that run the witness (`tier1.yml`, `tier2.yml`, `tier3-libcxx.yml`; contract
> §6a). *(Scope corrected at Gate A round 2 — it named tier 1 alone.)*

## 1. Reproduce the R1/R3 measurement (Linux) — FR-001, FR-009

This is the measurement the whole design rests on. It is configure-only; it does **not** rebuild the library.

> **The staged prefix is 086's, and on a clean machine it does not exist.** `-DFIXPP_STAGE_PREFIX=` below
> points at `/tmp/fixpp-stage-086`, the tree 086 left behind. That is legitimate to *reuse* — it is a real
> `cmake --install` of this repository, which is exactly what the measurement needs — but it is not
> reproducible from nothing, so the staging step is written out here rather than assumed. Reusing it is also
> not required: any prefix works, because the comparison is prefix-relative (R5), and R6 measured a different
> prefix to the same relative result.

```bash
B=/mnt/wsl/fixppbuild/build/087-fileapi-probe
MAIN=/mnt/wsl/fixppbuild/build/linux-clang-release

# Create the staged install if it is not already on disk (086 left it there).
# Any prefix works — the comparison is prefix-relative.
[ -d /tmp/fixpp-stage-086 ] || cmake --install "$MAIN" --prefix /tmp/fixpp-stage-086

rm -rf "$B"; mkdir -p "$B/.cmake/api/v1/query"
touch "$B/.cmake/api/v1/query/codemodel-v2"     # MUST exist BEFORE configure (contract §2a)

cmake -S tests/consumer -B "$B" -G Ninja \
  "-DCMAKE_TOOLCHAIN_FILE=$MAIN/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DFIXPP_STAGE_PREFIX=/tmp/fixpp-stage-086
```

Read the observation (reply names carry a hash — glob, never hard-code, contract C-5):

```bash
python3 - <<'PY' | tee "$FIXPP_087_EVIDENCE/linux-observed.txt"
import json,glob,os
B=os.path.expandvars("/mnt/wsl/fixppbuild/build/087-fileapi-probe/.cmake/api/v1/reply")
for n in ("probe_usage_requirements","probe_service_positive","probe_umbrella"):
    f=glob.glob(os.path.join(B,f"target-{n}-*.json"))
    d=json.load(open(f[0]))
    incs=[i for cg in d.get("compileGroups",[]) for i in cg.get("includes",[])]
    print(f"{n}: {len(incs)}")
    for i in incs: print(f"   isSystem={i.get('isSystem',False)} {i['path']}")
PY
```

**Observed — this is the whole feature in one output** (measured 2026-08-04; verbatim in `research.md` R6 and
in `research/reviews/orchestrator_087-system-include-binding_gate_a_r1_measurements.md`):

| target | entries |
|---|---|
| `probe_usage_requirements` (links `fixpp::capi`) | **1** — `<prefix>/include/capi` |
| `probe_service_positive` (links `fixpp::service`) | **2** — `service-iface`, `capi` |
| `probe_umbrella` (links `fixpp::fixpp`) | **7** — `<prefix>/include` + asio, OpenSSL, zlib, OpenTelemetry, protobuf, abseil |

The **1 vs 7** contrast is what makes the instrument discriminating: the narrow interface carries a single
root, the wide one carries an include root plus six third-party system roots.

> **Do not read the 7 as the reverted `fixpp::capi` set.** It is `probe_umbrella`'s, measured on a *different
> target*. A regression in `fixpp::capi`'s narrowing makes its set gain the umbrella include root and
> third-party roots of this class — but the reverted set also **retains `include/capi`**, which this 7-entry
> set does not contain, so the two are provably different and the reverted cardinality has never been
> measured. See `research.md` R3's correction box and contract §5's demonstration-#2 box.

## 2. Reproduce the R6 measurement (MSVC-under-Conan) — FR-009

FR-009 forbids prescribing the mechanism from one platform. **This measurement has been taken** — the verbatim
command and output are in `research.md` R6, and the full ten-target listing for both platforms is in
`research/reviews/orchestrator_087-system-include-binding_gate_a_r1_measurements.md`. The steps below
reproduce it.

Procedure: `reference_msvc_local_build_procedure` — **this is a memory slug, not a repository path; there is
no in-tree file of that name.** The reproducible parts of it are inlined below: sandbox
`C:\temp\fixpp-parallel`, **BuildTools** (note: under `C:\Program Files (x86)\`), toolset pinned.

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44.35207
where cl  &REM fail loudly if vcvars did not take — do NOT >nul this
cmake --install build\windows-msvc-debug --prefix C:\temp\fixpp-stage-087
mkdir C:\temp\087-fileapi-probe\.cmake\api\v1\query
type nul > C:\temp\087-fileapi-probe\.cmake\api\v1\query\codemodel-v2
cmake -S tests\consumer -B C:\temp\087-fileapi-probe -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=C:/temp/fixpp-parallel/build/windows-msvc-debug/conan_toolchain.cmake ^
  -DCMAKE_BUILD_TYPE=Debug -DFIXPP_STAGE_PREFIX=C:/temp/fixpp-stage-087
```

Then **read the Windows reply** — this step was missing and is the one that makes R6 auditable rather than
asserted. From WSL, the sandbox is visible at `/mnt/c/...`, so the same reader as §1 is used with only the
reply root changed:

```bash
python3 - <<'PY' | tee "$FIXPP_087_EVIDENCE/msvc-observed.txt"
import json,glob,os
B="/mnt/c/temp/087-fileapi-probe/.cmake/api/v1/reply"
idx=sorted(glob.glob(os.path.join(B,"index-*.json")))
print(f"reply index: {os.path.basename(idx[-1]) if idx else 'NONE'}")
for n in ("probe_usage_requirements","probe_service_positive","probe_umbrella"):
    f=glob.glob(os.path.join(B,f"target-{n}-*.json"))
    d=json.load(open(f[0]))
    incs=[i for cg in d.get("compileGroups",[]) for i in cg.get("includes",[])]
    print(f"{n}: {len(incs)}")
    for i in incs: print(f"   isSystem={i.get('isSystem',False)} {i['path']}")
PY
```

**Observed (2026-08-04): identical counts and classification to §1** — 1 / 2 / 7, all `isSystem=true`, paths
with forward slashes (`C:/temp/fixpp-stage-087/include/capi`), observed order the same. Reply index
`index-2026-08-04T17-33-02-0177.json`; staged prefix `C:/temp/fixpp-stage-087`. Verbatim output in
`research.md` R6. Record to `$FIXPP_087_EVIDENCE/msvc-observed.txt`.

> Delete the scratch `.bat` afterwards — a back-sync without `--delete` leaks it into the live repo.

## 3. Run the gate — SC-001, SC-005, SC-008

```bash
ctest --test-dir build/linux-clang-release -L consumer --no-tests=error --output-on-failure
```

**Expected**: `fixpp::consumer::install-witness` **Passed**, 1 test selected, and the log reports **both** legs
compared equal — exactly two leg results, no more and no fewer (contract C-6.4).

The registration count is asserted separately, the way CI will (FR-014, contract §6):

```bash
ctest --test-dir build/linux-clang-release -L consumer -N | sed -n 's/^Total Tests: //p'   # expect 1
```

## 4. Demonstrate RED — FR-007, SC-002, SC-003, SC-004

**Mandatory. A gate never observed failing proves nothing.** Nine causes, each recorded with its **real exit
status** (`> file` discards it), the **asserted diagnostic token**, and the first diagnostic line. Restore
between each and re-confirm green. **The contract's §5 table is the authority**; this is the operational
form of it.

**Four** induction classes, one per row — see contract §5 for why *(this list said "two" until Gate A round 2;
the table below has always used four)*:

- **expectation** (#1): mutate the **declared expectation** alone, before the correct one exists. The vacuity
  proof — the only class that reds the gate without touching package, reply or invocation.
- **package-side** (#2, #7, #8): mutate the tree, re-stage, re-run the witness.
- **invocation** (#6a): drive the shipped comparator wrongly — **no tree or reply mutation at all**. All
  three of #6a's sub-cases are plain `cmake -P` calls because contract C-6.4 requires the leg-set assertion to
  be a **separately invocable mode** of `compare_system_includes.cmake` rather than logic buried in the
  carrier's declaration. Without that, "one leg missing" and "a leg duplicated" could only be induced by
  editing `tests/consumer/CMakeLists.txt` — which this class forbids.
- **reply-side** (#3–#6): `cp -r` a **real** reply directory, mutate/delete the **copy**, and invoke the
  **shipped** `tests/consumer/compare_system_includes.cmake` against the copy. The sub-build is wiped and
  reconfigured on every witness run (`run_consumer_witness.cmake:46`), so a persistent tree edit can never
  reach the observed side — "drop an entry from the observed side" is **not** achievable by editing the tree.

| # | class | induce | asserted token | expect |
|---|---|---|---|---|
| 1 | expectation | *before the correct expectation exists*, declare the **service** leg as `include/service-iface` only, omitting `include/capi` | `LEAK` | red naming `include/capi` — the **vacuity proof**, taken *before* the gate is ever green. `LEAK` direction by construction so it cannot be confused with #3 |
| 2 | package | the exact diff at contract §5's demonstration-#2 box against `src/capi/CMakeLists.txt:97-99` — **not** a `PRIVATE`→`PUBLIC` keyword flip, which is not the same edit; `:112-115` is **not** touched | `LEAK` | red naming the entries the reverted interface adds. **Qualitative:** the set gains the umbrella include root and third-party roots; **record the count you observe** — no expected count is stated, and none has been measured on a reverted `fixpp::capi` |
| 3 | reply | in a **copy** of a real reply, delete one entry from `target-probe_service_positive-*.json`'s `compileGroups[].includes[]` | `DROP` | red naming the deleted entry — reachable only because C-1 asserts **equality**, not containment |
| 4 | reply | in a **copy**, flip one entry's `isSystem` `true`→`false`, paths untouched | `RECLASSIFIED`, **and that alone** | red naming the path and both classifications, with **no** `LEAK` or `DROP` alongside — the path matches on both sides so C-1 stage 1 removes the pair. The **only** demonstration that exercises the classification leg — `isSystem` never varies in the happy path |
| 5 | reply | in a **copy**, delete the per-target reply file; then, separately, the whole reply directory | `MISSING_REPLY` | red **naming the missing artifact** — must not read as "no includes", and distinct from #6 |
| 6 | reply | in a **copy**, truncate the per-target JSON mid-object so it is present but unparseable | `INPUT_ERROR` | red naming the file and the parse failure, **distinguishable** from `MISSING_REPLY` and from every C-1 token. This is FR-008 / SC-004 |
| 6a | invocation | **three sub-cases, all `cmake -P`**: compare mode with an unknown `leg`; **leg-set** mode over **one** result file (missing leg); leg-set mode over the **same result file twice** (duplicated leg) | `LEG_ERROR` | red naming the offending leg, **distinguishable from `INPUT_ERROR`** — a corrupt reply and a mis-driven carrier are different defects. Makes C-6.4's "exactly two legs" observable. **All three are mandatory**: the missing-leg case is the one C-6.4 exists for (a `capi`-only comparator reporting green) and is *not* discharged by the unknown-leg case |
| 7 | package | delete the **new 087 target** `probe_system_include_contract`; then, separately, delete `compare_system_includes.cmake` and keep the target | build failure | `ninja: error: unknown target 'probe_system_include_contract'` (Ninja's phrasing — *not* Make's "No rule to make target"); with only the script gone, the target's own command fails. Deleting an 086 target instead would re-prove an 086 obligation |
| 8 | package | restore the **pre-086** service `$<INSTALL_INTERFACE:>` value in `src/service/CMakeLists.txt` **alone** — the exact diff and its `git show cb397284` provenance are in contract §5's demonstration-#8 box; **not** a deletion of the entry, which would be a different mutation and a different token | `LEAK` **and** `DROP` | observed becomes `{include, include/capi}` vs expected `{include/service-iface, include/capi}`: red naming **`include` as observed-but-unexpected** *and* **`include/service-iface` as expected-but-absent**, from one mutation. **Plus the carrier's own capi-leg result from the same invocation**, still exactly `include/capi` |

For every direct `compare_system_includes.cmake` invocation in rows **3–6a**, pass the same staged install
prefix that the original configure used as compare mode's third argument. The copied replies preserve absolute
paths from that staged tree; without the prefix the comparator cannot perform C-3's prefix-relative comparison.
An entry outside that prefix remains absolute and therefore reds as `LEAK`, which is exactly what row #2's
package-side revert expects of the third-party Conan roots.

> ### ⚠️ #8 is not interchangeable with #2 — FR-007a
>
> `fixpp_service` links `fixpp_capi`, so reverting the C-ABI leg reds **both** legs. A service red obtained
> that way is not attributable to the service leg. 086 established this hazard by measurement; it is
> inherited, not hypothetical.
>
> **The same-run evidence is emitted BY THE GATE, out of one reply directory — not a second staging run and
> not your own follow-up read.** One `codemodel-v2` reply directory holds a `target-<name>-*.json` for every
> target, and contract C-6.2 requires the carrier to run **`capi` before `service`** while `compare` writes
> its per-leg result **before** it terminates. So the run that reds `service` already produced the `capi`-leg
> result showing exactly `include/capi`: record **that**, from the carrier's output (contract §2b).
>
> *(Until Gate A round 2 this said to read `target-probe_usage_requirements-*.json` out of the reply directory
> yourself. That works only because `run_consumer_witness.cmake:46` wipes the sub-build at the **start** of a
> run, so a failed run leaves its reply on disk — an incidental property no requirement pinned. FR-007a puts
> the obligation on what the run captures, so it is now a gate property. The manual read remains available as
> a cross-check; it is no longer what discharges FR-007a.)*

## 5. Full suite — SC-005

```bash
ctest --test-dir build/linux-clang-release --no-tests=error --output-on-failure
echo $? > "$FIXPP_087_EVIDENCE/ctest-after.rc"
```

**Expected**: all tests pass, and the three properties 086 already compares still compare equal — 087 adds a
leg and must not weaken the existing one (FR-012).

> **Run the FULL suite, not `-L consumer`.** A label-filtered run misses the codegen count pin and the
> git-cleanliness gate, and `fixpp::dict::codegen-build-graph-check` fails on an **uncommitted tree** — commit
> before the final run or it reds for a reason unrelated to this feature.
