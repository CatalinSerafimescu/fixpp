# bench/codegen/CMakeLists.txt
# T045 — NFR-003-1 typed-accessor latency harness (seam #3).
# T046 — NFR-003-2 compile-time ceiling bench (seam #2).
#
# Built only when FIXPP_BUILD_BENCH=ON (gated by the parent CMakeLists).
# Wired into the Tier-1 release preset bench run per quickstart.md §4.

find_package(benchmark CONFIG REQUIRED)

# ── T045: typed_accessor_bench ────────────────────────────────────────────────
# Google Benchmark harness for NFR-003-1 (string/int/char ≤20 ns;
# price(mr) decimal ≤75 ns; field_value ≤25 ns).
#
# R6 DEFERRED: the frozen wire stub returns field-absent on all get<>() calls,
# so these benchmarks measure accessor dispatch overhead only. Real NFR-003-1
# enforcement requires 2b (see typed_accessor_bench.cpp: FIXPP_R6_BENCH_DEFERRED).
add_executable(typed_accessor_bench typed_accessor_bench.cpp)
add_dependencies(typed_accessor_bench fixpp_codegen_generate)
target_link_libraries(typed_accessor_bench PRIVATE
    fixpp_dictionary
    benchmark::benchmark
    benchmark::benchmark_main
)
target_include_directories(typed_accessor_bench PRIVATE
    "${CMAKE_BINARY_DIR}/_codegen/include"
)
set_target_properties(typed_accessor_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/codegen"
)

# ── T046: compile_time_bench ──────────────────────────────────────────────────
# Script-based compile-time ceiling measurement (NFR-003-2 / seam #2).
# Measures real `clang++ -std=c++23 -fsyntax-only` wall time of including
# each generated Messages.hpp + Reify.hpp TU.
#
# Single-version ceiling ≤ 3 s (load-bearing).
# All-versions ceiling ≤ 15 s soft (env FIXPP_BENCH_ALL_VERSIONS_CEILING).
#
# Known finding (T046): v50sp2/Messages.hpp (~120 kLOC) exceeds the ≤3 s
# load-bearing ceiling in syntax-only mode (~8-9 s on typical hardware).
# See bench/codegen/compile_time_bench/FINDINGS.md for the documented record.
add_subdirectory(compile_time_bench)
