# bench/transport/CMakeLists.txt
# 012-2h-transport bench targets — T021/T022/T023.
# Source files authored in Phase 3a; wired by T029 once asio_tls_transport ships.
# benchmark::benchmark is already found by the parent bench/CMakeLists.txt.

# T021 — [2h §6.3] row 1 — read-path completion-handler dispatch ≤200 ns p99.
add_executable(bench_async_read_some_dispatch
  bench_async_read_some_dispatch.cpp)
target_link_libraries(bench_async_read_some_dispatch PRIVATE
  fixpp_transport  benchmark::benchmark)
set_target_properties(bench_async_read_some_dispatch PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/transport")

# T022 — [2h §6.3] row 2 — write-path issue on strand ≤200 ns p99.
add_executable(bench_async_write_issue bench_async_write_issue.cpp)
target_link_libraries(bench_async_write_issue PRIVATE
  fixpp_transport  benchmark::benchmark)
set_target_properties(bench_async_write_issue PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/transport")

# T023 — [2h §6.3] row 4 — TLS 1.3 1-RTT handshake loopback ≤10 ms p99.
add_executable(bench_tls_handshake_loopback bench_tls_handshake_loopback.cpp)
target_link_libraries(bench_tls_handshake_loopback PRIVATE
  fixpp_transport  fixpp_tls  benchmark::benchmark)
set_target_properties(bench_tls_handshake_loopback PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/transport")
