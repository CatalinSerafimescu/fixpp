# src/wire/CMakeLists.txt
# fixpp::wire — parser, offset table, writer, validator, framer [arch §4.3]

add_library(fixpp_wire INTERFACE)
add_library(fixpp::wire ALIAS fixpp_wire)

target_include_directories(fixpp_wire INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core, dictionary [arch §2.3]
target_link_libraries(fixpp_wire INTERFACE
  fixpp_core
  fixpp_dictionary
)
