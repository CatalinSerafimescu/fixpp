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
# fixpp_log exports FIXPP_LOG_MIN_LEVEL PUBLIC, so this target sees the library's
# value AND the override (=3), and the later one wins -- the override IS the
# property under test.
#
# ⚠️ THE OVERRIDE IS APPLIED AS -U THEN -D, NOT AS A SECOND -D, so that no
# redefinition happens at all and there is nothing to suppress. The previous
# shape defined it twice and silenced the resulting diagnostic with
# `-Wno-macro-redefined` for every non-MSVC compiler -- but that is a CLANG
# spelling. GCC does not recognise it, accepts it silently (an unknown `-Wno-*`
# is only reported when some other diagnostic fires), and still warns. Under
# FIXPP_WERROR that is a hard error, which is why this target could not build
# once #439 turned -Werror on for the gcc presets. The comment here previously
# claimed the suppression covered that case; it never did on GCC. Same shape as
# the `#pragma clang diagnostic` rot #439 found elsewhere -- see
# brain/failure-classes.md class 16.
#
# ⚠️ ORDER IS LOAD-BEARING AND COMES FROM CMAKE, not from this file: CMake emits
# <DEFINES> before <FLAGS>, so a `-U` added here can only ever follow the
# library's PUBLIC `-D`. That is exactly what is wanted -- undefine, then define
# -- but it also means the override MUST NOT also be a target_compile_definition,
# or the redefinition it is avoiding happens in the DEFINES group before this
# runs. Verified: the macro is 3 after this (g++ -dM -E), not undefined.
#
# MSVC is deliberately left on its proven define + /wd4005 path: that leg cannot
# be exercised from here, and the defect being fixed is GCC-only. Unifying it is
# a follow-up, not a change to ship untested to a required lane.
target_compile_definitions(log_alloc_test PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:FIXPP_LOG_MIN_LEVEL=3>   # warn (Level::warn == 3)
)
target_compile_options(log_alloc_test PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/wd4005>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UFIXPP_LOG_MIN_LEVEL>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-DFIXPP_LOG_MIN_LEVEL=3>
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

# T014 gate: zero global allocations in the guard window.
#
# ⚠️ This entry used to set LD_PRELOAD directly, with NO guard on the interceptor
# existing — and ld.so IGNORES an unloadable preload rather than failing, so on every
# machine without the hand-built .so (all of CI) this ran UNINSTRUMENTED and passed.
# MEASURED on main: `LD_PRELOAD=/nonexistent/libmallocnesia.so MALLOCNESIA_MAX_ALLOCS=0
# log_alloc_test` prints the ld.so notice, then `[  PASSED  ] 2 tests`, exit 0.
#
# Routed through the shared helper, which runs check_alloc.py — the only path that
# fails closed on a missing interceptor AND requires the child's interception witness.
# The PASS/FAIL_REGULAR_EXPRESSION pair is gone with it: both were satisfied by an
# uninstrumented run, so neither discriminated (fixpp#448).
fixpp_add_mallocnesia_test(
  NAME log_alloc_mallocnesia
  TARGET log_alloc_test
  LABELS "t014;log")

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
