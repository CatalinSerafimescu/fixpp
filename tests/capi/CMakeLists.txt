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

# T022/T023 (US2, SC-004) — 050 Feature B send-path error oracle: deferred
# session/app arms stay UNKNOWN (L-050-4), existing-published arms keep codes,
# live consumer-minor downgrade on the send-path composition.
add_executable(capi_error_block_test error_block_test.cpp)
target_link_libraries(capi_error_block_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_error_block COMMAND capi_error_block_test)

# ── 050 Feature B test targets ──────────────────────────────────────────────
# lifecycle_test (T015/T016) + send_recv_test (T028/T029/T030) reach the engine-
# internal handles (capi_internal.hpp under src/capi) through the documented
# test-only seams (L-050-1 dict + L-050-5 transport endpoint) and use the
# session test support (minimal_dictionary.hpp under tests/support). thunk_split
# (T024) additionally arms the SC-006 send-throw seam via the FIXPP_TEST_HOOKS-
# gated set_send_throw_hook(). All link fixpp_capi (session/transport/tls/wire/dict).
set(FIXPP_CAPI_FEATURE_B_INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}"          # capi_loopback_support.hpp
  "${CMAKE_SOURCE_DIR}/src/capi"         # capi_internal.hpp + the public src tree
  "${CMAKE_SOURCE_DIR}/tests"            # support/minimal_dictionary.hpp
)

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
add_test(NAME capi_message_read COMMAND capi_message_read_test)
set_tests_properties(capi_message_read PROPERTIES LABELS "capi;051")

# message_write_test — CA-009/CA-010-write: outbound construct/populate/commit + group builder
add_executable(capi_message_write_test message_write_test.cpp)
target_link_libraries(capi_message_write_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_message_write COMMAND capi_message_write_test)
set_tests_properties(capi_message_write PROPERTIES LABELS "capi;051")

# msg_clone_cross_strand_test — CA-009 clone seam ([2i §9] seam #13, SC-006)
add_executable(capi_msg_clone_cross_strand_test msg_clone_cross_strand_test.cpp)
target_link_libraries(capi_msg_clone_cross_strand_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_msg_clone_cross_strand COMMAND capi_msg_clone_cross_strand_test)
set_tests_properties(capi_msg_clone_cross_strand PROPERTIES LABELS "capi;051")

# toapp_callback_test — CA-009 toApp send-callback (US6)
add_executable(capi_toapp_callback_test toapp_callback_test.cpp)
target_link_libraries(capi_toapp_callback_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_toapp_callback COMMAND capi_toapp_callback_test)
set_tests_properties(capi_toapp_callback PROPERTIES LABELS "capi;051")

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
endif()
