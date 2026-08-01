# bench/capi/CMakeLists.txt
# 083-group-delimiter-resolution T065 — FR-022 path 3 (C-ABI commit).
# Built only when FIXPP_BUILD_BENCH=ON (gated by the parent bench/CMakeLists).
#
# Needs src/capi (capi_internal.hpp) for the two documented test-only seams
# this bench reproduces WITHOUT gtest: L-050-1 (dict handle) and L-050-5
# (loopback endpoint). It deliberately does not include
# tests/capi/capi_loopback_support.hpp, which would drag gtest into a bench.

find_package(benchmark CONFIG REQUIRED)

add_executable(capi_commit_group_bench capi_commit_group_bench.cpp)
target_link_libraries(capi_commit_group_bench PRIVATE
  fixpp_capi
  fixpp_dictionary
  benchmark::benchmark
  benchmark::benchmark_main
)
target_include_directories(capi_commit_group_bench PRIVATE
  "${CMAKE_SOURCE_DIR}/src/capi"
  "${CMAKE_SOURCE_DIR}/include"
)
set_target_properties(capi_commit_group_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/capi"
)
