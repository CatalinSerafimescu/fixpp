# src/core/CMakeLists.txt
# fixpp::core — primitives, PMR allocators, error type, etc. [arch §4.1]
# INTERFACE library for now; promoted to OBJECT in Phase 4 when real TUs land.
# Phase 3 default — revisit if core needs compiled translation units sooner.

add_library(fixpp_core INTERFACE)
add_library(fixpp::core ALIAS fixpp_core)

target_include_directories(fixpp_core INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# core has no internal project dependencies — it is the leaf [arch §2.2]
