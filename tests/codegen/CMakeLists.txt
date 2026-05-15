# tests/codegen/CMakeLists.txt
# 003-dictionary-codegen — US1 typed-accessor / shape suites (T017-T022).
#
# Build-time codegen into the BUILD TREE only (AC-C4 / AC-T2 — never the
# source tree). The RC#4 configure-time `generate-vXX` graph + per-version
# INTERFACE targets + the codegen-build-graph CTest land at T016/T039/T040;
# this is the minimal test-build wiring so the US1 suites can #include the
# generated headers. fixpp-codegen is added before the tests block in the
# top-level CMakeLists, so the target exists here.

if(NOT TARGET fixpp-codegen)
  message(STATUS "tests/codegen: fixpp-codegen not built; skipping US1 suites")
  return()
endif()

find_package(GTest CONFIG REQUIRED)

set(FIXPP_CODEGEN_OUT "${CMAKE_BINARY_DIR}/_codegen/include")
set(_xml "${CMAKE_SOURCE_DIR}/dictionaries")
set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/.codegen.stamp")

add_custom_command(
  OUTPUT "${_stamp}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIXPP_CODEGEN_OUT}/fixpp"
  COMMAND "$<TARGET_FILE:fixpp-codegen>"
          --xml "${_xml}/FIX42.xml"    --out "${FIXPP_CODEGEN_OUT}/fixpp"
          --xml "${_xml}/FIX44.xml"    --out "${FIXPP_CODEGEN_OUT}/fixpp"
          --xml "${_xml}/FIX50SP2.xml" --out "${FIXPP_CODEGEN_OUT}/fixpp"
          --xml "${_xml}/FIXT11.xml"   --out "${FIXPP_CODEGEN_OUT}/fixpp"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
  DEPENDS fixpp-codegen
          "${_xml}/FIX42.xml" "${_xml}/FIX44.xml"
          "${_xml}/FIX50SP2.xml" "${_xml}/FIXT11.xml"
  COMMENT "fixpp-codegen: generating per-version typed headers (build tree)"
  VERBATIM
)
add_custom_target(fixpp_codegen_generate DEPENDS "${_stamp}")

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
