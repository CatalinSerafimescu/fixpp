# 068-test-binary-grouping: isolation-safe otel tests are grouped into a
# whole-binary add_test executable (bucketed by shared conditional-guard
# ∩ label — none of the 6 carry LABELS). Real-socket (fixed/ephemeral-port
# HTTP server) and genuinely-concurrent (std::thread) tests stay standalone.
# Dispositions in specs/068-test-binary-grouping/dispositions.md.
find_package(GTest CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)

# ── E2 (T044) + TS-12-full: otel_e2_spans_tests ───────────────────────────────
# Grouped whole-binary bucket (068 US2): test_engine_close_teardown (E2 T044 —
# Engine::stop() invokes TracerProvider::shutdown()/MeterProvider::shutdown()/
# Logger sink flush; single-threaded ioc.run(), no real socket, same pattern
# as the already-grouped session_pure_tests precedent) + test_session_spans_full
# (SessionSpans ctor/dtor/span coverage; no thread, no socket). Union link-deps.
if(TARGET opentelemetry-cpp::api)
  add_executable(otel_e2_spans_tests
    test_engine_close_teardown.cpp
    test_session_spans_full.cpp
  )
  target_link_libraries(otel_e2_spans_tests PRIVATE
    fixpp_otel
    fixpp_session
    fixpp_mock_clock
    opentelemetry-cpp::api
    opentelemetry-cpp::trace
    opentelemetry-cpp::metrics
    opentelemetry-cpp::exporter_in_memory  # may be header-only in Conan config
    asio::asio
    GTest::gtest
    GTest::gtest_main
  )
  # Direct link of the in-memory exporter static lib (Conan LIBS list is empty
  # even though the .a exists; link it explicitly to resolve the Factory symbol
  # — needed by test_session_spans_full.cpp).
  find_library(OTEL_EXPORTER_IN_MEMORY_LIB
    NAMES opentelemetry_exporter_in_memory
    HINTS ${opentelemetry-cpp_LIB_DIRS_DEBUG} ${opentelemetry-cpp_LIB_DIRS}
  )
  if(OTEL_EXPORTER_IN_MEMORY_LIB)
    target_link_libraries(otel_e2_spans_tests PRIVATE ${OTEL_EXPORTER_IN_MEMORY_LIB})
  endif()
  add_test(NAME otel_e2_spans_tests COMMAND otel_e2_spans_tests)
endif()

# ── TS-11: test_dual_metric_export ────────────────────────────────────────────
# Tests: counter==3 visible via real Prometheus scrape on :9464 AND via a mock
# OTLP push exporter in one ForceFlush cycle (FR-017 / SC-005).
if(TARGET opentelemetry-cpp::api)
  add_executable(test_dual_metric_export test_dual_metric_export.cpp)
  target_link_libraries(test_dual_metric_export PRIVATE
    fixpp_otel
    opentelemetry-cpp::metrics
    GTest::gtest
    GTest::gtest_main
  )
  add_test(NAME ts11_dual_metric_export COMMAND test_dual_metric_export)
endif()

# ── otel_smoke_test ───────────────────────────────────────────────────────────
add_executable(otel_smoke_test otel_smoke_test.cpp)
target_link_libraries(otel_smoke_test PRIVATE
  fixpp_otel
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME otel_smoke COMMAND otel_smoke_test)

# ── TS-12: test_session_spans ─────────────────────────────────────────────────
# Tests: SessionSpans / ParseSpan parent-child linkage (explicit SpanContext,
# no Scope), cross-thread parenting, E3 no-op fallback (FR-019).
#
# The opentelemetry-cpp::exporter_in_memory Conan target has an empty LIBS_DEBUG
# list (header-only in the Conan cmake config) even though the static lib exists.
# We link libopentelemetry_exporter_in_memory.a directly via the conan -L dir.
if(TARGET opentelemetry-cpp::api)
  add_executable(test_session_spans test_session_spans.cpp)
  target_link_libraries(test_session_spans PRIVATE
    fixpp_otel
    opentelemetry-cpp::exporter_in_memory  # may be header-only in Conan config
    opentelemetry-cpp::trace               # SimpleSpanProcessorFactory etc.
    GTest::gtest
    GTest::gtest_main
  )
  # Direct link of the in-memory exporter static lib (Conan LIBS list is empty
  # even though the .a exists; link it explicitly to resolve the Factory symbol).
  find_library(OTEL_EXPORTER_IN_MEMORY_LIB
    NAMES opentelemetry_exporter_in_memory
    HINTS ${opentelemetry-cpp_LIB_DIRS_DEBUG} ${opentelemetry-cpp_LIB_DIRS}
  )
  if(OTEL_EXPORTER_IN_MEMORY_LIB)
    target_link_libraries(test_session_spans PRIVATE ${OTEL_EXPORTER_IN_MEMORY_LIB})
  endif()
  add_test(NAME ts12_session_spans COMMAND test_session_spans)
endif()

# ── test_exporters ────────────────────────────────────────────────────────────
# Coverage-remediation: exercises PrometheusExporter ctor/shutdown,
# OtlpMetricExporter ctor/shutdown, OtelDualExportBuilder::with_prometheus /
# with_otlp / build (two-AddMetricReader wiring).
if(TARGET opentelemetry-cpp::api)
  add_executable(test_exporters test_exporters.cpp)
  target_link_libraries(test_exporters PRIVATE
    fixpp_otel
    opentelemetry-cpp::metrics
    GTest::gtest
    GTest::gtest_main
  )
  add_test(NAME otel_exporters COMMAND test_exporters)
endif()
