# tests/integration/CMakeLists.txt
# 003-dictionary-codegen — US3 integration tests (T035).
# Seam #10b (FIXT cross-vocabulary end-to-end) and related dispatch round-trips.
#
# These tests are dispatch-consuming TUs: they #include the generated
# _dispatch/ headers (reify_dispatch_fixt.hpp + reify_dispatch_application.hpp)
# which define the inline dispatch helpers. The generated include dir is the
# same ${CMAKE_BINARY_DIR}/_codegen/include used by tests/codegen and
# tests/dictionary reify tests (AC-C4 — build-tree only, never source tree).
#
# fixpp_codegen_generate is defined in tests/codegen/CMakeLists.txt which is
# add_subdirectory'd BEFORE tests/integration in the top-level CMakeLists.txt.
# The ordering guarantees the custom target + stamp are visible here.

if(NOT TARGET fixpp_codegen_generate)
  message(STATUS
    "tests/integration: fixpp_codegen_generate not available; skipping US3 integration tests")
  return()
endif()

find_package(GTest CONFIG REQUIRED)

set(_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

set(FIXPP_INTEGRATION_TESTS
  integration_fixt_cross_vocabulary
)

foreach(target IN LISTS FIXPP_INTEGRATION_TESTS)
  string(REGEX REPLACE "^integration_" "" src_stem "${target}")
  add_executable(${target} "${src_stem}.cpp")
  add_dependencies(${target} fixpp_codegen_generate)
  target_link_libraries(${target} PRIVATE
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${target} PRIVATE
    "${_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
  )
  target_compile_definitions(${target} PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  )
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES LABELS integration)
endforeach()
