# src/dictionary/CMakeLists.txt
# fixpp::dictionary — runtime dictionary, XML loader, codegen output [arch §4.2]
#
# 002-dictionary-xml-loader: switches the target from INTERFACE to STATIC,
# ships `xml_loader.cpp` + `dictionary.cpp`, and links pugixml (research.md
# D-1 / D-15 — pugixml.hpp is consumed only in xml_loader.cpp; not
# transitively visible from public headers).

find_package(pugixml CONFIG REQUIRED)

add_library(fixpp_dictionary STATIC
  xml_loader.cpp
  # 074-orchestra-native-reader: native FIX Orchestra (fixr:repository) reader,
  # sibling to xml_loader.cpp. Consumes pugixml PRIVATELY (same as xml_loader).
  orchestra_loader.cpp
  dictionary.cpp
  # 003-dictionary-codegen (RC#1): the 003-owned upstream surface bodies.
  # field_traits.cpp is a bridge file (arch §2.4 v0.3 — #includes the
  # vendored frozen wire stub header; NOT a cyclic dictionary→wire link
  # edge; check_layers.py BRIDGE_SOURCE_FILES exemption, T049).
  version_profile.cpp
  version_registry.cpp
  field_traits.cpp
  # T037: dict::reify() + owning_message_handle R6 stub bodies ([2c §4.8]).
  # Bridge file (arch §2.4 v0.3 — #includes the vendored frozen wire stub
  # header; NOT a cyclic dictionary→wire link edge; BRIDGE_SOURCE_FILES).
  reify.cpp
)
add_library(fixpp::dictionary ALIAS fixpp_dictionary)

target_include_directories(fixpp_dictionary PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependency: core only [arch §2.3], PLUS the 2b cutover link edge:
# after 004 T028 the dict::reify / dict::field_traits bridge TUs bind the
# REAL wire surface (message_view_contract.hpp re-exports parser.hpp), whose
# OffsetTable/Framer bodies live in fixpp_wire. This is the intended
# post-cutover topology (003 reify depends on 004 wire). fixpp_wire <->
# fixpp_dictionary is a permitted static-archive cycle (CMake repeats the
# archives); the MODULE-layer cycle is exempted for the bridge TUs via
# check_layers.py BRIDGE_SOURCE_FILES / BRIDGE_EXEMPT_INCLUDES.
target_link_libraries(fixpp_dictionary
  PUBLIC fixpp_core fixpp_wire
  PRIVATE pugixml::pugixml
)

# ── 057-behavioral-reify-unblock: reify dispatch bridge ──────────────────────
# Lets shipped reify.cpp reach the build-tree-generated dispatch switches
# WITHOUT any shipped src/** file including a build-tree header (NFR-003-8).
# The bridge TU is materialized at configure time from a checked-in template
# into ${CMAKE_BINARY_DIR}/_codegen/ (OUTSIDE the check_layers.py src/** scan
# by design) and is the SOLE #includer of the _dispatch/*.hpp headers.
#
# Forward-reference note (intentional, generate-phase-resolved — NOT a bug):
# fixpp::dict::dispatch / fixpp_codegen_generate are defined later by
# include(cmake/Codegen.cmake) (CMakeLists.txt:190), and fixpp_wire by the
# later src/wire subdir — fixpp_dictionary already forward-links fixpp_wire and
# builds today. Do NOT "fix" this ordering by restructuring CMake.
#
# Bootstrap guard: the codegen bootstrap sub-configure (Codegen.cmake spawns a
# nested `-S .` build with FIXPP_CODEGEN_BOOTSTRAP_RUNNING=ON to build just the
# fixpp-codegen host tool) makes Codegen.cmake early-return WITHOUT declaring
# fixpp::dict::dispatch / fixpp_codegen_generate, so those forward-refs are
# unresolvable there. The bootstrap only needs the tool, never the runtime
# dispatch bridge, so skip the whole bridge block under that guard. Without this
# a FRESH build dir (no pre-existing codegen tool → bootstrap runs) fails to
# configure on every preset.
if(NOT FIXPP_CODEGEN_BOOTSTRAP_RUNNING)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_codegen")
  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/templates/reify_dispatch_bridge.cpp.in"
    "${CMAKE_BINARY_DIR}/_codegen/reify_dispatch_bridge.cpp"
    @ONLY
  )
  add_library(fixpp_dict_dispatch_bridge STATIC
    "${CMAKE_BINARY_DIR}/_codegen/reify_dispatch_bridge.cpp"
  )
  target_include_directories(fixpp_dict_dispatch_bridge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"  # private reify_dispatch_bridge.hpp (decl/def match)
  )
  # The _codegen/include dir (for <fixpp/_dispatch/*.hpp>) rides on
  # fixpp::dict::dispatch — keep it on THIS target ONLY, never broaden
  # fixpp_dictionary or every dict TU gains build-tree reach.
  target_link_libraries(fixpp_dict_dispatch_bridge
    PRIVATE fixpp::dict::dispatch fixpp_wire fixpp_dictionary
  )
  add_dependencies(fixpp_dict_dispatch_bridge fixpp_codegen_generate)

  # Back-edge declared BOTH ways (mirror the fixpp_wire <-> fixpp_dictionary cycle
  # above; NOT left to TU co-location luck — research D-1): reify.cpp
  # (fixpp_dictionary) calls the bridge's reify_dispatch_*; the bridge's generated
  # arms call detail::owning_message_handle_from_frame (fixpp_dictionary).
  target_link_libraries(fixpp_dictionary PUBLIC fixpp_dict_dispatch_bridge)
endif()
