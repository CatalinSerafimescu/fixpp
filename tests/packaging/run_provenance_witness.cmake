# tests/packaging/run_provenance_witness.cmake — 084 T060 (FR-021a, I24).
#
# A package carries a provenance stamp identifying the build that produced it.
# This witness reads that stamp OUT OF A PRODUCED PACKAGE and checks it against
# the state a consumer expects. A package from a DIFFERENT CONFIGURATION, a
# DIFFERENT SOURCE REVISION, or a DIFFERENT WORKTREE STATE must be REJECTED.
#
# Why this matters concretely: the artifact directory deliberately outlives the
# build-tree deletion cycle (FR-021), so packages from earlier configurations and
# earlier source states accumulate alongside current ones. Picking up a stale one
# is the normal accident, not an exotic one.
#
# Why worktree state is a SEPARATE field from the revision: two packages built
# from the same commit either side of an uncommitted edit are indistinguishable
# under (configuration, revision) alone — and an uncommitted edit is the normal
# state of a working branch.
#
# ⚠️ GATE B ROUND 2 (R2-F2): the PASS leg's expected revision and worktree MUST
# be computed INDEPENDENTLY of the artifact under test, or the comparison is
# circular by construction and cannot fail on a stale artifact -- exactly the
# case this witness exists to catch (R2-F1). Both are measured here, fresh,
# from FIXPP_SOURCE_DIR via `git`, the same approach
# tests/codegen/codegen_build_graph_test.cmake already uses for its own
# git-cleanliness gate. Because this witness invokes `cpack` against the
# CURRENT build tree, the artifact under test is packed from the current build
# tree — which makes this measurement the detector for a configure-time-frozen
# stamp. It does not establish that the tree's binaries are current with
# respect to that source state; see the residual note below.
#
# ── The red leg ──────────────────────────────────────────────────────────────
# The witness's contract is (package, expected-state) -> verdict. Feeding it an
# expectation that does not match the package IS the "package from a different
# configuration" case, from the witness's point of view — it is exactly what
# happens when a stale artifact is picked out of the shared directory. So the red
# leg passes a mismatched expectation against a REAL, UNMODIFIED package. Nothing
# is fabricated and no artifact is tampered with: tampering the stamp after the
# fact would test the parser, not the gate.
#
# ⚠️ RESIDUAL, named so a future reader does not over-read a green run: in a
# no-edit configure→build→test flow (the normal CI shape) the configure-time
# and install-time git measurements are IDENTICAL, so this standing gate stays
# green whether provenance is sampled at configure time or install time — it
# cannot, by itself, prove R2-F1 (the configure-time freeze) is fixed. Only a
# configure→edit→package→extract counter-test, run once and recorded outside
# this ctest, discriminates the two.
#
# Payload residual: this witness samples the source state independently, but it
# still packages whatever binaries already exist in the build tree. A green run
# therefore does not prove those binaries were rebuilt for that state; it proves
# only that the stamp matches the tree CPack packaged from. Binding the stamp to
# the payload bytes or build inputs is a separate follow-up issue.

cmake_minimum_required(VERSION 3.28)

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_WORK_DIR FIXPP_BUILD_TYPE FIXPP_SOURCE_DIR)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_provenance_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

# ── Independent measurement — computed BEFORE the artifact is even read ─────
find_program(_fixpp_witness_git_exe git)
if(NOT _fixpp_witness_git_exe)
  message(FATAL_ERROR "T060: git is required to compute the independent expectation")
endif()

execute_process(
  COMMAND "${_fixpp_witness_git_exe}" rev-parse HEAD
  WORKING_DIRECTORY "${FIXPP_SOURCE_DIR}"
  RESULT_VARIABLE _fixpp_exp_rev_rc
  OUTPUT_VARIABLE _fixpp_exp_rev
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_VARIABLE _fixpp_exp_rev_err)
if(NOT _fixpp_exp_rev_rc EQUAL 0 OR _fixpp_exp_rev STREQUAL "")
  message(FATAL_ERROR "T060: failed to measure the expected revision independently: ${_fixpp_exp_rev_err}")
endif()

execute_process(
  COMMAND "${_fixpp_witness_git_exe}" status --porcelain
  WORKING_DIRECTORY "${FIXPP_SOURCE_DIR}"
  RESULT_VARIABLE _fixpp_exp_status_rc
  OUTPUT_VARIABLE _fixpp_exp_porcelain
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_VARIABLE _fixpp_exp_status_err)
if(NOT _fixpp_exp_status_rc EQUAL 0)
  message(FATAL_ERROR "T060: failed to measure the expected worktree state independently: ${_fixpp_exp_status_err}")
endif()
if(_fixpp_exp_porcelain STREQUAL "")
  set(_fixpp_exp_worktree "clean")
else()
  set(_fixpp_exp_worktree "dirty")
endif()
message(STATUS "T060: independently measured expectation — revision=${_fixpp_exp_rev}, worktree=${_fixpp_exp_worktree}")

set(_pkgdir "${FIXPP_WORK_DIR}/packages")
file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")
file(MAKE_DIRECTORY "${_pkgdir}")

execute_process(
  COMMAND "${CMAKE_CPACK_COMMAND}" --config "${FIXPP_MAIN_BUILD_DIR}/CPackConfig.cmake"
          -G TGZ -B "${_pkgdir}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "cpack failed (exit ${_rc}):\n${_out}\n${_err}")
endif()

file(GLOB _tgz "${_pkgdir}/*.tar.gz")
if(_tgz STREQUAL "")
  message(FATAL_ERROR "T060: cpack produced no .tar.gz to read provenance from")
endif()
list(GET _tgz 0 _pkg)

set(_x "${FIXPP_WORK_DIR}/extract")
file(MAKE_DIRECTORY "${_x}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_pkg}"
                WORKING_DIRECTORY "${_x}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "T060: failed to extract ${_pkg}")
endif()

# Matched by BASENAME at any depth: the Linux layouts carry a `usr/` component
# that the Windows ZIP does not, and a GLOB_RECURSE pattern with intermediate
# literal directories must match at an exact depth — so a `usr/`-anchored pattern
# finds nothing on Windows and this reads as "the package carries NO provenance
# file" when the file is in fact present.
file(GLOB_RECURSE _prov_files "${_x}/*/fixpp-package-provenance.txt")
if(_prov_files STREQUAL "")
  message(FATAL_ERROR
    "T060/FR-021a: the package carries NO provenance file. Every artifact must be "
    "identifiable back to the build that produced it.")
endif()
list(GET _prov_files 0 _prov)
file(READ "${_prov}" _prov_text)

# ── The check, as a function so both legs run IDENTICAL logic ────────────────
# If the red leg ran different code from the pass leg it would prove nothing
# about the gate that actually runs in anger.
function(_fixpp_check_provenance _expect_config _expect_worktree _expect_revision _out_ok _out_why)
  set(_why "")
  # version/platform/toolchain carry no downstream comparison (unlike the four
  # fields below), so a blank render is otherwise invisible: this is the
  # renamed-template-token guard (Gate B round 2 fix moved these tokens off
  # the real CMake variable names to avoid shadowing them at install time --
  # a rename typo would render an EMPTY string via `file(CONFIGURE @ONLY)`,
  # not an error, and nothing else would catch it).
  foreach(_field version platform toolchain configuration source-revision source-worktree telemetry)
    if(NOT _prov_text MATCHES "${_field}[ ]*:[ ]*([^ \n][^\n]*)")
      list(APPEND _why "provenance has no '${_field}' field")
    endif()
  endforeach()
  if(NOT _why STREQUAL "")
    set(${_out_ok} FALSE PARENT_SCOPE)
    set(${_out_why} "${_why}" PARENT_SCOPE)
    return()
  endif()

  string(REGEX MATCH "configuration[ ]*:[ ]*([^\n]+)" _m "${_prov_text}")
  string(STRIP "${CMAKE_MATCH_1}" _got_config)
  string(REGEX MATCH "source-revision[ ]*:[ ]*([^\n]+)" _m "${_prov_text}")
  string(STRIP "${CMAKE_MATCH_1}" _got_rev)
  string(REGEX MATCH "source-worktree[ ]*:[ ]*([^\n]+)" _m "${_prov_text}")
  string(STRIP "${CMAKE_MATCH_1}" _got_worktree)

  if(NOT _got_config STREQUAL "${_expect_config}")
    list(APPEND _why "configuration is '${_got_config}', expected '${_expect_config}'")
  endif()
  if(NOT _got_worktree STREQUAL "${_expect_worktree}")
    list(APPEND _why "source-worktree is '${_got_worktree}', expected '${_expect_worktree}'")
  endif()
  # A revision must be a real commit hash, not the 'unknown' fallback: an
  # artifact whose provenance says 'unknown' cannot be traced to any source
  # state at all, which defeats the whole stamp.
  if(_got_rev STREQUAL "unknown" OR NOT _got_rev MATCHES "^[0-9a-f]+$")
    list(APPEND _why "source-revision is '${_got_rev}', not a commit hash")
  endif()
  # ⚠️ GATE B ROUND 2 (R2-F2): the revision was PREVIOUSLY only shape-checked
  # above (any hex string passed) -- never compared to the build under test.
  # Compared here against the INDEPENDENTLY measured HEAD, so a stale artifact
  # from a different commit in the shared artifact directory is rejected.
  if(NOT _got_rev STREQUAL "${_expect_revision}")
    list(APPEND _why "source-revision is '${_got_rev}', expected '${_expect_revision}'")
  endif()

  if(_why STREQUAL "")
    set(${_out_ok} TRUE PARENT_SCOPE)
  else()
    set(${_out_ok} FALSE PARENT_SCOPE)
  endif()
  set(${_out_why} "${_why}" PARENT_SCOPE)
endfunction()

# ── PASS leg — the package matches the build that produced it ────────────────
# ⚠️ GATE B ROUND 2 (R2-F2): the expected worktree state used to be read back
# OUT OF the artifact itself (`_actual_worktree`), which made this leg pass by
# construction — it could not fail on a stale stamp. Both expectations below
# (`_fixpp_exp_worktree`, `_fixpp_exp_rev`) are the INDEPENDENT measurement
# taken at the very top of this script, before the artifact was even built.
_fixpp_check_provenance("${FIXPP_BUILD_TYPE}" "${_fixpp_exp_worktree}" "${_fixpp_exp_rev}" _ok _why)
if(NOT _ok)
  string(REPLACE ";" "\n  " _why_pretty "${_why}")
  message(FATAL_ERROR "T060 PASS leg FAILED:\n  ${_why_pretty}")
endif()
message(STATUS "T060 PASS leg: provenance matches the producing build "
               "(configuration=${FIXPP_BUILD_TYPE}, worktree=${_fixpp_exp_worktree}, "
               "revision=${_fixpp_exp_rev})")

# ── User decision 2026-08-03: WARN-AND-ACCEPT on a dirty worktree ────────────
# "Require a clean tree for any artifact a witness accepts" is unrunnable as a
# standing local gate — a working branch is dirty by definition, and this
# bundle already rejected that exact shape in writing
# (run_telemetry_provenance_witness.cmake:111-115 — "A gate that cannot pass
# in the environment it runs in is not a strict gate; it is a broken one.").
# The operative discriminator against R2-F1 is the independent comparison
# above, not a clean-tree precondition: clean-at-configure → dirty-at-pack-time
# now correctly FAILS (the independently measured worktree no longer matches
# what a stale, configure-time-frozen stamp would have said), while a
# consistently-dirty working-branch build still passes with a warning.
if(_fixpp_exp_worktree STREQUAL "dirty")
  message(WARNING
    "T060: this package was built from a DIRTY worktree, so its source-revision "
    "does not fully identify its inputs. Correct for a working branch; such a "
    "package must not be published.")
endif()

# ── RED leg — a package from a DIFFERENT CONFIGURATION is rejected ───────────
if(FIXPP_BUILD_TYPE STREQUAL "Debug")
  set(_wrong_config "Release")
else()
  set(_wrong_config "Debug")
endif()

_fixpp_check_provenance("${_wrong_config}" "${_fixpp_exp_worktree}" "${_fixpp_exp_rev}" _red_ok _red_why)
if(_red_ok)
  message(FATAL_ERROR
    "T060 RED leg FAILED TO FAIL: a package stamped '${FIXPP_BUILD_TYPE}' was accepted "
    "as '${_wrong_config}'. Provenance is not discriminating by configuration, so a "
    "stale artifact from another configuration in the shared artifact directory "
    "would be consumed silently.")
endif()
string(REPLACE ";" "; " _red_why_pretty "${_red_why}")
message(STATUS "T060 RED leg observed: ${_red_why_pretty}")

# ── RED leg 2 — a different WORKTREE STATE is rejected ───────────────────────
# Separate from the configuration leg on purpose: (configuration, revision) alone
# cannot distinguish two builds either side of an uncommitted edit, and this is
# the field that closes that hole. If it were not checked independently, the gate
# would pass on exactly the staleness case FR-021a calls the LIKELY one.
if(_fixpp_exp_worktree STREQUAL "clean")
  set(_wrong_worktree "dirty")
else()
  set(_wrong_worktree "clean")
endif()
_fixpp_check_provenance("${FIXPP_BUILD_TYPE}" "${_wrong_worktree}" "${_fixpp_exp_rev}" _red2_ok _red2_why)
if(_red2_ok)
  message(FATAL_ERROR
    "T060 RED leg 2 FAILED TO FAIL: a package stamped worktree='${_fixpp_exp_worktree}' "
    "was accepted as '${_wrong_worktree}'. Worktree state is not discriminating, so "
    "two packages from the same commit either side of an uncommitted edit are "
    "indistinguishable — the likely staleness case, not the exotic one.")
endif()
string(REPLACE ";" "; " _red2_why_pretty "${_red2_why}")
message(STATUS "T060 RED leg 2 observed: ${_red2_why_pretty}")

# ── RED leg 3 — a different SOURCE REVISION is rejected ──────────────────────
# ⚠️ GATE B ROUND 2 (R2-F2): the revision axis had NO check and NO red leg at
# all before this fix — any hex string passed. Fed a WRONG *expected* value
# against the REAL, unmodified artifact (never a doctored `_prov_text`):
# `_fixpp_check_provenance` reads the artifact text as a free variable from
# the enclosing scope, so tampering the artifact text would test the parser,
# not the gate — exactly what this file's own header (above) refuses to do
# for the other two fields.
set(_wrong_revision "0000000000000000000000000000000000000000")
if(_wrong_revision STREQUAL "${_fixpp_exp_rev}")
  # Astronomically unlikely (would require HEAD to literally be the all-zero
  # hash), but fail loudly rather than silently pass a non-discriminating leg.
  message(FATAL_ERROR "T060: sentinel wrong-revision collides with the real HEAD; pick another sentinel")
endif()
_fixpp_check_provenance("${FIXPP_BUILD_TYPE}" "${_fixpp_exp_worktree}" "${_wrong_revision}" _red3_ok _red3_why)
if(_red3_ok)
  message(FATAL_ERROR
    "T060 RED leg 3 FAILED TO FAIL: a package stamped revision='${_fixpp_exp_rev}' "
    "was accepted as '${_wrong_revision}'. Source-revision is not discriminating, so "
    "a stale artifact from an earlier commit in the shared artifact directory "
    "would be consumed silently.")
endif()
string(REPLACE ";" "; " _red3_why_pretty "${_red3_why}")
message(STATUS "T060 RED leg 3 observed: ${_red3_why_pretty}")

message(STATUS "fixpp::packaging::provenance: OK")
