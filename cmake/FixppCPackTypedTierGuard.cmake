# cmake/FixppCPackTypedTierGuard.cmake — #613 (`.specify/613-builders-export.md` D-0).
#
# CPACK_PROJECT_CONFIG_FILE: cpack reads this file when it initialises each generator,
# so it runs for EVERY `cpack --config <build>/CPackConfig.cmake` invocation -- the
# fixpp-package target's command and each packaging witness that executes cpack. A
# guard on the wrapper target alone would miss the witnesses.
#
# Owner rulings: R-2 keeps "all four versions, every platform"; R-2a
# "Release packages only."; R-2b put the typed tier behind FIXPP_INSTALL_TYPED_TIER,
# in effect by default for Release, and stated that Debug configurations are never
# released. So a `release` package must carry the typed tier, and this refuses to
# generate one without it.
#
# Deliberately NOT an install(CODE) block: every `cmake --install` runs those, the
# consumer witness's stage install included, and a Release tree configured OFF is
# permitted to install. Deliberately NOT a configure-time FATAL either (see the
# comment beside include(CPack) in cmake/FixppPackaging.cmake).
#
# Inputs, written into CPackConfig.cmake by cmake/FixppPackaging.cmake:
#   CPACK_FIXPP_PKG_CONFIG            the lowercased build type in the artifact name
#   CPACK_FIXPP_TYPED_TIER_EFFECTIVE  FIXPP_INSTALL_TYPED_TIER_EFFECTIVE (ON|OFF)
if(NOT CPACK_FIXPP_TYPED_TIER_EFFECTIVE STREQUAL "ON" AND NOT CPACK_FIXPP_TYPED_TIER_EFFECTIVE STREQUAL "OFF")
  message(FATAL_ERROR
    "#613: CPACK_FIXPP_TYPED_TIER_EFFECTIVE is '${CPACK_FIXPP_TYPED_TIER_EFFECTIVE}', expected ON or OFF. "
    "cmake/FixppPackaging.cmake must set it before include(CPack); refusing to package "
    "without knowing whether the typed tier is in the payload.")
endif()
if(CPACK_FIXPP_PKG_CONFIG STREQUAL "release" AND CPACK_FIXPP_TYPED_TIER_EFFECTIVE STREQUAL "OFF")
  message(FATAL_ERROR
    "#613: refusing to generate a release package without the typed builder/validator tier: "
    "FIXPP_INSTALL_TYPED_TIER resolved OFF in this Release tree. Released packages carry it "
    "(owner rulings R-2/R-2a/R-2b, `.specify/613-builders-export.md`). Reconfigure with "
    "-DFIXPP_INSTALL_TYPED_TIER=AUTO (or ON) to package; an OFF Release tree may still be "
    "configured, built and installed, and its non-packaging tests run with -LE packaging.")
endif()
