# bench/codegen/compile_time_bench/CMakeLists.txt
# T046 — NFR-003-2 compile-time ceiling bench (seam #2).
#
# Registers a CTest target that invokes compile_time_bench.sh with the
# correct codegen include path and compiler. Labelled `bench` so it runs with
# `ctest -L bench`.
#
# This does NOT add a compile target (no add_executable) — the measurement IS
# a fresh clang++ -fsyntax-only invocation (not a running binary benchmark).

if(NOT TARGET fixpp_codegen_generate)
    message(STATUS
        "bench/codegen/compile_time_bench: fixpp_codegen_generate not available; skipping T046")
    return()
endif()

# The bench script must run after generated headers are written. Since codegen
# is configure-time (Codegen.cmake), headers exist when the build tree is
# configured; the dependency is satisfied by the configure order.
#
# We register as a CTest script test so it can be run standalone with:
#   ctest -R compile_time_bench --preset linux-clang-release
add_test(
    NAME compile_time_bench
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/compile_time_bench.sh"
            "${CMAKE_BINARY_DIR}/_codegen/include"
            "${CMAKE_CXX_COMPILER}"
            "${CMAKE_SOURCE_DIR}/include"
)
set_tests_properties(compile_time_bench PROPERTIES
    LABELS bench
    TIMEOUT 300   # up to 5 min: v50sp2 alone can take ~8-9 s, all-versions more
)
