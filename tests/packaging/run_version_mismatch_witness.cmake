# tests/packaging/run_version_mismatch_witness.cmake — 084 T036 (FR-006 / SC-006).
#
# SC-006: a consumer requesting an INCOMPATIBLE version must fail at CONFIGURE
# time with a version-specific diagnostic. "The failure must be observed, not
# assumed."
#
# TWO legs, and the control leg is what makes the red leg mean anything:
#   CONTROL — find_package(fixpp <installed version> REQUIRED) must SUCCEED.
#   RED     — find_package(fixpp <installed major + 1> REQUIRED) must FAIL, and
#             the diagnostic must name the VERSION, not merely say "not found".
#
# Without the control leg, a fixppConfig.cmake that was never installed, a
# malformed version file, or a misspelled package name would all make the red leg
# "fail at configure" and the test would pass for entirely the wrong reason --
# the discriminating-red-leg rule this feature applies to SC-007a/b and SC-016.
#
# Scratch output goes to FIXPP_WORK_DIR, which the caller places OUTSIDE the
# source tree (git-cleanliness gate).

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_PROBE_SRC_DIR FIXPP_WORK_DIR
             FIXPP_CXX_COMPILER FIXPP_PROJECT_VERSION FIXPP_BUILD_TYPE)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_version_mismatch_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_stage "${FIXPP_WORK_DIR}/stage")
file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${FIXPP_MAIN_BUILD_DIR}" --prefix "${_stage}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "cmake --install failed (exit ${_rc}):\n${_out}\n${_err}")
endif()

function(_fixpp_probe_version _version _build_subdir _out_rc _out_text)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -S "${FIXPP_PROBE_SRC_DIR}"
      -B "${FIXPP_WORK_DIR}/${_build_subdir}"
      -G "Ninja"
      "-DCMAKE_TOOLCHAIN_FILE=${FIXPP_MAIN_BUILD_DIR}/conan_toolchain.cmake"
      # Required, not optional: the Conan-generated dependency configs
      # (Crc32cConfig.cmake and siblings) hard-error without it, and they are
      # reached through find_dependency() INSIDE fixppConfig.cmake. Omitting it
      # makes the control leg fail for a reason that has nothing to do with
      # versioning -- which is precisely what the control leg exists to expose.
      "-DCMAKE_BUILD_TYPE=${FIXPP_BUILD_TYPE}"
      "-DCMAKE_CXX_COMPILER=${FIXPP_CXX_COMPILER}"
      "-DFIXPP_STAGE_PREFIX=${_stage}"
      "-DFIXPP_PROBE_VERSION=${_version}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  set(${_out_rc}   "${_rc}"            PARENT_SCOPE)
  set(${_out_text} "${_out}\n${_err}"  PARENT_SCOPE)
endfunction()

# ── CONTROL leg ──────────────────────────────────────────────────────────────
_fixpp_probe_version("${FIXPP_PROJECT_VERSION}" "control" _ctl_rc _ctl_text)
if(NOT _ctl_rc EQUAL 0)
  message(FATAL_ERROR
    "SC-006 CONTROL leg FAILED: find_package(fixpp ${FIXPP_PROJECT_VERSION} REQUIRED) "
    "should succeed against its own installed version but exited ${_ctl_rc}.\n"
    "The red leg below cannot be trusted until this passes -- a package that is "
    "simply unfindable would also 'fail at configure'.\n${_ctl_text}")
endif()
message(STATUS "SC-006 control leg: find_package(fixpp ${FIXPP_PROJECT_VERSION}) succeeded")

# ── RED leg ──────────────────────────────────────────────────────────────────
string(REGEX REPLACE "^([0-9]+)\\..*$" "\\1" _major "${FIXPP_PROJECT_VERSION}")
math(EXPR _incompatible_major "${_major} + 1")
set(_bad_version "${_incompatible_major}.0")

_fixpp_probe_version("${_bad_version}" "red" _red_rc _red_text)
if(_red_rc EQUAL 0)
  message(FATAL_ERROR
    "SC-006 RED leg FAILED TO FAIL: find_package(fixpp ${_bad_version} REQUIRED) "
    "SUCCEEDED against installed ${FIXPP_PROJECT_VERSION}. The version file is not "
    "enforcing compatibility, so an incompatible consumer would proceed to build "
    "and link against an ABI it never agreed to.\n${_red_text}")
endif()

# The diagnostic must be VERSION-specific. A bare "Could not find a package
# configuration file" would mean the config was not installed at all -- a
# different defect that must not be reported as SC-006 passing.
if(NOT _red_text MATCHES "[Cc]ompatible|version|Version")
  message(FATAL_ERROR
    "SC-006 RED leg failed for the WRONG REASON: configure did fail, but the "
    "diagnostic never mentions the version, so this does not demonstrate version "
    "checking.\n${_red_text}")
endif()

string(REGEX MATCH "[^\n]*[Cc]ompatible[^\n]*" _red_line "${_red_text}")
message(STATUS "SC-006 red leg observed (exit ${_red_rc}): ${_red_line}")
message(STATUS "fixpp::packaging::version-mismatch: OK")
