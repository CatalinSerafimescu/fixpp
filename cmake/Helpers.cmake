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
endfunction()
