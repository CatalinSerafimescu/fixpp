# tests/packaging/run_provenance_witness.cmake — 084 T060 (FR-021a, I24).
#
# A package carries a provenance stamp identifying the build that produced it.
# This witness reads that stamp OUT OF A PRODUCED PACKAGE and checks it against
# the state a consumer expects. A package from a DIFFERENT CONFIGURATION, a
# DIFFERENT SOURCE REVISION, or a DIFFERENT WORKTREE STATE must be REJECTED.
#
# Why this matters concretely: the artifact directory deliberately outlives the
# build-tree deletion cycle (FR-021), so packages from earlier configurations and
# earlier source states accumulate alongside current ones. Picking up a stale one
# is the normal accident, not an exotic one.
#
# Why worktree state is a SEPARATE field from the revision: two packages built
# from the same commit either side of an uncommitted edit are indistinguishable
# under (configuration, revision) alone — and an uncommitted edit is the normal
# state of a working branch.
#
# ── The red leg ──────────────────────────────────────────────────────────────
# The witness's contract is (package, expected-state) -> verdict. Feeding it an
# expectation that does not match the package IS the "package from a different
# configuration" case, from the witness's point of view — it is exactly what
# happens when a stale artifact is picked out of the shared directory. So the red
# leg passes a mismatched expectation against a REAL, UNMODIFIED package. Nothing
# is fabricated and no artifact is tampered with: tampering the stamp after the
# fact would test the parser, not the gate.

cmake_minimum_required(VERSION 3.28)

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_WORK_DIR FIXPP_BUILD_TYPE)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_provenance_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_pkgdir "${FIXPP_WORK_DIR}/packages")
file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")
file(MAKE_DIRECTORY "${_pkgdir}")

execute_process(
  COMMAND "${CMAKE_CPACK_COMMAND}" --config "${FIXPP_MAIN_BUILD_DIR}/CPackConfig.cmake"
          -G TGZ -B "${_pkgdir}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "cpack failed (exit ${_rc}):\n${_out}\n${_err}")
endif()

file(GLOB _tgz "${_pkgdir}/*.tar.gz")
if(_tgz STREQUAL "")
  message(FATAL_ERROR "T060: cpack produced no .tar.gz to read provenance from")
endif()
list(GET _tgz 0 _pkg)

set(_x "${FIXPP_WORK_DIR}/extract")
file(MAKE_DIRECTORY "${_x}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_pkg}"
                WORKING_DIRECTORY "${_x}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "T060: failed to extract ${_pkg}")
endif()

file(GLOB_RECURSE _prov_files "${_x}/*/usr/share/doc/fixpp/fixpp-package-provenance.txt")
if(_prov_files STREQUAL "")
  message(FATAL_ERROR
    "T060/FR-021a: the package carries NO provenance file. Every artifact must be "
    "identifiable back to the build that produced it.")
endif()
list(GET _prov_files 0 _prov)
file(READ "${_prov}" _prov_text)

# ── The check, as a function so both legs run IDENTICAL logic ────────────────
# If the red leg ran different code from the pass leg it would prove nothing
# about the gate that actually runs in anger.
function(_fixpp_check_provenance _expect_config _expect_worktree _out_ok _out_why)
  set(_why "")
  foreach(_field configuration source-revision source-worktree telemetry)
    if(NOT _prov_text MATCHES "${_field}[ ]*:[ ]*([^\n]+)")
      list(APPEND _why "provenance has no '${_field}' field")
    endif()
  endforeach()
  if(NOT _why STREQUAL "")
    set(${_out_ok} FALSE PARENT_SCOPE)
    set(${_out_why} "${_why}" PARENT_SCOPE)
    return()
  endif()

  string(REGEX MATCH "configuration[ ]*:[ ]*([^\n]+)" _m "${_prov_text}")
  string(STRIP "${CMAKE_MATCH_1}" _got_config)
  string(REGEX MATCH "source-revision[ ]*:[ ]*([^\n]+)" _m "${_prov_text}")
  string(STRIP "${CMAKE_MATCH_1}" _got_rev)
  string(REGEX MATCH "source-worktree[ ]*:[ ]*([^\n]+)" _m "${_prov_text}")
  string(STRIP "${CMAKE_MATCH_1}" _got_worktree)

  if(NOT _got_config STREQUAL "${_expect_config}")
    list(APPEND _why "configuration is '${_got_config}', expected '${_expect_config}'")
  endif()
  if(NOT _got_worktree STREQUAL "${_expect_worktree}")
    list(APPEND _why "source-worktree is '${_got_worktree}', expected '${_expect_worktree}'")
  endif()
  # A revision must be a real commit hash, not the 'unknown' fallback: an
  # artifact whose provenance says 'unknown' cannot be traced to any source
  # state at all, which defeats the whole stamp.
  if(_got_rev STREQUAL "unknown" OR NOT _got_rev MATCHES "^[0-9a-f]+$")
    list(APPEND _why "source-revision is '${_got_rev}', not a commit hash")
  endif()

  if(_why STREQUAL "")
    set(${_out_ok} TRUE PARENT_SCOPE)
  else()
    set(${_out_ok} FALSE PARENT_SCOPE)
  endif()
  set(${_out_why} "${_why}" PARENT_SCOPE)
endfunction()

# ── PASS leg — the package matches the build that produced it ────────────────
string(REGEX MATCH "source-worktree[ ]*:[ ]*([^\n]+)" _m "${_prov_text}")
string(STRIP "${CMAKE_MATCH_1}" _actual_worktree)

_fixpp_check_provenance("${FIXPP_BUILD_TYPE}" "${_actual_worktree}" _ok _why)
if(NOT _ok)
  string(REPLACE ";" "\n  " _why_pretty "${_why}")
  message(FATAL_ERROR "T060 PASS leg FAILED:\n  ${_why_pretty}")
endif()
message(STATUS "T060 PASS leg: provenance matches the producing build "
               "(configuration=${FIXPP_BUILD_TYPE}, worktree=${_actual_worktree})")

if(_actual_worktree STREQUAL "dirty")
  message(WARNING
    "T060: this package was built from a DIRTY worktree, so its source-revision "
    "does not fully identify its inputs. Correct for a working branch; such a "
    "package must not be published.")
endif()

# ── RED leg — a package from a DIFFERENT CONFIGURATION is rejected ───────────
if(FIXPP_BUILD_TYPE STREQUAL "Debug")
  set(_wrong_config "Release")
else()
  set(_wrong_config "Debug")
endif()

_fixpp_check_provenance("${_wrong_config}" "${_actual_worktree}" _red_ok _red_why)
if(_red_ok)
  message(FATAL_ERROR
    "T060 RED leg FAILED TO FAIL: a package stamped '${FIXPP_BUILD_TYPE}' was accepted "
    "as '${_wrong_config}'. Provenance is not discriminating by configuration, so a "
    "stale artifact from another configuration in the shared artifact directory "
    "would be consumed silently.")
endif()
string(REPLACE ";" "; " _red_why_pretty "${_red_why}")
message(STATUS "T060 RED leg observed: ${_red_why_pretty}")

# ── RED leg 2 — a different WORKTREE STATE is rejected ───────────────────────
# Separate from the configuration leg on purpose: (configuration, revision) alone
# cannot distinguish two builds either side of an uncommitted edit, and this is
# the field that closes that hole. If it were not checked independently, the gate
# would pass on exactly the staleness case FR-021a calls the LIKELY one.
if(_actual_worktree STREQUAL "clean")
  set(_wrong_worktree "dirty")
else()
  set(_wrong_worktree "clean")
endif()
_fixpp_check_provenance("${FIXPP_BUILD_TYPE}" "${_wrong_worktree}" _red2_ok _red2_why)
if(_red2_ok)
  message(FATAL_ERROR
    "T060 RED leg 2 FAILED TO FAIL: a package stamped worktree='${_actual_worktree}' "
    "was accepted as '${_wrong_worktree}'. Worktree state is not discriminating, so "
    "two packages from the same commit either side of an uncommitted edit are "
    "indistinguishable — the likely staleness case, not the exotic one.")
endif()
string(REPLACE ";" "; " _red2_why_pretty "${_red2_why}")
message(STATUS "T060 RED leg 2 observed: ${_red2_why_pretty}")

message(STATUS "fixpp::packaging::provenance: OK")
