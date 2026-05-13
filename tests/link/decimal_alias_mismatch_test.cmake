# tests/link/decimal_alias_mismatch_test.cmake
# Seam #9 — AC-B3 negative case: two TUs each define a conflicting FIXPP_DECIMAL_T.
# Expected outcome: link failure with an unresolved symbol matching
# "decimal_alias_sentinel<...mismatch_type...>" specifically.
# check_expected_failure.py invokes CMake on a self-contained sub-project defined here.

cmake_minimum_required(VERSION 3.28)
project(DecimalAliasMismatch LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

# Point at the library submodule root (set by the wrapper script via -DFIXPP_ROOT=...)
if(NOT DEFINED FIXPP_ROOT)
    message(FATAL_ERROR "FIXPP_ROOT must be set to the library submodule root directory")
endif()

# Point at the build lib/ directory containing libfixpp_core.a
# (set by check_expected_failure.py via -DFIXPP_LIB_DIR=...).
# This is required so that tu_a.cpp's pod_decimal sentinel resolves from the
# built library, leaving only mismatch_type's sentinel unresolvable — which
# is the actual AC-B3 contract being tested (P1-4 fix).
if(NOT DEFINED FIXPP_LIB_DIR)
    message(FATAL_ERROR "FIXPP_LIB_DIR must be set to the build lib/ directory")
endif()

include_directories("${FIXPP_ROOT}/include")

# Find the pre-built fixpp_core static library.
find_library(FIXPP_CORE_LIB
    NAMES fixpp_core
    PATHS "${FIXPP_LIB_DIR}"
    NO_DEFAULT_PATH
    REQUIRED
)

# Build a single executable from two TUs with conflicting FIXPP_DECIMAL_T aliases.
# TU A: uses the default alias (pod_decimal) — its sentinel IS defined in the library.
# TU B: uses a fake alias type (mismatch_type) — its sentinel is NOT defined → link error.
#
# By linking fixpp_core, the pod_decimal sentinel resolves; only mismatch_type's
# tag remains unresolvable. The linker error is therefore specifically about
# decimal_alias_sentinel<...mismatch_type...> — not a generic missing-library failure.

add_executable(decimal_alias_mismatch_test
  "${FIXPP_ROOT}/tests/link/tu_a.cpp"
  "${FIXPP_ROOT}/tests/link/tu_b.cpp"
)
# Link against the pre-built library so tu_a's pod_decimal sentinel resolves.
target_link_libraries(decimal_alias_mismatch_test PRIVATE "${FIXPP_CORE_LIB}")

# tu_a.cpp compiled without -DFIXPP_DECIMAL_T → defaults to pod_decimal
# tu_b.cpp compiled with -DFIXPP_DECIMAL_T=::mismatch_type → different sentinel
set_source_files_properties(
  "${FIXPP_ROOT}/tests/link/tu_b.cpp"
  PROPERTIES COMPILE_DEFINITIONS "FIXPP_DECIMAL_T=::fixpp::core::test::mismatch_type"
)
