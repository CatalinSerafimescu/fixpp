# tests/session/conformance/CMakeLists.txt
# Q2 in-scope [FIX-TC] conformance subset for 005-session-establishment-fsm.
# Parameterized GoogleTest scenarios authored from the QuickFIX oracle
# (research D-10): tc_establishment, tc_seqnum, tc_liveness, tc_reject,
# tc_logout, tc_sendingtime.
#
# Phase 3 (T020): tc_establishment_test.cpp — establishment scenarios.
# Later phases (T030/T038/T045/T052/T053): seqnum/liveness/logout/reject/
# sendingtime tests added here under TDD red-green-refactor per
# [const §VII.1]/[const §VII.3].
find_package(GTest CONFIG REQUIRED)

# ── T020: tc_establishment — 1a/1b/1c/1d/1e/2i/2k/2k oracle scenarios ─────────
add_executable(tc_establishment_test tc_establishment_test.cpp)
target_link_libraries(tc_establishment_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(tc_establishment_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_tc_establishment COMMAND tc_establishment_test)
set_tests_properties(session_tc_establishment PROPERTIES ENVIRONMENT
  "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")
