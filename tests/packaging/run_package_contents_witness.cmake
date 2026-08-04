# tests/packaging/run_package_contents_witness.cmake — 084 T058
# (FR-009, FR-012, FR-013, FR-018d, SC-004, SC-013).
#
# ENUMERATES PRODUCED PACKAGE CONTENTS. It must never inspect install rules: a
# rule whose PATTERN matches nothing yields a package missing content while
# looking entirely correct in CMake, and for the attribution set that is a LEGAL
# deficiency, not a cosmetic one.
#
# Three internal layouts, measured 2026-08-02 — normalise before comparing:
#   DEB  ./usr/lib/...
#   RPM  /usr/lib/...
#   TGZ  fixpp-<version>-<platform>-<toolchain>-<config>/usr/lib/...
#   ZIP  fixpp-<version>-windows-msvc-<config>/lib/...   (NO usr/ — Windows)
#
# Expected artifacts are derived BY MEMBER KIND, not from a flat "the exported
# static libraries": of the 18 export members, 13 are STATIC (one archive each),
# 3 are INTERFACE (no compiled output at all), 1 is an OBJECT library (loose
# objects, no archive) and the umbrella is INTERFACE. A gate written from the flat
# phrase either passes trivially ("at least one .a" — cannot catch a genuinely
# missing archive) or fails spuriously (one archive per member — impossible).

# Required for `IN_LIST`: a `cmake -P` script has no project(), so CMP0057 is
# unset and the if(... IN_LIST ...) form is a hard error rather than a fallback.
cmake_minimum_required(VERSION 3.28)

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_WORK_DIR FIXPP_SOURCE_DIR FIXPP_PROJECT_VERSION)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_package_contents_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_pkgdir "${FIXPP_WORK_DIR}/packages")
file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")
file(MAKE_DIRECTORY "${_pkgdir}")

# ── Produce the packages ─────────────────────────────────────────────────────
execute_process(
  COMMAND "${CMAKE_CPACK_COMMAND}" --config "${FIXPP_MAIN_BUILD_DIR}/CPackConfig.cmake"
          -B "${_pkgdir}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "cpack failed (exit ${_rc}):\n${_out}\n${_err}")
endif()

file(GLOB _artifacts "${_pkgdir}/*.deb" "${_pkgdir}/*.rpm" "${_pkgdir}/*.tar.gz" "${_pkgdir}/*.zip")
if(_artifacts STREQUAL "")
  message(FATAL_ERROR "T058: cpack exited 0 but produced no artifacts in ${_pkgdir}")
endif()
set(_expected_exts "")
set(_platform_token "")
set(_actual_exts "")
foreach(_artifact IN LISTS _artifacts)
  get_filename_component(_artifact_name "${_artifact}" NAME)
  if(_artifact_name MATCHES "linux-")
    set(_this_platform "linux")
  elseif(_artifact_name MATCHES "windows-")
    set(_this_platform "windows")
  else()
    message(FATAL_ERROR
      "T058: cannot derive the platform token from artifact '${_artifact_name}'. "
      "FR-017 requires the platform in the artifact's own name.")
  endif()

  if(_platform_token STREQUAL "")
    set(_platform_token "${_this_platform}")
  elseif(NOT _platform_token STREQUAL _this_platform)
    message(FATAL_ERROR
      "T058: produced artifacts disagree on platform token (${_platform_token} vs ${_this_platform}): "
      "${_artifacts}")
  endif()

  if(_artifact_name MATCHES "\\.tar\\.gz$")
    list(APPEND _actual_exts ".tar.gz")
  elseif(_artifact_name MATCHES "\\.deb$")
    list(APPEND _actual_exts ".deb")
  elseif(_artifact_name MATCHES "\\.rpm$")
    list(APPEND _actual_exts ".rpm")
  elseif(_artifact_name MATCHES "\\.zip$")
    list(APPEND _actual_exts ".zip")
  else()
    message(FATAL_ERROR
      "T058: produced artifact '${_artifact_name}' has an unrecognised package extension")
  endif()
endforeach()
list(REMOVE_DUPLICATES _actual_exts)
list(SORT _actual_exts)
if(_platform_token STREQUAL "linux")
  set(_expected_exts ".deb" ".rpm" ".tar.gz")
elseif(_platform_token STREQUAL "windows")
  set(_expected_exts ".zip")
else()
  message(FATAL_ERROR "T058: could not derive an expected format set from produced artifacts")
endif()
set(_missing_exts "")
foreach(_expected_ext IN LISTS _expected_exts)
  if(NOT _expected_ext IN_LIST _actual_exts)
    list(APPEND _missing_exts "${_expected_ext}")
  endif()
endforeach()
set(_extra_exts "")
foreach(_actual_ext IN LISTS _actual_exts)
  if(NOT _actual_ext IN_LIST _expected_exts)
    list(APPEND _extra_exts "${_actual_ext}")
  endif()
endforeach()
if(NOT _missing_exts STREQUAL "" OR NOT _extra_exts STREQUAL "")
  message(FATAL_ERROR
    "T058/FR-015: ${_platform_token} artifacts have the wrong format set.\n"
    "  missing: ${_missing_exts}\n"
    "  extra: ${_extra_exts}\n"
    "  expected: ${_expected_exts}\n"
    "  actual: ${_actual_exts}")
endif()
list(LENGTH _artifacts _n_artifacts)
message(STATUS "T058: enumerating ${_n_artifacts} produced artifact(s)")

# ── Extract one artifact, and DERIVE the expected archive count from it ──────
# The expected count is read from the package's OWN exported declaration — the
# number of `add_library(fixpp::X STATIC IMPORTED)` entries in the shipped
# fixppTargets.cmake — never from a literal here and never from a build-time
# variable. Two consequences, both wanted:
#   * adding or removing an export member cannot leave this gate asserting a
#     stale number (an exact-match gate checking the wrong value is the defect
#     this bundle already fixed once);
#   * it becomes a genuine cross-check — the package's declared STATIC members
#     against the package's actual archives — rather than a restatement of
#     something the build already believes.
# The extractable artifact is the TGZ on Linux and the ZIP on Windows — the
# platform ships one archive generator, not both, so requiring .tar.gz
# specifically made this test unrunnable on Windows (measured: it aborted with
# "no .tar.gz artifact" against a perfectly good ZIP).
set(_tgz "")
foreach(_a IN LISTS _artifacts)
  if(_a MATCHES "\\.tar\\.gz$" OR _a MATCHES "\\.zip$")
    set(_tgz "${_a}")
  endif()
endforeach()
if(_tgz STREQUAL "")
  message(FATAL_ERROR
    "T058: no .tar.gz or .zip artifact to extract for content checks (produced: ${_artifacts})")
endif()

set(_x "${FIXPP_WORK_DIR}/extract")
file(MAKE_DIRECTORY "${_x}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_tgz}"
                WORKING_DIRECTORY "${_x}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "T058: failed to extract ${_tgz}")
endif()

file(GLOB_RECURSE _shipped_targets "${_x}/*/fixppTargets.cmake")
if(_shipped_targets STREQUAL "")
  message(FATAL_ERROR "T058: the package ships no fixppTargets.cmake — nothing to derive from")
endif()
list(GET _shipped_targets 0 _shipped_targets_file)
file(READ "${_shipped_targets_file}" _targets_text)

string(REGEX MATCHALL "add_library\\(fixpp::[A-Za-z0-9_:]+ STATIC IMPORTED\\)"
       _static_decls "${_targets_text}")
list(LENGTH _static_decls FIXPP_EXPECTED_ARCHIVES)
if(FIXPP_EXPECTED_ARCHIVES EQUAL 0)
  message(FATAL_ERROR
    "T058: the shipped fixppTargets.cmake declares NO static imported targets. "
    "Either the export is empty or the parse regex no longer matches — in both "
    "cases the archive-count assertion below would pass vacuously.")
endif()
message(STATUS "T058: package declares ${FIXPP_EXPECTED_ARCHIVES} STATIC imported targets")

# ── T061 / SC-005: debug-vs-release fidelity, LINUX leg ──────────────────────
# Each platform is checked by its own mechanism (package-layout.md §7):
#   Linux    debug info lives INSIDE the archive members (DWARF) -> count
#            .debug_info sections in libfixpp_core.a
#   Windows  debug info lives in SEPARATE .pdb files -> asserted per-artifact
#            further down, two-sided
# TWO-SIDED here as well: Debug MUST carry debug_info, Release MUST NOT. A
# one-sided check would pass a Release package built with -g, shipping debug
# information (and the size that comes with it) to every consumer.
#
# Measured 2026-08-02 on the shipped artifacts: debug 228 KB / 4 sections,
# release 16 KB / 0 sections. Neither the archiver nor the linker strips
# anything — the compiler simply never emits debug info in Release.
if(_tgz MATCHES "linux-")
  file(GLOB_RECURSE _core_archives "${_x}/*/libfixpp_core.a")
  if(_core_archives STREQUAL "")
    message(FATAL_ERROR
      "T061: no libfixpp_core.a in the extracted package, so debug/release "
      "fidelity cannot be established.")
  endif()
  list(GET _core_archives 0 _core_archive)

  find_program(_fixpp_readelf NAMES readelf eu-readelf)
  if(NOT _fixpp_readelf)
    # Deliberately fatal, not skipped: a fidelity gate that quietly does not run
    # is the false-green this bundle refuses everywhere else. readelf ships with
    # binutils, which is already required to build.
    message(FATAL_ERROR
      "T061: readelf not found, so SC-005's Linux leg cannot run. Refusing to "
      "report a pass on a gate that did not execute.")
  endif()

  execute_process(COMMAND "${_fixpp_readelf}" -S "${_core_archive}"
                  OUTPUT_VARIABLE _readelf_out RESULT_VARIABLE _readelf_rc ERROR_QUIET)
  if(NOT _readelf_rc EQUAL 0)
    message(FATAL_ERROR "T061: readelf failed on ${_core_archive} (exit ${_readelf_rc})")
  endif()
  string(REGEX MATCHALL "debug_info" _dbg_hits "${_readelf_out}")
  list(LENGTH _dbg_hits _n_debug)

  # Configuration comes from the ARTIFACT'S OWN NAME, not a passed-in build-type
  # variable: the name is what a consumer sees, it already encodes all six FR-017
  # dimensions, and it cannot drift from the thing under test.
  get_filename_component(_tgz_name "${_tgz}" NAME)
  if(_tgz_name MATCHES "-debug")
    set(_cfg_lower "debug")
  elseif(_tgz_name MATCHES "-release")
    set(_cfg_lower "release")
  else()
    message(FATAL_ERROR
      "T061: cannot tell the configuration from '${_tgz_name}', so debug/release "
      "fidelity cannot be asserted. FR-017 requires the configuration in the name.")
  endif()

  if(_cfg_lower STREQUAL "debug" AND _n_debug EQUAL 0)
    message(FATAL_ERROR
      "SC-005/T061: a DEBUG package's libfixpp_core.a carries NO debug_info "
      "sections. On Linux the debug information lives inside the archive "
      "members, so this package is undebuggable.")
  endif()
  if(_cfg_lower STREQUAL "release" AND NOT _n_debug EQUAL 0)
    message(FATAL_ERROR
      "SC-005/T061: a RELEASE package's libfixpp_core.a carries ${_n_debug} "
      "debug_info sections. Release emits no debug information, so this "
      "configuration is not what the artifact name claims.")
  endif()
  message(STATUS
    "T061: ${_cfg_lower} libfixpp_core.a carries ${_n_debug} debug_info section(s) — "
    "correct for this configuration")
endif()

# ── List one artifact's contents, normalised ─────────────────────────────────
# RAW listing — exactly what the archive contains, no normalisation. Split out
# because the internal-prefix assertion must see the prefix that normalisation
# deliberately removes.
function(_fixpp_list_package_raw _path _out_raw)
  get_filename_component(_ext "${_path}" EXT)
  set(_raw "")
  if(_path MATCHES "\\.deb$")
    execute_process(COMMAND dpkg-deb -c "${_path}"
                    OUTPUT_VARIABLE _o RESULT_VARIABLE _r ERROR_VARIABLE _e)
    if(NOT _r EQUAL 0)
      message(FATAL_ERROR "T058: cannot list ${_path} (dpkg-deb exit ${_r}): ${_e}")
    endif()
    # dpkg-deb -c is `ls -l`-style; the path is the 6th field.
    string(REPLACE "\n" ";" _lines "${_o}")
    foreach(_l IN LISTS _lines)
      if(_l MATCHES "[ \t]([./][^ \t]*)$")
        list(APPEND _raw "${CMAKE_MATCH_1}")
      endif()
    endforeach()
  elseif(_path MATCHES "\\.rpm$")
    execute_process(COMMAND rpm -qlp "${_path}"
                    OUTPUT_VARIABLE _o RESULT_VARIABLE _r ERROR_VARIABLE _e)
    if(NOT _r EQUAL 0)
      message(FATAL_ERROR "T058: cannot list ${_path} (rpm exit ${_r}): ${_e}")
    endif()
    string(REPLACE "\n" ";" _raw "${_o}")
  else()  # .tar.gz / .zip
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar tf "${_path}"
                    OUTPUT_VARIABLE _o RESULT_VARIABLE _r ERROR_VARIABLE _e)
    if(NOT _r EQUAL 0)
      message(FATAL_ERROR "T058: cannot list ${_path} (tar exit ${_r}): ${_e}")
    endif()
    string(REPLACE "\n" ";" _raw "${_o}")
  endif()

  set(${_out_raw} "${_raw}" PARENT_SCOPE)
endfunction()

# Normalised listing — every generator on equal terms.
function(_fixpp_list_package _path _out_files)
  _fixpp_list_package_raw("${_path}" _raw)
  # Normalise: drop a leading "./", a leading "/", and the archive's top-level
  # <package-name>/ component, so all three generators compare on equal terms.
  set(_norm "")
  foreach(_f IN LISTS _raw)
    string(STRIP "${_f}" _f)
    if(_f STREQUAL "")
      continue()
    endif()
    string(REGEX REPLACE "^\\./" "" _f "${_f}")
    string(REGEX REPLACE "^/" "" _f "${_f}")
    string(REGEX REPLACE "^fixpp-[^/]+/" "" _f "${_f}")
    # `usr/` is a LINUX-only component: DEB and RPM install into the system tree
    # and need it, the Windows ZIP does not (MEASURED — it used to carry `usr/`
    # unconditionally, which is why this strip is written to tolerate BOTH). It
    # is removed here so every expectation below is expressed once, prefix-free,
    # instead of being duplicated per platform.
    string(REGEX REPLACE "^usr/" "" _f "${_f}")
    string(REGEX REPLACE "/$" "" _f "${_f}")
    if(NOT _f STREQUAL "")
      list(APPEND _norm "${_f}")
    endif()
  endforeach()
  set(${_out_files} "${_norm}" PARENT_SCOPE)
endfunction()

# ── Per-artifact assertions ──────────────────────────────────────────────────
foreach(_artifact IN LISTS _artifacts)
  get_filename_component(_aname "${_artifact}" NAME)
  _fixpp_list_package("${_artifact}" _files)
  list(LENGTH _files _nf)
  if(_nf LESS 10)
    message(FATAL_ERROR "T058: ${_aname} lists only ${_nf} entries — it is effectively empty")
  endif()

  set(_missing "")

  # ── The INTERNAL PREFIX must match the platform (measured, not normalised away)
  # Normalisation below strips `usr/` so one set of expectations covers every
  # generator — which also makes every check downstream BLIND to whether the
  # prefix was right. It has to be asserted here, on the RAW listing, or a
  # Windows ZIP shipping FHS `usr/lib/*.lib` passes the entire suite. That is not
  # hypothetical: it is exactly what happened, and the collected Release artifact
  # was wrong while contents reported green.
  #   DEB/RPM/TGZ → MUST have usr/   (they install into the system tree)
  #   ZIP          → MUST NOT        (extracted anywhere; `usr/` is meaningless)
  _fixpp_list_package_raw("${_artifact}" _raw_files)
  set(_has_usr 0)
  foreach(_f IN LISTS _raw_files)
    if(_f MATCHES "(^|/)usr/(lib|include|share)(/|$)")
      set(_has_usr 1)
    endif()
  endforeach()
  if(_aname MATCHES "\\.zip$" AND _has_usr)
    message(FATAL_ERROR
      "T058: ${_aname} is a ZIP but carries an FHS `usr/` prefix. A Windows package "
      "extracts wherever the consumer puts it and points CMAKE_PREFIX_PATH there; "
      "`usr/` means nothing on Windows. CPACK_PACKAGING_INSTALL_PREFIX must stay "
      "guarded to NOT WIN32.")
  endif()
  if(NOT _aname MATCHES "\\.zip$" AND NOT _has_usr)
    message(FATAL_ERROR
      "T058: ${_aname} carries NO `usr/` prefix. DEB/RPM install into the system "
      "tree and the TGZ mirrors them, so losing it would install into the wrong "
      "place.")
  endif()

  # MUST BE PRESENT — enumerated explicitly, never as a class.
  # Paths are PREFIX-FREE: the Linux-only `usr/` component was stripped during
  # normalisation, so one list covers DEB, RPM, TGZ and the Windows ZIP.
  foreach(_required
      "lib/cmake/fixpp/fixppConfig.cmake"
      "lib/cmake/fixpp/fixppConfigVersion.cmake"
      "lib/cmake/fixpp/fixppTargets.cmake"
      "share/doc/fixpp/NOTICE"
      "share/doc/fixpp/QUICKFIX_LICENSE.txt"
      "share/doc/fixpp/fixpp-package-provenance.txt"
      "share/fixpp/dictionaries/FIX44.xml")
    if(NOT _required IN_LIST _files)
      list(APPEND _missing "${_required}")
    endif()
  endforeach()

  # A hand-written public header and a GENERATED per-version typed header, which
  # arrive through two DIFFERENT install rules (FR-011a).
  set(_have_public 0)
  set(_have_generated 0)
  set(_n_archives 0)
  set(_have_objects 0)
  foreach(_f IN LISTS _files)
    if(_f MATCHES "^include/fixpp/wire/parser\\.hpp$")
      set(_have_public 1)
    endif()
    if(_f MATCHES "^include/fixpp/v44/Messages\\.hpp$")
      set(_have_generated 1)
    endif()
    # Archive naming is TOOLCHAIN-dependent and both forms must be recognised:
    # GNU/Clang emit `libfixpp_core.a`, MSVC emits `fixpp_core.lib` (MEASURED in
    # the windows-msvc-release ZIP). A pattern written for only the GNU form
    # counts ZERO archives on Windows, and since the expected count is derived
    # from the shipped fixppTargets.cmake this fails as a count mismatch — which
    # would read as a missing-archive defect rather than as a test that cannot
    # see the archives it is looking at.
    if(_f MATCHES "^lib/libfixpp_[A-Za-z0-9_]+\\.a$" OR _f MATCHES "^lib/fixpp_[A-Za-z0-9_]+\\.lib$")
      math(EXPR _n_archives "${_n_archives} + 1")
    endif()
    # Likewise `.o` (GNU/Clang) vs `.obj` (MSVC).
    if(_f MATCHES "^lib/objects-[A-Za-z]+/fixpp_capi_objects/.*\\.(o|obj)$")
      set(_have_objects 1)
    endif()
  endforeach()
  # ── FR-019: a Windows DEBUG package MUST carry separate symbol files ────────
  # Keyed off the artifact's OWN name, so no build-time variable has to be
  # threaded in and the check cannot drift from the thing it describes.
  #
  # This exists because the rule that ships them failed SILENTLY: it globbed
  # ${CMAKE_BINARY_DIR}/lib/ for *.pdb, MSVC writes a STATIC library's COMPILER
  # pdb into the target's object directory instead, and install(DIRECTORY …
  # FILES_MATCHING) over a directory with no match installs nothing and
  # SUCCEEDS. The Debug ZIP shipped undebuggable libraries and every gate was
  # green. Release correctly ships none (no /Zi, nothing to separate), so this
  # must NOT be asserted there.
  # TWO-SIDED, because that is what makes it a FIDELITY check (SC-005/T061)
  # rather than a presence check: Debug MUST carry symbol files, Release MUST
  # NOT. A one-sided assertion would pass a Release package that shipped debug
  # symbols it has no business carrying.
  if(_aname MATCHES "windows-msvc-")
    set(_have_pdb 0)
    foreach(_f IN LISTS _files)
      if(_f MATCHES "^lib/.*\\.pdb$")
        set(_have_pdb 1)
      endif()
    endforeach()
    if(_aname MATCHES "windows-msvc-debug" AND NOT _have_pdb)
      list(APPEND _missing
        "lib/*.pdb (FR-019: a Windows Debug package without separate symbol files "
        "ships UNDEBUGGABLE libraries — MSVC keeps a static library's debug info in "
        "a compiler pdb under its object dir, NOT next to the archive)")
    endif()
    if(_aname MATCHES "windows-msvc-release" AND _have_pdb)
      message(FATAL_ERROR
        "SC-005/T061: ${_aname} is a RELEASE package but carries lib/*.pdb. Release "
        "emits no debug information at all (no /Zi), so a symbol file here means the "
        "configuration is not what the artifact name claims.")
    endif()
  endif()

  if(NOT _have_public)
    list(APPEND _missing "include/fixpp/wire/parser.hpp (hand-written public header)")
  endif()
  if(NOT _have_generated)
    list(APPEND _missing "include/fixpp/v44/Messages.hpp (generated typed header, FR-011a)")
  endif()
  # objects-<CONFIG>/ is INTENDED content, not a leak: fixpp_capi_objects is an
  # OBJECT library in the export set and install(TARGETS) on one MANDATES an
  # OBJECTS DESTINATION.
  #
  # ⚠️ DO NOT "CLEAN THIS UP" — measured 2026-08-02, decision: keep (user).
  # The objects duplicate members already inside the capi archive, and NO
  # CONSUMER LINKS THEM: the Linux and Windows consumer_capi_witness link lines
  # carry libfixpp_capi.a / fixpp_capi.lib and ZERO loose objects, with no
  # reference to objects-* anywhere in either consumer build. (An IMPORTED
  # OBJECT library does not propagate its objects through
  # target_link_libraries — only a non-imported one does, since CMake 3.12 —
  # so they arrive only via an explicit $<TARGET_OBJECTS:>, which no consumer
  # writes.)
  #
  # They still CANNOT be deleted from the package. The generated
  # fixppTargets.cmake ends in an existence check over
  # _cmake_import_check_files_for_fixpp::capi_objects, so removing them makes
  # find_package(fixpp) FATAL_ERROR at configure time for every consumer —
  # trading dead weight for a broken package. Dropping them requires keeping the
  # OBJECT library out of the export closure entirely (fixpp_capi owning its
  # sources), which costs fixpp_capi_shared a second compilation.
  #
  # Measured cost: ~21 MB in the MSVC Debug ZIP (~half of it), ~1.2 MB on Linux
  # Release. The Debug figure is far larger than the Linux one this was
  # originally accepted on.
  if(NOT _have_objects)
    list(APPEND _missing "lib/objects-*/fixpp_capi_objects/*.{o,obj} (OBJECT-library member)")
  endif()

  if(NOT _missing STREQUAL "")
    string(REPLACE ";" "\n  " _missing_pretty "${_missing}")
    message(FATAL_ERROR
      "T058/SC-013: ${_aname} is MISSING required content:\n  ${_missing_pretty}")
  endif()

  # By KIND: 13 of the 18 export members are STATIC and each must contribute
  # exactly one archive. Asserted as an exact count, not ">= 1".
  if(NOT _n_archives EQUAL FIXPP_EXPECTED_ARCHIVES)
    message(FATAL_ERROR
      "T058: ${_aname} carries ${_n_archives} fixpp archives, expected exactly "
      "${FIXPP_EXPECTED_ARCHIVES} (one per STATIC export member). A count that "
      "drifts either way is a membership defect, not a naming detail.")
  endif()

  # ══ 086 T034/T035 — the isolated include roots ════════════════════════════
  #
  # Nothing here asserted the C-ABI headers AT ALL before this feature, at either
  # root: the denylist below is anchored on `^include/fixpp/…` and the
  # generated-tree allowlist on `^include/fixpp/(v[A-Za-z0-9]+)/…`, so both are
  # structurally incapable of matching a path under `include/capi/` or
  # `include/service-iface/`. A package that shipped neither new root — or shipped
  # one half-populated — passed every gate in this file.
  #
  # BY SET EQUALITY, NOT BY A HARDCODED CENSUS. The two roots are installed from
  # ONE source directory each (data-model I2), so the duplicate tree must match
  # the original exactly. A transcribed list of twelve filenames would go stale
  # the first time a C-ABI sub-header is added, and would go stale SILENTLY in the
  # direction that matters — a new header missing from the isolated root is
  # exactly the defect this is for.
  #
  # PREFIX-FREE: `_files` has already had the Linux-only `usr/` component
  # stripped (see normalisation above), so one expectation covers DEB, RPM, TGZ
  # and the Windows ZIP. A `usr/`-anchored glob would find nothing in the ZIP and
  # report "the package carries no C-ABI headers" — a defect claim about the
  # product manufactured by the test.
  foreach(_dup_pair
      "include/fix/|include/capi/fix/"
      "include/fixpp/service/|include/service-iface/fixpp/service/")
    string(REPLACE "|" ";" _dup_pair "${_dup_pair}")
    list(GET _dup_pair 0 _orig_root)
    list(GET _dup_pair 1 _iso_root)

    set(_orig_tails "")
    set(_iso_tails "")
    foreach(_f IN LISTS _files)
      if(_f MATCHES "^${_orig_root}(.+)$")
        list(APPEND _orig_tails "${CMAKE_MATCH_1}")
      endif()
      if(_f MATCHES "^${_iso_root}(.+)$")
        list(APPEND _iso_tails "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    list(SORT _orig_tails)
    list(SORT _iso_tails)

    # FR-010, and the floor that stops equality from holding vacuously: two empty
    # sets are equal. Without this an artifact shipping NO headers under either
    # root passes the comparison below with a perfectly straight face.
    # ⚠️ FATAL_ERROR, not `list(APPEND _missing …)`. `_missing` is initialised at
    # :316 and consumed ONCE at :463 — sixty lines ABOVE here — then reset at the
    # top of the next artifact's iteration. An append at this point is never read
    # by anything, so routing this branch through it made the floor DEAD CODE:
    # the check that exists to stop a vacuous pass would itself have passed
    # vacuously. Caught at /simplify; it is the same defect class this whole
    # feature is about, which is why it is worth this comment.
    # The floor requires a HEADER, not merely a non-empty tail list. Directory
    # entries survive normalisation (`include/fix/c_api/` -> `include/fix/c_api`)
    # and DO match `^include/fix/(.+)$`, so a bare `STREQUAL ""` floor would still
    # pass a package that shipped the directory skeleton at both roots and not one
    # header — set-equality between two identical skeletons holds.
    if(NOT _orig_tails MATCHES "\\.(h|hpp)(;|$)")
      message(FATAL_ERROR
        "086 FR-010: ${_aname} ships no HEADER under ${_orig_root} at all, so the "
        "set comparison below would hold vacuously. Either the install(DIRECTORY) "
        "rule for this root regressed, or a new PATTERN … EXCLUDE on CMakeLists.txt's "
        "include/ rule removed the subtree.\n  observed tails: ${_orig_tails}")
    elseif(NOT _orig_tails STREQUAL _iso_tails)
      string(REPLACE ";" "\n    " _o_pretty "${_orig_tails}")
      string(REPLACE ";" "\n    " _i_pretty "${_iso_tails}")
      message(FATAL_ERROR
        "086 FR-010: ${_aname} — the isolated root ${_iso_root} does not carry the same "
        "set as ${_orig_root}. Both are installed from one source directory, so any "
        "difference means an install rule is missing, mis-scoped, or was not updated "
        "when a header was added.\n"
        "  ${_orig_root}:\n    ${_o_pretty}\n"
        "  ${_iso_root}:\n    ${_i_pretty}")
    endif()
  endforeach()

  # FR-010a / C-5 — CONTAINMENT. The only assertion in the entire suite that
  # traces FR-001, which is a property of a ROOT, not of a target: an isolated
  # root that also carried, say, `include/capi/fixpp/wire/parser.hpp` would defeat
  # the isolation for every consumer, and no negative probe would catch it unless
  # it happened to name that exact header.
  # ⚠️ DIRECTORY ENTRIES ARE PART OF THE LISTING. DEB/RPM/TGZ enumerate the
  # directories they create, and the normalisation above strips the trailing "/",
  # so `include/capi/fix/` arrives here as `include/capi/fix` — indistinguishable
  # by name from a file, and NOT a match for "^include/capi/fix/". A containment
  # check written only against the file paths reports the subtree's own directory
  # as escaping it, which is what this one did on its first real run.
  #
  # The intermediate ancestors are DERIVED from the declared subtree rather than
  # listed, so adding a level to either root cannot leave a transcribed
  # allowance behind. Everything else under a root is still a violation — this
  # does not weaken the check: `include/service-iface/fixpp/wire/parser.hpp`
  # matches neither the subtree nor an ancestor.
  set(_uncontained "")
  foreach(_pair
      "include/capi/|include/capi/fix"
      "include/service-iface/|include/service-iface/fixpp/service")
    string(REPLACE "|" ";" _pair "${_pair}")
    list(GET _pair 0 _root)
    list(GET _pair 1 _subtree)

    set(_ancestors "")
    set(_cur "${_subtree}")
    while(TRUE)
      get_filename_component(_cur "${_cur}" DIRECTORY)
      if(_cur STREQUAL "" OR NOT _cur MATCHES "^${_root}")
        break()
      endif()
      list(APPEND _ancestors "${_cur}")
    endwhile()

    foreach(_f IN LISTS _files)
      if(_f MATCHES "^${_root}"
         AND NOT _f MATCHES "^${_subtree}(/|$)"
         AND NOT _f IN_LIST _ancestors)
        list(APPEND _uncontained "${_f}")
      endif()
    endforeach()
  endforeach()
  if(NOT _uncontained STREQUAL "")
    string(REPLACE ";" "\n  " _uncontained_pretty "${_uncontained}")
    message(FATAL_ERROR
      "086 FR-010a/C-5: ${_aname} — an isolated include root carries paths outside its "
      "declared subtree, so it is not isolated:\n  ${_uncontained_pretty}")
  endif()

  # MUST BE ABSENT — SET EQUALITY over the exact 7-pattern denylist, never a
  # subset. A check written from the 078 five-pattern tail would pass a package
  # leaking the build-tree-private reify bridge (_dispatch/) or the FIXT.1.1 tree
  # (vt11/) while looking perfectly coherent.
  set(_leaked "")
  foreach(_f IN LISTS _files)
    if(_f MATCHES "^include/fixpp/(_dispatch|vt11)(/|$)"
       OR _f MATCHES "^include/fixpp/[^/]+/(messages|groups|validators)(/|$)"
       OR _f MATCHES "^include/fixpp/[^/]+/(all|groups)\\.hpp$"
       OR _f MATCHES "^include/fixpp/(core|transport)/test(/|$)")
      list(APPEND _leaked "${_f}")
    endif()
  endforeach()
  if(NOT _leaked STREQUAL "")
    list(LENGTH _leaked _n_leaked)
    string(REPLACE ";" "\n  " _leaked_pretty "${_leaked}")
    message(FATAL_ERROR
      "T058/FR-013: ${_aname} LEAKS ${_n_leaked} denylisted path(s):\n  ${_leaked_pretty}")
  endif()

  # ── The generated-header tree is an ALLOWLIST, not just a denylist ─────────
  # The install rule is allowlist-by-EXCLUSION: everything under the codegen
  # include root ships unless one of seven patterns removes it. So a future
  # emitter adding a NEW artifact kind is installed BY DEFAULT and every
  # denylist-shaped check above passes over it — it matches none of the seven.
  # That is the silent leak T059 exists to prove catchable, and only an exact-set
  # assertion over each version directory's immediate children can catch it.
  set(_expected_generated "Fields.hpp;Messages.hpp;NormativeReferences.md;Reify.hpp;Validator.hpp")
  set(_versions_seen "")
  foreach(_f IN LISTS _files)
    if(_f MATCHES "^include/fixpp/(v[A-Za-z0-9]+)/([^/]+)$")
      list(APPEND _versions_seen "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _versions_seen)
  if(_versions_seen STREQUAL "")
    message(FATAL_ERROR
      "T058: ${_aname} ships no generated per-version header directory at all — "
      "the FR-011a install rule produced nothing and every check over it is vacuous.")
  endif()
  foreach(_ver IN LISTS _versions_seen)
    set(_actual "")
    foreach(_f IN LISTS _files)
      if(_f MATCHES "^include/fixpp/${_ver}/([^/]+)$")
        list(APPEND _actual "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES _actual)
    list(SORT _actual)
    set(_want ${_expected_generated})
    list(SORT _want)
    if(NOT "${_actual}" STREQUAL "${_want}")
      message(FATAL_ERROR
        "T058/FR-013: ${_aname} generated tree for ${_ver} is not the expected exact set.\n"
        "  expected: ${_want}\n"
        "  actual:   ${_actual}\n"
        "An ADDED entry is a new emitted artifact kind leaking into the package "
        "(the install rule ships anything not explicitly excluded); a MISSING one "
        "is a regressed install rule. Both are defects — update this set only "
        "together with a deliberate decision about what ships.")
    endif()
  endforeach()

  message(STATUS "T058: ${_aname} OK (${_nf} entries, ${_n_archives} archives, "
                 "generated tree exact for: ${_versions_seen})")
endforeach()

# ── NOTICE content, compared against the PINNED ANCHOR ───────────────────────
# The acknowledgment sentence is compared whitespace-normalised against
# dictionaries/QUICKFIX_LICENSE.txt, NEVER against a string typed into this test:
# it spans TWO lines, sits INDENTED inside clause 3, and is itself ENCLOSED IN
# QUOTATION MARKS, so a literal written from memory silently never matches and the
# one obligation classified as legal becomes unfalsifiable.
# Matched by BASENAME at any depth, deliberately: the Linux layouts carry an
# extra `usr/` component that the Windows ZIP does not, and a GLOB_RECURSE
# pattern containing intermediate literal directories must match them at an
# exact depth — so `*/share/doc/fixpp/NOTICE` silently finds nothing on Linux.
file(GLOB_RECURSE _notice_files "${_x}/*/NOTICE")
if(_notice_files STREQUAL "")
  message(FATAL_ERROR "T058/FR-018b: no NOTICE in the extracted package")
endif()
list(GET _notice_files 0 _notice)

file(READ "${FIXPP_SOURCE_DIR}/dictionaries/QUICKFIX_LICENSE.txt" _license_text)
file(READ "${_notice}" _notice_text)

# Whitespace-normalise both, then require the anchor sentence to appear in NOTICE.
function(_fixpp_squash _in _out)
  string(REGEX REPLACE "[ \t\r\n]+" " " _s "${_in}")
  set(${_out} "${_s}" PARENT_SCOPE)
endfunction()
_fixpp_squash("${_license_text}" _license_flat)
_fixpp_squash("${_notice_text}"  _notice_flat)

# Extract the clause-3 acknowledgment from the LICENCE ITSELF rather than
# hardcoding it, so the check tracks the anchor if upstream ever rewords it.
if(NOT _license_flat MATCHES "\"(This product includes software developed by[^\"]*)\"")
  message(FATAL_ERROR
    "T058: could not locate the clause-3 acknowledgment in QUICKFIX_LICENSE.txt. "
    "The anchor moved — the NOTICE check cannot be trusted until this is fixed.")
endif()
set(_ack "${CMAKE_MATCH_1}")

# LITERAL substring search, not MATCHES: the acknowledgment contains
# "(http://www.quickfixengine.org/)", whose parentheses, dots and slashes are
# regex metacharacters. Passing it to MATCHES compares against a *pattern* that
# happens to resemble the sentence — it would match text that is not the
# acknowledgment and fail on text that is.
string(FIND "${_notice_flat}" "${_ack}" _ack_pos)
if(_ack_pos EQUAL -1)
  message(FATAL_ERROR
    "T058/FR-018b: the packaged NOTICE does not carry the upstream clause-3 "
    "acknowledgment verbatim.\nExpected (whitespace-normalised):\n  ${_ack}")
endif()

message(STATUS "T058: NOTICE carries the clause-3 acknowledgment, matched against the pinned anchor")
message(STATUS "fixpp::packaging::contents: OK")
