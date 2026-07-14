# tools/quickfix_enum_golden/CMakeLists.txt
#
# 075-live-wire-enum-validation -- Phase 0.5, T002. Local-only golden
# generator: links a REAL, locally built QuickFIX v1.16.0 to run the FR-018
# corpus through FIX::DataDictionary::validate() and produce the checked-in
# tools/quickfix_enum_golden/golden.csv (FR-024).
#
# Guarded OFF by default (FIXPP_BUILD_QUICKFIX_GOLDEN). reference-engines/
# lives OUTSIDE the submodule's git boundary (three levels up from this
# file) and is never present in CI -- but that absence is NOT what keeps CI
# away from this target; the option default (OFF) plus the hard configure-time
# error below is the structural guard (075 T002, Gate A round 1 finding O-4).

option(FIXPP_BUILD_QUICKFIX_GOLDEN
  "Build the local-only QuickFIX v1.16.0 enum-domain golden generator (075 Phase 0.5). NEVER enabled in CI."
  OFF)

if(FIXPP_BUILD_QUICKFIX_GOLDEN)
  set(FIXPP_QUICKFIX_ROOT "${CMAKE_SOURCE_DIR}/../../../reference-engines/quickfix-cpp" CACHE PATH
    "Root of a locally built QuickFIX v1.16.0 checkout (outside the submodule's git boundary). Must contain include/quickfix/*.h and lib/libquickfix.so.")

  if(NOT EXISTS "${FIXPP_QUICKFIX_ROOT}/lib/libquickfix.so")
    message(FATAL_ERROR
      "FIXPP_BUILD_QUICKFIX_GOLDEN=ON but '${FIXPP_QUICKFIX_ROOT}/lib/libquickfix.so' does not "
      "exist. This hard error -- not a .gitignore claim -- is the structural guard (075 T002) "
      "that keeps CI from ever building this target: reference-engines/ lives outside the "
      "submodule's git boundary and is never present in CI. Point -DFIXPP_QUICKFIX_ROOT at a "
      "local QuickFIX v1.16.0 build, or leave FIXPP_BUILD_QUICKFIX_GOLDEN=OFF (the default).")
  endif()

  find_package(OpenSSL 3.0 REQUIRED)

  add_library(quickfix_enum_golden_ext SHARED IMPORTED)
  set_target_properties(quickfix_enum_golden_ext PROPERTIES
    IMPORTED_LOCATION "${FIXPP_QUICKFIX_ROOT}/lib/libquickfix.so"
    INTERFACE_INCLUDE_DIRECTORIES "${FIXPP_QUICKFIX_ROOT}/include")

  add_executable(quickfix_enum_golden main.cpp)

  target_link_libraries(quickfix_enum_golden PRIVATE quickfix_enum_golden_ext OpenSSL::Crypto)

  # The imported QuickFIX .so is not installed anywhere on the loader path;
  # rpath it explicitly so the guarded, local-only binary can actually run.
  set_target_properties(quickfix_enum_golden PROPERTIES
    BUILD_RPATH "${FIXPP_QUICKFIX_ROOT}/lib"
    INSTALL_RPATH "${FIXPP_QUICKFIX_ROOT}/lib")

  target_compile_definitions(quickfix_enum_golden PRIVATE
    FIXPP_GOLDEN_DICT_FIX44="${CMAKE_SOURCE_DIR}/dictionaries/FIX44.xml"
    FIXPP_GOLDEN_DICT_FIX41="${CMAKE_SOURCE_DIR}/dictionaries/FIX41.xml"
    FIXPP_GOLDEN_DICT_FIX42="${CMAKE_SOURCE_DIR}/dictionaries/FIX42.xml"
    FIXPP_GOLDEN_GENERATOR_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    FIXPP_GOLDEN_OUTPUT_CSV="${CMAKE_CURRENT_SOURCE_DIR}/golden.csv")

  # Host tool only: never installed, never linked into the user-facing
  # library, never part of a default preset.

  # ── T009: regeneration-and-diff target (FR-024 item 3) ────────────────────
  # Guarded the SAME way as quickfix_enum_golden itself (nested inside this
  # if(FIXPP_BUILD_QUICKFIX_GOLDEN) block -- it depends on that executable
  # existing) -- never defined, let alone run, in CI. When
  # `reference-engines/` is present and this option is explicitly turned on,
  # `cmake --build <preset> --target quickfix_enum_golden_regen_diff`
  # rebuilds the golden against a real (possibly newer) QuickFIX and fails
  # loudly on any drift from the checked-in golden.csv. This is the ONLY
  # mechanism able to catch drift against a newer QuickFIX release -- the CI
  # manifest gate (T007, tests/wire/enum_golden_manifest_test.cpp) provably
  # cannot, since it links no QuickFIX by design. Bound to the per-release
  # interop-run checklist: specs/016-interop-harness/quickstart.md Sec 3
  # "Full gate (release-prep)" -- run on every release-interop pass; a diff
  # is a RELEASE-BLOCKING finding, not a routine update.
  add_custom_target(quickfix_enum_golden_regen_diff
    COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/regen_and_diff.py"
            "--binary" "$<TARGET_FILE:quickfix_enum_golden>"
            "--golden" "${CMAKE_CURRENT_SOURCE_DIR}/golden.csv"
    DEPENDS quickfix_enum_golden
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "075 T009: regenerating tools/quickfix_enum_golden/golden.csv against the local QuickFIX build and diffing for drift"
    VERBATIM
  )
endif()
