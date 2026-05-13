# tests/oracle/CMakeLists.txt — Seam #8 pytest oracle wiring.
# Gate B P1 #4: previously this directory was not added at the parent level,
# so the Python Decimal oracle never ran. Adds a CTest entry that invokes
# pytest against decimal_compare_oracle_test.py and points the conftest
# fixture at the SHARED fixpp_capi artifact built in src/capi.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
  NAME decimal_compare_oracle
  COMMAND ${Python3_EXECUTABLE} -m pytest -xvs
          "${CMAKE_CURRENT_SOURCE_DIR}/decimal_compare_oracle_test.py"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Sanitizer-instrumented .so files require the sanitizer runtime to be
# preloaded before Python ctypes can dlopen them. Rather than hard-code
# runtime paths, oracle sanitizer builds fall back to the default
# conftest.py resolution (build/linux-clang-release/lib/libfixpp_capi.so),
# which is the correct mathematical-oracle reference regardless of preset.
if(FIXPP_ENABLE_ASAN OR FIXPP_ENABLE_UBSAN OR FIXPP_ENABLE_TSAN)
  set_tests_properties(decimal_compare_oracle PROPERTIES
    LABELS "oracle;tier1"
  )
else()
  set_tests_properties(decimal_compare_oracle PROPERTIES
    ENVIRONMENT "FIXPP_CAPI_LIB=$<TARGET_FILE:fixpp_capi_shared>"
    LABELS "oracle;tier1"
  )
endif()
