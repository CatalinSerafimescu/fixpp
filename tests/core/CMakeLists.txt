# tests/core/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# Existing Phase 3 smoke test
add_executable(core_smoke_test core_smoke_test.cpp)
target_link_libraries(core_smoke_test PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(core_smoke_test)

# Phase 4: fixpp_core_tests — pure C++ US1/US2/US4 tests (no C-ABI header dependency)
add_executable(fixpp_core_tests
  decimal_parse_test.cpp
  decimal_format_test.cpp
  decimal_compare_test.cpp
  decimal_roundtrip_property_test.cpp
  decimal_cross_traits_test.cpp
  decimal_alias_test.cpp
)
target_include_directories(fixpp_core_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests/support"
)
target_link_libraries(fixpp_core_tests PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(fixpp_core_tests)

# Phase 4: fixpp_capi_tests — tests that require the C-ABI header (US3)
# Split prevents US1/US2 builds from blocking on US3 header creation (T033)
add_executable(fixpp_capi_tests
  decimal_capi_layout_test.cpp
  decimal_reserved_tolerance_test.cpp
)
target_link_libraries(fixpp_capi_tests PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(fixpp_capi_tests)
