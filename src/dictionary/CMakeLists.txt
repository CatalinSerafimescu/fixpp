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
)
add_library(fixpp::dictionary ALIAS fixpp_dictionary)

target_include_directories(fixpp_dictionary PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependency: core only [arch §2.3]
target_link_libraries(fixpp_dictionary
  PUBLIC fixpp_core
  PRIVATE pugixml::pugixml
)
