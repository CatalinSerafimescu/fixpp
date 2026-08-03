# src/tap/CMakeLists.txt
# fixpp::tap — session tap ring buffer, iceoryx2 publisher, sync-callback [arch §4.9]

add_library(fixpp_tap INTERFACE)
add_library(fixpp::tap ALIAS fixpp_tap)

target_include_directories(fixpp_tap INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

# Allowed dependencies: core, wire (read-only views), log (interface) [arch §2.3]
target_link_libraries(fixpp_tap INTERFACE
  fixpp_core
  fixpp_wire
  fixpp_log
)
