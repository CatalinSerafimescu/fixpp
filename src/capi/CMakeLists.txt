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
  fixpp_tap
)
# 046-atomic-shared-ptr T014 (FR-011a): link fixpp_otel only when OTel is ON.
if(FIXPP_BUILD_OTEL)
  target_link_libraries(fixpp_capi_objects PUBLIC fixpp_otel)
endif()

add_library(fixpp_capi STATIC)
add_library(fixpp::capi ALIAS fixpp_capi)
target_link_libraries(fixpp_capi PUBLIC fixpp_capi_objects)

if(FIXPP_BUILD_TESTS)
  add_library(fixpp_capi_shared SHARED)
  target_link_libraries(fixpp_capi_shared PUBLIC fixpp_capi_objects)
  # OUTPUT_NAME makes the runtime artifact libfixpp_capi.so / fixpp_capi.dll.
  # ARCHIVE_OUTPUT_NAME renames ONLY the Windows import library so it does not
  # collide with the static fixpp_capi target's fixpp_capi.lib ("multiple rules
  # generate lib/fixpp_capi.lib"). No effect on POSIX (a SHARED lib has no
  # archive output there), so static→libfixpp_capi.a / shared→libfixpp_capi.so
  # are unchanged on Linux.
  set_target_properties(fixpp_capi_shared PROPERTIES
    OUTPUT_NAME fixpp_capi
    ARCHIVE_OUTPUT_NAME fixpp_capi_shared)
  # Only export fixpp_* symbols; hide all C++ internals (decimal_traits, etc.)
  # per [const §X.2]. Version script is more reliable than -fvisibility=hidden
  # across object libraries with transitive C++ deps. GNU-ld/lld only — MSVC's
  # link.exe rejects -Wl,--version-script (Windows symbol export would use a .def
  # / __declspec; the shared lib is test-only and has no Windows consumer yet —
  # the Python wheel is Linux-only, PY-005).
  if(NOT WIN32)
    target_link_options(fixpp_capi_shared PRIVATE
      "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/fixpp_capi.map")
  endif()
endif()
