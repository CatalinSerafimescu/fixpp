# cmake/FixppMallocnesia.cmake — the allocation-discipline gate's interceptor and the
# one registration path for every gate that uses it (fixpp#448).
#
# WHY THIS FILE EXISTS. The interceptor used to be a hand-built artifact:
# `make -C tools/mallocnesia` produced a gitignored `libmallocnesia.so`, and every gate
# was registered inside `if(EXISTS .../libmallocnesia.so)`. On any machine that had not
# run that make — every CI runner — the guarded entries were SILENTLY NEVER REGISTERED,
# and `--no-tests=error` could not notice because other tests still ran. A second group
# set LD_PRELOAD directly with no guard at all, and ld.so IGNORES an unloadable preload,
# so those ran uninstrumented and passed.
#
# Building it as a target removes the precondition rather than checking it: there is no
# longer a state in which the gates are registered against something that may not exist.
#
# ⚠️ THIS IS THE ONLY PRODUCER. The hand-run Makefile and the source-tree artifact it
# built are deleted (fixpp#448), and tools/check_alloc.py no longer searches that
# path. The .so remains gitignored and is never committed: a prebuilt binary has to
# match the runner's libc and arch, cannot be reviewed, and becomes the thing someone
# must remember to refresh when mallocnesia.c changes — the same staleness in a new
# place. Only the source is tracked.
#
# ⚠️ Linux-only AND non-sanitizer BY CONSTRUCTION, not by preference. The mechanism is
# ELF LD_PRELOAD
# symbol interposition; macOS needs DYLD_INSERT_LIBRARIES + interpose sections and
# Windows has no equivalent. Callers must not "port" this by relaxing the guard —
# a gate that registers on a platform where interposition does not happen is a gate
# that passes without measuring, which is the whole defect class this file closes.

# ⚠️ NOT SUPPORTED UNDER A SANITIZER, AND THIS IS A CORRECTNESS CONDITION, NOT A COST
# ONE. A sanitizer installs its OWN allocator ahead of this interposer, so our malloc
# hook never fires: the gates then report "interception confirmed" and catch nothing —
# a vacuous pass, which is the exact defect class fixpp#448 exists to remove.
#
# MEASURED on linux-clang-asan, and it is worth stating precisely because the obvious
# guard does not catch it: the interceptor's CONSTRUCTOR RUNS (the witness file is
# written, so `check_alloc.py` is satisfied that a preload took effect) while the
# planted allocation is NOT intercepted. Constructor execution and symbol interposition
# are different facts, and only the positive control can tell them apart — it FAILS
# there, with "interception was active yet the planted allocation was not caught".
#
# So the gates must not REGISTER on a sanitizer build. Excluding them at the ctest
# selection instead would leave them registered and runnable — and the unfiltered
# full-ctest runs on those lanes would pick them up and pass vacuously.
#
# THE CONDITION (fixpp#497): the gates are not registered when any `-fsanitize=` with a
# non-empty value appears in the flags read below, whichever sanitizer it names, including
# inside a generator expression. Naming sanitizers one by one let every unnamed one (LSan, MSan, the next)
# register the gates. The match itself, and what it does not match, is
# fixpp_mallocnesia_flags_name_sanitizer() in cmake/FixppMallocnesiaSanitizerMatch.cmake,
# tested by ci/test-mallocnesia-sanitizer-match.sh. The per-target
# `-fsanitize=fuzzer-no-link` that fixpp_instrument_libraries_for_fuzzing() adds is not
# seen here, and need not be: the top-level CMakeLists.txt refuses FIXPP_BUILD_FUZZ
# without ASan or UBSan, whose directory options are seen.
#
# Where a `-fsanitize=` can be when this file is evaluated, and so what is read:
#   - the CMAKE_<LANG>_FLAGS and CMAKE_<LINK>_LINKER_FLAGS variables, including their
#     per-config forms for the configs this build generates — these carry the cache,
#     the preset, CFLAGS/CXXFLAGS/LDFLAGS at first configure, and the Conan toolchain's
#     *_INIT values;
#   - this directory's COMPILE_OPTIONS and LINK_OPTIONS, which is where
#     cmake/Sanitizers.cmake puts FIXPP_ENABLE_{ASAN,UBSAN,TSAN}. It must therefore be
#     include()d before this file; the top-level CMakeLists.txt does so. A preset that
#     sets CMAKE_CXX_FLAGS in the cache replaces the Conan toolchain's -fsanitize, so
#     for such a preset this read is the only signal.
# A flag added later, or only to some target, is not seen. Re-check a configuration
# with `ctest --test-dir <build> -N -R _mallocnesia$`, which must list no test on a
# sanitizer build.
#
# ⚠️ C is enabled BEFORE the flag read below, not when the target is defined: until
# enable_language(C) runs, CFLAGS and the Conan toolchain's CMAKE_C_FLAGS_INIT have not
# reached CMAKE_C_FLAGS, so a C-only -fsanitize= would be missed on a first configure and
# seen on the next. `project()` declares `LANGUAGES CXX` only, so C is enabled here rather
# than widened there: adding C to the project line would run the C compiler probe on every
# configure of every preset, including the Windows and macOS ones that never build
# this target. Enabling it inside the platform guard keeps the cost where the need is.
# `enable_language` is idempotent, so the call tests/capi/CMakeLists.txt already makes
# for its own C sources is harmless — this module is included first, which merely makes
# that one the redundant call rather than this one.
if(UNIX AND NOT APPLE)
  enable_language(C)
endif()
set(_mn_flag_vars CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS
                  CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
set(_mn_configs ${CMAKE_BUILD_TYPE} ${CMAKE_CONFIGURATION_TYPES})
set(_mn_flags "")
foreach(_mn_var IN LISTS _mn_flag_vars)
  string(APPEND _mn_flags " ${${_mn_var}}")
  foreach(_mn_config IN LISTS _mn_configs)
    string(TOUPPER "${_mn_config}" _mn_config)
    string(APPEND _mn_flags " ${${_mn_var}_${_mn_config}}")
  endforeach()
endforeach()
get_directory_property(_mn_compile_options COMPILE_OPTIONS)
get_directory_property(_mn_link_options LINK_OPTIONS)
string(APPEND _mn_flags " ${_mn_compile_options} ${_mn_link_options}")
include("${CMAKE_CURRENT_LIST_DIR}/FixppMallocnesiaSanitizerMatch.cmake")
fixpp_mallocnesia_flags_name_sanitizer(_mn_sanitizer "${_mn_flags}")

if(UNIX AND NOT APPLE AND NOT _mn_sanitizer)
  include(CheckCSourceCompiles)
  check_c_source_compiles("#include <stdlib.h>
#ifndef __GLIBC__
#error not glibc
#endif
int main(void) { return 0; }" FIXPP_MALLOCNESIA_GLIBC)
  set(FIXPP_MALLOCNESIA_SUPPORTED ${FIXPP_MALLOCNESIA_GLIBC})
else()
  set(FIXPP_MALLOCNESIA_SUPPORTED FALSE)
  if(_mn_sanitizer)
    message(STATUS "fixpp: mallocnesia gates not registered — a sanitizer build "
                   "(${_mn_sanitizer}); its allocator would make them pass vacuously")
  endif()
endif()
if(UNIX AND NOT APPLE AND NOT _mn_sanitizer AND NOT FIXPP_MALLOCNESIA_SUPPORTED)
  message(STATUS "fixpp: mallocnesia gates not registered — the interceptor forwards to "
                 "glibc's allocator internals, and the controls call glibc-only entry points")
endif()

if(FIXPP_MALLOCNESIA_SUPPORTED AND NOT TARGET mallocnesia)
  add_library(mallocnesia SHARED "${CMAKE_SOURCE_DIR}/tools/mallocnesia/mallocnesia.c")
  set_target_properties(mallocnesia PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    # The name is load-bearing in diagnostics people grep for; keep it stable across
    # generators rather than inheriting a per-config decoration.
    OUTPUT_NAME "mallocnesia")
  # ⚠️ NOT sanitized and NOT covered, whatever the preset does. This library IS the
  # allocator on the paths it hooks; instrumenting it makes the sanitizer's own
  # allocator re-enter these hooks. It is also never linked into shipped artifacts.
  #
  # ⚠️ The coverage opt-outs are CLANG SPELLINGS and GCC REJECTS THEM OUTRIGHT
  # (`unrecognized command-line option '-fno-profile-instr-generate'`), so an
  # unguarded list breaks the linux-gcc-release preset at compile time, not subtly.
  # Verified by running gcc against both spellings. `-fno-sanitize=all` is common to
  # both, so only the coverage pair is conditional.
  target_compile_options(mallocnesia PRIVATE -fno-sanitize=all)
  target_link_options(mallocnesia PRIVATE -fno-sanitize=all)
  target_link_libraries(mallocnesia PRIVATE ${CMAKE_DL_LIBS})
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(mallocnesia PRIVATE -fno-profile-instr-generate
                                               -fno-coverage-mapping)
  endif()
endif()

# fixpp_add_mallocnesia_test(NAME <test> TARGET <binary-target>
#                            [LABELS <l>...] [DEPENDS <t>...] [ENVIRONMENT <e>]
#                            [EXPECT_VIOLATION [EXPECT_ENTRY <fn>]])
#
# EXPECT_ENTRY (positive controls only): the violation must be counted by the
# interceptor's <fn> hook, so a control planting through one entry point cannot pass on
# an allocation that reached another.
#
# No MAX_ALLOCS keyword: every gate wants zero, and zero is already the default in
# all three layers (check_alloc.py's argparse, and the interceptor's own `g_max`
# when MALLOCNESIA_MAX_ALLOCS is unset). A knob no call site turns is a fourth
# place for those defaults to disagree. Add it back when a gate needs a budget.
#
# The single registration path. It always routes through tools/check_alloc.py, never a
# raw LD_PRELOAD `cmake -E env` line: the wrapper is what FAILS CLOSED when the
# interceptor is absent and what requires the child's interception WITNESS. A raw
# LD_PRELOAD entry has neither property, and the group that used one is exactly the
# group that was passing uninstrumented.
#
# Every entry gets the `mallocnesia` label here, in one place, so the label and the
# gate population cannot drift apart the way they had: immediately BEFORE fixpp#448 the
# name set and the label set disagreed by more than half. That is a historical
# measurement of a fixed tree, not a description of today — re-derive the current
# state with `tools/check_mallocnesia_population.py`, which prints both sizes.
function(fixpp_add_mallocnesia_test)
  cmake_parse_arguments(_MN "EXPECT_VIOLATION" "NAME;TARGET;EXPECT_ENTRY"
                      "LABELS;DEPENDS;ENVIRONMENT" ${ARGN})
  if(NOT _MN_NAME OR NOT _MN_TARGET)
    message(FATAL_ERROR "fixpp_add_mallocnesia_test: NAME and TARGET are required")
  endif()
  if(_MN_EXPECT_ENTRY AND NOT _MN_EXPECT_VIOLATION)
    message(FATAL_ERROR "fixpp_add_mallocnesia_test: EXPECT_ENTRY needs EXPECT_VIOLATION")
  endif()
  if(NOT FIXPP_MALLOCNESIA_SUPPORTED)
    return()
  endif()

  # ⚠️ NOT a generator expression here. `$<$<BOOL:...>:--expect-violation>` evaluates to
  # the EMPTY STRING when false, and CMake passes that as a real, empty argv entry —
  # argparse then dies with `unrecognized arguments:` on every non-control gate. An
  # unquoted empty CMake variable, by contrast, drops out of the list entirely.
  set(_MN_EXTRA "")
  if(_MN_EXPECT_VIOLATION)
    set(_MN_EXTRA --expect-violation)
  endif()
  if(_MN_EXPECT_ENTRY)
    list(APPEND _MN_EXTRA --expect-entry ${_MN_EXPECT_ENTRY})
  endif()
  add_test(
    NAME ${_MN_NAME}
    COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            --binary "$<TARGET_FILE:${_MN_TARGET}>"
            --mallocnesia "$<TARGET_FILE:mallocnesia>"
            ${_MN_EXTRA})
  set_tests_properties(${_MN_NAME} PROPERTIES LABELS "mallocnesia;${_MN_LABELS}")
  if(_MN_ENVIRONMENT)
    # Passthrough, e.g. GTEST_FILTER to isolate the zero-alloc cell of a binary whose
    # other cells legitimately allocate. check_alloc.py runs the binary as a child with
    # the environment inherited, so this reaches gtest.
    set_tests_properties(${_MN_NAME} PROPERTIES ENVIRONMENT "${_MN_ENVIRONMENT}")
  endif()
  if(_MN_DEPENDS)
    set_tests_properties(${_MN_NAME} PROPERTIES DEPENDS "${_MN_DEPENDS}")
  endif()
endfunction()
