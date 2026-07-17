# 078 R2a leg (ii): nm --defined-only on the builder-only mock binary must
# show zero validate_/writer_traits symbols (SC-003).
#
# Invoked as: cmake -DFIXPP_NM=<nm> -DFIXPP_BINARY=<path> -P test_078_odr_nm_check.cmake

if(NOT FIXPP_NM)
  message(FATAL_ERROR "FIXPP_NM not set")
endif()
if(NOT FIXPP_BINARY)
  message(FATAL_ERROR "FIXPP_BINARY not set")
endif()

execute_process(
  COMMAND "${FIXPP_NM}" --defined-only "${FIXPP_BINARY}"
  OUTPUT_VARIABLE nm_output
  RESULT_VARIABLE nm_result
)
if(NOT nm_result EQUAL 0)
  message(FATAL_ERROR "nm failed on ${FIXPP_BINARY} (exit ${nm_result}):\n${nm_output}")
endif()

if(nm_output MATCHES "validate_|writer_traits")
  message(FATAL_ERROR
    "builder-only binary carries validator symbols (SC-003 violation):\n${nm_output}")
endif()

message(STATUS "078 R2a leg (ii) OK — zero validate_/writer_traits symbols in ${FIXPP_BINARY}")
