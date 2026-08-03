# Research — 086 C-ABI include isolation

**Date**: 2026-08-03 · **Branch**: `086-capi-include-isolation` · **CMake**: 3.30.0

Every finding below is **measured**, not read off `target_link_libraries`. That method is the one
`specs/084-packaging-cpack-export/contracts/package-layout.md` §2a records as having been wrong in three
places across a three-level cascade, and the spec's FR-016 forbids relying on it here.

## Method

A standalone CMake project reproducing fixpp's exact target shape — a source-less `STATIC` target reaching an
`OBJECT` library that carries the whole-tree include interface, plus an `INTERFACE` service target and an
`INTERFACE` umbrella, all in one `install(EXPORT)` — built and installed **twice**, with `-DFIXPP_ISO=OFF`
(status quo) and `-DFIXPP_ISO=ON` (proposed design). Consumers were then configured against each staged
prefix through `find_package`.

Reproducing it needs only the fixture below; it does not build fixpp.

```cmake
# the two lines under test, ISO=ON
target_link_libraries(fixpp_capi PRIVATE fixpp_capi_objects)          # was PUBLIC
target_include_directories(fixpp_capi PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"                    # in-tree: unchanged, permissive
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/capi>")            # installed: isolated
target_include_directories(fixpp_service INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/service-iface>")   # replaces src/service/CMakeLists.txt:12
install(DIRECTORY include/fix/           DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/capi/fix")
install(DIRECTORY include/fixpp/service/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/service-iface/fixpp/service")
# install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}") — UNCHANGED, no exclusions added
```

---

## R1 — Does a source-less `STATIC` target still absorb the objects under `PRIVATE`?

**Decision: yes. `PRIVATE` is safe; the archive is byte-for-byte equivalent in content.**

```
ISO=OFF  libfixpp_capi.a : capi.cpp.o
ISO=ON   libfixpp_capi.a : capi.cpp.o
```

**Rationale**: object absorption into a static archive follows the `TARGET_OBJECTS` link-library relationship,
which `PRIVATE` still establishes. Only *usage requirements* are withheld — and that set is wider than include
directories: compile definitions, compile options, compile features and system include directories go with
them (see R8's amendment and FR-009a). This was the single highest-risk unknown — a source-less archive that stopped absorbing its objects
would ship an empty `libfixpp_capi.a` and fail every consumer at link time, silently, because the target would
still exist and still export.

**Alternatives considered**: giving `fixpp_capi` the sources directly (so no OBJECT library is involved). Rejected
— `package-layout.md` §2a already evaluated and rejected it for v1.0, at the cost of `fixpp_capi_shared`
compiling its own copy of every TU.

## R2 — Does `fixpp_capi_objects` stay in the export closure?

**Decision: yes, unchanged. The export set does not move.**

The generated `fixppTargets.cmake` declares `fixpp::capi_objects` identically in both modes — same
`OBJECT IMPORTED` kind, same `INTERFACE_INCLUDE_DIRECTORIES`.

**Correction to the stated mechanism (Gate A r1).** The fixture's reasoning was that `PRIVATE` on a *static*
library still records the dependency in `INTERFACE_LINK_LIBRARIES` as `$<LINK_ONLY:>`, keeping it an export-set
*requirement* — true, but **not** what holds membership in the real tree. In fixpp, `fixpp_capi_objects` is
listed **by name** in `FIXPP_EXPORT_TARGETS` (`CMakeLists.txt:562`), which drives
`install(TARGETS ${FIXPP_EXPORT_TARGETS} EXPORT fixppTargets)` (`:733`). Membership is therefore by
**enumeration**, and is stable independently of any closure inference — the same conclusion 084 already
recorded (`package-layout.md` §2a `:132`: *"demoting the edge to `PRIVATE` does not escape it, because
`$<LINK_ONLY:>` entries are export requirements too"*), reached by a mechanism that does not depend on the
keyword at all. R2's conclusion stands on the stronger ground.

**Why this matters**: `package-layout.md` §2a records that the shipped `lib/objects-<CONFIG>/**` files are
checked by `_cmake_import_check_files_for_fixpp::capi_objects`, making their absence a configure-time
`FATAL_ERROR` for **every** consumer. Had `PRIVATE` dropped the target from the closure, the check and the
files would have to be removed together or the package would break for everyone. It does not, so **no part of
the 084 packaging arrangement has to change**, and spec FR-016's re-measurement obligation is expected to
confirm the same 18 members rather than a new count.

## R3 — What the generated targets file actually becomes

**Decision: the delivered shape is exactly the one §7.4:503 describes in intent.**

Measured diff, `stage-OFF` → `stage-ON`:

```diff
 set_target_properties(fixpp::capi PROPERTIES
-  INTERFACE_LINK_LIBRARIES "fixpp::capi_objects"
+  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include/capi"
+  INTERFACE_LINK_LIBRARIES "\$<LINK_ONLY:fixpp::capi_objects>"
 )
 set_target_properties(fixpp::service PROPERTIES
-  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"
+  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include/service-iface"
   INTERFACE_LINK_LIBRARIES "fixpp::capi"
 )
```

`$<LINK_ONLY:>` is the whole mechanism: the consumer still links the objects target, and therefore still
resolves every symbol, but does not inherit its include directories. The `-` line is the defect #218 reports,
verbatim.

> ### The generated text, pasted verbatim — every extraction command is derived from THIS, not from a description of it *(Gate A r2)*
>
> Re-run on this host, CMake 3.30.0, `cmake --install` of the fixture at both stages. `fixpp::capi`'s region of
> `lib/cmake/fixpp/fixppTargets.cmake`, byte for byte, at ISO=ON (line numbers from the generated file):
>
> ```
> 58|# Create imported target fixpp::capi
> 59|add_library(fixpp::capi STATIC IMPORTED)
> 60|
> 61|set_target_properties(fixpp::capi PROPERTIES
> 62|  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include/capi"
> 63|  INTERFACE_LINK_LIBRARIES "\$<LINK_ONLY:fixpp::capi_objects>"
> 64|)
> 65|
> 66|# Create imported target fixpp::capi_objects
> ```
>
> **Line 60 is blank.** CMake emits a blank line between `add_library()` and `set_target_properties()`, and
> another after the closing `)`. Two consequences that a description of the file cannot show, and that
> `quickstart.md` §3/§5 are now written against:
>
> 1. Any extraction terminated on `/^$/` from the `# Create imported target` comment captures **only the
>    comment and the `add_library` line** — which are byte-identical at both stages, so a `diff` of the two
>    captures exits **0 whatever the isolation did**. Measured: the blank-line-terminated `awk` this bundle
>    carried until Gate A r2 produced identical two-line captures at OFF and ON and `diff` returned rc=0.
> 2. `grep -A4` on the comment reaches line 62 and stops — the `$<LINK_ONLY:…>` line at 63 is **outside** the
>    window. `-A7` reaches the closing `)`. Measured both ways.
>
> The extraction that does span the property block, and that excludes `fixpp::capi_objects` by the trailing
> ` PROPERTIES$` anchor:
>
> ```bash
> awk '/^set_target_properties\(fixpp::capi PROPERTIES$/,/^\)$/'
> ```
>
> Measured output — OFF: `set_target_properties(…` + `INTERFACE_LINK_LIBRARIES "fixpp::capi_objects"` + `)`.
> ON: the four lines 61-64 above. `diff` of the two returns rc=1 with the delta this section already records.
> Do **not** substitute a range that ends on the `capi_objects` comment: nothing guarantees CMake emits the two
> targets adjacently.

Note `fixpp::service` keeps `INTERFACE_LINK_LIBRARIES "fixpp::capi"` — which is how it reaches the C-ABI
headers after isolation, satisfying FR-011a with no extra declaration.

## R4 — End-to-end reachability, measured through `find_package`

**Decision: 7/7 as specified.** Compile-only probes (see R5), consumer configured against the staged prefix
with `find_package(fixpp REQUIRED)` and one `target_link_libraries` line as its only hint.

| Mode | Target linked | Header | Result |
|---|---|---|---|
| **OFF** | `fixpp::capi` | `<fixpp/wire/parser.hpp>` | **COMPILES** ← the defect, reproduced |
| ON | `fixpp::capi` | `<fix/c_api.h>` | COMPILES |
| ON | `fixpp::capi` | `<fixpp/wire/parser.hpp>` | **FAILS** ← FR-003 delivered |
| ON | `fixpp::capi` | `<fixpp/service/control_plane_factory.hpp>` | FAILS |
| ON | `fixpp::service` | `<fixpp/service/…>` + `<fix/c_api.h>` | COMPILES ← FR-011a |
| ON | `fixpp::service` | `<fixpp/wire/parser.hpp>` | **FAILS** ← FR-011b delivered |
| ON | `fixpp::fixpp` | all three | COMPILES ← FR-004 / FR-011c preserved |

Row 1 is the **demonstrated-red** evidence FR-007 requires, obtained at design time: the assertion the witness
will make is already known to fail against the status quo. It still has to be re-demonstrated against the real
witness in the real tree at `/speckit-implement`; this establishes that the assertion *can* discriminate, not
that the shipped witness does.

## R5 — ⚠️ The negative witness MUST be compile-only **and cannot be a build target**

**Decision: the compile-must-fail assertion is a configure-time `try_compile` asserted FALSE, never an
executable and never a build target.**

> **Amended at Gate A round 1.** The original decision read "an `OBJECT` library (or `try_compile`)". The
> `OBJECT`-library leg is **not implementable in the tier this feature extends**, and stating it would have
> produced an unbuildable `/speckit-tasks` task. `tests/consumer/` is a standalone sub-project whose driver runs
> one `cmake --build` and raises `message(FATAL_ERROR "consumer build failed")` on **any** non-zero build exit
> (`tests/consumer/run_consumer_witness.cmake:96-104`) — so a probe target that is *required* to fail reds the
> entire witness. The assertion cannot be expressed as a target there at all.
>
> **What the mechanism must do** (spec FR-006a): evaluate the ❌ cell where the target's usage requirements
> propagate as they do to a real consumer target, compile **without linking**, and **invert** the result.
>
> **Instance**: `try_compile(<var> ... LINK_LIBRARIES fixpp::capi)` at consumer-**configure** time, with
> `CMAKE_TRY_COMPILE_TARGET_TYPE` set to `STATIC_LIBRARY` for the duration — compile-only, no link stage, no
> `main()` — and restored afterwards so it does not leak into any `check_*` module in scope. `<var>` MUST be
> FALSE; TRUE raises `FATAL_ERROR`, failing the *configure* step, which the driver catches at `:91-92`. Both
> the ✅ and ❌ cells then live in **one configured consumer**, which is what FR-008a's paired-evidence rule
> needs.
>
> **Fallback**: if the FR-007 demonstrated-red observation does **not** go red under a reverted isolation, the
> `try_compile` context is not faithfully carrying the usage requirements — fall back to a dedicated probe
> sub-project with its own `cmake -P` driver asserting a **non-zero** build result, mirroring the existing
> `execute_process` + `RESULT_VARIABLE` shape at `run_consumer_witness.cmake:96-104`. FR-007 is what decides
> between the two; it is not assumed.
>
> ✅ cells are unaffected: ordinary compile-only `OBJECT` library targets in the same sub-project, where a
> build failure reds the witness — the correct polarity, no inversion needed.

The finding that produced the compile-only rule in the first place stands as recorded below.

**This was found the hard way, by the research probe itself falling into it.** The first probe built an
`add_executable` and reported the umbrella row as `FAILS` — appearing to show that isolation had broken the C++
umbrella, which would have been a serious design defect. It had not. The probe's `main()` called a C-ABI
function; the umbrella does not link the C-ABI target, so the *link* stage failed while every `#include`
resolved perfectly. Rebuilt as an `OBJECT` library, the same row reports `COMPILES`.

A witness that links is a witness that can go green — or red — for reasons that have nothing to do with include
reachability. This is precisely the failure mode spec FR-008 names ("distinguish failed-because-isolation from
failed-for-any-other-reason"), and it is not hypothetical: it fired on the first attempt, in this feature,
against a design that was correct.

**Consequence for the plan**: `consumer_capi_witness` (which **links**, proving symbol resolution) and the new
isolation probes are **different kinds of test** and must stay separate.

> **Corrected at Gate A r2 — this witness is NEVER RUN.** An earlier revision of this paragraph asserted that
> `consumer_capi_witness` "is executed and its output asserted (`run_consumer_witness.cmake` step 4)". Read from
> the file, that is false: step 4 sets `set(_exe "${_sub_build}/consumer_witness")`
> (`run_consumer_witness.cmake:110`) — the **umbrella** witness — and asserts `^PASS:` on that one binary
> (`:142-143`). `consumer_capi_witness` is covered by the single `cmake --build` at `:96-104` and by nothing
> else, exactly as `tests/consumer/CMakeLists.txt:71` states: *"Building and linking IS the assertion — it need
> not run."* FR-009's strengthening therefore rests on the **link** stage: the added reference must pull the
> entry point's object out of the archive when `consumer_capi_witness` is linked (taking its address does),
> and **no runtime behaviour is asserted**. The "no call whose runtime failure would red it" proviso was
> derived from the false premise; it is over-restrictive rather than wrong, and is dropped.

## R6 — The additive layout is a strict superset

**Decision: confirmed by manifest comparison, per SC-003a.**

```
OFF:  include/fix/c_api.h
      include/fixpp/service/control_plane_factory.hpp
      include/fixpp/wire/parser.hpp
ON:   include/capi/fix/c_api.h                              ← added
      include/fix/c_api.h
      include/fixpp/service/control_plane_factory.hpp
      include/fixpp/wire/parser.hpp
      include/service-iface/fixpp/service/…                 ← added
comm -23 OFF ON  →  (empty)   # nothing in OFF is missing from ON
```

No install rule acquired an exclusion; `install(DIRECTORY include/ …)` is untouched. FR-005a holds by
construction, and the check that proves it compares **produced manifests**, not install rules.

## R7 — `fixpp_capi_shared` needs no change

**Decision: leave it exactly as it is.**

It links `fixpp_capi_objects` `PUBLIC` (`src/capi/CMakeLists.txt:50`) — a second propagation path the spec
flags. But it is gated on `FIXPP_BUILD_TESTS`, exists solely for the Python ctypes oracle, and is **not an
export member** (verified: zero occurrences of `fixpp_capi_shared` in the root `CMakeLists.txt`, so it is
absent from `FIXPP_EXPORT_TARGETS`). Nothing it propagates reaches an installed consumer.

## R8 — In-tree behaviour is preserved by construction

**Decision: `BUILD_INTERFACE` stays permissive; only `INSTALL_INTERFACE` is restricted.**

`fixpp_capi` and `fixpp_service` keep `$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>`, so in-tree consumers see
exactly what they see today. The **11 of 28** `tests/capi/*.cpp` whose compilation depends on the `-I` search
for a `fixpp/` header — **6** spelling it `<fixpp/…>`, **5** only `"fixpp/…"`, and `tests/capi/` holds no
`fixpp/` subdirectory for the quoted form to find — are additionally covered by the directory-scoped
`include_directories()` at `CMakeLists.txt:234`, so they have two reasons to keep working. *(Corrected at
Gate A r2: the earlier "6 of 28" was the angle-bracket count, which undercounts the dependent set by 5. The
argument is unaffected — `include_directories()` is directory-scoped over all 28 regardless of spelling.)*

> **⚠️ Scope of "by construction" (Gate A r1).** The argument above is stated over **include directories** and
> is therefore narrower than the change. `include_directories()` carries only include paths, but
> `$<LINK_ONLY:>` withholds `INTERFACE_COMPILE_DEFINITIONS`, `INTERFACE_COMPILE_OPTIONS`,
> `INTERFACE_COMPILE_FEATURES` and `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` as well. The closure carries at least
> one live PUBLIC compile definition: `FIXPP_LOG_MIN_LEVEL` (`src/log/CMakeLists.txt:27`, documented at
> `:24-26` as "propagated to every consumer so the `if constexpr` cutoff in the LOG macros is build-wide"),
> consumed unguarded at `include/fixpp/log/logger.hpp:275,301,333` with no `#ifndef` fallback.
> **Verified: no live break.** `include/fixpp/core/logger_fwd.hpp` is forward-declaration only, and
> `logger.hpp` is included only from `src/config/logger_resolver.*`, `src/session/engine.cpp` and
> `src/log/logger.cpp` — all in targets that link `fixpp_log` directly; no `fixpp_capi` consumer reaches it.
> The **installed** C-ABI consumer does lose the definition — measured, R10 — harmlessly today.
>
> **The remedy is a check, and at Gate A r2 it became two checks** *(the single "the OFF→ON property diff
> loses **only** `INTERFACE_INCLUDE_DIRECTORIES`" formulation this note carried before was unsatisfiable in
> both readings: R3 shows **two** direct properties moving and the include property being **gained**, not lost;
> and read as "the consumer loses only include directories" it is contradicted by the sentence directly above —
> the definition **is** lost)*. FR-009a now splits them:
>
> - **(i)** the direct-property delta of `fixpp::capi`'s generated block, as a **closed enumeration filled in
>   from the measurement** (R3), not a wildcard and not a transcribed list;
> - **(ii)** the effective usage-requirement delta **measured at the consumer**, because compile definitions
>   reach a C-ABI consumer through `fixpp_capi_objects` → `fixpp_log` (`src/capi/CMakeLists.txt:29-38`) and
>   **never through `fixpp::capi`'s own block** — which reads identically whether the definition propagates or
>   not, so (i) is structurally blind to it. Instrument and its measured discrimination: **R10**.
>
> The enumerated, presently-unreachable set — today exactly `FIXPP_LOG_MIN_LEVEL` — is **permitted** to
> disappear as a recorded consequence. In-tree behaviour is preserved by construction; the *installed*
> usage-requirement delta is measured, not assumed.

In-tree enforcement of the layering rule remains `tools/check_layers.py`'s job (`architecture.md`:509); this
feature does not extend it, and its `"capi"`/`"service"` rule rows are unaffected. Note what that job **is** —
a source include-edge lint over `src/**` and `bindings/**` (`tools/check_layers.py:2-7`, `:173-176`), with no
notion of a CMake link interface. FR-014 corrects the three sites that claim otherwise.

## R9 — Does the `try_compile` ❌ mechanism actually carry the imported target's include interface?

**Decision: yes. R5's amended mechanism is MEASURED, not merely reasoned — `try_compile` +
`LINK_LIBRARIES <imported target>` propagates `INTERFACE_INCLUDE_DIRECTORIES`, and the pair discriminates.**

*(Added at Gate A round 1. R5's mechanism was written as a property plus an instance, with FR-007 named as the
decider because the propagation was unverified. It has now been run against the same 5-target fixture the
Method section describes, already staged at ISO=OFF and ISO=ON.)*

Fixture consumer — `find_package(fixpp REQUIRED)` against each staged prefix, nothing else on the include
path:

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)   # compile-only: no main(), no link stage
try_compile(POS_OK SOURCES pos.cpp LINK_LIBRARIES fixpp::fixpp_capi CXX_STANDARD 23)  # #include <fix/c_api.h>
try_compile(NEG_OK SOURCES neg.cpp LINK_LIBRARIES fixpp::fixpp_capi CXX_STANDARD 23)  # #include <fixpp/wire/parser.hpp>
```

*(`fixpp::fixpp_capi` is the fixture's imported name for the target that corresponds to `fixpp::capi` in the
real tree.)*

| stage | `POS_OK` (`<fix/c_api.h>`) | `NEG_OK` (`<fixpp/wire/parser.hpp>`) |
|---|---|---|
| **OFF** (status quo) | TRUE | **TRUE** ← the defect, reproduced through this mechanism |
| ON (isolated) | TRUE | **FALSE** ← FR-003 delivered, observed by the mechanism §4a prescribes |

**What this establishes:**

1. **The propagation works.** `try_compile` with `LINK_LIBRARIES <imported target>` **does** propagate that
   imported target's `INTERFACE_INCLUDE_DIRECTORIES`. This was the specific unknown flagged when R5 was
   amended; it is now proven for the mechanism as written.
2. **It is genuinely compile-only.** `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` means neither probe source
   has a `main()` and both compiled with no link stage — so R5's confound (a link failure read as an include
   failure) **cannot recur** in this shape.
3. **The pair discriminates.** The OFF row shows the negative probe returning TRUE against the *unfixed*
   package, so the assertion goes red for the right reason rather than for any reason. This is design-time
   demonstrated-red evidence for the **mechanism**, parallel to R4 row 1.

**Limits — stated so this is not over-read:**

4. **Fixture-scoped.** 5 targets, no Conan toolchain, Linux/clang only. It does **not** discharge the
   real-tree obligations below, and point 3 is **not** a substitute for FR-007's obligation to observe the
   **shipped** witness red in the **real** tree. What R9 closes is "can this mechanism express the assertion at
   all", not "does the delivered witness fire".

**Consequence for the plan**: `contracts/include-interface.md` §4a's instance is now cited to R9 rather than
offered as an unverified decision rule. The `cmake -P` driver remains the named **fallback**, and FR-007
remains the decider — R9 removes the reason to expect the fallback, not the obligation to check.

## R10 — Can the usage-requirement delta be observed AT THE CONSUMER, and does the instrument discriminate?

**Decision: yes. `file(GENERATE … CONTENT "$<TARGET_PROPERTY:<probe>,COMPILE_DEFINITIONS>")` on a probe target
that links the imported C-ABI target reports the closure's PUBLIC definition at ISO=OFF and loses it at
ISO=ON — MEASURED, not reasoned.**

*(Added at Gate A round 2. FR-009a(ii) prescribes this instrument; R2-1's finding is that the targets-file diff
it replaces is **structurally blind** to compile definitions, since they travel `fixpp_capi_objects` →
`fixpp_log` (`src/capi/CMakeLists.txt:29-38`) and never appear in `fixpp::capi`'s own generated block. An
instrument named in a requirement without being run is the defect this round exists to stop, so it was run.)*

Fixture: the same 5-target project the Method section describes, with `FIXPP_LOG_MIN_LEVEL=2` added as a
`PUBLIC` compile definition on `fixpp_capi_objects` — mirroring `src/log/CMakeLists.txt:27` — staged at both
modes. A probe consumer configured against each staged prefix:

```cmake
add_library(probe_capi OBJECT p.cpp)
target_link_libraries(probe_capi PRIVATE fixpp::capi)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/probe_defs.txt"
     CONTENT "$<TARGET_PROPERTY:probe_capi,COMPILE_DEFINITIONS>\n")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/probe_incs.txt"
     CONTENT "$<TARGET_PROPERTY:probe_capi,INCLUDE_DIRECTORIES>\n")
```

| stage | probe's evaluated `COMPILE_DEFINITIONS` | probe's evaluated `INCLUDE_DIRECTORIES` |
|---|---|---|
| **OFF** (status quo) | `FIXPP_LOG_MIN_LEVEL=2` | `<prefix>/include` |
| ON (isolated) | *(empty)* | `<prefix>/include/capi` |

**What this establishes:**

1. **The property is collected transitively.** `$<TARGET_PROPERTY:…,COMPILE_DEFINITIONS>` on a consumer target
   does pick up `INTERFACE_COMPILE_DEFINITIONS` from a linked **imported** target's closure. This was the
   load-bearing unknown; the instrument is not blind.
2. **It discriminates.** The definition is present at OFF and absent at ON, so FR-009a(ii)'s check goes red for
   the right reason rather than reporting the same thing either way — unlike the targets-file diff it replaces.
3. **The loss is real, and is the one R8's amendment predicted.** An installed C-ABI consumer does stop
   receiving `FIXPP_LOG_MIN_LEVEL`. FR-009a(ii) records that as the pre-approved exception rather than
   pretending nothing is withheld.

**Limits — stated so this is not over-read:**

4. **Fixture-scoped, same as R9.** 5 targets, no Conan toolchain, Linux/clang. It establishes that the
   instrument *works*; the **expected set** it is compared against must still be enumerated from the real
   18-member closure at `/speckit-implement`, never transcribed from this table.
4a. **The probe consumer reached the imported targets by `include(fixppTargets.cmake)`, not by
   `find_package`.** Stated because the method must not read stronger than what ran: the fixture ships no
   `fixppConfig.cmake`, so `find_package(fixpp REQUIRED)` — which the real sub-project uses
   (`tests/consumer/CMakeLists.txt:55`) — could not resolve there. The generated targets file is what
   `find_package` ultimately includes, so the propagation measured above is expected to be identical; but
   "identical" is an inference here, not a measurement, and the real-tree run at `/speckit-implement` is what
   settles it.
5. **`file(GENERATE)` alone asserts nothing.** It writes at *generate* time, so no configure-time `if()` can
   read it back. FR-009a(ii) requires the read-and-compare step to be named and to run — in the driver after
   the sub-build (`run_consumer_witness.cmake`, after `:96-104`) or as a `cmake -P` build step.
6. **`INCLUDE_DIRECTORIES` is recorded here as a by-product, not as a shipped assertion.** The exact-set form
   over evaluated include directories would be a valid alternative instrument for SC-001; this feature does
   **not** adopt it — see `plan.md` → Gate A → Round 2 disagreements for why.

---

## Consolidated decisions

| # | Decision | Basis |
|---|---|---|
| D-1 | `fixpp_capi` links `fixpp_capi_objects` **`PRIVATE`**, gaining its own restricted `INSTALL_INTERFACE` | R1, R3 |
| D-2 | `src/service/CMakeLists.txt:12`'s whole-tree `INSTALL_INTERFACE` is **replaced** by the service-iface root | R3, spec FR-011d |
| D-3 | Three installed roots, **strictly additive**; no install rule gains an exclusion | R6 |
| D-4 | Isolation probes are **compile-only** and separate from the linking `consumer_capi_witness`: ✅ cells are `OBJECT` targets; ❌ cells are configure-time `try_compile` asserted FALSE, because a must-fail *target* would red the whole witness | R5, **R9** (mechanism measured) |
| D-5 | `fixpp_capi_objects`, `fixpp_capi_shared` and the export-set membership are **unchanged** — membership held by explicit enumeration at `CMakeLists.txt:562`, not by closure inference | R2, R7 |
| D-6 | `BUILD_INTERFACE` permissive, `INSTALL_INTERFACE` restricted — in-tree unaffected | R8 |

## What is NOT yet proven, and must be at `/speckit-implement`

1. **The real tree, not the repro.** Every finding here comes from a 5-target fixture. fixpp has 18 export
   members, two deliberate static-archive cycles, and a Conan toolchain. D-1 must be re-measured on a real
   configure + install (spec FR-016).
2. **The 18-member count.** R2 predicts it does not move. Predicted ≠ measured; FR-016 requires the generate run.
3. **MSVC.** Everything above is Linux. `$<LINK_ONLY:>` is generator-independent, but the `usr/`-prefix
   asymmetry means the new content assertions must be written to hold on the Windows ZIP too — and that is a
   CI-only job (`feedback_local_verify_clang_only_misses_gcc_release_ci_job`).
4. **The witness goes red in the real tree.** R4 row 1 shows the assertion discriminates in the fixture. FR-007
   demands the shipped witness be observed red against the real package.
5. **The `try_compile` mechanism in the REAL tree.** *(Narrowed — the propagation question is now CLOSED by
   **R9**: `try_compile` + `LINK_LIBRARIES <imported target>` does carry `INTERFACE_INCLUDE_DIRECTORIES`, it is
   genuinely compile-only under `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, and the pair discriminates.)*
   What remains is what R9 explicitly does not cover: the same mechanism against the **18-member tree under the
   Conan toolchain**, inside the real `tests/consumer/` sub-project rather than a fixture consumer. FR-007's
   demonstrated-red observation on the **shipped** witness is still the test; R5's named fallback (a `cmake -P`
   driver asserting a non-zero build result) remains available but is no longer expected.
6. **MSVC, for the probes specifically.** R9 is Linux/clang. `try_compile` and
   `CMAKE_TRY_COMPILE_TARGET_TYPE` are generator-independent, but nothing here has run under MSVC — see item 3.
7. **The usage-requirement delta beyond include directories** (R8's amendment / FR-009a). *(Narrowed at Gate A
   r2 — the **instrument** question is CLOSED by **R10**: a probe target's evaluated `COMPILE_DEFINITIONS`
   does collect the closure's definitions through an imported target, and it discriminates OFF vs ON. The
   earlier entry here predicted "the OFF→ON diff of `fixpp::capi` loses only `INTERFACE_INCLUDE_DIRECTORIES`",
   which R3 and R8's amendment already contradict; it is withdrawn, not merely unmeasured.)* What remains is
   the **content**, on the real tree: the enumeration of the 18-member closure's PUBLIC/INTERFACE compile
   definitions, options and features, and the measured set an installed `fixpp::capi` consumer actually
   receives against it. FR-009a(ii) owns both; neither may be transcribed from R10's fixture table.
8. **The closed enumeration of FR-009a(i).** R3 measures a two-property delta on the fixture. The real
   `fixppTargets.cmake` may carry more, and the enumeration must be filled in from *its* generate run — a
   hardcoded two-item list would be a new unsatisfiable MUST.
