# tests/dictionary/CMakeLists.txt
# 002-dictionary-xml-loader test targets per quickstart.md §2. Each test
# target is labelled `dictionary` so `ctest -L dictionary` exercises the
# whole suite.

find_package(GTest CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

# Path to vendored XML fixtures so tests can locate them regardless of
# build-tree depth (used by load(path, mr) AC-L1 / AC-L2 paths and the
# round-trip / determinism / dictionary_lookup suites).
set(FIXPP_DICT_DATA_DIR "${CMAKE_SOURCE_DIR}/dictionaries")

set(FIXPP_DICT_TESTS
  dictionary_ref_shape_test
  dictionary_xml_loader_test
  dictionary_pmr_allocation_test
  dictionary_round_trip_test
  dictionary_determinism_test
  dictionary_concurrent_readers_test
  dictionary_negative_paths_test
  dictionary_parser_error_test
  dictionary_oom_injection_test
  dictionary_lookup_test
  # 003-dictionary-codegen — Foundational shape tests T004-T006 (RC#1).
  dictionary_version_profile_test
  dictionary_field_traits_test
  dictionary_version_registry_test
  # 041-validation-gate-wiring T007/T008: as_table_view() builder tests.
  dictionary_table_view_test
  # 063-nested-group-parse-correctness T010 [US1]: Defect-A context-scoped
  # group-membership discriminating guard (real FIX44.xml, MassQuote 295).
  dictionary_defect_a_group_context_test
  # 063 T016 [US1]: loader-faithful reused-NumInGroup-tag census over all
  # nine runtime XMLs (collision report + table-size report, [pin#3-report]).
  dictionary_reused_tag_census_test
  # 063 T017 [US1]: per-collision discriminating regression guards, driven
  # by the T016 census, parameterized across all nine dict families.
  dictionary_collision_membership_guards_test
  # 063 T018 [US1]: no-alloc gate for repeated group_member_fn / context-
  # store lookups [pin#3-noalloc].
  dictionary_group_context_lookup_alloc_gate_test
)

foreach(target IN LISTS FIXPP_DICT_TESTS)
  string(REGEX REPLACE "^dictionary_" "" src_stem "${target}")
  add_executable(${target} "${src_stem}.cpp")
  target_link_libraries(${target} PRIVATE
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
  )
  target_compile_definitions(${target} PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
  )
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES LABELS dictionary)
endforeach()

# T028: round_trip_test directly parses XML fixtures via pugixml to build the
# ground-truth (msg_type, tag) pair set for exact exhaustiveness comparison.
target_link_libraries(dictionary_round_trip_test PRIVATE pugixml::pugixml)

# 063 T016: the census's delimiter-variance scan (question 2) directly parses
# the raw XML via pugixml (bounded read-only scan; see file header) — same
# precedent as dictionary_round_trip_test above.
target_link_libraries(dictionary_reused_tag_census_test PRIVATE pugixml::pugixml)

# 063 T018: mallocnesia gate for the group-context lookup no-alloc test —
# re-runs it under LD_PRELOAD so global malloc/calloc/realloc is intercepted
# (dual gate alongside the in-TU global operator-new counter; absent
# libmallocnesia.so → skipped, per feedback_tracking_pmr_resource_false_pass).
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME dictionary_group_context_lookup_alloc_gate_test_mallocnesia
    COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:dictionary_group_context_lookup_alloc_gate_test>")
  set_tests_properties(dictionary_group_context_lookup_alloc_gate_test_mallocnesia PROPERTIES
    LABELS "dictionary;alloc_guard;mallocnesia;063")
endif()

# ── US2 Reify tests (T028-T031) ─────────────────────────────────────────────
# These tests include generated <vXX>/Reify.hpp (build-tree only, AC-C4).
# fixpp_codegen_generate is defined in tests/codegen/CMakeLists.txt, which is
# add_subdirectory'd BEFORE tests/dictionary in the top-level CMakeLists.txt
# (reordered at T028-T031 — previously dictionary came first).
# The generated include dir is ${CMAKE_BINARY_DIR}/_codegen/include (mirrors
# FIXPP_CODEGEN_OUT in tests/codegen/CMakeLists.txt).

if(TARGET fixpp_codegen_generate)
  set(_REIFY_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

  set(FIXPP_REIFY_TESTS
    reify_test
    reify_move_test
    reify_cross_strand_test
    reify_oom_test
    # T034 [US3]: runtime-dispatch + FIXT cross-vocabulary dispatch shape tests
    # (seam #15a/#15b/#15c/#10c; AC-D1..D3/D6/D7). Dispatch-consuming TU —
    # includes the generated _dispatch/ headers (Entity 8 / I-11).
    reify_dispatch_test
  )

  foreach(target IN LISTS FIXPP_REIFY_TESTS)
    add_executable(dictionary_${target} "${target}.cpp")
    add_dependencies(dictionary_${target} fixpp_codegen_generate)
    target_link_libraries(dictionary_${target} PRIVATE
      fixpp_dictionary
      GTest::gtest
      GTest::gtest_main
    )
    target_include_directories(dictionary_${target} PRIVATE
      "${_REIFY_CODEGEN_INC}"
      "${CMAKE_SOURCE_DIR}/tests"
    )
    target_compile_definitions(dictionary_${target} PRIVATE
      "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
    )
    add_test(NAME dictionary_${target} COMMAND dictionary_${target})
    set_tests_properties(dictionary_${target} PROPERTIES LABELS dictionary)
  endforeach()

  # reify_cross_strand_test is a TSan target per [const §IX.2] / AC-R5/AC-T3.
  set_tests_properties(dictionary_reify_cross_strand_test PROPERTIES
    LABELS "dictionary;tsan")
else()
  message(STATUS
    "tests/dictionary: fixpp_codegen_generate not available; skipping US2 reify tests")
endif()
