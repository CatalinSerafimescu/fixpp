# tests/oracle/CMakeLists.txt — Seam #8 pytest oracle wiring.
# Gate B P1 #4: previously this directory was not added at the parent level,
# so the Python Decimal oracle never ran. Adds a CTest entry that invokes
# pytest against decimal_compare_oracle_test.py and points the conftest
# fixture at the SHARED fixpp_capi artifact built in src/capi.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Sanitizer-instrumented .so files require the sanitizer runtime to be
# preloaded before Python ctypes can dlopen them — the codebase does not
# wire LD_PRELOAD plumbing for that. The oracle is a *mathematical*
# correctness gate (Python `decimal.Decimal` vs `fixpp_decimal_compare`),
# not a memory-safety gate, so skipping it on sanitizer presets loses no
# meaningful coverage: it still runs in clang-release, clang-coverage,
# gcc-release, and the bench job, which all build a non-instrumented .so.
if(NOT (FIXPP_ENABLE_ASAN OR FIXPP_ENABLE_UBSAN OR FIXPP_ENABLE_TSAN))
  add_test(
    NAME decimal_compare_oracle
    COMMAND ${Python3_EXECUTABLE} -m pytest -xvs
            "${CMAKE_CURRENT_SOURCE_DIR}/decimal_compare_oracle_test.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  set_tests_properties(decimal_compare_oracle PROPERTIES
    ENVIRONMENT "FIXPP_CAPI_LIB=$<TARGET_FILE:fixpp_capi_shared>"
    LABELS "oracle;tier1"
  )
endif()
