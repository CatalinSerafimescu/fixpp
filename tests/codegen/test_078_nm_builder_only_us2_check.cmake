# 078 T024 [US2] discrimination check: `nm -C --defined-only` (demangled) on
# the builder-only-linked binary must show zero fixpp::v44::validate_<Msg> /
# writer_traits symbols (SC-003/FR-005).
#
# Deliberately does NOT reuse test_078_odr_nm_check.cmake (the T002 mock
# probe's check): that script greps the RAW MANGLED name for the bare
# substring "validate_", which is a false positive against a binary that
# also links real fixpp_wire -- fixpp::wire::body_builder carries a
# genuinely-named validate_group_grammar() member (unrelated wire-grammar
# validation, not a generated validate_<Msg>). The T002 mock binary never
# links fixpp_wire, so this collision never surfaced there. Demangling
# (`nm -C`) and anchoring on the `fixpp::v44::validate_` namespace-qualified
# prefix distinguishes the generated free function from
# fixpp::wire::body_builder::validate_group_grammar (confirmed empirically:
# see 078 T024 discrimination note).
#
# Invoked as: cmake -DFIXPP_NM=<nm> -DFIXPP_BINARY=<path> -P test_078_nm_builder_only_us2_check.cmake

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
  message(FATAL_ERROR "nm -C failed on ${FIXPP_BINARY} (exit ${nm_result}):\n${nm_output}")
endif()

if(nm_output MATCHES "fixpp::v44::validate_|writer_traits")
  message(FATAL_ERROR
    "builder-only-linked binary carries validator symbols (SC-003/FR-005 violation):\n${nm_output}")
endif()

message(STATUS "078 T024 US2 OK -- zero fixpp::v44::validate_/writer_traits symbols in ${FIXPP_BINARY}")
