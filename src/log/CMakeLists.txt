# src/log/CMakeLists.txt
# fixpp::log — async logger core, sink interface, default sinks [arch §4.7]

add_library(fixpp_log INTERFACE)
add_library(fixpp::log ALIAS fixpp_log)

target_include_directories(fixpp_log INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core only [arch §2.3]
target_link_libraries(fixpp_log INTERFACE fixpp_core)

# Compile-time minimum log level (FR-010 / SC-003): propagated to every
# consumer so the `if constexpr` cutoff in the LOG macros is build-wide.
# Default set in the top-level CMakeLists (build-type-conditional).
target_compile_definitions(fixpp_log INTERFACE FIXPP_LOG_MIN_LEVEL=${FIXPP_LOG_MIN_LEVEL})
