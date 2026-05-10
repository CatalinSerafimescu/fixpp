# cmake/Sanitizers.cmake
# ASan / UBSan / TSan flag wiring for Clang and GCC.
# Windows MSVC ASan is handled via CMakePresets cacheVariables (/fsanitize=address).
# Phase 3 default — revisit if per-sanitizer runtime options need tuning.

option(FIXPP_ENABLE_ASAN  "Enable AddressSanitizer"     OFF)
option(FIXPP_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(FIXPP_ENABLE_TSAN  "Enable ThreadSanitizer"      OFF)

# Sanitizers only apply on Clang/GCC (Linux Tier 1 per [const §IX.2]).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")

  if(FIXPP_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()

  if(FIXPP_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
  endif()

  if(FIXPP_ENABLE_TSAN)
    if(FIXPP_ENABLE_ASAN)
      message(FATAL_ERROR "TSan and ASan cannot be combined.")
    endif()
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
  endif()

endif()
