# src/core/CMakeLists.txt
# fixpp::core — primitives, PMR allocators, error type, decimal traits [arch §4.1]
# Promoted from INTERFACE to a compiled library in Phase 4 (001-core-decimal).

add_library(fixpp_core STATIC
  decimal.cpp
)
add_library(fixpp::core ALIAS fixpp_core)

target_include_directories(fixpp_core PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# core has no internal project dependencies — it is the leaf [arch §2.2].
# The asio-dependent fixpp::core threading impls do NOT live in this leaf
# target: system_clock_source.cpp is compiled into fixpp_session; the
# test-only mock_clock.cpp is a test-support lib (built only with tests).
if(FIXPP_BUILD_TESTS)
  add_subdirectory(test)   # fixpp_mock_clock — test-only deterministic Clock
endif()
