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
  # precedent). Pure text-parse of groups.hpp (078: G_...Args structs' new
  # data-only home; no #include of any heavy header, unlike the sources
  # above) -- cheap, isolation-safe, reuses the
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
  # 078-precompiled-builder-libs census re-point: Builders.hpp -> all.hpp
  # (the monolith is gone, FR-008; all.hpp is the registry's new home).
  "FIXPP_CODEGEN_VLATEST_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/all.hpp\""
  # 077-builder-args-dedup T018 [US2]: per-version all.hpp paths for the
  # no-emit / positive-control witness (test_077_builder_no_emit.cpp).
  "FIXPP_CODEGEN_V42_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v42/all.hpp\""
  "FIXPP_CODEGEN_V44_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v44/all.hpp\""
  "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v50sp2/all.hpp\""
  "FIXPP_CODEGEN_VT11_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vt11/all.hpp\""
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
# #includes the ~78MB deduped fixpp::vlatest::all.hpp; build THIS
# target with -j1 (feedback_build_resource_cap_oom). Gated on
# FIXPP_BUILD_HEAVY_BUILDER_TESTS (heavy-TU config gating — top-level CMakeLists).
if(FIXPP_BUILD_HEAVY_BUILDER_TESTS)
add_executable(codegen_077_vlatest_builder_completeness_test
  test_077_vlatest_builder_completeness.cpp
)
add_dependencies(codegen_077_vlatest_builder_completeness_test fixpp_codegen_generate
  fixpp_builders_vlatest fixpp_validators_vlatest)
target_link_libraries(codegen_077_vlatest_builder_completeness_test PRIVATE
  fixpp_wire
  fixpp_dictionary
  pugixml::pugixml
  fixpp::builders::vlatest
  fixpp::validators::vlatest
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_077_vlatest_builder_completeness_test PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
target_compile_definitions(codegen_077_vlatest_builder_completeness_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  # 078-precompiled-builder-libs census re-point: Builders.hpp -> all.hpp.
  "FIXPP_CODEGEN_VLATEST_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/all.hpp\""
)
# MSVC C1128: this TU takes the address of every vlatest build_/validate_ over
# the ~78MB deduped all.hpp (173 plans); under debug/asan the section count
# exceeds the default COFF object-format limit. /bigobj lifts the 2^16 cap.
target_compile_options(codegen_077_vlatest_builder_completeness_test PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
add_test(NAME codegen_077_vlatest_builder_completeness_test COMMAND codegen_077_vlatest_builder_completeness_test)
set_tests_properties(codegen_077_vlatest_builder_completeness_test PROPERTIES
  LABELS "codegen;077;builder-completeness;vlatest")
endif()  # FIXPP_BUILD_HEAVY_BUILDER_TESTS
endif()  # FIXPP_CODEGEN_FIX_LATEST

# 077-builder-args-dedup T023 [US4]: v44 builder-completeness census
# (contracts/builder-completeness.md C1-C4) — address-of compile-time gate
# over the generated fixpp::v44::all.hpp + the committed
# generated/v44_builder_completeness_entries.def table, cross-checked at
# runtime against an independent raw-XML walk (builder_completeness_common.hpp).
add_executable(codegen_077_v44_builder_completeness_test
  test_077_v44_builder_completeness.cpp
)
add_dependencies(codegen_077_v44_builder_completeness_test fixpp_codegen_generate
  fixpp_builders_v44 fixpp_validators_v44)
target_link_libraries(codegen_077_v44_builder_completeness_test PRIVATE
  fixpp_wire
  fixpp_dictionary
  pugixml::pugixml
  fixpp::builders::v44
  fixpp::validators::v44
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_077_v44_builder_completeness_test PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
target_compile_definitions(codegen_077_v44_builder_completeness_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  # 078-precompiled-builder-libs census re-point: Builders.hpp -> all.hpp.
  "FIXPP_CODEGEN_V44_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v44/all.hpp\""
)
add_test(NAME codegen_077_v44_builder_completeness_test COMMAND codegen_077_v44_builder_completeness_test)
set_tests_properties(codegen_077_v44_builder_completeness_test PROPERTIES
  LABELS "codegen;077;builder-completeness")

# 077-builder-args-dedup T023 [US4]: v50sp2 builder-completeness census. Own
# executable (not joined to the v44 target above) — #includes the ~75MB
# deduped fixpp::v50sp2::all.hpp (RSS discipline, mirrors
# tests/session/CMakeLists.txt's per-version builder-roundtrip split). Gated on
# FIXPP_BUILD_HEAVY_BUILDER_TESTS (heavy-TU config gating — top-level CMakeLists).
if(FIXPP_BUILD_HEAVY_BUILDER_TESTS)
add_executable(codegen_077_v50sp2_builder_completeness_test
  test_077_v50sp2_builder_completeness.cpp
)
add_dependencies(codegen_077_v50sp2_builder_completeness_test fixpp_codegen_generate
  fixpp_builders_v50sp2 fixpp_validators_v50sp2)
target_link_libraries(codegen_077_v50sp2_builder_completeness_test PRIVATE
  fixpp_wire
  fixpp_dictionary
  pugixml::pugixml
  fixpp::builders::v50sp2
  fixpp::validators::v50sp2
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(codegen_077_v50sp2_builder_completeness_test PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
target_compile_definitions(codegen_077_v50sp2_builder_completeness_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  # 078-precompiled-builder-libs census re-point: Builders.hpp -> all.hpp.
  "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v50sp2/all.hpp\""
)
# MSVC C1128: this TU takes the address of every v50sp2 build_/validate_ over
# the ~75MB deduped all.hpp (156 plans); under debug/asan the section count
# exceeds the default COFF object-format limit. /bigobj lifts the 2^16 cap.
target_compile_options(codegen_077_v50sp2_builder_completeness_test PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
add_test(NAME codegen_077_v50sp2_builder_completeness_test COMMAND codegen_077_v50sp2_builder_completeness_test)
set_tests_properties(codegen_077_v50sp2_builder_completeness_test PROPERTIES
  LABELS "codegen;077;builder-completeness")
endif()  # FIXPP_BUILD_HEAVY_BUILDER_TESTS

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
  # 078-precompiled-builder-libs census re-point: Builders.hpp -> all.hpp
  # (V50SP2 feeds the C4 group-struct text-parse, which derives sibling
  # groups.hpp from this path -- see test_077_v42_vt11_completeness_and_c4.cpp).
  "FIXPP_CODEGEN_V42_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v42/all.hpp\""
  "FIXPP_CODEGEN_VT11_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vt11/all.hpp\""
  "FIXPP_CODEGEN_V50SP2_BUILDERS_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v50sp2/all.hpp\""
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
#   (4) [historical -- retired 078] Gate B PR#187 r1 F3 (SC-003) originally
#       pinned `--families official` v44/Builders.hpp == checked-in golden
#       (promoting the 069 T009 local byte-identity procedure into a
#       CI-automated gate); 078's OfficialModeBuildersStructuralShape
#       replaces it with a structural witness over v44/all.hpp (see
#       determinism_test.cpp's retirement-rationale comment).
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
  "FIXPP_CODEGEN_076_GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/specs/076-fix-latest-typed-codegen/contracts/golden\""
  # 078-precompiled-builder-libs T007/T008 (R6/FR-010): v44/v50sp2/vlatest
  # builder-tier golden SET (groups.hpp/all.hpp/validators/traits.hpp/
  # messages/<Msg>.*), replacing 077's single-file monolith goldens.
  "FIXPP_CODEGEN_078_GOLDEN_DIR=\"${CMAKE_SOURCE_DIR}/specs/078-precompiled-builder-libs/contracts/golden\""
)
add_test(NAME codegen_determinism_test COMMAND codegen_determinism_test)
set_tests_properties(codegen_determinism_test PROPERTIES LABELS codegen)

# 077-builder-args-dedup T019 [US2]: only defined when THIS build was
# configured with the tier ON -- MainTreeVlatestBuildersUnaffectedByOffPathWitness
# in determinism_test.cpp is #ifdef-guarded on it, so an OFF-configured tree
# (where _codegen/include/fixpp/vlatest/ doesn't exist at all) never sees a
# spurious existence-check failure.
if(FIXPP_CODEGEN_FIX_LATEST)
  # gate-b/r6 (078-precompiled-builder-libs): macro renamed from the stale
  # ..._BUILDERS_HPP (pre-078 census re-point: Builders.hpp -> all.hpp).
  target_compile_definitions(codegen_determinism_test PRIVATE
    "FIXPP_CODEGEN_MAIN_VLATEST_ALL_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/vlatest/all.hpp\"")
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
    # 078-precompiled-builder-libs census re-point: Builders.hpp -> all.hpp
    # (builder_registry's new home, T011).
    "-DFIXPP_BUILDERS_HEADER=${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v44/all.hpp"
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

# 078-precompiled-builder-libs T002 (research.md R2a — Phase-0 BLOCKING
# prerequisite): hand-written ~30-line ODR / builder⟂validator (SC-003) probe.
# Hand-writes a MINIMAL MOCK of the intended split (test_078_odr_mock/) — the
# real emitter split (T003+) does not exist yet — to settle R1 (disjoint
# objects) and R2 (traits home) empirically before the emitter commits to the
# layout. Independent of fixpp_codegen_generate; no codegen dependency.
#
# Two disjoint mock static libs, mirroring data-model.md Entity 4:
#   test078_mock_builders   <- msg_a.builder.cpp,   msg_b.builder.cpp   (build_ only)
#   test078_mock_validators <- msg_a.validator.cpp, msg_b.validator.cpp (validate_ + traits)
add_library(test078_mock_builders STATIC
  test_078_odr_mock/msg_a.builder.cpp
  test_078_odr_mock/msg_b.builder.cpp
)

add_library(test078_mock_validators STATIC
  test_078_odr_mock/msg_a.validator.cpp
  test_078_odr_mock/msg_b.validator.cpp
)

# Legs (i) + (iii): the "both-linked" probe binary. Force-inlines
# validate_MsgA (test_078_odr_mock/msg_a.validator.inl) while linking BOTH
# mock libs and calling build_/validate_ from both messages — the test IS the
# link succeeding (a duplicate-symbol regression fails the build, not an
# assertion).
add_executable(test078_odr_sc003_probe test_078_odr_sc003_probe.cpp)
target_link_libraries(test078_odr_sc003_probe PRIVATE
  test078_mock_builders
  test078_mock_validators
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME test078_odr_sc003_probe COMMAND test078_odr_sc003_probe)
set_tests_properties(test078_odr_sc003_probe PROPERTIES
  LABELS "codegen;078;test_078_odr")

# Leg (ii): builder-only binary — links ONLY test078_mock_builders.
add_executable(test078_odr_builder_only_probe test_078_odr_builder_only_nm_probe.cpp)
target_link_libraries(test078_odr_builder_only_probe PRIVATE test078_mock_builders)

# `nm --defined-only` on the builder-only binary must show zero
# validate_/writer_traits symbols (SC-003).
find_program(FIXPP_078_NM_TOOL nm)
if(FIXPP_078_NM_TOOL)
  add_test(
    NAME test078_odr_sc003_probe_nm_builder_only
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXPP_NM=${FIXPP_078_NM_TOOL}"
      "-DFIXPP_TARGET=$<TARGET_FILE:test078_odr_builder_only_probe>"
      "-DFIXPP_ABSENT=validate_|writer_traits"
      "-DFIXPP_DEMANGLE=OFF"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test_078_nm_assert.cmake"
  )
  set_tests_properties(test078_odr_sc003_probe_nm_builder_only PROPERTIES
    LABELS "codegen;078;test_078_odr")
else()
  message(STATUS "tests/codegen: nm not found; skipping test078_odr_sc003_probe_nm_builder_only leg (ii)")
endif()

# ── 078-precompiled-builder-libs T020 [US1]: slim-header compile + link
# witness (US1 AC1/AC2). Own OBJECT library so the nm discrimination check
# below can inspect the compiled .o directly (before it is folded into the
# test executable) -- links only fixpp::builders::v44 (the v44 lib is built
# locally; vlatest is CI-deferred per the orchestrator brief).
add_library(test078_slim_compile_us1_obj OBJECT test_078_slim_compile_us1.cpp)
add_dependencies(test078_slim_compile_us1_obj fixpp_codegen_generate)
target_include_directories(test078_slim_compile_us1_obj PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
target_link_libraries(test078_slim_compile_us1_obj PRIVATE GTest::gtest)

add_executable(test078_slim_compile_us1 $<TARGET_OBJECTS:test078_slim_compile_us1_obj>)
add_dependencies(test078_slim_compile_us1 fixpp_builders_v44)
target_link_libraries(test078_slim_compile_us1 PRIVATE
  fixpp::builders::v44
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME test078_slim_compile_us1 COMMAND test078_slim_compile_us1)
set_tests_properties(test078_slim_compile_us1 PROPERTIES
  LABELS "codegen;078;us1;slim-compile")

# Discrimination (US1 AC1): the compiled OBJECT must carry zero DEFINED
# build_ symbols and one UNDEFINED build_NewOrderSingle reference.
if(FIXPP_078_NM_TOOL)
  add_test(
    NAME test078_slim_compile_us1_nm_object_check
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXPP_NM=${FIXPP_078_NM_TOOL}"
      "-DFIXPP_TARGET=$<TARGET_OBJECTS:test078_slim_compile_us1_obj>"
      "-DFIXPP_ABSENT=fixpp::v44::build_"
      "-DFIXPP_UNDEFINED_PRESENT=fixpp::v44::build_NewOrderSingle"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test_078_nm_assert.cmake"
  )
  set_tests_properties(test078_slim_compile_us1_nm_object_check PROPERTIES
    LABELS "codegen;078;us1;slim-compile")
else()
  message(STATUS "tests/codegen: nm not found; skipping test078_slim_compile_us1_nm_object_check")
endif()

# ── 078-precompiled-builder-libs T022 [US1]: two-version no-symbol-collision
# witness (US1 AC3). Links BOTH fixpp::builders::v44 and fixpp::builders::v50sp2
# (both built locally; vlatest is CI-deferred).
add_executable(test078_two_version_link_us1 test_078_two_version_link_us1.cpp)
add_dependencies(test078_two_version_link_us1 fixpp_codegen_generate
  fixpp_builders_v44 fixpp_builders_v50sp2)
target_link_libraries(test078_two_version_link_us1 PRIVATE
  fixpp::builders::v44
  fixpp::builders::v50sp2
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test078_two_version_link_us1 PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
add_test(NAME test078_two_version_link_us1 COMMAND test078_two_version_link_us1)
set_tests_properties(test078_two_version_link_us1 PROPERTIES
  LABELS "codegen;078;us1;two-version-link")

# ── 078-precompiled-builder-libs T024 [US2]: `nm` builder-only witness
# (SC-003/FR-005). Links ONLY fixpp::builders::v44 -- NOT
# fixpp::validators::v44 -- proving a send-only consumer carries zero
# validator code. Real-lib counterpart to the T002 R2a probe's leg (ii).
add_executable(test078_nm_builder_only_us2 test_078_nm_builder_only_us2.cpp)
add_dependencies(test078_nm_builder_only_us2 fixpp_codegen_generate fixpp_builders_v44)
target_link_libraries(test078_nm_builder_only_us2 PRIVATE
  fixpp::builders::v44
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test078_nm_builder_only_us2 PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
add_test(NAME test078_nm_builder_only_us2 COMMAND test078_nm_builder_only_us2)
set_tests_properties(test078_nm_builder_only_us2 PROPERTIES
  LABELS "codegen;078;us2;nm-builder-only")

# Discrimination (SC-003/FR-005): the LINKED BINARY must carry zero
# fixpp::v44::validate_/writer_traits symbols. Demangled + namespace-anchored
# (the shared test_078_nm_assert.cmake default) -- NOT the T002 leg (ii)
# witness's bare-mangled "validate_" substring match, which false-positives
# against fixpp::wire::body_builder::validate_group_grammar, a real symbol
# pulled in by this binary's fixpp_wire dependency (never present in T002's
# hand-written mock).
if(FIXPP_078_NM_TOOL)
  add_test(
    NAME test078_nm_builder_only_us2_nm_check
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXPP_NM=${FIXPP_078_NM_TOOL}"
      "-DFIXPP_TARGET=$<TARGET_FILE:test078_nm_builder_only_us2>"
      "-DFIXPP_ABSENT=fixpp::v44::validate_|writer_traits"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test_078_nm_assert.cmake"
  )
  set_tests_properties(test078_nm_builder_only_us2_nm_check PROPERTIES
    LABELS "codegen;078;us2;nm-builder-only")
else()
  message(STATUS "tests/codegen: nm not found; skipping test078_nm_builder_only_us2_nm_check")
endif()

# ── 078-precompiled-builder-libs T026 [US3]: builder-side mixing witness
# (FR-007 headline / New-4). Own OBJECT library so the object-level nm
# discrimination check below can inspect the compiled .o directly (before it
# is folded into the test executable), mirroring T020's pattern -- force-
# inlines NewOrderSingle while linking fixpp::builders::v44 for
# ExecutionReport.
add_library(test078_builder_mixing_us3_obj OBJECT test_078_builder_mixing_us3.cpp)
add_dependencies(test078_builder_mixing_us3_obj fixpp_codegen_generate)
target_link_libraries(test078_builder_mixing_us3_obj PRIVATE
  fixpp_dictionary
  GTest::gtest
)
target_include_directories(test078_builder_mixing_us3_obj PRIVATE
  "${FIXPP_CODEGEN_OUT}"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(test078_builder_mixing_us3_obj PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)

add_executable(test078_builder_mixing_us3 $<TARGET_OBJECTS:test078_builder_mixing_us3_obj>)
add_dependencies(test078_builder_mixing_us3 fixpp_builders_v44)
target_link_libraries(test078_builder_mixing_us3 PRIVATE
  fixpp::builders::v44
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(test078_builder_mixing_us3 PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test078_builder_mixing_us3 COMMAND test078_builder_mixing_us3)
set_tests_properties(test078_builder_mixing_us3 PROPERTIES
  LABELS "codegen;078;us3;builder-mixing")

# Discrimination (US3 AC1/FR-006/FR-007): the compiled OBJECT must show
# build_NewOrderSingle DEFINED locally (force-inlined) and
# build_ExecutionReport UNDEFINED (resolves only at final link).
if(FIXPP_078_NM_TOOL)
  add_test(
    NAME test078_builder_mixing_us3_object_nm_check
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXPP_NM=${FIXPP_078_NM_TOOL}"
      "-DFIXPP_TARGET=$<TARGET_OBJECTS:test078_builder_mixing_us3_obj>"
      "-DFIXPP_PRESENT=fixpp::v44::build_NewOrderSingle"
      "-DFIXPP_ABSENT=fixpp::v44::build_ExecutionReport"
      "-DFIXPP_UNDEFINED_PRESENT=fixpp::v44::build_ExecutionReport"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test_078_nm_assert.cmake"
  )
  set_tests_properties(test078_builder_mixing_us3_object_nm_check PROPERTIES
    LABELS "codegen;078;us3;builder-mixing")
else()
  message(STATUS "tests/codegen: nm not found; skipping test078_builder_mixing_us3_object_nm_check")
endif()

# ── 078-precompiled-builder-libs Gate B RC#3 [US3]: builder-side SAFE-path
# counter-test for the inline-XOR-link contract (spec.md Edge Case ~line 128;
# quickstart.md Scenario 4d). A 2-TU program that force-inlines
# build_NewOrderSingle in BOTH TUs -- never link-resolved in this binary, so
# it deliberately does NOT link fixpp::builders::v44.
add_executable(test078_builder_inline_all_tus_us3
  test_078_builder_inline_all_tus_us3.cpp
  test_078_builder_inline_all_tus_us3_tu2.cpp
)
add_dependencies(test078_builder_inline_all_tus_us3 fixpp_codegen_generate)
target_link_libraries(test078_builder_inline_all_tus_us3 PRIVATE
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test078_builder_inline_all_tus_us3 PRIVATE
  "${FIXPP_CODEGEN_OUT}"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(test078_builder_inline_all_tus_us3 PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
)
add_test(NAME test078_builder_inline_all_tus_us3 COMMAND test078_builder_inline_all_tus_us3)
set_tests_properties(test078_builder_inline_all_tus_us3 PROPERTIES
  LABELS "codegen;078;us3;builder-inline-all-tus")

# ── 078-precompiled-builder-libs T027 [US3]: validator-side mixing witness
# (R2a leg i, at real-lib scale). Force-inlines validate_Email (shares the
# G_33Args group plan) while linking fixpp::validators::v44 for validate_News.
add_executable(test078_validator_mixing_us3 test_078_validator_mixing_us3.cpp)
add_dependencies(test078_validator_mixing_us3 fixpp_codegen_generate fixpp_validators_v44)
target_link_libraries(test078_validator_mixing_us3 PRIVATE
  fixpp_wire
  fixpp::validators::v44
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test078_validator_mixing_us3 PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
add_test(NAME test078_validator_mixing_us3 COMMAND test078_validator_mixing_us3)
set_tests_properties(test078_validator_mixing_us3 PROPERTIES
  LABELS "codegen;078;us3;validator-mixing")

# ── 078-precompiled-builder-libs Gate B RC#3 [US3]: validator-side SAFE-path
# counter-test for the inline-XOR-link contract (spec.md Edge Case ~line 128;
# quickstart.md Scenario 4d). A 2-TU program that force-inlines
# validate_Email in BOTH TUs -- never link-resolved in this binary, so it
# deliberately does NOT link fixpp::validators::v44.
add_executable(test078_validator_inline_all_tus_us3
  test_078_validator_inline_all_tus_us3.cpp
  test_078_validator_inline_all_tus_us3_tu2.cpp
)
add_dependencies(test078_validator_inline_all_tus_us3 fixpp_codegen_generate)
target_link_libraries(test078_validator_inline_all_tus_us3 PRIVATE
  fixpp_wire
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test078_validator_inline_all_tus_us3 PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
add_test(NAME test078_validator_inline_all_tus_us3 COMMAND test078_validator_inline_all_tus_us3)
set_tests_properties(test078_validator_inline_all_tus_us3 PROPERTIES
  LABELS "codegen;078;us3;validator-inline-all-tus")

# ── 078-precompiled-builder-libs T028 [US3]: SC-002 `nm` link-only witness.
# Calls a SUBSET (3) of v44's builders, linking fixpp::builders::v44 --
# proves per-message .o archive granularity (the linker pulls only the 3
# referenced .builder.o members, not the whole version-wide set).
add_executable(test078_nm_link_only_us3 test_078_nm_link_only_us3.cpp)
add_dependencies(test078_nm_link_only_us3 fixpp_codegen_generate fixpp_builders_v44)
target_link_libraries(test078_nm_link_only_us3 PRIVATE
  fixpp::builders::v44
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test078_nm_link_only_us3 PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
add_test(NAME test078_nm_link_only_us3 COMMAND test078_nm_link_only_us3)
set_tests_properties(test078_nm_link_only_us3 PROPERTIES
  LABELS "codegen;078;us3;nm-link-only")

# Discrimination (SC-002): the LINKED BINARY must carry exactly the 3 called
# build_ symbols and NOT the un-called 4th (build_OrderCancelRequest).
if(FIXPP_078_NM_TOOL)
  add_test(
    NAME test078_nm_link_only_us3_nm_check
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXPP_NM=${FIXPP_078_NM_TOOL}"
      "-DFIXPP_TARGET=$<TARGET_FILE:test078_nm_link_only_us3>"
      "-DFIXPP_PRESENT=fixpp::v44::build_NewOrderSingle;fixpp::v44::build_ExecutionReport;fixpp::v44::build_News"
      "-DFIXPP_ABSENT=fixpp::v44::build_OrderCancelRequest"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test_078_nm_assert.cmake"
  )
  set_tests_properties(test078_nm_link_only_us3_nm_check PROPERTIES
    LABELS "codegen;078;us3;nm-link-only")
else()
  message(STATUS "tests/codegen: nm not found; skipping test078_nm_link_only_us3_nm_check")
endif()

# ── 078-precompiled-builder-libs T029 [US4]: `all.hpp` full-set equivalence
# (SC-005/FR-008/FR-012). #include's ONLY <fixpp/v44/all.hpp>; a full 83-entry
# address-of census (reusing builder_completeness_common.hpp + the committed
# generated/v44_builder_completeness_entries.def, mirrors codegen_077_v44_
# builder_completeness_test's wiring) plus a representative byte-identical
# sample.
add_executable(test078_all_hpp_fullset_us4 test_078_all_hpp_fullset_us4.cpp)
add_dependencies(test078_all_hpp_fullset_us4 fixpp_codegen_generate
  fixpp_builders_v44 fixpp_validators_v44)
target_link_libraries(test078_all_hpp_fullset_us4 PRIVATE
  fixpp_wire
  fixpp_dictionary
  pugixml::pugixml
  fixpp::builders::v44
  fixpp::validators::v44
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test078_all_hpp_fullset_us4 PRIVATE
  "${FIXPP_CODEGEN_OUT}"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(test078_all_hpp_fullset_us4 PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries\""
  "FIXPP_CODEGEN_V44_ALL_HPP=\"${FIXPP_CODEGEN_OUT}/fixpp/v44/all.hpp\""
)
add_test(NAME test078_all_hpp_fullset_us4 COMMAND test078_all_hpp_fullset_us4)
set_tests_properties(test078_all_hpp_fullset_us4 PROPERTIES
  LABELS "codegen;078;us4;all-hpp-fullset")

# ── 078-precompiled-builder-libs T031 [US4]: negative check -- the old
# include path (fixpp/<ns>/Builders.hpp) is gone (US4 AC2/SC-005 removal
# leg). Compile-time __has_include guards (v44/v50sp2) + a runtime
# filesystem sweep across all three builder-bearing versions. No builder
# lib needed to link (pure #include/filesystem probe) -- only the codegen
# generation dependency.
add_executable(test078_builders_hpp_removed_us4 test_078_builders_hpp_removed_us4.cpp)
add_dependencies(test078_builders_hpp_removed_us4 fixpp_codegen_generate)
target_link_libraries(test078_builders_hpp_removed_us4 PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(test078_builders_hpp_removed_us4 PRIVATE
  "${FIXPP_CODEGEN_OUT}"
)
target_compile_definitions(test078_builders_hpp_removed_us4 PRIVATE
  "FIXPP_CODEGEN_OUT_DIR=\"${FIXPP_CODEGEN_OUT}\""
)
add_test(NAME test078_builders_hpp_removed_us4 COMMAND test078_builders_hpp_removed_us4)
set_tests_properties(test078_builders_hpp_removed_us4 PROPERTIES
  LABELS "codegen;078;us4;builders-hpp-removed")
