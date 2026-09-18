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
if(UNIX AND NOT APPLE AND NOT FIXPP_ENABLE_ASAN AND NOT FIXPP_ENABLE_TSAN
   AND NOT FIXPP_ENABLE_UBSAN)
  set(FIXPP_MALLOCNESIA_SUPPORTED TRUE)
else()
  set(FIXPP_MALLOCNESIA_SUPPORTED FALSE)
endif()

if(FIXPP_MALLOCNESIA_SUPPORTED AND NOT TARGET mallocnesia)
  # ⚠️ `project()` declares `LANGUAGES CXX` only, so C is enabled HERE rather than
  # widened there: adding C to the project line would run the C compiler probe on every
  # configure of every preset, including the Windows and macOS ones that never build
  # this target. Enabling it inside the platform guard keeps the cost where the need is.
  # `enable_language` is idempotent, so the call tests/capi/CMakeLists.txt already makes
  # for its own C sources is harmless — this module is included first, which merely makes
  # that one the redundant call rather than this one.
  #
  # (An earlier revision of this comment justified the placement by claiming this is
  # "the sole C translation unit in the tree". That is false — tests/capi has three and
  # tests/consumer a fourth. The placement is right; the reason given for it was not.)
  enable_language(C)
  add_library(mallocnesia SHARED "${CMAKE_SOURCE_DIR}/tools/mallocnesia/mallocnesia.c")
  target_link_libraries(mallocnesia PRIVATE ${CMAKE_DL_LIBS})
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
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(mallocnesia PRIVATE -fno-profile-instr-generate
                                               -fno-coverage-mapping)
  endif()
endif()

# fixpp_add_mallocnesia_test(NAME <test> TARGET <binary-target>
#                            [LABELS <l>...] [DEPENDS <t>...] [ENVIRONMENT <e>]
#                            [EXPECT_VIOLATION])
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
# gate population cannot drift apart the way they had (measured on main: 18 entries by
# name, 8 by label).
function(fixpp_add_mallocnesia_test)
  cmake_parse_arguments(_MN "EXPECT_VIOLATION" "NAME;TARGET"
                      "LABELS;DEPENDS;ENVIRONMENT" ${ARGN})
  if(NOT _MN_NAME OR NOT _MN_TARGET)
    message(FATAL_ERROR "fixpp_add_mallocnesia_test: NAME and TARGET are required")
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
