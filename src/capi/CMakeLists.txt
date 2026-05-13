# src/capi/CMakeLists.txt
# fixpp::capi — extern "C" ABI opaque-handle layer [arch §4.10]
# The ONLY translation unit that crosses the AGPL boundary.

add_library(fixpp_capi STATIC
  capi.cpp
  decimal.cpp
  decimal_assert.cpp
)
add_library(fixpp::capi ALIAS fixpp_capi)

target_include_directories(fixpp_capi PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# capi may depend on all engine modules [arch §2.3]
target_link_libraries(fixpp_capi PUBLIC
  fixpp_core
  fixpp_session
  fixpp_wire
  fixpp_dictionary
  fixpp_transport
  fixpp_tls
  fixpp_log
  fixpp_otel
  fixpp_tap
)
