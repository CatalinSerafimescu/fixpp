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

# 007-threading-clock T051 (Seam 7 / [2d §9.7]): zero-global-heap between
# parse and fromApp over a 10⁴-message corpus. All dynamic storage is from
# PMR arenas (cancellable_dispatch node from session_arena; per-awaiter
# promise frame from message_arena on PMR fallback). Same mallocnesia harness.
# CTest NAME is deliberately NOT `threading_`-prefixed (driven by
# tools/check_alloc.py under LD_PRELOAD, not swept by the ^threading_ ctest
# selection — same convention as alloc_guard_sync_embedded above).
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

# 007-threading-clock T052 (Seam 18 / [2d §9.18]): zero-global-heap after the
# first heartbeat cycle over 10⁴ cycles, BOTH threading modes; an idle session
# that never sleeps allocates no slot (E12 / D-8 / N-P1-1). Same harness.
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

# 005-session-establishment-fsm T059 (Seam #12 / SC-009 / I-7):
# zero-global-heap on the 005-owned hot paths — SeqnumManager
# (assign_outbound + check_inbound) and the admin builders
# (build_heartbeat / build_test_request). The cancellable_dispatch + clock
# sleep paths are covered upstream by 007's alloc-guards above; this binary
# focuses on the 005-specific surface. Same mallocnesia harness.
# CTest NAME deliberately NOT `session_`-prefixed (driven by tools/check_alloc.py
# under LD_PRELOAD, not the ^session_ ctest selection — same convention as
# alloc_guard_dispatch above).
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
)
set_target_properties(test_validate_gate_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
# ── fixpp#448: the POSITIVE CONTROL for the entire mallocnesia population ─────
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
  LABELS "alloc_guard;positive-control;448")

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
