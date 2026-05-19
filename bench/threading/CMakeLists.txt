# bench/threading/CMakeLists.txt
# 007-threading-clock latency regression bench (seam 8, [2d §6.3]/§9.8; SC-004).
#
# T005 scaffold: declared-empty on purpose — the bench_threading target +
# bench/baselines/threading/threading_baselines.json land in Phase 8 (T053/T056).
# This file exists so `add_subdirectory(threading)` in ../CMakeLists.txt
# configures and builds clean before any bench body is written
# (mirrors the bench/sync INTERFACE-first precedent from 006).

# Intentionally no targets yet. Phase 8 (T053) adds:
#   add_executable(bench_threading bench_threading.cpp)
#   target_link_libraries(bench_threading PRIVATE
#     fixpp_core fixpp_session asio::asio
#     benchmark::benchmark benchmark::benchmark_main)
