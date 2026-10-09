# tests/sync/run_asio_recycler_negative_compile.cmake — fixpp#544 (B35;
# `.specify/544-hot-path-zero-alloc.md` §2.4, "The mechanical guards"), the driver of the
# ctest asio_recycler_guard_negative_compile.
#
# It syntax-checks one TU that includes an installed fixpp header that includes asio
# (fixtures/asio_recycler_guard/negative_compile.cpp), three times, with every include
# directory and definition the in-tree asio::asio carries except ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE:
#   omitted     the compile must FAIL and its output must carry the guard's token;
#   N + 1       the compile must FAIL and its output must carry the guard's token;
#   N           the compile must SUCCEED, so the TU is well-formed but for the definition.
# A failure without the token is a failure for another reason, and fails this test.
#
# The token is spelled here, not read from the guard header, so that a change to the
# guard's message has to be made in both places.
#
# Required -D inputs: FIXPP_CXX, FIXPP_CXX_ID, FIXPP_CXX_FLAGS ('|'-separated, may be empty),
# FIXPP_INCLUDE_DIRS and FIXPP_DEFINES ('|'-separated), FIXPP_TU, FIXPP_CACHE_SIZE.

set(_token "FIXPP_ASIO_RECYCLER_ODR")

foreach(_var FIXPP_CXX FIXPP_CXX_ID FIXPP_INCLUDE_DIRS FIXPP_TU FIXPP_CACHE_SIZE)
  if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
    message(FATAL_ERROR "run_asio_recycler_negative_compile.cmake: -D${_var}=... is required")
  endif()
endforeach()
if(NOT FIXPP_CACHE_SIZE MATCHES "^[0-9]+$")
  message(FATAL_ERROR "FIXPP_CACHE_SIZE='${FIXPP_CACHE_SIZE}' is not a number")
endif()

string(REPLACE "|" ";" _flags "${FIXPP_CXX_FLAGS}")
string(REPLACE "|" ";" _incs "${FIXPP_INCLUDE_DIRS}")
string(REPLACE "|" ";" _defs "${FIXPP_DEFINES}")

if(FIXPP_CXX_ID STREQUAL "MSVC")
  set(_base /nologo /std:c++latest /EHsc /Zs)
  set(_inc_flag /I)
  set(_def_flag /D)
else()
  set(_base -std=c++23 -fsyntax-only)
  set(_inc_flag -I)
  set(_def_flag -D)
endif()

set(_args ${_flags} ${_base})
foreach(_d IN LISTS _incs)
  list(APPEND _args "${_inc_flag}${_d}")
endforeach()
foreach(_d IN LISTS _defs)
  if(_d MATCHES "^ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE(=|$)")
    message(FATAL_ERROR "FIXPP_DEFINES carries ${_d}: the driver must be given the set without it")
  endif()
  list(APPEND _args "${_def_flag}${_d}")
endforeach()

math(EXPR _wrong "${FIXPP_CACHE_SIZE} + 1")
set(_failed "")

function(_compile _label _extra _expect_fail)
  execute_process(
    COMMAND "${FIXPP_CXX}" ${_args} ${_extra} "${FIXPP_TU}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err)
  set(_all "${_out}\n${_err}")
  string(FIND "${_all}" "${_token}" _at)
  if(_expect_fail)
    if(_rc EQUAL 0)
      message(STATUS "[asio-recycler-negative] FAIL ${_label}: the compile succeeded")
      set(_failed "${_failed};${_label}" PARENT_SCOPE)
    elseif(_at EQUAL -1)
      message(STATUS "[asio-recycler-negative] FAIL ${_label}: the compile failed (exit ${_rc}) "
                     "without the token ${_token}:\n${_all}")
      set(_failed "${_failed};${_label}" PARENT_SCOPE)
    else()
      message(STATUS "[asio-recycler-negative] PASS ${_label}: failed (exit ${_rc}) on the guard")
    endif()
  else()
    if(NOT _rc EQUAL 0)
      message(STATUS "[asio-recycler-negative] FAIL ${_label}: the compile failed (exit ${_rc}):\n${_all}")
      set(_failed "${_failed};${_label}" PARENT_SCOPE)
    else()
      message(STATUS "[asio-recycler-negative] PASS ${_label}: compiled")
    endif()
  endif()
endfunction()

_compile("definition omitted" "" TRUE)
_compile("definition ${_wrong}" "${_def_flag}ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=${_wrong}" TRUE)
_compile("definition ${FIXPP_CACHE_SIZE}" "${_def_flag}ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=${FIXPP_CACHE_SIZE}" FALSE)

if(_failed)
  message(FATAL_ERROR "asio_recycler_guard_negative_compile: failed cases:${_failed}")
endif()
message(STATUS "asio_recycler_guard_negative_compile: OK")
