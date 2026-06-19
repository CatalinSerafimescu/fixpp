# cmake/ProjectOptions.cmake
# Toggle options for optional build components.
# Phase 3 default — revisit if build-time requirements change.

option(FIXPP_BUILD_TESTS   "Build GoogleTest unit + smoke tests"      ON)
option(FIXPP_BUILD_BENCH   "Build Google Benchmark targets"           OFF)
option(FIXPP_BUILD_FUZZ    "Build libFuzzer harnesses (Clang only)"   OFF)
option(FIXPP_BUILD_PYTHON  "Build SWIG Python bindings"               OFF)
option(FIXPP_BUILD_SERVICE "Build fixppd service binary"              OFF)
option(FIXPP_BUILD_INTEROP_PERF "Build the Phase 9 fixpp benchmark driver (perf/)" OFF)
