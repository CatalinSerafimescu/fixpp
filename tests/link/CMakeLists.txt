# tests/link/CMakeLists.txt — Seam #9 alias-mismatch link-failure check.
# Gate B P1 #5: previously this directory existed but was never added at the
# parent level, so check_expected_failure.py was never invoked from ctest.
#
# The negative test verifies AC-B3: two TUs with conflicting FIXPP_DECIMAL_T
# fail to link with an "undefined reference to ... decimal_alias_sentinel ..."
# message specifically for the mismatched type. check_expected_failure.py exits
# 0 only when the expected build failure and the mismatch-type substring both
# occur.
#
# --fixpp-lib-dir is passed so the cmake sub-project can link tu_a.cpp against
# the already-built libfixpp_core.a, making the pod_decimal sentinel resolve
# cleanly and leaving only the mismatch_type sentinel undefined (P1-4 fix).

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
  NAME decimal_alias_mismatch_link
  COMMAND ${Python3_EXECUTABLE}
          "${CMAKE_CURRENT_SOURCE_DIR}/check_expected_failure.py"
          --fixpp-root "${CMAKE_SOURCE_DIR}"
          --fixpp-lib-dir "${CMAKE_BINARY_DIR}/lib"
)
set_tests_properties(decimal_alias_mismatch_link PROPERTIES
  LABELS "link;tier1"
  TIMEOUT 120
)
