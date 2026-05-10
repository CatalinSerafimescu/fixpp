# bench/CMakeLists.txt
# Built only when FIXPP_BUILD_BENCH=ON per cmake/ProjectOptions.cmake.
# Phase 3 default — revisit as real benchmarks land in Phase 4+.

find_package(benchmark CONFIG REQUIRED)

add_executable(placeholder_bench
  src/placeholder_bench.cpp
)

target_link_libraries(placeholder_bench PRIVATE
  benchmark::benchmark
  benchmark::benchmark_main
)

set_target_properties(placeholder_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
