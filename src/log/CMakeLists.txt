# src/log/CMakeLists.txt
# fixpp::log — async logger core, sink interface, default sinks [arch §4.7]

add_library(fixpp_log INTERFACE)
add_library(fixpp::log ALIAS fixpp_log)

target_include_directories(fixpp_log INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core only [arch §2.3]
target_link_libraries(fixpp_log INTERFACE fixpp_core)
