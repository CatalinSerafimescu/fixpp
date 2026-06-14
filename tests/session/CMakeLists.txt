# tests/session/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

# OpenSSL/zlib resolution for the live-TLS engine + CompID-binding tests that
# link fixpp_transport directly (015 engine_* + 013/014 binding witnesses).
# fixpp_tls/fixpp_transport's PRIVATE OpenSSL is not propagated to test
# executables linking the static libs — same pattern as tests/transport and
# tests/tls. find_library may MISS under sanitizer presets (conan emits a
# non-_RELEASE config suffix, so openssl_LIB_DIRS_RELEASE is empty); the
# OpenSSL::/ZLIB:: target fallback then resolves it regardless of config.
# (015 /speckit-verify: tests/session consumed FIXPP_TRANSPORT_OPENSSL_*_LIB
# without resolving them itself, breaking the sanitizer/CI build.)
find_package(OpenSSL 3.0 REQUIRED)
find_library(FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB
    NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_library(FIXPP_TRANSPORT_OPENSSL_SSL_LIB
    NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_package(ZLIB REQUIRED)
find_library(FIXPP_TRANSPORT_ZLIB_LIB
    NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
if(FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB-NOTFOUND"
   OR NOT FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB)
    set(FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(FIXPP_TRANSPORT_OPENSSL_SSL_LIB STREQUAL "FIXPP_TRANSPORT_OPENSSL_SSL_LIB-NOTFOUND"
   OR NOT FIXPP_TRANSPORT_OPENSSL_SSL_LIB)
    set(FIXPP_TRANSPORT_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(FIXPP_TRANSPORT_ZLIB_LIB STREQUAL "FIXPP_TRANSPORT_ZLIB_LIB-NOTFOUND"
   OR NOT FIXPP_TRANSPORT_ZLIB_LIB)
    set(FIXPP_TRANSPORT_ZLIB_LIB ZLIB::ZLIB)
endif()

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

# 034-credential-store-redaction — masker unit (Phase 2) + disk-byte / no-op /
# fault-injection witnesses (Phases 3-6). Uses Session + FileStore fixtures, so
# it mirrors the flush-test registration (FSM double + mock clock + support dir).
# FIXPP_TEST_HOOKS exposes store_then_emit_test_access() for the T010 over-bound
# fault-injection cell (frame-injection seam; methods-only, no Session layout change).
fixpp_add_store_test(credential_store_redaction SOURCES
  test_credential_store_redaction.cpp
  _fixtures_/test_double_fsm.cpp
  EXTRA_LIBS fixpp_mock_clock
  EXTRA_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/tests/support"
)
target_compile_definitions(credential_store_redaction PRIVATE FIXPP_TEST_HOOKS)

# T011 alloc gate — dual-gate per [[feedback_tracking_pmr_resource_false_pass]]:
# the NoHeap.StorePath_NoNewAllocation cell runs in the base target (weak no-op
# hooks); the _mallocnesia variant re-runs ONLY that cell under LD_PRELOAD via
# check_alloc.py so a global-heap escape in the copy+mask window aborts.
# GTEST_FILTER isolates the NoHeap cell (the FileStore/thread-pool cells need not
# run under interception). Absent mallocnesia.so → skip (base test is the fallback).
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME credential_store_redaction_mallocnesia
    COMMAND
      python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
      "--binary" "$<TARGET_FILE:credential_store_redaction>"
  )
  set_tests_properties(credential_store_redaction_mallocnesia PROPERTIES
    ENVIRONMENT "GTEST_FILTER=NoHeap.*"
    LABELS "034;credential-store-redaction;alloc_guard"
    DEPENDS credential_store_redaction)
endif()

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

# ── 035-filestore-io-offload test registrations (T001 Setup) ─────────────────
# Three new test targets: thread-id offload witnesses (US1/US2, SC-001/002),
# per-method cancellation contract (US2, SC-004), and real-pool concurrency +
# TSan/ASan witness (US3, SC-005). All use fixpp_add_store_test so the TSan
# suppressions file is wired consistently (asio any_executor refcount fence).
# Sanitizer preset selection is toolchain-level; all three targets build under
# all presets unconditionally (same pattern as store_file_crash_survival above).
# RED test cells are added in Phase 3 (T005), Phase 4 (T008/T009), and Phase 5
# (T013) respectively. These skeleton targets compile with a single Smoke test.

# T001/T005: thread-id offload witnesses + saturation/deadlock edge cases.
# FIXPP_TEST_HOOKS exposes the g_offload_syscall_tid / _entered / _done probes
# written by the offloaded lambda; required for SC-001 / SC-002 thread-id cells.
fixpp_add_store_test(store_file_offload_thread SOURCES
  test_file_store_offload_thread.cpp
)
target_compile_definitions(store_file_offload_thread PRIVATE FIXPP_TEST_HOOKS)

# T001/T009: FileStore per-method cancellation contract (binary §6.1.4).
# FIXPP_TEST_HOOKS exposes install_store_offload_probe — needed for arm (b)
# mid-syscall witnesses and the no-wedge cell (same as store_file_offload_thread).
fixpp_add_store_test(store_file_cancellation SOURCES
  test_file_store_cancellation.cpp
)
target_compile_definitions(store_file_cancellation PRIVATE FIXPP_TEST_HOOKS)

# T001/T013: real-pool concurrent store/retrieve/reset TSan+ASan witness.
# This target is the PRIMARY TSan gate for the 035 feature (the inert offload
# made the concurrency surface dormant; making it genuine is the whole risk).
# FIXPP_TEST_HOOKS exposes read_and_reset_retrieve_pread_count() and
# read_and_reset_catch_fired() for the T013/T015 generation-guard witnesses.
fixpp_add_store_test(store_file_concurrent_tsan SOURCES
  test_file_store_concurrent_tsan.cpp
)
target_compile_definitions(store_file_concurrent_tsan PRIVATE FIXPP_TEST_HOOKS)

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

# ── 036-admin-emit-toadmin-coverage T004/T013 (Invocation 1) ────────────────
# Vertical-slice proof: wire fire_to_admin_ at emit_session_reject_ (T015) and
# Guard-3 Logout (T023). FIXPP_TEST_HOOKS needed for US3 BMR cells (T027/T028):
# seqnum_mgr_test_access().peek_outbound() witnesses no outbound seqnum consumed on veto.
# Cells: EmitSessionReject_FromAdminVeto (T004) + Logout_Guard3LogonAckSendingTime (T013).
# Anchors: spec.md FR-001/FR-002; plan.md ARM-1; session.cpp:331/1728/3368.
add_threading_test(session_admin_emit_toadmin_coverage test_admin_emit_toadmin_coverage.cpp)
# FIXPP_TEST_HOOKS exposes seqnum_mgr_test_access() for the T027/T028 BMR outbound-seqnum
# discriminating witness (INV-COV-5 parity; C2: assign_outbound only on !suppressed).
target_compile_definitions(session_admin_emit_toadmin_coverage PRIVATE FIXPP_TEST_HOOKS)
set_tests_properties(session_admin_emit_toadmin_coverage PROPERTIES
  LABELS "036;us1;us2;us3;bmr;toadmin_coverage;toapp")

# SC-002 static enumeration gate (T041) — gate-b/r1 FQ-1.
# Parses src/session/session.cpp and asserts every build_reject( / build_logout(
# site has fire_to_admin_( between the builder and its store_then_emit(, and
# every build_business_message_reject( site has ->toApp( in the same interval.
# Exits 1 (prints offending line) if any admin emit site is unwired — catches a
# FUTURE unwired site even when the dynamic scenario cells pass.
# Anchors: spec.md SC-002/FR-001/FR-005; tasks.md T041; research.md Decision-1.
add_test(NAME session_admin_emit_observation_gate
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_admin_emit_observation.py"
    "${CMAKE_SOURCE_DIR}/src/session/session.cpp"
)
set_tests_properties(session_admin_emit_observation_gate PROPERTIES
  LABELS "036;sc002;static_gate")

# ── 037-resend-reply-possdup-tags T001/T004 (Cell 1) ─────────────────────────
# Direct builder unit test: build_sequence_reset_gapfill → assert 43=Y + 122=<own 52>.
# No Session/io_context/mock_clock required — pure free-function call + field walk.
# T001: test file; T004: Cell 1 assertions; T005: confirmed RED; T008: confirmed GREEN.
# Anchors: spec.md FR-001/FR-002/FR-003; plan.md T004; tasks.md §Phase 3.
add_threading_test(session_resend_reply_possdup test_resend_reply_possdup.cpp)
set_tests_properties(session_resend_reply_possdup PROPERTIES
  LABELS "037;us1;possdup;gapfill")

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

# S-023: inbound SequenceReset(35=4) NewSeqNo(36) application (GapFill + Reset
# arms). FIXPP_TEST_HOOKS exposes seqnum_mgr_test_access() to read next-inbound.
add_threading_test(session_inbound_sequence_reset test_inbound_sequence_reset.cpp)
target_compile_definitions(session_inbound_sequence_reset PRIVATE FIXPP_TEST_HOOKS)

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

# T012/T021 — CompID binding no-identity gate arms (fail-closed mTLS / permissive
# non-mTLS); the on-list/off-list seam cells were removed in 015 US4 (SC-006).
# FR-007/FR-009 / US2 AC3 / SC-003/SC-006.
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

# ── 015-runtime-engine T019 (US4) — seam-removal grep gate + live-binding ────
# SC-006/FR-009: zero per-config peer-identity test-seam references across
# src/+include/+tests/, AND the binding gate authorizes from a live handshake
# identity (the production live_peer_id_ path), not a config seam.
add_executable(engine_seam_removal
  engine_seam_removal_test.cpp)
target_link_libraries(engine_seam_removal PRIVATE
  fixpp_session
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_seam_removal PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(engine_seam_removal PRIVATE
  FIXPP_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME engine_seam_removal COMMAND engine_seam_removal)
set_tests_properties(engine_seam_removal PROPERTIES
  LABELS "015;us4;compid;seam_removal")

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

# ── 015-runtime-engine Phase 2 (T007) — Engine harness compile smoke ─────────
# Proves engine_loopback_harness.hpp compiles and the Engine ctor/stop() shape
# is correct. Behavioral coverage is provided by US1/US2/US3 live tests.
# CTest filter: ctest -R 'engine_harness_compile_smoke'
add_executable(engine_harness_compile_smoke_test
  engine_harness_compile_smoke_test.cpp
)
target_link_libraries(engine_harness_compile_smoke_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_harness_compile_smoke_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp" (via loopback_tls_fixture.hpp)
)
target_compile_definitions(engine_harness_compile_smoke_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME engine_harness_compile_smoke COMMAND engine_harness_compile_smoke_test)
set_tests_properties(engine_harness_compile_smoke PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "015;foundational;harness;compile_smoke")

# ── 015-runtime-engine Phase 3 (US1) — T008/T009/T010 ───────────────────────
# TDD RED-first per [const §VII.3]; turns GREEN when T011/T012/T013 impl lands.
# CTest filter: ctest -R 'engine_acceptor|engine_firstframe'
#
# All three tests drive the Engine through its public API over real loopback-TLS.
# They need fixpp_session + fixpp_transport + fixpp_tls + OpenSSL libs + the
# src/ include path (for loopback_tls_fixture.hpp) and FIXPP_TLS_FIXTURE_DIR.
# Mirrors the reconnect_live_happy_path and live_identity_binding test targets.

# T008 — on-list live identity admits to established state (SC-001 / US1 AC1).
add_executable(engine_acceptor_test
  engine_acceptor_test.cpp
)
target_link_libraries(engine_acceptor_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_acceptor_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp" (via loopback_tls_fixture.hpp)
)
target_compile_definitions(engine_acceptor_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
  # exposes seqnum_mgr_test_access().next_inbound_unsafe() for the coalesced
  # first-frame surplus witness (F-015-002).
  FIXPP_TEST_HOOKS
)
add_test(NAME engine_acceptor COMMAND engine_acceptor_test)
set_tests_properties(engine_acceptor PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "015;us1;live_tls;acceptor")

# T009 — off-list + absent/delayed identity → fail-CLOSED (SC-002 / US1 AC2).
add_executable(engine_acceptor_failclosed_test
  engine_acceptor_failclosed_test.cpp
)
target_link_libraries(engine_acceptor_failclosed_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_acceptor_failclosed_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(engine_acceptor_failclosed_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME engine_acceptor_failclosed COMMAND engine_acceptor_failclosed_test)
set_tests_properties(engine_acceptor_failclosed PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "015;us1;live_tls;acceptor;failclosed")

# T010 — bounded pre-session window: stall/silent/partial/over-budget →
#         close + reclaim (SC-011 / FR-014 / C1/C5).
add_executable(engine_firstframe_test
  engine_firstframe_test.cpp
)
target_link_libraries(engine_firstframe_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_firstframe_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(engine_firstframe_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME engine_firstframe COMMAND engine_firstframe_test)
set_tests_properties(engine_firstframe PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "015;us1;live_tls;firstframe;window")

# ── 015-runtime-engine Phase 4 (US2) — T014 ────────────────────────────────
# TDD RED-first per [const §VII.3]; turns GREEN when T015 (real read-pump) lands.
# CTest filter: ctest -R 'engine_readpump'
#
# T014 — read-pump delivers subsequent frames exactly-once in order (SC-003 /
#         US2 AC1/AC2; FR-004/012; C2). Observability via next_inbound_unsafe()
#         (FIXPP_TEST_HOOKS) and Session::state().
add_executable(engine_readpump_test
  engine_readpump_test.cpp
)
target_link_libraries(engine_readpump_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_readpump_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp" (via loopback_tls_fixture.hpp)
)
target_compile_definitions(engine_readpump_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
  FIXPP_TEST_HOOKS
)
add_test(NAME engine_readpump COMMAND engine_readpump_test)
set_tests_properties(engine_readpump PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "015;us2;live_tls;readpump")

# ── 015-runtime-engine Phase 4 (US2) — T016(a) ──────────────────────────────
# TDD RED-first per [const §VII.3]; turns GREEN when T016(b)-(e) impl lands.
# CTest filter: ctest -R 'engine_connect'
#
# T016(a) — initiator connect-then-Logon: server receives initiator Logon on
#            the wire; session reaches Active; read-pump advances next_inbound.
#            (FR-003 / C2i / E-1a / SC-010)
# Observability via next_inbound_unsafe() (FIXPP_TEST_HOOKS) + Session::state().
add_executable(engine_connect_test
  engine_connect_test.cpp
)
target_link_libraries(engine_connect_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_connect_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp" (via loopback_tls_fixture.hpp)
)
target_compile_definitions(engine_connect_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
  FIXPP_TEST_HOOKS
)
add_test(NAME engine_connect COMMAND engine_connect_test)
set_tests_properties(engine_connect PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "015;us2;live_tls;connect")

# CTest filter: ctest -R 'engine_lifecycle'
#
# T017 [US3] — programmatic multi-session lifecycle: register initiator+acceptor,
#   non-blocking start, both loops run + both lookup()-retrievable by SessionId
#   (null before open, non-null after), clean idempotent stop, duplicate-identity
#   rejection. (FR-001/002/011 / SC-004/005 / C5/C6 / E-5/E-7)
add_executable(engine_lifecycle_test
  engine_lifecycle_test.cpp
)
target_link_libraries(engine_lifecycle_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_lifecycle_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp" (via loopback_tls_fixture.hpp)
)
target_compile_definitions(engine_lifecycle_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
  FIXPP_TEST_HOOKS
)
add_test(NAME engine_lifecycle COMMAND engine_lifecycle_test)
set_tests_properties(engine_lifecycle PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "015;us3;live_tls;lifecycle")

# ── gate-b/r1 FQ-1: live outbound serialization + error propagation ──────────
# Proves (1a) write-error propagates through store_then_emit → FSM → Disconnected
# and (1b) at most one async_write is in-flight per Transport (≤1-in-flight
# contract, transport.hpp:47-50). Uses custom transport doubles (no live TLS).
# CTest filter: ctest -R 'live_outbound_serialized'
add_threading_test(live_outbound_serialized test_live_outbound_serialized.cpp)
target_include_directories(live_outbound_serialized PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)

# ── 016-interop-harness T007/T008 — reconnect-policy / down-peer stop() witness ──
# RED witness for the 015 down-peer carry-forward: Engine::stop() must return within
# a bound when an initiator targets an unreachable peer (the in-flight async_connect
# must be promptly cancellable). T008 (SessionConfig reconnect-policy field + bounded
# cancellable connect) makes it GREEN.
add_executable(reconnect_policy_witness_test
  reconnect_policy_witness_test.cpp
)
target_link_libraries(reconnect_policy_witness_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(reconnect_policy_witness_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(reconnect_policy_witness_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME reconnect_policy_witness COMMAND reconnect_policy_witness_test)
set_tests_properties(reconnect_policy_witness PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "016;us-foundational;reconnect;down-peer;watchdog"
  TIMEOUT 60)

# ── 017-log-otel T013 — SessionConfig amendment regression ──────────────────
# Asserts logger_override/tracer_override present + default-null, and that
# log_sink_override is absent (compile-time static_asserts are the real gate).
# Links fixpp_session (brings session_config.hpp).
add_executable(session_config_017_amendment_test test_017_session_config_amendment.cpp)
target_link_libraries(session_config_017_amendment_test PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME session_config_017_amendment COMMAND session_config_017_amendment_test)

# ── 017-log-otel T036 — trace_context accessor regression ────────────────────
# Verifies (1) Session::get_trace_context() returns initial_trace_context after
# open(); (2) trace_context_value() is removed (compile-time static_assert);
# (3) Engine::engine_trace_context() returns the seeded snapshot (TS-6b).
# Links fixpp_session + fixpp_mock_clock (for deterministic Clock seeding).
add_executable(session_trace_context_accessors_test
  test_trace_context_accessors.cpp
)
target_link_libraries(session_trace_context_accessors_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_trace_context_accessors_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_trace_context_accessors
  COMMAND session_trace_context_accessors_test)

# ── 019-app-callbacks T006 — MsgType classifier unit test ───────────────────
# Pins the admin/app classifier truth table (all 6 admin MsgTypes + sample app
# MsgTypes + edge cases). Links fixpp_session (brings src/ internal include path
# for msgtype_classifier.hpp). src/ added as PRIVATE include dir.
add_executable(msgtype_classifier_019_test
  test_019_msgtype_classifier.cpp
)
target_link_libraries(msgtype_classifier_019_test PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(msgtype_classifier_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
)
add_test(NAME msgtype_classifier_019 COMMAND msgtype_classifier_019_test)
set_tests_properties(msgtype_classifier_019 PROPERTIES
  LABELS "019;foundational;classifier")

# ── 019-app-callbacks T003/T005/T007 — Application compile-smoke ─────────────
# Proves application.hpp compiles + Application is subclassable + EngineConfig
# gains the application field + callback_dispatch_scope + invoke_callback_safe.
add_executable(application_019_compile_smoke_test
  test_019_application_compile_smoke.cpp
)
target_link_libraries(application_019_compile_smoke_test PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME application_019_compile_smoke COMMAND application_019_compile_smoke_test)
set_tests_properties(application_019_compile_smoke PROPERTIES
  LABELS "019;foundational;compile_smoke;application")

# ── 019-app-callbacks T008 — Application inbound delivery tests ──────────────
# fromApp/fromAdmin fire exactly once on exec_ with correct MessageView+SessionId;
# INV-6 FSM-rejected messages don't reach callbacks; SC-001 ordering;
# application==nullptr → no callbacks (FR-014 byte-identity); fromAdmin reject
# → session Reject(35=3).
# US1 AC1/AC2/AC4; FR-002/003/004/005/014; INV-1/4/6; SC-001/003/006.
add_executable(application_inbound_019_test
  test_application_inbound.cpp
)
target_link_libraries(application_inbound_019_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(application_inbound_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME application_inbound_019 COMMAND application_inbound_019_test)
set_tests_properties(application_inbound_019 PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "019;us1;inbound;application")

# ── 019-app-callbacks T009 — BusinessMessageReject(35=j) tests ───────────────
# MANDATORY named test (Gate A RC#3): fromApp reject → BusinessMessageReject(35=j)
# with RefMsgType(372)/RefSeqNum(45)/BusinessRejectReason(380) present; NOT
# session Reject(35=3). fromApp accept → no reject on wire. US1 AC3; FR-005;
# SC-003; research D4; [FIX50SP2] Infrastructure / Business Rejects (A-014).
add_executable(application_business_reject_019_test
  test_application_business_reject.cpp
)
target_link_libraries(application_business_reject_019_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(application_business_reject_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME application_business_reject_019 COMMAND application_business_reject_019_test)
set_tests_properties(application_business_reject_019 PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "019;us1;business_reject;application")

# ── 019-app-callbacks T012 — outbound origination + interception tests ───────
# US2: Engine::send crosses wire after toApp; toApp veto drops transmit; toAdmin
# fires on admin emits; non-established session → state error; unknown id → 119.
add_executable(application_outbound_019_test
  test_application_outbound.cpp
)
target_link_libraries(application_outbound_019_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(application_outbound_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME application_outbound_019 COMMAND application_outbound_019_test)
set_tests_properties(application_outbound_019 PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "019;us2;outbound;application")

# ── 019-app-callbacks T015 — lifecycle notifications tests ───────────────────
# US3: onCreate/onLogon/onLogout exactly-once, in order, on exec_; three onLogout
# exit paths (graceful, terminal, callback-threw); fromAdmin completeness for
# inbound Logout(35=5) and SequenceReset(35=4). FR-009; FR-004; INV-7.
add_executable(application_lifecycle_019_test
  test_application_lifecycle.cpp
)
target_link_libraries(application_lifecycle_019_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(application_lifecycle_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME application_lifecycle_019 COMMAND application_lifecycle_019_test)
set_tests_properties(application_lifecycle_019 PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "019;us3;lifecycle;application")

# ── 019-app-callbacks T017 — strand serialization, drain, keepalive tests ────
# FR-010/012; INV-2/INV-3; SC-005; research D3.
# TSan-gated: NoConcurrentCallbacksForOneSession proves no data race on session
# members under concurrent inbound. ASan-gated: DrainBeforeDestroy + keepalive.
# All tests have internal self-deadlines (ioc.run_for → no hang).
add_executable(application_strand_019_test
  test_application_strand.cpp
)
target_link_libraries(application_strand_019_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(application_strand_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME application_strand_019 COMMAND application_strand_019_test)
set_tests_properties(application_strand_019 PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "019;phase6;strand;drain;application")

# ── 019-app-callbacks T018 — throw-from-callback disposal at every site ───────
# FR-011; 7 cells: fromApp/fromAdmin/toApp/toAdmin/onCreate/onLogon/onLogout.
# Each cell: engine catches throw, terminal-closes session, re-entrancy guard
# cleared, engine not corrupted (second session works). All self-deadlined.
add_executable(application_throw_019_test
  test_application_throw.cpp
)
target_link_libraries(application_throw_019_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(application_throw_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME application_throw_019 COMMAND application_throw_019_test)
set_tests_properties(application_throw_019 PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "019;phase6;throw;application")

# ── gate-b/r1 FIX-5 — real Engine::send tests (replace substituted tests) ─────
# Exercises Engine::send public API: (a) send from foreign thread crosses wire,
# (b) re-entrant send from inside toApp does not deadlock, (c) send_counter_
# drain race (stop() + ~Engine() UAF-safe). All use real loopback TLS;
# GTEST_SKIP when FIXPP_TLS_FIXTURE_DIR absent. [FR-006/012; FIX-1/FIX-2/FIX-5]
add_executable(application_engine_send_019_test
  test_application_engine_send.cpp
)
target_link_libraries(application_engine_send_019_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  fixpp_mock_clock
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(application_engine_send_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(application_engine_send_019_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME application_engine_send_019 COMMAND application_engine_send_019_test)
set_tests_properties(application_engine_send_019 PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "019;gate_b;engine_send;application;live_tls"
  TIMEOUT 30)

# ── 019-app-callbacks T020 — G2 enablement witness ───────────────────────────
# SC-002: opaque NewOrderSingle (35=D) → ExecutionReport (35=8) round-trip via
# Engine::send + fromApp callbacks. Real loopback-TLS (mirrors engine_lifecycle
# pattern); GTEST_SKIP when FIXPP_TLS_FIXTURE_DIR absent. Proves G2 is
# implementable on the 019 public surface.
add_executable(g2_enablement_witness_019_test
  test_019_g2_enablement_witness.cpp
)
target_link_libraries(g2_enablement_witness_019_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(g2_enablement_witness_019_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(g2_enablement_witness_019_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME g2_enablement_witness_019 COMMAND g2_enablement_witness_019_test)
set_tests_properties(g2_enablement_witness_019 PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "019;phase7;g2;witness;live_tls"
  TIMEOUT 30)

# ── 020-g2-business-messages US1 ─────────────────────────────────────────────

# T005 — builder unit tests: INV-2/3/4, FR-008, alloc witness.
# Links fixpp_session only (no generated v44 headers needed — builder is pure).
add_executable(business_messages_build_test
  test_business_messages_build.cpp
)
target_link_libraries(business_messages_build_test PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(business_messages_build_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME business_messages_build COMMAND business_messages_build_test)
set_tests_properties(business_messages_build PROPERTIES
  LABELS "020;us1;build;builder")

# T006 — typed read flyweight tests (AS4; FR-006).
# Needs the generated fixpp::v44 headers + parser. Link fixpp_dictionary which
# carries the generated include dir (same pattern as dictionary reify tests).
if(TARGET fixpp_codegen_generate)
  set(_BM_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

  add_executable(business_messages_read_test
    test_business_messages_read.cpp
  )
  add_dependencies(business_messages_read_test fixpp_codegen_generate)
  target_link_libraries(business_messages_read_test PRIVATE
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(business_messages_read_test PRIVATE
    "${_BM_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
  )
  add_test(NAME business_messages_read COMMAND business_messages_read_test)
  set_tests_properties(business_messages_read PROPERTIES
    LABELS "020;us1;read;flyweight")
else()
  message(STATUS
    "tests/session: fixpp_codegen_generate not available; skipping business_messages_read_test")
endif()

# ── 020-g2-business-messages US1 send-path ──────────────────────────────────
#
# T007 — send-path + round-trip RED suite (INV-1/5/7/8; FR-004a/009/016).
#   INV-1: wire field order (MsgType field-3) + digit-only BodyLength + checksum.
#   INV-8: opaque-payload validation, no seqnum consumed on rejection.
#   INV-5: inbound-reject → BusinessMessageReject(35=j) + session stays Active.
#   INV-7: re-entrant Engine::send from fromApp under multi-threaded executor
#          (asio::thread_pool 2+; GTEST_SKIP when FIXPP_TLS_FIXTURE_DIR absent).
#   INV-1 + INV-8 + INV-5 are TLS-FIXTURE-FREE and always run.
#   INV-7 needs live TLS (same skip contract as engine_send/g2_enablement_witness).
#
# Links fixpp_session + transport/TLS (for INV-7 live engine path) + fixpp_mock_clock.
add_executable(business_messages_roundtrip_test
  test_business_messages_roundtrip.cpp
)
target_link_libraries(business_messages_roundtrip_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  fixpp_mock_clock
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(business_messages_roundtrip_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(business_messages_roundtrip_test PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME business_messages_roundtrip COMMAND business_messages_roundtrip_test)
set_tests_properties(business_messages_roundtrip PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "020;us1;roundtrip;send_path;inv1;inv5;inv7;inv8"
  TIMEOUT 60)

# ── 021-inbound-possdup-origsendingtime T004/T005 — Arm A/B tolerance tests ───
# Stage-2 too-low possdup disposition (Arm A tolerate / Arm B fatal).
# data-model.md §1 Stage 2; contracts/session-possdup.md C1/C3; research.md D1/D2/D3.
# FIXPP_TEST_HOOKS: exposes seqnum_mgr_test_access().next_inbound_unsafe() for INV-1.
add_executable(session_inbound_poss_dup_tolerance
  test_inbound_poss_dup_tolerance.cpp
  $<TARGET_OBJECTS:session_test_support>  # extract_field (support/possdup_test_support.hpp)
)
target_link_libraries(session_inbound_poss_dup_tolerance PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_inbound_poss_dup_tolerance PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_inbound_poss_dup_tolerance PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_inbound_poss_dup_tolerance
  COMMAND session_inbound_poss_dup_tolerance)
set_tests_properties(session_inbound_poss_dup_tolerance PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "021;us1;possdup;tolerance;inv1;inv2;inv5")

# 021 T007 Stage-1 PossDup validation tests (Arms C/D/E + at-expected + engine-parity pin).
# FIXPP_TEST_HOOKS: exposes seqnum_mgr_test_access().next_inbound_unsafe() for AS4 no-advance.
add_executable(session_inbound_poss_dup_validation
  test_inbound_poss_dup_validation.cpp
  $<TARGET_OBJECTS:session_test_support>  # extract_field (support/possdup_test_support.hpp)
)
target_link_libraries(session_inbound_poss_dup_validation PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_inbound_poss_dup_validation PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_inbound_poss_dup_validation PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_inbound_poss_dup_validation
  COMMAND session_inbound_poss_dup_validation)
set_tests_properties(session_inbound_poss_dup_validation PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "021;us2;possdup;validation;armc;armd;arme")

# ── 022-possresend-allowpossdup-send T001 — US1 PossResend + US2 AllowPosDup ──
# US1 (witness-only): inbound PossResend(97) parity/characterization witnesses.
# data-model §3 disposition table; contracts §C4. Mirrors the 021 possdup target.
add_executable(session_inbound_poss_resend
  test_inbound_poss_resend.cpp
  $<TARGET_OBJECTS:session_test_support>  # extract_field (support/possdup_test_support.hpp)
)
target_link_libraries(session_inbound_poss_resend PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_inbound_poss_resend PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_inbound_poss_resend PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_inbound_poss_resend
  COMMAND session_inbound_poss_resend)
set_tests_properties(session_inbound_poss_resend PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "022;us1;possresend")

# US2 (production): AllowPosDup send-path strip — scanner + boundary-anchored
# excision witnesses (strip/retain/embedded/boundary/malformed-131/no-op/
# resend-independence/no-heap). data-model §2; contracts §C2/§C3.
add_executable(session_send_allow_pos_dup_strip
  test_send_allow_pos_dup_strip.cpp
  $<TARGET_OBJECTS:session_test_support>  # extract_field (support/possdup_test_support.hpp)
)
target_link_libraries(session_send_allow_pos_dup_strip PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_send_allow_pos_dup_strip PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_send_allow_pos_dup_strip PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_send_allow_pos_dup_strip
  COMMAND session_send_allow_pos_dup_strip)
set_tests_properties(session_send_allow_pos_dup_strip PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "022;us2;allowposdup;strip")

# ── 024-reset-refresh-on-logon (S-017) — ResetOn{Logon,Logout,Disconnect} knobs ─
# Three additive SessionConfig reset knobs wired onto the 013 reset_to_one()
# primitive at open()/inbound-Logon/close(). Witnesses: initiator+acceptor
# ResetOnLogon (141 OR-of-three, reset-before-check_inbound), teardown
# (graceful/peer-Logout/abnormal-drop), single-fire guard, durable disposition
# split, both-roles. FIXPP_TEST_HOOKS required (set_counters_for_test seed-non-1
# seam). contracts/reset-knobs.md §C1–C5; quickstart.md.
add_executable(session_reset_on_lifecycle
  test_reset_on_lifecycle.cpp
  $<TARGET_OBJECTS:session_test_support>
)
target_link_libraries(session_reset_on_lifecycle PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_reset_on_lifecycle PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_reset_on_lifecycle PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_reset_on_lifecycle
  COMMAND session_reset_on_lifecycle)
set_tests_properties(session_reset_on_lifecycle PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "024;s017;reset-knobs")

# ── 023-engine-session-strand T001/T002 — MT lifecycle + strand-binding witness ─
#
# Witnesses V-1..V-12 (per-session strand binding, control-plane confinement,
# snapshot readers, bounded-handle).  Mirrors business_messages_roundtrip_test
# (:1326-1350) in link requirements and TLS fixture wiring.
add_executable(engine_session_strand_test
  test_engine_session_strand.cpp
)
target_link_libraries(engine_session_strand_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  fixpp_mock_clock
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_session_strand_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"  # for "transport/asio_tls_transport.hpp" (V-10 socket-executor check)
)
target_compile_definitions(engine_session_strand_test PRIVATE
  FIXPP_TEST_HOOKS
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME engine_session_strand COMMAND engine_session_strand_test)
set_tests_properties(engine_session_strand PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "023;session;control_strand;tsan"
  TIMEOUT 120)

# ── 026-nanosecond-sendingtime (S-039) — session emit/inbound precision suite ──
# Session config/emit/inbound + no-heap witnesses for configurable SendingTime(52)
# precision incl. nanos (27-char) + lenient inbound parse. Core formatter/parser
# witnesses extend the EXISTING session_fix_time_roundtrip target (not here).
# Mirrors the 024 session_reset_on_lifecycle registration. specs/026 tasks T001.
add_executable(session_sending_time_precision
  test_sending_time_precision.cpp
  $<TARGET_OBJECTS:session_test_support>
)
target_link_libraries(session_sending_time_precision PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_sending_time_precision PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_sending_time_precision PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_sending_time_precision
  COMMAND session_sending_time_precision)
set_tests_properties(session_sending_time_precision PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "026;s039;sending-time-precision")

# ── T017 [P]: mallocnesia interception gate for the no-heap witness ───────────
# The dual-gate binding per [[feedback_tracking_pmr_resource_false_pass]]:
# session_sending_time_precision runs the witness; the _mallocnesia variant
# re-runs it under LD_PRELOAD so global-heap escapes are intercepted.
# Mirrors perf_session_recovery_alloc_guard_mallocnesia in tests/perf/CMakeLists.txt.
# Absent mallocnesia.so → skip (the base test is the fallback).
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME session_sending_time_precision_mallocnesia
    COMMAND
      python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
      "--binary" "$<TARGET_FILE:session_sending_time_precision>"
  )
  set_tests_properties(session_sending_time_precision_mallocnesia PROPERTIES
    LABELS "026;s039;sending-time-precision;alloc_guard"
    DEPENDS session_sending_time_precision)
endif()

# ── 027-next-expected-msgseqnum (S-031) — T001/T004 walk-extraction witnesses ──
# T001 (Setup): test skeleton + ctest target registration.
# T004 (Foundational RED first): walk-extraction safety witnesses:
#   WalkExtraction_TwoValueEnd_ExplicitEndBeyondStore (ResendRequest-caller half)
#   WalkExtraction_TwoValueEnd_EndSeqNo0_EmptyStore (ResendRequest-caller half)
#   WalkExtraction_SingleImplementation (structural grep — RED until T005)
# FIXPP_TEST_SOURCE_DIR wired so the grep witnesses can locate session.cpp.
# data-model I-NEX-3, research D-5, contracts C3.
add_executable(session_next_expected_msgseqnum
  test_next_expected_msgseqnum.cpp
)
target_link_libraries(session_next_expected_msgseqnum PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_next_expected_msgseqnum PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_next_expected_msgseqnum PRIVATE
  FIXPP_TEST_HOOKS
  FIXPP_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME session_next_expected_msgseqnum
  COMMAND session_next_expected_msgseqnum)
set_tests_properties(session_next_expected_msgseqnum PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "027;s031;next-expected-msgseqnum;foundational")

# ── T010 [P]: mallocnesia interception gate for the NoHeap.Emit789Append witness ──
# Dual-gate per [[feedback_tracking_pmr_resource_false_pass]]:
# session_next_expected_msgseqnum runs the witness; the _mallocnesia variant
# re-runs it under LD_PRELOAD so global-heap escapes are intercepted.
# Mirrors session_sending_time_precision_mallocnesia (026 pattern).
# Absent mallocnesia.so → skip (the base test is the fallback).
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME session_next_expected_msgseqnum_mallocnesia
    COMMAND
      python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
      "--binary" "$<TARGET_FILE:session_next_expected_msgseqnum>"
  )
  set_tests_properties(session_next_expected_msgseqnum_mallocnesia PROPERTIES
    LABELS "027;s031;next-expected-msgseqnum;alloc_guard"
    DEPENDS session_next_expected_msgseqnum)
endif()

# ── 028-validation-compat-toggles (S-040/S-041) — T001 Setup skeleton ────────
# T001: skeleton + ctest target registration.
# Real RED witnesses (T004/T006/T007/T012) are added in later phases.
# Fixture shape mirrors test_next_expected_msgseqnum.cpp (Fixture + OutboundCapture
# + CountingApp028; free TEST() macros not TEST_F).
# data-model I-VCT-1..11; contracts/validation-compat-toggles.md C1/C2/C3.
add_executable(session_validation_compat_toggles
  test_validation_compat_toggles.cpp
)
target_link_libraries(session_validation_compat_toggles PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_validation_compat_toggles PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_validation_compat_toggles PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_validation_compat_toggles
  COMMAND session_validation_compat_toggles)
set_tests_properties(session_validation_compat_toggles PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "028;s040;s041;validation-compat-toggles;setup")
# T012 NoHeap_RelaxedDeliverPath: mallocnesia LD_PRELOAD wired so global-heap
# escapes from the S4 deliver-without-advance path are intercepted by the
# alloc_guard_{start,end,count} hooks. This APPEND avoids overwriting TSAN_OPTIONS
# (same pattern as perf_session_recovery_alloc_guard in tests/perf/CMakeLists.txt).
# [[feedback_tracking_pmr_resource_false_pass]]
set_property(TEST session_validation_compat_toggles APPEND PROPERTY
  ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")

# T012 mallocnesia companion — runs the binary under check_alloc.py so any global
# malloc/free call between the alloc_guard_start/end markers exits nonzero (the
# binding no-heap proof; the TSAN variant above only wires LD_PRELOAD).
# Pattern mirrors session_next_expected_msgseqnum_mallocnesia (027) and
# session_sending_time_precision_mallocnesia (026).
# Absent mallocnesia.so → skip; the base test is the fallback.
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME session_validation_compat_toggles_mallocnesia
    COMMAND
      python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
      "--binary" "$<TARGET_FILE:session_validation_compat_toggles>"
  )
  set_tests_properties(session_validation_compat_toggles_mallocnesia PROPERTIES
    LABELS "028;s040;s041;validation-compat-toggles;alloc_guard"
    DEPENDS session_validation_compat_toggles)
endif()

# ── 029-persistent-seqnum-hydrate (S-042) — T001 Setup skeleton ──────────────
# T001: skeleton + ctest target registration.
# Real RED witnesses (T006/T008/T009/T012) are added in later phases.
# Fixture shape mirrors test_next_expected_msgseqnum.cpp (Fixture + OutboundCapture
# + CountingApp029 + FaultStore/FaultStoreFactory; free TEST() macros not TEST_F).
# FaultStore: fault-injecting / callback-observing store for W3/W6/W14.
# data-model W1..W14; contracts/seqnum-hydrate.md C1..C4.
add_executable(session_persistent_seqnum_hydrate
  test_persistent_seqnum_hydrate.cpp
)
target_link_libraries(session_persistent_seqnum_hydrate PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_persistent_seqnum_hydrate PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_persistent_seqnum_hydrate PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_persistent_seqnum_hydrate
  COMMAND session_persistent_seqnum_hydrate)
set_tests_properties(session_persistent_seqnum_hydrate PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "029;s042;persistent-seqnum-hydrate;setup")
# T012 NoHeap_HydrateAndPersistPaths: wire mallocnesia LD_PRELOAD so the
# alloc_guard_count() weak symbol is replaced by the live interceptor.
# Pattern mirrors session_validation_compat_toggles (028) and
# session_next_expected_msgseqnum (027).
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  set_property(TEST session_persistent_seqnum_hydrate APPEND PROPERTY
    ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
endif()
# T012 mallocnesia companion — runs the binary under check_alloc.py so any
# global malloc/calloc/realloc between the guard markers causes exit(1).
# Absent mallocnesia.so → skip; the base test is the fallback.
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME session_persistent_seqnum_hydrate_mallocnesia
    COMMAND
      python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
      "--binary" "$<TARGET_FILE:session_persistent_seqnum_hydrate>"
  )
  set_tests_properties(session_persistent_seqnum_hydrate_mallocnesia PROPERTIES
    LABELS "029;s042;persistent-seqnum-hydrate;noheap"
    DEPENDS session_persistent_seqnum_hydrate)
endif()

# ── 025-refresh-on-logon (S-018) — T001 Setup skeleton ──────────────────────
# T001: skeleton + W1/W2 RED witnesses.
# Harness mirrors test_persistent_seqnum_hydrate.cpp (029):
#   FaultStore / FaultStoreFactory / MockReconnectFactory / Fixture
#   make_initiator / make_reconnect_initiator helpers.
# W1/W2: drive_reconnect() with mock transport = 2nd-logon vehicle;
#   asserts store re-read + inbound counter = store value on 2nd logon.
# Anchors: data-model.md W1/W2; contracts/refresh-knob.md C1/C2/C3/C4.
add_executable(session_refresh_on_logon
  test_refresh_on_logon.cpp
)
target_link_libraries(session_refresh_on_logon PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_refresh_on_logon PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_refresh_on_logon PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME session_refresh_on_logon
  COMMAND session_refresh_on_logon)
set_tests_properties(session_refresh_on_logon PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "025;s018;refresh-on-logon;setup")
# T040 W8 NoHeap_RehydratePath: wire mallocnesia LD_PRELOAD so the
# alloc_guard_count() weak symbol is replaced by the live interceptor.
# Pattern mirrors session_persistent_seqnum_hydrate (029).
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  set_property(TEST session_refresh_on_logon APPEND PROPERTY
    ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
endif()
# T040 mallocnesia companion — runs the binary under check_alloc.py so any
# global malloc/calloc/realloc between the guard markers causes exit(1).
# Absent mallocnesia.so → skip; the base test is the fallback.
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME session_refresh_on_logon_mallocnesia
    COMMAND
      python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
      "--binary" "$<TARGET_FILE:session_refresh_on_logon>"
  )
  set_tests_properties(session_refresh_on_logon_mallocnesia PROPERTIES
    LABELS "025;s018;refresh-on-logon;noheap"
    DEPENDS session_refresh_on_logon)
endif()

# ── 033-fixt-fix50sp2-session — T002 Setup + T004/T005 Foundational ──────────
# T002: create RED test targets + ctest registration so `ctest -R fixt` works.
# T004/T005: render_appl_ver_id (application_version → wire 1137 string) unit
#   tests.  RED when helper absent (compile/link error naming render_appl_ver_id);
#   green after T005 adds the inline helper to version_profile.hpp.
# No session/transport/clock libs needed — tests cover only the dict helper and
# the logon_credentials/redactor utilities (header-only).
# Pattern mirrors session_next_expected_msgseqnum (027): target, link, discovery.
add_executable(session_fixt_logon_establishment
  test_fixt_logon_establishment.cpp
)
target_link_libraries(session_fixt_logon_establishment PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_fixt_logon_establishment PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_fixt_logon_establishment
  COMMAND session_fixt_logon_establishment)
set_tests_properties(session_fixt_logon_establishment PROPERTIES
  LABELS "033;s020;s025;fixt-logon-establishment;foundational")

# T002: credentials test target.
# T009/T010: logon_credentials operator<< redaction + redact_tag554 utility.
# T019/T020 (US2 W6/W7): credential emission, seam capture, redaction witnesses.
# Requires fixpp_session + fixpp_mock_clock for W6/W7 (added at US2 phase).
add_executable(session_fixt_credentials
  test_fixt_credentials.cpp
)
target_link_libraries(session_fixt_credentials PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_fixt_credentials PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_fixt_credentials
  COMMAND session_fixt_credentials)
set_tests_properties(session_fixt_credentials PROPERTIES
  LABELS "033;s022;fixt-credentials;foundational")

# ── Conformance subdirectory (005 Q2 in-scope [FIX-TC] subset) ───────────────
# Phase 3 adds tc_establishment_test.cpp (T020); later phases add more.
add_subdirectory(conformance)
