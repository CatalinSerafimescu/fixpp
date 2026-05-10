# src/service/CMakeLists.txt
# fixpp::service — gRPC control plane + iceoryx2 data plane [arch §4.11]
# The service binary (fixppd) consumes the engine through the C ABI ONLY.
# [arch §8] — never includes engine internal headers.
# Phase 3 default — revisit when FIXPP_BUILD_SERVICE becomes functional.

add_library(fixpp_service INTERFACE)
add_library(fixpp::service ALIAS fixpp_service)

target_include_directories(fixpp_service INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# service depends on capi only — never on C++ engine internals [arch §2.3]
target_link_libraries(fixpp_service INTERFACE fixpp_capi)
