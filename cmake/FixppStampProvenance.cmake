# cmake/FixppStampProvenance.cmake — 084 T053/T062a (FR-021a, I24).
#
# Included from an install(CODE) block in FixppPackaging.cmake, so this file
# runs at INSTALL TIME -- once per `cpack` / `cmake --install` invocation --
# NOT at configure time. That is the entire point of the Gate B round 2 fix
# (R2-F1): a stamp computed here reflects the tree that is actually being
# staged right now, including edits made after the last `cmake` configure. A
# `configure_file()`-based stamp is frozen once and never re-triggered by a
# later edit, which shipped a `source-worktree: clean` artifact for a build
# that was, at pack time, actually dirty -- a false affirmative.
#
# Every variable this script reads with a `_fixpp_stamp_` prefix is baked in
# at CONFIGURE time by the install(CODE) block that includes this file (see
# FixppPackaging.cmake) -- they are stable per-configuration constants (source
# dir, project version, platform, compiler id/version, build type, telemetry
# flag, docdir, and the FIXPP_PACKAGE_REVISION/WORKTREE cache overrides).
# Everything else here -- the two `execute_process(git ...)` calls, and
# CMAKE_INSTALL_PREFIX / $ENV{DESTDIR} -- belongs to install-time and is read
# fresh on every invocation.

find_program(_fixpp_stamp_git_exe git)

# ── revision ──────────────────────────────────────────────────────────────
# Same fail-open hazard as the original configure-time logic (FixppPackaging.cmake
# history, 2026-08-02): trust execute_process output ONLY when RESULT_VARIABLE
# says the command succeeded. A failed command must not silently read as
# "unknown became a real hash" or, worse, as "clean".
set(FIXPP_PACKAGE_REVISION "${_fixpp_stamp_override_revision}")
if(FIXPP_PACKAGE_REVISION STREQUAL "")
  set(FIXPP_PACKAGE_REVISION "unknown")
  if(_fixpp_stamp_git_exe)
    execute_process(
      COMMAND "${_fixpp_stamp_git_exe}" rev-parse HEAD
      WORKING_DIRECTORY "${_fixpp_stamp_source_dir}"
      RESULT_VARIABLE _fixpp_stamp_rev_rc
      OUTPUT_VARIABLE _fixpp_stamp_rev
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_fixpp_stamp_rev_rc EQUAL 0 AND NOT _fixpp_stamp_rev STREQUAL "")
      set(FIXPP_PACKAGE_REVISION "${_fixpp_stamp_rev}")
    endif()
  endif()
endif()

# ── worktree ──────────────────────────────────────────────────────────────
set(FIXPP_PACKAGE_WORKTREE "${_fixpp_stamp_override_worktree}")
if(FIXPP_PACKAGE_WORKTREE STREQUAL "")
  set(FIXPP_PACKAGE_WORKTREE "unknown")
  if(_fixpp_stamp_git_exe)
    execute_process(
      COMMAND "${_fixpp_stamp_git_exe}" status --porcelain
      WORKING_DIRECTORY "${_fixpp_stamp_source_dir}"
      RESULT_VARIABLE _fixpp_stamp_status_rc
      OUTPUT_VARIABLE _fixpp_stamp_porcelain
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    # "clean" is asserted ONLY when the command actually RAN and reported
    # nothing -- a FAILED `git status` also yields empty output, which must
    # NOT read as "clean".
    if(_fixpp_stamp_status_rc EQUAL 0)
      if(_fixpp_stamp_porcelain STREQUAL "")
        set(FIXPP_PACKAGE_WORKTREE "clean")
      else()
        set(FIXPP_PACKAGE_WORKTREE "dirty")
      endif()
    endif()
  endif()
endif()

# ── render the template, at install time ─────────────────────────────────
set(FIXPP_STAMP_VERSION "${_fixpp_stamp_project_version}")
set(FIXPP_STAMP_PLATFORM "${_fixpp_stamp_platform}")
set(FIXPP_STAMP_COMPILER_ID "${_fixpp_stamp_compiler_id}")
set(FIXPP_STAMP_COMPILER_VERSION "${_fixpp_stamp_compiler_version}")
set(FIXPP_STAMP_CONFIG "${_fixpp_stamp_build_type}")
set(FIXPP_PACKAGE_TELEMETRY "${_fixpp_stamp_telemetry}")

file(READ "${_fixpp_stamp_source_dir}/cmake/fixpp-package-provenance.txt.in" _fixpp_stamp_template)

# DESTDIR is how CPack stages archive generators (and how `make install`/
# `cmake --install` support staged installs generally); CMAKE_INSTALL_PREFIX
# inside a generated cmake_install.cmake is the prefix WITHOUT DESTDIR
# applied, so a manual file write (unlike install(FILES), which handles this
# internally) must prepend it explicitly or the stamp lands outside the
# staging tree CPack actually archives.
set(_fixpp_stamp_dest
    "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${_fixpp_stamp_docdir}/fixpp-package-provenance.txt")
file(CONFIGURE
  OUTPUT "${_fixpp_stamp_dest}"
  CONTENT "${_fixpp_stamp_template}"
  @ONLY)
