# tests/packaging/run_package_target_witness.cmake — 084 packaging::package-target
#
# Asserts the SHAPE of the production fixpp-package target against the generated
# Ninja build graph. The standing witnesses invoke cpack directly; this one
# exists so regressions in the target wrapper itself cannot slip through green.

cmake_minimum_required(VERSION 3.28)

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_SOURCE_DIR)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_package_target_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

find_program(_ninja NAMES ninja)
if(NOT _ninja)
  message(FATAL_ERROR
    "packaging::package-target requires Ninja, and no 'ninja' executable was found")
endif()

set(_query_target "CMakeFiles/fixpp-package")
execute_process(
  COMMAND "${_ninja}" -C "${FIXPP_MAIN_BUILD_DIR}" -t query "${_query_target}"
  RESULT_VARIABLE _query_rc
  OUTPUT_VARIABLE _query_out
  ERROR_VARIABLE _query_err)
if(NOT _query_rc EQUAL 0)
  message(FATAL_ERROR
    "packaging::package-target: ninja -t query ${_query_target} failed (exit ${_query_rc}).\n"
    "${_query_out}\n${_query_err}")
endif()
if(_query_out STREQUAL "")
  message(FATAL_ERROR
    "packaging::package-target: ninja -t query ${_query_target} returned no output")
endif()

string(REGEX MATCHALL "\n    \\|\\| [^\n]+" _query_input_lines "\n${_query_out}")
set(_query_inputs "")
foreach(_line IN LISTS _query_input_lines)
  string(REGEX REPLACE "^\n    \\|\\| ([^\n]+)$" "\\1" _input "${_line}")
  list(APPEND _query_inputs "${_input}")
endforeach()
list(REMOVE_DUPLICATES _query_inputs)
list(SORT _query_inputs)
if(_query_inputs STREQUAL "")
  message(FATAL_ERROR
    "packaging::package-target: ninja -t query found no order-only inputs for ${_query_target}")
endif()

set(_expected_inputs_manifest "${FIXPP_MAIN_BUILD_DIR}/fixpp-package-target-inputs.txt")
if(NOT EXISTS "${_expected_inputs_manifest}")
  message(FATAL_ERROR
    "packaging::package-target: expected manifest '${_expected_inputs_manifest}' does not exist")
endif()
file(STRINGS "${_expected_inputs_manifest}" _expected_inputs)
list(FILTER _expected_inputs EXCLUDE REGEX "^$")
list(REMOVE_DUPLICATES _expected_inputs)
list(SORT _expected_inputs)
if(_expected_inputs STREQUAL "")
  message(FATAL_ERROR
    "packaging::package-target: expected-input manifest '${_expected_inputs_manifest}' is empty")
endif()

set(_missing_inputs "")
foreach(_expected_input IN LISTS _expected_inputs)
  if(NOT _expected_input IN_LIST _query_inputs)
    list(APPEND _missing_inputs "${_expected_input}")
  endif()
endforeach()
if(_missing_inputs)
  message(FATAL_ERROR
    "packaging::package-target: fixpp-package is missing expected build inputs.\n"
    "Missing: ${_missing_inputs}\n"
    "Expected: ${_expected_inputs}\n"
    "Actual query inputs: ${_query_inputs}")
endif()

execute_process(
  COMMAND "${_ninja}" -C "${FIXPP_MAIN_BUILD_DIR}" -t commands fixpp-package
  RESULT_VARIABLE _commands_rc
  OUTPUT_VARIABLE _commands_out
  ERROR_VARIABLE _commands_err)
if(NOT _commands_rc EQUAL 0)
  message(FATAL_ERROR
    "packaging::package-target: ninja -t commands fixpp-package failed (exit ${_commands_rc}).\n"
    "${_commands_out}\n${_commands_err}")
endif()
if(_commands_out STREQUAL "")
  message(FATAL_ERROR
    "packaging::package-target: ninja -t commands fixpp-package returned no commands")
endif()

set(_clear_fragment "-E rm -rf ${FIXPP_MAIN_BUILD_DIR}/_packages")
set(_cpack_fragment "--config ${FIXPP_MAIN_BUILD_DIR}/CPackConfig.cmake")
set(_copy_fragment "-P ${FIXPP_SOURCE_DIR}/cmake/FixppCopyArtifacts.cmake")
string(FIND "${_commands_out}" "${_clear_fragment}" _clear_pos)
string(FIND "${_commands_out}" "${_cpack_fragment}" _cpack_pos)
string(FIND "${_commands_out}" "${_copy_fragment}" _copy_pos)
if(_clear_pos LESS 0 OR _cpack_pos LESS 0 OR _copy_pos LESS 0)
  message(FATAL_ERROR
    "packaging::package-target: required packaging command fragments were not all present.\n"
    "clear='${_clear_fragment}' at ${_clear_pos}\n"
    "cpack='${_cpack_fragment}' at ${_cpack_pos}\n"
    "copy='${_copy_fragment}' at ${_copy_pos}")
endif()
if(NOT (_clear_pos LESS _cpack_pos AND _cpack_pos LESS _copy_pos))
  message(FATAL_ERROR
    "packaging::package-target: packaging commands are out of order.\n"
    "Expected: clear -> cpack -> copy\n"
    "Actual positions: clear=${_clear_pos}, cpack=${_cpack_pos}, copy=${_copy_pos}")
endif()

message(STATUS
  "fixpp::packaging::package-target: OK (inputs=${_query_inputs})")
