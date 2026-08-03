# src/log/CMakeLists.txt
# fixpp::log — async logger core, sink interface, default sinks [arch §4.7]
#
# Converted from INTERFACE to STATIC in slice 3b-i (T023/T025/T026):
# format_registry.cpp and logger.cpp are the first compiled translation units.
# Pattern follows src/tls/CMakeLists.txt.

add_library(fixpp_log STATIC
  format_registry.cpp
  logger.cpp
  file_sink.cpp
  syslog_sink.cpp
)
add_library(fixpp::log ALIAS fixpp_log)

target_include_directories(fixpp_log PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

# Allowed dependencies: core only [arch §2.3]
target_link_libraries(fixpp_log PUBLIC fixpp_core)

# Compile-time minimum log level (FR-010 / SC-003): propagated to every
# consumer so the `if constexpr` cutoff in the LOG macros is build-wide.
# Default set in the top-level CMakeLists (build-type-conditional).
target_compile_definitions(fixpp_log PUBLIC FIXPP_LOG_MIN_LEVEL=${FIXPP_LOG_MIN_LEVEL})

# ── fixpp_log_otlp — OtlpLogSink (separate target) ───────────────────────────
#
# ARCHITECTURE INVARIANT: OtlpLogSink depends on the OTel SDK (for
# BatchLogRecordProcessor + OtlpHttpLogRecordExporter).  It MUST NOT be
# compiled into fixpp_log (which must stay OTel-free so FileSink/SyslogSink
# users do not pull in the OTel SDK).  This separate STATIC library links
# fixpp_log + the OTel SDK; consumers that want OTLP log export add
# fixpp_log_otlp to their target_link_libraries.
#
# [arch §2.3], contracts/log-sinks.md §OtlpLogSink, T043.
if(TARGET opentelemetry-cpp::api)
  add_library(fixpp_log_otlp STATIC otlp_log_sink.cpp)
  add_library(fixpp::log_otlp ALIAS fixpp_log_otlp)

  target_include_directories(fixpp_log_otlp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )

  # Links fixpp_log (sink interface + record) + OTel SDK log components.
  target_link_libraries(fixpp_log_otlp PUBLIC
    fixpp_log
    opentelemetry-cpp::logs
    opentelemetry-cpp::exporter_otlp_http_log
    opentelemetry-cpp::otlp_recordable
  )

  # Propagate the same compile-time log level cutoff.
  target_compile_definitions(fixpp_log_otlp PUBLIC
    FIXPP_LOG_MIN_LEVEL=${FIXPP_LOG_MIN_LEVEL}
  )
endif()
