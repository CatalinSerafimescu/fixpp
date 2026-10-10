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

# fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.4, R-1′): asio's recycler slot
# count, PUBLIC. The selector is "publishes the include/ root", not "links asio": the
# installed headers include asio (clock.hpp among them), and every exported C++ member that
# publishes that root links fixpp_core PUBLIC/INTERFACE, directly or transitively, so one
# attachment here reaches all of them and their CMake consumers. fixpp::capi and
# fixpp::service reach it only through $<LINK_ONLY:>, which withholds it; their headers
# include no asio. fixpp_core's own TUs include no asio, so it is inert here. The value
# comes from the root CMakeLists.txt (cmake/FixppAsioRecycler.cmake).
if(NOT FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE MATCHES "^[0-9]+$")
  message(FATAL_ERROR "src/core: FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE is not set")
endif()
target_compile_definitions(fixpp_core PUBLIC
  "ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=${FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE}")

# core has no internal project dependencies — it is the leaf [arch §2.2].
# The asio-dependent fixpp::core threading impls do NOT live in this leaf
# target: system_clock_source.cpp is compiled into fixpp_session; the
# test-only mock_clock.cpp is a test-support lib, built whenever something links
# it: the tests, and also bench/session and tests/fuzz, which link fixpp_mock_clock
# and otherwise fail with `cannot find -lfixpp_mock_clock` under FIXPP_BUILD_TESTS=OFF.
if(FIXPP_BUILD_TESTS OR FIXPP_BUILD_BENCH OR FIXPP_BUILD_FUZZ)
  add_subdirectory(test)   # fixpp_mock_clock — test-only deterministic Clock
endif()
