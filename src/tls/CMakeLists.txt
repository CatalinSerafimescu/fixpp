# src/tls/CMakeLists.txt
# fixpp::tls — cert_source, Pinset, CipherPolicy, SecurityProfile [arch §4.6]

add_library(fixpp_tls INTERFACE)
add_library(fixpp::tls ALIAS fixpp_tls)

target_include_directories(fixpp_tls INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core only [arch §2.3]
target_link_libraries(fixpp_tls INTERFACE fixpp_core)
