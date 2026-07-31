# tests/dictionary/CMakeLists.txt
# 068-test-binary-grouping: isolation-safe dictionary tests are grouped into
# whole-binary add_test executables (bucketed by shared link-deps ∩ the
# "dictionary" label); isolation-sensitive tests stay standalone. Each grouped
# binary carries LABELS "dictionary" so `ctest -L dictionary` selects the
# grouped entries which each run all their cases. Dispositions + `-R`→`-L`
# replacements are recorded in specs/068-test-binary-grouping/dispositions.md.
# Reference precedent: tests/core/CMakeLists.txt.

find_package(GTest CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

# Path to vendored XML fixtures so tests can locate them regardless of
# build-tree depth (used by load(path, mr) AC-L1 / AC-L2 paths and the
# round-trip / determinism / dictionary_lookup suites).
set(FIXPP_DICT_DATA_DIR "${CMAKE_SOURCE_DIR}/dictionaries")

# ── Grouped bucket: pure/stateless dictionary tests (15 .cpp → 1 binary) ──────
# Label-homogeneous "dictionary". pugixml is unioned in for round_trip and
# reused_tag_census (raw-XML ground-truth scans); harmless for the others.
add_executable(dictionary_pure_tests
  ref_shape_test.cpp
  loader_disposition_test.cpp     # 083 T035/T042: FR-006 / FR-006a / FR-023 dispositions
  xml_loader_test.cpp
  xml_loader_enum_test.cpp        # 075 T012/T013: enum-value parse + loader strictness
  xml_enum_codeset_test.cpp       # 075 T028: enum_values() census + exposure witness
  round_trip_test.cpp
  determinism_test.cpp
  negative_paths_test.cpp
  parser_error_test.cpp
  lookup_test.cpp
  version_profile_test.cpp
  field_traits_test.cpp
  version_registry_test.cpp
  table_view_test.cpp
  required_scope_test.cpp          # fixpp#201: message-level required scope (group leak)
  defect_a_group_context_test.cpp
  reused_tag_census_test.cpp
  collision_membership_guards_test.cpp
  test_072_delimiter_guard_test.cpp  # 072 US1: load-time nested==parent delimiter guard witness
  oom_injection_test.cpp          # local failing_pmr_resource seam — no global state
)
target_link_libraries(dictionary_pure_tests PRIVATE
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(dictionary_pure_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(dictionary_pure_tests PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
  # 083 T035 (W-6): loader_disposition_test.cpp loads all TEN shipped
  # dictionaries under the fail-closed default, Orchestra included. Spelled out
  # rather than reusing FIXPP_ORCHESTRA_DATA_DIR, which this file sets further
  # down (line ~98) and so would expand empty here.
  "FIXPP_ORCHESTRA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/dictionaries/orchestra\"")
add_test(NAME dictionary_pure_tests COMMAND dictionary_pure_tests)
set_tests_properties(dictionary_pure_tests PROPERTIES LABELS "dictionary;075")

# ── Standalone: isolation-sensitive dictionary tests (FR-002) ─────────────────
# Each keeps its own executable + add_test(NAME <target>) so its name, labels,
# and special properties survive. Reasons in dispositions.md.
set(FIXPP_DICT_STANDALONE
  dictionary_pmr_allocation_test                   # in-TU global-new alloc counting
  dictionary_concurrent_readers_test              # genuinely concurrent (std::thread)
  dictionary_group_context_lookup_alloc_gate_test # alloc gate + mallocnesia -R by name
)
foreach(target IN LISTS FIXPP_DICT_STANDALONE)
  string(REGEX REPLACE "^dictionary_" "" src_stem "${target}")
  add_executable(${target} "${src_stem}.cpp")
  target_link_libraries(${target} PRIVATE
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/tests")
  target_compile_definitions(${target} PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\"")
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES LABELS dictionary)
endforeach()

# ── 075-live-wire-enum-validation: table_view::enum_valid unit witnesses ─────
# Standalone (new; needs its own "075" label so `ctest -L 075` selects it
# without widening the "dictionary"-only pure bucket's LABELS). T017/T018/T019.
add_executable(dictionary_table_view_enum_domain_test table_view_enum_domain_test.cpp)
target_link_libraries(dictionary_table_view_enum_domain_test PRIVATE
  fixpp_dictionary
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME dictionary_table_view_enum_domain_test COMMAND dictionary_table_view_enum_domain_test)
set_tests_properties(dictionary_table_view_enum_domain_test PROPERTIES LABELS "075;dictionary")

# ── 074-orchestra-native-reader: native Orchestra reader test bucket ─────────
# Grouped whole-binary executable (const §VII.8), LABELS "dictionary;orchestra";
# selected by `ctest -L orchestra`. NO gtest_discover_tests. pugixml is unioned
# in for raw-XML ground-truth scans (T012/T022). FIXPP_ORCHESTRA_DATA_DIR points
# at the vendored OrchestraFIXLatest.xml; FIXPP_DICT_DATA_DIR kept for the
# T024/T022 QuickFIX-XML no-regression + wrong-grammar pins.
set(FIXPP_ORCHESTRA_DATA_DIR "${CMAKE_SOURCE_DIR}/dictionaries/orchestra")

# 074 T021 (FR-007/SC-007) — supply-chain integrity gate: recompute the sha1 of
# the vendored OrchestraFIXLatest.xml at configure time (portable file(SHA1),
# Linux + MSVC) and hard-fail if it drifts from the spike's grade-1 pin. This is
# the "scripted check" arm of T021; the gtest OrchestraProvenance covers the
# UPSTREAM/LICENSE/NOTICE artifacts.
set(FIXPP_ORCHESTRA_XML "${FIXPP_ORCHESTRA_DATA_DIR}/OrchestraFIXLatest.xml")
set(FIXPP_ORCHESTRA_SHA1_PIN "26f60db1c1f52d169d3b6825ac68800abf487fde")
if(NOT EXISTS "${FIXPP_ORCHESTRA_XML}")
  message(FATAL_ERROR "074: vendored Orchestra source missing: ${FIXPP_ORCHESTRA_XML}")
endif()
file(SHA1 "${FIXPP_ORCHESTRA_XML}" _fixpp_orchestra_sha1)
if(NOT _fixpp_orchestra_sha1 STREQUAL "${FIXPP_ORCHESTRA_SHA1_PIN}")
  message(FATAL_ERROR
    "074 supply-chain integrity FAIL: ${FIXPP_ORCHESTRA_XML}\n"
    "  computed sha1 = ${_fixpp_orchestra_sha1}\n"
    "  pinned  sha1 = ${FIXPP_ORCHESTRA_SHA1_PIN}\n"
    "  The vendored file is wrong or tampered — do NOT proceed; re-vendor from the pinned commit.")
endif()

add_executable(dictionary_orchestra_tests
  orchestra_loader_test.cpp
)
target_link_libraries(dictionary_orchestra_tests PRIVATE
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(dictionary_orchestra_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(dictionary_orchestra_tests PRIVATE
  "FIXPP_ORCHESTRA_DATA_DIR=\"${FIXPP_ORCHESTRA_DATA_DIR}\""
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\"")
add_test(NAME dictionary_orchestra_tests COMMAND dictionary_orchestra_tests)
set_tests_properties(dictionary_orchestra_tests PROPERTIES LABELS "dictionary;orchestra")

# ── 080-orchestra-runtime-load: dict::load_any shared dispatch helper ───────
# Grouped whole-binary executable (const §VII.8), LABELS "dictionary;orchestra;080";
# selected by `ctest -L 080`. Mirrors the dictionary_orchestra_tests block
# above; reuses the same FIXPP_ORCHESTRA_DATA_DIR / FIXPP_DICT_DATA_DIR vars.
add_executable(dictionary_load_any_tests
  load_any_test.cpp
)
target_link_libraries(dictionary_load_any_tests PRIVATE
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(dictionary_load_any_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(dictionary_load_any_tests PRIVATE
  "FIXPP_ORCHESTRA_DATA_DIR=\"${FIXPP_ORCHESTRA_DATA_DIR}\""
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
  # 080 T015 (FR-004 single-dispatch source-inspection gate): repo src root so
  # the gate can read the two call-site TUs + the sole sniff TU verbatim.
  "FIXPP_SRC_DIR=\"${CMAKE_SOURCE_DIR}/src\"")
add_test(NAME dictionary_load_any_tests COMMAND dictionary_load_any_tests)
set_tests_properties(dictionary_load_any_tests PROPERTIES LABELS "dictionary;orchestra;080")

# ── 075-live-wire-enum-validation T021: dictionary census gates ──────────────
# STANDALONE (const §VII.8) — an exact-set completeness gate is
# isolation-sensitive. Scans the shipped dictionaries/*.xml directly via
# pugixml (SC-010/SC-011) rather than a hand-maintained expectation list.
# Reuses reused_tag_census.hpp's kRuntimeDicts (the nine) and
# FIXPP_ORCHESTRA_DATA_DIR (set above by the 074 block).
add_executable(dictionary_dict_enum_census_test dict_enum_census_test.cpp)
target_link_libraries(dictionary_dict_enum_census_test PRIVATE
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(dictionary_dict_enum_census_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(dictionary_dict_enum_census_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
  "FIXPP_ORCHESTRA_DATA_DIR=\"${FIXPP_ORCHESTRA_DATA_DIR}\"")
add_test(NAME dictionary_dict_enum_census_test COMMAND dictionary_dict_enum_census_test)
set_tests_properties(dictionary_dict_enum_census_test PROPERTIES LABELS "075;dictionary;census")

# ── 079-required-presence-scope T015-T017: required-set census (Contract 1 +
# 1a) — STANDALONE (const §VII.8) exact-set completeness gate, selected via
# `-R required_scope_census` (Article VII §8 carve-out). Links the codegen
# tool's ir.cpp directly (mirrors codegen_067_emit_builders_unit_test —
# tests/codegen/CMakeLists.txt) for the IR safety-net leg; host-tool-only, no
# generated-header dependency (fixpp_codegen_generate NOT required).
add_executable(dictionary_required_scope_census_test
  required_scope_census_test.cpp
  "${CMAKE_SOURCE_DIR}/tools/codegen/fixpp-codegen/ir.cpp"
)
target_include_directories(dictionary_required_scope_census_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/tools/codegen/fixpp-codegen"
)
target_link_libraries(dictionary_required_scope_census_test PRIVATE
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(dictionary_required_scope_census_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
  "FIXPP_ORCHESTRA_DATA_DIR=\"${FIXPP_ORCHESTRA_DATA_DIR}\"")
add_test(NAME required_scope_census COMMAND dictionary_required_scope_census_test)
set_tests_properties(required_scope_census PROPERTIES LABELS "dictionary;079;census")

# ── 081-strict-validation-residuals T005/T006: FIXT.1.1 framing-table
# non-circular census (Concern A) — STANDALONE (const §VII.8) exact-set
# completeness gate, mirroring required_scope_census_test above. Includes the
# private production header src/dictionary/fixt_framing_table.hpp via an
# explicit target_include_directories seam (same pattern as ir.hpp above).
add_executable(dictionary_fixt_header_merge_test fixt_header_merge_test.cpp)
target_include_directories(dictionary_fixt_header_merge_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
  "${CMAKE_SOURCE_DIR}/src/dictionary")
target_link_libraries(dictionary_fixt_header_merge_test PRIVATE
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(dictionary_fixt_header_merge_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\"")
add_test(NAME fixt_header_merge COMMAND dictionary_fixt_header_merge_test)
set_tests_properties(fixt_header_merge PROPERTIES LABELS "dictionary;081;census")

# ── 083-group-delimiter-resolution T006: all-ten delimiter census (FR-002/
# FR-010/FR-012/FR-013/FR-015) — STANDALONE (const §VII.8) exact-set
# completeness gate, mirroring required_scope_census_test above. Foundational
# RED pin: the assertions express the POST-FIX target (every defect bucket
# zero), so this is expected to FAIL until Phase 3/5's loader fix lands (see
# spec.md's Baseline table: 335 wrong-delimiter / 52 polluted / 30
# unregistered measured on today's tree). Own TIMEOUT — this sweeps a
# comparable corpus to required_scope_census (~35s) plus a second discovery
# discrimination pass per context, so generous headroom.
add_executable(dictionary_delimiter_census_test delimiter_census_test.cpp)
target_include_directories(dictionary_delimiter_census_test PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(dictionary_delimiter_census_test PRIVATE
  fixpp_dictionary
  pugixml::pugixml
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(dictionary_delimiter_census_test PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
  "FIXPP_ORCHESTRA_DATA_DIR=\"${FIXPP_ORCHESTRA_DATA_DIR}\"")
add_test(NAME delimiter_census COMMAND dictionary_delimiter_census_test)
set_tests_properties(delimiter_census PROPERTIES LABELS "dictionary;083;census" TIMEOUT 180)

# mallocnesia gate for the group-context lookup no-alloc test — re-runs the
# standalone binary under LD_PRELOAD so global malloc/calloc/realloc is
# intercepted (dual gate alongside the in-TU global operator-new counter; absent
# libmallocnesia.so → skipped, per feedback_tracking_pmr_resource_false_pass).
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
  add_test(
    NAME dictionary_group_context_lookup_alloc_gate_test_mallocnesia
    COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:dictionary_group_context_lookup_alloc_gate_test>")
  set_tests_properties(dictionary_group_context_lookup_alloc_gate_test_mallocnesia PROPERTIES
    LABELS "dictionary;alloc_guard;mallocnesia;063")
endif()

# ── US2 Reify tests ─────────────────────────────────────────────────────────
# These tests include generated <vXX>/Reify.hpp (build-tree only, AC-C4).
# fixpp_codegen_generate is defined in tests/codegen/CMakeLists.txt, which is
# add_subdirectory'd BEFORE tests/dictionary in the top-level CMakeLists.txt.
# The generated include dir is ${CMAKE_BINARY_DIR}/_codegen/include.
if(TARGET fixpp_codegen_generate)
  set(_REIFY_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")

  # Grouped reify bucket (4 .cpp → 1 binary), label "dictionary". All four use
  # only local pmr seams / anon-namespace helpers (isolation-safe, D3/D5).
  add_executable(dictionary_reify_tests
    reify_test.cpp
    reify_move_test.cpp
    reify_dispatch_test.cpp
    reify_oom_test.cpp            # local failing/counting pmr — no global new-handler
  )
  add_dependencies(dictionary_reify_tests fixpp_codegen_generate)
  target_link_libraries(dictionary_reify_tests PRIVATE
    fixpp_dictionary
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(dictionary_reify_tests PRIVATE
    "${_REIFY_CODEGEN_INC}"
    "${CMAKE_SOURCE_DIR}/tests")
  target_compile_definitions(dictionary_reify_tests PRIVATE
    "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\"")
  add_test(NAME dictionary_reify_tests COMMAND dictionary_reify_tests)
  set_tests_properties(dictionary_reify_tests PROPERTIES LABELS dictionary)

  # Standalone reify tests (FR-002): a TSan seam + two label-heterogeneous 066
  # witnesses (their distinct label set forbids grouping under the "dictionary"
  # bucket — over-selection guard, D4).
  set(FIXPP_REIFY_STANDALONE
    reify_cross_strand_test           # TSan target (LABELS dictionary;tsan) + threads
    reify_membership_identity_test    # label-heterogeneous 066;dictionary;us1
    reify_membership_copy_oom_test    # global-new alloc counting + 066;dictionary;us1
  )
  foreach(target IN LISTS FIXPP_REIFY_STANDALONE)
    add_executable(dictionary_${target} "${target}.cpp")
    add_dependencies(dictionary_${target} fixpp_codegen_generate)
    target_link_libraries(dictionary_${target} PRIVATE
      fixpp_dictionary
      GTest::gtest
      GTest::gtest_main
    )
    target_include_directories(dictionary_${target} PRIVATE
      "${_REIFY_CODEGEN_INC}"
      "${CMAKE_SOURCE_DIR}/tests")
    target_compile_definitions(dictionary_${target} PRIVATE
      "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\"")
    add_test(NAME dictionary_${target} COMMAND dictionary_${target})
    set_tests_properties(dictionary_${target} PROPERTIES LABELS dictionary)
  endforeach()

  # reify_cross_strand_test is a TSan target per [const §IX.2] / AC-R5/AC-T3.
  set_tests_properties(dictionary_reify_cross_strand_test PROPERTIES
    LABELS "dictionary;tsan")

  # 066-dict-backed-inbound-parse T008: label override so `ctest -L 066`
  # (and the T021/T022 coverage-index / completeness audit) picks these up.
  set_tests_properties(dictionary_reify_membership_identity_test PROPERTIES
    LABELS "066;dictionary;us1")
  set_tests_properties(dictionary_reify_membership_copy_oom_test PROPERTIES
    LABELS "066;dictionary;us1")
else()
  message(STATUS
    "tests/dictionary: fixpp_codegen_generate not available; skipping US2 reify tests")
endif()
