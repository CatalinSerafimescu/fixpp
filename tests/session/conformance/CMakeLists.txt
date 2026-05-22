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

# ── T030: tc_seqnum — 2a/2c/2q/2r oracle scenarios (NOT 1a/2b — deferred) ────
# Red-first TDD: authored before T031–T036; turns green at T036.
# D-10 conformance subset: in-scope seqnum scenarios only.
add_executable(tc_seqnum_test tc_seqnum_test.cpp)
target_link_libraries(tc_seqnum_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(tc_seqnum_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_tc_seqnum COMMAND tc_seqnum_test)
set_tests_properties(session_tc_seqnum PROPERTIES ENVIRONMENT
  "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")

# ── T045: tc_logout — TC-009 subset (13b_UnsolicitedLogoutMessage + timeout) ─
# Red-first TDD: authored before T046–T048; turns green at T049.
# Scenarios: 13b_UnsolicitedLogoutMessage (fix42/fix44); GracefulLogoutTimeout;
# GracefulLogoutBothDirections.
add_executable(tc_logout_test tc_logout_test.cpp)
target_link_libraries(tc_logout_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(tc_logout_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_tc_logout COMMAND tc_logout_test)
set_tests_properties(session_tc_logout PROPERTIES ENVIRONMENT
  "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")

# ── T052: tc_reject — TC-005 scenario 7 + TC-010 scenarios 14a–14g ─────────
# Red-first TDD: authored before T054/T056; turns green at T057.
# 14h/14i/14j deferred per D-10 (repeating-group — not authored).
add_executable(tc_reject_test tc_reject_test.cpp)
target_link_libraries(tc_reject_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(tc_reject_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_tc_reject COMMAND tc_reject_test)
set_tests_properties(session_tc_reject PROPERTIES ENVIRONMENT
  "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")

# ── T053: tc_sendingtime — 1d_InvalidLogonBadSendingTime + 2o_SendingTimeValueOutOfRange ─
# fix42 + fix44 oracle coverage (D-10). FIXT/5.0SP2 deferred per D-10.
add_executable(tc_sendingtime_test tc_sendingtime_test.cpp)
target_link_libraries(tc_sendingtime_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(tc_sendingtime_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_tc_sendingtime COMMAND tc_sendingtime_test)
set_tests_properties(session_tc_sendingtime PROPERTIES ENVIRONMENT
  "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")

# ── T038/T042: tc_liveness — US4 Phase 6 retirement (partial) ────────────────
# 4b_ReceivedTestRequest: inbound TestRequest → engine echoes Heartbeat(112=)
# is now assertable because transport_send_ is wired (US4/T046). The 4a
# outbound-idle Heartbeat scenario requires tracking last_outbound_steady_
# which is deferred to a follow-up phase. Ships 4b GREEN; 4a deferred with
# traceability in tc_liveness_test.cpp.
#
# Task tracking: tasks.md T038 + T042 closed-partial (4b done, 4a deferred).
add_executable(tc_liveness_test tc_liveness_test.cpp)
target_link_libraries(tc_liveness_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(tc_liveness_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_tc_liveness COMMAND tc_liveness_test)
set_tests_properties(session_tc_liveness PROPERTIES ENVIRONMENT
  "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")
