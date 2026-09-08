find_package(GTest CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)

# ── Slice 3a: layout smoke test ───────────────────────────────────────────
add_executable(log_smoke_test log_smoke_test.cpp)
target_link_libraries(log_smoke_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_smoke COMMAND log_smoke_test)

# ── Slice 3b-i: T015 (TS-2) — overflow drop_newest ───────────────────────
add_executable(log_overflow_test test_overflow_drop_newest.cpp)
target_link_libraries(log_overflow_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_overflow COMMAND log_overflow_test)

# ── Slice 3b-i: T019 (TS-8) — level + category filter ────────────────────
add_executable(log_filter_test test_level_and_category_filter.cpp)
target_link_libraries(log_filter_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_filter COMMAND log_filter_test)

# ── Slice 3b-ii: T027 — shutdown(drain_timeout) + async_flush ────────────
add_executable(log_shutdown_test test_shutdown_async_flush.cpp)
target_link_libraries(log_shutdown_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_shutdown COMMAND log_shutdown_test)

# ── Slice 3b-ii: T016 (TS-3) — block overflow on raw std::thread ─────────
add_executable(log_block_test test_block_overflow_raw_thread.cpp)
target_link_libraries(log_block_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_block COMMAND log_block_test)

# ── Slice 3b-ii: T014 (TS-1) — compile cutoff + dual-gate zero-alloc ─────
# NOTE: This test is compiled with FIXPP_LOG_MIN_LEVEL overridden to warn(3)
# so that debug/info call sites are compiled out.
add_executable(log_alloc_test test_compile_cutoff_zero_alloc.cpp)
target_compile_definitions(log_alloc_test PRIVATE
  FIXPP_LOG_MIN_LEVEL=3   # warn (Level::warn == 3)
)
target_link_libraries(log_alloc_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(log_alloc_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
add_test(NAME log_alloc COMMAND log_alloc_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# T014 gate: run under LD_PRELOAD=mallocnesia to catch global new() calls.
# MALLOCNESIA_MAX_ALLOCS=0 means zero allocations allowed in the guard window.
set(MALLOCNESIA_SO "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
add_test(NAME log_alloc_mallocnesia
  COMMAND ${CMAKE_COMMAND} -E env
    "LD_PRELOAD=${MALLOCNESIA_SO}"
    "MALLOCNESIA_MAX_ALLOCS=0"
    $<TARGET_FILE:log_alloc_test>
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(log_alloc_mallocnesia PROPERTIES
  PASS_REGULAR_EXPRESSION "PASSED"
  FAIL_REGULAR_EXPRESSION "\\[mallocnesia\\] FAIL")

# ── Slice 3c: T017 (TS-4) — FileSink rotation + byte bound ───────────────────
add_executable(log_file_rotation_test test_file_sink_rotation.cpp)
target_link_libraries(log_file_rotation_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(log_file_rotation_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"  # support/temp_dir.hpp (#404)
)
add_test(NAME log_file_rotation COMMAND log_file_rotation_test)

# ── Slice 3c: T018 (TS-5) — FileSink fsync on drain thread ───────────────────
add_executable(log_file_fsync_test test_file_sink_async_fsync.cpp)
target_link_libraries(log_file_fsync_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(log_file_fsync_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"  # support/temp_dir.hpp (#404)
)
add_test(NAME log_file_fsync COMMAND log_file_fsync_test)
# Gate B PR #227 F-1 / #400. CONDITION, not a census: some assertions in this
# file still decide pass/fail from a small absolute wall-clock ceiling, and a
# correct implementation descheduled behind a CPU-heavy co-runner reports an
# I/O-blocking regression that never happened. Re-derive which ones and how much
# slack each has -- do not trust a list written here, it goes stale silently:
#   grep -n 'EXPECT_LT\|EXPECT_GT' test_file_sink_async_fsync.cpp
# RUN_SERIAL is isolation from CTest's OWN scheduler, not from unrelated machine
# load, so it mitigates and does not cure. It is a no-op on every serial lane.
#
# #400 removed the two close() ceilings rather than widening them: the join
# obligation they named is now asserted CAUSALLY (the injected fsync sets a flag
# as its last act; close() must have joined by the time it returns), which has no
# budget to tune and, unlike a ceiling, goes red on the detach regression.
# Prefer that shape over a new ceiling when adding to this file.
#
# TIMEOUT is the instrument for a HANG. Dropping the close() ceilings means a
# close() that never returns no longer fails an assertion, so it must fail here
# instead -- ctest's default is 1500 s, which reports a hang as 25 minutes of
# nothing. The whole binary measured ~1.4 s on the TSan lane.
set_tests_properties(log_file_fsync PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

# ── #211: FileSink under producer backpressure (real sink, rotation storm) ───
add_executable(log_file_backpressure_test test_file_sink_backpressure.cpp)
target_link_libraries(log_file_backpressure_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(log_file_backpressure_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"  # support/temp_dir.hpp (#404)
)
add_test(NAME log_file_backpressure COMMAND log_file_backpressure_test)
# RUN_SERIAL for two independent reasons, both structural rather than measured:
#   * the rotation storm creates and renames files in a loop, and the test reads
#     the whole directory back; it must not share a working directory or the
#     machine's I/O with another log test mid-rotation.
#   * backpressure is "the producer outruns the drain". A CPU-heavy co-runner
#     starving the DRAIN only makes drops more likely, so the risk is not a
#     false red here but a co-runner starving the PRODUCER into keeping up.
# TIMEOUT is the hang instrument (ctest's default is 1500 s). The bound is
# generous against the 60 s drain_timeout the test configures, so it fires only
# on a genuine hang, never on a slow runner.
set_tests_properties(log_file_backpressure PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# ── Slice 3c: T029 — SyslogSink smoke test (POSIX-only; guard inside test) ───
add_executable(log_syslog_test test_syslog_sink.cpp)
target_link_libraries(log_syslog_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_syslog COMMAND log_syslog_test)

# ── Phase 4: T031 (TS-6) — LOG-003 trace correlation (three macros) ──────────
# Links fixpp_session (for Engine + Session) + fixpp_log + fixpp_mock_clock.
add_executable(log_trace_correlation_test test_trace_correlation.cpp)
target_include_directories(log_trace_correlation_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"   # support/wait_until.hpp (#315)
)
target_link_libraries(log_trace_correlation_test PRIVATE
  fixpp_log
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_trace_correlation COMMAND log_trace_correlation_test)

# ── Phase 4: T032 (TS-7) — FIXPP_LOG0 from a raw thread ─────────────────────
add_executable(log_log0_raw_thread_test test_log0_raw_thread.cpp)
target_include_directories(log_log0_raw_thread_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"   # support/wait_until.hpp (#315)
)
target_link_libraries(log_log0_raw_thread_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_log0_raw_thread COMMAND log_log0_raw_thread_test)

# ── Phase 5: T037 (TS-10) + E10 — OtlpLogSink field mapping + failing exporter
# Requires the OTel SDK (fixpp_log_otlp separate target).
if(TARGET opentelemetry-cpp::api AND TARGET fixpp_log_otlp)
  add_executable(log_otlp_sink_test test_otlp_log_sink.cpp)
  target_link_libraries(log_otlp_sink_test PRIVATE
    fixpp_log_otlp
    opentelemetry-cpp::logs
    opentelemetry-cpp::exporter_otlp_http_log
    opentelemetry-cpp::otlp_recordable
    GTest::gtest
    GTest::gtest_main
  )
  add_test(NAME ts10_otlp_log_sink COMMAND log_otlp_sink_test)
endif()
