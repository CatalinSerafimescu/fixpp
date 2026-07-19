# tools/quickfix_required_golden/CMakeLists.txt
#
# 079-required-presence-scope -- T018. Local-only golden generator: links a
# REAL, locally built QuickFIX v1.16.0 to run the 9 QuickFIX-schema
# dictionaries through the real FIX::DataDictionary::isRequiredField() and
# produce the checked-in tools/quickfix_required_golden/golden.csv
# (Contract 2, census-and-agreement.md).
#
# Guarded OFF by default, sharing the SAME FIXPP_BUILD_QUICKFIX_GOLDEN option
# + FIXPP_QUICKFIX_ROOT cache var as tools/quickfix_enum_golden (075 T002) --
# option()/CACHE re-declaration with the same name is idempotent, so turning
# the shared flag ON builds BOTH generators. reference-engines/ lives OUTSIDE
# the submodule's git boundary (three levels up from this file) and is never
# present in CI -- but that absence is NOT what keeps CI away from this
# target; the option default (OFF) plus the hard configure-time error below
# is the structural guard (mirrors 075 T002 / Gate A round 1 finding O-4).

option(FIXPP_BUILD_QUICKFIX_GOLDEN
  "Build the local-only QuickFIX v1.16.0 golden generator(s) (075/079). NEVER enabled in CI."
  OFF)

if(FIXPP_BUILD_QUICKFIX_GOLDEN)
  set(FIXPP_QUICKFIX_ROOT "${CMAKE_SOURCE_DIR}/../../../reference-engines/quickfix-cpp" CACHE PATH
    "Root of a locally built QuickFIX v1.16.0 checkout (outside the submodule's git boundary). Must contain include/quickfix/*.h and lib/libquickfix.so.")

  if(NOT EXISTS "${FIXPP_QUICKFIX_ROOT}/lib/libquickfix.so")
    message(FATAL_ERROR
      "FIXPP_BUILD_QUICKFIX_GOLDEN=ON but '${FIXPP_QUICKFIX_ROOT}/lib/libquickfix.so' does not "
      "exist. This hard error -- not a .gitignore claim -- is the structural guard that keeps CI "
      "from ever building this target: reference-engines/ lives outside the submodule's git "
      "boundary and is never present in CI. Point -DFIXPP_QUICKFIX_ROOT at a local QuickFIX "
      "v1.16.0 build, or leave FIXPP_BUILD_QUICKFIX_GOLDEN=OFF (the default).")
  endif()

  find_package(OpenSSL 3.0 REQUIRED)
  find_package(pugixml CONFIG REQUIRED)

  if(NOT TARGET quickfix_enum_golden_ext)
    add_library(quickfix_enum_golden_ext SHARED IMPORTED)
    set_target_properties(quickfix_enum_golden_ext PROPERTIES
      IMPORTED_LOCATION "${FIXPP_QUICKFIX_ROOT}/lib/libquickfix.so"
      INTERFACE_INCLUDE_DIRECTORIES "${FIXPP_QUICKFIX_ROOT}/include")
  endif()

  add_executable(quickfix_required_golden main.cpp)

  target_link_libraries(quickfix_required_golden PRIVATE
    quickfix_enum_golden_ext
    OpenSSL::Crypto
    pugixml::pugixml
  )

  # The imported QuickFIX .so is not installed anywhere on the loader path;
  # rpath it explicitly so the guarded, local-only binary can actually run.
  set_target_properties(quickfix_required_golden PROPERTIES
    BUILD_RPATH "${FIXPP_QUICKFIX_ROOT}/lib"
    INSTALL_RPATH "${FIXPP_QUICKFIX_ROOT}/lib")

  set(FIXPP_REQUIRED_GOLDEN_DICT_DIR "${CMAKE_SOURCE_DIR}/dictionaries")

  target_compile_definitions(quickfix_required_golden PRIVATE
    "FIXPP_REQUIRED_GOLDEN_DICT_DIR=\"${FIXPP_REQUIRED_GOLDEN_DICT_DIR}\""
    "FIXPP_GOLDEN_GENERATOR_SOURCE=\"${CMAKE_CURRENT_SOURCE_DIR}/main.cpp\""
    "FIXPP_REQUIRED_GOLDEN_OUTPUT_CSV=\"${CMAKE_CURRENT_SOURCE_DIR}/golden.csv\""
    # 081-strict-validation-residuals T020: per-group parity golden (SEPARATE
    # file from golden.csv -- see main.cpp's T020 header-comment banner).
    "FIXPP_REQUIRED_GOLDEN_GROUPS_OUTPUT_CSV=\"${CMAKE_CURRENT_SOURCE_DIR}/golden_groups.csv\"")

  # Host tool only: never installed, never linked into the user-facing
  # library, never part of a default preset.

  # ── regeneration-and-diff target (mirrors 075 T009) ─────────────────────
  # Guarded the SAME way as quickfix_required_golden itself (nested inside
  # this if(FIXPP_BUILD_QUICKFIX_GOLDEN) block) -- never defined, let alone
  # run, in CI. When `reference-engines/` is present and this option is
  # explicitly turned on, `cmake --build <preset> --target
  # quickfix_required_golden_regen_diff` rebuilds the golden against a real
  # (possibly newer) QuickFIX and fails loudly on any drift from the
  # checked-in golden.csv. Bound to the per-release interop-run checklist
  # (specs/016-interop-harness/quickstart.md Sec 3 "Full gate
  # (release-prep)") -- a diff is a RELEASE-BLOCKING finding, not a routine
  # update.
  add_custom_target(quickfix_required_golden_regen_diff
    COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/regen_and_diff.py"
            "--binary" "$<TARGET_FILE:quickfix_required_golden>"
            "--golden" "${CMAKE_CURRENT_SOURCE_DIR}/golden.csv"
    DEPENDS quickfix_required_golden
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "079 T018: regenerating tools/quickfix_required_golden/golden.csv against the local QuickFIX build and diffing for drift"
    VERBATIM
  )
endif()
