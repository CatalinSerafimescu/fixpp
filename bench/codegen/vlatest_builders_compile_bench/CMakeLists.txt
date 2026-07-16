# bench/codegen/vlatest_builders_compile_bench/CMakeLists.txt
# 077-builder-args-dedup T015 [US1] — vlatest builder-tier compile-resource
# bench (SC-001/SC-002; quickstart.md V1).
#
# Registers a CTest target that invokes compile_bench.sh with the correct
# codegen include path and compiler. Labelled `compile-budget` so it runs
# with `ctest -L compile-budget`.
#
# This does NOT add a compile target (no add_executable) — the measurement IS
# a fresh `clang++ -fsyntax-only` invocation under /usr/bin/time -v (not a
# running binary benchmark), mirroring bench/codegen/compile_time_bench's
# script-based approach (T046 precedent).
#
# Gated on FIXPP_CODEGEN_FIX_LATEST — with the tier OFF, vlatest/Builders.hpp
# does not exist; the script itself SKIPs (exit 0) in that case, but the
# target is still only registered when both fixpp_codegen_generate exists AND
# the option is ON, to avoid a permanently-skipping ctest entry in an OFF
# build's default matrix.
if(NOT TARGET fixpp_codegen_generate)
    message(STATUS
        "bench/codegen/vlatest_builders_compile_bench: fixpp_codegen_generate not available; skipping T015")
    return()
endif()

if(NOT FIXPP_CODEGEN_FIX_LATEST)
    message(STATUS
        "bench/codegen/vlatest_builders_compile_bench: FIXPP_CODEGEN_FIX_LATEST=OFF; skipping T015")
    return()
endif()

add_test(
    NAME vlatest_builders_compile_bench
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/compile_bench.sh"
            "${CMAKE_BINARY_DIR}/_codegen/include"
            "${CMAKE_CXX_COMPILER}"
            "${CMAKE_SOURCE_DIR}/include"
)
set_tests_properties(vlatest_builders_compile_bench PROPERTIES
    LABELS "bench;077;compile-budget"
    TIMEOUT 300   # up to 5 min: measured ~66-85s on the reference host
)
