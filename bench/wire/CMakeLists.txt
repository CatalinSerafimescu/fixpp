# bench/wire/CMakeLists.txt
# T048 — [2b §6.6] Wire latency benchmarks (seam #5).
# T049 — [arch §11 row 1] OffsetTable footprint spike (seam #6, SC-008).
# Built only when FIXPP_BUILD_BENCH=ON (gated by the parent bench/CMakeLists).

find_package(benchmark CONFIG REQUIRED)

# Shared include path for the test-only frame_view_factory (friend of frame_view).
# The factory lives in tests/support/; the bench uses it to mint frame_views
# without the production Framer algorithm (mirrors the T008 design).
set(_WIRE_TEST_SUPPORT_INC "${CMAKE_SOURCE_DIR}/tests")

# ── T048: framer_bench ────────────────────────────────────────────────────────
add_executable(framer_bench framer_bench.cpp)
target_link_libraries(framer_bench PRIVATE
  fixpp_wire
  benchmark::benchmark
  benchmark::benchmark_main
)
target_include_directories(framer_bench PRIVATE "${_WIRE_TEST_SUPPORT_INC}")
set_target_properties(framer_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/wire"
)

# ── T048: parser_bench ────────────────────────────────────────────────────────
# [2b §6.6] Parser<Iter/Index>::parse ceilings: ≤80 ns (Iter,20tag),
# ≤400 ns (Index,20tag), ≤4 µs (Index,200tag).
# hffix comparison: hffix is NOT wired in — D-14 "measured-not-blocker".
add_executable(parser_bench parser_bench.cpp)
target_link_libraries(parser_bench PRIVATE
  fixpp_wire
  benchmark::benchmark
  benchmark::benchmark_main
)
target_include_directories(parser_bench PRIVATE "${_WIRE_TEST_SUPPORT_INC}")
set_target_properties(parser_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/wire"
)

# ── T048: offset_table_bench ──────────────────────────────────────────────────
# [2b §6.6] OffsetTable::find ceiling: ≤15 ns (32-slot hash, after build).
add_executable(offset_table_bench offset_table_bench.cpp)
target_link_libraries(offset_table_bench PRIVATE
  fixpp_wire
  benchmark::benchmark
  benchmark::benchmark_main
)
target_include_directories(offset_table_bench PRIVATE "${_WIRE_TEST_SUPPORT_INC}")
set_target_properties(offset_table_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/wire"
)

# ── T048: writer_bench ────────────────────────────────────────────────────────
# [2b §6.6] Writer::commit ceilings: ≤80 ns (20tag), ≤800 ns (200tag).
add_executable(writer_bench writer_bench.cpp)
target_link_libraries(writer_bench PRIVATE
  fixpp_wire
  benchmark::benchmark
  benchmark::benchmark_main
)
set_target_properties(writer_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/wire"
)

# ── T049: offset_table_footprint_bench ───────────────────────────────────────
# [arch §11 row 1] / [2b §10 Q1] eager-vs-lazy footprint spike (SC-008).
# Over-4096-occ corpora use a LOCAL raised cap (kFootprintBenchMaxOcc=32768)
# defined inside the bench source — the shipped default_max_offset_entries
# is NEVER modified here.
add_executable(offset_table_footprint_bench offset_table_footprint_bench.cpp)
target_link_libraries(offset_table_footprint_bench PRIVATE
  fixpp_wire
  benchmark::benchmark
  benchmark::benchmark_main
)
target_include_directories(offset_table_footprint_bench PRIVATE
  "${_WIRE_TEST_SUPPORT_INC}"
)
set_target_properties(offset_table_footprint_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/wire"
)

# ── T052: check_alive_bench ───────────────────────────────────────────────────
# D-8 [2b §10 Q3] — View::check_alive() generation-counter cost on a 200-tag
# read loop. Debug build (NDEBUG off) measures the real slot lookup cost;
# release shows a constexpr no-op. Verdict recorded in .specify/decisions/
# 004-wire-codec-verify.md §D-8.
add_executable(check_alive_bench check_alive_bench.cpp)
target_link_libraries(check_alive_bench PRIVATE
  fixpp_wire
  benchmark::benchmark
  benchmark::benchmark_main
)
set_target_properties(check_alive_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/wire"
)

# ── 075 Gate B r1: validator_bench (Article VIII §3) ─────────────────────────
# [2b §6.6] Validator::validate ceiling: ≤ 200 ns. Uses a REAL FIX44.xml
# table_view (not mock_dict_table.hpp) to exercise the enum_valid() path.
set(FIXPP_DICT_DATA_DIR "${CMAKE_SOURCE_DIR}/dictionaries")

add_executable(validator_bench validator_bench.cpp)
target_link_libraries(validator_bench PRIVATE
  fixpp_wire
  fixpp_dictionary
  benchmark::benchmark
  benchmark::benchmark_main
)
target_compile_definitions(validator_bench PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
)
set_target_properties(validator_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/wire"
)
