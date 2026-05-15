cmake_minimum_required(VERSION 3.28)

project(
  fixpp
  VERSION 0.0.1
  DESCRIPTION "Modern C++23 FIX protocol library"
  LANGUAGES CXX
)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static libs may be relinked into the SHARED fixpp_capi variant (built for the
# Python Decimal oracle test, seam #8). Requires PIC on every engine archive.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Project options ──────────────────────────────────────────────────────────
include(cmake/ProjectOptions.cmake)

# ── CMake helpers & toolchain wiring ────────────────────────────────────────
include(cmake/Helpers.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Coverage.cmake)

# ── Conan-generated toolchain (present only after `conan install`) ───────────
# The CMakePresets each point CMAKE_TOOLCHAIN_FILE to
# ${sourceDir}/build/<preset>/conan_toolchain.cmake.
# When building without presets (e.g. IDE) the user passes -DCMAKE_TOOLCHAIN_FILE.

# ── External dependencies (Conan-generated CMakeDeps) ───────────────────────
find_package(GTest CONFIG QUIET)
find_package(benchmark CONFIG QUIET)

# ── Enforce output layout consistent with [arch §7.1] ────────────────────────
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ── Global include path ───────────────────────────────────────────────────────
include_directories("${CMAKE_SOURCE_DIR}/include")

# ── Testing ──────────────────────────────────────────────────────────────────
enable_testing()

# ── Modules (acyclic order per [arch §2.2]) ──────────────────────────────────
add_subdirectory(src/core)
add_subdirectory(src/dictionary)
add_subdirectory(src/wire)
add_subdirectory(src/tls)
add_subdirectory(src/transport)
add_subdirectory(src/log)
add_subdirectory(src/otel)
add_subdirectory(src/session)
add_subdirectory(src/tap)
add_subdirectory(src/capi)
add_subdirectory(src/service)

# ── Codegen host tool (003-dictionary-codegen, build-only [const §III.5]) ────
# F1 Candidate A: C++23 host executable reusing the merged fixpp::dictionary
# runtime (no second XML parser — research.md D-1). Added AFTER src/dictionary
# so fixpp::dictionary exists. Never installed / never linked into the
# user-facing library. The full [2c §7.6] target graph + configure-time
# fixpp::dict::generate-vXX custom targets land with the US1 emitters (T016).
option(FIXPP_BUILD_CODEGEN_TOOL "Build the fixpp-codegen host tool" ON)
if(FIXPP_BUILD_CODEGEN_TOOL)
  add_subdirectory(tools/codegen/fixpp-codegen)
endif()

# ── Bindings ─────────────────────────────────────────────────────────────────
if(FIXPP_BUILD_PYTHON)
  add_subdirectory(bindings/python)
endif()

# ── Benchmarks ───────────────────────────────────────────────────────────────
if(FIXPP_BUILD_BENCH)
  add_subdirectory(bench)
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
if(FIXPP_BUILD_TESTS)
  add_subdirectory(tests/core)
  add_subdirectory(tests/dictionary)
  add_subdirectory(tests/wire)
  add_subdirectory(tests/session)
  add_subdirectory(tests/transport)
  add_subdirectory(tests/tls)
  add_subdirectory(tests/log)
  add_subdirectory(tests/otel)
  add_subdirectory(tests/tap)
  add_subdirectory(tests/capi)
  add_subdirectory(tests/service)
  add_subdirectory(tests/alloc_guard)
  add_subdirectory(tests/oracle)
  add_subdirectory(tests/link)
endif()

if(FIXPP_BUILD_FUZZ)
  add_subdirectory(tests/fuzz)
endif()

# ── CMake package export (fixpp:: namespace per [arch §7.4]) ────────────────
include(GNUInstallDirs)
install(
  DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
