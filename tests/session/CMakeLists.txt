# tests/session/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

# ── 009-session-fsm-finalize T005 — shared test helper ──────────────────────
# OBJECT library so executables can absorb the helper without an install/link
# step; consumed via $<TARGET_OBJECTS:session_test_support> in test sources.
add_library(session_test_support OBJECT
  support/frame_field_extract.cpp
)
target_include_directories(session_test_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

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

# ── US5 (Phase 7) seam registrations ────────────────────────────────────────
add_threading_test(threading_backpressure_drop_oldest_banned     test_backpressure_drop_oldest_banned.cpp)               # seam 13
add_threading_test(threading_version_registry_missing_dict_layer test_version_registry_missing_routes_to_dict_layer.cpp)  # seam 20

# ── gate-b/r1 RC#1: SecurityProfile sentinel rejection seam ─────────────────
# FR-018 / N-P2-3 / [const §XII.5] — Session::open() rejects kind::unset.
add_threading_test(threading_session_open_rejects_unset_security_profile
  test_session_open_rejects_unset_security_profile.cpp)  # gate-b/r1

# ── 008-message-store seam registrations (T003 helper) ──────────────────────
# Mirrors add_threading_test pattern; store seams use CTest prefix `store_`
# (selects 18 of 21 seams that live in tests/session/; corpus replay is under
# tests/conformance/, alloc-guard under tests/perf/, fuzzer under tests/fuzz/).
# Registrations are added red-first per TDD ([const §VII.3]) as each seam
# lands (T012–T019, T027–T029, T033–T039, T044–T045).
# fixpp_session transitively brings fixpp_core + asio + crc32c (the latter
# linked PRIVATE on fixpp_session once file_store.cpp ships in T024).
# Uses fixpp_add_store_test from cmake/fixpp_store_test.cmake (included at root).
# ── T012–T019 US1 (Phase 3) store seam registrations ────────────────────────
# T012: scripted test-double FSM fixture is header-only; _fixtures_/test_double_fsm.hpp
#       is included directly by each seam test (no separate executable).
#
# T013 — seam 1: MemoryStore round-trip (SC-001)
fixpp_add_store_test(store_memory_round_trip SOURCES
  test_memory_store_round_trip.cpp
  _fixtures_/test_double_fsm.cpp
)

# T014 — seam 2: FileStore crash survival (SC-002 / [2e §D.4])
fixpp_add_store_test(store_file_crash_survival SOURCES
  test_file_store_crash_survival.cpp
  _fixtures_/test_double_fsm.cpp
)

# T014 (split) — seam 2 CompID-validation sub-cases ([2e §D.4] Gap 1 close)
fixpp_add_store_test(store_file_compid_validation SOURCES
  test_file_store_compid_validation.cpp
)

# T015 — seam 3: FileStore torn-write recovery (FR-012 / I-14)
fixpp_add_store_test(store_file_torn_write SOURCES
  test_file_store_torn_write.cpp
  _fixtures_/test_double_fsm.cpp
)

# T016 — seam 7: Outbound store-after-commit (AC US1 #3 / [2b §4.5])
fixpp_add_store_test(store_outbound_post_commit SOURCES
  test_outbound_store_post_commit.cpp
  _fixtures_/test_double_fsm.cpp
)

# T017 — seam 8: retrieve() with gaps ([FIX-SL §4.8.3] / [FIX-SL §4.8.5])
fixpp_add_store_test(store_retrieve_with_gaps SOURCES
  test_retrieve_with_gaps.cpp
  _fixtures_/test_double_fsm.cpp
)

# T018 — seam 10: reset() happy path (FR-010 / I-15 / SC-003)
fixpp_add_store_test(store_reset SOURCES
  test_store_reset.cpp
  _fixtures_/test_double_fsm.cpp
)
# (T019 — seam 17 — store_corpus_replay is registered in tests/conformance/CMakeLists.txt)

# ── T027–T029 US2 (Phase 4) store seam registrations ────────────────────────

# T027 — seam 4: MemoryStore bounded-capacity exhaustion + storage-DoS guard
#         (FR-006 / FR-014 / I-08 / I-11 / SC-004 / AC US2 #1 / #2 / #2a)
fixpp_add_store_test(store_memory_capacity SOURCES
  test_memory_store_capacity.cpp
  _fixtures_/test_double_fsm.cpp
)

# T028 — seam 15: MemoryStore zero-allocator-calls under bounded policy
#         (FR-007 / I-10 / SC-007)
fixpp_add_store_test(store_memory_zero_allocator_calls SOURCES
  test_memory_store_zero_allocator_calls.cpp
  _fixtures_/test_double_fsm.cpp
)

# T029 — seam 19: FileStore::flush_for_session_close() + Session::close(graceful)
#         A1 dispatch (FR-028 / I-17 / AC US2 #4 / #5)
#         Uses Session so it needs GTest and the support headers.
fixpp_add_store_test(store_file_flush_for_session_close SOURCES
  test_file_store_flush_for_session_close.cpp
  _fixtures_/test_double_fsm.cpp
  EXTRA_LIBS fixpp_mock_clock
  EXTRA_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/tests/support"
)

# ── T033–T039 US3 (Phase 5) store seam registrations ────────────────────────

# T033 — seam 5: FIFO-fair concurrent writer (FR-015 / FR-016 / I-01 / SC-005)
fixpp_add_store_test(store_fifo_fair SOURCES
  test_store_fifo_fair.cpp
)

# T034 — seam 6: per-method cancellation contract (FR-020 / I-07 / I-13 / SC-006)
fixpp_add_store_test(store_cancellation_contract SOURCES
  test_store_cancellation_contract.cpp
)

# T035 — seam 9: retrieve-visitor span lifetime + no-mutex-across-co_await
#         (FR-017 / FR-019 / I-03 / I-04 / I-20 / AC US1 #4)
fixpp_add_store_test(store_retrieve_visitor SOURCES
  test_retrieve_visitor.cpp
)

# T036 — seam 10 (crash-cut variants): reset() atomic-rename crash harness
#         (FR-010 / I-15 / SC-003 / AC US3 #4)
fixpp_add_store_test(store_reset_crash_cut SOURCES
  test_store_reset_crash_cut.cpp
)

# F3 resolution (/speckit-verify 2026-05-21): allow store_file_crash_survival
# and store_reset_crash_cut to run under TSan. Both tests fork() and the child
# spawns asio worker threads to drive co_spawn; TSan's default `die_after_fork=1`
# refuses to run worker threads in a multi-threaded-fork child and kills it
# (exit 66). Setting `die_after_fork=0` lets the child continue; TSan does NOT
# instrument post-fork-child races, but the durability invariants these tests
# verify (atomic-rename + parent-dir-fsync + advisory-flock across SIGKILL)
# operate at the kernel-syscall level and are already covered by ASan + UBSan +
# debug runs. The documented limitation is acceptable: the child runs <1s of
# linear file-I/O before SIGKILL, with no concurrent logic to race-track.
#
# OVERWRITE (not APPEND) the ENVIRONMENT property: glibc returns the LAST
# occurrence of duplicate env vars; appending a second TSAN_OPTIONS would
# silently lose the suppressions string set by fixpp_add_store_test. Merge
# die_after_fork=0 with halt_on_error=1 + suppressions into one TSAN_OPTIONS.
set_tests_properties(store_file_crash_survival store_reset_crash_cut PROPERTIES
    ENVIRONMENT
    "TSAN_OPTIONS=die_after_fork=0 halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")

# T037 — seam 16: PMR poison on retrieve recovery path (I-21 / [2a §4.2])
fixpp_add_store_test(store_pmr_poison_retrieve SOURCES
  test_store_pmr_poison_retrieve.cpp
)

# T038 — seam 18: session shutdown ordering (I-22 / SC-005)
fixpp_add_store_test(store_shutdown_ordering SOURCES
  test_store_shutdown_ordering.cpp
)

# T039 — seam 20: seqnum out-of-order rejection under writer mutex
#         (FR-018 / I-05 / AC US3 #5)
fixpp_add_store_test(store_seqnum_out_of_order SOURCES
  test_store_seqnum_out_of_order.cpp
)
# FIXPP_TEST_HOOKS enables test_set_counter() for the overflow test (B3).
target_compile_definitions(store_seqnum_out_of_order PRIVATE FIXPP_TEST_HOOKS)

# ── T044–T045 US4 (Phase 6) QuickFIX compat seam registrations ───────────────

# T044 — seam 11: Path-B compile-time guard (FR-032 / AC US4 #2)
# The build IS the test: static_asserts at file scope fire on regression.
fixpp_add_store_test(store_quickfix_compat_path_b_guard SOURCES
  test_quickfix_compat_path_b_guard.cpp
)

# T045 — seam 12: cfg_loader happy path + CompID-validation defense-in-depth
# (FR-008 / FR-030 / AC US4 #1 / SC-009 / [2e §D.4] defense-in-depth mirror)
fixpp_add_store_test(store_quickfix_compat_cfg_loader SOURCES
  test_quickfix_compat_cfg_loader.cpp
)

# Coverage uplift — F4.3 remediation: FileStore-specific paths in file_store.cpp
# not exercised by existing seam tests (which use MemoryStore for generic ops).
# Covers: next_seqnum(incr=true), retrieve() edge cases, flush_for_session_close(),
# counter-record recovery, and gap/stop/abort visitor paths on FileStore.
fixpp_add_store_test(store_file_coverage_uplift SOURCES
  test_file_store_coverage_uplift.cpp
  _fixtures_/test_double_fsm.cpp
)

# ── 005-session-establishment-fsm seam registrations (T007/T009) ─────────────
# Seam #13: seqnum_t consumer/build check (T007 red → T008 green).
# Seam #9:  fix_time format↔parse roundtrip corpus (T009 red → T011 green).
# Both use add_threading_test (fixpp_session + GTest; no mock_clock needed for
# these two scaffolding seams — they test pure-value types).

# Seam #13: T007 seqnum_t handoff test.
add_threading_test(session_seqnum_t_handoff seqnum_t_handoff_test.cpp)

# Seam #9: T009 fix_time roundtrip test. fixpp_core provides the implementation
# (fix_time.cpp compiled into fixpp_core at T011 / src/core/CMakeLists.txt).
add_threading_test(session_fix_time_roundtrip fix_time_roundtrip_test.cpp)

# ── 005 Phase 3 (US1) — seam #2: Logon handshake test (T019) ────────────────
# Red-first TDD: authored before T021–T026 implementation; must turn green
# at T026. Links fixpp_mock_clock for deterministic SendingTime assertions.
add_threading_test(session_logon_handshake logon_handshake_test.cpp)

# ── 005 Phase 8 /speckit-verify Step 4 coverage uplift ──────────────────────
# Adversarial-input tests for defensive code paths that the well-formed
# seam corpus doesn't exercise: builder buffer-too-small, parser malformed
# field fallback, Logon-ack seq=0, Logout cancel_sleeps mid-graceful.
add_threading_test(session_coverage_adversarial coverage_adversarial_test.cpp)

# ── 005 Phase 4 (US2) — seam #3/#4/#10: seqnum tests (T027/T028/T029) ───────
# Red-first TDD: authored before T031–T036 implementation.
# session_seqnum_manager: seam #3, SeqnumManager unit tests (T027).
# session_seqnum_gap_fatal: seam #4, too-high gap fatal FSM tests (T028).
# session_durable_before_transmit: seam #10, store-before-write ordering (T029).

# Seam #3: SeqnumManager increment/overflow/fatal checks (T027).
# FIXPP_TEST_HOOKS enables set_counters_for_test() for the overflow test.
add_executable(session_seqnum_manager seqnum_manager_test.cpp)
target_link_libraries(session_seqnum_manager PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_seqnum_manager PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_seqnum_manager PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_seqnum_manager COMMAND session_seqnum_manager)
set_tests_properties(session_seqnum_manager PROPERTIES ENVIRONMENT
  "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")

# Seam #4: Too-high gap fatal disposition (T028).
add_threading_test(session_seqnum_gap_fatal seqnum_gap_fatal_test.cpp)

# Seam #10: Durable-before-transmit ordering (T029).
add_threading_test(session_durable_before_transmit durable_before_transmit_test.cpp)

# ── 005 Phase 5 (US3) — seam #5: Heartbeat/TestRequest test (T037) ───────────
# Red-first TDD: authored before T039–T042 implementation; turns green at T042.
# Tests: build_heartbeat/build_test_request builders; inbound TestRequest echo;
# HeartBtInt=0 disable; unanswered-TR disconnect; inbound Heartbeat liveness.
add_threading_test(session_heartbeat_testrequest heartbeat_testrequest_test.cpp)

# ── 005 Phase 6 (US4) — seam #6/#11: Logout exchange + cancellation (T043/T044)
# Red-first TDD: authored before T046–T048; turns green at T049.
# seam #6: Logout exchange — graceful both-directions; never-confirmed timeout;
#          non-Active Logout transitions; outbound store-before-transmit (T029 retire).
# seam #11: Two-phase cancellation — child cancellation_state; close idempotent;
#           close(terminal) skips phase 1.
add_threading_test(session_logout_exchange logout_exchange_test.cpp)
add_threading_test(session_cancellation_two_phase cancellation_two_phase_test.cpp)

# ── 005 Phase 7 (US5) — seam #7/#8: Reject + SendingTime tests (T050/T051) ────
# Red-first TDD: authored before T054/T056 implementation; turns green at T057.
# seam #7: session_reject — build_reject shape; no-reject-loop (I-5); type-invalid-for-state.
# seam #8: sending_time  — check_sending_time unit tests; Q3 integration (Active + Logon paths).
add_threading_test(session_reject      session_reject_test.cpp)
add_threading_test(session_sending_time sending_time_test.cpp)

# ── 005 Phase 8 — T058: Seam #1 FSM transition matrix (FR-001 / I-1) ────────
# Meta-attestation that every state×event cell is defined (no UB / no silent
# no-op). Detailed per-cell behaviour is the per-US seam suite's coverage.
add_threading_test(session_fsm_transition_matrix fsm_transition_matrix_test.cpp)

# ── 005 Phase 8 — T060: noexcept window + no-C++-across-C-ABI layering ──────
# Compile-time static_assert + nm/grep checks for FR-015 / [const §X.2].
# Needs FIXPP_TEST_BINARY_DIR + FIXPP_TEST_SOURCE_DIR for the nm/grep probes.
add_threading_test(session_layering test_session_layering.cpp)
target_compile_definitions(session_layering PRIVATE
  FIXPP_TEST_BINARY_DIR="${CMAKE_BINARY_DIR}"
  FIXPP_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ── 009-session-fsm-finalize T006 — Session::send path test (US1 FR-001) ────
# TDD red: T006 authors RED (compile + fail); T007 + T008 turn GREEN.
# Uses session_test_support OBJECT library (T005 frame_field_extract helper).
# F3 (Round-A drift): FIXPP_TEST_HOOKS enables seqnum_mgr_test_access() on Session
# so tests can verify SeqnumManager counter consistency after send().
add_threading_test(session_send_path send_path_test.cpp
  $<TARGET_OBJECTS:session_test_support>)
target_compile_definitions(session_send_path PRIVATE FIXPP_TEST_HOOKS)

# ── 009-session-fsm-finalize Phase 6 (US4) — T019/T021 ──────────────────────

# T019 — cross-session TestReqID disjointness + monotonicity (FR-010 / SC-003).
# Registered under BOTH debug (assertion-level checks) AND tsan (data-race check).
# The tsan preset selection is handled by the preset's toolchain; both CTest entries
# exist unconditionally and the TSan-specific race detection fires only on the
# linux-clang-tsan build.
#
# CTest label `session_` picks this test up with `-R 'session_|tc_'`.
add_threading_test(session_test_request_id_cross_session_race
  test_test_request_id_cross_session_race.cpp)

# T021 — SeqnumManager::drain() called in close() (FR-011 / SC-004).
# Tests: CloseWithHolderDoesNotTerminate + NeverOpenedDestructionSafe +
#        OpenThenCloseTerminalIdempotent + DrainCalledByClose.
# RED before T022 (close() does not call drain); GREEN after T022.
# FIXPP_TEST_HOOKS: exposes seqnum_mgr_test_access() on Session and
# mutex_test_access() on SeqnumManager (needed for holder-during-drain scenario).
add_threading_test(session_seqnum_drain_on_close
  test_seqnum_drain_on_close.cpp)
target_compile_definitions(session_seqnum_drain_on_close PRIVATE FIXPP_TEST_HOOKS)

# ── Conformance subdirectory (005 Q2 in-scope [FIX-TC] subset) ───────────────
# Phase 3 adds tc_establishment_test.cpp (T020); later phases add more.
add_subdirectory(conformance)
