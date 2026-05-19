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

# 007-threading-clock seam 19 — session_executor round-trip + the
# executor_not_serialised enforcement point + the N5 open-path arm. Links
# fixpp_session (make_session_executor is the core-namespace bridge defined
# in the session TU; transitively pulls fixpp_core + asio). CTest name uses
# the `threading_` prefix (plan.md Test-seam map).
add_executable(threading_session_executor_round_trip
  test_session_executor_round_trip.cpp
)
target_link_libraries(threading_session_executor_round_trip PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME threading_session_executor_round_trip
         COMMAND threading_session_executor_round_trip)

# 007-threading-clock core seam helper — links fixpp_session (brings
# fixpp_core + asio + the production system_clock_source) + fixpp_mock_clock
# (test-only deterministic Clock) + the tests/ include root (scripted FSM).
function(add_threading_core_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE
    fixpp_session
    fixpp_mock_clock
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${name} PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
  )
  add_test(NAME ${name} COMMAND ${name})
  # See tests/session/CMakeLists.txt for the asio shared_target_executor
  # TSan suppression rationale (US4 seam 6 et al).
  set_tests_properties(${name} PROPERTIES ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")
endfunction()

# ── US2 (Phase 4) core seam registrations ───────────────────────────────────
add_threading_core_test(threading_mock_clock_determinism      test_mock_clock_determinism.cpp)       # seam 1
add_threading_core_test(threading_sleep_cancel_race           test_sleep_cancel_race.cpp)            # seam 10 (TSan+ASan)
add_threading_core_test(threading_engine_shutdown_order       test_engine_shutdown_order.cpp)        # seam 14
add_threading_core_test(threading_third_party_clock_conformance test_third_party_clock_conformance.cpp)  # seam 15 / SC-006

# ── US4 (Phase 6) core seam registrations ───────────────────────────────────
add_threading_core_test(threading_trace_context_resume        test_trace_context_resume.cpp)         # seam 6
add_threading_core_test(threading_session_local_lifetime      test_session_local_lifetime.cpp)       # seam 17 (TSan)
add_threading_core_test(threading_session_executor_accessor_survives_erasure test_session_executor_accessor_survives_erasure.cpp)  # seam 21

# Phase 4: fixpp_core_tests — pure C++ US1/US2/US4 tests (no C-ABI header dependency)
add_executable(fixpp_core_tests
  decimal_parse_test.cpp
  decimal_format_test.cpp
  decimal_compare_test.cpp
  decimal_roundtrip_property_test.cpp
  decimal_cross_traits_test.cpp
  decimal_alias_test.cpp
  decimal_predicates_test.cpp
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
  decimal_capi_error_test.cpp
  decimal_reserved_tolerance_test.cpp
)
target_link_libraries(fixpp_capi_tests PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(fixpp_capi_tests)
