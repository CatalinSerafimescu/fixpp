# tests/packaging/CMakeLists.txt — 084-packaging-cpack-export test registrations.
#
# EVERY test here configures and builds a CMake sub-project, so every one is
# RUN_SERIAL with an explicit TIMEOUT, mirroring the existing consumer witness.
# Two hazards these tests must respect (quickstart.md §7b):
#   * scratch output goes OUTSIDE the source tree, or it reds the git-cleanliness
#     gate in tests/codegen/codegen_build_graph_test.cmake;
#   * concurrent sub-configures of the same source dir race, hence RUN_SERIAL.

set(_fixpp_pkg_work "${CMAKE_BINARY_DIR}/_packaging_tests")

# ── T036 (FR-006 / SC-006): incompatible version fails at CONFIGURE ──────────
add_test(
  NAME fixpp::packaging::version-mismatch
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_PROBE_SRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/version_probe"
    "-DFIXPP_WORK_DIR=${_fixpp_pkg_work}/version-mismatch"
    "-DFIXPP_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DFIXPP_PROJECT_VERSION=${PROJECT_VERSION}"
    "-DFIXPP_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_version_mismatch_witness.cmake"
)
set_tests_properties(fixpp::packaging::version-mismatch PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 300
  LABELS "packaging"
)

# ── T041/T042/T043 (SC-016, FR-018e): the clean-environment tier ─────────────
add_test(
  NAME fixpp::packaging::clean-env
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_CONSUMER_SRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/clean_env"
    "-DFIXPP_WORK_DIR=${_fixpp_pkg_work}/clean-env"
    "-DFIXPP_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DFIXPP_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_clean_env_witness.cmake"
)
set_tests_properties(fixpp::packaging::clean-env PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 600
  LABELS "packaging"
)
