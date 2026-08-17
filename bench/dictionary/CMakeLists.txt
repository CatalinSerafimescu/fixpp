# bench/dictionary/CMakeLists.txt
# NFR-002-1 — Google Benchmark harness for fixpp::dict::XmlLoader.
# NFR-003-3 — T047: dict::reify / reify_as latency harness (seam #5/#6).
# Built only when FIXPP_BUILD_BENCH=ON (gated by the parent CMakeLists).

find_package(benchmark CONFIG REQUIRED)

set(FIXPP_DICT_DATA_DIR "${CMAKE_SOURCE_DIR}/dictionaries")

add_executable(xml_loader_bench xml_loader_bench.cpp)
target_link_libraries(xml_loader_bench PRIVATE
  fixpp_dictionary
  benchmark::benchmark
  benchmark::benchmark_main
)
target_compile_definitions(xml_loader_bench PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
)
set_target_properties(xml_loader_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/dictionary"
)

# ── #263: xml_loader_arena_probe ────────────────────────────────────────────
# Diagnostic only (no benchmark::, no ctest label): reports the loader's byte
# footprint per dictionary and whether the 4 MiB NFR-002-1 arena above
# overflows into its upstream.
add_executable(xml_loader_arena_probe xml_loader_arena_probe.cpp)
target_link_libraries(xml_loader_arena_probe PRIVATE fixpp_dictionary)
target_compile_definitions(xml_loader_arena_probe PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
)
set_target_properties(xml_loader_arena_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/dictionary"
)

# ── T011: table_view_footprint_bench ────────────────────────────────────────
# 075-live-wire-enum-validation SC-006 pre-change baseline: as_table_view()
# build time + sizeof(table_view) on FIX50SP2 (largest) and FIX44 (mid-size).
add_executable(table_view_footprint_bench table_view_footprint_bench.cpp)
target_link_libraries(table_view_footprint_bench PRIVATE
  fixpp_dictionary
  benchmark::benchmark
  benchmark::benchmark_main
)
target_compile_definitions(table_view_footprint_bench PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
)
set_target_properties(table_view_footprint_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/dictionary"
)

# ── T047: reify_bench ────────────────────────────────────────────────────────
# NFR-003-3 dict::reify_as / dict::reify latency harness (seam #5/#6).
# Includes generated Reify.hpp and _dispatch/ headers (build-tree, AC-C4).
#
# R6 DEFERRED: frozen wire stub returns field-absent; timings measure dispatch
# overhead only. Real NFR-003-3 numbers require 2b (see reify_bench.cpp).
if(TARGET fixpp_codegen_generate)
  set(_REIFY_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

  add_executable(reify_bench reify_bench.cpp)
  add_dependencies(reify_bench fixpp_codegen_generate)
  target_link_libraries(reify_bench PRIVATE
    fixpp_dictionary
    benchmark::benchmark
    benchmark::benchmark_main
  )
  target_include_directories(reify_bench PRIVATE
    "${_REIFY_CODEGEN_INC}"
  )
  set_target_properties(reify_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/dictionary"
  )
else()
  message(STATUS
    "bench/dictionary: fixpp_codegen_generate not available; skipping reify_bench (T047)")
endif()
