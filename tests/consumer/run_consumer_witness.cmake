# tests/consumer/run_consumer_witness.cmake — 061-slim T024 (FR-008/SC-004) driver.
#
# Invoked via `cmake -P` as a ctest (see the add_test(... "fixpp::consumer::install-witness")
# registration in the top-level CMakeLists.txt). Mirrors the tests/codegen T039
# cmake -P build-graph-test pattern: no hard-coded paths, everything comes in via -D.
#
# Steps (SC-004 — external consumer TU compiles AND links against the INSTALLED
# package, no build-tree-private path):
#   1. `cmake --install` the main build into a fresh staging prefix.
#   2. Configure tests/consumer/ as a STANDALONE CMake project (own project(),
#      not add_subdirectory()'d) pointed ONLY at the staged include dir.
#   3. Build it.
#   4. Run the resulting executable against the real FIX44.xml and assert it
#      printed PASS and exited 0.
#
# Required -D inputs:
#   FIXPP_MAIN_BUILD_DIR   — the configured main build dir (build/<preset>);
#                            supplies conan_toolchain.cmake + lib/ archives.
#   FIXPP_SOURCE_DIR       — repo root; supplies dictionaries/FIX44.xml (runtime
#                            data, not a header — reading it is not a
#                            build-tree-private *header* path per SC-004).
#   FIXPP_CONSUMER_SRC_DIR — tests/consumer/ (this directory).
#   FIXPP_WITNESS_WORK_DIR — scratch dir for the staged install + consumer
#                            sub-build (created fresh; NOT the source tree).

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_SOURCE_DIR FIXPP_CONSUMER_SRC_DIR FIXPP_WITNESS_WORK_DIR
             FIXPP_CXX_COMPILER FIXPP_C_COMPILER)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_consumer_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_stage    "${FIXPP_WITNESS_WORK_DIR}/stage")
set(_sub_build "${FIXPP_WITNESS_WORK_DIR}/build")

# Build the consumer in the SAME config as the main build so the Conan-resolved
# pugixml (a private link dep of fixpp_dictionary) has an imported location for
# the sub-build's config. A Debug consumer against a Release main build leaves
# pugixml::pugixml empty -> undefined pugixml refs from xml_loader.cpp.o.
# FIXPP_BUILD_TYPE is passed by the top-level add_test; default to Debug if a
# multi-config generator left CMAKE_BUILD_TYPE empty there.
if(NOT DEFINED FIXPP_BUILD_TYPE OR FIXPP_BUILD_TYPE STREQUAL "")
  set(FIXPP_BUILD_TYPE "Debug")
endif()

file(REMOVE_RECURSE "${_stage}" "${_sub_build}")

# ── 1. Stage-install the main build ──────────────────────────────────────────
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${FIXPP_MAIN_BUILD_DIR}" --prefix "${_stage}"
  RESULT_VARIABLE _install_rc
  OUTPUT_VARIABLE _install_out
  ERROR_VARIABLE  _install_err
)
if(NOT _install_rc EQUAL 0)
  message(FATAL_ERROR "cmake --install failed (exit ${_install_rc}):\n${_install_out}\n${_install_err}")
endif()

# FR-008 sub-assertion: v42/v44/v50sp2 present, _dispatch/vt11 absent.
foreach(_ver v42 v44 v50sp2)
  if(NOT EXISTS "${_stage}/include/fixpp/${_ver}/Messages.hpp")
    message(FATAL_ERROR "Staged install missing fixpp/${_ver}/Messages.hpp — T023 install rule regressed")
  endif()
endforeach()
if(EXISTS "${_stage}/include/fixpp/_dispatch")
  message(FATAL_ERROR "Staged install leaked build-tree-private fixpp/_dispatch/ (must be excluded)")
endif()
if(EXISTS "${_stage}/include/fixpp/vt11")
  message(FATAL_ERROR "Staged install leaked fixpp/vt11/ (must be excluded per FR-008)")
endif()

# ── 2. Configure the standalone consumer project ─────────────────────────────
# Reuses the main build's Conan toolchain (resolves pugixml identically to the
# main build — no separate dependency-resolution mechanism) but points the
# consumer's ONLY include path at the staged prefix (never the build tree or
# source include/ — see tests/consumer/CMakeLists.txt).
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${FIXPP_CONSUMER_SRC_DIR}"
    -B "${_sub_build}"
    -G "Ninja"
    "-DCMAKE_TOOLCHAIN_FILE=${FIXPP_MAIN_BUILD_DIR}/conan_toolchain.cmake"
    "-DCMAKE_BUILD_TYPE=${FIXPP_BUILD_TYPE}"
    "-DCMAKE_CXX_COMPILER=${FIXPP_CXX_COMPILER}"
    "-DCMAKE_C_COMPILER=${FIXPP_C_COMPILER}"
    "-DFIXPP_STAGE_PREFIX=${_stage}"
    "-DFIXPP_BUILD_LIB_DIR=${FIXPP_MAIN_BUILD_DIR}/lib"
  RESULT_VARIABLE _cfg_rc
  OUTPUT_VARIABLE _cfg_out
  ERROR_VARIABLE  _cfg_err
)
if(NOT _cfg_rc EQUAL 0)
  message(FATAL_ERROR "consumer configure failed (exit ${_cfg_rc}):\n${_cfg_out}\n${_cfg_err}")
endif()

# ── 3. Build it ────────────────────────────────────────────────────────────
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_sub_build}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE  _build_err
)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR "consumer build failed (exit ${_build_rc}):\n${_build_out}\n${_build_err}")
endif()

# ── 4. Run it and check the output ────────────────────────────────────────────
# Ninja single-config drops the exe at the sub-build root; the basename carries a
# .exe suffix on Windows (this -P script has no CMAKE_EXECUTABLE_SUFFIX, so probe
# both) and none on UNIX.
set(_exe "${_sub_build}/consumer_witness")
if(NOT EXISTS "${_exe}" AND EXISTS "${_exe}.exe")
  set(_exe "${_exe}.exe")
endif()
if(NOT EXISTS "${_exe}")
  message(FATAL_ERROR "consumer_witness executable not found at ${_exe}")
endif()

execute_process(
  COMMAND "${_exe}" "${FIXPP_SOURCE_DIR}/dictionaries/FIX44.xml"
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _run_out
  ERROR_VARIABLE  _run_err
)
message(STATUS "consumer_witness stdout: ${_run_out}")
if(NOT _run_rc EQUAL 0)
  message(FATAL_ERROR "consumer_witness exited ${_run_rc}:\n${_run_out}\n${_run_err}")
endif()
if(NOT _run_out MATCHES "^PASS:")
  message(FATAL_ERROR "consumer_witness did not print PASS: ${_run_out}")
endif()

message(STATUS "fixpp::consumer::install-witness: OK")
