# src/otel/CMakeLists.txt
# fixpp::otel — trace/metric/log span helpers, OTLP+Prometheus exporter [arch §4.8]
#
# Converted from INTERFACE to STATIC in 017 Phase 5 slice 5a (T040/T041):
# providers.cpp and session_spans.cpp are the first compiled translation units.
# Pattern follows src/log/CMakeLists.txt.

add_library(fixpp_otel STATIC
  providers.cpp
  session_spans.cpp
)
add_library(fixpp::otel ALIAS fixpp_otel)

target_include_directories(fixpp_otel PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core, log (sink interface) [arch §2.3]
# asio::asio is added here because core/trace_context.hpp (included via
# otel/trace_context.hpp) transitively pulls asio/awaitable headers.
# The otel module itself does not use asio; this is a transitive include-edge
# dependency only.
target_link_libraries(fixpp_otel PUBLIC
  fixpp_core
  fixpp_log
)
if(TARGET asio::asio)
  target_link_libraries(fixpp_otel PUBLIC asio::asio)
endif()

# OTel SDK link wiring (017-log-otel FR-023).
# Guarded by `if(TARGET ...)` so the library still configures when the
# toolchain is absent.  Exact target names confirmed against the generated
# opentelemetry-cpp config at the T005 configure step.
if(TARGET opentelemetry-cpp::api)
  target_link_libraries(fixpp_otel PUBLIC
    opentelemetry-cpp::api
    opentelemetry-cpp::trace
    opentelemetry-cpp::metrics
    opentelemetry-cpp::logs
    opentelemetry-cpp::exporter_otlp_http
    opentelemetry-cpp::exporter_prometheus
  )
endif()
