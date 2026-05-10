# src/dictionary/CMakeLists.txt
# fixpp::dictionary — runtime dictionary, XML loader, codegen output [arch §4.2]

add_library(fixpp_dictionary INTERFACE)
add_library(fixpp::dictionary ALIAS fixpp_dictionary)

target_include_directories(fixpp_dictionary INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependency: core only [arch §2.3]
target_link_libraries(fixpp_dictionary INTERFACE fixpp_core)
