# cmake/fixpp_store_test.cmake
#
# fixpp_add_store_test — shared CMake helper for 008-message-store test targets.
#
# Consolidates the near-identical add_store_test / add_store_conformance_test /
# add_store_perf_test function bodies (TSan env, include paths, link, add_test).
# The optional KIND parameter is reserved for future CTest label assignment.
#
# Usage:
#   fixpp_add_store_test(<target-name>
#       SOURCES <file1> [<file2> ...]
#       [EXTRA_LIBS <lib1> ...]
#       [EXTRA_INCLUDE_DIRS <dir1> ...]
#   )
#
# Constraints (briefed):
#   - add_store_bench is NOT unified here (Google Benchmark, no GTest, different flow).
#   - The perf/ LD_PRELOAD wiring is deferred to D-17 /gate-a re-touch; this
#     helper does NOT handle LD_PRELOAD.

function(fixpp_add_store_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;EXTRA_LIBS;EXTRA_INCLUDE_DIRS")

    add_executable(${name} ${ARG_SOURCES})

    target_link_libraries(${name} PRIVATE
        fixpp_session
        GTest::gtest
        GTest::gtest_main
        ${ARG_EXTRA_LIBS}
    )

    target_include_directories(${name} PRIVATE
        "${CMAKE_SOURCE_DIR}/tests"
        ${ARG_EXTRA_INCLUDE_DIRS}
    )

    add_test(NAME ${name} COMMAND ${name})

    # TSan suppressions for asio's any_executor refcount fence (carried into
    # store seams that route work through asio::any_io_executor).
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")
endfunction()
