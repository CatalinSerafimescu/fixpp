# bench/log/CMakeLists.txt
# T030 / TS-9: Logger::enqueue() latency benchmark (non-overflow producer path).
# [2k §6.2] ceiling ≤ 50ns on non-overflow path.
# Built only when FIXPP_BUILD_BENCH=ON (gated by parent bench/CMakeLists).

find_package(benchmark CONFIG REQUIRED)

add_executable(log_enqueue_bench log_enqueue.cpp)
target_link_libraries(log_enqueue_bench PRIVATE
  fixpp_log
  benchmark::benchmark
)
set_target_properties(log_enqueue_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/log"
)
