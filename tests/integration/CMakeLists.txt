# tests/integration/CMakeLists.txt
# 003-dictionary-codegen — US3 (T035) and US4 (T038) integration tests.
# Seam #10b (FIXT cross-vocabulary end-to-end) and seam #10a (multi-version
# coexistence, AC-C1..C3).
#
# These tests consume generated headers from the build-tree path
# ${CMAKE_BINARY_DIR}/_codegen/include (AC-C4 — build-tree only, never source tree).
#
# fixpp_codegen_generate is defined in cmake/Codegen.cmake (T016 — no-op at build
# time; headers already on disk after configure). It is add_subdirectory'd before
# tests/integration in the top-level CMakeLists.txt so the target is visible here.

if(NOT TARGET fixpp_codegen_generate)
  message(STATUS
    "tests/integration: fixpp_codegen_generate not available; skipping integration tests")
  return()
endif()

find_package(GTest CONFIG REQUIRED)

set(_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

# 068-test-binary-grouping US2: both integration tests are pure (no threads,
# no global/main, no ODR collisions — verified TEST() suite names distinct)
# and share LABELS "integration"; union their link-deps into one binary.
# US3 (T035): Seam #10b — FIXT cross-vocabulary end-to-end.
# US4 (T038): Seam #10a — multi-version coexistence (AC-C1..C3); a single TU
# includes both fixpp/v42/Messages.hpp and fixpp/v50sp2/Messages.hpp, all
# assertions are static_assert (compile-time) — NOT R6-blocked. Linking
# fixpp::dict::v42 and fixpp::dict::v50sp2 INTERFACE targets demonstrates
# AC-C2: the two targets carry independent INTERFACE_INCLUDE_DIRECTORIES and
# do NOT transitively pull in each other's logical version targets.
# Dispositions + `-R`→`-L` replacements recorded in
# specs/068-test-binary-grouping/dispositions.md.
add_executable(integration_pure_tests
  fixt_cross_vocabulary.cpp
  multi_session_multi_version.cpp
)
add_dependencies(integration_pure_tests fixpp_codegen_generate)
target_link_libraries(integration_pure_tests PRIVATE
  fixpp_dictionary
  fixpp::dict::v42
  fixpp::dict::v50sp2
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(integration_pure_tests PRIVATE
  "${_CODEGEN_INC}"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(integration_pure_tests PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME integration_pure_tests COMMAND integration_pure_tests)
set_tests_properties(integration_pure_tests PROPERTIES LABELS integration)
