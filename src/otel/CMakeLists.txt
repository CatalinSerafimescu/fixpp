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
