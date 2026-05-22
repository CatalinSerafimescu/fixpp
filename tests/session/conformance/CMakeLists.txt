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

# ── T038: tc_liveness — DEFERRED to US4 (Phase 6) ───────────────────────────
# The 4a/4b [FIX-TC] liveness oracle scenarios require asserting engine-
# emitted Heartbeat frames against the oracle's E= lines. That requires the
# outbound transport binding (Session::send via the Session transport member)
# which is scoped to US4 (Phase 6 / T046 seam #11 + follow-ups). No Session
# transport member exists yet; without it there is no observable outbound
# frame and no honest conformance assertion is possible.
#
# tc_liveness_test.cpp is retained as a documented header-only source file
# explaining the deferral. It is NOT compiled into a test executable here —
# CI cleanly reports 0 conformance tests under this name until US4 wires it.
#
# Task tracking: tasks.md T038 + T042 carry the inline deferred-note.
