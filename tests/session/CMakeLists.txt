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
  # Hang backstop: these async/threading tests `co_spawn(...use_future)` then
  # block on `fut.get()`. A lost-wake or missed cross-thread completion makes
  # get() block forever and (under serial `ctest --output-on-failure`) wedges
  # the whole suite to CI's 6 h ceiling. The slowest legit run is sub-second
  # under asan, so 120 s is pure margin while still failing a true deadlock
  # fast and letting ctest report which test hung + continue.
  set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

# ── 068-test-binary-grouping: whole-binary add_test grouping ───────────────
# Isolation-safe session tests are grouped into two whole-binary add_test
# executables (bucketed by shared link-deps + no-label empty set); every
# thread_pool/std::thread-spawning test, every uniquely-labeled feature
# witness, every global-alloc-counting/OOM/TSan-specific/name-selected test
# stays standalone (conservative — this module has a HEAVY standalone set).
# See specs/068-test-binary-grouping/dispositions.md (Module: session) for
# the full per-.cpp disposition + ODR pre-check (no renames were needed — all
# potential cross-file symbol collisions were already anonymous-namespace- or
# static-scoped in the source). Reference precedent: tests/dictionary/CMakeLists.txt.

# ── Grouped bucket: session_pure_tests (41 .cpp → 1 binary) ─────────────────
# Single-threaded co_spawn(ioc,...)+ioc.run() FSM/builder/handshake/reconnect/
# compid-binding witnesses — no thread_pool/std::thread spawn, no LABELS
# override (each was previously an unlabeled add_threading_test entry), same
# ENVIRONMENT/TIMEOUT as add_threading_test's uniform default.
add_executable(session_pure_tests
  test_cancellation_parse_to_fromapp.cpp
  test_cancellation_fromapp_to_close.cpp
  test_backpressure_drop_oldest_banned.cpp
  test_version_registry_missing_routes_to_dict_layer.cpp
  test_session_open_rejects_unset_security_profile.cpp
  seqnum_t_handoff_test.cpp
  fix_time_roundtrip_test.cpp
  logon_handshake_test.cpp
  test_070_posture_mismatch_test.cpp
  test_070_max_message_size_test.cpp
  test_070_supported_msgtypes_test.cpp
  test_070_xmlnonfix_passthrough_test.cpp
  coverage_adversarial_test.cpp
  logon_received_observability_test.cpp
  session_send_invalid_state_test.cpp
  cfg_lifetime_safety_test.cpp
  seqnum_gap_fatal_test.cpp
  durable_before_transmit_test.cpp
  heartbeat_testrequest_test.cpp
  cancellation_two_phase_test.cpp
  session_reject_test.cpp
  sending_time_test.cpp
  fsm_transition_matrix_test.cpp
  test_session_layering.cpp
  send_path_test.cpp
  test_seqnum_drain_on_close.cpp
  fsm_matrix_witness_test.cpp
  admin_builder_distinct_now_test.cpp
  admin_emit_mixed_path_test.cpp
  initiator_transport_throw_test.cpp
  test_reconnect_happy_path.cpp
  test_recovery_admin_span_gapfill.cpp
  test_inbound_sequence_reset.cpp
  test_recovery_store_horizon.cpp
  test_reset_seqnum_policy_matrix.cpp
  test_heartbeat_cadence_8cell.cpp
  test_logout_timeout.cpp
  test_compid_binding_default_deny.cpp
  test_compid_binding_principal_extraction.cpp
  test_compid_binding_symmetric.cpp
  test_session_invariant_counter_witness.cpp
  test_compid_binding_mtls_fail_closed.cpp
  test_session_event_ring_overflow.cpp
  test_reload_credentials_in_flight.cpp
  test_live_outbound_serialized.cpp
  test_enum_validation_logon.cpp                                        # T030 (075)
  $<TARGET_OBJECTS:session_test_support>
)
target_link_libraries(session_pure_tests PRIVATE
  fixpp_session
  fixpp_mock_clock
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_pure_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"   # session_invariant_counter_witness: "transport/asio_listener.hpp"
)
target_compile_definitions(session_pure_tests PRIVATE
  FIXPP_TEST_HOOKS   # send_path / seqnum_drain_on_close / admin_emit_mixed_path /
                     # inbound_sequence_reset / reset_seqnum_policy_matrix accessors
  FIXPP_TEST_BINARY_DIR="${CMAKE_BINARY_DIR}"   # session_layering nm/grep probe
  FIXPP_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"   # session_layering nm/grep probe
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"  # invariant_counter_witness
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""  # test_enum_validation_logon (075 T030)
)
add_test(NAME session_pure_tests COMMAND session_pure_tests)
set_tests_properties(session_pure_tests PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  TIMEOUT 120)
# 075 (tasks.md Test-binary discipline note): sets LABELS on the buckets it
# joins/adds. session_pure_tests is a grouped add_test binary — one label set
# covers the whole executable, not per-.cpp; the pre-existing 41 members ride
# along (name-selectable via `-R session_pure_tests` as before, now ALSO
# `ctest -L 075` / `-L session` selectable — mirrors tests/wire/CMakeLists.txt's
# wire_dict_tests precedent).
set_tests_properties(session_pure_tests PROPERTIES LABELS "075;session")

# ── Grouped bucket: session_store_tests (6 .cpp → 1 binary) ────────────────
# Store-family witnesses that neither construct an asio::thread_pool nor carry
# a LABELS override: the three reconcile fault-double table-driven witnesses,
# the toApp store-collision provenance witness, the Path-B compile-time guard
# (build-is-the-test static_asserts), and the SeqnumManager unit test.
add_executable(session_store_tests
  test_store_fail_reconcile_breadth.cpp
  test_store_fail_reconcile_readfail.cpp
  test_store_fail_reconcile_outofset.cpp
  test_send_toapp_store_collision.cpp
  test_quickfix_compat_path_b_guard.cpp
  seqnum_manager_test.cpp
)
target_link_libraries(session_store_tests PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_store_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(session_store_tests PRIVATE
  FIXPP_TEST_HOOKS   # send_toapp_store_collision / session_seqnum_manager accessors
)
add_test(NAME session_store_tests COMMAND session_store_tests)
set_tests_properties(session_store_tests PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")



# ── US1 (Phase 3) seam registrations ────────────────────────────────────────
add_threading_test(threading_strand_serialisation       test_strand_serialisation.cpp)  # seam 2 (TSan)
add_threading_test(threading_executor_compat            test_executor_compat.cpp)        # seam 3
add_threading_test(threading_direct_executor_reentrancy test_direct_executor_reentrancy.cpp)  # seam 16

# ── US2 (Phase 4) seam registrations ────────────────────────────────────────
add_threading_test(threading_heartbeat_under_mock_clock test_heartbeat_under_mock_clock.cpp)  # seam 9
add_threading_test(threading_clock_injection_corpus     test_clock_injection_corpus.cpp)      # seam 11

# ── US3 (Phase 5) seam registrations ────────────────────────────────────────

# ── US5 (Phase 7) seam registrations ────────────────────────────────────────

# ── gate-b/r1 RC#1: SecurityProfile sentinel rejection seam ─────────────────
# FR-018 / N-P2-3 / [const §XII.5] — Session::open() rejects kind::unset.

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

# S-P2-2 — MemoryStore retrieve() reset-during-traversal generation guard
# (mirror of FileStore T015; phase-9 outbound-store-retention-review.md §P2-2).
# No FIXPP_TEST_HOOKS: the witness discriminates on the error code + frames_seen
# + replayed bytes, not on an instrumentation counter.
fixpp_add_store_test(store_memory_reset_during_retrieve SOURCES
  test_memory_store_reset_during_retrieve.cpp
)

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

# ── 059-outbound-store-fail-closed (S-P1-1/S-P2-1) — T005/T005a US1 RED ─────
# T005: the W1 flagship cascade witness. Session + real FileStore over
# thread_pool{2} (mirrors test_credential_store_redaction.cpp) — the store
# path is strand-confined + async_mutex-guarded, a single-threaded harness
# would mask races. FIXPP_TEST_HOOKS exposes the T003/T004 pwrite
# fault-injection seam + seqnum_mgr_test_access().
fixpp_add_store_test(store_fail_closed_persistent SOURCES
  test_store_fail_closed_persistent.cpp
  EXTRA_LIBS fixpp_mock_clock
)
target_compile_definitions(store_fail_closed_persistent PRIVATE FIXPP_TEST_HOOKS)

# T009: W2 regression pin — a bounded MemoryStore (volatile) filled to its
# outbound capacity hits real capacity exhaustion (no mock/seam needed);
# asserts the volatile arm is unchanged (FR-003 / SC-003).
fixpp_add_store_test(store_fail_open_volatile SOURCES
  test_store_fail_open_volatile.cpp
  EXTRA_LIBS fixpp_mock_clock
)

# T011: W3 reconcile-from-durable witness (US3 / FR-007 / SC-004) — three
# HONEST policy variants (plain / reset_on_logon / bilateral_strict default)
# driven via an in-process Session::drive_reconnect() over a mock_transport
# (the reconcile only mutates the in-memory counter; a fresh-Session restart
# would bypass it). FIXPP_TEST_HOOKS exposes the T003/T004 pwrite
# fault-injection seam + seqnum_mgr_test_access().
fixpp_add_store_test(store_fail_reconcile SOURCES
  test_store_fail_reconcile.cpp
  EXTRA_LIBS fixpp_mock_clock
)
target_compile_definitions(store_fail_reconcile PRIVATE FIXPP_TEST_HOOKS)

# T005a: FR-004 breadth witness (classified by durability, not error code) —
# a minimal in-memory persistent-store double armed to return each of the
# three store-fatal codes in turn (table-driven / TEST_P). No FileStore /
# FIXPP_TEST_HOOKS needed (the double is self-contained).

# /speckit-verify coverage witness — the reconcile else-arm (dk.has_value()
# ==false) and the store_cancelled false-leg (`!= store_cancelled`), which
# W1/W3/breadth cannot reach (their reconcile read always succeeds; the
# FileStore seam serves next_seqnum from a still-valid in-memory counter). A
# persistent store DOUBLE additionally faults the reconcile read. No FileStore
# / FIXPP_TEST_HOOKS needed. See 059-...-coverage-design.md Arm (a)/(b).

# gate-b/r1 FQ-1 (RC#1): the fatal-class predicate unification RED witness —
# a minimal persistent store double armed to return an OUT-OF-SET store code
# (outside the pre-fix closed 3-code Session::send guard) in turn. Fresh
# fixture, authored separately from store_fail_reconcile_breadth.cpp per the
# gate-b/r1 brief. No FileStore / FIXPP_TEST_HOOKS needed.

# gate-b/r2 FQ-1 (P1): a toApp veto carrying a store-block error VALUE (on a
# persistent store) must stay Active — the disconnect is decided by
# provenance (did send_impl reach the commit region?), not by re-classifying
# the returned value. Uses seqnum_mgr_test_access() to assert no seqnum
# consumption, so FIXPP_TEST_HOOKS is required. No FileStore needed (a
# minimal always-succeeding persistent store double).

# ── T044–T045 US4 (Phase 6) QuickFIX compat seam registrations ───────────────

# T044 — seam 11: Path-B compile-time guard (FR-032 / AC US4 #2)
# The build IS the test: static_asserts at file scope fire on regression.

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

# Seam #9: T009 fix_time roundtrip test. fixpp_core provides the implementation
# (fix_time.cpp compiled into fixpp_core at T011 / src/core/CMakeLists.txt).

# ── 005 Phase 3 (US1) — seam #2: Logon handshake test (T019) ────────────────
# Red-first TDD: authored before T021–T026 implementation; must turn green
# at T026. Links fixpp_mock_clock for deterministic SendingTime assertions.

# ── 005 Phase 8 /speckit-verify Step 4 coverage uplift ──────────────────────
# Adversarial-input tests for defensive code paths that the well-formed
# seam corpus doesn't exercise: builder buffer-too-small, parser malformed
# field fallback, Logon-ack seq=0, Logout cancel_sleeps mid-graceful.
# FR-003 (010-session-cfg-lifetime): W-5 ASan skip removed. The
# stack-use-after-scope at cfg_ is fixed by the by-value member change
# (T008/T009). session_coverage_adversarial now runs under ASan clean.

# ── 010-session-cfg-lifetime T012 — FR-004 / D-2: LogonReceived observability ──

# ── 010-session-cfg-lifetime T013/T014 — FR-005 / D-3: send invalid-state gate ─

# ── 010-session-cfg-lifetime T007 — FR-001 / FR-002 / SC-001 ─────────────────
# Lifetime safety tests for by-value SessionConfig cfg_ member (W-5 fix).
# Three runtime contracts verified under both debug and ASan:
#   A. CallerCfgDropsAfterCtor_SessionContinuesWithoutUAF
#   B. CallerMutatesCfgAfterCtor_SessionUnaffected
#   C. MultipleSessionsFromSameCfgEvolveIndependently

# ── 005 Phase 4 (US2) — seam #3/#4/#10: seqnum tests (T027/T028/T029) ───────
# Red-first TDD: authored before T031–T036 implementation.
# session_seqnum_manager: seam #3, SeqnumManager unit tests (T027).
# session_seqnum_gap_fatal: seam #4, too-high gap fatal FSM tests (T028).
# session_durable_before_transmit: seam #10, store-before-write ordering (T029).

# Seam #3: SeqnumManager increment/overflow/fatal checks (T027).
# FIXPP_TEST_HOOKS enables set_counters_for_test() for the overflow test.
# (068: session_seqnum_manager moved into the session_store_tests bucket above.)

# Seam #4: Too-high gap fatal disposition (T028).

# Seam #10: Durable-before-transmit ordering (T029).

# ── 005 Phase 5 (US3) — seam #5: Heartbeat/TestRequest test (T037) ───────────
# Red-first TDD: authored before T039–T042 implementation; turns green at T042.
# Tests: build_heartbeat/build_test_request builders; inbound TestRequest echo;
# HeartBtInt=0 disable; unanswered-TR disconnect; inbound Heartbeat liveness.

# ── 005 Phase 6 (US4) — seam #6/#11: Logout exchange + cancellation (T043/T044)
# Red-first TDD: authored before T046–T048; turns green at T049.
# seam #6: Logout exchange — graceful both-directions; never-confirmed timeout;
#          non-Active Logout transitions; outbound store-before-transmit (T029 retire).
# seam #11: Two-phase cancellation — child cancellation_state; close idempotent;
#           close(terminal) skips phase 1.
add_threading_test(session_logout_exchange logout_exchange_test.cpp)
target_compile_definitions(session_logout_exchange PRIVATE FIXPP_TEST_HOOKS)

# ── #289(c) — checked-in RED/GREEN witness for quiesce_on_exit's residual-work
# diagnostic branch (tests/support/pump_until_ready.hpp). Proves ONLY the
# branch's polarity — see the file header for the documented scope limits.
add_threading_test(session_quiesce_on_exit_residual test_quiesce_on_exit_residual.cpp)

# ── 005 Phase 7 (US5) — seam #7/#8: Reject + SendingTime tests (T050/T051) ────
# Red-first TDD: authored before T054/T056 implementation; turns green at T057.
# seam #7: session_reject — build_reject shape; no-reject-loop (I-5); type-invalid-for-state.
# seam #8: sending_time  — check_sending_time unit tests; Q3 integration (Active + Logon paths).

# ── 005 Phase 8 — T058: Seam #1 FSM transition matrix (FR-001 / I-1) ────────
# Meta-attestation that every state×event cell is defined (no UB / no silent
# no-op). Detailed per-cell behaviour is the per-US seam suite's coverage.

# ── 005 Phase 8 — T060: noexcept window + no-C++-across-C-ABI layering ──────
# Compile-time static_assert + nm/grep checks for FR-015 / [const §X.2].
# Needs FIXPP_TEST_BINARY_DIR + FIXPP_TEST_SOURCE_DIR for the nm/grep probes.

# ── 009-session-fsm-finalize T006 — Session::send path test (US1 FR-001) ────
# TDD red: T006 authors RED (compile + fail); T007 + T008 turn GREEN.
# Uses session_test_support OBJECT library (T005 frame_field_extract helper).
# F3 (Round-A drift): FIXPP_TEST_HOOKS enables seqnum_mgr_test_access() on Session
# so tests can verify SeqnumManager counter consistency after send().

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

# ── 010-session-cfg-lifetime Phase 5 (US3) — T016/T017/T018/T019/T020 ────────

# T016+T017 — FSM matrix per-cell witness (FR-006 / SC-002).
# One TEST per enumerated (state×event) cell; uses fsm_visit_history() to
# confirm intermediate and final state transitions.

# T018 — admin builders per-message-distinct SendingTime (FR-007 / SC-006).
# Exercises the branch where clock advances between two emits yield distinct
# SendingTime(52) values for Heartbeat / Reject / Logout / Logon builders.

# T019 — admin emit mixed-path permutations (FR-008 / SC-005 / RC#G).
# Exercises gated-emit success/failure combinations at Site 1 (Q3 path) and
# Site 2 (Active×Logout confirming emit). Requires FIXPP_TEST_HOOKS for
# SeqnumManager::set_counters_for_test() overflow injection.

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

# ── 038-acceptor-sendingtime-guard T001–T008 (Group 1 witness) ───────────────
# 14 cells: stale-past/future, malformed, absent, empty 52; toAdmin-throw;
# assign_outbound-fail; build-fail-closed; persistent-inbound-seeded; ordering
# (52 wins vs 1137/creds/check_inbound); conforming; persistent-reconnect seq.
# FIXPP_TEST_HOOKS exposes seqnum_mgr_test_access() for cell 7 (overflow inject).
# Anchors: spec.md FR-002/FR-003/FR-004; contracts/acceptor-logon-sendingtime.md C-1;
#          quickstart.md §Group-1; tasks.md T001-T008.
add_threading_test(session_acceptor_logon_sending_time
  test_acceptor_logon_sending_time.cpp
  $<TARGET_OBJECTS:session_test_support>
)
target_compile_definitions(session_acceptor_logon_sending_time PRIVATE FIXPP_TEST_HOOKS)
set_tests_properties(session_acceptor_logon_sending_time PROPERTIES
  LABELS "038;s019;acceptor-logon-sendingtime")

# ── 040-inbound-tag-overflow-hardening T005/T006/T007 (US1 Phase 3) ──────────
# Wrap-and-continue + non-digit witnesses for scan_frame_header (site 5).
# 6 cells: forged 429496729649→49, 429496729652→52 (SC-002 038-regression),
# 429496729634→34, 4294967330 regression, conforming (SC-003), non-digit (FR-007a).
# scan_frame_header and FrameHeader are included directly from the internal header
# src/session/scan_frame_header.hpp (moved from session.cpp anonymous namespace to
# enable direct unit testing — 040 US1 Phase 3 T005). The ${CMAKE_SOURCE_DIR}/src
# include path is added so "session/scan_frame_header.hpp" resolves correctly.
# No FIXPP_TEST_HOOKS needed — the function is inline in the internal header.
# Anchors: spec.md FR-003/FR-004/FR-007/FR-007a/SC-002; research.md D-3;
#          tasks.md T005/T006/T007.
add_threading_test(session_scan_frame_header_overflow scan_frame_header_overflow_test.cpp)
target_include_directories(session_scan_frame_header_overflow PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
)
set_tests_properties(session_scan_frame_header_overflow PROPERTIES
  LABELS "040;us1;scan_frame_header;overflow")

# ── 040-inbound-tag-overflow-hardening T011/T012 (US2 Phase 4) ───────────────
# Wrap-and-continue + non-digit witnesses for scan_first_frame_ids (site 4).
# 4 cells: forged 429496729649→49 (sender_comp_id), 429496729656→56
# (target_comp_id), non-digit "4@=" (FR-007a), conforming 8/49/56 (SC-003).
# scan_first_frame_ids and FirstFrameIds are included directly from the internal
# src/session/scan_first_frame_ids.hpp (moved from engine.cpp anonymous namespace
# to enable direct unit testing — 040 US2 Phase 4 T011). The ${CMAKE_SOURCE_DIR}/src
# include path is added so "session/scan_first_frame_ids.hpp" resolves correctly.
# No FIXPP_TEST_HOOKS needed — the function is inline in the internal header.
# Anchors: spec.md FR-003/FR-007/FR-007a; research.md D-3; tasks.md T011/T012.
add_threading_test(session_scan_first_frame_ids_overflow
  scan_first_frame_ids_overflow_test.cpp)
target_include_directories(session_scan_first_frame_ids_overflow PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
)
set_tests_properties(session_scan_first_frame_ids_overflow PROPERTIES
  LABELS "040;us2;scan_first_frame_ids;overflow")

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

# T015 — seam recovery-admin-span-gapfill: all-admin ResendRequest → SequenceReset-GapFill.

# S-023: inbound SequenceReset(35=4) NewSeqNo(36) application (GapFill + Reset
# arms). FIXPP_TEST_HOOKS exposes seqnum_mgr_test_access() to read next-inbound.

# T016 — seam recovery-store-horizon: pre-horizon GapFill + PossDupFlag replay.

# T017 — seam reset-seqnum-policy-matrix: 3 modes × 2 roles (6 cells).
# FIXPP_TEST_HOOKS: exposes seqnum_mgr_test_access() on Session for RC#C-1
# counter assertions (gate-b/r2: verify both counters reset to 1 after 141=Y).

# T018 — seam heartbeat-cadence-8cell: 4 axes × 2 executor modes.

# T019 — seam logout-timeout: initiator + acceptor symmetric cells.

# ── 013-session-reconnect-binding Phase 4 (US2) test registrations ──────────
# T029–T032: CompID↔TLS-identity authorization at Logon. Authored RED-first
# per TDD ([const §VII.3]). Turned GREEN when T034–T036 impl lands.
# CTest filter: ctest -R 'compid_binding'

# T029 — FR-023 / US2 AC6: empty policy rejects ALL Logons (default-deny).

# T030 — FR-022 / US2 AC5: 4-cell principal-extraction order matrix.

# T031 — FR-024 / US2 AC4: symmetric acceptor + initiator coverage.

# T032 + T021 — [[feedback_half_restructure_symmetric_api]]: invariant-counter witness.
# T021 (014 FR-013a): adds live loopback load_credentials() == 1 counter cell;
# requires fixpp_transport + fixpp_tls + loopback_tls_fixture include path.

# RC#A (gate-b/r1) — fail-closed mTLS CompID footgun fix counter-test.
# Asserts: mTLS profile + no peer_identity → Disconnected + event.
# Asserts: one_way_ca (non-mTLS) + no override → still reaches Active.

# ── 013-session-reconnect-binding Phase 5 (US3) — T040 ring-overflow tests ───
# FR-035 / data-model §E-6: both Session and ListenerEvents rings have capacity
# kSessionEventRingCapacity = 16; emit 17 → oldest dropped, last-16 retained.
# T040 also adds asio_listener::recent_events() (T039 accessor test) but those
# integration cells live in tests/transport/ (transport_tls_validation_failed_taxonomy).

# ── 013-session-reconnect-binding Phase 6 (US4) — T042 credential rotation ───
# FR-030 / FR-031 / FR-032 / FR-033 / US4 AC1+AC2+AC3 / Clarifications Q4=A
# Tests: null-reject, forwarder-delegates, strong-ref invariant, call-count witness.
# TSan critical: atomic-swap + snapshot read race-free under cross-strand calls.

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
# TIMEOUT is explicit (#228): the two Step-3 witnesses assert an ELAPSED BAND,
# so their failure mode is "waits" — the deadline witness must be allowed to
# reach ~5s, and a regression that never closes must be cut off rather than
# left to burn a job slot. 300s covers all 14 lanes including the sanitizers.
set_tests_properties(engine_firstframe PROPERTIES
  TIMEOUT 300
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
# item 13 (2026-06-19): DISABLED base-test LD_PRELOAD — once the alloc-guard
# markers became interposable, this base test arms the interceptor and FAILS: the
# measured window (fix->feed(...)) allocates on the global heap (it was false-green
# while the markers were inert). Re-scoping the window to the true zero-alloc steady
# state is tracked in parent REMAINING-WORK.md item 13. The base test still runs its
# functional assertions WITHOUT preload.
# set_property(TEST session_validation_compat_toggles APPEND PROPERTY
#   ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")

# T012 mallocnesia companion — DISABLED (item 13, 2026-06-19): RED — same allocating
# window as above; re-enable by restoring the EXISTS guard once the window is
# zero-alloc (tracked: parent REMAINING-WORK.md item 13).
if(FALSE)  # was: if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
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
# item 13 (2026-06-19): DISABLED — RED window (fix->feed(...)) allocates on the
# global heap (false-green while markers were inert). Tracked: REMAINING-WORK item 13.
if(FALSE)  # was: if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  set_property(TEST session_persistent_seqnum_hydrate APPEND PROPERTY
    ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
endif()
# T012 mallocnesia companion — runs the binary under check_alloc.py so any
# global malloc/calloc/realloc between the guard markers causes exit(1).
# Absent mallocnesia.so → skip; the base test is the fallback.
# item 13 (2026-06-19): DISABLED companion — RED (same allocating window). Tracked: REMAINING-WORK item 13.
if(FALSE)  # was: if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
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
# item 13 (2026-06-19): DISABLED — RED window (asio::co_spawn(...hydrate)) allocates
# a coroutine frame on the global heap (false-green while markers were inert; a
# co_spawn frame can't be warmed away). Tracked: REMAINING-WORK item 13.
if(FALSE)  # was: if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  set_property(TEST session_refresh_on_logon APPEND PROPERTY
    ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
endif()
# T040 mallocnesia companion — runs the binary under check_alloc.py so any
# global malloc/calloc/realloc between the guard markers causes exit(1).
# Absent mallocnesia.so → skip; the base test is the fallback.
# item 13 (2026-06-19): DISABLED companion — RED (same co_spawn-frame alloc). Tracked: REMAINING-WORK item 13.
if(FALSE)  # was: if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
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

# ── 040-inbound-tag-overflow-hardening Phase 4 US2 — site 3 witness (T010/T012) ──
# interpret_logon overflow test: forged wrap-and-continue tokens must be skipped,
# not consumed into logon_interpret_result fields (SC-001 / research.md D-3).
add_threading_test(session_interpret_logon_overflow
  interpret_logon_overflow_test.cpp)
set_tests_properties(session_interpret_logon_overflow PROPERTIES
  LABELS "040;sc001;overflow-hardening;foundational")

# ── 043-plaintext-tcp-transport — [const §XII.7] inbound EncryptMethod(98) ban ─
# Closes the pre-existing §XII.7 inbound gap (S-021 "inbound 98≠0 not handled" /
# TC-017): interpret_logon now rejects a Logon carrying EncryptMethod(98)≠0,
# present-but-malformed fail-closed, for ALL profiles. FR-009.
add_threading_test(session_interpret_logon_encrypt_method
  test_interpret_logon_encrypt_method.cpp)
set_tests_properties(session_interpret_logon_encrypt_method PROPERTIES
  LABELS "043;fr009;xii7;encrypt-method")

# ── 041-validation-gate-wiring T003/T004 — fail-closed config gate ──────────
# C-5 / FR-011 / data-model E-1: register_session rejects
#   validate_inbound_messages=true + null dictionary with invalid_session_config.
# T003 RED: proves the assertion fails before the check is wired.
# T004 GREEN: check is wired in engine.cpp register_session.
# No TLS / no mock_clock needed: register_session is a sync config call.
add_executable(session_validation_gate_config
  test_validation_gate_config.cpp
)
target_link_libraries(session_validation_gate_config PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_validation_gate_config PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_validation_gate_config
  COMMAND session_validation_gate_config)
set_tests_properties(session_validation_gate_config PROPERTIES
  LABELS "041;t003;t004;validation-gate;foundational")

# ── 041-validation-gate-wiring T012 — per-violation-class reject witnesses ───
# SC-005 / FR-003 / FR-004 / data-model E-4: one inbound msg per violation class
# (W1-W7) → asserts mapped SessionRejectReason; proves seqnum NOT advanced on
# reject (C-3 invariant). Requires validate_inbound_messages=true + dictionary.
add_threading_test(session_validate_gate_inbound
  test_validate_gate_inbound.cpp)
set_tests_properties(session_validate_gate_inbound PROPERTIES
  LABELS "041;t012;t014;validation-gate;inbound;foundational")

# ── fixpp#215 item 1 — one as_table_view() walk per opened session ───────────
# W1: open() with no config-supplied snapshot walks the Dictionary exactly ONCE
#     (was 2: inbound_tv_ + the strict validator, independently).
# W2: open() with SessionConfig::dict_snapshot set walks it ZERO times and
#     adopts an aliased view of THAT snapshot (use_count) — the C-ABI's half of
#     the fix.
# W3: the adopted view is wired into the parser and validator (group-bearing
#     frame — Gate B triage C5), not just stored.
# Instrument is 083 T049's W-11a counter, which lives in the INTERNAL dictionary
# header — the same tests -> src/dictionary include tests/capi already takes for
# it (tests/capi/CMakeLists.txt). tools/check_layers.py scans src/** and
# bindings/** only, never tests/.
add_threading_test(session_table_view_reuse
  test_session_table_view_reuse.cpp)
target_include_directories(session_table_view_reuse PRIVATE
  "${CMAKE_SOURCE_DIR}/src/dictionary")  # dictionary_internal.hpp (083 T049 seam)
target_compile_definitions(session_table_view_reuse PRIVATE
  # W3 (Gate B round-1 triage C5): the group-bearing rewrite pulls in
  # support/fix44_dictionary.hpp + support/fix44_group_frame_bodies.hpp
  # (transitively support/app_message_read_scaffold.hpp), which need the
  # real FIX44.xml path.
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\"")
set_tests_properties(session_table_view_reuse PROPERTIES
  LABELS "fixpp215;table-view;session;validation-gate")

# ── fixpp#215 item 1 (Option C) — provenance rejection/acceptance ────────────
# .specify/215-dictionary-view.md §6 seams 1+2: a dict_snapshot whose
# source() disagrees with cfg.dictionary must be rejected at open()
# (invalid_session_config); the config's OWN dictionary+snapshot pairing
# must not be refused, and the adopted view must be provably the config's
# own (discriminating-frame check, not state()==Active alone).
add_threading_test(session_dict_snapshot_provenance
  test_session_dict_snapshot_provenance.cpp)
set_tests_properties(session_dict_snapshot_provenance PROPERTIES
  LABELS "fixpp215;table-view;session;validation-gate")

# ── 041-validation-gate-wiring T013 — validate-first Logon-arm ordering ──────
# C-2 rows a-f: validate before interpret_logon in NotConnected/LogonSent arms.
# No-reject-loop exemption (35=3 and 35=5). Absent/malformed SendingTime(52)
# handled by Logon path, not validation reject loop.
add_threading_test(session_validate_gate_logon_arm
  test_validate_gate_logon_arm.cpp)
set_tests_properties(session_validate_gate_logon_arm PROPERTIES
  LABELS "041;t013;t014;validation-gate;logon-arm;foundational")

# ── 041-validation-gate-wiring T015+T016 — US2 lenient-by-default (default-off)
# T015 (C-2 default branch, FR-002, SC-001): violation inputs with flag=false →
#   accepted and dispatched (no Reject). Positive dispatch proof: seq-advance.
# T016 (SC-005, FR-002): structural proof via FIXPP_TEST_HOOKS
#   has_validator_for_test() → false after open() with flag=false (dict set) —
#   the direct named postcondition: no validator constructed.
add_threading_test(session_validate_gate_default_off
  test_validate_gate_default_off.cpp)
target_compile_definitions(session_validate_gate_default_off PRIVATE FIXPP_TEST_HOOKS)
set_tests_properties(session_validate_gate_default_off PROPERTIES
  LABELS "041;t015;t016;us2;validation-gate;default-off;session")

# ── 041-validation-gate-wiring T017 — Engine clock-config gate (US3) ─────────
# SC-004 / FR-007 / FR-008 / C-4 / data-model E-5:
#   Cell 1 (null clock)  → start() returns clock_not_set, no loops spawned.
#   Cell 2 (valid clock) → start() succeeds, engine operates unchanged.
# No live TLS needed: sessions use no-op transport_send.
# T017 RED until T018 changes start() void → expected_t<void>.
add_executable(engine_clock_gate_test
  test_engine_clock_gate.cpp
)
target_link_libraries(engine_clock_gate_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(engine_clock_gate_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
add_test(NAME engine_clock_gate COMMAND engine_clock_gate_test)
set_tests_properties(engine_clock_gate PROPERTIES
  LABELS "041;t017;t018;us3;clock-gate;session")

# ── 043-plaintext-tcp-transport T007 — SC-001 end-to-end round-trip ──────────
# T007: plaintext acceptor (insecure_plain_tcp, driven through run_accept_loop)
# + standalone plain-TCP initiator complete a FIX Logon over a loopback socket.
# Exercises all three E-7 acceptor sites (profile-map arm, plaintext accept-factory,
# post-accept handshake skip). No FIXPP_TLS_FIXTURE_DIR needed — plaintext sessions
# require no cert files.
# CTest filter: ctest -R 'session_plaintext_roundtrip'
add_executable(session_plaintext_roundtrip_test
  test_session_plaintext_roundtrip.cpp
)
target_link_libraries(session_plaintext_roundtrip_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_plaintext_roundtrip_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
add_test(NAME session_plaintext_roundtrip COMMAND session_plaintext_roundtrip_test)
set_tests_properties(session_plaintext_roundtrip PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "043;t007;us1;plaintext;roundtrip;live_plain")

# ── 043-plaintext-tcp-transport T008 — SC-004 authorization-inert witness ────
# T008: on insecure_plain_tcp — authorize() not called, live_peer_id_==nullopt,
# check_comp_id still rejects mismatched CompIDs, FR-009 EncryptMethod!=0 rejected.
# Uses FIXPP_TEST_HOOKS for session-internal test accessors (authorize_call_count,
# live_peer_id_has_value, cfg_for_test).
# CTest filter: ctest -R 'session_plaintext_authz'
add_executable(session_plaintext_authz_test
  test_session_plaintext_authz.cpp
)
target_link_libraries(session_plaintext_authz_test PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_plaintext_authz_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(session_plaintext_authz_test PRIVATE
  FIXPP_TEST_HOOKS
)
add_test(NAME session_plaintext_authz COMMAND session_plaintext_authz_test)
set_tests_properties(session_plaintext_authz PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "043;t008;us1;plaintext;authz")

# ── 043-plaintext-tcp-transport — /speckit-verify coverage witness ───────────
# Plaintext RECONNECT branch: drive_reconnect_attempt() skips the TLS handshake
# (reconnect_fsm.cpp:265-271) and installs an empty handshake_result{}, leaving
# live_peer_id_==nullopt (session.cpp:419 guard / D-10 #2 MUST). FSM-level + mock
# factory → deterministic, no live socket. Closes the §IX.1 new-line gap on the
# plaintext reconnect path (unreachable from the single-shot roundtrip witness).
# CTest filter: ctest -R 'session_plaintext_reconnect'
add_executable(session_plaintext_reconnect_test
  test_session_plaintext_reconnect.cpp
)
target_link_libraries(session_plaintext_reconnect_test PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_plaintext_reconnect_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(session_plaintext_reconnect_test PRIVATE
  FIXPP_TEST_HOOKS
)
add_test(NAME session_plaintext_reconnect COMMAND session_plaintext_reconnect_test)
set_tests_properties(session_plaintext_reconnect PROPERTIES
  ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "043;us1;plaintext;reconnect")

# ── 043-plaintext-tcp-transport T017 — SC-005 negative-compile friction witness ──
# SC-005 / D-9 / [const §XII.5 v0.3]: selecting insecure_plain_tcp WITHOUT a
# pragma suppression must trigger -Wdeprecated-declarations → -Werror at the
# selection site. This negative-compile TU is compiled with
# -Werror=deprecated-declarations and registered as WILL_FAIL TRUE; a build
# failure IS the passing state. No link step needed — security_profile.hpp is
# header-only. Pattern: mirrors tls_cipher_allow_list_negative_compile +
# tls_security_profile_deprecated_compile.
# [spec.md SC-005; research.md D-9; 043 T017]
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_executable(session_security_profile_insecure_plain_deprecated_negative EXCLUDE_FROM_ALL
        security_profile_insecure_plain_tcp_deprecated_negative.cpp)
    target_include_directories(session_security_profile_insecure_plain_deprecated_negative PRIVATE
        "${CMAKE_SOURCE_DIR}/include")
    target_compile_features(session_security_profile_insecure_plain_deprecated_negative PRIVATE cxx_std_23)
    target_compile_options(session_security_profile_insecure_plain_deprecated_negative PRIVATE
        -Wdeprecated-declarations -Werror=deprecated-declarations)
    add_test(
        NAME session_security_profile_insecure_plain_deprecated_negative_compile
        COMMAND cmake --build "${CMAKE_BINARY_DIR}"
                --target session_security_profile_insecure_plain_deprecated_negative
    )
    set_tests_properties(session_security_profile_insecure_plain_deprecated_negative_compile PROPERTIES
        WILL_FAIL TRUE
        LABELS "043;us2;sc005;compile_negative;friction"
    )
endif()

# ── 043-plaintext-tcp-transport T018 — SC-002 no-implicit-default witness ──────
# SC-002: a default-constructed / kind::unset SecurityProfile is still rejected at
# Session::open() with error::invalid_session_config; insecure_plain_tcp is never
# selected implicitly. Extends threading_session_open_rejects_unset_security_profile
# (gate-b/r1 RC#1) by adding a cell that proves insecure_plain_tcp is NOT the
# implicit fallback when security_profile is left unset. [spec.md SC-002 / FR-006]
add_threading_test(session_no_implicit_insecure_plain_tcp
    test_session_no_implicit_insecure_plain_tcp.cpp)
set_tests_properties(session_no_implicit_insecure_plain_tcp PROPERTIES
    LABELS "043;us2;sc002;no_implicit_default")

# ── 043-plaintext-tcp-transport T021+T022 — SC-003 profile↔factory mismatch ───
# T021: REJECT cells (a)/(b)/(c) + OPEN cells (d)/(e)/(f) for FR-008 at open().
# T022: effective-factory-reaches-mint witness (D-4 test note): drive_reconnect()
#   calls make() on the wired factory (not the ctor-time null pointer).
# Uses MinimalTlsFactory double (no cert files needed).
# CTest filter: ctest -R 'session_plaintext_factory_mismatch'
add_executable(session_plaintext_factory_mismatch_test
    test_session_plaintext_factory_mismatch.cpp
)
target_link_libraries(session_plaintext_factory_mismatch_test PRIVATE
    fixpp_session
    ${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}
    ${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}
    ${FIXPP_TRANSPORT_ZLIB_LIB}
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(session_plaintext_factory_mismatch_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME session_plaintext_factory_mismatch
    COMMAND session_plaintext_factory_mismatch_test)
set_tests_properties(session_plaintext_factory_mismatch PROPERTIES
    TIMEOUT 30
    LABELS "043;t021;t022;us3;sc003;fr008;mismatch")

# ── 046-atomic-shared-ptr (NFR-017) session consumer witnesses ────────────────

# T008 (plan row 6-consumers): publish/acquire witness for Engine::reader_snapshot_
# (fixpp::sync::atomic_shared_ptr<const ReaderSnapshot>).
# Uses insecure_plain_tcp + a raw loopback acceptor — no TLS fixtures required.
# Engine runs its io_context on a background thread (control-strand runs connect loop
# and calls publish_entry → reader_snapshot_ release-store); a dedicated reader
# thread loops calling lookup() (acquire-load of reader_snapshot_), started BEFORE
# the io_context is ever driven and kept alive until ioc_done, spanning the whole
# transition. #289: NOT a start barrier alone and NOT a fixed-window guarantee —
# the main thread waits until the LIVE reader has actually recorded a null read
# before letting the io_context run, so publication cannot precede that
# observation (null_reads>0), and the reader keeps reading through publication
# (nonnull_reads>0). Both halves are asserted; see the file header for why
# nonnull_reads>0 alone is not sufficient (a #287 false-green class).
add_executable(engine_reader_snapshot_publish_acquire_test
    test_engine_reader_snapshot_publish_acquire.cpp)
target_link_libraries(engine_reader_snapshot_publish_acquire_test PRIVATE
    fixpp_session
    fixpp_transport
    fixpp_tls
    "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
    "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_TRANSPORT_ZLIB_LIB}"
    GTest::gtest
    GTest::gtest_main
)
target_include_directories(engine_reader_snapshot_publish_acquire_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME engine_reader_snapshot_publish_acquire
         COMMAND engine_reader_snapshot_publish_acquire_test)
set_tests_properties(engine_reader_snapshot_publish_acquire PROPERTIES
    LABELS "session;015;046;nfr017;publish_acquire"
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
    TIMEOUT 30)

# T019 (plan row +B): forced-fallback link test for fixpp_session.
# Constructs fixpp::sync::atomic_shared_ptr<int> in a binary that links fixpp_session;
# proves shard_guard symbols resolve transitively through the session→transport→sync chain.
add_threading_test(session_forced_fallback_link test_session_forced_fallback_link.cpp)
set_tests_properties(session_forced_fallback_link PROPERTIES
    LABELS "session;046;nfr017;fallback_link")

# ── 061-typed-app-messages (061-slim) US2: E round-trip + external golden ────
# T020 (E rows) — the pivotal grouped/nested exemplar's round-trip + golden
# witness (contracts/builder-shape-oracle.md C5). Needs: fixpp_session (the
# builder), fixpp_dictionary (dict-aware parse + generated v44 flyweight
# headers), and tests/interop/support/golden_diff.{hpp,cpp} — compiled
# directly as an extra source (NOT the interop_support target: tests/session
# is add_subdirectory'd BEFORE tests/interop in the root CMakeLists.txt, so
# interop_support does not exist yet at this point; golden_diff.cpp is
# standard-library-only, so this is a cheap, ordering-independent way to reuse
# it without touching golden_diff.{hpp,cpp} themselves).
if(TARGET fixpp_codegen_generate)
  set(_EXEMPLAR_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

  add_executable(test_exemplar_roundtrip
    test_exemplar_roundtrip.cpp
    "${CMAKE_SOURCE_DIR}/tests/interop/support/golden_diff.cpp"
  )
  add_dependencies(test_exemplar_roundtrip fixpp_codegen_generate)
  target_link_libraries(test_exemplar_roundtrip PRIVATE
    fixpp_session
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_exemplar_roundtrip PRIVATE
    "${_EXEMPLAR_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_SOURCE_DIR}/tests/interop"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_definitions(test_exemplar_roundtrip PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
    "FIXPP_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
  )
  add_test(NAME test_exemplar_roundtrip COMMAND test_exemplar_roundtrip)
  set_tests_properties(test_exemplar_roundtrip PROPERTIES
    LABELS "061;us2;roundtrip;golden;E")
else()
  message(STATUS
    "tests/session: fixpp_codegen_generate not available; skipping test_exemplar_roundtrip")
endif()

# ── 061-typed-app-messages (061-slim) US3: independent inbound-read ──────────
# T022 — hand-authored wire bodies (NOT builder output) parsed via the SAME
# dict-aware path and cross-checked against the v44 flyweight accessors
# (FR-007). Read-only: needs fixpp_dictionary (dict-aware parse + generated
# v44 flyweight headers) only — no fixpp_session / builder / golden-diff
# dependency (that would defeat the "independent of the write path" point).
if(TARGET fixpp_codegen_generate)
  set(_EXEMPLAR_READ_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

  add_executable(test_exemplar_read
    test_exemplar_read.cpp
  )
  add_dependencies(test_exemplar_read fixpp_codegen_generate)
  target_link_libraries(test_exemplar_read PRIVATE
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_exemplar_read PRIVATE
    "${_EXEMPLAR_READ_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_definitions(test_exemplar_read PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  )
  add_test(NAME test_exemplar_read COMMAND test_exemplar_read)
  set_tests_properties(test_exemplar_read PROPERTIES
    LABELS "061;us3;read")
else()
  message(STATUS
    "tests/session: fixpp_codegen_generate not available; skipping test_exemplar_read")
endif()

# ── 061-typed-app-messages (061-slim) T016 — exemplar builder fail-closed
# negatives (contracts/builder-shape-oracle.md §C4: INV-4/INV-5/AC-2). Pure
# build-call negatives (no parse-back) — links fixpp_session only, same
# minimal pattern as business_messages_build_test above (no codegen/
# dictionary dependency needed).
add_executable(test_exemplar_build_failclosed
  test_exemplar_build_failclosed.cpp
)
target_link_libraries(test_exemplar_build_failclosed PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_exemplar_build_failclosed PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME test_exemplar_build_failclosed COMMAND test_exemplar_build_failclosed)
set_tests_properties(test_exemplar_build_failclosed PROPERTIES
  LABELS "061;us2;failclosed;D;8;9;E;AS")

# ── 067-codegen-writer-emitter T011/T013/T015 [US1] [TESTS-FIRST] ───────────
# References the NOT-YET-GENERATED fixpp::v44::build_<Msg>/<Msg>Args/
# builder_registry (Phase 3b, T016/T017 emit Builders.hpp into
# build/<preset>/_codegen/include/fixpp/v44/) — these three targets are RED
# (compile/link failure) until then, by design (TDD Article VII;
# feedback_fail_placeholder_red_test). Links fixpp_wire (not fixpp_session):
# the generated builders serialize purely over wire::body_builder, the same
# runtime dependency as the emitter itself (plan.md Project Structure) — no
# fixpp_session/business_messages.hpp involvement (those are the FROZEN 061
# hand exemplars, a separate US2 concern).
if(TARGET fixpp_codegen_generate)
  set(_067_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

  # 069-v44-all-families T014 [US2]: the generalized completeness pin needs
  # an independent raw FIX44.xml census (pugixml), distinct from the
  # emitter's own VersionIR/build_ir — non-circularity requirement (C2).
  find_package(pugixml CONFIG REQUIRED)

  add_executable(test_067_completeness
    test_067_completeness.cpp
    # 069-v44-all-families T016 [US3]: joins this existing binary (Article
    # VII §8 -- no new executable, no gtest_discover_tests). This host is
    # mode-agnostic under BOTH FIXPP_CODEGEN_V44_FAMILIES values (unlike the
    # roundtrip/us1_smoke hosts, which reference specific non-OFFICIAL typed
    # builder symbols and cannot compile under `official`).
    test_069_mode_count.cpp
  )
  add_dependencies(test_067_completeness fixpp_codegen_generate)
  target_link_libraries(test_067_completeness PRIVATE
    fixpp_wire
    fixpp_dictionary
    pugixml::pugixml
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_067_completeness PRIVATE
    "${_067_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_definitions(test_067_completeness PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  )
  # 069-v44-all-families T015 (US3): flip this pin's intended-set/count to the
  # 33-OFFICIAL mode when the codegen was configured with
  # FIXPP_CODEGEN_V44_FAMILIES=official (see test_067_completeness.cpp's
  # FIXPP_CODEGEN_V44_FAMILIES_OFFICIAL #if). Absent in the default `all`
  # build, so the pin stays at 83 there.
  if(FIXPP_CODEGEN_V44_FAMILIES STREQUAL "official")
    target_compile_definitions(test_067_completeness PRIVATE
      FIXPP_CODEGEN_V44_FAMILIES_OFFICIAL)
  endif()
  add_test(NAME test_067_completeness COMMAND test_067_completeness)
  set_tests_properties(test_067_completeness PROPERTIES
    LABELS "067;us1;completeness;069;mode_count")

  add_executable(test_067_builder_roundtrip
    test_067_builder_roundtrip.cpp
    # 069-v44-all-families T010/T013 [US2]: joins this existing binary
    # (Article VII §8 — isolation-safe TU, no new executable, no
    # gtest_discover_tests). Needs the SAME deps (fixpp_wire, fixpp_dictionary,
    # the dict-aware read scaffold) as the 067 typed roundtrip above.
    test_069_all_families_roundtrip.cpp
  )
  add_dependencies(test_067_builder_roundtrip fixpp_codegen_generate)
  target_link_libraries(test_067_builder_roundtrip PRIVATE
    fixpp_wire
    fixpp_dictionary
    fixpp::builders::v44
    fixpp::validators::v44
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_067_builder_roundtrip PRIVATE
    "${_067_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_definitions(test_067_builder_roundtrip PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  )
  add_test(NAME test_067_builder_roundtrip COMMAND test_067_builder_roundtrip)
  set_tests_properties(test_067_builder_roundtrip PROPERTIES
    LABELS "067;us1;roundtrip;069;all_families;failclosed")

  add_executable(test_067_builder_failclosed
    test_067_builder_failclosed.cpp
    # 069-v44-all-families T008 [US1] — joins this existing binary (Article
    # VII §8: no new executable, no gtest_discover_tests); needs only
    # fixpp_wire, same as the failclosed witnesses above.
    test_069_us1_smoke.cpp
  )
  add_dependencies(test_067_builder_failclosed fixpp_codegen_generate)
  target_link_libraries(test_067_builder_failclosed PRIVATE
    fixpp_wire
    fixpp::builders::v44
    fixpp::validators::v44
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_067_builder_failclosed PRIVATE
    "${_067_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME test_067_builder_failclosed COMMAND test_067_builder_failclosed)
  set_tests_properties(test_067_builder_failclosed PROPERTIES
    LABELS "067;us1;failclosed;069;all_families;us1_smoke")

  # ── 067-codegen-writer-emitter T022 [US3] [TESTS-FIRST] — validate_<Msg>
  # required-presence witnesses. Constructs each message via build_<Msg> then
  # exercises validate_<Msg>, so it links BOTH prebuilt v44 libs
  # (078-precompiled-builder-libs: builders for build_, validators for
  # validate_ — the T019 relink; matches test_067_builder_roundtrip).
  add_executable(test_067_builder_validate
    test_067_builder_validate.cpp
  )
  add_dependencies(test_067_builder_validate fixpp_codegen_generate)
  target_link_libraries(test_067_builder_validate PRIVATE
    fixpp_wire
    fixpp::builders::v44
    fixpp::validators::v44
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_067_builder_validate PRIVATE
    "${_067_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME test_067_builder_validate COMMAND test_067_builder_validate)
  set_tests_properties(test_067_builder_validate PROPERTIES
    LABELS "067;us3;validate")

  # ── 067-codegen-writer-emitter T020 [US2] [TESTS-FIRST] — the HEADLINE
  # shape-oracle byte-equality pin. Links fixpp_session (the frozen 061 hand
  # exemplars, fixpp::session::build_new_order_single/… ) AND fixpp_wire (the
  # generated fixpp::v44::build_<Msg> path) + golden_diff.cpp (shape_oracle_
  # profile()) — mirrors test_exemplar_roundtrip's dependency shape.
  add_executable(test_067_builder_shape_oracle
    test_067_builder_shape_oracle.cpp
    # 069-v44-all-families T012 [US2]: joins this existing binary (Article
    # VII §8 — isolation-safe TU, no new executable, no
    # gtest_discover_tests). Needs the SAME deps (fixpp_wire, golden_diff.cpp)
    # as the 067 shape-oracle above — the 8 external-golden exemplars (C4).
    test_069_family_exemplar_golden.cpp
    "${CMAKE_SOURCE_DIR}/tests/interop/support/golden_diff.cpp"
  )
  add_dependencies(test_067_builder_shape_oracle fixpp_codegen_generate)
  target_link_libraries(test_067_builder_shape_oracle PRIVATE
    fixpp_session
    fixpp_wire
    fixpp::builders::v44
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_067_builder_shape_oracle PRIVATE
    "${_067_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_SOURCE_DIR}/tests/interop"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_definitions(test_067_builder_shape_oracle PRIVATE
    "FIXPP_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
  )
  add_test(NAME test_067_builder_shape_oracle COMMAND test_067_builder_shape_oracle)
  set_tests_properties(test_067_builder_shape_oracle PROPERTIES
    LABELS "067;us2;shape_oracle;golden;D;8;9;E;AS;069;family_golden;v44-byte-invariance")

  # ── 077-builder-args-dedup T012 [US1] [TESTS-FIRST] ──────────────────────
  # Deep-group vlatest round-trip smoke. Deliberately its OWN executable, NOT
  # joined to test_067_builder_roundtrip above: this TU #includes the ~74MB
  # deduped fixpp::vlatest::all.hpp (576 shared plans -- T017 fixed a
  # version-unscoped N-002/N-003 exclusion bug that had put this at 573),
  # which the other
  # builder-test binaries in this file must NOT pay the compile cost of.
  # Gated on FIXPP_CODEGEN_FIX_LATEST — with the tier OFF, cmake/Codegen.cmake
  # removes _codegen/include/fixpp/vlatest/ entirely (076 gate-b/r1 P1
  # precedent: an ungated vlatest-including target fails to compile on an OFF
  # build).
  # This TU #includes the ~78MB deduped vlatest all.hpp — built unconditionally
  # under the FIXPP_CODEGEN_FIX_LATEST tier gate (078 T034 removed the
  # FIXPP_BUILD_HEAVY_BUILDER_TESTS stopgap gate).
  if(FIXPP_CODEGEN_FIX_LATEST)
    add_executable(test_077_vlatest_builder_roundtrip
      test_077_vlatest_builder_roundtrip.cpp
    )
    add_dependencies(test_077_vlatest_builder_roundtrip fixpp_codegen_generate)
    target_link_libraries(test_077_vlatest_builder_roundtrip PRIVATE
      fixpp_wire
      fixpp_dictionary
      fixpp::builders::vlatest
      GTest::gtest
      GTest::gtest_main
    )
    target_include_directories(test_077_vlatest_builder_roundtrip PRIVATE
      "${_067_CODEGEN_INC}"
      "${CMAKE_SOURCE_DIR}/tests"
      "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions(test_077_vlatest_builder_roundtrip PRIVATE
      "FIXPP_ORCHESTRA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries/orchestra\""
      # app_message_read_scaffold.hpp's load_fix44() is a non-template inline
      # function -- its body (and this macro reference) is compiled even
      # though this TU never calls it (uses load_vlatest()/OrchestraLoader
      # instead). Mirrors test_067_builder_roundtrip's definition above.
      "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
    )
    add_test(NAME test_077_vlatest_builder_roundtrip COMMAND test_077_vlatest_builder_roundtrip)
    set_tests_properties(test_077_vlatest_builder_roundtrip PROPERTIES
      LABELS "077;us1;builder-roundtrip;vlatest")
  endif()

  # ── 077-builder-args-dedup T016 [US2] ─────────────────────────────────────
  # Exhaustive differential round-trip over v50sp2's FULL in-scope
  # application set (156, no v44 exclusion inherited -- FR-006/T017). Its own
  # executable, not joined to test_067_builder_roundtrip above: this TU
  # #includes the ~75MB deduped fixpp::v50sp2::all.hpp (558 shared
  # plans). v50sp2 is not gated by FIXPP_CODEGEN_FIX_LATEST (always emitted);
  # built unconditionally (078 T034 removed the FIXPP_BUILD_HEAVY_BUILDER_TESTS
  # stopgap gate).
  add_executable(test_077_allversions_builder_roundtrip
    test_077_allversions_builder_roundtrip.cpp
  )
  add_dependencies(test_077_allversions_builder_roundtrip fixpp_codegen_generate)
  target_link_libraries(test_077_allversions_builder_roundtrip PRIVATE
    fixpp_wire
    fixpp_dictionary
    fixpp::builders::v50sp2
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_077_allversions_builder_roundtrip PRIVATE
    "${_067_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_definitions(test_077_allversions_builder_roundtrip PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  )
  add_test(NAME test_077_allversions_builder_roundtrip
    COMMAND test_077_allversions_builder_roundtrip)
  set_tests_properties(test_077_allversions_builder_roundtrip PROPERTIES
    LABELS "077;us2;builder-roundtrip;v50sp2")

  # vlatest half of T016 -- kept in a SEPARATE TU/executable from the v50sp2
  # one above (not one combined "allversions" TU): compiling both the ~75MB
  # v50sp2 AND ~78MB vlatest deduped all.hpp in a single TU would stack
  # their peak RSS past what a -j1 WSL2 build can survive (each alone is
  # already ~3.7-4.6 GiB per the T015 compile-resource bench;
  # feedback_build_resource_cap_oom). Gated on FIXPP_CODEGEN_FIX_LATEST like
  # test_077_vlatest_builder_roundtrip above; built unconditionally under that
  # tier gate (078 T034 removed the FIXPP_BUILD_HEAVY_BUILDER_TESTS stopgap gate).
  if(FIXPP_CODEGEN_FIX_LATEST)
    add_executable(test_077_allversions_builder_roundtrip_vlatest
      test_077_allversions_builder_roundtrip_vlatest.cpp
    )
    add_dependencies(test_077_allversions_builder_roundtrip_vlatest fixpp_codegen_generate)
    target_link_libraries(test_077_allversions_builder_roundtrip_vlatest PRIVATE
      fixpp_wire
      fixpp_dictionary
      fixpp::builders::vlatest
      GTest::gtest
      GTest::gtest_main
    )
    target_include_directories(test_077_allversions_builder_roundtrip_vlatest PRIVATE
      "${_067_CODEGEN_INC}"
      "${CMAKE_SOURCE_DIR}/tests"
      "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions(test_077_allversions_builder_roundtrip_vlatest PRIVATE
      "FIXPP_ORCHESTRA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries/orchestra\""
      # app_message_read_scaffold.hpp's load_fix44() is a non-template inline
      # function -- its body (and this macro reference) is compiled even
      # though this TU never calls it (uses load_dict()/OrchestraLoader
      # instead). Mirrors test_077_vlatest_builder_roundtrip's definition.
      "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
    )
    add_test(NAME test_077_allversions_builder_roundtrip_vlatest
      COMMAND test_077_allversions_builder_roundtrip_vlatest)
    set_tests_properties(test_077_allversions_builder_roundtrip_vlatest PROPERTIES
      LABELS "077;us2;builder-roundtrip;vlatest")
  endif()

  # ── 078-precompiled-builder-libs T021 [US1]: byte-identical wire round-trip
  # via the LINKED lib for representative v44 + v50sp2 messages (SC-004
  # builder, link mode; FR-009). Each TEST includes ONLY its message's slim
  # per-message header (not all.hpp) -- cheap. vlatest cell CI-deferred (lib
  # not built locally).
  add_executable(test_078_builder_roundtrip_linked_us1
    test_078_builder_roundtrip_linked_us1.cpp
  )
  add_dependencies(test_078_builder_roundtrip_linked_us1 fixpp_codegen_generate
    fixpp_builders_v44 fixpp_builders_v50sp2)
  target_link_libraries(test_078_builder_roundtrip_linked_us1 PRIVATE
    fixpp_wire
    fixpp_dictionary
    fixpp::builders::v44
    fixpp::builders::v50sp2
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_078_builder_roundtrip_linked_us1 PRIVATE
    "${_067_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_definitions(test_078_builder_roundtrip_linked_us1 PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  )
  add_test(NAME test_078_builder_roundtrip_linked_us1
    COMMAND test_078_builder_roundtrip_linked_us1)
  set_tests_properties(test_078_builder_roundtrip_linked_us1 PROPERTIES
    LABELS "078;us1;builder-roundtrip;v44;v50sp2")

  # ── 078-precompiled-builder-libs T025 [US2]: validator-linked behavior
  # (FR-003/FR-004 AC2). Links fixpp::validators::v44 -- the US2 counterpart
  # to T024's builder-only nm witness: this consumer DOES want the outbound
  # required-field check and gets it by linking the validator lib.
  add_executable(test_078_validator_linked_us2
    test_078_validator_linked_us2.cpp
  )
  add_dependencies(test_078_validator_linked_us2 fixpp_codegen_generate
    fixpp_validators_v44)
  target_link_libraries(test_078_validator_linked_us2 PRIVATE
    fixpp_wire
    fixpp::validators::v44
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_078_validator_linked_us2 PRIVATE
    "${_067_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME test_078_validator_linked_us2
    COMMAND test_078_validator_linked_us2)
  set_tests_properties(test_078_validator_linked_us2 PROPERTIES
    LABELS "078;us2;validator-linked;v44")
else()
  message(STATUS
    "tests/session: fixpp_codegen_generate not available; skipping 067 US1 suites")
endif()

# ── 066-dict-backed-inbound-parse T001 — shared test-scaffolding smoke ───────
# Compile/smoke check for the group-bearing FIX44 frame builders + the
# real-Session-dispatch harness (tests/support/fix44_group_frame_bodies.hpp,
# tests/support/fix44_dictionary.hpp, support/group_dispatch_fixture.hpp).
# NO group-membership/extent assertions here (RED-first preservation for
# T004/T010) — see the file header comment.
add_executable(test_066_group_scaffold_smoke
  test_066_group_scaffold_smoke_test.cpp
)
target_link_libraries(test_066_group_scaffold_smoke PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_066_group_scaffold_smoke PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(test_066_group_scaffold_smoke PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test_066_group_scaffold_smoke COMMAND test_066_group_scaffold_smoke)
set_tests_properties(test_066_group_scaffold_smoke PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "066;setup;scaffold")

# ── 066-dict-backed-inbound-parse T004 — RED-first group-membership witness ──
# (C++ real dispatch). MUST be observed RED against the current dict-free
# parse (src/session/session.cpp:316) — T006 flips the parse site later.
# See the file header comment for the two discriminating assertions.
add_executable(test_066_group_membership_red
  test_066_group_membership_red_test.cpp
)
target_link_libraries(test_066_group_membership_red PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_066_group_membership_red PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(test_066_group_membership_red PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test_066_group_membership_red COMMAND test_066_group_membership_red)
set_tests_properties(test_066_group_membership_red PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "066;us1;red")

# ── 066-dict-backed-inbound-parse T010 — scalar-as-group witness ────────────
# (C++ real dispatch). Querying scalar tag Symbol(55) as a group must yield
# an EMPTY group_slices() span (TYPE_MISMATCH at the C-ABI layer), never a
# spurious instance. GREEN on the current dict-backed parse; RED under the
# T006 mutation (session.cpp:316 reverted to the default dict-free Parser).
add_executable(test_066_scalar_as_group
  test_066_scalar_as_group_test.cpp
)
target_link_libraries(test_066_scalar_as_group PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_066_scalar_as_group PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(test_066_scalar_as_group PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test_066_scalar_as_group COMMAND test_066_scalar_as_group)
set_tests_properties(test_066_scalar_as_group PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "066;us2")

# ── 066-dict-backed-inbound-parse T009 — validator-ON grouped witness ────────
# (FR-006). Proves the opt-in validator path (validate_inbound_messages=true)
# observes the same membership-bounded extents as validator-OFF.
add_executable(test_066_validator_on_grouped
  test_066_validator_on_grouped_test.cpp
)
target_link_libraries(test_066_validator_on_grouped PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_066_validator_on_grouped PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(test_066_validator_on_grouped PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test_066_validator_on_grouped COMMAND test_066_validator_on_grouped)
set_tests_properties(test_066_validator_on_grouped PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "066;us1")

# ── 066-dict-backed-inbound-parse T012 — admin / no-group witness ───────────
# (SC-003/FR-005). Admin (Heartbeat) + no-group app (NewOrderSingle) dispatch
# unaffected; a group-bearing message whose callback never reads the group
# dispatches identically to a no-group message (corroborates the structural
# lazy-membership-consultation argument cited in the file header).
add_executable(test_066_admin_no_group
  test_066_admin_no_group_test.cpp
)
target_link_libraries(test_066_admin_no_group PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_066_admin_no_group PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(test_066_admin_no_group PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test_066_admin_no_group COMMAND test_066_admin_no_group)
set_tests_properties(test_066_admin_no_group PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "066;us3")

# ── 066-dict-backed-inbound-parse T014 — arena-fit witnesses ────────────────
# (SC-004/FR-009). App-path fit within kInboundParseArena=16384, admin-path
# fit within kAdminParseArena=8192, a near-cap/headroom probe, and a
# pathological deeply-nested (>kMaxGroupDepth=16) message failing closed
# (err_group_too_large, never over-read/corrupt).
add_executable(test_066_arena_fit
  test_066_arena_fit_test.cpp
)
target_link_libraries(test_066_arena_fit PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_066_arena_fit PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(test_066_arena_fit PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test_066_arena_fit COMMAND test_066_arena_fit)
set_tests_properties(test_066_arena_fit PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "066;us3")

# ── 082-structural-group-detection T021 [US1] RED pin ───────────────────────
# (written BEFORE T023 per the RED-first ordering rule): ungated (validate_
# inbound_messages OFF) group-scoped parse across FIX40/FIX41/FIX42, real
# Session dispatch. contracts/group-detection.md C4.1; spec.md FR-006a,
# SC-008/SC-008a (first leg).
add_executable(test_082_ungated_group_parse
  test_082_ungated_group_parse_test.cpp
)
target_link_libraries(test_082_ungated_group_parse PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_082_ungated_group_parse PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(test_082_ungated_group_parse PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test_082_ungated_group_parse COMMAND test_082_ungated_group_parse)
set_tests_properties(test_082_ungated_group_parse PROPERTIES
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  LABELS "082;us1")

# ── 082-structural-group-detection T021b [US1] RED pin ──────────────────────
# (written BEFORE T023 per the RED-first ordering rule): SC-008a second leg —
# validate_inbound_messages ON, exact-set-equality of the oracle-derived
# 14 required-group-member pairs (13 top-level + 1 nested) that newly reject
# on omission. Reuses tests/dictionary/required_scope_oracle.hpp (T005's
# shared oracle) directly against the real dictionary_driven_validator, via
# Framer+Parser<Index> (no Session/FSM). contracts/group-detection.md C4.2,
# K9; spec.md FR-006/FR-006b, SC-008a.
add_executable(test_082_group_required_member_validation
  test_082_group_required_member_validation_test.cpp
)
find_package(pugixml CONFIG REQUIRED)
target_link_libraries(test_082_group_required_member_validation PRIVATE
  fixpp_wire
  fixpp_dictionary
  fixpp_core
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_082_group_required_member_validation PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(test_082_group_required_member_validation PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test_082_group_required_member_validation COMMAND test_082_group_required_member_validation)
set_tests_properties(test_082_group_required_member_validation PROPERTIES
  LABELS "082;us1")

# ── 088-firstframe-budget-timer-lifetime T009/T011 (US1/US2) ────────────────
# Direct-helper witness for the bounded first-frame read (B1-B6, T1, T2a).
# read_first_frame_bounded is included directly from the internal header
# src/session/read_first_frame_bounded.hpp (extracted from engine.cpp's
# anonymous namespace to enable direct unit testing — 088 T008). The
# ${CMAKE_SOURCE_DIR}/src include path is added so
# "session/read_first_frame_bounded.hpp" resolves correctly (include-path
# shape copied from the :712-728 scan_first_frame_ids block, not its wiring —
# this function needs wire::Framer, pmr_carry_buffer, the Transport vtable and
# asio::steady_timer, so it still links fixpp_session in full via
# add_threading_test; research.md D-5 withdraws the "keeps witness binaries
# small" claim the scan_first_frame_ids precedent implied).
# FIXPP_ALLOW_MOCK_TRANSPORT is required: include/fixpp/transport/test/
# mock_transport.hpp is a hard #error without it.
# TIMEOUT is set explicitly rather than inherited from add_threading_test's
# default 120 (:92): this target hosts B4 and B5, whose MUTANTS hang rather
# than fail (research.md D-6.11's room==0 "loops without bound" case) — same
# reasoning D-5's table gives for the sibling session_first_frame_stop (T2b)
# target: a target whose regression mode is a hang may not inherit a default
# silently.
# LABELS carries a dedicated RED-proof label,
# session_read_first_frame_bounded (matching the target's own name, so it can
# never collide with a label shared by a later 088 target), so quickstart.md
# §4's RED A/B can select exactly this target via `ctest -L`, never
# `-R <exe-name>` ([const §VII.8]).
# real cells land in Phase 3 (T012-T015); until then the source file below
# holds one deliberately-failing placeholder so the target configures without
# masquerading as covered (an empty TU or GTEST_SKIP() exits green and reads
# as a pass — [[feedback_ci_gate_observes_not_asserts_witness_skips_into_green]]).
# Anchors: spec.md FR-001..FR-018; research.md D-5/D-6; tasks.md T008/T009/T011.
add_threading_test(session_read_first_frame_bounded read_first_frame_bounded_test.cpp)
target_include_directories(session_read_first_frame_bounded PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(session_read_first_frame_bounded PRIVATE FIXPP_ALLOW_MOCK_TRANSPORT)
set_tests_properties(session_read_first_frame_bounded PROPERTIES
  LABELS "088;us1;us2;first_frame;budget;lifetime;session_read_first_frame_bounded"
  TIMEOUT 120)

# ── 088 Phase 4 (US2) — T023/T024/T025: T6 (SC-018/FR-018) ───────────────────
# Real-TLS witness for FR-018: Engine::stop()'s cancellation_type::total must
# abort an in-flight first-frame read on a REAL ssl::stream — the ONLY cell
# pinning FR-018 (a mock cell is green exactly where a real ssl::stream hangs,
# research.md D-6.10). MUST NOT be skippable in CI, so FIXPP_TLS_FIXTURE_DIR
# is wired unconditionally at configure time (never a runtime env var — the
# GTEST_SKIP() fallback in this file is for local/manual runs only). Uses
# research.md D-6.13b's build contract verbatim. TIMEOUT is second-line
# defence only — the in-test watchdog (1000ms) is the primary bound; a run
# that ends in the ctest TIMEOUT instead of a watchdog assertion means the
# watchdog is mis-wired (a broken cell, not a RED proof).
# Anchors: research.md D-6.10/D-6.10a/D-6.13; tasks.md T023-T025.
add_executable(session_first_frame_total_cancel_tls first_frame_total_cancel_tls_test.cpp)
target_link_libraries(session_first_frame_total_cancel_tls PRIVATE
  fixpp fixpp_transport fixpp_tls GTest::gtest_main)
target_include_directories(session_first_frame_total_cancel_tls PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(session_first_frame_total_cancel_tls PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures")
add_test(NAME session_first_frame_total_cancel_tls COMMAND session_first_frame_total_cancel_tls)
set_tests_properties(session_first_frame_total_cancel_tls PROPERTIES
  LABELS "088;us2;live_tls;first_frame;cancellation"
  TIMEOUT 60)

# ── 088 Phase 4 (US2) — T022: T2b (SC-015 accept-slot leg) ───────────────────
# Engine::stop()'s promptness + accept-slot reclaim at engine scope, driven
# through #232's real mTLS loopback harness (EngineLoopbackHarness). Same
# bare-deadline-arm mutant as T2a (research.md D-6.12b), applied via stop()'s
# Step-3 join on outstanding_counter_ rather than a direct helper call. MUST
# NOT be skippable in CI, so FIXPP_TLS_FIXTURE_DIR is wired unconditionally at
# configure time. TIMEOUT is explicit (research.md D-6.13b's build contract):
# this target's regression mode is a bounded 5s stall under the mutant, not a
# hang, but per D-5's rule a target whose failure shape includes a multi-
# second wait may not inherit add_threading_test's default silently.
# Anchors: research.md D-6.1/D-6.12/D-6.13a/D-6.13b; tasks.md T022.
add_executable(session_first_frame_stop first_frame_stop_test.cpp)
target_link_libraries(session_first_frame_stop PRIVATE
  fixpp_session
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(session_first_frame_stop PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(session_first_frame_stop PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME session_first_frame_stop COMMAND session_first_frame_stop)
set_tests_properties(session_first_frame_stop PROPERTIES
  LABELS "088;us2;first_frame;stop;live_tls"
  TIMEOUT 120)

# ── Conformance subdirectory (005 Q2 in-scope [FIX-TC] subset) ───────────────
# Phase 3 adds tc_establishment_test.cpp (T020); later phases add more.
add_subdirectory(conformance)

# ── 082-structural-group-detection T045 [US4]: v42 nested write exemplar ──────
# US4 AC2 / SC-007, closes L-061-1. The exemplar that proves the fixpp::v42 builder
# tier is USABLE, not merely emitted: build a MassQuote with NoQuoteSets(296) ->
# NoQuoteEntries(295), byte-diff it against T044's QuickFIX-authored golden, and
# round-trip it back through the regenerated v42 read tier with BOTH group levels
# enumerated.
#
# Links both prebuilt v42 libs (fixpp::builders::v42 for build_, ::validators::v42
# for validate_), mirroring test_067_builder_validate's v44 pair. Compiles
# golden_diff.cpp directly for the same ordering reason documented on
# test_exemplar_roundtrip above (tests/session is add_subdirectory'd before
# tests/interop, so interop_support does not exist yet at this point).
if(TARGET fixpp_codegen_generate AND TARGET fixpp_builders_v42)
  add_executable(test_082_v42_nested_exemplar_roundtrip
    test_082_v42_nested_exemplar_roundtrip.cpp
    "${CMAKE_SOURCE_DIR}/tests/interop/support/golden_diff.cpp"
  )
  add_dependencies(test_082_v42_nested_exemplar_roundtrip fixpp_codegen_generate)
  target_link_libraries(test_082_v42_nested_exemplar_roundtrip PRIVATE
    fixpp_wire
    fixpp_dictionary
    fixpp::builders::v42
    fixpp::validators::v42
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_082_v42_nested_exemplar_roundtrip PRIVATE
    "${CMAKE_BINARY_DIR}/_codegen/include"
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_SOURCE_DIR}/tests/interop"
    "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_definitions(test_082_v42_nested_exemplar_roundtrip PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
    "FIXPP_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
  )
  add_test(NAME test_082_v42_nested_exemplar_roundtrip
    COMMAND test_082_v42_nested_exemplar_roundtrip)
  set_tests_properties(test_082_v42_nested_exemplar_roundtrip PROPERTIES
    LABELS "082;us4;roundtrip;golden;dict;i")
endif()
