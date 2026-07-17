# 078 T026 [US3] discrimination check: nm on the COMPILED OBJECT (before it
# is folded into the final linked binary) for test_078_builder_mixing_us3.cpp
# must show fixpp::v44::build_NewOrderSingle DEFINED locally (the force-
# inlined body really was emitted into this TU) AND fixpp::v44::
# build_ExecutionReport UNDEFINED in that same object (its body resolves
# only at final link, from the linked fixpp::builders::v44 archive). This is
# the positive proof that per-message mixing actually happened as designed
# -- not merely that the final binary happened to link (FR-006/FR-007).
#
# Demangled (`nm -C`), anchored on the namespace-qualified name -- see
# test_078_nm_builder_only_us2_check.cmake for why a bare substring match is
# unsafe against a binary that also links fixpp_wire/fixpp_dictionary.
#
# Invoked as: cmake -DFIXPP_NM=<nm> -DFIXPP_OBJECT=<obj> -P test_078_builder_mixing_us3_object_nm_check.cmake

if(NOT FIXPP_NM)
  message(FATAL_ERROR "FIXPP_NM not set")
endif()
if(NOT FIXPP_OBJECT)
  message(FATAL_ERROR "FIXPP_OBJECT not set")
endif()

execute_process(
  COMMAND "${FIXPP_NM}" -C --defined-only "${FIXPP_OBJECT}"
  OUTPUT_VARIABLE nm_defined_output
  RESULT_VARIABLE nm_defined_result
)
if(NOT nm_defined_result EQUAL 0)
  message(FATAL_ERROR "nm -C --defined-only failed on ${FIXPP_OBJECT} (exit ${nm_defined_result}):\n${nm_defined_output}")
endif()

if(NOT nm_defined_output MATCHES "fixpp::v44::build_NewOrderSingle")
  message(FATAL_ERROR
    "mixing-witness object has no DEFINED fixpp::v44::build_NewOrderSingle -- "
    "the force-inline macro did not emit the body into this TU (FR-006 violation):\n${nm_defined_output}")
endif()

if(nm_defined_output MATCHES "fixpp::v44::build_ExecutionReport")
  message(FATAL_ERROR
    "mixing-witness object DEFINES fixpp::v44::build_ExecutionReport locally -- "
    "it should resolve only at final link from the linked archive, not be "
    "compiled into this TU (mixing did not happen as designed):\n${nm_defined_output}")
endif()

execute_process(
  COMMAND "${FIXPP_NM}" -C "${FIXPP_OBJECT}"
  OUTPUT_VARIABLE nm_all_output
  RESULT_VARIABLE nm_all_result
)
if(NOT nm_all_result EQUAL 0)
  message(FATAL_ERROR "nm -C failed on ${FIXPP_OBJECT} (exit ${nm_all_result}):\n${nm_all_output}")
endif()

if(NOT nm_all_output MATCHES "U .*fixpp::v44::build_ExecutionReport")
  message(FATAL_ERROR
    "mixing-witness object has no UNDEFINED fixpp::v44::build_ExecutionReport "
    "reference -- the call site may have been optimized away, making the "
    "\"resolves from the lib\" leg vacuous:\n${nm_all_output}")
endif()

message(STATUS "078 T026 US3 OK -- build_NewOrderSingle defined locally, build_ExecutionReport undefined (resolves from lib), in ${FIXPP_OBJECT}")
