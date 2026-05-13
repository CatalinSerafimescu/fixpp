# src/capi/CMakeLists.txt
# fixpp::capi — extern "C" ABI opaque-handle layer [arch §4.10]
# The ONLY translation unit that crosses the AGPL boundary.
#
# Two flavours of the same TU set:
#  * fixpp_capi (STATIC) — the shipped library; legal-isolation boundary.
#  * fixpp_capi_shared (SHARED, BUILD_TESTING only) — required by the Python
#    Decimal oracle test (seam #8) which uses ctypes.CDLL — Gate B P1 #4.
# Shared object source is kept in an OBJECT library to avoid source duplication.

add_library(fixpp_capi_objects OBJECT
  capi.cpp
  decimal.cpp
  decimal_assert.cpp
)
set_property(TARGET fixpp_capi_objects PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(fixpp_capi_objects PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(fixpp_capi_objects PUBLIC
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

add_library(fixpp_capi STATIC)
add_library(fixpp::capi ALIAS fixpp_capi)
target_link_libraries(fixpp_capi PUBLIC fixpp_capi_objects)

if(FIXPP_BUILD_TESTS)
  add_library(fixpp_capi_shared SHARED)
  target_link_libraries(fixpp_capi_shared PUBLIC fixpp_capi_objects)
  set_target_properties(fixpp_capi_shared PROPERTIES OUTPUT_NAME fixpp_capi)
  # Only export fixpp_* symbols; hide all C++ internals (decimal_traits, etc.)
  # per [const §X.2]. Version script is more reliable than -fvisibility=hidden
  # across object libraries with transitive C++ deps.
  target_link_options(fixpp_capi_shared PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/fixpp_capi.map")
endif()
