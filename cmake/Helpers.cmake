# cmake/Helpers.cmake
# Per-preset compile flags wired by CMakePresets.json via cache variables.
# Phase 3 default — revisit if additional per-preset flag sets are needed.

# Read preset name from environment (set by CI) or from cmake --preset.
# CMakePresets.json sets FIXPP_PRESET via cacheVariables.

# ── Common strict flags (#481) ───────────────────────────────────────────────
#
# Applied to every first-party target by fixpp_apply_common_flags_to_all_targets()
# below. Warnings only: promoting them to errors is FIXPP_WERROR's job
# (fixpp_maybe_werror), so -DFIXPP_WERROR=OFF leaves every warning a warning.
#
# ⚠️ CXX ONLY, via generator expressions, for the reason the -Wattributes block
# below gives: this repo has a C target (`mallocnesia`).
#
# ⚠️ PREPENDED (BEFORE), so a target's own `-Wno-<x>` still wins. Clang applies
# warning flags in command-line order, so a group flag placed AFTER a
# target-specific `-Wno-<x>` re-enables <x>; GCC keeps the explicit flag in
# either order. Check by compiling a TU with an unused parameter under
# `-Wno-unused-parameter -Wextra` and under the reverse order.
function(fixpp_apply_common_flags target)
  target_compile_options(${target} BEFORE PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall>
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wextra>
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wpedantic>
    # MSVC: no /WX here; fixpp_maybe_werror adds it under FIXPP_WERROR
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:__cplusplus>
    # C5030 "attribute is not recognized": the tree spells attributes MSVC does
    # not implement ([[clang::lifetimebound]], [[gnu::used]]). A misspelled one
    # stays a hard error where the attribute is understood: clang's
    # -Wunknown-attributes is on by default and -Werror promotes it.
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd5030>
    # C4324 "structure was padded due to alignment specifier": the padding is
    # what an alignas() member asks for.
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd4324>
  )
  # The CRT "unsafe"/POSIX-name C4996 diagnostics (getenv, fopen, getpid): the
  # tree calls these portable C/POSIX functions on purpose. The two macros only
  # gate the CRT's own deprecation annotations; a use of a [[deprecated]]
  # declaration still raises C4996.
  target_compile_definitions(${target} PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:_CRT_NONSTDC_NO_WARNINGS>
  )
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

# ── GCC does not know the `clang::` attribute namespace (#439) ───────────────
#
# The tree spells `[[clang::lifetimebound]]` in hand-written headers AND the
# codegen emitter writes it into every generated accessor, so the diagnostic
# count is dominated by generated code and moves with each regeneration. GCC
# parses the attribute, cannot act on it, and says so with `-Wattributes` --
# which is ON BY DEFAULT, not part of -Wall. Without this suppression `-Werror`
# on GCC is unusable.
#
# ⚠️ SUPPRESSED BY ATTRIBUTE, NOT BY NAMESPACE. `-Wno-attributes=clang::` would
# also swallow a MISSPELLED attribute in that namespace -- `[[clang::lifetimebond]]`
# compiles silently under it, on the very lane that exists to be strict. Naming
# the one attribute keeps that diagnostic. Measured three ways on GCC 13.3:
#   -Wno-attributes=clang::lifetimebound + [[clang::lifetimebound]] -> silent
#   -Wno-attributes=clang::lifetimebound + [[clang::lifetimebond]]  -> STILL errors
#   -Wno-attributes=clang::              + [[clang::lifetimebond]]  -> swallowed
# `lifetimebound` is currently the ONLY clang:: attribute in the tree; a second
# one means adding a second entry here, deliberately. Re-derive the population:
#   grep -rhoE '\[\[clang::[a-zA-Z_]+' include src tests tools perf | sort -u
#
# ⚠️ CXX ONLY, via a generator expression. `add_compile_options` is
# directory-scoped and reaches every language; this repo has a C target
# (`mallocnesia`). A configuration pairing GNU C++ with a non-GNU C compiler
# would hand this GCC-only spelling to that compiler, which rejects it as an
# unknown warning option -- fatal under -Werror.
#
# ⚠️ THE SUPPORT CHECK IS A BEHAVIOURAL PROBE, NOT A VERSION NUMBER. An earlier
# draft refused GCC < 13, on the belief that the scoped `-Wno-attributes=` form
# arrived in 13. That is a claim about other people's compilers that nothing
# here can re-check, it was disputed as wrong for GCC 12, and a version test
# cannot see a backport or a vendor build anyway. So compile the attribute WITH
# the flag under -Werror and ask whether it is silent -- the property actually
# depended upon. ⚠️ A bare flag-ACCEPTANCE test would not do: GCC accepts an
# unknown `-Wno-*` silently unless some other diagnostic fires, so it would pass
# on a compiler where the suppression does nothing.
#
# ⚠️ THE GATE ON FIXPP_WERROR IS A CACHE DECISION, NOT A LOGICAL ONE -- the
# suppression is correct on GCC unconditionally. GCC can never act on a
# `clang::` attribute, with or without -Werror; read this as "applied where it
# is load-bearing", NOT as "only needed under -Werror".
#
# What the gate buys, measured rather than assumed. Adding a flag moves EVERY
# compile command line, so a lane's ccache restore still HITs while every entry
# misses -- the signature ci/ccache-stats.sh calls pathological. Two lanes
# assert a 70% FATAL floor: python-wheel-build (tier1.yml) and all four
# linux-clang-libc++* legs (tier3-libcxx.yml). The libc++ legs are clang, so
# this flag never reaches them. python-wheel-build is GNU and never sets
# FIXPP_WERROR. When this gate was written, that lane's cache identity did not
# cover the flag surface, so ungated it would have breached its floor on a HIT.
#
# ⚠️ AMENDED BY #482 (flag surface added to the ccache tag): every ccache tag,  # claim-ok: supersede pointer naming the decision that amends this gate
# the wheel lane's included, now carries a digest of the compile-flag surface
# (ci/ccache-flag-surface.py), and this block's condition is part of it.
# Deleting the gate now ROTATES the tags: the restore MISSes, the floor exempts
# the MISS, and the cost is one cold build, not a breach and not GHCR tag
# surgery. The CMake half of the surface is shared by every ccache lane, so
# that cold build lands on every lane at once, not only on the GNU one.
#
# ⚠️ THE CONDITION, so this does not rot into a rule nobody can re-check: the
# gate keeps the command lines of some floored lane byte-identical only while
# that lane is GNU and does not set FIXPP_WERROR. Re-derive with
#   grep -rn "ccache-stats.sh" .github/workflows/   # which callers pass a floor
# and check that lane's compiler and FIXPP_WERROR. If no floored lane is GNU any
# more, DELETE the gate -- an unconditional suppression is the better mechanism.
# Deleting it while such a lane exists is also safe since #482; the price is  # claim-ok: condition (the flag digest is in the tag), not history
# the one cold build above.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND FIXPP_WERROR)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "-Werror -Wno-attributes=clang::lifetimebound")
  check_cxx_source_compiles(
    "struct S { int v; const int& f() const [[clang::lifetimebound]] { return v; } };
     int main() { return 0; }"
    FIXPP_GCC_SUPPRESSES_CLANG_LIFETIMEBOUND)
  unset(CMAKE_REQUIRED_FLAGS)

  if(NOT FIXPP_GCC_SUPPRESSES_CLANG_LIFETIMEBOUND)
    message(FATAL_ERROR
      "FIXPP_WERROR=ON with GCC ${CMAKE_CXX_COMPILER_VERSION}, but this compiler "
      "does not silence [[clang::lifetimebound]] under "
      "-Wno-attributes=clang::lifetimebound. Every such attribute in the tree -- "
      "hand-written and codegen-emitted alike -- is then a -Wattributes error, so "
      "this configuration cannot build. Use a GCC that supports the scoped "
      "-Wno-attributes= form, or configure with -DFIXPP_WERROR=OFF.")
  endif()

  add_compile_options(
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-attributes=clang::lifetimebound>)
endif()

# ── Buildsystem target walk ──────────────────────────────────────────────────
#
#   fixpp_collect_buildsystem_targets(<out-var> <type-regex>)
#
# Sets <out-var> to every target defined so far whose TYPE matches <type-regex>,
# walking each directory's BUILDSYSTEM_TARGETS recursively from the source root.
# Callers apply their own filter and action, and must run DEFERRED (see #417).
function(fixpp_collect_buildsystem_targets out_var type_regex)
  set(_dirs "${CMAKE_SOURCE_DIR}")
  set(_found "")
  while(_dirs)
    list(POP_FRONT _dirs _dir)
    get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    list(APPEND _dirs ${_subdirs})
    get_property(_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_tgt IN LISTS _targets)
      get_target_property(_type ${_tgt} TYPE)
      if(_type MATCHES "${type_regex}")
        list(APPEND _found "${_tgt}")
      endif()
    endforeach()
  endwhile()
  set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

# ── #417: FIXPP_WERROR reaches every first-party compiled target ─────────────
#
#   fixpp_apply_werror_to_all_targets()   — call DEFERRED from the top-level
#                                           CMakeLists.txt (see below)
#
# `fixpp_maybe_werror` used to have no call sites, so FIXPP_WERROR=ON (set by
# every preset inheriting `_base`) turned no warning into an error anywhere.
# A per-target call list would repeat that failure the next time a target is
# added without it, so the targets are ENUMERATED from the buildsystem instead:
# every directory's BUILDSYSTEM_TARGETS, recursively from the source root.
#
# ⚠️ CALL IT DEFERRED — `cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR}
# CALL ...)` — for the reason `fixpp_assert_every_fuzz_harness_replays` gives:
# BUILDSYSTEM_TARGETS holds only the targets defined so far, so an inline call
# would silently skip everything declared after it.
#
# ⚠️ OPT-OUT IS PER TARGET AND CARRIES A REASON. Set the target property
# FIXPP_WERROR_EXEMPT to the reason. The deliberate case is a negative-compile
# probe (a WILL_FAIL test whose passing state is "the build failed"): a blanket
# -Werror would let ANY stray warning fail that build, so the probe would stay
# green after the diagnostic it witnesses was gone.
#
# ⚠️ AN EMPTY ENUMERATION IS AN INSTRUMENT FAILURE: if the walk ever finds no
# compiled target, FIXPP_WERROR is inert again with nothing saying so.
function(fixpp_apply_werror_to_all_targets)
  if(NOT FIXPP_WERROR)
    return()
  endif()

  fixpp_collect_buildsystem_targets(_targets
    "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
  set(_applied 0)
  set(_exempt "")
  foreach(_tgt IN LISTS _targets)
    get_target_property(_reason ${_tgt} FIXPP_WERROR_EXEMPT)
    if(_reason)
      list(APPEND _exempt "${_tgt}")
      continue()
    endif()
    fixpp_maybe_werror(${_tgt})
    math(EXPR _applied "${_applied} + 1")
  endforeach()

  if(_applied EQUAL 0)
    message(FATAL_ERROR
      "FIXPP_WERROR=ON but no compiled target was enumerated, so no warning would fail "
      "a build. The target walk is broken, not the tree (#417).")
  endif()
  message(STATUS "fixpp: FIXPP_WERROR applied to ${_applied} target(s); exempt: ${_exempt}")
endfunction()

# ── #481: the common strict flags reach every first-party compiled target ────
#
#   fixpp_apply_common_flags_to_all_targets()   — call DEFERRED from the
#                                                 top-level CMakeLists.txt
#
# The same enumeration as fixpp_apply_werror_to_all_targets() above, and for
# the same reason: a per-target call list misses the next target added without
# it. Unlike that walk this one is not gated on FIXPP_WERROR — the flags only
# raise warnings; whether a warning fails the build stays FIXPP_WERROR's call.
#
# ⚠️ CALL IT DEFERRED, for the reason fixpp_apply_werror_to_all_targets() gives:
# BUILDSYSTEM_TARGETS holds only the targets defined so far.
#
# ⚠️ OPT-OUT IS PER TARGET AND CARRIES A REASON. Set the target property
# FIXPP_COMMON_FLAGS_EXEMPT to the reason. Reserve it for code this project
# does not author (generated by a third-party tool); first-party warnings are
# fixed at the site. A negative-compile probe needs no exemption: added
# warnings cannot make a build that must fail succeed.
#
# ⚠️ AN EMPTY ENUMERATION IS AN INSTRUMENT FAILURE: the flags would then reach
# nothing with nothing saying so.
function(fixpp_apply_common_flags_to_all_targets)
  fixpp_collect_buildsystem_targets(_targets
    "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
  set(_applied 0)
  set(_exempt "")
  foreach(_tgt IN LISTS _targets)
    get_target_property(_reason ${_tgt} FIXPP_COMMON_FLAGS_EXEMPT)
    if(_reason)
      list(APPEND _exempt "${_tgt}")
      continue()
    endif()
    fixpp_apply_common_flags(${_tgt})
    math(EXPR _applied "${_applied} + 1")
  endforeach()

  if(_applied EQUAL 0)
    message(FATAL_ERROR
      "No compiled target was enumerated, so the common warning flags reach nothing. "
      "The target walk is broken, not the tree (#481).")
  endif()
  message(STATUS "fixpp: common warning flags applied to ${_applied} target(s); exempt: ${_exempt}")
endfunction()

# ── #508: the library code a fuzzer links must feed it coverage ──────────────
#
#   fixpp_instrument_libraries_for_fuzzing()   — call DEFERRED from the
#                                                top-level CMakeLists.txt, inside
#                                                its `if(FIXPP_BUILD_FUZZ)` block
#
# A harness's own `-fsanitize=fuzzer` instruments ITS TU only. The library TUs it
# links were compiled with the preset's sanitizer flags alone, so libFuzzer got
# no edge feedback from the code under test and could not tell that an input
# reached new library code. This adds `-fsanitize=fuzzer-no-link` (coverage
# instrumentation, no libFuzzer main) to every library target defined under
# src/.
#
# ⚠️ THE POPULATION IS BY LOCATION: a STATIC/SHARED/MODULE/OBJECT library whose
# SOURCE_DIR is src/ or below. Enumerated from the buildsystem, as in
# fixpp_apply_werror_to_all_targets() above and for the same reason — a
# hand-written list misses the next library. This leaves out the generated
# fixpp_builders_*/fixpp_validators_* libraries (declared from cmake/Codegen.cmake
# at the source root) and every tests/ and tools/ target. A library that moves
# out of src/ silently drops out; re-derive the population from the STATUS line
# this prints.
#
# ⚠️ CALL IT DEFERRED, for the reason fixpp_apply_werror_to_all_targets() gives:
# BUILDSYSTEM_TARGETS holds only the targets defined so far.
#
# Why a fuzz build needs a sanitizer: the comment above the refusal in the
# top-level CMakeLists.txt. The per-harness `fuzz_libcov_*` ctests (registered
# by fixpp_add_fuzz_replay() below) check the effect on the linked binaries.
function(fixpp_instrument_libraries_for_fuzzing)
  set(_src_root "${CMAKE_SOURCE_DIR}/src")
  fixpp_collect_buildsystem_targets(_targets
    "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
  set(_instrumented "")
  foreach(_tgt IN LISTS _targets)
    get_target_property(_tgt_dir ${_tgt} SOURCE_DIR)
    # Path-component prefix, so a sibling such as `srcgen/` is not admitted.
    cmake_path(IS_PREFIX _src_root "${_tgt_dir}" NORMALIZE _under_src)
    if(NOT _under_src)
      continue()
    endif()
    target_compile_options(${_tgt} PRIVATE -fsanitize=fuzzer-no-link)
    list(APPEND _instrumented "${_tgt}")
  endforeach()

  if(NOT _instrumented)
    message(FATAL_ERROR
      "FIXPP_BUILD_FUZZ=ON but no library target under ${_src_root} was enumerated, so the "
      "fuzzers would get no coverage feedback from library code. The target walk is broken, "
      "not the tree (#508).")
  endif()
  list(LENGTH _instrumented _count)
  message(STATUS "fixpp: -fsanitize=fuzzer-no-link applied to ${_count} src/ library "
                 "target(s): ${_instrumented}")
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

  # #508: one library-coverage check per replayed harness (checked once even if
  # replayed twice); cmake/run_fuzz_libcov_check.cmake. The object list is
  # `|`-joined because a `;` inside an add_test argument splits the value.
  get_property(_already_replayed GLOBAL PROPERTY FIXPP_FUZZ_TARGETS_WITH_REPLAY)
  if(NOT "${fuzz_target}" IN_LIST _already_replayed)
    add_test(NAME fuzz_libcov_${fuzz_target}
             COMMAND "${CMAKE_COMMAND}"
                     "-DFIXPP_FUZZ_BIN=$<TARGET_FILE:${fuzz_target}>"
                     "-DFIXPP_HARNESS_OBJECTS=$<JOIN:$<TARGET_OBJECTS:${fuzz_target}>,|>"
                     "-DFIXPP_READELF=${CMAKE_READELF}"
                     -P "${CMAKE_SOURCE_DIR}/cmake/run_fuzz_libcov_check.cmake")
    set_tests_properties(fuzz_libcov_${fuzz_target} PROPERTIES
      LABELS "fuzz")
  endif()

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
