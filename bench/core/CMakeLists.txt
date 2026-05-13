# bench/core/CMakeLists.txt — decimal latency benchmarks (seam #5)

find_package(benchmark CONFIG REQUIRED)

add_executable(decimal_bench decimal_bench.cpp)
target_link_libraries(decimal_bench PRIVATE
  fixpp_core
  benchmark::benchmark
  benchmark::benchmark_main
)
set_target_properties(decimal_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/core"
)
