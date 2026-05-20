# tests/alloc_guard/CMakeLists.txt — allocation discipline test (seam #6)
# Runs under mallocnesia via tools/check_alloc.py.

add_executable(decimal_alloc_guard_test decimal_alloc_guard_test.cpp)
target_link_libraries(decimal_alloc_guard_test PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
set_target_properties(decimal_alloc_guard_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME decimal_alloc_guard_test COMMAND decimal_alloc_guard_test)

# 004-wire-codec T051 (FR-012/SC-002): zero-heap-alloc over the wire
# parse+serialize hot path. Same mallocnesia harness as decimal above.
add_executable(wire_alloc_guard_test wire_alloc_guard_test.cpp)
target_link_libraries(wire_alloc_guard_test PRIVATE
  fixpp_wire
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(wire_alloc_guard_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(wire_alloc_guard_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME wire_alloc_guard_test COMMAND wire_alloc_guard_test)

# 006-async-mutex T056 (Seam #10 b): zero-global-alloc for the async_mutex
# embedded (mr==nullptr) contended hot path. Same mallocnesia harness as above.
# CTest NAME is deliberately NOT `sync_`-prefixed: this binary is driven by
# tools/check_alloc.py under the mallocnesia LD_PRELOAD (like the wire/decimal
# alloc-guards), and must NOT be swept into the `^sync_` seam ctest selection
# (where, run plainly without mallocnesia, it would no-op or — under a non-built
# preset — show as a spurious "Not Run").
add_executable(sync_alloc_guard_test sync_alloc_guard_test.cpp)
target_link_libraries(sync_alloc_guard_test PRIVATE
  fixpp_sync
  GTest::gtest
  GTest::gtest_main
)
set_target_properties(sync_alloc_guard_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME alloc_guard_sync_embedded COMMAND sync_alloc_guard_test)

# 007-threading-clock T051 (Seam 7 / [2d §9.7]): zero-global-heap between
# parse and fromApp over a 10⁴-message corpus. All dynamic storage is from
# PMR arenas (cancellable_dispatch node from session_arena; per-awaiter
# promise frame from message_arena on PMR fallback). Same mallocnesia harness.
# CTest NAME is deliberately NOT `threading_`-prefixed (driven by
# tools/check_alloc.py under LD_PRELOAD, not swept by the ^threading_ ctest
# selection — same convention as alloc_guard_sync_embedded above).
add_executable(test_dispatch_alloc_guard test_dispatch_alloc_guard.cpp)
target_link_libraries(test_dispatch_alloc_guard PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_dispatch_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(test_dispatch_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME alloc_guard_dispatch COMMAND test_dispatch_alloc_guard)

# 007-threading-clock T052 (Seam 18 / [2d §9.18]): zero-global-heap after the
# first heartbeat cycle over 10⁴ cycles, BOTH threading modes; an idle session
# that never sleeps allocates no slot (E12 / D-8 / N-P1-1). Same harness.
add_executable(test_clock_sleep_alloc_guard test_clock_sleep_alloc_guard.cpp)
target_link_libraries(test_clock_sleep_alloc_guard PRIVATE
  fixpp_session
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test_clock_sleep_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(test_clock_sleep_alloc_guard PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME alloc_guard_clock_sleep COMMAND test_clock_sleep_alloc_guard)
