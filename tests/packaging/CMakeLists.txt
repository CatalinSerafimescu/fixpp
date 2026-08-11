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

# ── T058 (FR-009/FR-012/FR-013/FR-018d, SC-004, SC-013): package CONTENTS ────
# The expected archive count is NOT computed here. FIXPP_EXPORT_TARGETS is set in
# the root CMakeLists AFTER this subdirectory is added, so reading it here yields
# an empty list and an expected count of 0 -- an exact-match gate silently
# asserting the wrong value. The test derives the count from the SHIPPED
# fixppTargets.cmake instead, which also makes it a real cross-check.

# 086 / Gate B r2 P1 #1 — registered through an OUTER DRIVER, and NOT with
# PASS_REGULAR_EXPRESSION.
#
# Two things must both hold: the witness must exit 0, AND it must have executed
# the 086 block (which is otherwise deletable without anything noticing).
# `PASS_REGULAR_EXPRESSION` cannot express that conjunction — CTest IGNORES the
# exit code when the regex matches. Using it here, as round 1 did, made this gate
# strictly WEAKER: the witness checks DEB, RPM and TGZ in sequence, so a token
# printed for DEB masked a FATAL_ERROR raised for RPM or TGZ.
#
# run_package_contents_gate.cmake asserts the exit code FIRST, then the token.
add_test(
  NAME fixpp::packaging::contents
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_WITNESS_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/run_package_contents_witness.cmake"
    "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_WORK_DIR=${_fixpp_pkg_work}/contents"
    "-DFIXPP_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DFIXPP_PROJECT_VERSION=${PROJECT_VERSION}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_package_contents_gate.cmake"
)
set_tests_properties(fixpp::packaging::contents PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 900
  LABELS "packaging"
)

# ── T026 (FR-003/FR-007/FR-008): installed imported names map to real aliases ─
add_test(
  NAME fixpp::packaging::export-names
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_WORK_DIR=${_fixpp_pkg_work}/export-names"
    "-DFIXPP_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_export_names_witness.cmake"
)
set_tests_properties(fixpp::packaging::export-names PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 600
  LABELS "packaging"
)

# ── fixpp-package target shape: generated Ninja graph, not witness-local lore ─
add_test(
  NAME fixpp::packaging::package-target
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_package_target_witness.cmake"
)
set_tests_properties(fixpp::packaging::package-target PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 600
  LABELS "packaging"
)

# ── T060 (FR-021a, I24): package PROVENANCE ─────────────────────────────────
add_test(
  NAME fixpp::packaging::provenance
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_WORK_DIR=${_fixpp_pkg_work}/provenance"
    "-DFIXPP_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DFIXPP_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_provenance_witness.cmake"
)
set_tests_properties(fixpp::packaging::provenance PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 600
  LABELS "packaging"
)

# ── T062a (FR-011): TELEMETRY provenance — a FIFTH test, not an extension ────
# FIXPP_OTEL_OFF_PACKAGE is the red fixture: a real artifact from a real OTel-OFF
# tree. It is OPTIONAL — producing it needs an entire second build, so the red is
# a ONE-TIME RECORDED PROOF (T059-shaped, not T042-shaped; see the long note in
# run_telemetry_provenance_witness.cmake) and the standing assertion here is the
# PASS leg. Supplying a path that does not exist is still fatal: a silently
# skipped red leg is the thing this test refuses. Produce one with:
#   conan install . -pr conan/profiles/<p> -o "&:with_otel=False" -of build/<p>-otel-off
#   cmake -S . -B build/<p>-otel-off -DFIXPP_BUILD_OTEL=OFF \
#         -DFIXPP_ALLOW_OTEL_OFF_PACKAGE=ON ...
#   cmake --build build/<p>-otel-off --target <export members> && cpack -G TGZ ...
set(FIXPP_OTEL_OFF_PACKAGE "" CACHE FILEPATH
    "Red fixture for T062a: a package built from a genuinely OTel-OFF tree")
add_test(
  NAME fixpp::packaging::telemetry-provenance
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_WORK_DIR=${_fixpp_pkg_work}/telemetry"
    "-DFIXPP_OTEL_OFF_PACKAGE=${FIXPP_OTEL_OFF_PACKAGE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_telemetry_provenance_witness.cmake"
)
set_tests_properties(fixpp::packaging::telemetry-provenance PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 600
  LABELS "packaging"
)

# ── T038/T039/T040 (FR-005, SC-001): the REAL-CLIENT tier ───────────────────
# The only witness whose client was NOT written for this feature: fixpp's own
# ~1700-line perf/interop driver, built unmodified against the installed package
# and RUN over a real loopback FIX session. It is also the first witness to
# compile an asio header, which is what a package exporting asio-typed public
# APIs actually has to support.
#
# The longest TIMEOUT here: this leg stage-installs, configures, compiles a
# 1700-line TU against the full session/transport/tls stack, and then runs a
# 220-message FIX session.
add_test(
  NAME fixpp::packaging::real-client
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DFIXPP_PROJECT_SRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/real_client"
    "-DFIXPP_WORK_DIR=${_fixpp_pkg_work}/real-client"
    "-DFIXPP_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DFIXPP_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_real_client_witness.cmake"
)
set_tests_properties(fixpp::packaging::real-client PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 1200
  LABELS "packaging"
)
