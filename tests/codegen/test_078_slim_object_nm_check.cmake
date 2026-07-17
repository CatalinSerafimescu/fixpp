# 078 T020 (US1 AC1) discrimination check: nm on the compiled OBJECT (not the
# linked binary) for test_078_slim_compile_us1.cpp must show ZERO defined
# `build_` symbols (the slim header only declares build_NewOrderSingle; its
# body lives entirely in fixpp::builders::v44, resolved only at link time) and
# an UNDEFINED reference to build_NewOrderSingle (proving the call site is
# real, not optimized away).
#
# Invoked as: cmake -DFIXPP_NM=<nm> -DFIXPP_OBJECT=<obj> -P test_078_slim_object_nm_check.cmake

if(NOT FIXPP_NM)
  message(FATAL_ERROR "FIXPP_NM not set")
endif()
if(NOT FIXPP_OBJECT)
  message(FATAL_ERROR "FIXPP_OBJECT not set")
endif()

execute_process(
  COMMAND "${FIXPP_NM}" --defined-only "${FIXPP_OBJECT}"
  OUTPUT_VARIABLE nm_defined_output
  RESULT_VARIABLE nm_defined_result
)
if(NOT nm_defined_result EQUAL 0)
  message(FATAL_ERROR "nm --defined-only failed on ${FIXPP_OBJECT} (exit ${nm_defined_result}):\n${nm_defined_output}")
endif()

if(nm_defined_output MATCHES "build_")
  message(FATAL_ERROR
    "slim-compile object carries a DEFINED build_ symbol (US1 AC1 violation -- "
    "body should live only in the linked lib):\n${nm_defined_output}")
endif()

execute_process(
  COMMAND "${FIXPP_NM}" "${FIXPP_OBJECT}"
  OUTPUT_VARIABLE nm_all_output
  RESULT_VARIABLE nm_all_result
)
if(NOT nm_all_result EQUAL 0)
  message(FATAL_ERROR "nm failed on ${FIXPP_OBJECT} (exit ${nm_all_result}):\n${nm_all_output}")
endif()

if(NOT nm_all_output MATCHES "U .*build_NewOrderSingle")
  message(FATAL_ERROR
    "slim-compile object has no UNDEFINED build_NewOrderSingle reference -- "
    "the call site may have been optimized away, making this witness vacuous:\n${nm_all_output}")
endif()

message(STATUS "078 T020 US1 AC1 OK -- zero defined build_ symbols, one undefined build_NewOrderSingle reference in ${FIXPP_OBJECT}")
