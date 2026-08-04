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
named headers. Were `fixpp::capi`'s narrowing to regress, its observed set would move **1 → 7** and hand a
C-ABI consumer the asio, OpenSSL, protobuf and abseil headers. The gate's signal is therefore unmissable, not
marginal.

**A discrimination this large also satisfies US2 by construction**: the observed side is non-empty in the
passing state (1 and 2 entries), so "empty" is never the expected value and can never be mistaken for a pass.
That is the structural difference from the three existing legs, which legitimately compare empty-to-empty.

---

## R4 — What exactly is the expected set, per leg?

**Decision: MEASURED, and the two legs genuinely differ — confirming the clarify decision to bind both.**

| leg | expected entries (paths relative to the staged prefix) |
|---|---|
| `fixpp::capi` | `include/capi` — **1**, `isSystem=true` |
| `fixpp::service` | `include/service-iface`, `include/capi` — **2**, both `isSystem=true`, **in that order** |

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

| target | Linux/clang/Release | MSVC/Debug | identical? |
|---|---|---|---|
| `probe_usage_requirements` | 1 — `include/capi` | 1 — `include/capi` | **yes** |
| `probe_capi_positive`, `probe_capi_negative` | 1 | 1 | **yes** |
| `probe_service_positive`, `probe_service_negative` | 2 — `service-iface`, `capi` | 2 — same, same order | **yes** |
| `probe_umbrella` | 7 — prefix + 6 third-party roots | 7 — same shape, Windows Conan cache paths | **yes** |

Four findings that matter for implementation:

1. **Entry counts, ordering and `isSystem` are identical across platforms.** The expectation is therefore
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

**Decision: three independent guards, because "the gate observed nothing" is this feature's dominant risk.**

1. **A non-empty expectation.** Unlike the three existing legs, the expected set here is non-empty (R4), so an
   empty observation cannot equal it. Emptiness fails by arithmetic, not by a special case.
2. **Reply-existence check.** The reply directory and the per-target reply must exist and parse; absence is a
   `FATAL_ERROR` naming the missing file (FR-005). A File API query that was never created yields *no reply
   at all*, which is the realistic failure and must not read as "no includes".
3. **By-name requirement.** The carrier is listed in `run_consumer_witness.cmake`'s `_required_targets`, so
   deleting it fails the build (FR-006). 086 established that "the gate can be removed without anything
   noticing" is the same defect class as "the gate cannot fail".

---

## Residual risks carried into implementation

- **Both platforms are measured** (R3 Linux/clang/Release, R6 MSVC/Debug), so FR-009 is discharged and no
  platform scope-out is required. What remains unmeasured is *other* toolchains — gcc, libc++ — which the CI
  matrix covers and which share CMake's model rather than a compiler-specific path, so the risk is low but
  **not zero and not asserted here**.
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
