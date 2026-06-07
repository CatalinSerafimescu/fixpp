find_package(GTest CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)

# ── E2 (T044): test_engine_close_teardown ─────────────────────────────────────
# Regression: Engine::stop() invokes TracerProvider::shutdown(),
# MeterProvider::shutdown(), and Logger sink flush before returning.
# FR-014 / T044 / [2k §6.6] / NO session-FSM edit.
if(TARGET opentelemetry-cpp::api)
  add_executable(test_engine_close_teardown test_engine_close_teardown.cpp)
  target_link_libraries(test_engine_close_teardown PRIVATE
    fixpp_otel
    fixpp_session
    fixpp_mock_clock
    opentelemetry-cpp::api
    opentelemetry-cpp::trace
    opentelemetry-cpp::metrics
    asio::asio
    GTest::gtest
    GTest::gtest_main
  )
  add_test(NAME e2_engine_close_teardown COMMAND test_engine_close_teardown)
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

# ── test_session_spans_full ───────────────────────────────────────────────────
# Coverage-remediation: exercises SessionSpans ctor/dtor, make_store_span,
# make_dispatch_span, session_trace_context (from_span_context), the
# to_span_context non-zero path, StoreSpan and DispatchSpan ok/error branches.
# Uses fixpp::otel::TracerProvider with tracer_factory_for_test injection.
if(TARGET opentelemetry-cpp::api)
  add_executable(test_session_spans_full test_session_spans_full.cpp)
  target_link_libraries(test_session_spans_full PRIVATE
    fixpp_otel
    opentelemetry-cpp::exporter_in_memory
    opentelemetry-cpp::trace
    GTest::gtest
    GTest::gtest_main
  )
  if(OTEL_EXPORTER_IN_MEMORY_LIB)
    target_link_libraries(test_session_spans_full PRIVATE ${OTEL_EXPORTER_IN_MEMORY_LIB})
  else()
    find_library(OTEL_EXPORTER_IN_MEMORY_LIB_2
      NAMES opentelemetry_exporter_in_memory
      HINTS ${opentelemetry-cpp_LIB_DIRS_DEBUG} ${opentelemetry-cpp_LIB_DIRS}
    )
    if(OTEL_EXPORTER_IN_MEMORY_LIB_2)
      target_link_libraries(test_session_spans_full PRIVATE ${OTEL_EXPORTER_IN_MEMORY_LIB_2})
    endif()
  endif()
  add_test(NAME ts12_session_spans_full COMMAND test_session_spans_full)
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
