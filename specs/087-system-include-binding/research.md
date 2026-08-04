# Research — 087 system include directories bound at the installed-package consumer

**Feature**: `087-system-include-binding` · **Date**: 2026-08-04 · **Issue**: [#234](https://github.com/CatalinSerafimescu/fixpp/issues/234)

Every decision below that says **MEASURED** was produced by running the command shown against the **real
consumer project and a real staged install** — not a fixture. That distinction is the direct lesson of 086,
whose R9 measured a mechanism on a 5-target Linux/clang fixture, prescribed it, and had it fail under
Conan/MSVC after Gate B sign-off.

---

## R1 — Does the CMake File API report include directories with a system/non-system classification?

**Decision: YES. `codemodel-v2` → per-target reply → `compileGroups[].includes[]`, each entry carrying
`path` and `isSystem`. MEASURED.**

Method — the query file must be created **before** configure:

```bash
mkdir -p "$B/.cmake/api/v1/query" && touch "$B/.cmake/api/v1/query/codemodel-v2"
cmake -S tests/consumer -B "$B" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=<main-build>/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DFIXPP_STAGE_PREFIX=/tmp/fixpp-stage-086
```

CMake **3.30.0** on this host. The reply directory contains one `target-<name>-<config>-<hash>.json` per
target; the include list is `compileGroups[].includes[]`.

**Rationale for this instrument over the alternatives**: it reports CMake's *own* model of the include
interface, so it needs no compiler-specific command-line parsing (`-I` vs `/I` vs `-isystem` vs
`/external:I`) and no compiler invocation. `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` read via
`$<TARGET_PROPERTY:>` was rejected because there is **no documented collected form of it on a consumer**,
which is precisely why 086 declined to write this leg (spec Context).

**Alternatives considered**: (a) parse `compile_commands.json` — works, but re-introduces per-compiler flag
spelling, the thing this instrument exists to avoid; (b) `$<TARGET_PROPERTY:...,SYSTEM_INCLUDE_DIRECTORIES>`
via `file(GENERATE)` — the vacuous form, empty by construction; (c) invoke the compiler with `-v` and diff
the search path — measures the *compiler*, not the *package interface*, and is maximally platform-specific.

> ### ⚠️ This configure is hand-rolled, not the driver's — four divergences, each dismissed explicitly
>
> The measurement above configures the **real** `tests/consumer` project against a **real** staged install
> (that is the correction of 086's 5-target fixture mistake). It is still not byte-for-byte the configure the
> shipped gate will run, `tests/consumer/run_consumer_witness.cmake:77-97`. Stating the equivalence implicitly
> is what this project's doc-drift incidents are made of, so the four differences are enumerated and
> dismissed one by one:
>
> | # | measured here | the driver | why it cannot change `compileGroups[].includes[]` |
> |---|---|---|---|
> | 1 | `-DFIXPP_STAGE_PREFIX=/tmp/fixpp-stage-086` | `${FIXPP_WITNESS_WORK_DIR}/stage` (`:33`) | the expectation is **prefix-relative** (R5): the prefix is stripped from both sides before comparison, and R6 measured a *different* prefix on MSVC producing an identical relative set. This is the one divergence with positive evidence, not merely an argument |
> | 2 | `-DCMAKE_BUILD_TYPE=Release` fixed | `-DCMAKE_BUILD_TYPE=${FIXPP_BUILD_TYPE}` (`:83`) | the include entries come from the imported targets' `INTERFACE_INCLUDE_DIRECTORIES`, and the staged export declares those in the **config-independent** `lib/cmake/fixpp/fixppTargets.cmake`; the per-config `fixppTargets-release.cmake` carries **no** include-directory property at all *(measured on the staged tree: `grep -c INTERFACE_INCLUDE_DIRECTORIES` → 16 and 0 respectively)*. R6 corroborates: **Debug** on MSVC against **Release** here, same relative set |
> | 3 | `-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang` explicit | `${FIXPP_CXX_COMPILER}` / `${FIXPP_C_COMPILER}` passed through (`:84-85`) | the compiler selects nothing here: R2 measured that **no compiler-supplied root appears at all**, on either platform, and R6 ran a completely different compiler (MSVC) to the same relative result |
> | 4 | *(omitted)* | `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (`:89`) | it changes what CMake **writes out** (`compile_commands.json`), not what it models. The codemodel reply is produced from the same internal model either way, and enabling it adds no include directory to any target |
>
> **Residual.** These are arguments plus two cross-platform corroborations, not a re-take through the driver.
> The re-take is free once implementation step 1 lands — adding the query write to the driver *is* step 1 —
> and it supersedes this box. Until then the equivalence is argued, and this box is where that is admitted.

---

## R2 — Do compiler **built-in** search paths appear in the observation?

**Decision: NO. Only build-system-supplied entries appear. MEASURED.**

`probe_capi_positive`, `probe_usage_requirements`, `probe_capi_negative`, `probe_capi_negative_service` each
report **exactly one** include entry. No `/usr/include`, no `/usr/lib/clang/*/include`, no libc++ root.

**This resolves the clarification dependency the spec recorded against `/speckit-plan`** (spec
Clarifications, Q1): the expected set is **small and stable**, and **no toolchain-root enumeration or
tolerance rule is required**. FR-003a's closed-set expectation is therefore cheap to state exactly, and does
not churn with compiler or SDK versions — which was the entire risk behind the original
"enumerate vs classify" framing.

---

## R3 — Does the observation **discriminate** a narrowed interface from a wide one?

**Decision: YES, and by a large margin. MEASURED on the real consumer project, same configure run.**

| target | links | include entries |
|---|---|---|
| `probe_usage_requirements`, `probe_capi_positive{,_c}`, `probe_capi_negative{,_service}`, `consumer_capi_witness` | `fixpp::capi` | **1** |
| `probe_service_positive`, `probe_service_negative` | `fixpp::service` | **2** |
| `probe_umbrella`, `consumer_witness` | `fixpp::fixpp` | **7** |

The narrow set:

```
isSystem=True  <prefix>/include/capi
```

The wide set — the same run, so the difference is the *target's interface*, not the environment:

```
isSystem=True  <prefix>/include
isSystem=True  ~/.conan2/p/asio…/p/include
isSystem=True  ~/.conan2/p/b/opens…/p/include        (OpenSSL)
isSystem=True  ~/.conan2/p/b/zlib…/p/include
isSystem=True  ~/.conan2/p/b/opent…/p/include        (OpenTelemetry)
isSystem=True  ~/.conan2/p/b/proto…/p/include        (protobuf)
isSystem=True  ~/.conan2/p/b/absei…/p/include        (abseil)
```

**This makes #234's hypothetical concrete rather than theoretical.** The six third-party roots are all
`isSystem=true` — exactly the class the issue describes as invisible to a reachability probe aimed at two
named headers. Were `fixpp::capi`'s narrowing to regress, its observed set would gain the umbrella include
root and third-party roots of this class, handing a C-ABI consumer the asio, OpenSSL, protobuf and abseil
headers. The gate's signal is therefore unmissable, not marginal — the discrimination is between **one entry**
and **a set that includes six third-party roots**, not a one-entry margin.

> ### ⚠️ The reverted `fixpp::capi` set is NOT this 7-entry set, and its cardinality is unmeasured
>
> *(Corrected at Gate A round 1. The earlier wording — "its observed set would move **1 → 7**" — was an
> inference from a **different target**, restated downstream as a normative expectation.)*
>
> The 7 above is measured on `probe_umbrella`, which links `fixpp::fixpp`. It is **not** a measurement of a
> reverted `fixpp::capi`, and it cannot be substituted for one:
>
> - the revert demonstration mutates `src/capi/CMakeLists.txt:97-99` (the link line). The
>   `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/capi>` root is declared in a **separate command** at
>   `:112-115` and therefore **survives** the mutation — so the reverted set still contains `include/capi`;
> - the 7-entry umbrella set above contains `<prefix>/include` and **not** `<prefix>/include/capi`. The two
>   sets are therefore provably different;
> - what the reverted set actually is, is `{include/capi} ∪ closure(fixpp_capi_objects)` — and nothing
>   measures `closure(fixpp_capi_objects)` to equal `closure(fixpp::fixpp)`; it is a subset of it.
>
> **No replacement figure is stated here or anywhere in this bundle**, deliberately: substituting a second
> inferred number would reproduce the defect this box exists to remove. Demonstration #2's expectation is
> **qualitative** (contract §5) and the count is recorded when the revert is actually run.

**A discrimination this large also satisfies US2 by construction**: the observed side is non-empty in the
passing state (1 and 2 entries), so "empty" is never the expected value and can never be mistaken for a pass.
That is the structural difference from the three existing legs, which legitimately compare empty-to-empty.

---

## R4 — What exactly is the expected set, per leg?

**Decision: MEASURED, and the two legs genuinely differ — confirming the clarify decision to bind both.**

| leg | expected entries (paths relative to the staged prefix) |
|---|---|
| `fixpp::capi` | `include/capi` — **1**, `isSystem=true` |
| `fixpp::service` | `include/service-iface`, `include/capi` — **2**, both `isSystem=true`, observed in that order — *recorded, not asserted* (the comparison is over an unordered set; contract §1a, `data-model.md` I2) |

The service leg carries `include/capi` because `fixpp::service` links `fixpp::capi` (086 contract §2), and
`include/service-iface` from its own independently-declared `$<INSTALL_INTERFACE:>`. **Had 087 bound only the
C-ABI leg, the `service-iface` root would have gone unasserted** — the measurement confirms the clarify
answer rather than merely agreeing with it.

**Everything observed is `isSystem=true`.** Imported targets' `INTERFACE_INCLUDE_DIRECTORIES` are treated as
SYSTEM by default. Classification is therefore *uniform* in the passing state, so FR-003a's
classification-mismatch leg is a guard against future change rather than a currently-varying value — worth
stating so nobody later reads a green classification check as evidence of something it did not exercise.

---

## R5 — Path normalisation: the expectation cannot be written as absolute paths

**Decision: the comparison MUST normalise the staged prefix away before comparing. MEASURED constraint.**

Observed paths are **absolute** and embed the stage location (`/tmp/fixpp-stage-086/include/capi`) — which is
a per-run, per-machine value, and on Windows a different spelling entirely. An expectation written as absolute
paths would fail for reasons having nothing to do with the include interface, which is the
`feedback_bench_ab_needs_same_session_control`-shaped mistake of comparing across environments.

The expectation is therefore stated **relative to `FIXPP_STAGE_PREFIX`**, with the observed side made relative
before comparison. Separator normalisation is required too (Windows replies use `/` in JSON, but the prefix
CMake substitutes may not match byte-for-byte) — 086 hit the sibling of this bug and left a standing note
about joining both sides identically (`tests/consumer/CMakeLists.txt`, SEPARATOR NORMALISATION).

---

## R6 — MSVC-under-Conan (FR-009's second half)

**Decision: the mechanism behaves IDENTICALLY on MSVC-under-Conan. MEASURED. FR-009 is DISCHARGED and
FR-010a's scope-out contingency is NOT needed.**

Method — native MSVC sandbox `C:\temp\fixpp-parallel`, BuildTools 2022 (`vcvars64.bat
-vcvars_ver=14.44.35207`), Ninja, Debug, the existing Conan toolchain; the MSVC Debug build installed to
`C:\temp\fixpp-stage-087`; the **real** `tests/consumer` project configured against it with the same
`codemodel-v2` query.

### The extraction command and its verbatim output

*(Transcribed at Gate A round 1. Both platforms' reply directories were re-read from disk for the record —
nothing was re-configured. Provenance:
`research/reviews/orchestrator_087-system-include-binding_gate_a_r1_measurements.md`, which carries the full
ten-target listing for both platforms; the excerpts below are verbatim from it. This transcription is the
whole difference between "measured on both platforms" and "measured on Linux, projected to MSVC", which is
the epistemic state 086 was in for six Gate B rounds.)*

Reply roots and indices — the two are **different directories from different runs**, which is what makes this
two measurements rather than one:

| | reply root | reply index | staged prefix |
|---|---|---|---|
| Linux/clang/Release | `/mnt/wsl/fixppbuild/build/087-fileapi-probe/.cmake/api/v1/reply` | `index-2026-08-04T17-29-32-0215.json` | `/tmp/fixpp-stage-086` |
| MSVC/Debug under Conan | `/mnt/c/temp/087-fileapi-probe/.cmake/api/v1/reply` | `index-2026-08-04T17-33-02-0177.json` | `C:/temp/fixpp-stage-087` |

```bash
python3 - <<'PY'
import json,glob,os
for label,B in (("LINUX/clang/Release","/mnt/wsl/fixppbuild/build/087-fileapi-probe/.cmake/api/v1/reply"),
                ("MSVC/Debug (under Conan)","/mnt/c/temp/087-fileapi-probe/.cmake/api/v1/reply")):
    print(f"===== {label} =====")
    idx=sorted(glob.glob(os.path.join(B,"index-*.json")))
    print(f"  reply index: {os.path.basename(idx[-1]) if idx else 'NONE'}")
    for n in ("probe_usage_requirements","probe_capi_positive","probe_capi_positive_c",
              "probe_capi_negative","probe_capi_negative_service","consumer_capi_witness",
              "probe_service_positive","probe_service_negative","probe_umbrella","consumer_witness"):
        f=glob.glob(os.path.join(B,f"target-{n}-*.json"))
        if not f:
            print(f"  {n}: <no reply>"); continue
        d=json.load(open(f[0]))
        incs=[i for cg in d.get("compileGroups",[]) for i in cg.get("includes",[])]
        print(f"  {n}: {len(incs)}")
        for i in incs: print(f"      isSystem={i.get('isSystem',False)}  {i['path']}")
PY
```

**Verbatim output — Linux/clang/Release** (the three load-bearing targets; the record has all ten):

```
  reply index: index-2026-08-04T17-29-32-0215.json
  probe_usage_requirements: 1
      isSystem=True  /tmp/fixpp-stage-086/include/capi
  probe_service_positive: 2
      isSystem=True  /tmp/fixpp-stage-086/include/service-iface
      isSystem=True  /tmp/fixpp-stage-086/include/capi
  probe_umbrella: 7
      isSystem=True  /tmp/fixpp-stage-086/include
      isSystem=True  /home/catalin/.conan2/p/asio6e6c781a0fee4/p/include
      isSystem=True  /home/catalin/.conan2/p/b/opens4e49d3aae8182/p/include
      isSystem=True  /home/catalin/.conan2/p/b/zlib9f9522eeb1c41/p/include
      isSystem=True  /home/catalin/.conan2/p/b/opent042cee1005be7/p/include
      isSystem=True  /home/catalin/.conan2/p/b/proto5552344d6d496/p/include
      isSystem=True  /home/catalin/.conan2/p/b/absei1ed536903cba7/p/include
```

**Verbatim output — MSVC/Debug under Conan** (same three):

```
  reply index: index-2026-08-04T17-33-02-0177.json
  probe_usage_requirements: 1
      isSystem=True  C:/temp/fixpp-stage-087/include/capi
  probe_service_positive: 2
      isSystem=True  C:/temp/fixpp-stage-087/include/service-iface
      isSystem=True  C:/temp/fixpp-stage-087/include/capi
  probe_umbrella: 7
      isSystem=True  C:/temp/fixpp-stage-087/include
      isSystem=True  C:/Users/Catalin/.conan2/p/asio6e6c781a0fee4/p/include
      isSystem=True  C:/Users/Catalin/.conan2/p/b/opens93d7079eb81c0/p/include
      isSystem=True  C:/Users/Catalin/.conan2/p/b/zlib218ea5f1c653e/p/include
      isSystem=True  C:/Users/Catalin/.conan2/p/b/opent6c3b664c24219/p/include
      isSystem=True  C:/Users/Catalin/.conan2/p/b/protobc5e6c7617229/p/include
      isSystem=True  C:/Users/Catalin/.conan2/p/b/absei4772290644d78/p/include
```

### The comparison

| target | Linux/clang/Release | MSVC/Debug | identical? |
|---|---|---|---|
| `probe_usage_requirements` | 1 — `include/capi` | 1 — `include/capi` | **yes** |
| `probe_capi_positive`, `probe_capi_negative` | 1 | 1 | **yes** |
| `probe_service_positive`, `probe_service_negative` | 2 — `service-iface`, `capi` | 2 — same, same order | **yes** |
| `probe_umbrella` | 7 — prefix + 6 third-party roots | 7 — same shape, Windows Conan cache paths | **yes** |

Four findings that matter for implementation:

1. **Entry counts and `isSystem` are identical across platforms**, as is the observed ordering (*recorded,
   not asserted* — the comparison is over an unordered set; contract §1a). The expectation is therefore
   one set per leg, not one per platform.
2. **No compiler built-ins on MSVC either** (R2 holds). The Windows SDK and MSVC standard-library directories
   arrive through the `INCLUDE` environment variable set by `vcvars64.bat` — they are *not* CMake-supplied,
   so they never enter `compileGroups[].includes[]`. This is the same reason `/usr/include` is absent on
   Linux, so it is one rule, not a coincidence holding twice.
3. **The File API emits forward slashes even on Windows** (`C:/temp/fixpp-stage-087/include/capi`), which
   removes the separator-normalisation hazard R5 anticipated for the observed side. The *expected* side must
   still be built from `FIXPP_STAGE_PREFIX` with forward slashes to match.
4. **The prefix differs by platform** (`/tmp/fixpp-stage-086` vs `C:/temp/fixpp-stage-087`), confirming R5:
   the comparison must be prefix-relative or it is machine-specific.

> **Why this measurement was made before writing the mechanism into any artifact.** 086's R9 measured
> `try_compile` on a Linux/clang fixture, prescribed it in the contract, and cleared six Gate B rounds before
> `windows-msvc-debug` proved it could not resolve Conan's imported-target closure — forcing a post-sign-off
> round. FR-009 exists to make that sequence impossible here, and this is it being satisfied rather than
> deferred. **The residual 086 declared and did not close is the one 087 closed first.**

---

## R7 — Anti-vacuity: how this gate is prevented from passing while measuring nothing

**Decision: six independent guards, because "the gate observed nothing" is this feature's dominant risk.**
*(Three at first draft; the fourth and fifth were added at Gate A round 1 and this enumeration was not swept
until round 2. Guard 6 is the CI registration-count assertion from contract §6, titled "The last vacuity
path". The remaining asymmetry is disclosed below rather than mis-stated as mechanised.)*

1. **A non-empty expectation.** Unlike the three existing legs, the expected set here is non-empty (R4), so an
   empty observation cannot equal it. Emptiness fails by arithmetic, not by a special case.
2. **Reply-existence check.** The reply directory and the per-target reply must exist and parse; absence is a
   `FATAL_ERROR` naming the missing file (FR-005). A File API query that was never created yields *no reply
   at all*, which is the realistic failure and must not read as "no includes".
3. **By-name requirement.** The carrier is listed in `run_consumer_witness.cmake`'s `_required_targets`, so
   deleting it fails the build (FR-006). 086 established that "the gate can be removed without anything
   noticing" is the same defect class as "the gate cannot fail".
4. **Script deletion fails the carrier's own command.** The comparator is a standalone script invoked by
   `probe_system_include_contract`, so deleting `compare_system_includes.cmake` — including its `leg-set`
   mode — fails that target's build command even though the target still exists (contract C-6.1/C-6.3,
   demonstrated by §5 row #7's second sub-case).
5. **Exact-leg-set validation.** Success requires exactly two per-leg result files naming the distinct known
   legs `capi` and `service`; one file, the same file twice, or an unknown leg all red as `LEG_ERROR`
   (contract C-6.4, demonstrated by §5 row #6a). This is the anti-vacuity guard that prevents a `capi`-only
   comparator from reporting green while FR-001a disappears.
6. **The `consumer` label's registration count is asserted in CI, before the test step, on every workflow
   that runs the witness** — `tier1.yml`, `tier2.yml`, `tier3-libcxx.yml` (FR-014, contract §6/§6a). This is
   the only guard that sits **outside** the gate: `ctest -L` exits 0 when it selects nothing, so a lane where
   `FIXPP_BUILD_CODEGEN_TOOL` goes OFF would otherwise report green having run the gate zero times. No
   demonstration inside the gate can reach it.

> **One last turtle remains, and it is review-enforced rather than gate-enforced.** The carrier's own command
> list is not self-policing: deleting the inline `leg-set` invocation from `tests/consumer/CMakeLists.txt`
> leaves the build green in every state the gate can otherwise reach. The sibling case *is* caught — deleting
> one `compare` invocation leaves one result file and `leg-set` reds it — but the invocation of `leg-set`
> itself is necessarily outside the script and therefore outside the gate's own reach. This is the same class
> of review-time-only invariant contract C-4 / `data-model.md` I4 already disclose: real, admitted, and not
> misdescribed as mechanised.

---

## Residual risks carried into implementation

- **Both platforms are measured** (R3 Linux/clang/Release, R6 MSVC/Debug), so FR-009 is discharged and no
  platform scope-out is required. What remains unmeasured is *other* toolchains — gcc, libc++ — which share
  CMake's model rather than a compiler-specific path, so the risk is low but **not zero and not asserted
  here**. It is covered by CI **executing the same gate**: the witness carries the `consumer` label
  (`CMakeLists.txt:420-421`), and each unmeasured toolchain has a named `ctest` step that runs it —
  **gcc** → `.github/workflows/tier1.yml:544` (unfiltered on `linux-gcc-release`; `:513`'s `-LE packaging`
  covers the five clang lanes and retains `consumer`); **libc++** → `.github/workflows/tier3-libcxx.yml:341`
  and `:349`, **unfiltered**. So a divergence is a **red on the first PR**, not a silent pass — which is what
  FR-010a actually prohibits. Recorded in contract §1 as a toolchain-scope statement rather than left as an
  unqualified claim.

  > **This bullet cited `tier1.yml:511-513` for both toolchains until Gate A round 2, and that cite does not
  > reach libc++.** Tier 1's matrix is `linux-clang-{debug,release,asan,ubsan,tsan}` + `linux-gcc-release`
  > (`tier1.yml:293-299`) — **there is no libc++ lane in that file**; libc++ is `tier3-libcxx.yml:174-178`
  > and MSVC is `tier2.yml:176-179`. The substance survives (tier 3 runs `ctest` unfiltered, so the witness
  > does execute there), but the argument now rests on the line that decides it. **And it holds only once
  > each of those steps carries FR-014's registration-count assertion** — which is why contract §6a
  > prescribes it for all three workflows rather than tier 1 alone: libc++, the toolchain the old cite
  > missed, is on the one workflow with no count assertion of any kind today.
- **Multi-config generators** (Visual Studio, Xcode) produce per-config target replies; the consumer sub-build
  uses **Ninja** on both platforms (`run_consumer_witness.cmake` passes `-G Ninja` explicitly), so this is not
  exercised — recorded, not closed. If the sub-build generator ever changes, this instrument must be re-measured.
- **Reply file names are hashed and unstable** (`target-<name>-<config>-<hash>.json`); the index file, or a
  glob on `target-<name>-*`, must be used rather than any hard-coded name. A hard-coded name would break at
  the next configure and read as a missing reply.
- **The query must exist before configure.** A reply is produced only if `.cmake/api/v1/query/codemodel-v2`
  was present when CMake ran. This is the single most likely way for the gate to observe nothing, which is why
  FR-005 makes a missing reply fatal rather than empty (R7 guard 2).
- **`isSystem` is uniform (`true`) in the passing state today**, so FR-003a's classification leg is a guard
  against future change, not a currently-exercised discriminator. Stated so a green run is not misread as
  evidence that classification was tested.
