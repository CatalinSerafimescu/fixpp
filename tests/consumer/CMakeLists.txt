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
# Article IV §2 / architecture.md:509 rejects, and tools/check_layers.py enforces.
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
#  2. A "MUST NOT resolve" cell CANNOT be a build target here. The driver
#     run_consumer_witness.cmake:96-104 raises FATAL_ERROR on ANY non-zero build
#     exit, so a must-fail target reds the whole witness. The ❌ cells are
#     configure-time try_compile() asserted FALSE (contract §4a, measured in R9);
#     a TRUE result fails this configure, which the driver reports at :91-92.
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
# fixpp::capi (src/service/CMakeLists.txt:16). Narrowing fixpp::capi is exactly
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

# ── ❌ cells — configure-time try_compile(), asserted FALSE ──────────────────
#
# fixpp086_probe_unreachable(<label> <source> <link-target>)
#   Records the result in FIXPP_086_LEAKS (parent scope) instead of raising
#   immediately, so ONE configure reports EVERY leaked cell rather than only the
#   first. A run that stops at the first leak understates the defect and makes
#   the FR-007 red observation incomplete.
#
# NO_CACHE is load-bearing: try_compile normally writes its result to the CMake
# cache and never re-runs. The driver wipes the sub-build every run
# (run_consumer_witness.cmake:46) so it would not bite there, but a hand-driven
# re-configure after applying the isolation would silently replay a stale TRUE.
set(FIXPP_086_LEAKS "")
function(fixpp086_probe_unreachable _label _source _link)
  try_compile(_probe_ok
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_source}"
    LINK_LIBRARIES ${_link}
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    NO_CACHE
    OUTPUT_VARIABLE _probe_out)
  if(_probe_ok)
    set(_leaks "${FIXPP_086_LEAKS}")
    list(APPEND _leaks "${_label}")   # list(APPEND) so an empty start does not
    set(FIXPP_086_LEAKS "${_leaks}" PARENT_SCOPE)   # yield a leading empty element
    message(STATUS "086 probe LEAK: ${_label} — ${_source} COMPILED against ${_link}")
  else()
    message(STATUS "086 probe ok:   ${_label} — ${_source} correctly unreachable from ${_link}")
  endif()
  set(FIXPP_086_PROBE_LOG "${FIXPP_086_PROBE_LOG}${_label}: compiled=${_probe_ok}\n" PARENT_SCOPE)
endfunction()

# CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY is what makes the probes
# genuinely compile-only: no link stage, and no main() required (contract §4a,
# measured in research.md R9). Saved and restored around the block so it cannot
# leak into a later check_* module in this scope.
if(DEFINED CMAKE_TRY_COMPILE_TARGET_TYPE)
  set(_fixpp086_saved_ttype "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  set(_fixpp086_had_ttype TRUE)
else()
  set(_fixpp086_had_ttype FALSE)
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(FIXPP_086_PROBE_LOG "")

# <fixpp/wire/parser.hpp> — a SHIPPED public engine header. The probe header must
# be one whose own disappearance would itself be a defect (FR-008), or the
# assertion could pass for a reason unrelated to isolation.
fixpp086_probe_unreachable("capi->engine-header"
  probe_capi_negative.cpp fixpp::capi)

# A DISTINCT matrix cell, not a variant of the one above: the service plugin
# header is the one C++ header this feature republishes at a SECOND installed
# root, so a mis-wired fixpp::capi that picked up include/service-iface would
# leak this cell while the engine-header probe still passed.
fixpp086_probe_unreachable("capi->service-header"
  probe_capi_negative_service.cpp fixpp::capi)

# US6 / FR-011b — the SAME cell for the service target. [arch §8]: the service
# consumes the engine through the C ABI only.
#
# This one has its OWN demonstrated-red obligation and its own revert
# (src/service/CMakeLists.txt:12 alone, FR-011e). The asymmetry is DIRECTIONAL:
# fixpp_service links fixpp_capi (src/service/CMakeLists.txt:16), so reverting
# the C-ABI leg reds this probe too and cannot stand in for the service one.
fixpp086_probe_unreachable("service->engine-header"
  probe_service_negative.cpp fixpp::service)

if(_fixpp086_had_ttype)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_fixpp086_saved_ttype}")
else()
  unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
endif()

# Durable even when the configure aborts below — this file IS the FR-007 red
# observation when the isolation is absent or reverted.
file(WRITE "${CMAKE_BINARY_DIR}/probe-results.txt" "${FIXPP_086_PROBE_LOG}")

if(FIXPP_086_LEAKS)
  string(REPLACE ";" "\n  " _leaks_pretty "${FIXPP_086_LEAKS}")
  message(FATAL_ERROR
    "086 include isolation NOT delivered — these matrix cells are reachable and must not be:\n"
    "  ${_leaks_pretty}\n"
    "See specs/086-capi-include-isolation/contracts/include-interface.md §1.")
endif()

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
