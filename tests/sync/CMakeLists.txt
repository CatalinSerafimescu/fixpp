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
# sync_drain_reaper_abort_subscribers — RETIRED (048 Erratum E-5): tested
# cross-thread abort-subscriber machinery (drain_latch_state::signal_abort()/
# subscriber channel) which is REMOVED by 048. The drain is now uninterruptible
# and strand-local; no abort path or subscribers exist. Replaced by T009/T011.
# sync_cancel_and_drain_reentrant_after_abort — RETIRED (048): tested the
# F-2 gate-b/r1 regression lock (abort + reentrant → unexpected{sync_lock_aborted}).
# With E-5 the drain is uninterruptible; reentrant callers await draining_complete_
# then return ok (not aborted). Replaced by T011 (test_drain_reentrant_during_active).
# sync_reentrant_drain_uaf_window — RETIRED (048): tested the cross-thread F-3
# residual UAF window (abort → reentrant → destroy while resumers in-flight).
# With E-5 the UAF is prevented by in_flight_resumers_ instead of the latch;
# witnessed by T010 (test_drain_immediate_destroy_after_reap).

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

# ── Gate-B/r1 seam registrations (F-2 regression lock + F-3-residual UAF) ────
# Seam #30: RETIRED (048 Erratum E-5) — was sync_cancel_and_drain_reentrant_after_abort.
# Tests removed abort path. Replaced by sync_drain_reentrant_during_active (T011).
# Seam #31: RETIRED (048 Erratum E-5) — was sync_reentrant_drain_uaf_window.
# Tests removed cross-thread UAF window. Replaced by sync_drain_immediate_destroy (T010).

# ── 048 strand-local-reap witness registrations (T009–T013, T020) ────────────
# T009: SC-001 stress — N waiters on one strand, cancel_and_drain() reaps all,
#   no hang, internal 5s self-deadline.
add_sync_test(sync_drain_strand_local_reap        test_drain_strand_local_reap.cpp)
# T010: UAF guard — destroy mutex immediately after drain returns; ASan proves
#   in_flight_resumers_==0 held at drain return (INV-D).
add_sync_test(sync_drain_immediate_destroy        test_drain_immediate_destroy_after_reap.cpp)
# T011: reentrant drain — second cancel_and_drain() while first suspended awaits
#   draining_complete_, returns ok (not false-success).
add_sync_test(sync_drain_reentrant_during_active  test_drain_reentrant_during_active.cpp)
# T012: on-strand cancel during reap — CAS single-winner; each waiter resolved
#   exactly once; TSan-libstdc++ clean.
add_sync_test(sync_drain_onstrand_cancel          test_drain_onstrand_cancel_during_reap.cpp)
# T013: pre-drain holder — loop yields until holder unlocks; drain finalizes.
add_sync_test(sync_drain_predrain_holder          test_drain_predrain_holder.cpp)
# T020: layout golden — alignof==16; sizeof < 047 baseline (131152).
add_sync_test(sync_async_mutex_layout_golden      test_async_mutex_layout_golden.cpp)

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
    # 005-session-establishment-fsm T064 (Phase 8) — extend the [const §XV.9]
    # grep gate over every 005-owned awaitable-including header. Each must
    # NOT name any of the FR-014 six banned std::mutex types post-preprocess.
    "${CMAKE_SOURCE_DIR}/include/fixpp/core/fix_time.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/admin_messages.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/sending_time.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/seqnum_manager.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/session_fsm.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/session.hpp"
    # 039-ff-tail-hardening US4 (FR-011 / SC-005) — extend the [const §XV.9] gate
    # over the remaining session-side headers that (transitively) pull
    # asio::awaitable but were not yet listed. Confirmed clean post-preprocess
    # (their std::mutex occurrences are [const §XV.9] comment markers, stripped
    # by -E). The explicit list is a local CMake-gate convention — NOT a §VI.4
    # property (research.md D-4 corrects the earlier mis-citation above).
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/detail/has_flush_for_session_close.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/engine.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/file_store.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/memory_store.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/message_store.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/reconnect_fsm.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/session/retrieve_visitor.hpp"
)
