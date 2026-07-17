# 078 T028 [US3] discrimination check: nm -C --defined-only (demangled) on
# the linked binary for test_078_nm_link_only_us3.cpp must show ONLY the 3
# called v44 build_ symbols defined, and the un-called 4th
# (fixpp::v44::build_OrderCancelRequest -- never referenced by that TU)
# ABSENT -- proving per-message .o archive granularity (SC-002): the linker
# pulled only the 3 referenced .builder.o members out of fixpp_builders_v44.a,
# not the whole version-wide builder set.
#
# Demangled + namespace-qualified, per test_078_nm_builder_only_us2_check.cmake
# -- a bare mangled "build_" substring is not the risk here (SC-002 is an
# ABSENCE check on one specific symbol name, not a whole-class substring), but
# anchoring on the namespace-qualified name keeps this script consistent with
# the rest of 078's nm checks and avoids any future collision.
#
# Invoked as: cmake -DFIXPP_NM=<nm> -DFIXPP_BINARY=<path> -P test_078_nm_link_only_us3_check.cmake

if(NOT FIXPP_NM)
  message(FATAL_ERROR "FIXPP_NM not set")
endif()
if(NOT FIXPP_BINARY)
  message(FATAL_ERROR "FIXPP_BINARY not set")
endif()

execute_process(
  COMMAND "${FIXPP_NM}" -C --defined-only "${FIXPP_BINARY}"
  OUTPUT_VARIABLE nm_output
  RESULT_VARIABLE nm_result
)
if(NOT nm_result EQUAL 0)
  message(FATAL_ERROR "nm -C --defined-only failed on ${FIXPP_BINARY} (exit ${nm_result}):\n${nm_output}")
endif()

foreach(_sym IN ITEMS
    "fixpp::v44::build_NewOrderSingle"
    "fixpp::v44::build_ExecutionReport"
    "fixpp::v44::build_News")
  if(NOT nm_output MATCHES "${_sym}")
    message(FATAL_ERROR "expected DEFINED symbol ${_sym} missing from ${FIXPP_BINARY}:\n${nm_output}")
  endif()
endforeach()

if(nm_output MATCHES "fixpp::v44::build_OrderCancelRequest")
  message(FATAL_ERROR
    "link-only-3 binary carries fixpp::v44::build_OrderCancelRequest, which "
    "this TU never calls or references -- the linker pulled in more than the "
    "3 referenced .builder.o archive members (SC-002 violation):\n${nm_output}")
endif()

message(STATUS "078 T028 US3 OK -- exactly the 3 called v44 build_ symbols defined, build_OrderCancelRequest absent, in ${FIXPP_BINARY}")
