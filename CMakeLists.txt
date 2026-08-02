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

# ── 084 FR-011: is PACKAGING available in this configuration? ────────────────
# Computed HERE, before tests/ and before the CPack module, because both must
# agree — and the answer is needed long before cmake/FixppPackaging.cmake is
# included at the bottom of this file.
#
# FR-011 permits an OTel-OFF build as a development accelerator and forbids one
# reaching a shipped artifact. The first cut enforced that with a FATAL_ERROR in
# the packaging module, which was WRONG and would have RED-LINED THE ENTIRE
# WINDOWS CI TIER: .github/workflows/tier2.yml configures every MSVC lane with
# `-DFIXPP_BUILD_OTEL=OFF`, so an unconditional refusal fails a configuration
# the requirement explicitly PERMITS. It punished the permitted use case instead
# of the forbidden one. (Missed locally because the MSVC legs were run OTel-ON.)
#
# The invariant is unchanged and still airtight: an OTel-OFF build simply
# defines NO packaging targets and NO CPack configuration, so it CANNOT produce
# an artifact. That is a stronger guarantee than refusing to configure — there
# is nothing to invoke, rather than something that refuses when invoked.
option(FIXPP_ALLOW_OTEL_OFF_PACKAGE
       "TEST-ONLY escape hatch for proving the OTel-ON packaging gate red" OFF)
mark_as_advanced(FIXPP_ALLOW_OTEL_OFF_PACKAGE)

if(FIXPP_BUILD_OTEL OR FIXPP_ALLOW_OTEL_OFF_PACKAGE)
  set(FIXPP_PACKAGING_ENABLED ON)
else()
  set(FIXPP_PACKAGING_ENABLED OFF)
  message(STATUS
    "084 FR-011: packaging DISABLED for this OTel-OFF build — no CPack "
    "configuration and no packaging tests are defined, so no artifact can be "
    "produced. The build itself is unaffected (an OTel-OFF build is a permitted "
    "development accelerator).")
endif()

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

# ── Windows: stage opentelemetry-cpp's runtime DLLs beside the executables ────
#
# opentelemetry-cpp ships `opentelemetry_proto` as a DLL on Windows even though
# NOTHING in the Conan graph is shared=True. Without this, every executable that
# reaches the OTel chain — including ones that link no OTel target of their own
# and inherit the import transitively through fixpp::session — dies at
# STATUS_DLL_NOT_FOUND (0xc0000135) in the loader, before main(), producing no
# output whatsoever to diagnose from.
#
# Two things that look like the obvious fix and do NOT work here:
#
#   * `$<TARGET_RUNTIME_DLLS:tgt>` — the generator expression built for exactly
#     this. It expands to the DLLs of imported SHARED targets, and Conan's
#     generated config never declares `opentelemetry_proto` as an imported
#     target at all. It would expand to EMPTY and this block would silently do
#     nothing, which is worse than not having it.
#   * `${opentelemetry-cpp_BIN_DIRS_<CFG>}` — generated, but set to the empty
#     string, because the recipe does not declare `bindirs`.
#
# What IS reliable is the package folder, so glob its bin/ directly. Verified
# against the generated `opentelemetry-cpp-release-x86_64-data.cmake`:
# PACKAGE_FOLDER_RELEASE is set, BIN_DIRS_RELEASE is empty, and the DLL is at
# <package folder>/bin/opentelemetry_proto.dll.
#
# Everything lands in one directory (CMAKE_RUNTIME_OUTPUT_DIRECTORY above), and
# the Windows loader searches an executable's own directory first, so a single
# copy covers every test and tool without touching PATH. This is the fix that
# also works for a developer running the tests from a plain shell; CI has a
# `conanrun.bat` belt as well.
if(WIN32)
    set(_fixpp_runtime_dlls "")
    foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
        if(DEFINED opentelemetry-cpp_PACKAGE_FOLDER_${_cfg})
            file(GLOB _fixpp_found
                 "${opentelemetry-cpp_PACKAGE_FOLDER_${_cfg}}/bin/*.dll")
            list(APPEND _fixpp_runtime_dlls ${_fixpp_found})
        endif()
    endforeach()

    if(_fixpp_runtime_dlls)
        list(REMOVE_DUPLICATES _fixpp_runtime_dlls)
        add_custom_target(fixpp_runtime_dlls ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_fixpp_runtime_dlls} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMENT "Staging runtime DLLs next to the executables (Windows loader)"
            VERBATIM)
        message(STATUS "fixpp: staging runtime DLLs: ${_fixpp_runtime_dlls}")
    elseif(TARGET opentelemetry-cpp::api)
        # OTel is present but produced no DLLs to stage. Either the recipe
        # changed (good — say so) or the glob is looking in the wrong place
        # (bad). Either way this must be visible rather than a silent no-op:
        # the symptom otherwise is 0xc0000135 with nothing to go on.
        message(WARNING
            "fixpp: opentelemetry-cpp found but no DLLs under its package bin/. "
            "If Windows tests die with exit code 0xc0000135 "
            "(STATUS_DLL_NOT_FOUND), this glob is why — see CMakeLists.txt.")
    endif()
endif()

# ── Global include path ───────────────────────────────────────────────────────
include_directories("${CMAKE_SOURCE_DIR}/include")

# ── Install-layout variables ─────────────────────────────────────────────────
# 084 T005/FR-002a: hoisted from the packaging section below, where it sat until
# 2026-08-02. It MUST precede the add_subdirectory(src/*) block: every module
# target declares $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}> as part of its
# include interface, and an undefined CMAKE_INSTALL_INCLUDEDIR expands to the
# EMPTY STRING rather than erroring — yielding a bare $<INSTALL_INTERFACE:> that
# generates cleanly and exports targets carrying no include directory at all.
# The package would then install correctly, find_package() would succeed, and
# every consumer would fail to find a single header, with nothing in any CMake
# file looking wrong. Keep this include above line 146.
include(GNUInstallDirs)

# ── Testing ──────────────────────────────────────────────────────────────────
enable_testing()

# ── Modules (acyclic order per [arch §2.2]) ──────────────────────────────────
add_subdirectory(src/core)

# ── 060-int128-decimal-compare T002: force the portable mul_u64_wide path ────
# FIXPP_DECIMAL_FORCE_PORTABLE_MUL forces decimal.cpp's mul_u64_wide 3-way #if
# to its portable 32-bit-limb #else branch, so the Linux/Clang default build
# (which otherwise always takes the __int128 branch) can compile+cover the
# portable fallback (T012/T013). OFF by default — normal builds are unaffected.
option(FIXPP_DECIMAL_FORCE_PORTABLE_MUL "Force decimal.cpp's mul_u64_wide portable 32-bit-limb fallback" OFF)
if(FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
  target_compile_definitions(fixpp_core PRIVATE FIXPP_DECIMAL_FORCE_PORTABLE_MUL)
endif()

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

# 077/078: the v50sp2/vlatest builder completeness+roundtrip test TUs #include
# the deduped all.hpp and link the precompiled fixpp_builders_<ver> /
# fixpp_validators_<ver> libs (078). The 078 relink dropped their compile RSS
# well under the 16 GB hosted-runner limit (B-078-2 — v44 ~0.63 GiB, v50sp2/
# vlatest completeness ~2.0 GiB via all.hpp default mode, was ~3.7-4.6 GiB under
# the 077 monolith), so they build unconditionally. The #197 CI stopgap — the
# FIXPP_BUILD_HEAVY_BUILDER_TESTS gate + the heavy_builder_compile Ninja job
# pool — is removed (078 T034; CI-gated on the sanitizer-instrumented legs incl.
# the python-bindings asan/ubsan/tsan matrix). /bigobj is kept on the two
# address-of-everything completeness TUs as cheap insurance.

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

# ── 075-live-wire-enum-validation Phase 0.5 golden generator (local-only) ────
# Guarded OFF by default (FIXPP_BUILD_QUICKFIX_GOLDEN, declared inside this
# subdirectory's own CMakeLists.txt). Never enabled in CI; see T002.
add_subdirectory(tools/quickfix_enum_golden)

# ── 079-required-presence-scope T018 golden generator (local-only) ──────────
# Shares the SAME FIXPP_BUILD_QUICKFIX_GOLDEN guard + FIXPP_QUICKFIX_ROOT
# cache var as the 075 generator above (declared inside this subdirectory's
# own CMakeLists.txt; option()/cache-var re-declaration is idempotent). Never
# enabled in CI.
add_subdirectory(tools/quickfix_required_golden)

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
  # 084 — package/export witnesses. Gated with the CPack module below: every one
  # of them invokes cpack, so registering them where packaging is not configured
  # would fail on a missing CPackConfig.cmake rather than on anything real.
  if(FIXPP_PACKAGING_ENABLED)
    add_subdirectory(tests/packaging)
  endif()

  # 061-typed-app-messages (061-slim) T024 — FR-008/SC-004 external-consumer
  # install witness. tests/consumer/ is a STANDALONE CMake project (its own
  # project(), never add_subdirectory()'d — see tests/consumer/CMakeLists.txt)
  # so it never inherits this file's directory-scoped
  # include_directories("${CMAKE_SOURCE_DIR}/include"). Driven via cmake -P
  # (mirrors the T039 codegen_build_graph_test.cmake pattern): stage-install
  # this build, configure+build the consumer against the staged prefix only,
  # run it, assert PASS. Requires the T023 install rule (FIXPP_BUILD_CODEGEN_TOOL).
  if(FIXPP_BUILD_CODEGEN_TOOL)
    add_test(
      NAME "fixpp::consumer::install-witness"
      COMMAND "${CMAKE_COMMAND}"
        "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DFIXPP_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DFIXPP_CONSUMER_SRC_DIR=${CMAKE_SOURCE_DIR}/tests/consumer"
        "-DFIXPP_WITNESS_WORK_DIR=${CMAKE_BINARY_DIR}/_consumer_witness"
        "-DFIXPP_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DFIXPP_C_COMPILER=${CMAKE_C_COMPILER}"
        # Build the consumer sub-build in the SAME config as this build. Conan
        # resolves pugixml (a private link dep of fixpp_dictionary) for THIS
        # build's build_type; a config mismatch leaves pugixml::pugixml with no
        # imported location for the sub-build's config -> undefined pugixml refs
        # from xml_loader.cpp.o at link. Single-config Ninja presets always set
        # CMAKE_BUILD_TYPE; the run script defaults to Debug if it is empty.
        "-DFIXPP_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        -P "${CMAKE_SOURCE_DIR}/tests/consumer/run_consumer_witness.cmake"
    )
    set_tests_properties("fixpp::consumer::install-witness" PROPERTIES
      LABELS consumer
      TIMEOUT 300)
  endif()
endif()

if(FIXPP_BUILD_FUZZ)
  add_subdirectory(tests/fuzz)
  add_subdirectory(tests/config/fuzz)  # T037 — 044-toml-session-config fuzzer
endif()

# ── CMake package export (fixpp:: namespace per [arch §7.4]) ────────────────
# include(GNUInstallDirs) moved above the module block — see the note there (084 T005).
# 084 T047 (FR-012a sign-off, FR-013): the FIRST exclusions this rule has ever
# carried -- it installed the entire include/ tree unconditionally until
# 2026-08-02. Recorded as a DELIBERATE CHANGE IN DELIVERED CONTENT, on two
# separate grounds:
#   * include/fixpp/core/test/  -- mock_clock.hpp's backing target fixpp_mock_clock
#     is FIXPP_BUILD_TESTS-only and absent from any packaging build, so shipping
#     the header meant shipping a header with no library behind it.
#   * include/fixpp/transport/test/ -- header-only test infrastructure that
#     User Story 1 acceptance scenario 7 already treats as outside the
#     consumable surface.
# Do NOT over-exclude: include/fixpp/otel/, the detail/ subtrees,
# session/quickfix_compat/ and the .gitkeep files all SHIP.
# contracts/package-layout.md §2a is authoritative per row.
install(
  DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  PATTERN "fixpp/core/test"      EXCLUDE
  PATTERN "fixpp/transport/test" EXCLUDE
)

# 084 T046 (FR-018b): attribution ships in the package doc directory.
# TWO separate upstream obligations, discharged by TWO files -- they are not
# interchangeable:
#   * QUICKFIX_LICENSE.txt -- clauses 1 and 2 (reproduce the copyright notice,
#     conditions and disclaimer). The packages ship the dictionary XML, which IS
#     the redistributed material.
#   * NOTICE -- clause 3 (the acknowledgment sentence in end-user documentation).
# NOTICE is a TRACKED file at the repo root and reds the git-cleanliness gate in
# tests/codegen/codegen_build_graph_test.cmake if left uncommitted, so it is
# committed in the SAME commit as this rule.
install(
  FILES
    "${CMAKE_SOURCE_DIR}/dictionaries/QUICKFIX_LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/NOTICE"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

# 061-typed-app-messages (061-slim) T023 / FR-008 (research.md Decision 7):
# install the configure-time-generated typed-message flyweight headers onto
# the public installed include path. Previously these attached to the
# fixpp::dict::{v42,v44,v50sp2,vt11} INTERFACE targets via $<BUILD_INTERFACE:>
# only (cmake/Codegen.cmake) — an external consumer outside the build tree
# could not #include them. _dispatch/ (build-tree-private reify bridge) and
# vt11/ (FIXT.1.1, not part of the public v42/v44/v50sp2 set) are excluded.
#
# 078-precompiled-builder-libs: the split emits a `messages/`+`groups/`+
# `validators/` layout plus `all.hpp`/`groups.hpp` aggregators whose
# `build_<Msg>`/`validate_<Msg>` declarations resolve only against the
# unexported `fixpp_builders_<ver>`/`fixpp_validators_<ver>` libs
# (`cmake/Codegen.cmake`). Per `contracts/cmake-targets.md` "Install / export
# scope" Coherence rule (Gate A round 1, R3): while those libs are unexported,
# the slim typed layout must NOT be installed — an installed consumer would
# get unresolvable `build_`/`validate_` link symbols, and the generated
# `.builder.cpp`/`.validator.cpp` would leak as public "headers". This is a
# denylist against the 061 install rule below; extend it if a new emitted
# file/directory type is added under `_codegen/include/<ns>/`.
if(FIXPP_BUILD_CODEGEN_TOOL)
  install(
    DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    PATTERN "_dispatch" EXCLUDE
    PATTERN "vt11" EXCLUDE
    PATTERN "messages"   EXCLUDE   # 078 per-message .hpp/.builder.{inl,cpp}/.validator.{inl,cpp}
    PATTERN "groups"     EXCLUDE   # 078 per-plan group headers
    PATTERN "validators" EXCLUDE   # 078 validators/traits.hpp
    PATTERN "all.hpp"    EXCLUDE   # 078 aggregator
    PATTERN "groups.hpp" EXCLUDE   # 078 groups umbrella
    # ── 8th pattern, added by 084 T058/T059 (FR-013) ────────────────────────
    # Manifest.txt is the 076 T012 codegen CENSUS manifest — a ~12 MB generated
    # "DO NOT EDIT" audit artifact emitted under fixpp/vlatest/. It is read ONLY
    # by tests/codegen, and only from the BUILD tree (FIXPP_CODEGEN_VLATEST_MANIFEST
    # points into ${FIXPP_CODEGEN_OUT}); nothing reads it from an install prefix
    # and no consumer has any use for it. It is build-system output in FR-013's
    # sense, and it was ~12 MB of every package.
    #
    # ⚠️ Note for anyone updating docs: the denylist is now EIGHT patterns, not
    # seven. Every "the 7-pattern denylist" phrase in this feature's bundle
    # predates this exclusion. It was found by the T058 exact-set gate on its
    # first real run against a package — a denylist-only check could not have
    # found it, which is the whole point of T059.
    PATTERN "Manifest.txt" EXCLUDE
  )
endif()

# ── 084: export + packaging surface ──────────────────────────────────────────
#
# SKIPPED ENTIRELY during the codegen bootstrap. cmake/Codegen.cmake re-configures
# THIS SAME root CMakeLists into _codegen_bootstrap/ with
# FIXPP_CODEGEN_BOOTSTRAP_RUNNING=ON to build just the host codegen tool. That
# sub-configure deliberately does not create the dictionary/codegen targets
# (src/dictionary/CMakeLists.txt:73 guards fixpp_dict_dispatch_bridge on exactly
# this flag), so the fail-closed membership check below would fire and abort the
# bootstrap. A build-tool-only configure must not define an install or export
# surface at all.
#
# ⚠️ This only ever manifests on a FRESH tree: the bootstrap runs when
# _codegen_bootstrap/ does not yet exist, so an already-configured tree skips it
# and hides the fault. Found 2026-08-02 by the first configure of a new build
# directory, after many green reconfigures of an existing one.
if(NOT FIXPP_CODEGEN_BOOTSTRAP_RUNNING)

# ── 084 T022/T023: the fixppTargets export set ───────────────────────────────
#
# Membership basis: the ELEVEN measured members (the executed generate run
# recorded in the 084 Gate A round-1 measurement, M3-M5) plus the SIX derived at
# the 2026-08-01 D1/FR-012a sign-off. "Derived" means read off
# target_link_libraries rather than observed failing a generate — the method that
# was wrong three times across a three-level cascade, which is why T024 re-runs
# the experiment against this list rather than trusting it.
#
# ONE list, deliberately: install(TARGETS) and the EXPORT_NAME sweep both consume
# it, so a member added here cannot get an export entry without also getting its
# imported name. A second, separately-maintained list is exactly the half-applied
# sweep that would publish fixpp::fixpp_capi (T022).
set(FIXPP_EXPORT_TARGETS
  # -- the eleven measured (M3-M5) --
  fixpp_core
  fixpp_sync
  fixpp_log
  fixpp_wire
  fixpp_dictionary
  fixpp_tls
  fixpp_transport
  fixpp_session
  fixpp_dict_dispatch_bridge
  fixpp_dict_dispatch
  fixpp_otel
  # -- the six derived at the D1/FR-012a sign-off --
  fixpp_capi
  fixpp_capi_objects
  fixpp_config_toml
  fixpp_tap
  fixpp_service
)

# ── T029: the fixpp::fixpp umbrella (FR-001, FR-002) ─────────────────────────
# find_package(fixpp REQUIRED) + target_link_libraries(app PRIVATE fixpp::fixpp)
# is the whole contract. Linking fixpp_session pulls the measured closure
# transitively (core, sync, wire, dictionary, tls, transport, log, otel, and the
# dict dispatch pair) -- the umbrella does not restate it.
#
# ⚠️ The umbrella is deliberately NOT the whole export set. It does not reach
# fixpp_capi, fixpp_config_toml, fixpp_tap, fixpp_service or fixpp_log_otlp,
# because fixpp_session links none of them. That is correct, not a gap -- but the
# five are NOT one class:
#   * FOUR are exported so a consumer can link them BY NAME: fixpp::capi for a
#     C-ABI consumer who deliberately does not want the C++ umbrella (Article IV
#     §2 / architecture.md:509 -- linking both is REJECTED by tools/check_layers.py),
#     and fixpp::config_toml exactly as include/fixpp/config/toml_config_loader.hpp
#     instructs.
#   * The FIFTH, fixpp_log_otlp, is CLOSURE-ONLY: it is in the export set solely
#     as fixpp_config_toml's $<LINK_ONLY:> requirement. No public header names
#     fixpp::log_otlp and nothing instructs anyone to link it directly.
add_library(fixpp INTERFACE)
add_library(fixpp::fixpp ALIAS fixpp)
target_link_libraries(fixpp INTERFACE fixpp_session)
target_include_directories(fixpp INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
list(APPEND FIXPP_EXPORT_TARGETS fixpp)

# fixpp_log_otlp is the one genuinely configuration-dependent member: it exists
# only under if(TARGET opentelemetry-cpp::api) (src/log/CMakeLists.txt). It is a
# CLOSURE-ONLY member -- fixpp_config_toml names it via a PRIVATE link edge, which
# lands in INTERFACE_LINK_LIBRARIES as $<LINK_ONLY:> and is an export-set
# requirement all the same (FR-008a). No public header names fixpp::log_otlp.
if(TARGET fixpp_log_otlp)
  list(APPEND FIXPP_EXPORT_TARGETS fixpp_log_otlp)
endif()

# Fail CLOSED. Every other name above is expected to exist in EVERY in-scope
# configuration; if one does not, that is a membership defect to be diagnosed,
# never a member to be silently skipped. Filtering the list through
# if(TARGET ...) would turn a missing export member into a package that installs
# cleanly and is missing a library -- the silent-omission shape SC-003 exists for.
foreach(_fixpp_export_tgt IN LISTS FIXPP_EXPORT_TARGETS)
  if(NOT TARGET ${_fixpp_export_tgt})
    message(FATAL_ERROR
      "084 FR-007: export set names '${_fixpp_export_tgt}', which is not a target in "
      "this configuration. Diagnose the membership -- do not delete the entry.")
  endif()
endforeach()

# T022 -- EXPORT_NAME on EVERY member, one convention, applied over the SAME list.
# install(EXPORT ... NAMESPACE fixpp::) derives imported names from TARGET names;
# add_library(fixpp::X ALIAS ...) does NOT carry into an export. Left alone the
# package would publish fixpp::fixpp_capi while architecture.md:503 promises
# fixpp::capi. Convention = target name minus the fixpp_ prefix, which is the
# repository's existing de-facto one across 15 in-tree aliases.
foreach(_fixpp_export_tgt IN LISTS FIXPP_EXPORT_TARGETS)
  string(REGEX REPLACE "^fixpp_" "" _fixpp_export_short "${_fixpp_export_tgt}")
  set_target_properties(${_fixpp_export_tgt} PROPERTIES EXPORT_NAME "${_fixpp_export_short}")
endforeach()
unset(_fixpp_export_tgt)
unset(_fixpp_export_short)

# One target diverges from the prefix-strip rule, and it is the in-tree alias that
# decides: fixpp_dict_dispatch is aliased fixpp::dict::dispatch (a NESTED namespace,
# cmake/Codegen.cmake), not fixpp::dict_dispatch. Measured 2026-08-02: CMake accepts
# a "::" inside EXPORT_NAME and emits fixpp::dict::dispatch verbatim, so the export
# matches the alias and T026's set-equality needs no exemption.
set_target_properties(fixpp_dict_dispatch PROPERTIES EXPORT_NAME "dict::dispatch")

# T022/T026: two export members had no in-tree fixpp:: alias at all
# (fixpp_dict_dispatch_bridge, fixpp_capi_objects) while every other member had one.
# T026 asserts the alias census and the installed imported-name set are equal BOTH
# directions; without these, two installed names would have no alias to match and the
# gate would need a hand-maintained exemption list — the half-applied sweep T022
# exists to prevent. Adding the aliases keeps it a plain set equality with zero
# exemptions. Both are real members: the bridge by the measured $<LINK_ONLY:> closure,
# capi_objects by fixpp_capi's PUBLIC edge.
if(NOT TARGET fixpp::dict_dispatch_bridge)
  add_library(fixpp::dict_dispatch_bridge ALIAS fixpp_dict_dispatch_bridge)
endif()
if(NOT TARGET fixpp::capi_objects)
  add_library(fixpp::capi_objects ALIAS fixpp_capi_objects)
endif()

# T021 -- fixpp_dict_dispatch's install interface must stay EMPTY. Its sole
# include directory (cmake/Codegen.cmake) is $<BUILD_INTERFACE:> over
# _codegen/include, whose _dispatch/ content is install-EXCLUDED by the denylist
# above. It is in the export set ONLY to satisfy CMake's closure rule for the
# bridge's $<LINK_ONLY:> edge. Asserting emptiness here makes invariant I2 hold by
# construction rather than by luck, and trips FR-010's standing obligation if a
# future change gives it a non-empty one.
get_target_property(_fixpp_dd_inc fixpp_dict_dispatch INTERFACE_INCLUDE_DIRECTORIES)
if(_fixpp_dd_inc MATCHES "INSTALL_INTERFACE")
  message(FATAL_ERROR
    "084 T021/I2: fixpp_dict_dispatch acquired a non-empty install interface "
    "(${_fixpp_dd_inc}). Its _dispatch/ headers are install-excluded, so exporting "
    "an install include path would publish a directory the package does not ship.")
endif()
unset(_fixpp_dd_inc)

install(
  TARGETS ${FIXPP_EXPORT_TARGETS}
  EXPORT fixppTargets
  ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  OBJECTS  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
# NO `INCLUDES DESTINATION` -- deliberate, measured 2026-08-02 (T021/T027).
# It appends its path to the install interface of EVERY member, which did two
# unwanted things here:
#   1. It duplicated the entry ("<prefix>/include;<prefix>/include") on the 14
#      targets that already declare $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
#      themselves after the T006-T018 rewrite.
#   2. It gave fixpp_dict_dispatch a NON-EMPTY install interface, silently breaking
#      invariant I2 / rule B2. That member is link-closure-only and must export no
#      install include path at all.
# With the per-target rewrite in place this option is redundant; dropping it makes
# I2 hold BY CONSTRUCTION, which is what T021 asks for.

# Writing the export set is what makes CMake ENFORCE the link closure ("... includes
# target X which requires target Y that is not in any export set"). install(TARGETS
# ... EXPORT) alone only populates the set: with no install(EXPORT) the set is never
# generated, the closure is never checked, and generate succeeds over an incomplete
# export set. Keep these two together.
install(
  EXPORT fixppTargets
  FILE fixppTargets.cmake
  NAMESPACE fixpp::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fixpp"
)

# ── 084 T030: the find_dependency set, DERIVED from the configured target graph ─
#
# The RULE is the deliverable, not the list. For every export-set member, every
# imported target in its link interface contributes its find_package package name
# -- $<LINK_ONLY:> entries INCLUDED, because a static library does not link its
# own PRIVATE dependencies and the consumer's final link must resolve them.
#
# A hand-written enumeration was tried at Gate A and was wrong in BOTH directions:
# it named compression and networking packages no target links, and it omitted
# Crc32c, which every consumer needs. A derivation cannot drift that way.
#
# Applied to the current graph this yields SIX: OpenSSL, asio, pugixml, Crc32c,
# opentelemetry-cpp, tomlplusplus. ZLIB is deliberately absent -- no fixpp target
# links it (research R3 / invariant I7); a spurious requirement is as much a
# packaging defect as a missing one. If a ZLIB link error ever appears in the
# real-client witness, check whether OpenSSL's imported target regressed BEFORE
# adding find_dependency(ZLIB) -- adding it would mask the real fault.
set(_fixpp_dep_pkgs "")
foreach(_fixpp_export_tgt IN LISTS FIXPP_EXPORT_TARGETS)
  get_target_property(_fixpp_link_iface ${_fixpp_export_tgt} INTERFACE_LINK_LIBRARIES)
  if(NOT _fixpp_link_iface)
    continue()
  endif()
  foreach(_fixpp_link_entry IN LISTS _fixpp_link_iface)
    # Unwrap $<LINK_ONLY:X> -> X. This is where a PRIVATE dependency of a STATIC
    # library lands, and it is a genuine consumer-configure obligation.
    string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" _fixpp_link_entry "${_fixpp_link_entry}")
    # An imported third-party target is Namespace::target; the namespace IS the
    # find_package package name for every dependency in this graph.
    if(_fixpp_link_entry MATCHES "^([A-Za-z0-9_+.-]+)::")
      if(NOT CMAKE_MATCH_1 STREQUAL "fixpp")
        list(APPEND _fixpp_dep_pkgs "${CMAKE_MATCH_1}")
      endif()
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES _fixpp_dep_pkgs)
list(SORT _fixpp_dep_pkgs)

set(FIXPP_FIND_DEPENDENCY_BLOCK "")
foreach(_fixpp_dep IN LISTS _fixpp_dep_pkgs)
  string(APPEND FIXPP_FIND_DEPENDENCY_BLOCK "find_dependency(${_fixpp_dep})\n")
endforeach()
message(STATUS "fixpp package: find_dependency set (derived) = ${_fixpp_dep_pkgs}")

# ── T031: version file, derived from project(VERSION) ────────────────────────
# NEVER a packaging-local literal: a later GA bump (REMAINING-WORK item 13) must
# propagate with no packaging change. SameMajorVersion makes an incompatible
# request fail inside find_package() at CONFIGURE time (FR-006 / SC-006).
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/fixppConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

# ── T032: the package config itself ──────────────────────────────────────────
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fixppConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/fixppConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fixpp"
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fixppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fixppConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/fixpp"
)

# ── T033: the FIX dictionaries (FR-018a) ─────────────────────────────────────
# dictionaries/ had NO install rule of any kind before this feature, so the
# package shipped fixpp::dict::load_any(path, ...) with none of its data -- a
# public runtime-loading API with nothing to load. The XML is the redistributed
# upstream material, which is also why its licence ships alongside it (FR-018b).
install(
  DIRECTORY "${CMAKE_SOURCE_DIR}/dictionaries/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/fixpp/dictionaries"
)

# ── 084 T048-T057: CPack. Must come AFTER every install() rule above ─────────
# Guarded on the INCLUDE rather than by a return() inside the module: `include()`
# does not open a new scope, so a return() in the included file would return from
# THIS directory scope and silently skip everything below.
if(FIXPP_PACKAGING_ENABLED)
  include(cmake/FixppPackaging.cmake)
endif()

endif()  # NOT FIXPP_CODEGEN_BOOTSTRAP_RUNNING — see the banner above the export set

# 077/078: the heavy_builder_compile Ninja job pool (#197 CI stopgap that capped
# concurrent compilation of the deduped-header TUs to 2) is removed (078 T034) —
# the 078 relink dropped per-TU RSS enough that the default -j no longer OOMs the
# hosted runners.
