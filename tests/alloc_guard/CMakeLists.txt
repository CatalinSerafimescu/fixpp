# tests/alloc_guard/CMakeLists.txt — allocation discipline test (seam #6)
# Runs under mallocnesia via tools/check_alloc.py.

add_executable(decimal_alloc_guard_test decimal_alloc_guard_test.cpp)
target_link_libraries(decimal_alloc_guard_test PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(decimal_alloc_guard_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(decimal_alloc_guard_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME decimal_alloc_guard_test COMMAND decimal_alloc_guard_test)

# 004-wire-codec T051 (FR-012/SC-002): zero-heap-alloc over the wire
# parse+serialize hot path. Same mallocnesia harness as decimal above.
add_executable(wire_alloc_guard_test wire_alloc_guard_test.cpp)
target_link_libraries(wire_alloc_guard_test PRIVATE
  fixpp_wire
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(wire_alloc_guard_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(wire_alloc_guard_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME wire_alloc_guard_test COMMAND wire_alloc_guard_test)

# 006-async-mutex T056 (Seam #10 b): zero-global-alloc for the async_mutex
# embedded (mr==nullptr) contended hot path. Same mallocnesia harness as above.
# CTest NAME is deliberately NOT `sync_`-prefixed: this binary is driven by
# tools/check_alloc.py under the mallocnesia LD_PRELOAD (like the wire/decimal
# alloc-guards), and must NOT be swept into the `^sync_` seam ctest selection
# (where, run plainly without mallocnesia, it would no-op or — under a non-built
# preset — show as a spurious "Not Run").
add_executable(sync_alloc_guard_test sync_alloc_guard_test.cpp)
target_link_libraries(sync_alloc_guard_test PRIVATE
  fixpp_sync
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(sync_alloc_guard_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(sync_alloc_guard_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME alloc_guard_sync_embedded COMMAND sync_alloc_guard_test)

# 007-threading-clock T051 (Seam 7 / [2d §9.7]) asks for zero global heap between
# parse and fromApp over a 10⁴-message corpus. All dynamic storage is meant to come
# from PMR arenas (cancellable_dispatch node from session_arena; per-awaiter
# promise frame from message_arena on PMR fallback). No preload gate is registered
# for this binary (see "DELIBERATELY NOT GATED" below), so its window's global-heap
# half is unchecked and the cell checks its functional post-conditions only.
# CTest NAME is deliberately NOT `threading_`-prefixed, so the ^threading_ ctest
# selection does not sweep it (same convention as alloc_guard_sync_embedded above).
add_executable(test_dispatch_alloc_guard test_dispatch_alloc_guard.cpp)
target_link_libraries(test_dispatch_alloc_guard PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_dispatch_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(test_dispatch_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME alloc_guard_dispatch COMMAND test_dispatch_alloc_guard)

# 007-threading-clock T052 (Seam 18 / [2d §9.18]) asks for zero global heap after
# the first heartbeat cycle over 10⁴ cycles, BOTH threading modes; an idle session
# that never sleeps allocates no slot (E12 / D-8 / N-P1-1). No preload gate is
# registered ("DELIBERATELY NOT GATED" below): the global-heap half is unchecked.
add_executable(test_clock_sleep_alloc_guard test_clock_sleep_alloc_guard.cpp)
target_link_libraries(test_clock_sleep_alloc_guard PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_clock_sleep_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(test_clock_sleep_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME alloc_guard_clock_sleep COMMAND test_clock_sleep_alloc_guard)

# 005-session-establishment-fsm T059 (Seam #12 / SC-009 / I-7) asks for zero
# global heap on the 005-owned hot paths — SeqnumManager
# (assign_outbound + check_inbound) and the admin builders
# (build_heartbeat / build_test_request). The cancellable_dispatch + clock
# sleep paths are 007's alloc-guards above; this binary focuses on the
# 005-specific surface. No preload gate is registered ("DELIBERATELY NOT GATED"
# below): the global-heap half is unchecked.
# CTest NAME deliberately NOT `session_`-prefixed, so the ^session_ ctest
# selection does not sweep it (same convention as alloc_guard_dispatch above).
add_executable(test_session_alloc_guard test_session_alloc_guard.cpp)
target_link_libraries(test_session_alloc_guard PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_session_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(test_session_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME alloc_guard_session COMMAND test_session_alloc_guard)

# 041-validation-gate-wiring: zero-global-heap for the validate-ON (validate_
# inbound_messages=true) hot path.  Closes the blind spot in the suite: prior
# cells cover the validation-OFF default path only; this cell detects a
# per-message coroutine-frame heap alloc if validate_inbound_() were (re-)
# introduced as an asio::awaitable<> (the pre-simplify-triage regression).
# Same mallocnesia harness as the other alloc-guard cells.
# CTest NAME deliberately NOT `session_`-prefixed (driven by tools/check_alloc.py
# under LD_PRELOAD, not swept by the ^session_ ctest selection — same convention
# as alloc_guard_dispatch above).
add_executable(test_validate_gate_alloc_guard test_validate_gate_alloc_guard.cpp)
target_link_libraries(test_validate_gate_alloc_guard PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_validate_gate_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"  # 093: session/parse_capacity.hpp, the B(L) formula
)
set_target_properties(test_validate_gate_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
# ── fixpp#448: the malloc POSITIVE CONTROL for the mallocnesia population ─────
# Eighteen gates reporting "no allocations detected" cannot tell a clean tree from a
# dead interceptor. This one allocates on purpose and must be CAUGHT; if interception
# silently stops, this is the entry that reddens. Registered on the same lane and in
# the same ctest label as the gates it vouches for, so it cannot be skipped separately
# from them. See planted_alloc_witness.cpp for why it is not WILL_FAIL.
add_executable(planted_alloc_witness planted_alloc_witness.cpp)
target_include_directories(planted_alloc_witness PRIVATE "${CMAKE_SOURCE_DIR}/tests")
set_target_properties(planted_alloc_witness PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
fixpp_add_mallocnesia_test(
  NAME alloc_guard_positive_control_mallocnesia
  TARGET planted_alloc_witness
  EXPECT_VIOLATION
  EXPECT_ENTRY malloc
  LABELS "alloc_guard;positive-control;448")

# fixpp#497: one control per hooked entry point other than malloc, plus an over-aligned
# `new`. The control above plants a malloc, so it vouches for the malloc hook only. Each
# libc entry below plants through the entry point its PLANTED_ENTRY names, so a hook that
# stops counting reddens the entry naming it. The aligned_new control names the set of
# aligned entry points a C++ runtime's over-aligned `new` reaches.
if(FIXPP_MALLOCNESIA_SUPPORTED)
  add_executable(planted_entry_witness planted_entry_witness.cpp)
  target_include_directories(planted_entry_witness PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  set_target_properties(planted_entry_witness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  fixpp_add_mallocnesia_test(
    NAME alloc_guard_aligned_new_positive_control_mallocnesia
    TARGET planted_entry_witness
    EXPECT_VIOLATION
    EXPECT_ENTRY aligned_alloc,posix_memalign
    LABELS "alloc_guard;positive-control;497"
    ENVIRONMENT "PLANTED_ENTRY=aligned_new")
  foreach(_plant IN ITEMS calloc realloc aligned_alloc posix_memalign memalign valloc
                          pvalloc)
    fixpp_add_mallocnesia_test(
      NAME alloc_guard_${_plant}_positive_control_mallocnesia
      TARGET planted_entry_witness
      EXPECT_VIOLATION
      EXPECT_ENTRY ${_plant}
      LABELS "alloc_guard;positive-control;497"
      ENVIRONMENT "PLANTED_ENTRY=${_plant}")
  endforeach()
endif()

add_test(NAME alloc_guard_validate_gate COMMAND test_validate_gate_alloc_guard)
set_tests_properties(alloc_guard_validate_gate PROPERTIES
  LABELS "041;alloc_guard;validation-gate;t014"
)

# Mallocnesia gate: re-runs the validate-ON alloc-guard under LD_PRELOAD so global
# malloc/calloc/realloc is intercepted. registered on every FIXPP_MALLOCNESIA_SUPPORTED configuration (fixpp#448) — it used to be
# conditional on a hand-built .so, so it did not exist on any CI runner. The base test
# alloc_guard_validate_gate still runs WITHOUT interception and asserts the validation
# outcome only; it is a companion, never a fallback for this one.
fixpp_add_mallocnesia_test(
  NAME alloc_guard_validate_gate_mallocnesia
  TARGET test_validate_gate_alloc_guard
  LABELS "041;alloc_guard;validation-gate;t014"
  DEPENDS "alloc_guard_validate_gate")
# 093-inbound-frame-dispositions: label 093 (expected-ctest-093.txt). APPEND, after the
# last LABELS setter for these entries, so the existing labels survive.
set_property(TEST alloc_guard_validate_gate APPEND PROPERTY LABELS 093)
# The twin exists only where fixpp_add_mallocnesia_test registers it.
if(TEST alloc_guard_validate_gate_mallocnesia)
  set_property(TEST alloc_guard_validate_gate_mallocnesia APPEND PROPERTY LABELS 093)
endif()

# ── item 13 (2026-06-19): mallocnesia gate entries for the alloc-guard binaries
# that previously had NO LD_PRELOAD entry. The marker fix (weak UNDEFINED symbols,
# item 13) makes interception real; without these add_test entries the binaries
# ran plainly and never enforced zero-alloc under mallocnesia. registered on every FIXPP_MALLOCNESIA_SUPPORTED configuration
# since fixpp#448. Mirrors alloc_guard_validate_gate_mallocnesia.
#
# Only the binaries whose MEASURED WINDOW is genuinely zero-global-alloc are gated
# here (decimal / wire / sync — all verified green under the now-real interceptor).
# DELIBERATELY NOT GATED: test_dispatch_alloc_guard, test_clock_sleep_alloc_guard,
# test_session_alloc_guard — their windows wrap `ioc.run()` / `run_sync()` /
# `co_spawn()`, which allocate coroutine frames + asio run-loop handlers on the
# global heap (the false-green these were silently relying on). Gating them at
# max-allocs=0 would be red. Re-scoping those windows to the true steady-state hot
# path is tracked as a follow-up (see parent REMAINING-WORK.md item 13).
foreach(_ag IN ITEMS
    decimal_alloc_guard_test
    wire_alloc_guard_test
    sync_alloc_guard_test)
  fixpp_add_mallocnesia_test(
    NAME ${_ag}_mallocnesia
    TARGET ${_ag}
    LABELS "alloc_guard;item13")
endforeach()

# 066-dict-backed-inbound-parse T013 (SC-004/FR-004): zero-GLOBAL-heap gate
# for the dict-backed inbound parse+read path, measured at the
# Parser<Index>{tv} + OffsetTable level (NOT through Session::on_inbound_frame
# — see the file header's "MEASUREMENT SCOPE" note: a session-dispatch window
# wraps co_spawn/ioc.run(), which is the same coroutine-frame/asio-run-loop
# global-heap noise the "DELIBERATELY NOT GATED" note above already
# documents for test_dispatch_alloc_guard/test_session_alloc_guard). Dual
# gate: TU-local operator-new counter (this test) + mallocnesia below.
add_executable(test_dict066_grouped_read_alloc_guard test_dict066_grouped_read_alloc_guard.cpp)
target_link_libraries(test_dict066_grouped_read_alloc_guard PRIVATE
  fixpp_wire
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_dict066_grouped_read_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src"  # 093: session/parse_capacity.hpp, the B(L) formula
)
target_compile_definitions(test_dict066_grouped_read_alloc_guard PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
set_target_properties(test_dict066_grouped_read_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
# CTest NAME deliberately NOT alloc_guard_-prefixed by convention with the
# other mallocnesia-driven binaries above (driven by tools/check_alloc.py
# under LD_PRELOAD, not swept by a broader name-regex ctest selection).
add_test(NAME dict066_grouped_read_alloc_guard COMMAND test_dict066_grouped_read_alloc_guard)
set_tests_properties(dict066_grouped_read_alloc_guard PROPERTIES
  LABELS "066;us3;alloc_guard")

fixpp_add_mallocnesia_test(
  NAME dict066_grouped_read_alloc_guard_mallocnesia
  TARGET test_dict066_grouped_read_alloc_guard
  LABELS "066;us3;alloc_guard")
# 093-inbound-frame-dispositions: label 093 (expected-ctest-093.txt). APPEND, after the
# last LABELS setter for these entries, so the existing labels survive.
set_property(TEST dict066_grouped_read_alloc_guard APPEND PROPERTY LABELS 093)
# The twin exists only where fixpp_add_mallocnesia_test registers it.
if(TEST dict066_grouped_read_alloc_guard_mallocnesia)
  set_property(TEST dict066_grouped_read_alloc_guard_mallocnesia APPEND PROPERTY LABELS 093)
endif()

# 093-inbound-frame-dispositions T044 (quickstart Q-19; FR-052; plan OD-22): an Active
# read of the real session read pump makes no NEW global operator new call, against a
# constant taken from the merge-base's pump (its recipe is in the cell). Standalone ([const §VII.8]): its TU replaces
# global operator new for the whole binary, and its cells run an Engine over loopback TCP
# on a mock clock. No *_mallocnesia twin: its window wraps io_context::run_one_for(), the
# class the "DELIBERATELY NOT GATED" note above keeps out of the LD_PRELOAD gate.
# Linux only: the constant is the Linux one, where asio's reactor and executor queries
# are the ones the cell's constant names; another platform's completion path can make a
# different number of calls.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(test_093_pump_active_read_alloc_guard test_093_pump_active_read_alloc_guard.cpp)
  target_link_libraries(test_093_pump_active_read_alloc_guard PRIVATE
    fixpp_session
    fixpp_mock_clock
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_093_pump_active_read_alloc_guard PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
  )
  set_target_properties(test_093_pump_active_read_alloc_guard PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  add_test(NAME alloc_guard_093_pump_active_read COMMAND test_093_pump_active_read_alloc_guard)
  set_tests_properties(alloc_guard_093_pump_active_read PROPERTIES
    LABELS "093;alloc_guard")
endif()

# fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §10 T-14): owned-route
# dict::reify draws every allocation from the caller's mr. Dual gate: the TU-local
# operator-new counter (the plain test) plus mallocnesia. The owned cell is the
# gate; the borrowed cell is its EXPECT_VIOLATION control (the table copy on the
# global heap), each selected by GTEST_FILTER. Neither name carries
# `positive_control`: tools/check_mallocnesia_population.py rejects any label member
# with that in its name unless its POSITIVE_CONTROLS declares it.
add_executable(test_reify_owned_alloc_guard test_reify_owned_alloc_guard.cpp)
target_link_libraries(test_reify_owned_alloc_guard PRIVATE
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_reify_owned_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(test_reify_owned_alloc_guard PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
set_target_properties(test_reify_owned_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME reify_owned_alloc_guard COMMAND test_reify_owned_alloc_guard)
set_tests_properties(reify_owned_alloc_guard PROPERTIES LABELS "495;alloc_guard")

fixpp_add_mallocnesia_test(
  NAME reify_owned_alloc_guard_mallocnesia
  TARGET test_reify_owned_alloc_guard
  LABELS "495;alloc_guard"
  ENVIRONMENT "GTEST_FILTER=*OwnedRoute*")

fixpp_add_mallocnesia_test(
  NAME reify_borrowed_alloc_guard_expected_violation_mallocnesia
  TARGET test_reify_owned_alloc_guard
  EXPECT_VIOLATION
  LABELS "495;alloc_guard"
  ENVIRONMENT "GTEST_FILTER=*BorrowedRoute*")

# ── item 13: recurrence guard — fail if any test/bench source reintroduces a
# bodied alloc_guard_start/end definition (the local-symbol pattern that silently
# disables LD_PRELOAD interception and made every gate false-green). Cheap static
# scan, no preload needed; repo precedent = tools/check_admin_emit_observation.py.
add_test(
  NAME alloc_guard_markers_no_local_def
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc_guard_markers.py")

# ── fixpp#448: the SIBLING recurrence guard ──────────────────────────────────
# The one above catches a test DEFINING the markers locally (which silently disables
# interposition). This one catches a test registering a raw LD_PRELOAD itself, which
# bypasses the fail-closed wrapper, the witness, the label, and therefore CI selection.
# #448 named two such sites; both are converted, and this is what stops the third.
# Same shape, same label, no preload needed.
add_test(
  NAME alloc_guard_no_raw_preload
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_no_raw_mallocnesia_preload.py")
set_tests_properties(alloc_guard_no_raw_preload PROPERTIES
  LABELS "alloc_guard;mallocnesia;448")
set_tests_properties(alloc_guard_markers_no_local_def PROPERTIES
  LABELS "alloc_guard;mallocnesia;item13")

# ── fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3): driver-form windows ──
# W-D-R and its bracket, W-D's (b-L)/(b-S) pairs, the (c-L)/(c-H) arms and production
# twins, and W-D-W's stand-in bracket, in W-D's harness (tests/support/recycler_driver.hpp).
# The plain registration runs every case without the interceptor, for its functional
# post-conditions. The interceptor registrations are Release-only, one per case. Linux
# only: these windows' subject is class (b) and (c), which reach `std::aligned_alloc`, and
# MSVC's `_aligned_malloc` half is disclosed (§3, "Why MSVC is class (a) only").
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(test_544_driver_windows test_544_driver_windows.cpp)
  target_link_libraries(test_544_driver_windows PRIVATE
    fixpp_session
    fixpp_mock_clock
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_544_driver_windows PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  set_target_properties(test_544_driver_windows PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_test(NAME alloc_guard_544_driver_windows COMMAND test_544_driver_windows)
  set_tests_properties(alloc_guard_544_driver_windows PROPERTIES LABELS "544;alloc_guard")

  # name | gtest case | expected entry (empty: the case must read zero)
  set(_b35_driver_cells
    "alloc_guard_544_wdr_store_live_read_mallocnesia|WDR_StoreWithLiveRead|aligned_alloc"
    "alloc_guard_544_wdr_bracket_twin_mallocnesia|WDR_BracketTwin_NestFillsSlots|"
    "alloc_guard_544_wdr_bracket_arm_mallocnesia|WDR_BracketArm_NestOneOverSlots|aligned_alloc"
    "alloc_guard_544_wd_bl_arm_mallocnesia|WD_BL_Arm_OverLimitFrame|aligned_alloc"
    "alloc_guard_544_wd_bl_twin_mallocnesia|WD_BL_Twin_InLimitFrame|"
    "alloc_guard_544_wd_bs_arm_mallocnesia|WD_BS_Arm_NestOneOverSlots|aligned_alloc"
    "alloc_guard_544_wd_bs_twin_mallocnesia|WD_BS_Twin_NestFillsSlots|"
    "alloc_guard_544_cl_arm_mallocnesia|CL_Arm_StoreReplicaOnPublicLock|aligned_alloc"
    "alloc_guard_544_cl_production_twin_mallocnesia|CL_ProductionTwin_MemoryStoreStore|aligned_alloc"
    "alloc_guard_544_ch_arm_mallocnesia|CH_Arm_HydrateReplicaOnPublicLock|aligned_alloc"
    "alloc_guard_544_ch_production_twin_mallocnesia|CH_ProductionTwin_SeqnumManagerHydrate|aligned_alloc")
  foreach(_cell IN LISTS _b35_driver_cells)
    string(REPLACE "|" ";" _cell "${_cell}")
    list(GET _cell 0 _name)
    list(GET _cell 1 _case)
    list(GET _cell 2 _entry)
    fixpp_add_release_mallocnesia_test(
      NAME ${_name}
      TARGET test_544_driver_windows
      CASE "B35DriverWindows.${_case}"
      EXPECT_ENTRY "${_entry}"
      LABELS alloc_guard 544)
  endforeach()
endif()

# ── fixpp#544 (B35; §3): the run-thread windows (W-A, W-B, W-E, the veto-then-zero cell,
# arms (a), (b-L), (b-S) and (s)) on tests/support/run_thread_engine_rig.hpp. One source,
# two binaries: `mallocnesia.c` refuses a process that defines the allocator names, so the
# interceptor runs the build without a counter, and FIXPP_B35_TU_COUNTER builds T044's
# operator new counter in.
#
# The counter build is the T0.29 MSVC port: it is registered on every platform. On Windows
# it runs W-A, W-B and arm (a) only, the class-(a) cells an operator new counter can see
# there (§3, "Why MSVC is class (a) only"). Its count is asserted on Release builds only.
add_executable(test_544_run_thread_counter test_544_run_thread_windows.cpp)
target_compile_definitions(test_544_run_thread_counter PRIVATE FIXPP_B35_TU_COUNTER=1)
set(_b35_run_thread_targets test_544_run_thread_counter)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(test_544_run_thread_windows test_544_run_thread_windows.cpp)
  list(APPEND _b35_run_thread_targets test_544_run_thread_windows)
endif()
foreach(_t IN LISTS _b35_run_thread_targets)
  target_link_libraries(${_t} PRIVATE
    fixpp_session
    fixpp_mock_clock
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${_t} PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  set_target_properties(${_t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endforeach()
add_test(NAME alloc_guard_544_run_thread COMMAND test_544_run_thread_counter)
set_tests_properties(alloc_guard_544_run_thread PROPERTIES LABELS "544;alloc_guard" TIMEOUT 600)
if(WIN32)
  set_tests_properties(alloc_guard_544_run_thread PROPERTIES ENVIRONMENT
    "GTEST_FILTER=B35RunThreadWindows.WA_ActiveHeartbeat:B35RunThreadWindows.WB_ActiveAppMessage:B35RunThreadWindows.ArmA_TrackedExecutorFallback")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # name | gtest case | expected entry (empty: the case must read zero)
  set(_b35_rig_cells
    "alloc_guard_544_wa_active_heartbeat_mallocnesia|WA_ActiveHeartbeat|malloc,aligned_alloc"
    "alloc_guard_544_wb_active_app_message_mallocnesia|WB_ActiveAppMessage|malloc,aligned_alloc"
    "alloc_guard_544_we_sends_interleaved_mallocnesia|WE_InboundPairInterleavedWithSends|malloc,aligned_alloc"
    "alloc_guard_544_veto_then_zero_mallocnesia|VetoThenZero_SendsAfterAVetoedSend|malloc,aligned_alloc"
    "alloc_guard_544_arm_a_tracked_executor_mallocnesia|ArmA_TrackedExecutorFallback|malloc"
    "alloc_guard_544_wa_bl_arm_mallocnesia|WA_BL_Arm_OverLimitPlant|aligned_alloc"
    "alloc_guard_544_wa_bl_twin_mallocnesia|WA_BL_Twin_InLimitPlant|malloc,aligned_alloc"
    "alloc_guard_544_wa_bs_arm_mallocnesia|WA_BS_Arm_NestOneOverSlots|aligned_alloc"
    "alloc_guard_544_wa_bs_twin_mallocnesia|WA_BS_Twin_NestFillsSlots|malloc,aligned_alloc"
    "alloc_guard_544_arm_s_run_one_for_mallocnesia|ArmS_RunOneForScope|aligned_alloc")
  foreach(_cell IN LISTS _b35_rig_cells)
    string(REPLACE "|" ";" _cell "${_cell}")
    list(GET _cell 0 _name)
    list(GET _cell 1 _case)
    list(GET _cell 2 _entry)
    fixpp_add_release_mallocnesia_test(
      NAME ${_name}
      TARGET test_544_run_thread_windows
      CASE "B35RunThreadWindows.${_case}"
      EXPECT_ENTRY "${_entry}"
      LABELS alloc_guard 544)
  endforeach()
endif()

# ── fixpp#544 (B35; §3, "The TLS diagnostic"; R-5): W-A's run-thread shape on a TLS
# session. A diagnostic run by hand before and after the change (its file header has the
# recipe); it registers no ctest. Built on Linux, where the interceptor runs it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(OpenSSL 3.0 REQUIRED)
  find_library(FIXPP_B35_OPENSSL_CRYPTO_LIB NAMES crypto libcrypto
               PATHS "${openssl_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
  find_library(FIXPP_B35_OPENSSL_SSL_LIB NAMES ssl libssl
               PATHS "${openssl_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
  find_package(ZLIB REQUIRED)
  find_library(FIXPP_B35_ZLIB_LIB NAMES z libz PATHS "${zlib_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
  if(NOT FIXPP_B35_OPENSSL_CRYPTO_LIB)
    set(FIXPP_B35_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
  endif()
  if(NOT FIXPP_B35_OPENSSL_SSL_LIB)
    set(FIXPP_B35_OPENSSL_SSL_LIB OpenSSL::SSL)
  endif()
  if(NOT FIXPP_B35_ZLIB_LIB)
    set(FIXPP_B35_ZLIB_LIB ZLIB::ZLIB)
  endif()
  add_executable(test_544_tls_diagnostic test_544_tls_diagnostic.cpp)
  target_link_libraries(test_544_tls_diagnostic PRIVATE
    fixpp_session
    fixpp_transport
    fixpp_tls
    fixpp_mock_clock
    "${FIXPP_B35_OPENSSL_SSL_LIB}"
    "${FIXPP_B35_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_B35_ZLIB_LIB}"
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(test_544_tls_diagnostic PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  target_compile_definitions(test_544_tls_diagnostic PRIVATE
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures")
  set_target_properties(test_544_tls_diagnostic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
