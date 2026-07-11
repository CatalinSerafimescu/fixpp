# tests/codegen/CMakeLists.txt
# 003-dictionary-codegen — US1 typed-accessor / shape suites (T017-T022).
#
# Generated headers are produced at CMake CONFIGURE time by cmake/Codegen.cmake
# (T016 — [2c §7.6] configure-time graph, AC-C4 / AC-T2).  The
# fixpp_codegen_generate target is defined in cmake/Codegen.cmake (no-op at
# build time; headers already on disk after configure).  This file no longer
# defines the code-generation custom command — that is the single configure-time
# mechanism owned by Codegen.cmake.

if(NOT TARGET fixpp_codegen_generate)
  message(STATUS "tests/codegen: fixpp_codegen_generate not available; skipping US1 suites")
  return()
endif()

find_package(GTest CONFIG REQUIRED)

set(FIXPP_CODEGEN_OUT "${CMAKE_BINARY_DIR}/_codegen/include")

# 068-test-binary-grouping US2: pure/stateless codegen-output tests (9 .cpp →
# 1 binary). Label-homogeneous "codegen"; all share fixpp_dictionary+GTest
# link-deps. FIXPP_CODEGEN_MANIFEST is consumed only by conformance_test.cpp
# but is harmless to define for the others (same pattern as the dictionary
# pilot's shared-but-single-consumer FIXPP_DICT_DATA_DIR). Dispositions +
# `-R`→`-L` replacements are recorded in
# specs/068-test-binary-grouping/dispositions.md.
add_executable(codegen_pure_tests
  typed_accessor_test.cpp
  flyweight_shape_test.cpp
  msgtype_boundary_test.cpp
  validator_shape_test.cpp
  length_data_table_test.cpp
  group_entry_read_test.cpp
  nested_group_read_test.cpp
  group_entry_generation_trap_test.cpp   # EXPECT_DEATH only (gtest fork) — isolation-safe
  conformance/conformance_test.cpp        # T022 conformance — manifest-driven presence/shape
)
add_dependencies(codegen_pure_tests fixpp_codegen_generate)
target_link_libraries(codegen_pure_tests PRIVATE
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_pure_tests PRIVATE
  "${FIXPP_CODEGEN_OUT}"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(codegen_pure_tests PRIVATE
  "FIXPP_CODEGEN_INC=\"${FIXPP_CODEGEN_OUT}\""
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_MANIFEST=\"${CMAKE_CURRENT_SOURCE_DIR}/conformance/must_include_manifest.txt\""
)
add_test(NAME codegen_pure_tests COMMAND codegen_pure_tests)
set_tests_properties(codegen_pure_tests PROPERTIES LABELS codegen)

# ── Standalone: isolation-sensitive codegen tests (FR-002) ───────────────────
# codegen_group_entry_alloc_gate_test: in-TU global operator-new counter +
# live $<TARGET_FILE:...> mallocnesia sidecar selection below — must keep its
# exact name.
add_executable(codegen_group_entry_alloc_gate_test group_entry_alloc_gate_test.cpp)
add_dependencies(codegen_group_entry_alloc_gate_test fixpp_codegen_generate)
target_link_libraries(codegen_group_entry_alloc_gate_test PRIVATE
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_group_entry_alloc_gate_test PRIVATE
  "${FIXPP_CODEGEN_OUT}"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(codegen_group_entry_alloc_gate_test PRIVATE
  "FIXPP_CODEGEN_INC=\"${FIXPP_CODEGEN_OUT}\""
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME codegen_group_entry_alloc_gate_test COMMAND codegen_group_entry_alloc_gate_test)
set_tests_properties(codegen_group_entry_alloc_gate_test PROPERTIES LABELS codegen)

# 062 T019: mallocnesia gate for codegen_group_entry_alloc_gate_test — re-runs
# it under LD_PRELOAD so global malloc/calloc/realloc is intercepted (the
# dual-gate: the in-band counting_resource above catches PMR-routed
# allocation; this catches a non-PMR escape via global new, per
# feedback_tracking_pmr_resource_false_pass). Absent mallocnesia.so → skipped
# (the base ctest above is the fallback; the counting_resource + stack/NULL-
# upstream arena still enforce the gate without interception).
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME codegen_group_entry_alloc_gate_test_mallocnesia
    COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:codegen_group_entry_alloc_gate_test>")
  set_tests_properties(codegen_group_entry_alloc_gate_test_mallocnesia PROPERTIES
    LABELS "codegen;alloc_guard;mallocnesia;062")
endif()

# 067-codegen-writer-emitter T007/T009: emit_builders unit tests — link the
# codegen tool's ir.cpp/gen_util.hpp DIRECTLY (not via the generated headers)
# to inspect MessageIR.group_order pre-emission. Mirrors fixpp-codegen's own
# dependencies (fixpp::dictionary + pugixml, PRIVATE, host-tool-only).
find_package(pugixml CONFIG REQUIRED)
add_executable(codegen_067_emit_builders_unit_test
  test_067_emit_builders_unit.cpp
  "${CMAKE_SOURCE_DIR}/tools/codegen/fixpp-codegen/ir.cpp"
  "${CMAKE_SOURCE_DIR}/tools/codegen/fixpp-codegen/emit_builders.cpp"
)
target_include_directories(codegen_067_emit_builders_unit_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tools/codegen/fixpp-codegen"
)
target_link_libraries(codegen_067_emit_builders_unit_test PRIVATE
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(codegen_067_emit_builders_unit_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME codegen_067_emit_builders_unit_test COMMAND codegen_067_emit_builders_unit_test)
set_tests_properties(codegen_067_emit_builders_unit_test PROPERTIES LABELS "codegen;067")

# T041: determinism_test — NFR-003-7 / AC-T1 / AC-T2
#
# Assertions:
#   (1) Run fixpp-codegen twice into separate temp dirs; assert byte-identical.
#   (2) Assert no source-tree write (mtime/file-count snapshot of dictionaries/).
#   (3) Assert generated Messages.hpp == checked-in golden for all 4 versions.
#   (4) Gate B PR#187 r1 F3 (SC-003): `--families official` v44/Builders.hpp
#       == checked-in golden (promotes the 069 T009 local byte-identity
#       procedure into a CI-automated gate).
#
# The binary path is injected at CMake configure time via $<TARGET_FILE:fixpp-codegen>
# (a generator expression) so it resolves to the correct preset-specific path.
# add_dependencies ensures the binary is built before the test runs.
add_executable(codegen_determinism_test determinism_test.cpp)
add_dependencies(codegen_determinism_test fixpp-codegen)
target_link_libraries(codegen_determinism_test PRIVATE
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(codegen_determinism_test PRIVATE
  "FIXPP_CODEGEN_BIN=\"$<TARGET_FILE:fixpp-codegen>\""
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/specs/003-dictionary-codegen/contracts/golden\""
  "FIXPP_CODEGEN_069_OFFICIAL_GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/specs/069-v44-all-families/contracts/golden\""
)
add_test(NAME codegen_determinism_test COMMAND codegen_determinism_test)
set_tests_properties(codegen_determinism_test PROPERTIES LABELS codegen)

# T039: cmake -P build-graph test — fixpp::dict::codegen-build-graph-check
#
# AC-C4: asserts that (a) generated headers exist post-configure, (b) generation
# is configure-time (not build-time), (c) per-version INTERFACE targets carry
# INTERFACE_INCLUDE_DIRECTORIES into the build tree, (d) source tree is clean.
#
# CTest name uses '::' (allowed in add_test NAME but not in add_custom_target).
# The cmake -P script reads ${CMAKE_BINARY_DIR}/_codegen/graph_props.cmake which
# is written by cmake/Codegen.cmake at configure time.
#
# The graph_props.cmake path is passed via -DFIXPP_GRAPH_PROPS= so the script
# has no hard-coded paths and works with any preset / build directory.
add_test(
  NAME "fixpp::dict::codegen-build-graph-check"
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_GRAPH_PROPS=${CMAKE_BINARY_DIR}/_codegen/graph_props.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/codegen_build_graph_test.cmake"
)
set_tests_properties("fixpp::dict::codegen-build-graph-check" PROPERTIES
  LABELS codegen)
