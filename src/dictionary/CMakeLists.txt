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
