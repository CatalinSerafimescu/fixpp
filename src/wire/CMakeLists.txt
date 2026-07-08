# src/wire/CMakeLists.txt
# fixpp::wire — View flyweight base, Parser<Mode>, OffsetTable, Framer,
# Writer, Validator [arch §4.3] / 004-wire-codec.
#
# Header-mostly per OSS-006: view / parser / field_view / group_view /
# unknown_fields / errors are header-only; framer / offset_table / validator /
# writer are split out-of-line for compile-time + the dictionary_driven_-
# validator vtable. No new Conan row ([const §III.2]) — links fixpp::core
# and the dict::table_view value contract only.

add_library(fixpp_wire STATIC
  body_builder.cpp
  framer.cpp
  offset_table.cpp
  validator.cpp
  writer.cpp
)
add_library(fixpp::wire ALIAS fixpp_wire)

target_include_directories(fixpp_wire PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core, dictionary [arch §2.3]. (dict::table_view is a
# value-typed metadata contract owned by 2c; wire forward-declares it — no
# virtual wire->dict runtime edge.)
target_link_libraries(fixpp_wire PUBLIC
  fixpp_core
  fixpp_dictionary
)
