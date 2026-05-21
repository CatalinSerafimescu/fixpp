# bench/session/CMakeLists.txt
# 008-message-store latency benches per plan.md Technical-Context ceilings
# table ([2e §6.6]); baselines under bench/baselines/session/.
# CI fails on > 5% regression for MemoryStore rows / > 2× regression for
# FileStore disk-bound rows ([const §VIII.2]).
#
# Bench targets are added red-first as the impl tasks land (T049).
find_package(benchmark CONFIG REQUIRED)

function(add_store_bench name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE
    fixpp_session
    benchmark::benchmark
    benchmark::benchmark_main
  )
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
endfunction()

# ── 008-message-store bench registrations (T049 — bench_memory_store,
#    bench_file_store) ────────────────────────────────────────────────────────
add_store_bench(bench_memory_store bench_memory_store.cpp)
add_store_bench(bench_file_store   bench_file_store.cpp)
