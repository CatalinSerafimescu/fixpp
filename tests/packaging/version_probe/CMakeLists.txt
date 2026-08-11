# tests/packaging/version_probe/CMakeLists.txt — 084 T036 (FR-006 / SC-006).
#
# A deliberately INCOMPATIBLE consumer. It is configured by
# run_version_mismatch_witness.cmake against a staged install of fixpp 0.0.1 and
# MUST FAIL AT CONFIGURE TIME, inside find_package(), with a version-specific
# diagnostic -- not at build, not at link, and not with a generic "package not
# found" that would pass for the wrong reason.
#
# The requested version is injected as FIXPP_PROBE_VERSION so the same project
# serves both legs of the test: the RED leg asks for an incompatible major, and
# the control leg asks for the installed version and must SUCCEED. Without the
# control leg a broken fixppConfig.cmake -- or a misspelled package name -- would
# also "fail at configure" and the red leg would pass for entirely the wrong
# reason.
cmake_minimum_required(VERSION 3.28)
project(fixpp_version_probe CXX)

if(NOT FIXPP_STAGE_PREFIX)
  message(FATAL_ERROR "FIXPP_STAGE_PREFIX must be set")
endif()
if(NOT FIXPP_PROBE_VERSION)
  message(FATAL_ERROR "FIXPP_PROBE_VERSION must be set")
endif()

list(PREPEND CMAKE_PREFIX_PATH "${FIXPP_STAGE_PREFIX}")

find_package(fixpp ${FIXPP_PROBE_VERSION} REQUIRED)

message(STATUS "fixpp_version_probe: find_package(fixpp ${FIXPP_PROBE_VERSION}) SUCCEEDED")
