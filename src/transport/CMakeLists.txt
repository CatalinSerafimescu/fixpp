# src/transport/CMakeLists.txt
# fixpp::transport — Transport interface, ASIO TCP/TLS impl [arch §4.5]

add_library(fixpp_transport INTERFACE)
add_library(fixpp::transport ALIAS fixpp_transport)

target_include_directories(fixpp_transport INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core, tls, log (interfaces only) [arch §2.3]
target_link_libraries(fixpp_transport INTERFACE
  fixpp_core
  fixpp_tls
  fixpp_log
)
