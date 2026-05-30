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

# T022 (014 FR-013b) — TLS 1.3 1-RTT handshake loopback latency baseline.
# Wired to asio_tls_transport_factory + loopback acceptor via leaf_rsa2048.pem.
# Original scaffold comment said T023/T029; task renumbered to T022 in 014 tasks.md.
add_executable(bench_tls_handshake_loopback bench_tls_handshake_loopback.cpp)
target_link_libraries(bench_tls_handshake_loopback PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  benchmark::benchmark
)
target_include_directories(bench_tls_handshake_loopback PRIVATE
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp"
)
target_compile_definitions(bench_tls_handshake_loopback PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
set_target_properties(bench_tls_handshake_loopback PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/transport")
