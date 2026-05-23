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

# ── 005-session-establishment-fsm T061 (Phase 8) bench registrations ────────
# Tier-1 latency benches against the plan.md Technical-Context ceilings
# ([2e §6.6] / [2d §6.3]). ±5% regression gate per [const §VIII.2]; baselines
# under bench/baselines/session/. See each .cpp header for scope/deferral.
add_store_bench(fsm_bench       fsm_bench.cpp)
add_store_bench(seqnum_bench    seqnum_bench.cpp)
add_store_bench(fix_time_bench  fix_time_bench.cpp)
add_store_bench(heartbeat_bench heartbeat_bench.cpp)
