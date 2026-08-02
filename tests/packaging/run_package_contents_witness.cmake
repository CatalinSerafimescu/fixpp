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
set(_tgz "")
foreach(_a IN LISTS _artifacts)
  if(_a MATCHES "\\.tar\\.gz$")
    set(_tgz "${_a}")
  endif()
endforeach()
if(_tgz STREQUAL "")
  message(FATAL_ERROR "T058: no .tar.gz artifact to extract for content checks")
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

# ── List one artifact's contents, normalised ─────────────────────────────────
function(_fixpp_list_package _path _out_files)
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

  # MUST BE PRESENT — enumerated explicitly, never as a class.
  # Paths are PREFIX-FREE: the Linux-only `usr/` component was stripped during
  # normalisation, so one list covers DEB, RPM, TGZ and the Windows ZIP.
  foreach(_required
      "lib/cmake/fixpp/fixppConfig.cmake"
      "lib/cmake/fixpp/fixppConfigVersion.cmake"
      "lib/cmake/fixpp/fixppTargets.cmake"
      "share/doc/fixpp/NOTICE"
      "share/doc/fixpp/QUICKFIX_LICENSE.txt"
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
  if(NOT _have_public)
    list(APPEND _missing "include/fixpp/wire/parser.hpp (hand-written public header)")
  endif()
  if(NOT _have_generated)
    list(APPEND _missing "include/fixpp/v44/Messages.hpp (generated typed header, FR-011a)")
  endif()
  # objects-Release/ is INTENDED content, not a leak: fixpp_capi_objects is an
  # OBJECT library in the export set and needs OBJECTS DESTINATION. Its contents
  # duplicate members already inside libfixpp_capi.a (~1.2 MB) — a knowingly
  # accepted cost, recorded so a future reader does not "clean it up".
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
