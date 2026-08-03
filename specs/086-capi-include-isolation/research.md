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
which `PRIVATE` still establishes. Only *usage requirements* (include directories, compile definitions) are
withheld. This was the single highest-risk unknown — a source-less archive that stopped absorbing its objects
would ship an empty `libfixpp_capi.a` and fail every consumer at link time, silently, because the target would
still exist and still export.

**Alternatives considered**: giving `fixpp_capi` the sources directly (so no OBJECT library is involved). Rejected
— `package-layout.md` §2a already evaluated and rejected it for v1.0, at the cost of `fixpp_capi_shared`
compiling its own copy of every TU.

## R2 — Does `fixpp_capi_objects` stay in the export closure?

**Decision: yes, unchanged. The export set does not move.**

The generated `fixppTargets.cmake` declares `fixpp::capi_objects` identically in both modes — same
`OBJECT IMPORTED` kind, same `INTERFACE_INCLUDE_DIRECTORIES`. Membership is unaffected because `PRIVATE` on a
**static** library still records the dependency in `INTERFACE_LINK_LIBRARIES` (as `$<LINK_ONLY:>`, see R3),
which keeps it an export-set requirement.

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

## R5 — ⚠️ The negative witness MUST be compile-only

**Decision: the compile-must-fail assertion is built as an `OBJECT` library (or `try_compile`), never as an
executable.**

**This was found the hard way, by the research probe itself falling into it.** The first probe built an
`add_executable` and reported the umbrella row as `FAILS` — appearing to show that isolation had broken the C++
umbrella, which would have been a serious design defect. It had not. The probe's `main()` called a C-ABI
function; the umbrella does not link the C-ABI target, so the *link* stage failed while every `#include`
resolved perfectly. Rebuilt as an `OBJECT` library, the same row reports `COMPILES`.

A witness that links is a witness that can go green — or red — for reasons that have nothing to do with include
reachability. This is precisely the failure mode spec FR-008 names ("distinguish failed-because-isolation from
failed-for-any-other-reason"), and it is not hypothetical: it fired on the first attempt, in this feature,
against a design that was correct.

**Consequence for the plan**: `consumer_capi_witness` (which links, and should — it proves symbol resolution)
and the new isolation probes are **different kinds of test** and must stay separate targets.

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
exactly what they see today. The 6 of 28 `tests/capi/*.cpp` that include `<fixpp/…>` are additionally covered by
the directory-scoped `include_directories()` at `CMakeLists.txt:234`, so they have two reasons to keep working.

In-tree enforcement of the layering rule remains `tools/check_layers.py`'s job (`architecture.md`:509); this
feature does not extend it, and its `"capi"`/`"service"` rule rows are unaffected.

---

## Consolidated decisions

| # | Decision | Basis |
|---|---|---|
| D-1 | `fixpp_capi` links `fixpp_capi_objects` **`PRIVATE`**, gaining its own restricted `INSTALL_INTERFACE` | R1, R3 |
| D-2 | `src/service/CMakeLists.txt:12`'s whole-tree `INSTALL_INTERFACE` is **replaced** by the service-iface root | R3, spec FR-011d |
| D-3 | Three installed roots, **strictly additive**; no install rule gains an exclusion | R6 |
| D-4 | Isolation probes are **compile-only** targets, separate from the linking `consumer_capi_witness` | R5 |
| D-5 | `fixpp_capi_objects`, `fixpp_capi_shared` and the export-set membership are **unchanged** | R2, R7 |
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
