# bench/threading/CMakeLists.txt
# 007-threading-clock latency regression bench (seam 8, [2d §6.3]/§9.8; SC-004).
# T053/T056: bench_threading target + threading_baselines.json baseline.
#
# The ±5% regression gate is SOFT per [const §VIII.2].
# The cross-thread row is bench-soft per [2d §10] Q4 follow-up (never a CI gate).
# Wire the regression check via CTest (T056):
#   ctest --test-dir build/linux-clang-release -R '^bench_threading_regression'
# which runs tools/bench_compare.py against threading_baselines.json.
#
# find_package(benchmark) is inherited from the parent bench/CMakeLists.txt
# which already called it before add_subdirectory(threading).

add_executable(bench_threading bench_threading.cpp)
target_link_libraries(bench_threading PRIVATE
  fixpp_session          # fixpp_core + asio transitively
  benchmark::benchmark
  benchmark::benchmark_main
)
target_include_directories(bench_threading PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"   # for support/minimal_dictionary.hpp
)
set_target_properties(bench_threading PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/threading"
)

# Regression-check CTest entry (T056). Runs only if the baseline JSON exists;
# otherwise skips silently (the baseline is captured after the first release run).
# The test invokes tools/bench_compare.py with the committed baseline; CI
# integration is future work (see research.md D-22).
set(_bench_baseline
  "${CMAKE_SOURCE_DIR}/bench/baselines/threading/threading_baselines.json")
set(_bench_current_out
  "${CMAKE_BINARY_DIR}/bench/threading/bench_threading_current.json")

add_test(
  NAME bench_threading_regression
  COMMAND ${CMAKE_COMMAND}
    -DBENCH_BINARY=$<TARGET_FILE:bench_threading>
    -DBASELINE_JSON=${_bench_baseline}
    -DCURRENT_JSON=${_bench_current_out}
    -DTOOLS_DIR=${CMAKE_SOURCE_DIR}/tools
    -P "${CMAKE_SOURCE_DIR}/cmake/run_bench_regression.cmake"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
# Only run the regression check in release builds (benches are meaningless in
# debug). This test is SKIPPED in non-release presets by the cmake script.
set_tests_properties(bench_threading_regression PROPERTIES
  LABELS "bench;regression;threading"
)
