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
