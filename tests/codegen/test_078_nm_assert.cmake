# Shared parameterized `nm` discrimination-check script for 078's builder/
# validator split witnesses. Runs `nm [-C] --defined-only <FIXPP_TARGET>` and
# asserts FIXPP_PRESENT/FIXPP_ABSENT regex patterns against the defined-symbol
# output; if FIXPP_UNDEFINED_PRESENT is set, also runs a second `nm [-C]
# <FIXPP_TARGET>` pass (no --defined-only) and asserts those patterns against
# the full output, anchored on "U .*<pattern>" (proves the reference wasn't
# optimized away).
#
# Demangled (-C) + namespace-anchored patterns are the DEFAULT convention: a
# bare mangled substring (e.g. "validate_") false-positives against an
# unrelated real symbol once a witness links fixpp_wire/fixpp_dictionary
# (fixpp::wire::body_builder::validate_group_grammar is a genuine, unrelated
# "validate_" match -- see 078 T024 discrimination note). FIXPP_DEMANGLE=OFF
# is an explicit opt-out for a witness whose mock binary never links real
# fixpp_wire and deliberately checks the raw mangled substring.
#
# Invoked as:
#   cmake -DFIXPP_NM=<nm> -DFIXPP_TARGET=<path>
#         [-DFIXPP_PRESENT=<pat1>;<pat2>...] [-DFIXPP_ABSENT=<pat1>;<pat2>...]
#         [-DFIXPP_UNDEFINED_PRESENT=<pat1>;<pat2>...] [-DFIXPP_DEMANGLE=ON|OFF]
#         -P test_078_nm_assert.cmake

if(NOT FIXPP_NM)
  message(FATAL_ERROR "FIXPP_NM not set")
endif()
if(NOT FIXPP_TARGET)
  message(FATAL_ERROR "FIXPP_TARGET not set")
endif()
if(NOT DEFINED FIXPP_DEMANGLE)
  set(FIXPP_DEMANGLE ON)
endif()

if(FIXPP_DEMANGLE)
  set(_nm_defined_args -C --defined-only)
  set(_nm_all_args -C)
else()
  set(_nm_defined_args --defined-only)
  set(_nm_all_args "")
endif()

execute_process(
  COMMAND "${FIXPP_NM}" ${_nm_defined_args} "${FIXPP_TARGET}"
  OUTPUT_VARIABLE nm_defined_output
  RESULT_VARIABLE nm_defined_result
)
if(NOT nm_defined_result EQUAL 0)
  message(FATAL_ERROR "nm ${_nm_defined_args} failed on ${FIXPP_TARGET} (exit ${nm_defined_result}):\n${nm_defined_output}")
endif()

foreach(_pat IN LISTS FIXPP_PRESENT)
  if(NOT nm_defined_output MATCHES "${_pat}")
    message(FATAL_ERROR "expected DEFINED symbol matching '${_pat}' missing from ${FIXPP_TARGET}:\n${nm_defined_output}")
  endif()
endforeach()

foreach(_pat IN LISTS FIXPP_ABSENT)
  if(nm_defined_output MATCHES "${_pat}")
    message(FATAL_ERROR "${FIXPP_TARGET} unexpectedly carries a DEFINED symbol matching '${_pat}':\n${nm_defined_output}")
  endif()
endforeach()

if(FIXPP_UNDEFINED_PRESENT)
  execute_process(
    COMMAND "${FIXPP_NM}" ${_nm_all_args} "${FIXPP_TARGET}"
    OUTPUT_VARIABLE nm_all_output
    RESULT_VARIABLE nm_all_result
  )
  if(NOT nm_all_result EQUAL 0)
    message(FATAL_ERROR "nm ${_nm_all_args} failed on ${FIXPP_TARGET} (exit ${nm_all_result}):\n${nm_all_output}")
  endif()
  foreach(_pat IN LISTS FIXPP_UNDEFINED_PRESENT)
    if(NOT nm_all_output MATCHES "U .*${_pat}")
      message(FATAL_ERROR "expected UNDEFINED reference matching '${_pat}' missing from ${FIXPP_TARGET} -- the call site may have been optimized away:\n${nm_all_output}")
    endif()
  endforeach()
endif()

message(STATUS "078 nm-assert OK on ${FIXPP_TARGET}")
