# tests/codegen/CMakeLists.txt
# 003-dictionary-codegen — US1 typed-accessor / shape suites (T017-T022).
#
# Generated headers are produced at CMake CONFIGURE time by cmake/Codegen.cmake
# (T016 — [2c §7.6] configure-time graph, AC-C4 / AC-T2).  The
# fixpp_codegen_generate target is defined in cmake/Codegen.cmake (no-op at
# build time; headers already on disk after configure).  This file no longer
# defines the code-generation custom command — that is the single configure-time
# mechanism owned by Codegen.cmake.

if(NOT TARGET fixpp_codegen_generate)
  message(STATUS "tests/codegen: fixpp_codegen_generate not available; skipping US1 suites")
  return()
endif()

find_package(GTest CONFIG REQUIRED)

set(FIXPP_CODEGEN_OUT "${CMAKE_BINARY_DIR}/_codegen/include")

set(FIXPP_CODEGEN_TESTS
  codegen_typed_accessor_test
  codegen_flyweight_shape_test
  codegen_msgtype_boundary_test
  codegen_validator_shape_test
  codegen_length_data_table_test
)

foreach(target IN LISTS FIXPP_CODEGEN_TESTS)
  string(REGEX REPLACE "^codegen_" "" src_stem "${target}")
  add_executable(${target} "${src_stem}.cpp")
  add_dependencies(${target} fixpp_codegen_generate)
  target_link_libraries(${target} PRIVATE
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${target} PRIVATE
    "${FIXPP_CODEGEN_OUT}"
    "${CMAKE_SOURCE_DIR}/tests"
  )
  target_compile_definitions(${target} PRIVATE
    "FIXPP_CODEGEN_INC=\"${FIXPP_CODEGEN_OUT}\""
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  )
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES LABELS codegen)
endforeach()

# T022 conformance — manifest-driven presence/shape over the curated
# must-include CI subset (AC-G12). Runtime round-trip is R6-blocked (the
# vendored frozen wire stub carries no frame state); behavioural round-trip
# arrives when 2b swaps in the real body (spec §11 R6). The manifest is a
# checked-in, Gate-A-reviewed file.
add_executable(codegen_conformance_test conformance/conformance_test.cpp)
add_dependencies(codegen_conformance_test fixpp_codegen_generate)
target_link_libraries(codegen_conformance_test PRIVATE
  fixpp_dictionary GTest::gtest GTest::gtest_main)
target_include_directories(codegen_conformance_test PRIVATE
  "${FIXPP_CODEGEN_OUT}" "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(codegen_conformance_test PRIVATE
  "FIXPP_CODEGEN_MANIFEST=\"${CMAKE_CURRENT_SOURCE_DIR}/conformance/must_include_manifest.txt\"")
add_test(NAME codegen_conformance_test COMMAND codegen_conformance_test)
set_tests_properties(codegen_conformance_test PROPERTIES LABELS codegen)

# T039: cmake -P build-graph test — fixpp::dict::codegen-build-graph-check
#
# AC-C4: asserts that (a) generated headers exist post-configure, (b) generation
# is configure-time (not build-time), (c) per-version INTERFACE targets carry
# INTERFACE_INCLUDE_DIRECTORIES into the build tree, (d) source tree is clean.
#
# CTest name uses '::' (allowed in add_test NAME but not in add_custom_target).
# The cmake -P script reads ${CMAKE_BINARY_DIR}/_codegen/graph_props.cmake which
# is written by cmake/Codegen.cmake at configure time.
#
# The graph_props.cmake path is passed via -DFIXPP_GRAPH_PROPS= so the script
# has no hard-coded paths and works with any preset / build directory.
add_test(
  NAME "fixpp::dict::codegen-build-graph-check"
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_GRAPH_PROPS=${CMAKE_BINARY_DIR}/_codegen/graph_props.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/codegen_build_graph_test.cmake"
)
set_tests_properties("fixpp::dict::codegen-build-graph-check" PROPERTIES
  LABELS codegen)
