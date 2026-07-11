# tests/core/CMakeLists.txt
# 068-test-binary-grouping: isolation-safe core tests are grouped into
# whole-binary add_test executables (bucketed by shared link-deps, no
# LABELS on any of them). Isolation-sensitive tests (threading_* TSan
# seams, per-`-D` decimal_mul_u64_wide variants, exact-set completeness
# gates, the live-name decimal_compare_diff_oracle) stay standalone.
# Dispositions in specs/068-test-binary-grouping/dispositions.md.
find_package(GTest CONFIG REQUIRED)

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
target_include_directories(threading_session_executor_round_trip PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
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

# ── gate-b/r1 RC#2: FR-015 outside-session engine-fallback seam ─────────────
# Asserts current_trace_context() on plain executor returns default trace_context
# (D-17 / P1.2→P2 missing-test closure).
add_threading_core_test(threading_trace_context_engine_fallback
  test_trace_context_engine_fallback.cpp)  # gate-b/r1

# Phase 4: fixpp_core_tests — pure C++ US1/US2/US4 tests (no C-ABI header
# dependency). 068: whole-binary add_test (converted from gtest_discover_tests);
# core_smoke_test.cpp folded in (068 US2) — same link-deps, no LABELS.
add_executable(fixpp_core_tests
  core_smoke_test.cpp
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
add_test(NAME fixpp_core_tests COMMAND fixpp_core_tests)

# Phase 4: fixpp_capi_tests — tests that require the C-ABI header (US3)
# Split prevents US1/US2 builds from blocking on US3 header creation (T033)
# 068: whole-binary add_test (converted from gtest_discover_tests).
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
add_test(NAME fixpp_capi_tests COMMAND fixpp_capi_tests)

# ── 017-log-otel T008 — error completeness gate ─────────────────────────────
# Asserts exactly 7 new enumerators at slots 122–128 with exact-SET equality
# and smoke-asserts error_message/to_string non-empty (FR-015).
# Links fixpp_core only (no session/transport dependency).
add_executable(error_017_completeness test_017_error_completeness.cpp)
target_link_libraries(error_017_completeness PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME error_017_completeness COMMAND error_017_completeness)

# ── 019-app-callbacks T004 — error completeness gate ────────────────────────
# Asserts exactly 2 new enumerators at slots 129–130 with exact-SET equality
# and smoke-asserts error_message/to_string non-empty (FR-015; research D7).
# Links fixpp_core only (no session/transport dependency).
add_executable(error_019_completeness test_019_error_completeness.cpp)
target_link_libraries(error_019_completeness PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME error_019_completeness COMMAND error_019_completeness)
set_tests_properties(error_019_completeness PROPERTIES
  LABELS "019;foundational;error_slots")

add_executable(error_020_completeness test_020_error_completeness.cpp)
target_link_libraries(error_020_completeness PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME error_020_completeness COMMAND error_020_completeness)
set_tests_properties(error_020_completeness PROPERTIES
  LABELS "020;foundational;error_slots")

# ── 060-int128-decimal-compare T003 — differential-oracle harness ──────────
# Deterministic (seed=42) boundary-biased corpus asserting the production
# decimal_traits<pod_decimal>::compare agrees with a FROZEN verbatim copy of
# today's (pre-swap) digit-string comparator. Trivially green pre-T005;
# becomes discriminating once T005 swaps the different-exponent slow path.
# Own executable (not gtest_discover_tests) so `ctest -R decimal_compare_diff_oracle`
# matches the ctest test name directly.
add_executable(decimal_compare_diff_oracle_test decimal_compare_diff_oracle_test.cpp)
target_link_libraries(decimal_compare_diff_oracle_test PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME decimal_compare_diff_oracle COMMAND decimal_compare_diff_oracle_test)
set_tests_properties(decimal_compare_diff_oracle PROPERTIES
  LABELS "060;foundational;decimal_compare"
  TIMEOUT 120)

# ── 060-int128-decimal-compare T008 — direct mul_u64_wide unit test ─────────
# Two builds of the SAME .cpp: the default target compiles the __int128
# branch of the flag-identical mul_u64_wide copy; the _portable target
# forces FIXPP_DECIMAL_FORCE_PORTABLE_MUL so it compiles the #else
# schoolbook branch instead. Self-contained (no fixpp_core dependency —
# the primitive under test is copied inline); GTest only.
add_executable(decimal_mul_u64_wide_test decimal_mul_u64_wide_test.cpp)
target_link_libraries(decimal_mul_u64_wide_test PRIVATE
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME decimal_mul_u64_wide COMMAND decimal_mul_u64_wide_test)
set_tests_properties(decimal_mul_u64_wide PROPERTIES
  LABELS "060;US2;decimal_compare"
  TIMEOUT 120)

add_executable(decimal_mul_u64_wide_test_portable decimal_mul_u64_wide_test.cpp)
target_compile_definitions(decimal_mul_u64_wide_test_portable PRIVATE
  FIXPP_DECIMAL_FORCE_PORTABLE_MUL
)
target_link_libraries(decimal_mul_u64_wide_test_portable PRIVATE
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME decimal_mul_u64_wide_portable COMMAND decimal_mul_u64_wide_test_portable)
set_tests_properties(decimal_mul_u64_wide_portable PROPERTIES
  LABELS "060;US2;decimal_compare"
  TIMEOUT 120)
