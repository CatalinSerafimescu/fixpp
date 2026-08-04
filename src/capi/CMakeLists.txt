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

# 086 (issue #218) — the C-ABI include isolation [arch §7.4].
#
# PRIVATE, not PUBLIC. This one keyword is the entire mechanism: CMake records
# the edge as $<LINK_ONLY:fixpp_capi_objects> in fixpp_capi's INTERFACE_LINK_
# LIBRARIES, so the archive still absorbs every object and every symbol still
# resolves — but the objects target's usage requirements, INCLUDE DIRECTORIES
# FIRST AMONG THEM, are no longer inherited by a consumer. Measured, not
# inferred: research.md R1 (the objects are still linked) and R3 (the generated
# targets file).
#
# This is the line issue #218 is about. fixpp_capi_objects exposes
# ${_IMPORT_PREFIX}/include — the WHOLE tree — so with PUBLIC here a consumer
# that linked only fixpp::capi could reach every <fixpp/...> engine header. The
# defect was never in fixpp_capi's own properties, which is why 084's inspection
# of them reported it clean; it was in this transitive edge.
#
# ⚠️ $<LINK_ONLY:> withholds COMPILE_DEFINITIONS, COMPILE_OPTIONS,
# COMPILE_FEATURES and SYSTEM_INCLUDE_DIRECTORIES too, not just the include path.
# That is intended and enumerated (contracts/include-interface.md C-3): a C-ABI
# consumer stops receiving at least FIXPP_LOG_MIN_LEVEL and ASIO_STANDALONE,
# neither of which any C-ABI header consumes. tests/consumer/ measures the
# effective set at a real consumer target so a FUTURE requirement joining that
# set cannot slip through silently.
#
# ⚠️ THE `$<BUILD_INTERFACE:>` LINE IS NOT DECORATION — MEASURED, 2026-08-04.
# `PRIVATE` alone BREAKS THE IN-TREE BUILD, and the bundle predicted otherwise:
#
#     src/capi/capi_internal.hpp:23:10: fatal error:
#       'asio/executor_work_guard.hpp' file not found
#     (building tests/capi/capi_group_delimiter_ctx_test.cpp)
#
# In-tree targets that link fixpp_capi — tests/capi/** among them — were
# inheriting fixpp_capi_objects' ENTIRE usage-requirement set through the PUBLIC
# edge, including the CONAN dependencies' include directories (asio here). Adding
# $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include> below restores fixpp's OWN
# headers and nothing else, so it does not cover this: the missing header is a
# third party's, reached only through the target graph.
#
# So the build interface keeps the full PUBLIC edge and only the INSTALL
# interface is narrowed. In-tree link behaviour is byte-for-byte what it was
# (invariant C-4), while the installed fixpp::capi carries exactly
# $<LINK_ONLY:fixpp::capi_objects> — install(EXPORT) evaluates $<BUILD_INTERFACE:>
# to empty, so the second entry does not reach the exported target.
#
# The two entries do not double-link: this is the arrangement in force before
# this feature, where consumers already saw both the archive and the object
# files, and a static archive contributes only members not already defined.
target_link_libraries(fixpp_capi
  PRIVATE fixpp_capi_objects
  PUBLIC  "$<BUILD_INTERFACE:fixpp_capi_objects>")

# The isolated root the consumer DOES get. BUILD stays the whole tree so nothing
# in-tree changes (CMakeLists.txt:234's directory-scoped include_directories()
# makes everything reachable in-tree by design, and this feature does not touch
# that); only the INSTALL interface is narrowed.
#
# The destination is include/capi and the headers land at include/capi/fix/**,
# so <fix/c_api.h> is spelled EXACTLY as before. §7.4:503's literal prescription
# — INTERFACE_INCLUDE_DIRECTORIES = include/fix/ — cannot work: every C-ABI
# header carries the `fix/` component in its own spelling, in both angle-bracket
# and quoted form (include/fix/c_api.h:40-48), so that root would resolve
# <fix/c_api.h> to include/fix/fix/c_api.h. FR-013 rewrites §7.4 accordingly.
target_include_directories(fixpp_capi PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/capi>"
)

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
