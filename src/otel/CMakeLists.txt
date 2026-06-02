# src/otel/CMakeLists.txt
# fixpp::otel — trace/metric/log span helpers, OTLP+Prometheus exporter [arch §4.8]

add_library(fixpp_otel INTERFACE)
add_library(fixpp::otel ALIAS fixpp_otel)

target_include_directories(fixpp_otel INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core, log (sink interface) [arch §2.3]
target_link_libraries(fixpp_otel INTERFACE
  fixpp_core
  fixpp_log
)

# OTel SDK link wiring (017-log-otel FR-023). The opentelemetry-cpp::* targets
# come from the Conan CMakeDeps package (find_package in the top-level
# CMakeLists). Guarded by `if(TARGET ...)` so the INTERFACE skeleton still
# configures before the toolchain is regenerated; the concrete per-source link
# (and the move from INTERFACE to a compiled library) lands with the otel
# sources in Phase 5 / US3. Exact exporter target names are confirmed against
# the generated opentelemetry-cpp config at the T005 configure step.
if(TARGET opentelemetry-cpp::api)
  target_link_libraries(fixpp_otel INTERFACE
    opentelemetry-cpp::api
    opentelemetry-cpp::trace
    opentelemetry-cpp::metrics
    opentelemetry-cpp::logs
    opentelemetry-cpp::exporter_otlp_http
    opentelemetry-cpp::exporter_prometheus
  )
endif()
