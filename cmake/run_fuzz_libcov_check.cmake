# cmake/run_fuzz_libcov_check.cmake — #508 regression guard, run as `cmake -P`.
#
# Registered once per replayed harness by fixpp_add_fuzz_replay() in
# cmake/Helpers.cmake. It checks that a fuzz binary carries coverage counters
# from code OUTSIDE its own harness objects, i.e. that the library TUs it links
# were built with -fsanitize=fuzzer-no-link (fixpp_instrument_libraries_for_fuzzing()).
#
# THE CONDITION: the binary's total inline 8-bit counters — M in libFuzzer's
# `INFO: Loaded N modules (M inline 8-bit counters)` — must EXCEED the counters
# the harness objects contribute, which is the summed size of every
# `__sancov_cntrs` section in them (one byte per counter). With the library TUs
# uninstrumented, M cannot exceed that sum: a header-inline function is a COMDAT
# that the linker may take from an uninstrumented library copy instead, which
# only ever removes harness counters.
#
# ⚠️ EVERY "COULD NOT MEASURE" OUTCOME IS A FAILURE, never a pass: a missing
# input, a readelf error, an object with no `__sancov_cntrs` section, a section
# row this script cannot parse, a binary that exits non-zero or is killed, and a
# Loaded line that is absent or ambiguous. An instrument that cannot read its
# inputs must not read as green.
#
# Required -D inputs:
#   FIXPP_FUZZ_BIN          the fuzz executable
#   FIXPP_HARNESS_OBJECTS   its own object files, `|`-separated
#   FIXPP_READELF           readelf (CMAKE_READELF)
#   FIXPP_WORK_DIR          scratch dir, recreated on every run

foreach(_var FIXPP_FUZZ_BIN FIXPP_HARNESS_OBJECTS FIXPP_READELF FIXPP_WORK_DIR)
  if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
    message(FATAL_ERROR "run_fuzz_libcov_check.cmake: -D${_var}=... is required")
  endif()
endforeach()
foreach(_path "${FIXPP_FUZZ_BIN}" "${FIXPP_READELF}")
  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "run_fuzz_libcov_check.cmake: '${_path}' does not exist")
  endif()
endforeach()

# ── 1. Counters contributed by the harness's own objects ─────────────────────
string(REPLACE "|" ";" _objects "${FIXPP_HARNESS_OBJECTS}")
set(_harness_counters 0)
foreach(_obj IN LISTS _objects)
  if(NOT EXISTS "${_obj}")
    message(FATAL_ERROR "run_fuzz_libcov_check.cmake: harness object '${_obj}' does not exist")
  endif()
  execute_process(
    COMMAND "${FIXPP_READELF}" -S -W "${_obj}"
    RESULT_VARIABLE _re_rc
    OUTPUT_VARIABLE _re_out
    ERROR_VARIABLE  _re_err)
  if(NOT _re_rc STREQUAL "0")
    message(FATAL_ERROR "readelf -S -W '${_obj}' failed (${_re_rc}):\n${_re_err}")
  endif()

  # Name column only: whitespace before, whitespace after, so `.rela__sancov_cntrs`
  # or a longer name cannot match. Every name hit must also parse as a full row.
  string(REGEX MATCHALL "[ \t]__sancov_cntrs[ \t]" _name_hits "${_re_out}")
  string(REGEX MATCHALL
    "[ \t]__sancov_cntrs[ \t]+[A-Z_]+[ \t]+[0-9a-fA-F]+[ \t]+[0-9a-fA-F]+[ \t]+[0-9a-fA-F]+"
    _rows "${_re_out}")
  list(LENGTH _name_hits _n_names)
  list(LENGTH _rows _n_rows)
  if(_n_names EQUAL 0)
    message(FATAL_ERROR
      "'${_obj}' has no __sancov_cntrs section, so it was not built with "
      "-fsanitize=fuzzer and the harness baseline cannot be measured.")
  endif()
  if(NOT _n_names EQUAL _n_rows)
    message(FATAL_ERROR
      "'${_obj}': ${_n_names} __sancov_cntrs name(s) but ${_n_rows} parseable row(s); "
      "the readelf -S -W layout is not the one this script reads.\n${_re_out}")
  endif()
  foreach(_row IN LISTS _rows)
    string(REGEX REPLACE ".*[ \t]([0-9a-fA-F]+)$" "\\1" _size_hex "${_row}")
    math(EXPR _harness_counters "${_harness_counters} + 0x${_size_hex}")
  endforeach()
endforeach()

# ── 2. Total counters in the linked binary ───────────────────────────────────
# A fresh empty input dir per run: never a corpus, whose inputs could make the
# run slow or crash for reasons unrelated to this check.
file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")
file(MAKE_DIRECTORY "${FIXPP_WORK_DIR}/empty")
execute_process(
  COMMAND "${FIXPP_FUZZ_BIN}" -runs=0
          "-artifact_prefix=${FIXPP_WORK_DIR}/"
          "${FIXPP_WORK_DIR}/empty"
  WORKING_DIRECTORY "${FIXPP_WORK_DIR}"
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _run_out
  ERROR_VARIABLE  _run_out)
# STREQUAL, not EQUAL: a signal death is reported as a string.
if(NOT _run_rc STREQUAL "0")
  message(FATAL_ERROR "'${FIXPP_FUZZ_BIN}' exited abnormally (${_run_rc}):\n${_run_out}")
endif()

string(REGEX MATCHALL "INFO: Loaded [0-9]+ modules +\\([0-9]+ inline 8-bit counters\\)"
       _loaded "${_run_out}")
list(LENGTH _loaded _n_loaded)
if(NOT _n_loaded EQUAL 1)
  message(FATAL_ERROR
    "expected exactly one libFuzzer `INFO: Loaded ... inline 8-bit counters` line from "
    "'${FIXPP_FUZZ_BIN}', found ${_n_loaded}; the total cannot be read.\n${_run_out}")
endif()
string(REGEX REPLACE ".*\\(([0-9]+) inline.*" "\\1" _total_counters "${_loaded}")

# ── 3. The condition ─────────────────────────────────────────────────────────
if(NOT _total_counters GREATER _harness_counters)
  message(FATAL_ERROR
    "'${FIXPP_FUZZ_BIN}' carries ${_total_counters} inline 8-bit counters, not more than the "
    "${_harness_counters} its own harness objects contribute: no library code it links is "
    "coverage-instrumented, so libFuzzer is blind to it (#508). Check that "
    "fixpp_instrument_libraries_for_fuzzing() ran and that the linked libraries live under src/.")
endif()
message(STATUS
  "fuzz libcov: ${FIXPP_FUZZ_BIN}: total ${_total_counters} > harness ${_harness_counters}")
