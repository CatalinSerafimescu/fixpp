# tests/packaging/run_telemetry_provenance_witness.cmake — 084 T062a (FR-011).
#
# EVERY shipped artifact must be built FIXPP_BUILD_OTEL=ON.
#
# FR-011 explicitly PERMITS an OTel-OFF dependency build as a development
# accelerator while packaging logic is being written, and forbids one reaching a
# shipped artifact. Nothing enforced the second half: T003 pins OTel-ON only for
# the new linux-gcc-debug preset, and the five pre-existing presets inherit the
# option default unasserted — so a leftover accelerator configure would ship
# silently, reproducing the very asymmetry spec Assumption 4 exists to prevent.
#
# ── This is a FIFTH ctest and deliberately NOT an extension of T060 ──────────
# T060 (packaging::provenance) and this test read the SAME provenance record but
# assert different things off DIFFERENT red fixtures:
#   T060's red  — a package from a mismatched configuration / revision / worktree
#   this one's  — a package from a deliberately OTel-OFF configuration
# Merged into one ctest, a failure could not say which gate fired — the shape
# T059 refuses against T025 and T042 refuses against T041.

cmake_minimum_required(VERSION 3.28)

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_WORK_DIR)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_telemetry_provenance_witness.cmake: -D${_var}=... is required")
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

# Reads the telemetry field out of one artifact. Shared by both legs so the red
# leg exercises the SAME code path that runs in anger.
function(_fixpp_telemetry_of _package _work _out_state)
  set(_x "${_work}/x")
  file(REMOVE_RECURSE "${_x}")
  file(MAKE_DIRECTORY "${_x}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_package}"
                  WORKING_DIRECTORY "${_x}" RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "T062a: failed to extract ${_package}")
  endif()
  # By BASENAME at any depth — `usr/` is Linux-only (see run_provenance_witness).
  file(GLOB_RECURSE _p "${_x}/*/fixpp-package-provenance.txt")
  if(_p STREQUAL "")
    message(FATAL_ERROR
      "T062a/FR-021a: ${_package} carries no provenance file, so its telemetry state "
      "cannot be established. An unidentifiable artifact must not ship.")
  endif()
  list(GET _p 0 _pfile)
  file(READ "${_pfile}" _t)
  if(NOT _t MATCHES "telemetry[ ]*:[ ]*([^\n]+)")
    message(FATAL_ERROR
      "T062a: provenance in ${_package} has no 'telemetry' field. The stamp cannot "
      "answer the question this gate asks, so the gate would pass vacuously.")
  endif()
  string(STRIP "${CMAKE_MATCH_1}" _state)
  set(${_out_state} "${_state}" PARENT_SCOPE)
endfunction()

# ── PASS leg: every produced artifact is OTel-ON ─────────────────────────────
file(GLOB _artifacts "${_pkgdir}/*.tar.gz")
if(_artifacts STREQUAL "")
  message(FATAL_ERROR "T062a: cpack produced no artifact to inspect")
endif()
foreach(_a IN LISTS _artifacts)
  get_filename_component(_n "${_a}" NAME)
  _fixpp_telemetry_of("${_a}" "${FIXPP_WORK_DIR}/pass" _state)
  if(NOT _state STREQUAL "ON")
    message(FATAL_ERROR
      "T062a/FR-011: ${_n} is stamped telemetry='${_state}'. Every shipped artifact "
      "must be built FIXPP_BUILD_OTEL=ON; an OTel-OFF build is permitted only as a "
      "development accelerator and must never be packaged.")
  endif()
  message(STATUS "T062a: ${_n} telemetry=ON")
endforeach()

# ── RED leg: a genuinely OTel-OFF package must be REJECTED ───────────────────
# FIXPP_OTEL_OFF_PACKAGE points at a real artifact produced from a real OTel-OFF
# tree (configured with the FIXPP_ALLOW_OTEL_OFF_PACKAGE escape hatch, which
# exists solely for this proof). Nothing is fabricated: rewriting the stamp would
# test the parser, not the gate.
#
# ── Why the red leg is OPTIONAL here, unlike T042's ──────────────────────────
# This feature runs its red legs in two shapes, and the choice is not stylistic:
#
#   T042 (clean-env)  — red leg is PERMANENT and runs every time, because it is
#                       cheap and self-contained: copy a dependency prefix,
#                       delete one file, re-configure.
#   T059 (contents)   — red leg was a ONE-TIME RECORDED PROOF, because producing
#                       it requires mutating the build tree.
#
# Producing this red fixture requires an ENTIRE SECOND BUILD from a genuinely
# OTel-OFF tree. That is T059-shaped, not T042-shaped, so the red is a one-time
# recorded proof (`.specify/decisions/084-packaging-cpack-export-verify.md`,
# audited by T075 clause (v)) and the STANDING assertion is the PASS leg above.
#
# The pass leg is not a formality: it fails on any artifact whose provenance is
# missing, unparseable, or stamped OFF, which is the defect FR-011 names. What is
# NOT re-proven on every run is that the check can distinguish OFF from ON — and
# that was proven once, against a real artifact, and recorded.
#
# ⚠️ EARLIER DISPOSITION, DELIBERATELY REVERSED 2026-08-02: this script used to
# FATAL_ERROR when no fixture was supplied. That is wrong for a STANDING ctest —
# CI runs `ctest --preset <p>` with no label filter, so it would have gone red on
# every lane, for the absence of a fixture no lane builds. A gate that cannot
# pass in the environment it runs in is not a strict gate; it is a broken one.
if(NOT DEFINED FIXPP_OTEL_OFF_PACKAGE OR FIXPP_OTEL_OFF_PACKAGE STREQUAL "")
  message(STATUS
    "T062a: no OTel-OFF fixture supplied; the PASS leg above is the standing "
    "assertion and the red leg is the one-time recorded proof in "
    ".specify/decisions/084-packaging-cpack-export-verify.md. Supply "
    "-DFIXPP_OTEL_OFF_PACKAGE=<artifact> to re-run the red leg here.")
  message(STATUS "fixpp::packaging::telemetry-provenance: OK (pass leg only)")
  return()
endif()
if(NOT EXISTS "${FIXPP_OTEL_OFF_PACKAGE}")
  message(FATAL_ERROR
    "T062a: an OTel-OFF fixture was SPECIFIED but does not exist at "
    "${FIXPP_OTEL_OFF_PACKAGE}. A silently-skipped red leg is exactly what this "
    "test refuses; fix the path or unset the variable deliberately.")
endif()

_fixpp_telemetry_of("${FIXPP_OTEL_OFF_PACKAGE}" "${FIXPP_WORK_DIR}/red" _red_state)
if(_red_state STREQUAL "ON")
  message(FATAL_ERROR
    "T062a RED leg is VACUOUS: the fixture at ${FIXPP_OTEL_OFF_PACKAGE} is stamped "
    "telemetry=ON, so it is not an OTel-OFF package and rejecting it would prove "
    "nothing. Rebuild the fixture from a genuinely OTel-OFF tree.")
endif()
message(STATUS "T062a RED leg observed: OTel-OFF fixture is stamped telemetry='${_red_state}' "
               "and is rejected by the same check that passed the shipped artifacts")

message(STATUS "fixpp::packaging::telemetry-provenance: OK")
