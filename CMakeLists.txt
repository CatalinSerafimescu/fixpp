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
include(cmake/fixpp_store_test.cmake)  # 008-message-store unified test helper

# ── Conan-generated toolchain (present only after `conan install`) ───────────
# The CMakePresets each point CMAKE_TOOLCHAIN_FILE to
# ${sourceDir}/build/<preset>/conan_toolchain.cmake.
# When building without presets (e.g. IDE) the user passes -DCMAKE_TOOLCHAIN_FILE.

# ── External dependencies (Conan-generated CMakeDeps) ───────────────────────
find_package(GTest CONFIG QUIET)
find_package(benchmark CONFIG QUIET)
# asio/1.36.0 — 006-async-mutex (first async/coroutine feature; user-approved
# new Conan row per [const §III.2], conanfile.py).
find_package(asio CONFIG QUIET)
# crc32c/1.1.2 — 008-message-store FileStore per-record + sentinel CRC32
# (Castagnoli polynomial 0x1EDC6F41; research D-3 / [const §III.2]).
# QUIET so the symbol exists for src/session even if the toolchain hasn't been
# regenerated yet (linker fail surfaces the real issue if the dep is missing).
find_package(Crc32c CONFIG QUIET)
# openssl/3.6.2 — 011-tls-policy [arch §4.6]. Minimum 3.0 per plan.md
# Technical Context. QUIET while tls/ symbols are still landing; src/tls
# tightens to REQUIRED once fixpp_tls grows .cpp sources.
find_package(OpenSSL 3.0 QUIET)
# opentelemetry-cpp/1.26.0 — 017-log-otel [arch §4.8] (FR-023). QUIET while
# otel/ + log/ sources are still landing; the otel module tightens to REQUIRED
# once fixpp_otel grows .cpp sources (Phase 5 / US3). Built with with_abi_v2 +
# with_prometheus + with_otlp_http (conanfile.py default_options).
find_package(opentelemetry-cpp CONFIG QUIET)

# ── 017-log-otel build options (FR-023) ──────────────────────────────────────
# FIXPP_LOG_MIN_LEVEL — compile-time minimum log level: trace=0 debug=1 info=2
# warn=3 error=4 fatal=5. Below-level call sites are eliminated by `if constexpr`
# (FR-010 / SC-003). The default is build-type-conditional (NOT a single
# hardcoded value, which would break one build type): trace (0) in Debug for
# full diagnostics, info (2) in optimized builds. Override with -D on configure.
if(NOT DEFINED FIXPP_LOG_MIN_LEVEL)
  if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(FIXPP_LOG_MIN_LEVEL 2 CACHE STRING "Compile-time minimum log level (0=trace .. 5=fatal)")
  else()
    set(FIXPP_LOG_MIN_LEVEL 0 CACHE STRING "Compile-time minimum log level (0=trace .. 5=fatal)")
  endif()
endif()
# FIXPP_LOG_SPIKE_QUILL — TS-13 quill spike backend (FR-021/FR-023). OFF by
# default; a default build MUST NOT require quill. When ON, also pass the
# conanfile option -o fixpp/*:spike_quill=True so the quill dependency resolves.
option(FIXPP_LOG_SPIKE_QUILL "Build the TS-13 quill spike backend" OFF)

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
add_subdirectory(src/core/sync)   # fixpp_sync — 006-async-mutex (depends on fixpp_core + asio)
add_subdirectory(src/dictionary)
add_subdirectory(src/wire)
add_subdirectory(src/tls)
add_subdirectory(src/transport)
add_subdirectory(src/log)
add_subdirectory(src/otel)
add_subdirectory(src/session)
add_subdirectory(src/config)  # 044-toml-session-config: opt-in TOML loader target
add_subdirectory(src/tap)
add_subdirectory(src/capi)
add_subdirectory(src/service)

# ── Codegen host tool (003-dictionary-codegen, build-only [const §III.5]) ────
# F1 Candidate A: C++23 host executable reusing the merged fixpp::dictionary
# runtime (no second XML parser — research.md D-1). Added AFTER src/dictionary
# so fixpp::dictionary exists. Never installed / never linked into the
# user-facing library.
option(FIXPP_BUILD_CODEGEN_TOOL "Build the fixpp-codegen host tool" ON)
if(FIXPP_BUILD_CODEGEN_TOOL)
  add_subdirectory(tools/codegen/fixpp-codegen)
endif()

# ── [2c §7.6] Configure-time codegen target graph (T016) ─────────────────────
# cmake/Codegen.cmake:
#   • Bootstrap-builds fixpp-codegen at configure time (execute_process into
#     ${CMAKE_BINARY_DIR}/_codegen_bootstrap) — AC-C4 / [arch §4.2] step 3.
#   • Runs the tool immediately → ${CMAKE_BINARY_DIR}/_codegen/include/fixpp/…
#   • Defines fixpp_codegen_generate (no-op marker; tests depend on it).
#   • Defines fixpp::dict::{v42,v44,v50sp2,vt11,dispatch,runtime} INTERFACE
#     targets with INTERFACE_INCLUDE_DIRECTORIES into the build tree only.
#   • Defines fixpp::dict::generate-vXX configure-time marker targets.
# Must be included AFTER src/dictionary (fixpp::dictionary exists) and AFTER
# tools/codegen/fixpp-codegen (target defined, sources present for bootstrap).
if(FIXPP_BUILD_CODEGEN_TOOL)
  include(cmake/Codegen.cmake)
endif()

# ── Bindings ─────────────────────────────────────────────────────────────────
if(FIXPP_BUILD_PYTHON)
  add_subdirectory(bindings/python)
endif()

# ── Benchmarks ───────────────────────────────────────────────────────────────
if(FIXPP_BUILD_BENCH)
  add_subdirectory(bench)
endif()

# ── Phase 9 interop benchmark driver (perf/) ───────────────────────────────────
if(FIXPP_BUILD_INTEROP_PERF)
  add_subdirectory(perf)
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
if(FIXPP_BUILD_TESTS)
  add_subdirectory(tests/core)
  add_subdirectory(tests/codegen)  # 003-dictionary-codegen US1 suites (T017-T022).
                                   # fixpp_codegen_generate is now defined in cmake/Codegen.cmake
                                   # (included above) — visible here and in all test subdirs.
  add_subdirectory(tests/integration)  # 003-dictionary-codegen US3 integration (T035).
                                       # dispatch-consuming TUs #include the generated
                                       # _dispatch/ headers (Entity 8 / I-11).
  add_subdirectory(tests/dictionary)
  add_subdirectory(tests/wire)
  add_subdirectory(tests/session)
  add_subdirectory(tests/transport)
  add_subdirectory(tests/interop)  # 016-interop-harness — session-layer interop gate (T002)
  add_subdirectory(tests/tls)
  add_subdirectory(tests/config)  # 044-toml-session-config — happy-path + selector-build (T011/T012)
  add_subdirectory(tests/log)
  add_subdirectory(tests/otel)
  add_subdirectory(tests/tap)
  add_subdirectory(tests/capi)
  add_subdirectory(tests/service)
  add_subdirectory(tests/alloc_guard)
  add_subdirectory(tests/oracle)
  add_subdirectory(tests/link)
  add_subdirectory(tests/sync)   # 006-async-mutex — 29 sync_* seams (T002)
  add_subdirectory(tests/conformance)  # 008-message-store — seam 17 store-side corpus replay (T019)
  add_subdirectory(tests/perf)         # 008-message-store — seam 14 mallocnesia alloc-guard (T050)
endif()

if(FIXPP_BUILD_FUZZ)
  add_subdirectory(tests/fuzz)
  add_subdirectory(tests/config/fuzz)  # T037 — 044-toml-session-config fuzzer
endif()

# ── CMake package export (fixpp:: namespace per [arch §7.4]) ────────────────
include(GNUInstallDirs)
install(
  DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
