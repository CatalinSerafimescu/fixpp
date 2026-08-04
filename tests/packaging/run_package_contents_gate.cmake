# tests/packaging/run_package_contents_gate.cmake — 086 / Gate B r2 P1 #1.
#
# Outer driver for run_package_contents_witness.cmake. It exists because the two
# things that must both hold cannot both be checked by CTest properties:
#
#   1. the witness must EXIT 0  — otherwise a FATAL_ERROR anywhere in it passes;
#   2. the witness must have RUN the 086 block  — otherwise deleting that block
#      leaves the script exiting 0 on the inherited 084 assertions alone.
#
# CTest's `PASS_REGULAR_EXPRESSION` cannot express the conjunction: it explicitly
# **IGNORES the process exit code** when the regex matches. Registering the
# witness with a pass-regex for the completion token — this gate's own round-1
# fix — therefore made it STRICTLY WEAKER than before: on Linux the witness
# processes DEB, RPM and TGZ in sequence, so a token printed while checking DEB
# masked a FATAL_ERROR raised while checking RPM or TGZ. The gate written to stop
# silent deletion introduced silent failure.
#
# Both legs are asserted here instead, in order, with the exit code first.

foreach(_var FIXPP_WITNESS_SCRIPT FIXPP_MAIN_BUILD_DIR FIXPP_WORK_DIR FIXPP_SOURCE_DIR
             FIXPP_PROJECT_VERSION)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_package_contents_gate.cmake: -D${_var}=... is required")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_MAIN_BUILD_DIR=${FIXPP_MAIN_BUILD_DIR}"
    "-DFIXPP_WORK_DIR=${FIXPP_WORK_DIR}"
    "-DFIXPP_SOURCE_DIR=${FIXPP_SOURCE_DIR}"
    "-DFIXPP_PROJECT_VERSION=${FIXPP_PROJECT_VERSION}"
    -P "${FIXPP_WITNESS_SCRIPT}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE  _err
)

# The witness writes its progress to stderr via message(STATUS); surface all of it
# so a failure here is diagnosable without re-running by hand.
message(STATUS "---- package-contents witness output ----\n${_out}\n${_err}")

# LEG 1 — exit code. First, because a non-zero exit means the witness FOUND
# something, and that finding is what should be reported.
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "package-contents witness FAILED (exit ${_rc}). See the output above.")
endif()

# LEG 2 — the 086 block ran, over EVERY produced artifact. The witness asserts
# the per-artifact count against the artifact count itself before printing this,
# so the token cannot be emitted by a partial run.
set(_token "086: FR-010/FR-010a asserted over")
string(FIND "${_out}${_err}" "${_token}" _tok_pos)
if(_tok_pos EQUAL -1)
  message(FATAL_ERROR
    "086: the witness exited 0 but never reported completing the FR-010/FR-010a "
    "block. Either that block was removed from run_package_contents_witness.cmake, "
    "or it was skipped. An exit code alone cannot detect this: with the block gone "
    "the script still passes every inherited 084 assertion and exits 0.")
endif()

message(STATUS "fixpp::packaging::contents gate: OK (witness exited 0 AND asserted the 086 block)")
