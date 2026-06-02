# bench/log/CMakeLists.txt
# T030 / TS-9: Logger::enqueue() latency benchmark (non-overflow producer path).
# [2k §6.2] ceiling ≤ 50ns on non-overflow path.
# Built only when FIXPP_BUILD_BENCH=ON (gated by parent bench/CMakeLists).
#
# T045 / TS-13: Own MPSC ring spike — 4 producers, 10M records, 3 fill rates,
# p99/p999 + Criterion-A mallocnesia zero-alloc gate.
# The quill comparison is gated behind FIXPP_LOG_SPIKE_QUILL=ON and requires
# the quill Conan dep; the default build does NOT need quill.

find_package(benchmark CONFIG REQUIRED)

add_executable(log_enqueue_bench log_enqueue.cpp)
target_link_libraries(log_enqueue_bench PRIVATE
  fixpp_log
  benchmark::benchmark
)
set_target_properties(log_enqueue_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/log"
)

# ── T045 / TS-13: log_spike ───────────────────────────────────────────────────
# Stand-alone spike binary (no Google Benchmark dep — produces its own output).
add_executable(log_spike log_spike.cpp)
target_link_libraries(log_spike PRIVATE fixpp_log)
set_target_properties(log_spike PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/log"
)

# Quill comparison (optional — requires FIXPP_LOG_SPIKE_QUILL=ON + quill Conan dep).
if(FIXPP_LOG_SPIKE_QUILL)
  find_package(quill CONFIG REQUIRED)
  target_compile_definitions(log_spike PRIVATE FIXPP_LOG_SPIKE_QUILL)
  target_link_libraries(log_spike PRIVATE quill::quill)
endif()
