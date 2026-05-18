# bench/sync/CMakeLists.txt — 006-async-mutex latency benchmarks (T003/T031).
# Seam #1 uncontended (≤ 20–25 ns warm) + seam #2 contended (≤ 80 ns) /
# unlock uncontended (≤ 15 ns) / #12 dispatch-vs-post — [2f §6.3] ceilings,
# ±5% vs bench/baselines/sync/async_mutex_baselines.json ([const §VIII.1/2]).
# Built only when FIXPP_BUILD_BENCH=ON (gated by parent bench/CMakeLists).
# Bodies are authored in Phase 3 T031; Phase 1 ships compiling placeholders.

find_package(benchmark CONFIG REQUIRED)

add_executable(bench_async_mutex_uncontended bench_async_mutex_uncontended.cpp)
target_link_libraries(bench_async_mutex_uncontended PRIVATE
  fixpp_sync
  benchmark::benchmark
  benchmark::benchmark_main
)
set_target_properties(bench_async_mutex_uncontended PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/sync"
)

add_executable(bench_async_mutex_contended bench_async_mutex_contended.cpp)
target_link_libraries(bench_async_mutex_contended PRIVATE
  fixpp_sync
  benchmark::benchmark
  benchmark::benchmark_main
)
set_target_properties(bench_async_mutex_contended PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/sync"
)
