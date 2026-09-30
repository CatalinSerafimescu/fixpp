# cmake/run_fuzz_libcov_check.cmake — #508 regression guard, run as `cmake -P`.
#
# Registered once per replayed harness by fixpp_add_fuzz_replay() in
# cmake/Helpers.cmake. It checks that a fuzz binary carries coverage counters
# from code OUTSIDE its own harness objects. Attributing that excess to the src/
# libraries rests on two premises: under FIXPP_BUILD_FUZZ only the src/ library
# targets get -fsanitize=fuzzer-no-link (fixpp_instrument_libraries_for_fuzzing()),
# and an unfixed binary's total equals its harness sum (L-508-1).
#
# THE CONDITION: the binary's inline 8-bit counters must EXCEED the counters its
# harness objects contribute. Both are read statically, as the summed size of
# every `__sancov_cntrs` section (one byte per counter); in the linked binary
# that size is the M libFuzzer reports as `INFO: Loaded N modules (M inline
# 8-bit counters)`. With the library TUs uninstrumented, the binary's total
# cannot exceed the harness sum: a header-inline function is a COMDAT that the
# linker may take from an uninstrumented library copy instead, which only ever
# removes harness counters.
#
# ⚠️ EVERY "COULD NOT MEASURE" OUTCOME IS A FAILURE, never a pass: a missing
# input, a readelf error, a file (object or binary) with no `__sancov_cntrs`
# section, and a section row this script cannot parse. An instrument that
# cannot read its inputs must not read as green.
#
# Required -D inputs:
#   FIXPP_FUZZ_BIN          the fuzz executable
#   FIXPP_HARNESS_OBJECTS   its own object files, `|`-separated
#   FIXPP_READELF           readelf (CMAKE_READELF)

foreach(_var FIXPP_FUZZ_BIN FIXPP_HARNESS_OBJECTS FIXPP_READELF)
  if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
    message(FATAL_ERROR "run_fuzz_libcov_check.cmake: -D${_var}=... is required")
  endif()
endforeach()
string(REPLACE "|" ";" _objects "${FIXPP_HARNESS_OBJECTS}")
foreach(_path "${FIXPP_FUZZ_BIN}" "${FIXPP_READELF}" ${_objects})
  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "run_fuzz_libcov_check.cmake: '${_path}' does not exist")
  endif()
endforeach()

# Sets <out_var> to the summed size of every `__sancov_cntrs` section in <file>.
# <role> names the file in the failure text.
function(_sancov_cntrs_size file role out_var)
  execute_process(
    COMMAND "${FIXPP_READELF}" -S -W "${file}"
    RESULT_VARIABLE _re_rc
    OUTPUT_VARIABLE _re_out
    ERROR_VARIABLE  _re_err)
  if(NOT _re_rc STREQUAL "0")
    message(FATAL_ERROR "readelf -S -W '${file}' failed (${_re_rc}):\n${_re_err}")
  endif()

  # Name column only: whitespace before, whitespace after, so `.rela__sancov_cntrs`
  # or a longer name cannot match. Every name hit must also parse as a full row.
  set(_name_re "[ \t]__sancov_cntrs[ \t]")
  string(REGEX MATCHALL "${_name_re}" _name_hits "${_re_out}")
  string(REGEX MATCHALL
    "${_name_re}+[A-Z_]+[ \t]+[0-9a-fA-F]+[ \t]+[0-9a-fA-F]+[ \t]+[0-9a-fA-F]+"
    _rows "${_re_out}")
  list(LENGTH _name_hits _n_names)
  list(LENGTH _rows _n_rows)
  if(_n_names EQUAL 0)
    message(FATAL_ERROR
      "${role} '${file}' has no __sancov_cntrs section, so it carries no -fsanitize=fuzzer "
      "coverage counters and its count cannot be measured.")
  endif()
  if(NOT _n_names EQUAL _n_rows)
    message(FATAL_ERROR
      "${role} '${file}': ${_n_names} __sancov_cntrs name(s) but ${_n_rows} parseable row(s); "
      "the readelf -S -W layout is not the one this script reads.\n${_re_out}")
  endif()
  set(_sum 0)
  foreach(_row IN LISTS _rows)
    string(REGEX REPLACE ".*[ \t]([0-9a-fA-F]+)$" "\\1" _size_hex "${_row}")
    math(EXPR _sum "${_sum} + 0x${_size_hex}")
  endforeach()
  set(${out_var} "${_sum}" PARENT_SCOPE)
endfunction()

set(_harness_counters 0)
foreach(_obj IN LISTS _objects)
  _sancov_cntrs_size("${_obj}" "harness object" _n)
  math(EXPR _harness_counters "${_harness_counters} + ${_n}")
endforeach()
_sancov_cntrs_size("${FIXPP_FUZZ_BIN}" "fuzz binary" _total_counters)

if(NOT _total_counters GREATER _harness_counters)
  message(FATAL_ERROR
    "'${FIXPP_FUZZ_BIN}' carries ${_total_counters} inline 8-bit counters, not more than the "
    "${_harness_counters} its own harness objects contribute: no library code it links is "
    "coverage-instrumented, so libFuzzer is blind to it (#508). Check that "
    "fixpp_instrument_libraries_for_fuzzing() ran and that the linked libraries live under src/.")
endif()
message(STATUS
  "fuzz libcov: ${FIXPP_FUZZ_BIN}: total ${_total_counters} > harness ${_harness_counters}")
