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

# ── 046-atomic-shared-ptr T014 (FR-011a): OTel toggle ────────────────────────
# FIXPP_BUILD_OTEL=ON (default): the OTel integration is compiled and linked.
#   This is the PRIMARY acceptance path and must stay unchanged on normal builds.
# FIXPP_BUILD_OTEL=OFF: the src/otel subdirectory is excluded, no OTel SDK is
#   required. Used by the libc++ Tier-2 lane (linux-clang-libc++) via
#   -o fixpp/*:with_otel=False + -DFIXPP_BUILD_OTEL=OFF so the full SDK rebuild
#   under libc++ is deferred. The fallback path in engine.cpp is still compiled.
#
# Consistency check: FIXPP_BUILD_OTEL and the Conan-provided opentelemetry-cpp
# package must agree. A mismatch (e.g. OTel-ON but SDK absent, or vice-versa)
# is a misconfiguration and fails immediately before any target is defined.
option(FIXPP_BUILD_OTEL "Build the OpenTelemetry integration" ON)
if(FIXPP_BUILD_OTEL AND NOT TARGET opentelemetry-cpp::api)
  message(FATAL_ERROR
    "FIXPP_BUILD_OTEL=ON but the opentelemetry-cpp Conan package was not found "
    "(opentelemetry-cpp::api target is absent). Either install the package "
    "(`conan install . -o fixpp/*:with_otel=True ...`) or disable OTel "
    "(`-DFIXPP_BUILD_OTEL=OFF -o fixpp/*:with_otel=False`).")
endif()
if(NOT FIXPP_BUILD_OTEL AND TARGET opentelemetry-cpp::api)
  message(FATAL_ERROR
    "FIXPP_BUILD_OTEL=OFF but the opentelemetry-cpp Conan package IS present "
    "(opentelemetry-cpp::api target exists). Either remove the package "
    "(`conan install . -o fixpp/*:with_otel=False ...`) or enable OTel "
    "(`-DFIXPP_BUILD_OTEL=ON` — the default).")
endif()
# Propagate FIXPP_BUILD_OTEL as a 0/1 compile definition to all targets so
# #if FIXPP_BUILD_OTEL guards in engine.cpp are evaluated correctly. Default
# is 1 (OTel ON) to match the CMake option default and keep all existing code
# paths unchanged when the option is not explicitly passed.
add_compile_definitions(FIXPP_BUILD_OTEL=$<BOOL:${FIXPP_BUILD_OTEL}>)

# Windows: <windows.h> (pulled transitively via openssl, and directly by the
# file_store Win32 path) defines min/max as macros, which break std::min/std::max
# (e.g. include/fixpp/transport/listener_events.hpp). NOMINMAX suppresses them;
# WIN32_LEAN_AND_MEAN trims the rest of the windows.h surface. Global so every TU
# that transitively includes windows.h is covered.
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ── 046-atomic-shared-ptr (NFR-017): atomic_shared_ptr force-mode overrides ───
# Single target-level mode applied UNIFORMLY to the whole build via a global
# compile definition (research D-9 / contract CT-4 — never a per-TU define).
# FALLBACK forces the sharded-mutex path on a P0718-capable toolchain so the
# fallback is continuously regression-exercised without a libc++ host; NATIVE is
# a documented (untested) escape hatch. Mutually exclusive (the detect header
# also #errors if both are set). The guard TU (src/core/sync/atomic_shared_ptr.cpp)
# is ALWAYS compiled regardless of mode (always-ship-guard), so a forced-fallback
# build always finds its shard_guard symbols at link.
option(FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK "Force the atomic_shared_ptr sharded-mutex fallback path" OFF)
option(FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE "Force the atomic_shared_ptr native std alias path" OFF)
if(FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK AND FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE)
  message(FATAL_ERROR "FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK and FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE are mutually exclusive.")
endif()
if(FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
  add_compile_definitions(FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK)
endif()
if(FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE)
  add_compile_definitions(FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE)
endif()

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
if(FIXPP_BUILD_OTEL)
  add_subdirectory(src/otel)
else()
  # Stub fixpp_otel target so consumers (session, capi, tests) that link it
  # unconditionally still configure cleanly when OTel is disabled. The stub is
  # an empty INTERFACE library — no headers, no SDK symbols, no link edges.
  add_library(fixpp_otel INTERFACE)
  add_library(fixpp::otel ALIAS fixpp_otel)
endif()
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
  # Windows defaults to a 1 MiB stack; several tests put large parse arenas on
  # the stack (e.g. the dictionary suites' 1–4 MiB std::array buffers) that fit
  # comfortably under Linux's 8 MiB default but overflow on Windows (a __chkstk
  # crash, worsened by MSVC debug's fat frames). Reserve 8 MiB to match Linux —
  # for test executables only (placed after the library targets) and on MSVC
  # only. /STACK also sets the default reserve for std::thread-created threads.
  if(MSVC)
    add_link_options(/STACK:8388608)
  endif()
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
  if(FIXPP_BUILD_OTEL)
    add_subdirectory(tests/otel)
  endif()
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
