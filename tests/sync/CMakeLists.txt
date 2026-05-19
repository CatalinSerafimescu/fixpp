# tests/sync/CMakeLists.txt — 006-async-mutex test suite (29 named seams).
# Every test registers under the `sync_` CTest name prefix so
# `ctest --preset linux-clang-debug -R '^sync_'` selects the full seam set
# (quickstart §2).  ASan/UBSan/TSan/coverage are driven by the active preset;
# TSan is the mandatory non-waivable gate for this concurrency primitive.
#
# Seam→file binding is one-to-one and glob-free ([const §VI.4]; plan.md
# Test-seam map).  Registrations are added as each seam file lands red-first
# per TDD ([const §VII.3]) — mirrors the tests/wire incremental precedent.
find_package(GTest CONFIG REQUIRED)

# Labelled fixture corpus for seam #14 (the [const §XV.9] grep gate).
set(SYNC_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# Repo root + the grep-gate script path, exported so seam #14 can drive the
# tool over the corpus from the test binary.
set(SYNC_GREP_GATE_SCRIPT
  "${CMAKE_SOURCE_DIR}/tools/check_no_std_mutex_in_awaitable_headers.sh")

# helper: add a sync_ test executable linking fixpp::sync + GTest.
# Default linkage follows the active preset; seam #5 (destructor death test)
# overrides to release linkage at its registration site (quickstart §2).
function(add_sync_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE
    fixpp_sync
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${name} PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
    "${SYNC_FIXTURES_DIR}"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES
    ENVIRONMENT "SYNC_FIXTURES_DIR=${SYNC_FIXTURES_DIR};SYNC_GREP_GATE_SCRIPT=${SYNC_GREP_GATE_SCRIPT}")
endfunction()

# ── US1 seam registrations (T016–T025) ──────────────────────────────────────
add_sync_test(sync_uncontended_latency   test_uncontended_latency.cpp)
add_sync_test(sync_contended_latency     test_contended_latency.cpp)
add_sync_test(sync_fifo_fairness         test_fifo_fairness.cpp)
add_sync_test(sync_contention_stress     test_contention_stress.cpp)
add_sync_test(sync_executor_compat       test_executor_compat.cpp)
add_sync_test(sync_dispatch_vs_post      test_dispatch_vs_post.cpp)
add_sync_test(sync_cross_strand_acquire  test_cross_strand_acquire.cpp)
add_sync_test(sync_guard_destructive_move test_guard_destructive_move.cpp)
add_sync_test(sync_unlock_reaper_splice  test_unlock_reaper_splice.cpp)
add_sync_test(sync_result_write_race     test_result_write_race.cpp)

# ── US2 seam registrations (T033–T037) ──────────────────────────────────────
add_sync_test(sync_cancellation_mid_wait     test_cancellation_mid_wait.cpp)
add_sync_test(sync_race_cancel_pre_drain     test_race_cancel_pre_drain.cpp)
add_sync_test(sync_race_multi_cancel         test_race_multi_cancel.cpp)
add_sync_test(sync_race_cancel_during_resume test_race_cancel_during_resume.cpp)
add_sync_test(sync_residual_cancel_graceful  test_residual_cancel_graceful.cpp)

# ── US4 seam registrations (T053–T057) ──────────────────────────────────────
add_sync_test(sync_tsan_clean              test_tsan_clean.cpp)
add_sync_test(sync_asan_clean              test_asan_clean.cpp)
add_sync_test(sync_halo_firing             test_halo_firing.cpp)
add_sync_test(sync_pmr_fallback            test_pmr_fallback.cpp)
add_sync_test(sync_slot_allocator_storage  test_slot_allocator_storage.cpp)

# ── US3 seam registrations (T041–T047) ──────────────────────────────────────
# Seam #5: destructor release-linkage death test.
# Note: EXPECT_DEATH fires std::terminate() in the child process; this
# registration runs under the active preset (debug or release). Full
# release-linkage CI execution is wired at T050/T052 by the parent.
add_sync_test(sync_destructor_release_death      test_destructor_release_death.cpp)
add_sync_test(sync_cancel_and_drain              test_cancel_and_drain.cpp)
add_sync_test(sync_cancel_and_drain_concurrent   test_cancel_and_drain_concurrent.cpp)
add_sync_test(sync_drain_latch_holder_lifecycle  test_drain_latch_holder_lifecycle.cpp)
add_sync_test(sync_in_flight_acquirer_coverage   test_in_flight_acquirer_coverage.cpp)
add_sync_test(sync_drain_awaitable_cancellation  test_drain_awaitable_cancellation.cpp)
add_sync_test(sync_drain_reaper_abort_subscribers test_drain_reaper_abort_subscribers.cpp)

# ── Seam registrations (added red-first as Phase 2 / per-story tasks land) ────
# US1 (T016–T025): sync_uncontended_latency #1, sync_contended_latency #2,
#   sync_fifo_fairness #3, sync_contention_stress #6, sync_executor_compat #11,
#   sync_dispatch_vs_post #12, sync_cross_strand_acquire #13,
#   sync_guard_destructive_move #20, sync_unlock_reaper_splice #27,
#   sync_result_write_race #28
# US2 (T033–T037): sync_cancellation_mid_wait #4, sync_race_cancel_pre_drain #15,
#   sync_race_multi_cancel #16, sync_race_cancel_during_resume #17,
#   sync_residual_cancel_graceful #22
# US3 (T041–T047): sync_destructor_release_death #5 (release linkage),
#   sync_cancel_and_drain #19, sync_cancel_and_drain_concurrent #23,
#   sync_drain_latch_holder_lifecycle #24, sync_in_flight_acquirer_coverage #25,
#   sync_drain_awaitable_cancellation #26, sync_drain_reaper_abort_subscribers #29
# US4 (T053–T057): sync_tsan_clean #7, sync_asan_clean #8, sync_halo_firing #9,
#   sync_pmr_fallback #10, sync_slot_allocator_storage #21
# US5 (T062–T065): sync_no_std_mutex_ci_gate #14 (+ 3 fixtures/ headers)
# Polish: sync_arm64_weak_memory #18 (T068); sync_consumer_contract_compile (T078)

# ── Polish seam registrations (T068, T078) ───────────────────────────────────
# Seam #18: weak-memory contention stress verifying the I-01..I-31 ordering
# spec under a multi-threaded io_context (TSan is the portable check; native
# ARM64 execution recorded host-unavailable in the verify doc — T068).
add_sync_test(sync_arm64_weak_memory test_arm64_weak_memory.cpp)
# T078: consumer compile/link contract — SC-008 / [2e §3.1] hand-off gate.
# Compile-only static_assert/SFINAE TU over the SHIPPED headers; links
# fixpp_sync to confirm non-inline surface link-completeness.
add_sync_test(sync_consumer_contract_compile test_consumer_contract_compile.cpp)

# ── US5 seam registration (T062) ─────────────────────────────────────────────
# Seam #14: CI grep-gate driver.  Shells out to the bash gate script over the
# T063–T065 fixture corpus and asserts zero-FN / zero-FP.
# add_sync_test sets a baseline ENVIRONMENT with SYNC_FIXTURES_DIR and
# SYNC_GREP_GATE_SCRIPT; the explicit set_tests_properties below extends it
# with the two include paths the gate script needs (project + asio).
add_sync_test(sync_no_std_mutex_ci_gate test_no_std_mutex_ci_gate.cpp)
set_tests_properties(sync_no_std_mutex_ci_gate PROPERTIES
  ENVIRONMENT
    "SYNC_GREP_GATE_SCRIPT=${SYNC_GREP_GATE_SCRIPT};\
SYNC_FIXTURES_DIR=${SYNC_FIXTURES_DIR};\
SYNC_GATE_INC=${CMAKE_SOURCE_DIR}/include;\
SYNC_ASIO_INC=$<TARGET_PROPERTY:asio::asio,INTERFACE_INCLUDE_DIRECTORIES>"
)

# ── T066: first-class Tier-1 gate over the REAL awaitable-header corpus ───────
# Seam #14 above validates the gate's behaviour against the synthetic T063–T065
# fixtures.  This ctest is the production gate itself ([const §XV.9] / [2f
# §6.6] / SC-006): it runs the bash script directly over every real header that
# (transitively) pulls asio::awaitable — async_mutex.hpp and the session-side
# helper — and MUST exit 0 (zero violations).  It rides the existing Tier-1
# ctest/conan environment (no separate workflow job; asio include dir resolved
# from the target), so it is a first-class non-waivable Tier-1 step.  The
# corpus list is explicit and glob-free ([const §VI.4]); add new awaitable
# headers here as they land.
add_test(NAME check_no_std_mutex_corpus
  COMMAND bash "${SYNC_GREP_GATE_SCRIPT}"
    -I "${CMAKE_SOURCE_DIR}/include"
    -I "$<TARGET_PROPERTY:asio::asio,INTERFACE_INCLUDE_DIRECTORIES>"
    --
    "${CMAKE_SOURCE_DIR}/include/fixpp/core/sync/async_mutex.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/async_lock_via_session_executor.hpp"
)
