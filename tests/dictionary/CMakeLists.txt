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
