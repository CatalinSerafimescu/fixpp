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
  version.cpp
  error.cpp
  config.cpp      # 050 Feature B (CA-005): opaque engine/session config builders
  engine.cpp      # 050 Feature B (CA-005/007): engine lifecycle + CapiApplication trampoline
  session.cpp     # 050 Feature B (CA-005/006/007): session open/close/is_established/send/register_callback
  dictionary.cpp  # 052 US1: fixpp_dict_load_from_xml + fixpp_dict_destroy (FR-001..003)
  message_read.cpp  # 051 Feature C (CA-008/CA-010-read): inbound field + group read accessors
  message_write.cpp # 051 Feature C (CA-009/CA-010-write): outbound construct/populate/commit + group builder
)
set_property(TARGET fixpp_capi_objects PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(fixpp_capi_objects PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
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
  # link.exe rejects -Wl,--version-script.
  if(WIN32)
    # MSVC has no version-script equivalent; auto-generate a .def from all object
    # symbols so the C ABI is exported. The shared lib is test-only (BUILD_TESTS)
    # and exists solely for the Python ctypes oracle (tests/oracle) to dlopen and
    # resolve fixpp_* — there is no shipped Windows consumer, so the §X.2
    # "only fixpp_*" surface restriction (enforced on POSIX via the version
    # script) does not apply to this artifact.
    set_target_properties(fixpp_capi_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  else()
    target_link_options(fixpp_capi_shared PRIVATE
      "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/fixpp_capi.map")
  endif()
endif()
