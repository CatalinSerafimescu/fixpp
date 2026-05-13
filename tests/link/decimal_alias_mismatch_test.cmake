# tests/link/decimal_alias_mismatch_test.cmake
# Seam #9 — AC-B3 negative case: two TUs each define a conflicting FIXPP_DECIMAL_T.
# Expected outcome: link failure with an unresolved symbol containing "decimal_alias_sentinel".
# check_expected_failure.py invokes CMake on a self-contained sub-project defined here.

cmake_minimum_required(VERSION 3.28)
project(DecimalAliasMismatch LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

# Point at the library submodule root (set by the wrapper script via -DFIXPP_ROOT=...)
if(NOT DEFINED FIXPP_ROOT)
    message(FATAL_ERROR "FIXPP_ROOT must be set to the library submodule root directory")
endif()

include_directories("${FIXPP_ROOT}/include")

# Build two executables with conflicting aliases.
# TU A: uses the default alias (pod_decimal) — will find the sentinel definition.
# TU B: uses a fake alias type — will NOT find a sentinel definition → link error.

# We test by building a single executable that links two object files,
# each compiled with a different FIXPP_DECIMAL_T.

add_executable(decimal_alias_mismatch_test
  "${FIXPP_ROOT}/tests/link/tu_a.cpp"
  "${FIXPP_ROOT}/tests/link/tu_b.cpp"
)
target_compile_definitions(decimal_alias_mismatch_test PRIVATE)
# tu_a.cpp compiled without -DFIXPP_DECIMAL_T → defaults to pod_decimal
# tu_b.cpp compiled with -DFIXPP_DECIMAL_T=::mismatch_type → different sentinel
set_source_files_properties(
  "${FIXPP_ROOT}/tests/link/tu_b.cpp"
  PROPERTIES COMPILE_DEFINITIONS "FIXPP_DECIMAL_T=::fixpp::core::test::mismatch_type"
)
