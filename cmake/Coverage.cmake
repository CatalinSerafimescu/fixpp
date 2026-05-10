# cmake/Coverage.cmake
# llvm-cov + llvm-profdata wiring.
# Active only on Linux/Clang when FIXPP_ENABLE_COVERAGE=ON.
# No-op on GCC or MSVC per [const §IX.1].
# Phase 3 default — revisit if coverage toolchain changes.

option(FIXPP_ENABLE_COVERAGE "Enable llvm-cov source-based coverage" OFF)

if(FIXPP_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING
      "Coverage is only supported with Clang (llvm-cov / llvm-profdata). "
      "FIXPP_ENABLE_COVERAGE=ON has no effect on this compiler.")
  else()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)

    # Convenience targets — invoked by CI after ctest.
    find_program(LLVM_PROFDATA llvm-profdata HINTS /usr/lib/llvm-18/bin /usr/bin)
    find_program(LLVM_COV     llvm-cov      HINTS /usr/lib/llvm-18/bin /usr/bin)

    if(LLVM_PROFDATA AND LLVM_COV)
      add_custom_target(coverage-merge
        COMMAND ${LLVM_PROFDATA} merge -sparse
                ${CMAKE_BINARY_DIR}/default.profraw
                -o ${CMAKE_BINARY_DIR}/coverage.profdata
        COMMENT "Merging coverage profiles"
      )
      add_custom_target(coverage-report
        DEPENDS coverage-merge
        COMMAND ${LLVM_COV} export
                ${CMAKE_BINARY_DIR}/bin/fixpp_tests
                -instr-profile=${CMAKE_BINARY_DIR}/coverage.profdata
                -format=lcov
                > ${CMAKE_BINARY_DIR}/coverage.lcov
        COMMENT "Generating LCOV coverage report"
      )
    else()
      message(STATUS
        "llvm-profdata / llvm-cov not found; coverage targets disabled. "
        "Install llvm-18 to enable them.")
    endif()
  endif()
endif()
