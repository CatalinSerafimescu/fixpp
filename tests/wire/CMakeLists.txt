# tests/wire/CMakeLists.txt — 004-wire-codec test suite.
# 068-test-binary-grouping: isolation-safe wire tests are grouped into
# whole-binary add_test executables (bucketed by shared link-deps; none of
# the 34 carry LABELS, so `-L`/`-R '^wire_'` selection is untouched at the
# binary-name level). test_body_builder.cpp (in-TU global operator new/
# operator new[] allocation counter) stays standalone. Dispositions in
# specs/068-test-binary-grouping/dispositions.md.
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

# ── Grouped bucket: pure/stateless wire tests (28 .cpp → 1 binary) ───────────
# Includes the former standalone R6 smoke (wire_smoke_test.cpp — no ENV
# reference, so the homogeneous FIXPP_WIRE_CONFORMANCE_DIR ENV is a harmless
# no-op for it) and the seam #2 conformance driver + fork-based EXPECT_DEATH
# lifetime traps (view_test.cpp / lifetime_trap_test.cpp — groupable, D3) +
# three_arena_pinning_test.cpp (local pmr counting_resource, no global new).
add_wire_test(wire_pure_tests
  wire_smoke_test.cpp                                                  # R6
  view_test.cpp                                                        # T010
  conformance/conformance_test.cpp                                     # T009
  framer_partial_read_test.cpp                                         # T038
  checksum_bodylength_corruption_test.cpp                              # T039
  parser_index_test.cpp                                                # T012
  parser_iter_test.cpp                                                 # T013
  offset_table_test.cpp                                                # T014
  lifetime_trap_test.cpp                                               # T015
  unknown_fields_test.cpp                                              # T017
  three_arena_pinning_test.cpp                                         # T018
  noexcept_trap_test.cpp                                               # T020
  round_trip_property_test.cpp                                        # T033
  validator_domain_test.cpp                                           # T043
  validator_per_version_test.cpp                                      # T044
  writer_error_path_test.cpp                                          # T055
  framer_error_path_test.cpp                                          # T055
  parser_error_path_test.cpp                                          # T055
  validator_type_check_test.cpp                                       # T055
  offset_table_error_path_test.cpp                                    # T056 cov
  accessor_smoke_test.cpp                                             # T055-R2
  tag_scan_test.cpp                                                   # T004 (040)
  hostile_input_hardening_test.cpp                                    # wire-hostile-input-review
  offset_table_overflow_test.cpp                                      # T008/T012 (040)
  parser_overflow_test.cpp                                            # T009/T012 (040)
  group_slice_trailing_soh_test.cpp                                   # T008 (062)
  nested_group_slices_cache_test.cpp                                  # /speckit-verify cov (062)
  nested_group_extent_test.cpp                                        # T019/T020/T022 (063)
)

# ── Standalone: 061-slim body-builder alloc gate (FR-002) ────────────────────
# in-TU global ::operator new / ::operator new[] allocation counter (D3).
add_wire_test(wire_body_builder_test test_body_builder.cpp)            # 061-slim T005/T006

# ── Grouped bucket: codegen-gated wire tests (3 .cpp → 1 binary) ─────────────
# Each needs the build-tree generated fixpp/v<XX>/Messages.hpp or Reify.hpp;
# fixpp_codegen_generate is defined in tests/codegen, add_subdirectory'd
# before tests/wire. Target creation stays unconditional (matches the
# pre-068 per-.cpp behavior); only the codegen include dir / fixpp_dictionary
# link / add_dependencies are gated, so the built set is unchanged (FR-007).
add_wire_test(wire_codegen_tests
  repeating_group_equivalence_test.cpp                                 # T016 (062 T010)
  cutover_2b_gated_test.cpp                                            # T019 (T059)
  toplevel_read_regression_test.cpp                                    # 062 T022
)
if(TARGET fixpp_codegen_generate)
  add_dependencies(wire_codegen_tests fixpp_codegen_generate)
  target_include_directories(wire_codegen_tests PRIVATE
    "${CMAKE_BINARY_DIR}/_codegen/include")
  target_link_libraries(wire_codegen_tests PRIVATE fixpp_dictionary)
endif()

# ── Grouped bucket: dictionary-linked wire tests (2 .cpp → 1 binary) ─────────
# 041-validation-gate-wiring T009/T009a (compile-witness, production
# table_view, no mock + Float parse-error remap witness) + 066-dict-backed-
# inbound-parse T003 (MessageView::membership_copy() identity/outlives
# witness). Both link fixpp_dictionary for live XmlLoader::load_from_string
# — not registered under the pure wire bucket to avoid pulling dict deps
# into wire-only suites (pre-existing rationale, preserved). Neither
# references FIXPP_DICT_DATA_DIR (inline XML via load_from_string); the
# compile-def is unioned onto the bucket as a harmless no-op (pre-existing
# on the first member, not removed).
add_executable(wire_dict_tests
  validator_production_table_view_test.cpp
  validator_nested_membership_test.cpp
  message_view_membership_copy_test.cpp)
target_link_libraries(wire_dict_tests PRIVATE
  fixpp_wire
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main)
target_include_directories(wire_dict_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(wire_dict_tests PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\"")
add_test(NAME wire_dict_tests COMMAND wire_dict_tests)
