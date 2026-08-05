# tests/consumer/CMakeLists.txt — external-consumer compile+link witness.
#
# Originally 061-slim FR-008/SC-004 (header-install witness). Converted by
# 084 T034 to consume the real CMake package.
#
# This is a STANDALONE CMake project — intentionally NOT add_subdirectory()'d
# from the main build (see run_consumer_witness.cmake, which configures/builds it
# as a separate `cmake -S/-B` invocation). Being a separate project() means it
# does NOT inherit the top-level CMakeLists.txt's directory-scoped
# include_directories(), so every path this target sees arrives through the
# imported targets — a missing or broken install rule fails this configure rather
# than silently falling back to a build-tree-private path.
#
# 084: fixpp NOW HAS a CMake package-config export. The whole contract is
#
#     find_package(fixpp REQUIRED)
#     target_link_libraries(app PRIVATE fixpp::fixpp)
#
# and this file is the first thing that proves it. Everything the pre-084 version
# hand-rolled is GONE, deliberately:
#   * the explicit ${FIXPP_STAGE_PREFIX}/include include path  -> arrives via the
#     umbrella's $<INSTALL_INTERFACE:>
#   * the four hand-listed build-tree archives (FIXPP_BUILD_LIB_DIR)  -> the
#     export closure supplies them, from the INSTALLED prefix
#   * -Wl,--start-group / --end-group  -> CMake repeats archives itself when
#     ordering exported TARGETS. The hand-rolling existed because raw archive
#     paths carry no dependency graph to repeat from; two deliberate static
#     archive cycles do exist (wire <-> dictionary, dictionary -> bridge ->
#     dictionary) and the exported graph expresses them.
#   * the standalone find_package(pugixml)  -> find_dependency inside
#     fixppConfig.cmake, derived from the target graph
#
# ⚠️ SCOPE (spec Assumption 7): this tier is green under the PRODUCING BUILD'S
# environment only. run_consumer_witness.cmake hands it the producer's Conan
# toolchain, so it CANNOT fail on a dependency a third-party consumer would have
# to supply for itself. That property is SC-016's (tests/packaging/), not this
# one's — do not cite this witness as evidence the package is consumable off the
# producing host.
cmake_minimum_required(VERSION 3.28)
# 086 T008: C is enabled so the INSTALLED C-ABI interface can be witnessed from a
# C compiler (probe_capi_positive_c.c). US1 promises "a C or C++ integrator";
# in-tree C-cleanliness was already pinned (tests/capi/CMakeLists.txt:13, :23) but
# that says nothing about what the package delivers.
project(fixpp_consumer_witness C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT FIXPP_STAGE_PREFIX)
  message(FATAL_ERROR "FIXPP_STAGE_PREFIX (staged `cmake --install` prefix) must be set")
endif()

# The ONLY hint given. No include directory, no library directory, no archive
# name, no third-party find_package(): SC-001 requires zero manually specified
# include or library paths, so anything beyond this line would weaken the claim.
list(PREPEND CMAKE_PREFIX_PATH "${FIXPP_STAGE_PREFIX}")

find_package(fixpp REQUIRED)

add_executable(consumer_witness consumer_witness.cpp)
target_link_libraries(consumer_witness PRIVATE fixpp::fixpp)

# ── T034: a SECOND, SEPARATE target exercising a BY-NAME export member ───────
# Every other witness in this feature links only the umbrella or the session
# stack, so without this no witness would exercise a by-name member's INSTALLED
# imported name at all, and T022's EXPORT_NAME sweep would be proven only
# statically. This target links fixpp::capi — a member the umbrella deliberately
# does not reach.
#
# ⚠️ It MUST be a separate target, never merged with consumer_witness above:
# linking fixpp::capi and fixpp::fixpp into one target is the combination
# Article IV §2 / architecture.md §7.4 rejects.
#
# NOTHING MECHANICALLY ENFORCES THAT (corrected by 086/FR-014 — this comment used
# to say tools/check_layers.py did). That script is a source #include-edge lint
# over src/** and bindings/** (:2-7, :173-176); it parses no CMake and reads no
# link interface, and an installed package cannot observe which targets a
# consumer links together anyway. Keeping these two witnesses separate IS the
# convention being upheld, which is why the separation is load-bearing here
# rather than stylistic.
#
# Building and linking IS the assertion — it need not run. The existing harness
# registers ONE add_test that configures and builds this whole sub-project, so
# this target is covered with no new add_test and no edit to
# run_consumer_witness.cmake.
add_executable(consumer_capi_witness consumer_capi_witness.cpp)
target_link_libraries(consumer_capi_witness PRIVATE fixpp::capi)

# ═════════════════════════════════════════════════════════════════════════════
# 086-capi-include-isolation — the §1 reachability matrix
# (specs/086-capi-include-isolation/contracts/include-interface.md)
#
# Three rules decide whether any of this is worth anything. All three are
# structural, and all three were arrived at by measurement, not by taste:
#
#  1. A "MUST NOT resolve" cell is asserted by COMPILATION, never by a build that
#     links. research.md R5 measured a false negative on the umbrella row caused
#     by an unresolved symbol while every #include resolved correctly.
#  2. A "MUST NOT resolve" cell is asserted by BUILDING, because the assertion is
#     inverted. It used to be that such a cell could not be a build target at all:
#     the driver raises FATAL_ERROR on ANY non-zero build exit, so a probe that
#     had to FAIL to build would red the whole witness. Since Gate B r2 the ❌
#     cells use `__has_include` + a unique-token `#error` and must COMPILE, so an
#     ordinary OBJECT-library target expresses them exactly, and the driver's
#     existing build check IS the assertion. (They were configure-time
#     `try_compile` calls until Gate B r3, when MSVC Debug proved that form
#     cannot see the Conan closure — see the ❌ block below.)
#  3. A passing ✅ cell NEVER establishes a ❌ cell. Under the additive layout
#     <fix/c_api.h> resolves from EITHER root, so a green positive probe is
#     exactly as consistent with the defect being present as with it being fixed.
#     Evidence is the PAIR, from this one configured consumer (FR-008a) — which
#     is why both halves live in this file and not in two projects.
# ═════════════════════════════════════════════════════════════════════════════

# ── ✅ cells — ordinary compile-only OBJECT libraries ────────────────────────
# OBJECT, not add_executable: no link stage, so rule 1 holds for these too and a
# green cannot be manufactured by a symbol that happens to resolve.
add_library(probe_capi_positive OBJECT probe_capi_positive.cpp)
target_link_libraries(probe_capi_positive PRIVATE fixpp::capi)

add_library(probe_capi_positive_c OBJECT probe_capi_positive_c.c)
target_link_libraries(probe_capi_positive_c PRIVATE fixpp::capi)
set_target_properties(probe_capi_positive_c PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

# US6 / FR-011a — fixpp::service reaches the plugin header AND the C ABI. The
# C-ABI half is the load-bearing one: fixpp::service declares no C-ABI root of
# its own and reaches <fix/c_api.h> only through its existing link to
# fixpp::capi (src/service/CMakeLists.txt:30). Narrowing fixpp::capi is exactly
# the change that could sever that path.
add_library(probe_service_positive OBJECT probe_service_positive.cpp)
target_link_libraries(probe_service_positive PRIVATE fixpp::service)

# US3 / FR-004 / FR-011c — the umbrella still reaches EVERYTHING, including both
# headers this feature republishes at isolated roots. The regression guard for
# the whole feature: the isolation is delivered by ADDING roots, never by
# removing anything from the umbrella's, and this is what goes red if that stops
# being true.
#
# A SEPARATE TU, deliberately: consumer_witness.cpp:34-37 includes neither of
# those two headers, and SC-003 trades on that file staying byte-unchanged.
add_library(probe_umbrella OBJECT probe_umbrella.cpp)
target_link_libraries(probe_umbrella PRIVATE fixpp::fixpp)

# ── ❌ cells — ORDINARY BUILD TARGETS, asserted to COMPILE ───────────────────
#
# These were configure-time `try_compile` calls until Gate B r3 (CI). Two changes
# made that both unnecessary and actively wrong:
#
# 1. **The inversion removed the reason for try_compile.** A ❌ cell could not be
#    a build target while it had to FAIL to build — the driver FATAL_ERRORs on any
#    non-zero build exit, so a must-fail target reds the whole witness. Since
#    Gate B r2 the probes assert the opposite: `__has_include` + a unique-token
#    `#error`, which must COMPILE. A target that must build is exactly what this
#    harness expresses natively, and the driver's existing build check IS the
#    assertion.
#
# 2. **try_compile could not see the Conan closure — MEASURED on CI.** MSVC Debug
#    failed with:
#
#        The link interface of target "opentelemetry-cpp::exporter_prometheus"
#        contains: CONAN_LIB::…_exporter_prometheus_…_DEBUG
#        but the target was not found.
#
#    `try_compile(... LINK_LIBRARIES fixpp::capi)` exports the imported-target
#    closure into CMake's scratch project, which never runs the Conan machinery
#    that defines those `CONAN_LIB::…_DEBUG` targets. So the probe failed for a
#    reason with NOTHING to do with include reachability — the BROKEN category —
#    on every MSVC Debug run. As ordinary targets in THIS project they build
#    against the fully-resolved closure and the problem disappears.
#
#    Worth recording: under the pre-r2 polarity this would have been a SILENT
#    FALSE GREEN. The probe would have "correctly failed to compile" and reported
#    the isolation working. The inversion is what turned it into a visible,
#    correctly-classified failure.
#
# The three outcomes are still distinguishable, now through the build:
#   builds                          -> header unreachable. The pass.
#   fails w/ FIXPP_086_FORBIDDEN_…  -> ISOLATION BREACH (the #error names itself).
#   fails otherwise                 -> a broken probe/toolchain, equally red.
# In all failing cases the driver surfaces the compiler output verbatim, so the
# category is readable from the log rather than inferred.
add_library(probe_capi_negative OBJECT probe_capi_negative.cpp)
target_link_libraries(probe_capi_negative PRIVATE fixpp::capi)

add_library(probe_capi_negative_service OBJECT probe_capi_negative_service.cpp)
target_link_libraries(probe_capi_negative_service PRIVATE fixpp::capi)

add_library(probe_service_negative OBJECT probe_service_negative.cpp)
target_link_libraries(probe_service_negative PRIVATE fixpp::service)
# ── C-3 leg 3 — effective usage requirements at a real consumer target ───────
# FR-009a(ii). Not a duplicate of quickstart §3's property diff: compile
# definitions reach a C-ABI consumer through fixpp_capi_objects -> fixpp_log
# (src/capi/CMakeLists.txt:29-38) and never appear in fixpp::capi's own exported
# block, which reads IDENTICALLY whether they propagate or not. Leg 2 is
# structurally incapable of seeing them; this target is where they show up.
#
# file(GENERATE) alone asserts NOTHING — it only writes. The compare lives in
# run_consumer_witness.cmake, after the sub-build.
add_library(probe_usage_requirements OBJECT probe_usage_requirements.cpp)
target_link_libraries(probe_usage_requirements PRIVATE fixpp::capi)

# The RIGHT-HAND SIDE of the comparison, as a literal WITH AN ORIGIN. FR-009a(ii)
# requires the expected set to have a named producer in the tree rather than
# being derived from the run it checks — a comparison whose expected value comes
# out of the run is satisfied by whatever the run produced, which is the same
# no-op-gate shape as a file(GENERATE) nothing reads.
#
# Empty, all three, and that is a CLOSED expectation, not a shrug: `fixpp::capi`
# carries $<LINK_ONLY:fixpp::capi_objects> (contract §2) and $<LINK_ONLY:>
# withholds COMPILE_DEFINITIONS, COMPILE_OPTIONS, COMPILE_FEATURES and
# SYSTEM_INCLUDE_DIRECTORIES along with the include path. Naming an expected
# non-empty list instead would have to enumerate what survives, and nothing does.
# What is thereby lost is enumerated and pre-approved: today AT LEAST
# FIXPP_LOG_MIN_LEVEL (src/log/CMakeLists.txt:27) and ASIO_STANDALONE (carried by
# asio::asio, linked unwrapped inside the closure). Membership in that set is
# decided by this predicate, not by that list.
set(FIXPP_086_EXPECTED_COMPILE_DEFINITIONS "")
set(FIXPP_086_EXPECTED_COMPILE_OPTIONS "")
set(FIXPP_086_EXPECTED_COMPILE_FEATURES "")

# SEPARATOR NORMALISATION — both sides must be joined the same way or the compare
# fails for a reason that has nothing to do with the interface. The OBSERVED side
# is `$<JOIN:…,|>`; a bare `${VAR}` expansion of a CMake list uses `;`. Both are
# empty today so `STREQUAL` holds either way, but the moment anyone sets a
# non-empty expectation — the one edit this instrument invites — `A;B` vs `A|B`
# would fail permanently and look like a real finding.
foreach(_p COMPILE_DEFINITIONS COMPILE_OPTIONS COMPILE_FEATURES)
  string(REPLACE ";" "|" FIXPP_086_EXPECTED_${_p} "${FIXPP_086_EXPECTED_${_p}}")
endforeach()

# Both sides go into the file so the driver compares two values it was GIVEN,
# neither of which it invents.
file(GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/usage-requirements.txt"
  CONTENT
"EXPECTED_COMPILE_DEFINITIONS=${FIXPP_086_EXPECTED_COMPILE_DEFINITIONS}
OBSERVED_COMPILE_DEFINITIONS=$<JOIN:$<TARGET_PROPERTY:probe_usage_requirements,COMPILE_DEFINITIONS>,|>
EXPECTED_COMPILE_OPTIONS=${FIXPP_086_EXPECTED_COMPILE_OPTIONS}
OBSERVED_COMPILE_OPTIONS=$<JOIN:$<TARGET_PROPERTY:probe_usage_requirements,COMPILE_OPTIONS>,|>
EXPECTED_COMPILE_FEATURES=${FIXPP_086_EXPECTED_COMPILE_FEATURES}
OBSERVED_COMPILE_FEATURES=$<JOIN:$<TARGET_PROPERTY:probe_usage_requirements,COMPILE_FEATURES>,|>
")

# ═════════════════════════════════════════════════════════════════════════════
# 087-system-include-binding — the installed system-include interface
# (specs/087-system-include-binding/contracts/system-include-interface.md)
#
# T010: the carrier target. A NEW, uniquely named target — never one of 086's
# (contract C-6.2) — whose build command invokes the standalone comparator
# (compare_system_includes.cmake, T008/T009) once per leg, `capi` THEN
# `service`, then `leg-set` over the two collected result files. The
# capi-before-service ordering is load-bearing, not incidental (C-6.2): it is
# what makes §5 row #8's same-run capi evidence a property of THIS gate's
# output rather than of a demonstrator's follow-up read — a `COMMAND` list
# short-circuits on the first non-zero exit, so if `service` ran first a red
# there would leave no capi-leg result behind at all.
#
# ⚠️ No expectation is declared yet (T013 comes next). FIXPP_087_EXPECTED_CAPI
# and FIXPP_087_EXPECTED_SERVICE are empty on purpose: contract §3 C-2 / C-6.4
# requires `compare` to reject an empty expectation as LEG_ERROR at
# argument-validation time, before any reply is even located — so until T013
# fills these in, the `capi` leg (which runs first) reds LEG_ERROR and the
# build stops there. That is Phase 2's required end state.
set(FIXPP_087_EXPECTED_CAPI "")
set(FIXPP_087_EXPECTED_SERVICE "")

# The codemodel-v2 reply directory for THIS sub-build. Contract §2a: the query
# file is written by run_consumer_witness.cmake before this project is
# configured, so by build time (when this target's COMMANDs run) the reply
# already exists, produced by the same configure that defined every target
# below.
set(_fixpp_087_reply_dir "${CMAKE_BINARY_DIR}/.cmake/api/v1/reply")

# Contract C-6.1: these MUST lie under CMAKE_BINARY_DIR as THIS file sees it —
# i.e. inside the `-B` tree run_consumer_witness.cmake wipes at the START of
# every run — so leg-set's "exactly two" is an assertion about THIS run.
set(_fixpp_087_result_capi    "${CMAKE_BINARY_DIR}/fixpp-087-result-capi.txt")
set(_fixpp_087_result_service "${CMAKE_BINARY_DIR}/fixpp-087-result-service.txt")

add_custom_target(probe_system_include_contract
  COMMAND "${CMAKE_COMMAND}"
    -DFIXPP_087_MODE=compare
    "-DFIXPP_087_REPLY_DIR=${_fixpp_087_reply_dir}"
    -DFIXPP_087_LEG=capi
    "-DFIXPP_087_INSTALL_PREFIX=${FIXPP_STAGE_PREFIX}"
    "-DFIXPP_087_EXPECTATION=${FIXPP_087_EXPECTED_CAPI}"
    "-DFIXPP_087_RESULT_FILE=${_fixpp_087_result_capi}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_system_includes.cmake"
  COMMAND "${CMAKE_COMMAND}"
    -DFIXPP_087_MODE=compare
    "-DFIXPP_087_REPLY_DIR=${_fixpp_087_reply_dir}"
    -DFIXPP_087_LEG=service
    "-DFIXPP_087_INSTALL_PREFIX=${FIXPP_STAGE_PREFIX}"
    "-DFIXPP_087_EXPECTATION=${FIXPP_087_EXPECTED_SERVICE}"
    "-DFIXPP_087_RESULT_FILE=${_fixpp_087_result_service}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_system_includes.cmake"
  COMMAND "${CMAKE_COMMAND}"
    -DFIXPP_087_MODE=leg-set
    "-DFIXPP_087_RESULT_FILES=${_fixpp_087_result_capi};${_fixpp_087_result_service}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_system_includes.cmake"
  VERBATIM
  COMMENT "087: comparing the installed system-include interface (capi, then service)"
)
