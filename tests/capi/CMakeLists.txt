# tests/capi/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(capi_smoke_test capi_smoke_test.cpp)
target_link_libraries(capi_smoke_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_smoke COMMAND capi_smoke_test)

# T004 (US1) — GTest version accessor tests
add_executable(capi_version_test version_test.cpp)
target_link_libraries(capi_version_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_version COMMAND capi_version_test)

# T009 (US2, SC-002/006) — error-surface correctness oracle: drives the 116
# fixpp::core::error enumerators through translate() against the checked-in
# expected_error_map.csv; FIXPP_CAPI_DATA_DIR points the test at the CSV.
add_executable(capi_error_surface_test error_surface_test.cpp)
target_link_libraries(capi_error_surface_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(capi_error_surface_test PRIVATE
  FIXPP_CAPI_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME capi_error_surface COMMAND capi_error_surface_test)

# T015 (US2, SC-004) — occupancy-gate negative test: clean tree passes, a
# redefined/collided published slot makes tools/check_capi_occupancy.sh fail
# deterministically.
add_test(NAME capi_occupancy_negative
  COMMAND bash "${CMAKE_SOURCE_DIR}/tools/test_capi_occupancy_negative.sh")

# T005 (US1, SC-001) — pure-C compile+link smoke: header C-cleanliness gate.
# enable_language(C) must be invoked before add_executable with C sources.
enable_language(C)
add_executable(capi_version_smoke_c capi_version_smoke.c)
target_link_libraries(capi_version_smoke_c PRIVATE fixpp_capi)
# fixpp_capi (static) drags C++ runtime; the C linker driver cannot resolve those
# symbols — set LINKER_LANGUAGE CXX so CMake picks the C++ driver for linking.
set_target_properties(capi_version_smoke_c PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME capi_version_smoke_c COMMAND capi_version_smoke_c)

# T016 (US3, SC-003) — pure-C handle-catalogue compile+link smoke: the umbrella
# is C-clean and the 5 opaque handle typedefs + null/invalid codes are usable
# from C. LINKER_LANGUAGE CXX because fixpp_capi (static) drags the C++ runtime.
add_executable(capi_handles_compile_test handles_compile_test.c)
target_link_libraries(capi_handles_compile_test PRIVATE fixpp_capi)
set_target_properties(capi_handles_compile_test PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME capi_handles_compile COMMAND capi_handles_compile_test)

# T021 (US4, SC-005) — reentrancy-gate positive/negative test: the real surface
# passes; a 0-class or 2-class symbol makes tools/check_capi_reentrancy.sh fail.
add_test(NAME capi_reentrancy_negative
  COMMAND bash "${CMAKE_SOURCE_DIR}/tools/test_capi_reentrancy_negative.sh")
