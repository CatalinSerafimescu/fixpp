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
project(fixpp_consumer_witness CXX)

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
