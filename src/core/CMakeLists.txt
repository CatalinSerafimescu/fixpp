# src/core/CMakeLists.txt
# fixpp::core — primitives, PMR allocators, error type, decimal traits [arch §4.1]
# Promoted from INTERFACE to a compiled library in Phase 4 (001-core-decimal).

add_library(fixpp_core STATIC
  decimal.cpp
  fix_time.cpp         # 005-session-establishment-fsm T011: FIX UTCTimestamp
                       # formatter + parser (core/ module-exit row #4, D-3).
)
add_library(fixpp::core ALIAS fixpp_core)

target_include_directories(fixpp_core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

# core has no internal project dependencies — it is the leaf [arch §2.2].
# The asio-dependent fixpp::core threading impls do NOT live in this leaf
# target: system_clock_source.cpp is compiled into fixpp_session; the
# test-only mock_clock.cpp is a test-support lib, built whenever something links
# it: the tests, and also bench/session and tests/fuzz, which link fixpp_mock_clock
# and otherwise fail with `cannot find -lfixpp_mock_clock` under FIXPP_BUILD_TESTS=OFF.
if(FIXPP_BUILD_TESTS OR FIXPP_BUILD_BENCH OR FIXPP_BUILD_FUZZ)
  add_subdirectory(test)   # fixpp_mock_clock — test-only deterministic Clock
endif()
