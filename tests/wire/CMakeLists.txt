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
  nested_group_slices_failloud_test.cpp                               # T001 (073)
)
# nested_group_slices_failloud_test.cpp (T001) needs FIXPP_TEST_HOOKS to reach
# the nested_cache_access_for_testing friend seam (gate-b/r1 FQ-2 — the friend
# is now gated behind FIXPP_TEST_HOOKS, matching the session-layer test-only
# convention rather than shipping ungated in the installed public header).
target_compile_definitions(wire_pure_tests PRIVATE FIXPP_TEST_HOOKS)
# 079 T006/T007/T010/T011: validator_type_check_test.cpp needs the real
# vendored dictionaries (FIX44/FIX50SP2/FIX42) for the required-presence-
# scope real-frame accept/reject regressions (fixpp#201).
target_compile_definitions(wire_pure_tests PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\"")
# 079 T024: label the bucket `wire` so `ctest -L wire` selects the US1/US2
# real-frame regressions living in validator_type_check_test.cpp (matches the
# wire_dict_tests "075;wire" + required_scope_two_tier_test "079;wire" pattern;
# quickstart.md §2/§6 select this bucket by label, Article VII §8).
set_tests_properties(wire_pure_tests PROPERTIES LABELS "079;wire")

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
  group_view_failloud_test.cpp                                         # T008 (073)
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
  message_view_membership_copy_test.cpp
  validator_enum_domain_test.cpp                                      # T022/T022a/T023 (075)
  validator_enum_multivalue_test.cpp                                  # T027 (075)
  fixt_header_validate_test.cpp                                       # T003/T004 (081)
  delimiter_divergence_wire_test.cpp                                  # T009 (083)
  consume_group_nested_delim_test.cpp)                                # T010 (083)
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
# 075 (tasks.md Test-binary discipline note): sets LABELS on the buckets it
# joins/adds. wire_dict_tests is a grouped add_test binary — one label set
# covers the whole executable, not per-.cpp; the 3 pre-existing members ride
# along (name-selectable via `-R wire_dict_tests` as before, now ALSO
# `ctest -L 075` / `-L wire` selectable).
set_tests_properties(wire_dict_tests PROPERTIES LABELS "075;wire")

# ── Standalone: 075 T007 QuickFIX enum-golden manifest gate ─────────────────
# Isolation-sensitive per [const §VII.8] (a data-driven exact-hash gate) —
# stays standalone rather than joining a grouped bucket. LABELS "075;golden;
# manifest" so it is ctest -L selectable. Links NO QuickFIX and NEVER touches
# reference-engines/: it only hashes checked-in files (dictionaries/*.xml,
# tools/quickfix_enum_golden/main.cpp) and re-parses the checked-in
# golden.csv, so it builds and runs identically whether or not
# reference-engines/ is present on the machine.
find_package(OpenSSL 3.0 REQUIRED)
add_executable(enum_golden_manifest_test enum_golden_manifest_test.cpp)
target_link_libraries(enum_golden_manifest_test PRIVATE
  OpenSSL::Crypto
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(enum_golden_manifest_test PRIVATE
  "FIXPP_GOLDEN_CSV_PATH=\"${CMAKE_SOURCE_DIR}/tools/quickfix_enum_golden/golden.csv\""
  "FIXPP_GOLDEN_MAIN_CPP_PATH=\"${CMAKE_SOURCE_DIR}/tools/quickfix_enum_golden/main.cpp\""
  "FIXPP_GOLDEN_DICT_FIX44_PATH=\"${CMAKE_SOURCE_DIR}/dictionaries/FIX44.xml\""
  "FIXPP_GOLDEN_DICT_FIX41_PATH=\"${CMAKE_SOURCE_DIR}/dictionaries/FIX41.xml\""
  "FIXPP_GOLDEN_DICT_FIX42_PATH=\"${CMAKE_SOURCE_DIR}/dictionaries/FIX42.xml\""
)
add_test(NAME enum_golden_manifest_test COMMAND enum_golden_manifest_test)
set_tests_properties(enum_golden_manifest_test PROPERTIES LABELS "075;golden;manifest")

# ── Standalone: 075 T031 QuickFIX enum-golden PARITY gate (SC-009) ──────────
# Isolation-sensitive per [const §VII.8] (a data-driven exact gate consuming
# the checked-in golden + manifest) — stays standalone rather than joining
# wire_dict_tests. LABELS "075;golden;parity". Links NO QuickFIX: it drives
# fixpp's OWN production validate() (real dictionaries -> as_table_view() ->
# dictionary_driven_validator) over the row data recorded in
# tools/quickfix_enum_golden/golden.csv, so it builds and runs identically
# whether or not reference-engines/ is present (same posture as
# enum_golden_manifest_test).
add_executable(enum_golden_parity_test enum_golden_parity_test.cpp)
target_link_libraries(enum_golden_parity_test PRIVATE
  fixpp_wire
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(enum_golden_parity_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(enum_golden_parity_test PRIVATE
  "FIXPP_GOLDEN_CSV_PATH=\"${CMAKE_SOURCE_DIR}/tools/quickfix_enum_golden/golden.csv\""
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME enum_golden_parity_test COMMAND enum_golden_parity_test)
set_tests_properties(enum_golden_parity_test PROPERTIES LABELS "075;golden;parity")

# ── Standalone: 079-required-presence-scope T019 QuickFIX required-set
# PARITY gate (Contract 2) ───────────────────────────────────────────────
# Isolation-sensitive per [const §VII.8] (a data-driven exact-set gate
# consuming the checked-in golden). Links NO QuickFIX: it re-derives the
# independent oracle (tests/dictionary/required_scope_oracle.hpp, shared with
# the T015-T017 census — single-oracle guarantee) directly from the vendored
# dictionaries/*.xml and compares it against the checked-in
# tools/quickfix_required_golden/golden.csv (produced by T018 against a REAL,
# locally built QuickFIX v1.16.0), so it builds and runs identically whether
# or not reference-engines/ is present (same posture as
# enum_golden_parity_test above).
find_package(pugixml CONFIG REQUIRED)
add_executable(required_scope_parity_test required_scope_parity_test.cpp)
target_link_libraries(required_scope_parity_test PRIVATE
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(required_scope_parity_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests/dictionary")
target_compile_definitions(required_scope_parity_test PRIVATE
  "FIXPP_REQUIRED_GOLDEN_CSV=\"${CMAKE_SOURCE_DIR}/tools/quickfix_required_golden/golden.csv\""
  # 081-strict-validation-residuals T020: per-group parity golden (SEPARATE
  # file from golden.csv -- see tools/quickfix_required_golden/main.cpp's
  # T020 header-comment banner).
  "FIXPP_REQUIRED_GOLDEN_GROUPS_CSV=\"${CMAKE_SOURCE_DIR}/tools/quickfix_required_golden/golden_groups.csv\""
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME required_scope_parity COMMAND required_scope_parity_test)
set_tests_properties(required_scope_parity PROPERTIES LABELS "079;golden;parity;census")

# ── Standalone: 076-fix-latest-typed-codegen T009(b) — vlatest reify
# round-trip (universal reify/read-back leg only; see the .cpp file header
# for the T009(a) Builders.hpp compile-cost escalation). Needs the
# build-tree generated fixpp/vlatest/Reify.hpp (fixpp_codegen_generate,
# defined in tests/codegen, add_subdirectory'd before tests/wire — same
# guard as wire_codegen_tests above). Dict-free (Parser<Index>{} default
# ctor — no fixpp_dictionary link needed); kept standalone rather than
# joining wire_codegen_tests so `ctest -L vlatest_roundtrip` selects only
# this TU, not the other 4 codegen-gated files' unrelated dictionary deps.
# gate-b/r1 P1: also gated on FIXPP_CODEGEN_FIX_LATEST — with the tier OFF,
# cmake/Codegen.cmake removes _codegen/include/fixpp/vlatest/ entirely, so
# this target must not exist or the OFF build fails to compile
# (FR-003/B-1 "build succeeds when OFF").
if(TARGET fixpp_codegen_generate AND FIXPP_CODEGEN_FIX_LATEST)
  add_executable(wire_vlatest_reify_roundtrip_test vlatest_reify_roundtrip_test.cpp)
  add_dependencies(wire_vlatest_reify_roundtrip_test fixpp_codegen_generate)
  target_link_libraries(wire_vlatest_reify_roundtrip_test PRIVATE
    fixpp_wire
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(wire_vlatest_reify_roundtrip_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_BINARY_DIR}/_codegen/include"
  )
  add_test(NAME wire_vlatest_reify_roundtrip_test COMMAND wire_vlatest_reify_roundtrip_test)
  set_tests_properties(wire_vlatest_reify_roundtrip_test PROPERTIES LABELS "076;vlatest_roundtrip")
endif()

# ── Standalone: 075 T044 legacy CHAR->String (pre-FIX.4.2) witness ──────────
# Drives fixpp's OWN production path (real FIX40.xml/FIX41.xml/FIX42.xml/
# FIX44.xml -> XmlLoader -> as_table_view() -> dictionary_driven_validator)
# rather than joining wire_dict_tests, mirroring enum_golden_parity_test's
# posture (isolation-simple, no shared bucket state to reason about).
add_executable(validator_legacy_char_type_test validator_legacy_char_type_test.cpp)
target_link_libraries(validator_legacy_char_type_test PRIVATE
  fixpp_wire
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(validator_legacy_char_type_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(validator_legacy_char_type_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\"")
add_test(NAME validator_legacy_char_type_test COMMAND validator_legacy_char_type_test)
set_tests_properties(validator_legacy_char_type_test PROPERTIES LABELS "075;wire;dictionary")

# ── Standalone: 079-required-presence-scope T013 [US3] — two-tier
# verdict-agreement (Contract 3). Header-only-includes the generated
# v44/v50sp2/vlatest message headers (FIXPP_VALIDATORS_HEADER_ONLY_<Msg>) to
# read fixpp::wire::writer_traits<...>::required_checks directly, so it
# links NO fixpp::validators::* lib — mirroring validator_legacy_char_type_
# test's isolation-simple posture. Needs the build-tree generated
# fixpp/{v44,v50sp2}/messages/*.hpp (fixpp_codegen_generate, defined in
# tests/codegen, add_subdirectory'd before tests/wire — same guard as
# wire_codegen_tests above) UNCONDITIONALLY, and ADDITIONALLY
# fixpp/vlatest/messages/PositionReport.hpp when FIXPP_CODEGEN_FIX_LATEST is
# ON. Gate B r1 F4: the v44/v50sp2 legs are NOT vlatest-gated (v44/v50sp2
# generated headers exist whenever fixpp_codegen_generate does), so the
# earlier `AND FIXPP_CODEGEN_FIX_LATEST` on the WHOLE target silently
# dropped the v44 end-to-end two-tier check and the v50sp2 derivation check
# too when the option was OFF — register the target whenever
# fixpp_codegen_generate exists; the TU itself #ifdef-guards ONLY the
# vlatest include + TEST on the FIXPP_TEST_HAS_VLATEST macro defined below
# (mirrors wire_vlatest_reify_roundtrip_test's guard for the vlatest-only
# case above — with the tier OFF, cmake/Codegen.cmake removes
# _codegen/include/fixpp/vlatest/ entirely, so the TU must not #include it).
if(TARGET fixpp_codegen_generate)
  add_executable(required_scope_two_tier_test required_scope_two_tier_test.cpp)
  add_dependencies(required_scope_two_tier_test fixpp_codegen_generate)
  target_link_libraries(required_scope_two_tier_test PRIVATE
    fixpp_wire
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(required_scope_two_tier_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_BINARY_DIR}/_codegen/include"
  )
  target_compile_definitions(required_scope_two_tier_test PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
    "FIXPP_ORCHESTRA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries/orchestra\"")
  if(FIXPP_CODEGEN_FIX_LATEST)
    target_compile_definitions(required_scope_two_tier_test PRIVATE FIXPP_TEST_HAS_VLATEST)
  endif()
  add_test(NAME required_scope_two_tier_test COMMAND required_scope_two_tier_test)
  set_tests_properties(required_scope_two_tier_test PROPERTIES LABELS "079;wire;two-tier")
endif()
