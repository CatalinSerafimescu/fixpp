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
add_test(NAME log_file_rotation COMMAND log_file_rotation_test)

# ── Slice 3c: T018 (TS-5) — FileSink fsync on drain thread ───────────────────
add_executable(log_file_fsync_test test_file_sink_async_fsync.cpp)
target_link_libraries(log_file_fsync_test PRIVATE
  fixpp_log
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME log_file_fsync COMMAND log_file_fsync_test)
# Gate B PR #227 F-1: this test decides pass/fail from 40 ms (enqueue-does-not-
# block, :211) and 100 ms (flush-returns-before-a-500 ms-fsync, :255) wall-clock
# ceilings — its own comment concedes the 100 ms is "3x the flush_deadline for OS
# scheduling jitter". Under `ctest --parallel` a correct implementation can be
# descheduled past either ceiling behind a CPU-heavy co-runner, reporting an
# I/O-blocking regression that never happened. RUN_SERIAL costs 1.42 s (its whole
# runtime, measured on the TSan lane) and is a no-op on every serial lane.
# The deeper fix is to make the assertions scheduler-independent — assert enqueue
# completes while the mocked fsync is still blocked, rather than comparing a small
# wall-clock number — but that is a test-semantics change, not a CI probe's job.
set_tests_properties(log_file_fsync PROPERTIES RUN_SERIAL TRUE)

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
