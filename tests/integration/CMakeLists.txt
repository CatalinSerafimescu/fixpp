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

# ── US3 (T035): Seam #10b — FIXT cross-vocabulary end-to-end ─────────────────
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

# ── US4 (T038): Seam #10a — multi-version coexistence (AC-C1..C3) ────────────
# A single TU includes both fixpp/v42/Messages.hpp and fixpp/v50sp2/Messages.hpp.
# All assertions are static_assert (compile-time) — NOT R6-blocked.
# Links fixpp::dict::v42 and fixpp::dict::v50sp2 INTERFACE targets so that each
# per-version include root is reachable via its dedicated target.  This also
# demonstrates AC-C2: the two targets carry independent INTERFACE_INCLUDE_DIRECTORIES
# and do NOT transitively pull in each other's logical version targets.
add_executable(integration_multi_session_multi_version
  multi_session_multi_version.cpp
)
add_dependencies(integration_multi_session_multi_version fixpp_codegen_generate)
target_link_libraries(integration_multi_session_multi_version PRIVATE
  fixpp::dict::v42
  fixpp::dict::v50sp2
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(integration_multi_session_multi_version PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME integration_multi_session_multi_version
  COMMAND integration_multi_session_multi_version)
set_tests_properties(integration_multi_session_multi_version PROPERTIES
  LABELS integration)
