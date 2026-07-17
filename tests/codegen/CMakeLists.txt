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

# 076-fix-latest-typed-codegen T008/T011: vlatest compile-smoke +
# dispatch-exclusion witnesses. T008 needs the vlatest {Fields,Messages,
# Validator,Reify}.hpp read-tier headers (fixpp_codegen_generate); T011 needs
# ONLY the existing v42/v44/v50sp2 _dispatch/ surface (fixpp_dictionary) —
# grouped into one binary since both are cheap/isolation-safe (unlike
# Builders.hpp-dependent work, deliberately NOT attempted here — see the .cpp
# file headers).
#
# gate-b/r1 P1: gated on FIXPP_CODEGEN_FIX_LATEST — with the tier OFF,
# cmake/Codegen.cmake removes _codegen/include/fixpp/vlatest/ entirely, so
# this target (and the two below) must not exist or the OFF build fails to
# compile (FR-003/B-1 "build succeeds when OFF").
if(FIXPP_CODEGEN_FIX_LATEST)
add_executable(codegen_vlatest_tests
  vlatest_compile_smoke_test.cpp        # T008
  vlatest_dispatch_exclusion_test.cpp   # T011
  # 077-builder-args-dedup T013 [US1]: joins this existing binary
  # ([const §VII.8] whole-binary grouping; 069's test_067_builder_roundtrip
  # precedent). Pure text-parse of Builders.hpp (no #include of the heavy
  # header, unlike the sources above) -- cheap, isolation-safe, reuses the
  # FIXPP_CODEGEN_VLATEST_BUILDERS_HPP macro already defined below.
  test_077_builder_dedup_count.cpp      # T013
  # 077-builder-args-dedup T018 [US2]: vt11/v42 emit-no-builders witness +
  # v44/v50sp2/vlatest positive control. Pure filesystem existence check --
  # same isolation-safety rationale as T013 above.
  test_077_builder_no_emit.cpp          # T018
)
add_dependencies(codegen_vlatest_tests fixpp_codegen_generate)
target_link_libraries(codegen_vlatest_tests PRIVATE
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_vlatest_tests PRIVATE
  "${FIXPP_CODEGEN_OUT}"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(codegen_vlatest_tests PRIVATE
  "FIXPP_DISPATCH_APPLICATION_HEADER_PATH=\"${FIXPP_CODEGEN_OUT}/fixpp/_dispatch/reify_dispatch_application.hpp\""
  "FIXPP_CODEGEN_VLATEST_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/Builders.hpp\""
  # 077-builder-args-dedup T018 [US2]: per-version Builders.hpp paths for the
  # no-emit / positive-control witness (test_077_builder_no_emit.cpp).
  "FIXPP_CODEGEN_V42_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v42/Builders.hpp\""
  "FIXPP_CODEGEN_V44_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v44/Builders.hpp\""
  "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v50sp2/Builders.hpp\""
  "FIXPP_CODEGEN_VT11_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vt11/Builders.hpp\""
)
add_test(NAME codegen_vlatest_tests COMMAND codegen_vlatest_tests)
set_tests_properties(codegen_vlatest_tests PROPERTIES
  LABELS "codegen;076;vlatest_smoke;vlatest_dispatch_exclusion;077;builder-dedup")

# 076-fix-latest-typed-codegen T013 [US2]: V-1 completeness census —
# independent raw-XML walker (pugixml, no ir.cpp/ir.hpp/emit_manifest.cpp
# dependency — see the .cpp file header for the non-circularity rationale)
# diffed against the T012-emitted Manifest.txt. Needs pugixml (already
# find_package'd above for codegen_067_emit_builders_unit_test) + the
# generated manifest path.
add_executable(codegen_vlatest_census_test vlatest_completeness_census_test.cpp)
add_dependencies(codegen_vlatest_census_test fixpp_codegen_generate)
target_link_libraries(codegen_vlatest_census_test PRIVATE
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(codegen_vlatest_census_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_VLATEST_MANIFEST=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/Manifest.txt\""
)
add_test(NAME codegen_vlatest_census_test COMMAND codegen_vlatest_census_test)
set_tests_properties(codegen_vlatest_census_test PROPERTIES
  LABELS "codegen;076;vlatest_census")

# 076-fix-latest-typed-codegen T014 [US2]: V-1b manifest<->class
# consistency gate — pure text parsing of the shipped Messages.hpp (class
# side, non-circular w.r.t. ir.cpp/emit_manifest.cpp) diffed against a
# from-scratch re-projection of the T012-emitted Manifest.txt (manifest
# side). No pugixml / fixpp header dependency — see the .cpp file header
# for the extraction/projection rules and non-circularity rationale.
add_executable(codegen_vlatest_manifest_class_test vlatest_manifest_class_consistency_test.cpp)
add_dependencies(codegen_vlatest_manifest_class_test fixpp_codegen_generate)
target_link_libraries(codegen_vlatest_manifest_class_test PRIVATE
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(codegen_vlatest_manifest_class_test PRIVATE
  "FIXPP_CODEGEN_VLATEST_MANIFEST=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/Manifest.txt\""
  "FIXPP_CODEGEN_VLATEST_MESSAGES_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/Messages.hpp\""
)
add_test(NAME codegen_vlatest_manifest_class_test COMMAND codegen_vlatest_manifest_class_test)
set_tests_properties(codegen_vlatest_manifest_class_test PROPERTIES
  LABELS "codegen;076;vlatest_manifest_class")

# 077-builder-args-dedup T023 [US4]: vlatest builder-completeness census.
# Own executable, gated on FIXPP_CODEGEN_FIX_LATEST (mirrors
# test_077_vlatest_builder_roundtrip above the tier gate at T012) —
# #includes the ~78MB deduped fixpp::vlatest::Builders.hpp; build THIS
# target with -j1 (feedback_build_resource_cap_oom).
add_executable(codegen_077_vlatest_builder_completeness_test
  test_077_vlatest_builder_completeness.cpp
)
add_dependencies(codegen_077_vlatest_builder_completeness_test fixpp_codegen_generate)
target_link_libraries(codegen_077_vlatest_builder_completeness_test PRIVATE
  fixpp_wire
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_077_vlatest_builder_completeness_test PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
target_compile_definitions(codegen_077_vlatest_builder_completeness_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_VLATEST_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/Builders.hpp\""
)
# MSVC C1128: this TU takes the address of every vlatest build_/validate_ over
# the ~78MB deduped Builders.hpp (173 plans); under debug/asan the section count
# exceeds the default COFF object-format limit. /bigobj lifts the 2^16 cap.
target_compile_options(codegen_077_vlatest_builder_completeness_test PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
add_test(NAME codegen_077_vlatest_builder_completeness_test COMMAND codegen_077_vlatest_builder_completeness_test)
set_tests_properties(codegen_077_vlatest_builder_completeness_test PROPERTIES
  LABELS "codegen;077;builder-completeness;vlatest")
endif()  # FIXPP_CODEGEN_FIX_LATEST

# 077-builder-args-dedup T023 [US4]: v44 builder-completeness census
# (contracts/builder-completeness.md C1-C4) — address-of compile-time gate
# over the generated fixpp::v44::Builders.hpp + the committed
# generated/v44_builder_completeness_entries.def table, cross-checked at
# runtime against an independent raw-XML walk (builder_completeness_common.hpp).
add_executable(codegen_077_v44_builder_completeness_test
  test_077_v44_builder_completeness.cpp
)
add_dependencies(codegen_077_v44_builder_completeness_test fixpp_codegen_generate)
target_link_libraries(codegen_077_v44_builder_completeness_test PRIVATE
  fixpp_wire
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_077_v44_builder_completeness_test PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
target_compile_definitions(codegen_077_v44_builder_completeness_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_V44_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v44/Builders.hpp\""
)
add_test(NAME codegen_077_v44_builder_completeness_test COMMAND codegen_077_v44_builder_completeness_test)
set_tests_properties(codegen_077_v44_builder_completeness_test PROPERTIES
  LABELS "codegen;077;builder-completeness")

# 077-builder-args-dedup T023 [US4]: v50sp2 builder-completeness census. Own
# executable (not joined to the v44 target above) — #includes the ~75MB
# deduped fixpp::v50sp2::Builders.hpp (RSS discipline, mirrors
# tests/session/CMakeLists.txt's per-version builder-roundtrip split).
add_executable(codegen_077_v50sp2_builder_completeness_test
  test_077_v50sp2_builder_completeness.cpp
)
add_dependencies(codegen_077_v50sp2_builder_completeness_test fixpp_codegen_generate)
target_link_libraries(codegen_077_v50sp2_builder_completeness_test PRIVATE
  fixpp_wire
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_077_v50sp2_builder_completeness_test PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
target_compile_definitions(codegen_077_v50sp2_builder_completeness_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v50sp2/Builders.hpp\""
)
# MSVC C1128: this TU takes the address of every v50sp2 build_/validate_ over
# the ~75MB deduped Builders.hpp (156 plans); under debug/asan the section count
# exceeds the default COFF object-format limit. /bigobj lifts the 2^16 cap.
target_compile_options(codegen_077_v50sp2_builder_completeness_test PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
add_test(NAME codegen_077_v50sp2_builder_completeness_test COMMAND codegen_077_v50sp2_builder_completeness_test)
set_tests_properties(codegen_077_v50sp2_builder_completeness_test PROPERTIES
  LABELS "codegen;077;builder-completeness")

# 077-builder-args-dedup T026 [US4]: vt11 + v42 completeness (C3c) + the C4
# structural-key safety pin. Cheap/ungated (no Builders.hpp #include —
# filesystem existence + text parsing only).
add_executable(codegen_077_v42_vt11_completeness_c4_test
  test_077_v42_vt11_completeness_and_c4.cpp
)
add_dependencies(codegen_077_v42_vt11_completeness_c4_test fixpp_codegen_generate)
target_link_libraries(codegen_077_v42_vt11_completeness_c4_test PRIVATE
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(codegen_077_v42_vt11_completeness_c4_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_V42_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v42/Builders.hpp\""
  "FIXPP_CODEGEN_VT11_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vt11/Builders.hpp\""
  "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v50sp2/Builders.hpp\""
)
add_test(NAME codegen_077_v42_vt11_completeness_c4_test COMMAND codegen_077_v42_vt11_completeness_c4_test)
set_tests_properties(codegen_077_v42_vt11_completeness_c4_test PROPERTIES
  LABELS "codegen;077;builder-completeness")

# 077-builder-args-dedup T025 [US4]: builder_completeness_mutation_witness —
# standalone, §8-EXEMPT (spawns a subprocess, writes a scratch temp dir; not
# folded into a whole-binary target). Depends on the T024 seam's
# fixpp-codegen-drop-witness binary (EXCLUDE_FROM_ALL — built only via this
# dependency).
add_executable(codegen_builder_completeness_mutation_witness_test
  builder_completeness_mutation_witness_test.cpp
)
add_dependencies(codegen_builder_completeness_mutation_witness_test fixpp-codegen-drop-witness)
target_link_libraries(codegen_builder_completeness_mutation_witness_test PRIVATE
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(codegen_builder_completeness_mutation_witness_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_DROP_WITNESS_BIN=\"$<TARGET_FILE:fixpp-codegen-drop-witness>\""
)
add_test(NAME codegen_builder_completeness_mutation_witness_test
  COMMAND codegen_builder_completeness_mutation_witness_test)
set_tests_properties(codegen_builder_completeness_mutation_witness_test PROPERTIES
  LABELS "codegen;077;builder-completeness;mutation-witness")

# T041: determinism_test — NFR-003-7 / AC-T1 / AC-T2
#
# Assertions:
#   (1) Run fixpp-codegen twice into separate temp dirs; assert byte-identical.
#   (2) Assert no source-tree write (mtime/file-count snapshot of dictionaries/).
#   (3) Assert generated Messages.hpp == checked-in golden for all 4 versions.
#   (4) Gate B PR#187 r1 F3 (SC-003): `--families official` v44/Builders.hpp
#       == checked-in golden (promotes the 069 T009 local byte-identity
#       procedure into a CI-automated gate).
#   (5) 076 T017 [US3] (V-4): generated vlatest/Messages.hpp == checked-in
#       076 golden.
#   (6) 076 T018/T019 [US3] (V-7/FR-004/SC-003): an OFF-path job (legacy
#       XMLs only) and an ON-path job (legacy + orchestra) produce
#       byte-identical legacy output, vlatest/ is absent OFF and present +
#       golden-matching ON.
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
  "FIXPP_CODEGEN_076_GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/specs/076-fix-latest-typed-codegen/contracts/golden\""
  # 077-builder-args-dedup T027 [Polish]: v44 `all`-mode + v50sp2 + vlatest
  # Builders.hpp checked-in-golden-diff gate (FR-013).
  "FIXPP_CODEGEN_077_GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/specs/077-builder-args-dedup/contracts/golden\""
)
add_test(NAME codegen_determinism_test COMMAND codegen_determinism_test)
set_tests_properties(codegen_determinism_test PROPERTIES LABELS codegen)

# 077-builder-args-dedup T019 [US2]: only defined when THIS build was
# configured with the tier ON -- MainTreeVlatestBuildersUnaffectedByOffPathWitness
# in determinism_test.cpp is #ifdef-guarded on it, so an OFF-configured tree
# (where _codegen/include/fixpp/vlatest/ doesn't exist at all) never sees a
# spurious existence-check failure.
if(FIXPP_CODEGEN_FIX_LATEST)
  target_compile_definitions(codegen_determinism_test PRIVATE
    "FIXPP_CODEGEN_MAIN_VLATEST_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/Builders.hpp\"")
endif()

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
  LABELS codegen
  RESOURCE_LOCK codegen_source_tree)

# Gate B PR#187 round 3 F1: seed a stale-tool scenario by mutating a generator
# source, re-running configure without rebuilding the main-tree binary, and
# proving Codegen.cmake rebuilds the bootstrap tool before regeneration.
add_test(
  NAME "fixpp::dict::codegen-source-staleness-check"
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_CMAKE_COMMAND=${CMAKE_COMMAND}"
    "-DFIXPP_CONFIGURE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DFIXPP_CONFIGURE_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DFIXPP_CODEGEN_MAIN_BIN=$<TARGET_FILE:fixpp-codegen>"
    "-DFIXPP_CODEGEN_BOOTSTRAP_BIN=${CMAKE_BINARY_DIR}/_codegen_bootstrap/bin/fixpp-codegen${CMAKE_EXECUTABLE_SUFFIX}"
    "-DFIXPP_GENERATOR_SOURCE=${CMAKE_SOURCE_DIR}/tools/codegen/fixpp-codegen/emit_builders.cpp"
    "-DFIXPP_BUILDERS_HEADER=${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v44/Builders.hpp"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/codegen_source_staleness_test.cmake"
)
set_tests_properties("fixpp::dict::codegen-source-staleness-check" PROPERTIES
  LABELS codegen
  RUN_SERIAL TRUE
  RESOURCE_LOCK codegen_source_tree)

# 077-builder-args-dedup T022 [US3] (FR-009/SC-005, T006 decision = FULL
# 16-artifact recursive byte-diff): regenerates the legacy read tier into an
# isolated scratch dir (NOT the shared _codegen tree -- no RUN_SERIAL/
# RESOURCE_LOCK needed, mirrors codegen_determinism_test's TempDir pattern)
# and SHA256-diffs all 16 artifacts against the pre-077 T001 baseline.
add_test(
  NAME "fixpp::dict::read-tier-byte-diff"
  COMMAND "${CMAKE_COMMAND}"
    "-DFIXPP_CODEGEN_BIN=$<TARGET_FILE:fixpp-codegen>"
    "-DFIXPP_DICT_DATA_DIR=${CMAKE_SOURCE_DIR}/dictionaries"
    "-DFIXPP_READ_TIER_OUT=${CMAKE_BINARY_DIR}/_read_tier_byte_diff_scratch"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/read_tier_byte_diff_test.cmake"
)
set_tests_properties("fixpp::dict::read-tier-byte-diff" PROPERTIES
  LABELS "codegen;077;read-tier-byte-diff")
