# tests/wire/CMakeLists.txt — 004-wire-codec test suite.
# Every wire test registers under the `wire_` CTest name prefix so
# `ctest --preset linux-clang-debug -R '^wire_'` selects the full seam set
# (quickstart §2). ASan/UBSan/TSan/coverage are driven by the active preset.
find_package(GTest CONFIG REQUIRED)

# Shared corpus dir for the seam #2 conformance driver (T009).
set(WIRE_CONFORMANCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/conformance")

# helper: add a wire_ test executable linking fixpp::wire + GTest
function(add_wire_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE
    fixpp_wire
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${name} PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES
    ENVIRONMENT "FIXPP_WIRE_CONFORMANCE_DIR=${WIRE_CONFORMANCE_DIR}")
endfunction()

# Existing R6 smoke (kept).
add_executable(wire_smoke_test wire_smoke_test.cpp)
target_link_libraries(wire_smoke_test PRIVATE
  fixpp_wire GTest::gtest GTest::gtest_main)
add_test(NAME wire_smoke COMMAND wire_smoke_test)

# Foundational (Phase 2)
add_wire_test(wire_view_test view_test.cpp)                            # T010
add_wire_test(wire_conformance_test conformance/conformance_test.cpp)  # T009

# US1 / US2 / US3 / US4 test executables are registered as their
# corresponding test tasks land (T011.. red-first per TDD).
add_wire_test(wire_framer_partial_read_test
  framer_partial_read_test.cpp)                                        # T038
add_wire_test(wire_checksum_bodylength_corruption_test
  checksum_bodylength_corruption_test.cpp)                             # T039
add_wire_test(wire_parser_index_test parser_index_test.cpp)            # T012
add_wire_test(wire_parser_iter_test parser_iter_test.cpp)              # T013
add_wire_test(wire_offset_table_test offset_table_test.cpp)            # T014
add_wire_test(wire_lifetime_trap_test lifetime_trap_test.cpp)          # T015
add_wire_test(wire_repeating_group_equivalence_test
  repeating_group_equivalence_test.cpp)                                # T016
add_wire_test(wire_unknown_fields_test unknown_fields_test.cpp)        # T017
add_wire_test(wire_three_arena_pinning_test
  three_arena_pinning_test.cpp)                                        # T018
add_wire_test(wire_cutover_2b_gated_test cutover_2b_gated_test.cpp)    # T019
# T059: the cutover gate now exercises the 003 owning deep-copy round-trip,
# so it needs the build-tree generated <vXX>/Reify.hpp + fixpp_dictionary
# (fixpp_dictionary↔fixpp_wire is the permitted post-cutover static-archive
# cycle, T028). fixpp_codegen_generate is defined in tests/codegen, which is
# add_subdirectory'd before tests/wire.
if(TARGET fixpp_codegen_generate)
  add_dependencies(wire_cutover_2b_gated_test fixpp_codegen_generate)
  target_include_directories(wire_cutover_2b_gated_test PRIVATE
    "${CMAKE_BINARY_DIR}/_codegen/include")
  target_link_libraries(wire_cutover_2b_gated_test PRIVATE fixpp_dictionary)
endif()
add_wire_test(wire_noexcept_trap_test noexcept_trap_test.cpp)          # T020

# US2 — Writer / serialize / round-trip (T032–T033)
add_wire_test(wire_round_trip_property_test
  round_trip_property_test.cpp)                                        # T033

# US4 — Validator domain (seam #14) + per-version parameterized suite (SC-005)
add_wire_test(wire_validator_domain_test validator_domain_test.cpp)        # T043
add_wire_test(wire_validator_per_version_test validator_per_version_test.cpp) # T044

# T055 — Coverage hardening: targeted error-path tests for writer / framer /
# parser / validator to reach ≥90% line / ≥80% branch on wire scope.
add_wire_test(wire_writer_error_path_test writer_error_path_test.cpp)         # T055
add_wire_test(wire_framer_error_path_test framer_error_path_test.cpp)         # T055
add_wire_test(wire_parser_error_path_test parser_error_path_test.cpp)         # T055
add_wire_test(wire_validator_type_check_test validator_type_check_test.cpp)   # T055
