# tests/codegen/codegen_source_staleness_test.cmake — Gate B PR#187 round 3
#
# Seeds the residual F1 stale-tool hole in a configured build tree:
#   1. Mutate tools/codegen/fixpp-codegen/emit_builders.cpp without rebuilding
#      the main-tree fixpp-codegen binary.
#   2. Re-run configure on the SAME build tree.
#   3. Assert Codegen.cmake rejects the stale main binary, rebuilds the
#      bootstrap tool, and regenerates v44/all.hpp from a fresh tool.
#   4. Restore the source file and confirm a subsequent clean reconfigure
#      settles back to "up-to-date".

cmake_minimum_required(VERSION 3.25)

set(_PASS_COUNT 0)
set(_FAIL_COUNT 0)
set(_FAIL_MSGS "")

macro(_assert condition msg)
  cmake_language(EVAL CODE "
    if(${condition})
      set(_assert_result TRUE)
    else()
      set(_assert_result FALSE)
    endif()
  ")
  if(_assert_result)
    math(EXPR _PASS_COUNT "${_PASS_COUNT} + 1")
  else()
    math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
    list(APPEND _FAIL_MSGS "  FAIL: ${msg}")
  endif()
endmacro()

macro(_assert_false condition msg)
  cmake_language(EVAL CODE "
    if(${condition})
      set(_assert_false_result TRUE)
    else()
      set(_assert_false_result FALSE)
    endif()
  ")
  if(NOT _assert_false_result)
    math(EXPR _PASS_COUNT "${_PASS_COUNT} + 1")
  else()
    math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
    list(APPEND _FAIL_MSGS "  FAIL: ${msg}")
  endif()
endmacro()

macro(_assert_contains haystack needle msg)
  string(FIND "${haystack}" "${needle}" _idx)
  if(NOT _idx EQUAL -1)
    math(EXPR _PASS_COUNT "${_PASS_COUNT} + 1")
  else()
    math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
    list(APPEND _FAIL_MSGS "  FAIL: ${msg}")
  endif()
endmacro()

macro(_assert_streq lhs rhs msg)
  if("${lhs}" STREQUAL "${rhs}")
    math(EXPR _PASS_COUNT "${_PASS_COUNT} + 1")
  else()
    math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
    list(APPEND _FAIL_MSGS "  FAIL: ${msg}")
  endif()
endmacro()

foreach(_var IN ITEMS
    FIXPP_CMAKE_COMMAND
    FIXPP_CONFIGURE_SOURCE_DIR
    FIXPP_CONFIGURE_BINARY_DIR
    FIXPP_CODEGEN_MAIN_BIN
    FIXPP_CODEGEN_BOOTSTRAP_BIN
    FIXPP_GENERATOR_SOURCE
    FIXPP_BUILDERS_HEADER)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "[F1 witness] Missing required -D argument: ${_var}")
  endif()
endforeach()

file(READ "${FIXPP_GENERATOR_SOURCE}" _original_generator_source)

function(_run_configure _output_var _result_var)
  execute_process(
    COMMAND "${FIXPP_CMAKE_COMMAND}" -S "${FIXPP_CONFIGURE_SOURCE_DIR}" -B "${FIXPP_CONFIGURE_BINARY_DIR}"
    RESULT_VARIABLE _cfg_result
    OUTPUT_VARIABLE _cfg_output
    ERROR_VARIABLE  _cfg_error
  )
  set(${_output_var} "${_cfg_output}\n${_cfg_error}" PARENT_SCOPE)
  set(${_result_var} "${_cfg_result}" PARENT_SCOPE)
endfunction()

macro(_assert_registry_size_83)
  if(NOT EXISTS "${FIXPP_BUILDERS_HEADER}")
    list(APPEND _FAIL_MSGS "  FAIL: all.hpp missing: ${FIXPP_BUILDERS_HEADER}")
    math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
  else()
    file(READ "${FIXPP_BUILDERS_HEADER}" _builders_contents)
    string(FIND "${_builders_contents}"
      "inline constexpr ::std::array<builder_registry_entry, 83> builder_registry = {{"
      _registry_idx)
    if(NOT _registry_idx EQUAL -1)
      math(EXPR _PASS_COUNT "${_PASS_COUNT} + 1")
    else()
      list(APPEND _FAIL_MSGS
        "  FAIL: v44/all.hpp does not expose builder_registry.size() == 83.")
      math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
    endif()
  endif()
endmacro()

set(_sentinel "\n// Gate B PR#187 round 3 stale-codegen witness sentinel\n")
file(APPEND "${FIXPP_GENERATOR_SOURCE}" "${_sentinel}")

set(_first_stamp "${FIXPP_CONFIGURE_BINARY_DIR}/_codegen/f1_stale_tool_first.stamp")
file(WRITE "${_first_stamp}" "first")
execute_process(COMMAND "${FIXPP_CMAKE_COMMAND}" -E sleep 1)

_run_configure(_first_output _first_result)

_assert("_first_result EQUAL 0" "mutated-source reconfigure must succeed")
_assert_contains(
  "${_first_output}"
  "[Codegen] Generator-source fingerprint changed; checking codegen tool freshness."
  "mutated-source reconfigure must report generator-source fingerprint change")
_assert_contains(
  "${_first_output}"
  "[Codegen] Main-build fixpp-codegen is stale relative to generator sources; rebuilding bootstrap tool."
  "mutated-source reconfigure must reject the stale main-build fixpp-codegen")
_assert_contains(
  "${_first_output}"
  "[Codegen] Bootstrap: building fixpp-codegen target"
  "mutated-source reconfigure must rebuild the bootstrap fixpp-codegen target")
_assert_contains(
  "${_first_output}"
  "[Codegen] Running fixpp-codegen at configure time"
  "mutated-source reconfigure must regenerate headers at configure time")
_assert("EXISTS \"${FIXPP_CODEGEN_BOOTSTRAP_BIN}\""
  "bootstrap fixpp-codegen binary must exist after stale-tool reconfigure")
if(EXISTS "${FIXPP_CODEGEN_BOOTSTRAP_BIN}")
  _assert("\"${FIXPP_CODEGEN_BOOTSTRAP_BIN}\" IS_NEWER_THAN \"${_first_stamp}\""
    "bootstrap fixpp-codegen binary must be rebuilt after the stale-tool seed")
endif()
_assert_false("\"${FIXPP_CODEGEN_MAIN_BIN}\" IS_NEWER_THAN \"${_first_stamp}\""
  "main-tree fixpp-codegen binary must stay stale during configure-time recovery")
_assert_registry_size_83()

file(WRITE "${FIXPP_GENERATOR_SOURCE}" "${_original_generator_source}")

set(_second_stamp "${FIXPP_CONFIGURE_BINARY_DIR}/_codegen/f1_stale_tool_second.stamp")
file(WRITE "${_second_stamp}" "second")
execute_process(COMMAND "${FIXPP_CMAKE_COMMAND}" -E sleep 1)

_run_configure(_second_output _second_result)

_assert("_second_result EQUAL 0" "restored-source reconfigure must succeed")
_assert_contains(
  "${_second_output}"
  "[Codegen] Generator-source fingerprint changed; checking codegen tool freshness."
  "restored-source reconfigure must report generator-source fingerprint change")
_assert_contains(
  "${_second_output}"
  "[Codegen] Bootstrap: building fixpp-codegen target"
  "restored-source reconfigure must rebuild the bootstrap tool from restored sources")
_assert_registry_size_83()

set(_restored_contents "")
file(READ "${FIXPP_GENERATOR_SOURCE}" _restored_contents)
_assert_streq("${_restored_contents}" "${_original_generator_source}"
  "generator source must be restored byte-for-byte after the witness")

_run_configure(_third_output _third_result)

_assert("_third_result EQUAL 0" "clean follow-up reconfigure must succeed")
_assert_contains(
  "${_third_output}"
  "[Codegen] Generated headers up-to-date; skipping re-run."
  "clean follow-up reconfigure must settle to the up-to-date path")
_assert_registry_size_83()

execute_process(
  COMMAND git -C "${FIXPP_CONFIGURE_SOURCE_DIR}" status --porcelain -- tools/codegen/fixpp-codegen/emit_builders.cpp
  OUTPUT_VARIABLE _git_status
  ERROR_VARIABLE  _git_status_err
  RESULT_VARIABLE _git_status_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(_git_status_result EQUAL 0)
  _assert_streq("${_git_status}" ""
    "generator source witness must leave no git diff after restoration")
endif()

message(STATUS "[F1 witness] ============================================================")
message(STATUS "[F1 witness] Results: ${_PASS_COUNT} passed, ${_FAIL_COUNT} failed")

if(_FAIL_COUNT GREATER 0)
  message(STATUS "[F1 witness] Failures:")
  foreach(_msg IN LISTS _FAIL_MSGS)
    message(STATUS "${_msg}")
  endforeach()
  message(FATAL_ERROR
    "[F1 witness] fixpp::dict::codegen-source-staleness-check FAILED "
    "(${_FAIL_COUNT} assertion(s) failed — see above).")
else()
  message(STATUS "[F1 witness] fixpp::dict::codegen-source-staleness-check PASSED "
    "(${_PASS_COUNT} assertions).")
endif()
