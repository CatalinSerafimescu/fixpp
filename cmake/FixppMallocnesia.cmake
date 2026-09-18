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
# ⚠️ Linux-only BY CONSTRUCTION, not by preference. The mechanism is ELF LD_PRELOAD
# symbol interposition; macOS needs DYLD_INSERT_LIBRARIES + interpose sections and
# Windows has no equivalent. Callers must not "port" this by relaxing the guard —
# a gate that registers on a platform where interposition does not happen is a gate
# that passes without measuring, which is the whole defect class this file closes.

if(UNIX AND NOT APPLE)
  set(FIXPP_MALLOCNESIA_SUPPORTED TRUE)
else()
  set(FIXPP_MALLOCNESIA_SUPPORTED FALSE)
endif()

if(FIXPP_MALLOCNESIA_SUPPORTED AND NOT TARGET mallocnesia)
  # ⚠️ `project()` declares `LANGUAGES CXX` only, so C is enabled HERE rather than
  # widened there: this interceptor is the sole C translation unit in the tree, it is
  # test-only, and adding C to the project line would run the C compiler probe on every
  # configure of every preset — including the Windows and macOS ones that will never
  # build this target. Enabling it inside the platform guard keeps the cost where the
  # need is.
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
#                            [MAX_ALLOCS <n>] [LABELS <l>...] [DEPENDS <t>...])
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
  cmake_parse_arguments(_MN "" "NAME;TARGET;MAX_ALLOCS" "LABELS;DEPENDS" ${ARGN})
  if(NOT _MN_NAME OR NOT _MN_TARGET)
    message(FATAL_ERROR "fixpp_add_mallocnesia_test: NAME and TARGET are required")
  endif()
  if(NOT FIXPP_MALLOCNESIA_SUPPORTED)
    return()
  endif()
  if(NOT DEFINED _MN_MAX_ALLOCS)
    set(_MN_MAX_ALLOCS 0)
  endif()

  add_test(
    NAME ${_MN_NAME}
    COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            --binary "$<TARGET_FILE:${_MN_TARGET}>"
            --mallocnesia "$<TARGET_FILE:mallocnesia>"
            --max-allocs ${_MN_MAX_ALLOCS})
  set_tests_properties(${_MN_NAME} PROPERTIES LABELS "mallocnesia;${_MN_LABELS}")
  if(_MN_DEPENDS)
    set_tests_properties(${_MN_NAME} PROPERTIES DEPENDS "${_MN_DEPENDS}")
  endif()
endfunction()
