# tests/sync/CMakeLists.txt — async_mutex test suite (35 registered sync_* seams;
# the `add_sync_test(...)` calls below are authoritative). 048 / Erratum E-5: +6
# strand-local drain witnesses (sync_drain_strand_local_reap / _immediate_destroy /
# _reentrant_during_active / _onstrand_cancel / _predrain_holder / sync_async_mutex_
# layout_golden); −3 retired cross-thread-machinery tests (reaper_abort_subscribers,
# cancel_and_drain_reentrant_after_abort, reentrant_drain_uaf_window). The historical
# US-map comments below predate 048 and are not kept in lockstep — trust add_sync_test.
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
# 058 T032 (US5, T-7): FIFO fairness ACROSS drain cycles (the residual-vs-
# later-arrival straddle) — sync_fifo_fairness above only covers WITHIN a
# single drain cycle. See file header for the deterministic poll_one()/gate
# driver design (a determinism contract, not a race — deliberate deviation
# from the T027-T031 genuinely-MT idiom).
add_sync_test(sync_fifo_across_cycles    test_fifo_across_cycles.cpp)
add_sync_test(sync_contention_stress     test_contention_stress.cpp)
# 058 T040 (US5, Phase 7.5 coverage-design-gate addition): H-A genuinely-MT
# hammer — organic (seam-OFF, coverage-lane-visible) coverage of the
# free-list pop/bump-counter/release_ref-push CAS-retry loops, the
# async_lock slow-path push CAS-retry, and the FIRST unlock() terminal-CAS-
# fail recursive-unlock arm ("F4"). See file header for the CORRECTION note
# (empirically measured F6/push_residual as NOT organically coverable —
# escalated, recommend waiver) + .specify/decisions/058-async-mutex-
# hardening-coverage-design.md. Ordinary add_sync_test linkage (no
# FIXPP_ASYNC_MUTEX_TEST_SEAM) so it runs on every preset including the
# seam-OFF coverage lane.
#
# Dual config (file header has the full rationale): the default (debug/TSan/
# ASan) build uses the brief-literal light config; FIXPP_ENABLE_COVERAGE
# switches on a much-higher-volume, jitter-free burst (via the
# FIXPP_ASYNC_MUTEX_MT_HAMMER_COVERAGE_BURST compile definition) needed to
# organically hit the F4 recursive-unlock arm's few-instruction race window
# — measured to never fire under the brief-literal jittered design. The
# burst is confined to the coverage lane so it never inflates the
# correctness-lane TIMEOUT / TSan/ASan runtime.
add_sync_test(sync_async_mutex_mt_hammer test_async_mutex_mt_hammer.cpp)
set_tests_properties(sync_async_mutex_mt_hammer PROPERTIES TIMEOUT 120)
if(FIXPP_ENABLE_COVERAGE)
  target_compile_definitions(sync_async_mutex_mt_hammer PRIVATE
    FIXPP_ASYNC_MUTEX_MT_HAMMER_COVERAGE_BURST)
  set_tests_properties(sync_async_mutex_mt_hammer PROPERTIES TIMEOUT 300)
endif()
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
# 058 T015 (US2): genuinely multi-threaded, cross-executor drain-then-destroy
# HB-observation witness (research.md D-2). See file header for the coordination
# design (deterministic single-threaded park + genuinely-threaded reap/destroy).
add_sync_test(sync_drain_destroy_inflight_mt     test_drain_destroy_inflight_mt.cpp)
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
# 058 T045: three new genuinely-MT D-2 discriminator epochs (20/20/15 reps,
# each spinning two dedicated OS threads + bounded wait_ready deadlines) were
# added on top of the pre-existing MultiThreadedContentionMutualExclusion
# case. The internal deadlines make a hang near-impossible, but this is a
# per-test backstop against the ctest default (1500s) per
# feedback_ci_hung_test_no_timeout_burns_6h — cheap insurance, not a fix.
set_tests_properties(sync_arm64_weak_memory PROPERTIES TIMEOUT 120)
# T078: consumer compile/link contract — SC-008 / [2e §3.1] hand-off gate.
# Compile-only static_assert/SFINAE TU over the SHIPPED headers; links
# fixpp_sync to confirm non-inline surface link-completeness.
add_sync_test(sync_consumer_contract_compile test_consumer_contract_compile.cpp)

# ── 046-atomic-shared-ptr seam registrations (NFR-017) ───────────────────────
# Primitive-level gtest ports of the locked research harness (18/18).
# Runs on both native and forced-fallback presets; the ff preset supplies
# -DFIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON so FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE=0.
add_sync_test(sync_atomic_shared_ptr_primitive  test_atomic_shared_ptr_primitive.cpp)
add_sync_test(sync_atomic_shared_ptr_concurrency test_atomic_shared_ptr_concurrency.cpp)

# ── US5 seam registration (T062) ─────────────────────────────────────────────
# Seam #14: CI grep-gate driver.  Shells out to the bash gate script over the
# T063–T065 fixture corpus and asserts zero-FN / zero-FP.
# add_sync_test sets a baseline ENVIRONMENT with SYNC_FIXTURES_DIR and
# SYNC_GREP_GATE_SCRIPT; the explicit set_tests_properties below extends it
# with the two include paths the gate script needs (project + asio).
# WAIVED on Windows (cross-platform sign-off 2026-06-22): both the gate driver
# below and the corpus ctest shell out to `bash` running the POSIX lint script
# tools/check_no_std_mutex_in_awaitable_headers.sh, which has no Windows analog.
# The [const §XV.9] header-hygiene property is platform-invariant and remains a
# first-class non-waivable Tier-1 (Linux) gate.
if(NOT WIN32)
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
endif()  # NOT WIN32 — no_std_mutex bash lint gate (waived on Windows)

# ── 046 T013 CT-1c/CT-1d: libc++ corpus gate (FIXPP_LIBCXX_LANE only) ────────
# Registers two ctests guarded by FIXPP_LIBCXX_LANE=ON (set only by the
# linux-clang-libc++ preset). Inert on all default libstdc++ presets.
#
# CT-1c (check_no_std_mutex_corpus_libcxx):
#   Re-runs the [const §XV.9] corpus check under libc++ preprocessing.
#   --require-awaitable-marker: a header that emits no asio/awaitable.hpp
#   marker indicates empty / misrouted preprocessing → RED (fail-closed).
#
# CT-1d (check_libcxx_active):
#   Compiles a tiny snippet that #errors if _LIBCPP_VERSION is not defined.
#   Proves the libc++ stdlib headers were actually active during compilation
#   (not silently resolved to libstdc++). Discriminates: libc++ → exit 0;
#   libstdc++ / -stdlib= flag silently ignored → exit 1.
if(FIXPP_LIBCXX_LANE)
  add_test(NAME check_no_std_mutex_corpus_libcxx
    COMMAND bash "${SYNC_GREP_GATE_SCRIPT}"
      -I "${CMAKE_SOURCE_DIR}/include"
      -I "$<TARGET_PROPERTY:asio::asio,INTERFACE_INCLUDE_DIRECTORIES>"
      -stdlib=libc++
      --require-awaitable-marker
      --
      "${CMAKE_SOURCE_DIR}/include/fixpp/core/sync/async_mutex.hpp"
      "${CMAKE_SOURCE_DIR}/include/fixpp/session/engine.hpp"
  )
  # CT-1d: _LIBCPP_VERSION active probe.
  # Compiles a one-liner via bash -c to avoid a dedicated source file.
  # <version> is the standard feature-test header; it defines _LIBCPP_VERSION
  # when compiled under libc++ and is absent (or defines nothing matching)
  # under libstdc++ / MSVC. The snippet is passed on stdin so no temp file
  # is needed.
  add_test(NAME check_libcxx_active
    COMMAND bash -c
      "printf '#include <version>\\n#ifndef _LIBCPP_VERSION\\n#error libc++ not active\\n#endif\\n' \
       | '${CMAKE_CXX_COMPILER}' -stdlib=libc++ -std=c++23 -x c++ -c -o /dev/null -"
  )
endif()

# ── 058-async-mutex-hardening T001/T002: standalone D-7 ABA-interleave seam ──
# research.md D-7: FIXPP_ASYNC_MUTEX_TEST_SEAM changes async_mutex.hpp's inline
# out-of-line body token sequences. A seam-enabled TU linked against ANY
# non-seam TU that also instantiates those bodies is an ODR violation, so this
# executable MUST NOT link fixpp_sync (or any other fixpp static library) via
# add_sync_test(). It is built directly against the header include path +
# asio::asio (header-only, no compiled TUs) + GTest — async_mutex.hpp's own
# includes are all header-only (<asio/*>, fixpp/core/error.hpp; the latter has
# no .cpp) so ZERO fixpp library linkage is required or present. This is the
# strongest form of "standalone": no fixpp object file of any kind is linked
# into this binary, seam-enabled or not.
#
# T007/T008 author the real witness bodies later; today this registers a
# minimal compiling stub (test_async_mutex_aba_interleave.cpp) so CMake
# configures green without any real race logic.
#
# T002 lane wiring (D-7 "document the expected TSan report" branch — NOT the
# "non-TSan lane" branch): the target is registered on EVERY preset, TSan
# included. Excluding TSan entirely would remove the *only* discriminating
# lane for D-7 part 2 (`pop_pre_link_load`, T008): that phase's witness is
# TSan-RED against the pre-fix member-`free_next_` shape and TSan-clean only
# after the fix (T011 "confirm ... TSan clean"). A blanket non-TSan exclusion
# would make part 2 non-discriminating everywhere — the exact witness-quality
# failure this feature exists to eliminate.
#
# Consequence to read at T007/T008/T012 time: the PRE-FIX mutation-RED run of
# this target is EXPECTED to produce
#   - a real TSan data-race report on the `linux-clang-tsan` /
#     `linux-clang-libc++-tsan` presets (part 2's discriminator), and
#   - an assertion failure (no TSan report) on every other preset (part 1,
#     the `pop_pre_cas` ABA window, is TSan-invisible per D-7's problem
#     statement) —
# neither is an unexpected sanitizer gate failure; do not suppress or waive
# the TSan report generically. Post-fix (T009/T010 landed) both phases are
# clean on every lane, TSan included.
add_executable(test_async_mutex_aba_interleave
  test_async_mutex_aba_interleave.cpp)
target_compile_definitions(test_async_mutex_aba_interleave PRIVATE
  FIXPP_ASYNC_MUTEX_TEST_SEAM)
target_include_directories(test_async_mutex_aba_interleave PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(test_async_mutex_aba_interleave PRIVATE
  asio::asio
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME sync_async_mutex_aba_interleave
  COMMAND test_async_mutex_aba_interleave)

# ── 058-async-mutex-hardening T046: F4/F6 terminal-CAS recursive-unlock ─────
# deterministic seam witnesses (coverage-design gate EMPIRICAL CORRECTION —
# see .specify/decisions/058-async-mutex-hardening-coverage-design.md). Same
# standalone / zero-fixpp_sync-linkage ODR discipline as
# test_async_mutex_aba_interleave above (the two new
# `unlock_pre_terminal_cas_fast` / `unlock_pre_terminal_cas_fifo` seam
# call-sites in async_mutex.hpp's unlock() are FIXPP_ASYNC_MUTEX_TEST_SEAM-
# gated). Per-test TIMEOUT bounds a mis-pinned interleave (both witnesses
# also bound their internal waits and force-stop the stuck io_context rather
# than hanging indefinitely, per the anti-hang build discipline).
add_executable(test_async_mutex_terminal_cas_recursive_unlock
  test_async_mutex_terminal_cas_recursive_unlock.cpp)
target_compile_definitions(test_async_mutex_terminal_cas_recursive_unlock PRIVATE
  FIXPP_ASYNC_MUTEX_TEST_SEAM)
target_include_directories(test_async_mutex_terminal_cas_recursive_unlock PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(test_async_mutex_terminal_cas_recursive_unlock PRIVATE
  asio::asio
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME sync_async_mutex_terminal_cas_recursive_unlock
  COMMAND test_async_mutex_terminal_cas_recursive_unlock)
set_tests_properties(sync_async_mutex_terminal_cas_recursive_unlock PROPERTIES TIMEOUT 60)

# ── 058-async-mutex-hardening Gate-B MAJOR-2: contended-acquire livelock ────
# deterministic seam witness (async_mutex.hpp `old_state` refresh fix at the
# contended acquire loop's not_locked branch). Same standalone / zero-
# fixpp_sync-linkage ODR discipline as the targets above (the two new
# `acq_pre_state_reload` / `acq_pre_notlocked_cas` seam call-sites in
# async_mutex.hpp's async_lock() are FIXPP_ASYNC_MUTEX_TEST_SEAM-gated).
add_executable(test_async_mutex_acquire_livelock
  test_async_mutex_acquire_livelock.cpp)
target_compile_definitions(test_async_mutex_acquire_livelock PRIVATE
  FIXPP_ASYNC_MUTEX_TEST_SEAM)
target_include_directories(test_async_mutex_acquire_livelock PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(test_async_mutex_acquire_livelock PRIVATE
  asio::asio
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME sync_async_mutex_acquire_livelock
  COMMAND test_async_mutex_acquire_livelock)
set_tests_properties(sync_async_mutex_acquire_livelock PROPERTIES TIMEOUT 60)

# ── 058-async-mutex-hardening Gate-B MAJOR-1: chain-walk CAS-loss ──────────
# deterministic seam witnesses (unlock()'s residual-walk and fresh-FIFO-walk
# `queued -> granted` CAS-loss arms, previously waived as coverage-design
# W-7 / re-labelled W-12 in the verify doc). Same standalone / zero-
# fixpp_sync-linkage ODR discipline as the targets above (the two new
# `unlock_pre_grant_cas_residual` / `unlock_pre_grant_cas_fifo` seam
# call-sites in async_mutex.hpp's unlock() are FIXPP_ASYNC_MUTEX_TEST_SEAM-
# gated). Uses test_seam_mutable_slot (T026 accessor) read-only for the
# ref-balance oracle.
add_executable(test_async_mutex_chain_walk_cas_loss
  test_async_mutex_chain_walk_cas_loss.cpp)
target_compile_definitions(test_async_mutex_chain_walk_cas_loss PRIVATE
  FIXPP_ASYNC_MUTEX_TEST_SEAM)
target_include_directories(test_async_mutex_chain_walk_cas_loss PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(test_async_mutex_chain_walk_cas_loss PRIVATE
  asio::asio
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME sync_async_mutex_chain_walk_cas_loss
  COMMAND test_async_mutex_chain_walk_cas_loss)
set_tests_properties(sync_async_mutex_chain_walk_cas_loss PROPERTIES TIMEOUT 60)

# ── 058-async-mutex-hardening T020/T021/T022: AM-P2-3 bounded exhaustion ────
# research.md D-4/D-7: test_seam_preset_waiter_pool_next /
# test_seam_waiter_pool_next / test_seam_slot_attached_awaiter are
# FIXPP_ASYNC_MUTEX_TEST_SEAM-gated `async_mutex` accessors — same class-
# definition-alters-under-macro hazard as the pop-phase seam above, so this
# target is standalone (zero fixpp_sync linkage) for the identical ODR reason
# documented at the aba_interleave registration.
add_executable(test_pool_exhaustion_reuse
  test_pool_exhaustion_reuse.cpp)
target_compile_definitions(test_pool_exhaustion_reuse PRIVATE
  FIXPP_ASYNC_MUTEX_TEST_SEAM)
target_include_directories(test_pool_exhaustion_reuse PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(test_pool_exhaustion_reuse PRIVATE
  asio::asio
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME sync_pool_exhaustion_reuse
  COMMAND test_pool_exhaustion_reuse)

# ── 058-async-mutex-hardening T023/T024/T026: AM-P3-1/AM-P3-2 impossible-
# state terminate-trap witnesses ───────────────────────────────────────────
# research.md D-5/D-6: uses the FIXPP_ASYNC_MUTEX_TEST_SEAM-gated mutable
# `test_seam_mutable_slot` accessor to fault-inject the provably-impossible
# `granted`-mid-chain-walk / null-`attached_awaiter_` states the T023/T024
# traps exist to catch. Same ODR discipline as the two standalone targets
# above (zero fixpp_sync linkage) — a FIXPP_ASYNC_MUTEX_TEST_SEAM-enabled TU
# must not link any fixpp object that also instantiates async_mutex.hpp's
# inline out-of-line bodies without the macro. Death tests (fork-based
# EXPECT_DEATH), so linked without gtest_main is fine (same as the other two
# standalone targets — GTest::gtest_main still used for a uniform main()).
add_executable(test_am_p3_impossible_state_traps
  test_am_p3_impossible_state_traps.cpp)
target_compile_definitions(test_am_p3_impossible_state_traps PRIVATE
  FIXPP_ASYNC_MUTEX_TEST_SEAM)
target_include_directories(test_am_p3_impossible_state_traps PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(test_am_p3_impossible_state_traps PRIVATE
  asio::asio
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME sync_am_p3_impossible_state_traps
  COMMAND test_am_p3_impossible_state_traps)

# ── 046-atomic-shared-ptr (NFR-017): census-regrowth guard (FR-005 / SC-004) ──
# Rejects any NEW raw std::atomic<std::shared_ptr> in project-owned code (the raw
# form does not compile under libc++). Runs over the real tree → exit 0.
add_test(NAME census_no_raw_atomic_shared_ptr
  COMMAND bash "${CMAKE_SOURCE_DIR}/tools/check_no_raw_atomic_shared_ptr.sh"
    "${CMAKE_SOURCE_DIR}")
# SC-004 mutation discrimination: the guard MUST FAIL on an injected raw
# re-introduction (fixture root carrying a deliberate raw slot). WILL_FAIL flips
# the expected exit so a guard that stopped detecting regrowth turns this RED.
add_test(NAME census_no_raw_atomic_shared_ptr_mutation
  COMMAND bash "${CMAKE_SOURCE_DIR}/tools/check_no_raw_atomic_shared_ptr.sh"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/census_mutation")
set_tests_properties(census_no_raw_atomic_shared_ptr_mutation
  PROPERTIES WILL_FAIL TRUE)
