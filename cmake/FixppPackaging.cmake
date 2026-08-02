# ── 084-packaging-cpack-export: CPack configuration ──────────────────────────
#
# Factored out of the top-level CMakeLists.txt (which is already ~360 lines),
# matching the existing cmake/*.cmake convention.
#
# ONE CPack invocation per configured build tree (research R10). The presets are
# all single-config, and the serial build-and-delete discipline plus the storage
# budget forbid two coexisting configurations, so a multi-config generator would
# buy nothing.

include(GNUInstallDirs)

# ── T057 (FR-011a): the codegen tool must be ON for every packaged build ─────
# FIXPP_BUILD_CODEGEN_TOOL defaults ON and is overridden nowhere, so this is
# LATENT today -- pinned here so it STAYS latent. It silently gates TWO things:
# the generated-typed-header install rule, AND the add_test() that registers
# fixpp::consumer::install-witness. A lane that set it OFF would ship a package
# with no typed headers and simultaneously deregister the witness meant to catch
# that -- a skip that reads as green.
if(NOT FIXPP_BUILD_CODEGEN_TOOL)
  message(FATAL_ERROR
    "084 FR-011a: packaging requires FIXPP_BUILD_CODEGEN_TOOL=ON. With it OFF the "
    "package ships no generated typed headers AND the install-witness test is not "
    "registered, so the omission would not be caught by any gate.")
endif()

# ── T062a (FR-011): telemetry provenance -- shipped artifacts are OTel-ON ─────
# FR-011 permits an OTel-OFF dependency build as a DEVELOPMENT ACCELERATOR while
# packaging logic is being written, and forbids one reaching a shipped artifact.
# Nothing enforced the second half: T003 pins OTel-ON only for the new
# linux-gcc-debug preset; the five pre-existing presets inherit the option
# default and were unasserted, so a leftover accelerator configure would ship
# silently -- reproducing exactly the gcc-Debug asymmetry spec Assumption 4
# exists to prevent.
#
# FIXPP_ALLOW_OTEL_OFF_PACKAGE exists ONLY to prove this gate red (it is what the
# packaging::telemetry-provenance test flips). It must never be set in CI or in
# any shipped configuration.
option(FIXPP_ALLOW_OTEL_OFF_PACKAGE
       "TEST-ONLY escape hatch for proving the OTel-ON packaging gate red" OFF)
mark_as_advanced(FIXPP_ALLOW_OTEL_OFF_PACKAGE)
if(FIXPP_BUILD_OTEL)
  set(FIXPP_PACKAGE_TELEMETRY "ON")
else()
  set(FIXPP_PACKAGE_TELEMETRY "OFF")
  if(NOT FIXPP_ALLOW_OTEL_OFF_PACKAGE)
    message(FATAL_ERROR
      "084 FR-011: refusing to configure packaging for an OTel-OFF build. Every "
      "shipped artifact must be built FIXPP_BUILD_OTEL=ON. An OTel-OFF build is "
      "permitted as a development accelerator, but it must not produce a package.")
  endif()
  message(WARNING
    "084 FR-011: packaging an OTel-OFF build because FIXPP_ALLOW_OTEL_OFF_PACKAGE "
    "is set. This is the RED-PROOF path for packaging::telemetry-provenance and "
    "must never be used for a shipped artifact.")
endif()

# ── T051 (FR-017, I10): artifact naming ──────────────────────────────────────
# The name encodes PRODUCT, VERSION, PLATFORM, TOOLCHAIN, CONFIGURATION and
# FORMAT, and must be unique across the whole six-configuration x format matrix.
#
# ⚠️ FORMAT is a NAMING dimension, not merely a uniqueness one: the three Linux
# formats of one configuration are DISTINCT artifacts and each must be
# independently identifiable by name. Encoding only the first five and relying on
# directory placement would ship three same-named DEB/RPM/TGZ artifacts. Here the
# format is carried by the extension each generator appends (.deb/.rpm/.tar.gz/
# .zip), which FR-017 accepts as the format component -- so CPACK_PACKAGE_FILE_NAME
# must NOT be overridden per-generator, or that property is lost.
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _fixpp_pkg_platform)
string(TOLOWER "${CMAKE_CXX_COMPILER_ID}" _fixpp_pkg_toolchain)
string(TOLOWER "${CMAKE_BUILD_TYPE}" _fixpp_pkg_config)
if(NOT _fixpp_pkg_config)
  message(FATAL_ERROR
    "084 FR-017: CMAKE_BUILD_TYPE is empty. The artifact name encodes the "
    "configuration, and an unnamed configuration breaks matrix uniqueness.")
endif()

set(CPACK_PACKAGE_NAME "fixpp")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "fixpp-${PROJECT_VERSION}-${_fixpp_pkg_platform}-${_fixpp_pkg_toolchain}-${_fixpp_pkg_config}")

# ── T049 (FR-018, FR-018c): metadata ─────────────────────────────────────────
# ⚠️ FR-018c constrains the DESCRIPTION wording. Third-party engine compatibility
# is stated AS FACT and must never imply endorsement by, or affiliation with, the
# upstream project: QuickFIX licence clause 4 forbids using its names to endorse
# or promote derived products. (Clause 5 -- derived products may not be called
# "QuickFIX" nor carry it in their name -- is discharged by the product name
# `fixpp`; recorded here so it is not satisfied merely by accident.)
set(CPACK_PACKAGE_VENDOR "the fixpp authors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Modern C++23 FIX protocol library (static libraries, headers and CMake package config)")
set(CPACK_PACKAGE_CONTACT "the fixpp authors")
# The project's OWN licence (constitution Article V §1) -- NOT the licence of the
# redistributed dictionary data, which ships separately as QUICKFIX_LICENSE.txt.
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# ── T050 (FR-018e obligations 2 and 3): dependency metadata ──────────────────
# Versions are read from conanfile.py and cited; never transcribed from an anchor
# doc or a prior spec.
#
# find_dependency LOCATES, it does not PROVIDE -- so this text is the operator's
# only warning before find_package(fixpp) fails at configure time.
#
#   Package            Pin       conanfile.py   ABI character
#   -----------------  --------  -------------  --------------------------------
#   OpenSSL            3.6.2     :69            ABI-stable
#   asio               1.38.0    :67            no ABI surface (header-only)
#   tomlplusplus       3.4.0     :77            no ABI surface (header-only)
#   pugixml            1.15      :66            ABI-fragile (compiled C++)
#   Crc32c             1.1.2     :68            ABI-fragile (compiled C++)
#   opentelemetry-cpp  1.26.0    :94            ABI-fragile, ACUTE -- largest
#                                               surface (7 imported targets on
#                                               fixpp_otel + 3 on fixpp_log_otlp)
#                                               and fastest-moving
set(CPACK_PACKAGE_DESCRIPTION
"fixpp is a modern C++23 implementation of the FIX protocol, shipped as static
libraries with a CMake package configuration.

  find_package(fixpp REQUIRED)
  target_link_libraries(app PRIVATE fixpp::fixpp)

The package is provider-agnostic: it names only imported CMake targets, so its
dependencies resolve against your own CMAKE_PREFIX_PATH by whatever provider you
use. It does NOT bundle them.

YOU MUST SUPPLY, and they must be discoverable by find_package:
  OpenSSL           (tested against 3.6.2)  - ABI-stable
  asio              (tested against 1.38.0) - header-only
  tomlplusplus      (tested against 3.4.0)  - header-only
  pugixml           (tested against 1.15)   - ABI-fragile, compiled C++
  Crc32c            (tested against 1.1.2)  - ABI-fragile; RARELY DISTRO-PACKAGED
  opentelemetry-cpp (tested against 1.26.0) - ABI-fragile; RARELY DISTRO-PACKAGED,
                                              and REQUIRED BY EVERY ARTIFACT this
                                              project ships

The ABI-fragile dependencies offer no cross-version ABI guarantee: build them
with the same toolchain and standard library as this package.

fixpp is an independent implementation, neither affiliated with nor endorsed by
the QuickFIX project. Redistributed FIX dictionary data retains its upstream
licence; see the NOTICE and QUICKFIX_LICENSE.txt files in the package doc
directory.")

# ── T048 (FR-011, FR-015): generators ────────────────────────────────────────
# T056 (FR-016) -- THE WINDOWS INSTALLER SEAM. Windows is ZIP-only for v1.0 by
# user decision. To add an installer format later, append WIX or NSIS to the
# Windows generator list below and set its generator-specific variables; nothing
# else in this file is format-specific, and no consumer-visible contract changes.
# That is the entire seam -- deliberately one line, so the deferral costs nothing
# to reverse. See quickstart.md §8.
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "DEB;RPM;TGZ")
endif()

# ── T052 (FR-020, FR-021, I20/I21): staging and output ───────────────────────
# CPack stages into _CPack_Packages/ INSIDE the build tree, so deleting the tree
# removes the staged files automatically. CMAKE_INSTALL_PREFIX must never point
# at a system location -- violating that silently pollutes the host and is what
# makes the automatic cleanup hold.
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/_packages")

# FR-021: finished artifacts must survive the build-tree deletion that the serial
# build discipline performs between configurations -- CPACK_PACKAGE_DIRECTORY is
# INSIDE the tree, so on its own nothing outlives the cycle. FIXPP_ARTIFACT_DIR is
# the durable location; `cmake --build . --target fixpp-package` builds, packages
# and copies out in one step.
#
# Deliberately a TARGET rather than a post-CPack hook: CPack has no portable
# "after all generators" hook, and a copy that silently did not happen would
# leave T062 asserting over an empty directory.
set(FIXPP_ARTIFACT_DIR "/mnt/wsl/fixppbuild/artifacts" CACHE PATH
    "Durable directory for finished package artifacts (survives build-tree deletion)")

add_custom_target(fixpp-package
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIXPP_ARTIFACT_DIR}"
  COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
  COMMAND "${CMAKE_COMMAND}"
          "-DFIXPP_PACKAGE_DIR=${CPACK_PACKAGE_DIRECTORY}"
          "-DFIXPP_ARTIFACT_DIR=${FIXPP_ARTIFACT_DIR}"
          -P "${CMAKE_SOURCE_DIR}/cmake/FixppCopyArtifacts.cmake"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "084 FR-020/FR-021: package, then copy artifacts to ${FIXPP_ARTIFACT_DIR}"
  VERBATIM)
# ── Internal prefix — Linux ONLY ─────────────────────────────────────────────
# MEASURED 2026-08-02, not assumed: with this set unconditionally, the Windows
# ZIP shipped `fixpp-0.0.1-windows-msvc-release/usr/lib/fixpp_core.lib` — an FHS
# path that means nothing on Windows, with a POSIX permission set applied beneath
# it. A Windows developer package is expected to extract to <root>/{include,lib,
# bin}; `usr/` reads as a packaging bug to any consumer of the ZIP.
#
# DEB and RPM genuinely need /usr — they install into the system tree. The
# archive generators do not: they extract wherever the consumer puts them, and
# the consumer points CMAKE_PREFIX_PATH at that directory.
if(NOT WIN32)
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  # POSIX permission bits are meaningless to the ZIP generator; scoped with the
  # prefix they belong to rather than left applying to a Windows package.
  set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

# ── T053 + T062a (FR-021a, I24): provenance stamping ─────────────────────────
# Configuration + source revision ALONE IS INSUFFICIENT: two packages built from
# the same commit either side of an UNCOMMITTED EDIT are indistinguishable under
# that pair -- and an uncommitted edit is the NORMAL state of a working branch,
# so it is the likely staleness case, not the exotic one. Worktree cleanliness is
# therefore recorded as a third field, reusing the machinery
# tests/codegen/codegen_build_graph_test.cmake already uses.
#
# This is live, not theoretical: the artifact directory deliberately outlives the
# build-tree deletion cycle (FR-021), so packages from earlier configurations and
# earlier source states accumulate alongside current ones.
# ⚠️ TWO FAIL-OPEN BUGS FIXED HERE 2026-08-02 — found while preparing the MSVC
# sandbox, which has no `.git` (the rsync procedure excludes it). This is NOT a
# sandbox-only path: a build from a released SOURCE TARBALL hits it identically.
#
#   1. The `set(... "unknown")` fallbacks were DEAD CODE whenever git EXISTS but
#      the directory is not a repository. `execute_process` assigns OUTPUT_VARIABLE
#      the command's (empty) stdout on failure rather than leaving the prior
#      value, so the revision became "" -- verified directly, not assumed. An
#      artifact then shipped with a BLANK revision, which reads as a formatting
#      glitch rather than as "provenance unavailable".
#   2. Worse: a FAILED `git status --porcelain` also yields empty output, which
#      compared equal to "" and stamped the package **"clean"** -- an affirmative
#      claim of worktree cleanliness that was never verified. Fail-open on the
#      exact field FR-021a added to catch staleness.
#
# Both are fixed by trusting output only when RESULT_VARIABLE says the command
# succeeded, and by never inferring "clean" from a command that did not run.
find_package(Git QUIET)

# Explicit override, for building from a source tree that is legitimately not a
# git repository (source tarball; the rsync-populated MSVC sandbox). Providing a
# revision that CAN be traced is strictly better than stamping "unknown"; leaving
# it unset is still honest, because "unknown" fails T060 by design.
set(FIXPP_PACKAGE_REVISION "" CACHE STRING
    "Override the stamped source revision when the source tree is not a git repo")
set(FIXPP_PACKAGE_WORKTREE "" CACHE STRING
    "Override the stamped worktree state (clean|dirty) alongside FIXPP_PACKAGE_REVISION")

if(FIXPP_PACKAGE_REVISION STREQUAL "")
  set(FIXPP_PACKAGE_REVISION "unknown")
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE _fixpp_git_rev_rc
      OUTPUT_VARIABLE _fixpp_git_rev
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_fixpp_git_rev_rc EQUAL 0 AND NOT _fixpp_git_rev STREQUAL "")
      set(FIXPP_PACKAGE_REVISION "${_fixpp_git_rev}")
    endif()
  endif()
endif()

if(FIXPP_PACKAGE_WORKTREE STREQUAL "")
  set(FIXPP_PACKAGE_WORKTREE "unknown")
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE _fixpp_git_status_rc
      OUTPUT_VARIABLE _fixpp_git_porcelain
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    # "clean" is asserted ONLY when the command actually RAN and reported nothing.
    if(_fixpp_git_status_rc EQUAL 0)
      if(_fixpp_git_porcelain STREQUAL "")
        set(FIXPP_PACKAGE_WORKTREE "clean")
      else()
        set(FIXPP_PACKAGE_WORKTREE "dirty")
      endif()
    endif()
  endif()
endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/fixpp-package-provenance.txt.in"
  "${CMAKE_BINARY_DIR}/fixpp-package-provenance.txt"
  @ONLY)
install(
  FILES "${CMAKE_BINARY_DIR}/fixpp-package-provenance.txt"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}")

# ── T055 (FR-019): Windows debug symbol files ────────────────────────────────
# The Microsoft toolchain emits debug information to SEPARATE symbol files
# alongside its static libraries, not inside the archive members -- so a Windows
# Debug package built with Linux-shaped install rules ships UNDEBUGGABLE
# libraries. THERE IS NO LINUX COUNTERPART TO THIS RULE: on Linux the compiler
# writes DWARF into the .o members of the .a, and the archiver copies it.
#
# ⚠️ The exact artifact naming and location MUST be verified against real MSVC
# output during the Windows legs; this rule is written from the documented
# behaviour and is NOT yet measured.
if(MSVC)
  install(
    DIRECTORY "${CMAKE_BINARY_DIR}/lib/"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILES_MATCHING PATTERN "*.pdb")
endif()

# ── Generator-specific ───────────────────────────────────────────────────────
set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
set(CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0")
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
# ⚠️ CPACK_DEBIAN_FILE_NAME / CPACK_RPM_FILE_NAME are deliberately NOT set to
# DEB-DEFAULT / RPM-DEFAULT. Those switch each generator to its distribution
# naming convention (fixpp_0.0.1_amd64.deb), which carries NEITHER the toolchain
# NOR the configuration -- so linux-gcc-release and linux-clang-debug would
# produce IDENTICALLY NAMED .deb files and silently overwrite each other in the
# shared artifact directory. That is precisely the collision FR-017/I10 forbids
# and the silent omission SC-003 exists to catch. Leaving them unset makes both
# generators inherit CPACK_PACKAGE_FILE_NAME, which encodes all six dimensions.
# The install tree writes into /usr/{include,lib,share}; without these the RPM
# generator claims ownership of directories the distribution already owns.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/include;/usr/lib;/usr/share;/usr/share/doc")

include(CPack)
