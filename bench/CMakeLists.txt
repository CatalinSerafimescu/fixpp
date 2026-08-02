# bench/CMakeLists.txt
# Built only when FIXPP_BUILD_BENCH=ON per cmake/ProjectOptions.cmake.

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

add_subdirectory(core)
add_subdirectory(capi)   # 083-group-delimiter-resolution bench (T065 — FR-022 path 3, C-ABI commit)
add_subdirectory(codegen)
add_subdirectory(dictionary)
add_subdirectory(wire)
add_subdirectory(sync)   # 006-async-mutex bench (T003)
add_subdirectory(threading)  # 007-threading-clock bench (T005 scaffold; body T053/T056)
add_subdirectory(session)    # 008-message-store bench (T049 — MemoryStore + FileStore latency rows)
add_subdirectory(tls)        # 011-tls-policy bench (T021/T022 Pinset, T041 verify_peer)
add_subdirectory(transport)  # 012-2h-transport bench (T021/T022/T023 — populated Phase 3)
add_subdirectory(log)        # 017-log-otel bench (T030/TS-9 enqueue latency)
