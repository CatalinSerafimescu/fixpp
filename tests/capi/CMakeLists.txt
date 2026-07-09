# tests/capi/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(capi_smoke_test capi_smoke_test.cpp)
target_link_libraries(capi_smoke_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_smoke COMMAND capi_smoke_test)

# T004 (US1) — GTest version accessor tests
add_executable(capi_version_test version_test.cpp)
target_link_libraries(capi_version_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_version COMMAND capi_version_test)

# T009 (US2, SC-002/006) — error-surface correctness oracle: drives the 116
# fixpp::core::error enumerators through translate() against the checked-in
# expected_error_map.csv; FIXPP_CAPI_DATA_DIR points the test at the CSV.
add_executable(capi_error_surface_test error_surface_test.cpp)
target_link_libraries(capi_error_surface_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(capi_error_surface_test PRIVATE
  FIXPP_CAPI_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME capi_error_surface COMMAND capi_error_surface_test)

# T015 (US2, SC-004) — occupancy-gate negative test: clean tree passes, a
# redefined/collided published slot makes tools/check_capi_occupancy.sh fail
# deterministically.
add_test(NAME capi_occupancy_negative
  COMMAND bash "${CMAKE_SOURCE_DIR}/tools/test_capi_occupancy_negative.sh")

# T005 (US1, SC-001) — pure-C compile+link smoke: header C-cleanliness gate.
# enable_language(C) must be invoked before add_executable with C sources.
enable_language(C)
add_executable(capi_version_smoke_c capi_version_smoke.c)
target_link_libraries(capi_version_smoke_c PRIVATE fixpp_capi)
# fixpp_capi (static) drags C++ runtime; the C linker driver cannot resolve those
# symbols — set LINKER_LANGUAGE CXX so CMake picks the C++ driver for linking.
set_target_properties(capi_version_smoke_c PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME capi_version_smoke_c COMMAND capi_version_smoke_c)

# T016 (US3, SC-003) — pure-C handle-catalogue compile+link smoke: the umbrella
# is C-clean and the 5 opaque handle typedefs + null/invalid codes are usable
# from C. LINKER_LANGUAGE CXX because fixpp_capi (static) drags the C++ runtime.
add_executable(capi_handles_compile_test handles_compile_test.c)
target_link_libraries(capi_handles_compile_test PRIVATE fixpp_capi)
set_target_properties(capi_handles_compile_test PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME capi_handles_compile COMMAND capi_handles_compile_test)

# T021 (US4, SC-005) — reentrancy-gate positive/negative test: the real surface
# passes; a 0-class or 2-class symbol makes tools/check_capi_reentrancy.sh fail.
add_test(NAME capi_reentrancy_negative
  COMMAND bash "${CMAKE_SOURCE_DIR}/tools/test_capi_reentrancy_negative.sh")

# ── 050 Feature B (and 051 T022) internal-seam include set ──────────────────
# lifecycle_test (T015/T016) + send_recv_test (T028/T029/T030) reach the engine-
# internal handles (capi_internal.hpp under src/capi) through the documented
# test-only seams (L-050-1 dict + L-050-5 transport endpoint) and use the
# session test support (minimal_dictionary.hpp under tests/support). thunk_split
# (T024) additionally arms the SC-006 send-throw seam via the FIXPP_TEST_HOOKS-
# gated set_send_throw_hook(). All link fixpp_capi (session/transport/tls/wire/dict).
# Defined BEFORE any target that references it (error_block_test T022 + Feature-B
# targets below both use this variable).
set(FIXPP_CAPI_FEATURE_B_INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}"          # capi_loopback_support.hpp
  "${CMAKE_SOURCE_DIR}/src/capi"         # capi_internal.hpp + the public src tree
  "${CMAKE_SOURCE_DIR}/tests"            # support/minimal_dictionary.hpp
)

# T004 (US1, SC-004) — 051 error-block foundational oracle (translate re-point +
# strerror + per-code-minor downgrade).  T022 (US5) appends live end-to-end arms
# over loopback — uses capi_internal.hpp (check_session seam) + the loopback
# support (L-050-1 dict + L-050-5 endpoint).  Include the Feature-B set.
add_executable(capi_error_block_test error_block_test.cpp)
target_link_libraries(capi_error_block_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_error_block_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_error_block COMMAND capi_error_block_test)

# T015/T016 (US1, SC-007) — engine + session lifecycle from C.
add_executable(capi_lifecycle_test lifecycle_test.cpp)
target_link_libraries(capi_lifecycle_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_lifecycle_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_lifecycle COMMAND capi_lifecycle_test)

# T012 (US1, CA-005) — exhaustive config-builder contract: per-setter null-handle
# guards, eager validation arms (CONFIG_INVALID), enum mappings, happy paths.
add_executable(capi_config_builders_test config_builders_test.cpp)
target_link_libraries(capi_config_builders_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_config_builders_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_config_builders COMMAND capi_config_builders_test)

# T015/T019/T025/T031 (US1/US2/US3) — engine+session lifecycle/op REJECTION arms:
# null-handle guards, register-before-start (FR-004/FR-011), major gate, clock /
# double-start surfacing, eager send validation, never-established close.
add_executable(capi_lifecycle_negative_test lifecycle_negative_test.cpp)
target_link_libraries(capi_lifecycle_negative_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_lifecycle_negative_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_lifecycle_negative COMMAND capi_lifecycle_negative_test)

# T024 (US2, SC-006) — thunk split: in-process SIGABRT trap + construction-time
# no-abort discipline + the steady-state send-throw seam (FIXPP_TEST_HOOKS).
add_executable(capi_thunk_split_test thunk_split_test.cpp)
target_link_libraries(capi_thunk_split_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_thunk_split_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
target_compile_definitions(capi_thunk_split_test PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME capi_thunk_split COMMAND capi_thunk_split_test)

# T028/T029/T030 (US3, SC-001/SC-008/FR-013a) — headline two-C-ABI-engine
# plaintext round-trip with the drain-thread reply path.
add_executable(capi_send_recv_test send_recv_test.cpp)
target_link_libraries(capi_send_recv_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_send_recv_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
# Issue #151: the reaped-close tests use the FIXPP_TEST_HOOKS Session accessor
# is_drained_for_test() to wait DETERMINISTICALLY for the acceptor to reach
# closed_drained (avoiding a sleep that races the `closing` window) and the
# set_session_ever_established seam (capi_internal.hpp gated decl). ODR-safe: both
# FIXPP_TEST_HOOKS blocks are methods/decls only — no gated data members — so the
# struct layouts match the non-hooks library (same pattern as capi_error_live_test).
target_compile_definitions(capi_send_recv_test PRIVATE FIXPP_TEST_HOOKS)
# Split into two ctest entries so WILL_FAIL is scoped ONLY to the deliberate
# ASan UAF witness (T029), not to the positive round-trip tests (CA-006/007).
# A binary-wide WILL_FAIL would invert a real ASan failure in the positive tests
# into a pass, silently masking CA-006/007 ASan coverage.
#
# capi_send_recv_positive — all tests EXCEPT the ASan UAF witness: must PASS
#   normally, including under ASan (no WILL_FAIL).
# capi_send_recv_uaf_negative — ONLY the ASan witness: WILL_FAIL under ASan
#   (the process aborts on the deliberate stack-use-after-return); the test is
#   #if-guarded in the source, so without ASan it does not exist and the
#   negative entry exits 0 (no WILL_FAIL needed outside ASan).
add_test(NAME capi_send_recv_positive
    COMMAND capi_send_recv_test
        --gtest_filter=-CapiSendRecv.InboundHandleUseAfterReturnCaughtUnderAsan)
add_test(NAME capi_send_recv_uaf_negative
    COMMAND capi_send_recv_test
        --gtest_filter=CapiSendRecv.InboundHandleUseAfterReturnCaughtUnderAsan)
if(FIXPP_ENABLE_ASAN)
    set_tests_properties(capi_send_recv_uaf_negative PROPERTIES WILL_FAIL TRUE)
endif()

# T034a (US3, FR-013) — §VIII.5 zero-global-alloc gate for the on-strand receive
# trampoline (CapiApplication::fromApp). Runs the synchronous fromApp corpus as a
# plain test (markers no-op); the BINDING gate is the mallocnesia ctest entry below
# (global-malloc interception via LD_PRELOAD — a counting-resource alone false-passes).
add_executable(capi_recv_alloc_guard_test recv_alloc_guard_test.cpp)
target_link_libraries(capi_recv_alloc_guard_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_recv_alloc_guard_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_recv_alloc_guard COMMAND capi_recv_alloc_guard_test)

# gate-b/r1 (Q3+Q4): live send-path negative witnesses + live downgrade witness.
# Q3(a) malformed payload → FIXPP_ERR_UNKNOWN + no transmit.
# Q3(b) seqnum_max overflow → FIXPP_ERR_STORE_RUNTIME + no transmit (needs FIXPP_TEST_HOOKS).
# Q4 live consumer_minor downgrade through fixpp_engine_start public thunk.
add_executable(capi_error_live_test error_live_test.cpp)
target_link_libraries(capi_error_live_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_error_live_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
target_compile_definitions(capi_error_live_test PRIVATE FIXPP_TEST_HOOKS)
add_test(NAME capi_error_live COMMAND capi_error_live_test)
set_tests_properties(capi_error_live PROPERTIES LABELS "capi;050")

# ── 051 Feature C test targets (stubs; bodies land per-story T005/T008/T011/T019) ──
# message_read_test — CA-008/CA-010-read: inbound field + group read accessors
add_executable(capi_message_read_test message_read_test.cpp)
target_link_libraries(capi_message_read_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_message_read_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_message_read COMMAND capi_message_read_test)
set_tests_properties(capi_message_read PROPERTIES LABELS "capi;051")

# message_write_test — CA-009/CA-010-write: outbound construct/populate/commit + group builder
add_executable(capi_message_write_test message_write_test.cpp)
target_link_libraries(capi_message_write_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_message_write_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_message_write COMMAND capi_message_write_test)
set_tests_properties(capi_message_write PROPERTIES LABELS "capi;051")

# msg_clone_cross_strand_test — CA-009 clone seam ([2i §9] seam #13, SC-006)
add_executable(capi_msg_clone_cross_strand_test msg_clone_cross_strand_test.cpp)
target_link_libraries(capi_msg_clone_cross_strand_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_msg_clone_cross_strand_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_msg_clone_cross_strand COMMAND capi_msg_clone_cross_strand_test)
set_tests_properties(capi_msg_clone_cross_strand PROPERTIES LABELS "capi;051")

# toapp_callback_test — CA-009 toApp send-callback (US6, T019)
# Reaches capi_internal.hpp + capi_loopback_support.hpp (L-050-1/L-050-5 seams)
# and the session test support (minimal_dictionary.hpp) — same include set as the
# Feature-B loopback tests.
add_executable(capi_toapp_callback_test toapp_callback_test.cpp)
target_link_libraries(capi_toapp_callback_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_toapp_callback_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_toapp_callback COMMAND capi_toapp_callback_test)
set_tests_properties(capi_toapp_callback PROPERTIES LABELS "capi;051")

# toapp_alloc_guard_test — T020 / SC-003 third zero-heap path: the on-strand
# toApp trampoline (CapiApplication::toApp) wraps MessageView in a STACK
# fixpp_msg and must not touch the global heap ([const §VIII.5], E-6).
# Runs SYNCHRONOUSLY like recv_alloc_guard_test (marker thread = calling thread).
# Binding gate = the mallocnesia ctest entry below.
add_executable(capi_toapp_alloc_guard_test toapp_alloc_guard_test.cpp)
target_link_libraries(capi_toapp_alloc_guard_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_toapp_alloc_guard_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_toapp_alloc_guard COMMAND capi_toapp_alloc_guard_test)
set_tests_properties(capi_toapp_alloc_guard PROPERTIES LABELS "capi;051;alloc_guard")

# T011 (052 US2 / SC-001) — two-engine TCP loopback round-trip, pure public C-ABI headers.
# Witnesses: set_tcp_endpoint, acceptor_bound_endpoint, set_reset_seqnum_policy,
# E1 dict-handle-destroy-before-open, D-4 bilateral_strict (production default) establishment.
# No capi_loopback_support.hpp / capi_internal.hpp seam.
add_executable(capi_public_roundtrip_test public_roundtrip_test.cpp)
target_link_libraries(capi_public_roundtrip_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(capi_public_roundtrip_test PRIVATE
  FIXPP_DICT_DIR="${CMAKE_SOURCE_DIR}/dictionaries")
add_test(NAME capi_public_roundtrip COMMAND capi_public_roundtrip_test)
set_tests_properties(capi_public_roundtrip PROPERTIES LABELS "capi;052")

# T018 (052 US3 / CA-053) — field iteration: fixpp_msg_field_count + fixpp_msg_field_at.
# FR-006 wire-order (mutation-discriminating), FR-007 superset on repeating-group frame
# (SC-002), zero-global-heap alloc guard (SC-003 in-process marker), clone cross-thread
# (SC-003 THREAD_SAFE clone witness), error paths (NULL_HANDLE/INVALID_HANDLE/INDEX_OOR).
add_executable(capi_message_field_iteration_test message_field_iteration_test.cpp)
target_link_libraries(capi_message_field_iteration_test PRIVATE fixpp_capi GTest::gtest GTest::gtest_main)
target_include_directories(capi_message_field_iteration_test PRIVATE ${FIXPP_CAPI_FEATURE_B_INCLUDES})
add_test(NAME capi_message_field_iteration COMMAND capi_message_field_iteration_test)
set_tests_properties(capi_message_field_iteration PROPERTIES LABELS "capi;052" TIMEOUT 120)

# T003/T009 (052 US1 / SC-004) — dict loader public-API witness: load all bundled XMLs,
# null/missing/malformed-XML negative arms, sequential + concurrent double-destroy (TSan).
# Public headers only — no capi_internal.hpp seam needed.
add_executable(capi_dictionary_load_test dictionary_load_test.cpp)
target_link_libraries(capi_dictionary_load_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(capi_dictionary_load_test PRIVATE
  FIXPP_DICT_DIR="${CMAKE_SOURCE_DIR}/dictionaries")
add_test(NAME capi_dictionary_load COMMAND capi_dictionary_load_test)
set_tests_properties(capi_dictionary_load PROPERTIES LABELS "capi;052")

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
    add_test(
        NAME capi_recv_alloc_guard_mallocnesia
        COMMAND
            python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:capi_recv_alloc_guard_test>"
    )
    set_tests_properties(capi_recv_alloc_guard_mallocnesia PROPERTIES
        LABELS "capi;050;alloc_guard"
        DEPENDS capi_recv_alloc_guard_test
    )

    # SC-003 dual gate — read path: mallocnesia LD_PRELOAD interception for the
    # inbound get_* accessors. The in-process marker test is in message_read_test.cpp
    # (ZeroGlobalHeapAllocGuard). A counting-resource alone false-passes
    # ([[feedback_tracking_pmr_resource_false_pass]]); this entry is the binding gate.
    add_test(
        NAME capi_message_read_mallocnesia
        COMMAND
            python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:capi_message_read_test>"
    )
    set_tests_properties(capi_message_read_mallocnesia PROPERTIES
        LABELS "capi;051;alloc_guard"
        DEPENDS capi_message_read_test
    )

    # SC-003 dual gate — write path: mallocnesia LD_PRELOAD interception for the
    # outbound set_*/commit accessors. The in-process marker test is in
    # message_write_test.cpp (ZeroGlobalHeapSetCommitGuard).
    add_test(
        NAME capi_message_write_mallocnesia
        COMMAND
            python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:capi_message_write_test>"
    )
    set_tests_properties(capi_message_write_mallocnesia PROPERTIES
        LABELS "capi;051;alloc_guard"
        DEPENDS capi_message_write_test
    )

    # SC-003 dual gate — toApp path: mallocnesia LD_PRELOAD interception for the
    # on-strand toApp trampoline (CapiApplication::toApp). The in-process marker
    # test is in toapp_alloc_guard_test.cpp (TrampolineToAppNoGlobalHeapAlloc).
    # Third zero-heap path per SC-003 / T020 ([[feedback_tracking_pmr_resource_false_pass]]).
    add_test(
        NAME capi_toapp_alloc_guard_mallocnesia
        COMMAND
            python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:capi_toapp_alloc_guard_test>"
    )
    set_tests_properties(capi_toapp_alloc_guard_mallocnesia PROPERTIES
        LABELS "capi;051;alloc_guard"
        DEPENDS capi_toapp_alloc_guard_test
    )

    # SC-003 dual gate — field iteration: mallocnesia LD_PRELOAD interception for
    # fixpp_msg_field_count + fixpp_msg_field_at (CA-053 / 052 US3).  These functions
    # are pure span lookups over the OffsetTable; the in-process alloc_guard_markers
    # test confirms zero-alloc; mallocnesia is the binding gate
    # ([[feedback_tracking_pmr_resource_false_pass]]).
    add_test(NAME capi_message_field_iteration_mallocnesia
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
                "--binary" "$<TARGET_FILE:capi_message_field_iteration_test>"
    )
    set_tests_properties(capi_message_field_iteration_mallocnesia PROPERTIES
        LABELS "capi;052;alloc_guard"
        DEPENDS capi_message_field_iteration_test
    )
endif()

# 062 T022 (Polish, FR-007/SC-004) — C-ABI symbol-set + error-enum
# no-regression guard: 062 is a wire+codegen-internal fix and must make NO
# C-ABI / error-enum change. Reuses the frozen golden file the CI
# abi-golden.yml workflow enforces (shells out to `nm` on the REAL built
# fixpp_capi archive) rather than hand-maintaining a second symbol list.
add_executable(capi_abi_symbol_golden_test abi_symbol_golden_test.cpp)
target_link_libraries(capi_abi_symbol_golden_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(capi_abi_symbol_golden_test PRIVATE
  "FIXPP_CAPI_LIB=\"$<TARGET_FILE:fixpp_capi>\""
  "FIXPP_CAPI_GOLDEN=\"${CMAKE_SOURCE_DIR}/tests/abi/golden/fixpp_capi_symbols.txt\""
)
add_test(NAME capi_abi_symbol_golden COMMAND capi_abi_symbol_golden_test)

# ── 066-dict-backed-inbound-parse T001 — C-ABI engine-loopback scaffold smoke ─
# Compile/smoke check for capi_dict066_loopback_support.hpp (a real
# two-C-ABI-engine plaintext loopback pair configured with the REAL FIX44
# dictionary) + the group-bearing frame builders
# (tests/support/fix44_group_frame_bodies.hpp). NO group-membership/extent
# assertions here (RED-first preservation for T005) — see the file header
# comment.
add_executable(capi_dict066_group_loopback_smoke_test dict066_group_loopback_smoke_test.cpp)
target_link_libraries(capi_dict066_group_loopback_smoke_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_dict066_group_loopback_smoke_test PRIVATE
  ${FIXPP_CAPI_FEATURE_B_INCLUDES}
)
target_compile_definitions(capi_dict066_group_loopback_smoke_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME capi_dict066_group_loopback_smoke COMMAND capi_dict066_group_loopback_smoke_test)
set_tests_properties(capi_dict066_group_loopback_smoke PROPERTIES
  LABELS "066;setup;scaffold;capi")

# ── 066-dict-backed-inbound-parse T005 — RED-first group-membership witness ──
# (C-ABI engine loopback). MUST be observed RED against the current
# dict-free parse (src/session/session.cpp:316) — T006 flips the parse site
# later. See the file header comment for the discriminating assertion.
add_executable(capi_dict066_group_membership_red_test dict066_group_membership_red_test.cpp)
target_link_libraries(capi_dict066_group_membership_red_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(capi_dict066_group_membership_red_test PRIVATE
  ${FIXPP_CAPI_FEATURE_B_INCLUDES}
)
target_compile_definitions(capi_dict066_group_membership_red_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME capi_dict066_group_membership_red COMMAND capi_dict066_group_membership_red_test)
set_tests_properties(capi_dict066_group_membership_red PROPERTIES
  LABELS "066;us1;red;capi")
