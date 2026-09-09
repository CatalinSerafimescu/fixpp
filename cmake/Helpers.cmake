# cmake/Helpers.cmake
# Per-preset compile flags wired by CMakePresets.json via cache variables.
# Phase 3 default — revisit if additional per-preset flag sets are needed.

# Read preset name from environment (set by CI) or from cmake --preset.
# CMakePresets.json sets FIXPP_PRESET via cacheVariables.

# ── Common strict flags for Clang/GCC ────────────────────────────────────────
function(fixpp_apply_common_flags target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wno-unused-parameter   # phase-3 stubs generate lots of these
      -fno-exceptions         # Phase 3 default — revisit if module specs mandate exceptions
    )
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${target} PRIVATE
      /W4
      /WX
      /permissive-
      /Zc:__cplusplus
    )
  endif()
endfunction()

# ── Werror — turned on in CI via FIXPP_WERROR cache variable ─────────────────
option(FIXPP_WERROR "Treat compile warnings as errors" OFF)

function(fixpp_maybe_werror target)
  if(FIXPP_WERROR)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(${target} PRIVATE -Werror)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
      target_compile_options(${target} PRIVATE /WX)
    endif()
  endif()
endfunction()

# ── Fuzz corpus replay registration (#213) ───────────────────────────────────
#
#   fixpp_add_fuzz_replay(<test-name> <fuzz-target> <input-dir>)
#
# Registers ONE libFuzzer corpus/crash-reproducer replay as an ordinary ctest
# test. Shared rather than copied because the two call sites
# (tests/fuzz/, tests/config/fuzz/) must agree on properties that have
# CORRECTNESS consequences, not merely cosmetic ones:
#
# ⚠️ REPLAY, NOT A FUZZING RUN. `-runs=0` executes each input once and exits
# non-zero on any crash: fast, deterministic, CI-safe. It must never become a
# time- or run-bounded campaign here — a campaign belongs in a dedicated job,
# not in the suite every lane runs. Centralised so that stays one decision.
#
# ⚠️ ARTIFACTS ARE REDIRECTED OUT OF THE SOURCE TREE, by BOTH the working
# directory and -artifact_prefix. On a crash libFuzzer writes `crash-<sha1>` to
# its CWD; without these a red replay litters the repo root with untracked files
# (observed while proving this instrument could go RED at all). A future edit to
# the artifact layout or the timeout is now one edit, not two that can drift
# silently with nothing checking they agree.
#
# ⚠️ `-runs=0` EXECUTES N+1 INPUTS, not N — libFuzzer adds the empty input.
# Anything that ever asserts a count must account for that, or it will report an
# off-by-one and be "fixed" in the wrong direction.
function(fixpp_add_fuzz_replay test_name fuzz_target input_dir)
  if(NOT TARGET ${fuzz_target})
    message(FATAL_ERROR
      "fixpp_add_fuzz_replay(${test_name}): target '${fuzz_target}' does not exist, so "
      "nothing can replay '${input_dir}'. Inputs that cannot be replayed are the "
      "accumulated-but-unenforced state #213 was filed about.")
  endif()

  set(_artifacts "${CMAKE_BINARY_DIR}/fuzz-replay-artifacts")
  file(MAKE_DIRECTORY "${_artifacts}")

  add_test(NAME ${test_name}
           COMMAND ${fuzz_target}
                   -runs=0
                   "-artifact_prefix=${_artifacts}/${test_name}-"
                   "${input_dir}")
  # ⚠️ UBSan IS RECOVERABLE BY DEFAULT, AND THESE BINARIES CARRY IT.
  # The fuzz targets are built with -fsanitize=fuzzer,address,undefined, but they
  # run on the ASan lane, whose preset adds only -fsanitize=address and sets no
  # UBSAN_OPTIONS. So without this, a UBSan finding in a replayed seed prints
  # `runtime error:`, execution continues, the process exits 0 and the replay is
  # GREEN — verbatim the defect #268 records for the ubsan lanes ("this lane ran
  # the whole suite for its entire existence and could not go red on a UBSan
  # finding"), reintroduced on a different lane by the change that enabled these
  # binaries.
  #
  # FAIL_REGULAR_EXPRESSION is the belt to that braces: halt_on_error makes the
  # process exit non-zero, and the pattern reddens the test even if some future
  # option or a suppression file lets execution continue.
  set_tests_properties(${test_name} PROPERTIES
    LABELS "fuzz"
    TIMEOUT 300
    WORKING_DIRECTORY "${_artifacts}"
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
    FAIL_REGULAR_EXPRESSION "runtime error:")

  # #408: record that this target now has SOMETHING replaying it. This is the
  # only place that knows it, and recording it here is what lets the
  # completeness assertion below be shape-agnostic — see that function's header.
  set_property(GLOBAL APPEND PROPERTY FIXPP_FUZZ_TARGETS_WITH_REPLAY "${fuzz_target}")
endfunction()

# ── #408: every fuzz harness must actually be REPLAYED by something ──────────
#
#   fixpp_assert_every_fuzz_harness_replays(<exempt-list-var-name>)
#
# A libFuzzer harness that no ctest replays is COMPILED by the fuzz lane
# (`FIXPP_BUILD_FUZZ=ON` on `linux-clang-asan`) and never executed: it stays
# green forever having run not one input. Nothing is missing, nothing errors, no
# job goes red — there is simply nothing to find. That is how #405's genuine hang
# in `fuzz_message_store` survived, and #213 closed only the neighbouring gap
# (inputs that existed but were never replayed).
#
# ⚠️ THE INVARIANT IS "A REPLAY EXISTS", NOT "A CORPUS DIRECTORY EXISTS".
# The first draft of this check tested `IS_DIRECTORY corpus/<name>/`, which is a
# PROXY for the property that matters and is true only for one of the two fuzz
# suites: tests/fuzz/ names inputs `corpus/<harness>/`, while tests/config/fuzz/
# names them `crashes/` and registers `fuzz_replay_toml_crashes` against
# `fuzz_toml_loader` — a corpus deliberately NOT named after its harness. A check
# written against the directory convention therefore could not be shared, and
# widening it to glob both trees would MISCOUNT while looking authoritative (the
# error #408 records from PR #407's first draft). Asking `fixpp_add_fuzz_replay`
# what it actually registered removes the coupling to any naming convention, so
# the same check serves both suites and keeps working if either renames its
# inputs.
#
# ⚠️ ENUMERATE FROM THE BUILDSYSTEM, NEVER FROM A HAND-WRITTEN LIST. A second
# list of harness names would be derived from the same `add_executable` calls it
# is meant to check, so it could never disagree with them — a tautology that
# reports PASS because it cannot report anything else. `BUILDSYSTEM_TARGETS` is
# the set of targets the calling directory actually defined, so a new harness is
# visible the moment it is written, whether or not its author knew this exists.
#
# ⚠️ CALL IT DEFERRED — `cmake_language(DEFER CALL ...)` — WHICH IS LOAD-BEARING.
# `BUILDSYSTEM_TARGETS` reports only the targets defined SO FAR, and appending a
# new `add_executable` to the end of a CMakeLists is the most natural way to add
# a harness. Called inline this check is silently GREEN for anything declared
# below it: that is not hypothetical, a forced arm proved exactly that against
# the inline first draft. The considered alternative was to fold the check into
# the corpus-registration loop, which by its position already runs after every
# harness is declared; it was rejected because that loop is keyed on corpus
# DIRECTORIES and inverting it would rewrite working registration code and still
# need a second pass to catch an orphan corpus. Deferring is the smaller change
# that makes placement stop mattering.
#
# The argument is the NAME of a list variable holding harness base-names (the
# target name minus its `fuzz_` prefix) deliberately shipped without a replay.
# Each entry is a decision with a reason, not a backlog.
function(fixpp_assert_every_fuzz_harness_replays exempt_var)
  set(_exempt "${${exempt_var}}")
  get_property(_replayed GLOBAL PROPERTY FIXPP_FUZZ_TARGETS_WITH_REPLAY)

  get_property(_dir_targets
               DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
               PROPERTY BUILDSYSTEM_TARGETS)

  set(_harnesses "")
  foreach(_tgt IN LISTS _dir_targets)
    if(_tgt MATCHES "^fuzz_")
      string(REGEX REPLACE "^fuzz_" "" _name "${_tgt}")
      list(APPEND _harnesses "${_name}")
    endif()
  endforeach()
  # Sorted for the STATUS line only -- both loops below use IN_LIST, which is
  # order-independent. Nothing here depends on the ordering.
  list(SORT _harnesses)
  list(LENGTH _harnesses _count)
  message(STATUS "fixpp: fuzz harnesses enumerated in ${CMAKE_CURRENT_SOURCE_DIR} "
                 "(${_count}): ${_harnesses}")

  # ⚠️ AN EMPTY ENUMERATION IS AN INSTRUMENT FAILURE, NOT A CLEAN BUILD. If
  # BUILDSYSTEM_TARGETS ever stops returning these targets (a refactor moving the
  # add_executable calls into a subdirectory, a CMake change), every loop below
  # iterates nothing and passes — the unguarded state restored silently, which is
  # the exact shape this whole function exists to prevent.
  if(_count EQUAL 0)
    message(FATAL_ERROR
      "${CMAKE_CURRENT_SOURCE_DIR}: ZERO fuzz harnesses were enumerated from "
      "BUILDSYSTEM_TARGETS, so the completeness check would pass by iterating nothing. "
      "The enumeration is broken, not the tree.")
  endif()

  foreach(_h IN LISTS _harnesses)
    if("fuzz_${_h}" IN_LIST _replayed)
      continue()
    endif()
    if("${_h}" IN_LIST _exempt)
      continue()
    endif()
    message(FATAL_ERROR
      "${CMAKE_CURRENT_SOURCE_DIR}: harness `fuzz_${_h}` is built but NO ctest replays it, so "
      "CI compiles it and never runs it — it reports green having executed nothing (#408; that "
      "is how #405's hang survived). Register a replay for it with fixpp_add_fuzz_replay() — in "
      "tests/fuzz/ that means adding `corpus/${_h}/` with at least one seed AND a matching "
      "`!tests/fuzz/corpus/${_h}/` negation in .gitignore — or name `${_h}` in the "
      "${exempt_var} list with the reason it cannot be replayed.")
  endforeach()

  # ⚠️ THE EXEMPTION LIST IS CHECKED IN BOTH DIRECTIONS, because a list that is
  # only ever read when something is missing rots the moment the tree moves past
  # it — a survey presented as a property. A name here that no longer names a
  # harness, or that has since gained a replay, is a stale claim that reads as a
  # live decision.
  foreach(_e IN LISTS _exempt)
    if(NOT "${_e}" IN_LIST _harnesses)
      message(FATAL_ERROR
        "${CMAKE_CURRENT_SOURCE_DIR}: ${exempt_var} names `${_e}`, but no `fuzz_${_e}` target "
        "exists in this directory. The harness was renamed or removed; drop the stale exemption "
        "rather than leaving it to describe a tree that no longer exists.")
    endif()
    if("fuzz_${_e}" IN_LIST _replayed)
      message(FATAL_ERROR
        "${CMAKE_CURRENT_SOURCE_DIR}: `${_e}` is listed in ${exempt_var}, but a replay for "
        "`fuzz_${_e}` is now registered. The exemption is obsolete — remove it, so the list "
        "keeps meaning `deliberately unreplayed` rather than `once was`.")
    endif()
  endforeach()
endfunction()
