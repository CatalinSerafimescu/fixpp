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
# FR-003 (010-session-cfg-lifetime): W-5 ASan skip removed. The
# stack-use-after-scope at cfg_ is fixed by the by-value member change
# (T008/T009). session_coverage_adversarial now runs under ASan clean.

# ── 010-session-cfg-lifetime T012 — FR-004 / D-2: LogonReceived observability ──
add_threading_test(session_logon_received_observability logon_received_observability_test.cpp)

# ── 010-session-cfg-lifetime T013/T014 — FR-005 / D-3: send invalid-state gate ─
add_threading_test(session_send_invalid_state session_send_invalid_state_test.cpp)

# ── 010-session-cfg-lifetime T007 — FR-001 / FR-002 / SC-001 ─────────────────
# Lifetime safety tests for by-value SessionConfig cfg_ member (W-5 fix).
# Three runtime contracts verified under both debug and ASan:
#   A. CallerCfgDropsAfterCtor_SessionContinuesWithoutUAF
#   B. CallerMutatesCfgAfterCtor_SessionUnaffected
#   C. MultipleSessionsFromSameCfgEvolveIndependently
add_threading_test(session_cfg_lifetime_safety cfg_lifetime_safety_test.cpp)

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
target_compile_definitions(session_logout_exchange PRIVATE FIXPP_TEST_HOOKS)
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

# ── 010-session-cfg-lifetime Phase 5 (US3) — T016/T017/T018/T019/T020 ────────

# T016+T017 — FSM matrix per-cell witness (FR-006 / SC-002).
# One TEST per enumerated (state×event) cell; uses fsm_visit_history() to
# confirm intermediate and final state transitions.
add_threading_test(session_fsm_matrix_witness fsm_matrix_witness_test.cpp)

# T018 — admin builders per-message-distinct SendingTime (FR-007 / SC-006).
# Exercises the branch where clock advances between two emits yield distinct
# SendingTime(52) values for Heartbeat / Reject / Logout / Logon builders.
add_threading_test(session_admin_builder_distinct_now admin_builder_distinct_now_test.cpp)

# T019 — admin emit mixed-path permutations (FR-008 / SC-005 / RC#G).
# Exercises gated-emit success/failure combinations at Site 1 (Q3 path) and
# Site 2 (Active×Logout confirming emit). Requires FIXPP_TEST_HOOKS for
# SeqnumManager::set_counters_for_test() overflow injection.
add_threading_test(session_admin_emit_mixed_path admin_emit_mixed_path_test.cpp)
target_compile_definitions(session_admin_emit_mixed_path PRIVATE FIXPP_TEST_HOOKS)

# T021 — Initiator transport-throw witness (FR-009 / SC-007).
# Symmetric to the acceptor-side witness in session_send_path (send_path_test.cpp).
# Exercises Session::open() when transport.send() throws during Logon emit;
# asserts dispatch_aborted return and LogonSent FSM end-state per 005 design.
add_threading_test(session_initiator_transport_throw initiator_transport_throw_test.cpp)

# ── T040 (012 US4 seam #6) — Session FSM via mock_transport ─────────────────
# Compile-time smoke that the public mock_transport test header is consumable
# from tests/session/ TUs. Full Logon/Heartbeat/Logout cells are DISABLED_
# pending a session-Phase-4 driver entry point on the Session ABI.
add_executable(session_fsm_via_mock_transport
  test_session_fsm_via_mock_transport.cpp
)
target_link_libraries(session_fsm_via_mock_transport PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME session_fsm_via_mock_transport
         COMMAND session_fsm_via_mock_transport)
set_tests_properties(session_fsm_via_mock_transport PROPERTIES
  LABELS "session;012;us4;seam6;mock")

# ── 013-session-reconnect-binding Phase 3 (RED) test registrations ──────────
# T014–T019: authored red-first per TDD ([const §VII.3]); turns green when
# Phase 3 impl (T023–T028) lands. All 6 use add_threading_test (fixpp_session
# + fixpp_mock_clock + GTest; no mock_transport needed for session-level tests).
# CTest label filter: ctest -R 'reconnect|recovery|reset_seqnum|heartbeat_cadence|logout_timeout'

# T014 — seam reconnect-happy-path: AwaitingResend flag + ResendRequest emit.
add_threading_test(reconnect_happy_path test_reconnect_happy_path.cpp)

# T015 — seam recovery-admin-span-gapfill: all-admin ResendRequest → SequenceReset-GapFill.
add_threading_test(recovery_admin_span_gapfill test_recovery_admin_span_gapfill.cpp)

# T016 — seam recovery-store-horizon: pre-horizon GapFill + PossDupFlag replay.
add_threading_test(recovery_store_horizon test_recovery_store_horizon.cpp)

# T017 — seam reset-seqnum-policy-matrix: 3 modes × 2 roles (6 cells).
# FIXPP_TEST_HOOKS: exposes seqnum_mgr_test_access() on Session for RC#C-1
# counter assertions (gate-b/r2: verify both counters reset to 1 after 141=Y).
add_threading_test(reset_seqnum_policy_matrix test_reset_seqnum_policy_matrix.cpp)
target_compile_definitions(reset_seqnum_policy_matrix PRIVATE FIXPP_TEST_HOOKS)

# T018 — seam heartbeat-cadence-8cell: 4 axes × 2 executor modes.
add_threading_test(heartbeat_cadence_8cell test_heartbeat_cadence_8cell.cpp)

# T019 — seam logout-timeout: initiator + acceptor symmetric cells.
add_threading_test(logout_timeout test_logout_timeout.cpp)

# ── 013-session-reconnect-binding Phase 4 (US2) test registrations ──────────
# T029–T032: CompID↔TLS-identity authorization at Logon. Authored RED-first
# per TDD ([const §VII.3]). Turned GREEN when T034–T036 impl lands.
# CTest filter: ctest -R 'compid_binding'

# T029 — FR-023 / US2 AC6: empty policy rejects ALL Logons (default-deny).
add_threading_test(compid_binding_default_deny
  test_compid_binding_default_deny.cpp)

# T030 — FR-022 / US2 AC5: 4-cell principal-extraction order matrix.
add_threading_test(compid_binding_principal_extraction
  test_compid_binding_principal_extraction.cpp)

# T031 — FR-024 / US2 AC4: symmetric acceptor + initiator coverage.
add_threading_test(compid_binding_symmetric
  test_compid_binding_symmetric.cpp)

# T032 + T021 — [[feedback_half_restructure_symmetric_api]]: invariant-counter witness.
# T021 (014 FR-013a): adds live loopback load_credentials() == 1 counter cell;
# requires fixpp_transport + fixpp_tls + loopback_tls_fixture include path.
add_threading_test(session_invariant_counter_witness
  test_session_invariant_counter_witness.cpp)
target_link_libraries(session_invariant_counter_witness PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
)
target_include_directories(session_invariant_counter_witness PRIVATE
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp"
)
target_compile_definitions(session_invariant_counter_witness PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)

# RC#A (gate-b/r1) — fail-closed mTLS CompID footgun fix counter-test.
# Asserts: mTLS profile + no peer_identity → Disconnected + event.
# Asserts: one_way_ca (non-mTLS) + no override → still reaches Active.
add_threading_test(compid_binding_mtls_fail_closed
  test_compid_binding_mtls_fail_closed.cpp)

# ── 013-session-reconnect-binding Phase 5 (US3) — T040 ring-overflow tests ───
# FR-035 / data-model §E-6: both Session and ListenerEvents rings have capacity
# kSessionEventRingCapacity = 16; emit 17 → oldest dropped, last-16 retained.
# T040 also adds asio_listener::recent_events() (T039 accessor test) but those
# integration cells live in tests/transport/ (transport_tls_validation_failed_taxonomy).
add_threading_test(session_event_ring_overflow
  test_session_event_ring_overflow.cpp)
target_link_libraries(session_event_ring_overflow PRIVATE fixpp_transport)

# ── 013-session-reconnect-binding Phase 6 (US4) — T042 credential rotation ───
# FR-030 / FR-031 / FR-032 / FR-033 / US4 AC1+AC2+AC3 / Clarifications Q4=A
# Tests: null-reject, forwarder-delegates, strong-ref invariant, call-count witness.
# TSan critical: atomic-swap + snapshot read race-free under cross-strand calls.
add_threading_test(session_reload_credentials_in_flight
  test_reload_credentials_in_flight.cpp)

# ── 014-transport-active-binding Phase 3 (US1) — T006/T007/T008 ─────────────
# TDD RED-first per [const §VII.3]; turns GREEN when T009/T010 impl lands.
# CTest filter: ctest -R 'reconnect_live|reconnect_backoff|reconnect_cancel'
#
# T006 — live loopback-TLS reconnect happy-path (SC-001 / FR-001 MVP witness).
# Cell B drives a REAL TLS connect+handshake via loopback and asserts Active.
# FIXPP_TLS_FIXTURE_DIR is wired unconditionally (follows the convention used
# by transport live-TLS tests, e.g. transport_inflight_exclusivity). The cell
# GTEST_SKIP()s only when the cert files are genuinely absent at runtime.
add_executable(reconnect_live_happy_path
  test_reconnect_live_happy_path.cpp)
target_link_libraries(reconnect_live_happy_path PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(reconnect_live_happy_path PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"    # for "transport/asio_listener.hpp" (via loopback_tls_fixture.hpp)
)
target_compile_definitions(reconnect_live_happy_path PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME reconnect_live_happy_path COMMAND reconnect_live_happy_path)
set_tests_properties(reconnect_live_happy_path PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "014;us1;live_tls;reconnect")

# T007 — backoff/cap: connect-fail/handshake-fail/make-fail each consume one attempt.
add_executable(reconnect_backoff_cap
  test_reconnect_backoff_cap.cpp)
target_link_libraries(reconnect_backoff_cap PRIVATE
  fixpp_session
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(reconnect_backoff_cap PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(reconnect_backoff_cap PRIVATE FIXPP_ALLOW_MOCK_TRANSPORT)
add_test(NAME reconnect_backoff_cap COMMAND reconnect_backoff_cap)
set_tests_properties(reconnect_backoff_cap PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "014;us1;reconnect")

# T008 — total-cancel mid-handshake: releases in-flight transport (ASan no-leak).
add_executable(reconnect_cancel_mid_handshake
  test_reconnect_cancel_mid_handshake.cpp)
target_link_libraries(reconnect_cancel_mid_handshake PRIVATE
  fixpp_session
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(reconnect_cancel_mid_handshake PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(reconnect_cancel_mid_handshake PRIVATE FIXPP_ALLOW_MOCK_TRANSPORT)
add_test(NAME reconnect_cancel_mid_handshake COMMAND reconnect_cancel_mid_handshake)
set_tests_properties(reconnect_cancel_mid_handshake PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "014;us1;reconnect;cancel")

# ── 014-transport-active-binding Phase 4 (US2) — T011/T012/T013 ─────────────
# TDD RED-first per [const §VII.3]; turns GREEN when T014/T015 impl lands.
# CTest filter: ctest -R 'live_identity|compid_binding_seam|auth_fail_cap'

# T011 — live identity binding witness: real handshake peer_id drives authorize().
# SC-003 / FR-006 / US2 AC1 / I-4.
add_executable(live_identity_binding
  test_live_identity_binding.cpp)
target_link_libraries(live_identity_binding PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(live_identity_binding PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(live_identity_binding PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME live_identity_binding COMMAND live_identity_binding)
set_tests_properties(live_identity_binding PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "014;us2;live_tls;identity_binding")

# T012 — CompID binding seam: on-list/off-list/absent via logon_peer_identity_override.
# FR-007 / US2 AC2/AC3 / SC-003.
add_executable(compid_binding_seam
  test_compid_binding_seam.cpp)
target_link_libraries(compid_binding_seam PRIVATE
  fixpp_session
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(compid_binding_seam PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME compid_binding_seam COMMAND compid_binding_seam)
set_tests_properties(compid_binding_seam PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "014;us2;compid;seam")

# T013 auth-fail cell lives in reconnect_backoff_cap (already registered above
# as reconnect_backoff_cap). The new AuthFailReconnectCapTest class is compiled
# into the same executable — rebuild it with additional dependencies.
# We patch the existing target to add the session/compid_authorization headers.
# (The executable already links fixpp_session which brings all required libs.)

# ── 014-transport-active-binding Phase 5 (US3) — T016 ────────────────────────
# TDD RED-first per [const §VII.3]; turns GREEN when T017/T018 impl lands.
# CTest filter: ctest -R 'credentials_rotated_emit'
#
# T016 — credential rotation emit: real SHA-256 fingerprints, emit-before-make,
#         first-load no-event, no-op-still-emits.  All 4 cells must execute.
# SC-005 / FR-009/010/011 / US3 AC1/AC2 / I-5/I-6/I-7 / data-model E-3 / C3.
# FIXPP_TLS_FIXTURE_DIR wired unconditionally (Cell 4 needs the live cert).
add_executable(credentials_rotated_emit
  test_credentials_rotated_emit.cpp)
target_link_libraries(credentials_rotated_emit PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(credentials_rotated_emit PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(credentials_rotated_emit PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME credentials_rotated_emit COMMAND credentials_rotated_emit)
set_tests_properties(credentials_rotated_emit PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "014;us3;live_tls;credentials_rotated")

# ── 015-runtime-engine Phase 2 (T004) — SessionId unit test ─────────────────
# TDD RED-first per [const §VII.3]; turns GREEN when T004 impl lands in
# include/fixpp/session/engine.hpp.
# CTest filter: ctest -R 'engine_session_id'

add_executable(engine_session_id_test
  engine_session_id_test.cpp
)
target_link_libraries(engine_session_id_test PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_session_id_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME engine_session_id COMMAND engine_session_id_test)
set_tests_properties(engine_session_id PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "015;foundational;session_id")

# ── Conformance subdirectory (005 Q2 in-scope [FIX-TC] subset) ───────────────
# Phase 3 adds tc_establishment_test.cpp (T020); later phases add more.
add_subdirectory(conformance)
