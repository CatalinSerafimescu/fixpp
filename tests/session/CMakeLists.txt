# tests/session/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(session_smoke_test session_smoke_test.cpp)
target_link_libraries(session_smoke_test PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME session_smoke COMMAND session_smoke_test)

# ── 007-threading-clock seam suite ──────────────────────────────────────────
# Seam→file binding is one-to-one and glob-free (plan.md Test-seam map).
# CTest name prefix `threading_` selects the set:
#   ctest --test-dir build/<preset> -R '^threading_'
# Registrations are added red-first per TDD ([const §VII.3]) as each seam
# lands. fixpp_session transitively brings fixpp_core + asio.
function(add_threading_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE
    fixpp_session
    fixpp_mock_clock   # test-only deterministic Clock (unused by US1 seams)
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${name} PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
  )
  add_test(NAME ${name} COMMAND ${name})
  # asio's any_executor ref-count uses memory_order_release + manual fence;
  # TSan cannot track happens-before through it and flags shared_target_
  # executor<session_executor> refcount races during teardown when a custom
  # executor target is erased into any_io_executor (US4 seam 6 et al). The
  # suppressions file scopes the noise; harmless under non-TSan builds.
  set_tests_properties(${name} PROPERTIES ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")
endfunction()

# ── US1 (Phase 3) seam registrations ────────────────────────────────────────
add_threading_test(threading_strand_serialisation       test_strand_serialisation.cpp)  # seam 2 (TSan)
add_threading_test(threading_executor_compat            test_executor_compat.cpp)        # seam 3
add_threading_test(threading_direct_executor_reentrancy test_direct_executor_reentrancy.cpp)  # seam 16

# ── US2 (Phase 4) seam registrations ────────────────────────────────────────
add_threading_test(threading_heartbeat_under_mock_clock test_heartbeat_under_mock_clock.cpp)  # seam 9
add_threading_test(threading_clock_injection_corpus     test_clock_injection_corpus.cpp)      # seam 11

# ── US3 (Phase 5) seam registrations ────────────────────────────────────────
add_threading_test(threading_cancellation_parse_to_fromapp test_cancellation_parse_to_fromapp.cpp)  # seam 4
add_threading_test(threading_cancellation_fromapp_to_close test_cancellation_fromapp_to_close.cpp)  # seam 5
