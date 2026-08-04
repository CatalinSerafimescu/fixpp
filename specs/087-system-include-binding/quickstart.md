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

## 1. Reproduce the R1/R3 measurement (Linux) — FR-001, FR-009

This is the measurement the whole design rests on. It is configure-only; it does **not** rebuild the library.

```bash
B=/mnt/wsl/fixppbuild/build/087-fileapi-probe
MAIN=/mnt/wsl/fixppbuild/build/linux-clang-release

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

**Expected — this is the whole feature in one output:**

| target | entries |
|---|---|
| `probe_usage_requirements` (links `fixpp::capi`) | **1** — `<prefix>/include/capi` |
| `probe_service_positive` (links `fixpp::service`) | **2** — `service-iface`, `capi` |
| `probe_umbrella` (links `fixpp::fixpp`) | **7** — prefix + asio, OpenSSL, zlib, OpenTelemetry, protobuf, abseil |

The **1 vs 7** contrast is the discrimination: a regression in `fixpp::capi`'s narrowing moves its set to the
7-entry shape and hands a C-ABI consumer six third-party system roots.

## 2. Reproduce the R6 measurement (MSVC-under-Conan) — FR-009

FR-009 forbids prescribing the mechanism from one platform. Procedure:
[`reference_msvc_local_build_procedure`] — sandbox `C:\temp\fixpp-parallel`, **BuildTools** (note: under
`C:\Program Files (x86)\`), toolset pinned.

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

**Expected: identical counts, order and classification to §1** — 1 / 2 / 7, all `isSystem=true`, paths with
forward slashes. Record to `$FIXPP_087_EVIDENCE/msvc-observed.txt`.

> Delete the scratch `.bat` afterwards — a back-sync without `--delete` leaks it into the live repo.

## 3. Run the gate — SC-001, SC-005

```bash
ctest --test-dir build/linux-clang-release -L consumer --no-tests=error --output-on-failure
```

**Expected**: `fixpp::consumer::install-witness` **Passed**, 1 test selected, and the log reports both legs
compared equal.

## 4. Demonstrate RED — FR-007, SC-002, SC-003, SC-004

**Mandatory. A gate never observed failing proves nothing.** Six causes, each recorded with its **real exit
status** (`> file` discards it) and first diagnostic. Restore between each and re-confirm green.

| # | induce | expect |
|---|---|---|
| 1 | assert a deliberately wrong expectation | red — the **vacuity proof**, taken *before* the gate is ever green |
| 2 | `src/capi/CMakeLists.txt`: `PRIVATE` → `PUBLIC` | red; capi's set **1 → 7**, naming the third-party roots |
| 3 | drop an expected entry from the observed side | red — reachable only because C-1 asserts **equality**, not containment |
| 4 | delete the reply file | red **naming the file** — must not read as "no includes" |
| 5 | delete the carrier target | build fails by name: `ninja: error: unknown target '<name>'` (Ninja's phrasing — *not* Make's "No rule to make target") |
| 6 | revert the service `$<INSTALL_INTERFACE:>` **alone** | red, **plus same-run evidence the capi leg stayed isolated** |

> ### ⚠️ #6 is not interchangeable with #2 — FR-007a
>
> `fixpp_service` links `fixpp_capi`, so reverting the C-ABI leg reds **both** legs. A service red obtained
> that way is not attributable to the service leg. Capture `fixpp::capi`'s properties from demonstration #6's
> own staged prefix and confirm `include/capi` + `$<LINK_ONLY:>` were still in force while the service leg
> failed. 086 established this hazard by measurement; it is inherited, not hypothetical.

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
