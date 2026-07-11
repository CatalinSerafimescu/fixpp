# cmake/Codegen.cmake
# [2c §7.6] Configure-time codegen target graph.
#
# CONTRACT (AC-C4 / [arch §4.2] step 3 / AC-T2):
#   • All generated headers are emitted into the BUILD TREE only:
#       ${CMAKE_BINARY_DIR}/_codegen/include/fixpp/{v42,v44,v50sp2,vt11}/
#       ${CMAKE_BINARY_DIR}/_codegen/include/fixpp/_dispatch/
#   • Generation runs at CMake CONFIGURE time (not build time), so a consumer
#     can #include generated headers immediately after cmake --preset.
#   • The source tree is NEVER written; git status --porcelain stays clean.
#
# BOOTSTRAP MECHANISM ([2c §7.6] — configure-time bootstrap):
#   fixpp-codegen is a C++23 host executable compiled from this project.
#   At configure time the main build hasn't compiled it yet, so we need to
#   either find an existing binary or build it.  Resolution order:
#     1. Main build tree bin/ — the most recent successful build already has
#        the binary; use it directly (fastest path, always consistent).
#     2. Bootstrap sub-build in ${CMAKE_BINARY_DIR}/_codegen_bootstrap — a
#        nested configure+build limited to fixpp-codegen only, using the same
#        CMAKE_TOOLCHAIN_FILE and CMAKE_BUILD_TYPE so Conan-provided dependencies
#        (pugixml, etc.) are found identically.
#   An internal cache guard (FIXPP_CODEGEN_BOOTSTRAP_RUNNING) prevents the
#   nested configure from triggering another bootstrap invocation.
#
# RECONCILIATION with tests/codegen existing wiring:
#   The legacy add_custom_target(fixpp_codegen_generate ...) in
#   tests/codegen/CMakeLists.txt ran codegen at BUILD time.  T016 replaces
#   that with a single configure-time mechanism defined here.
#   fixpp_codegen_generate is now defined in THIS file as a no-op custom
#   target (headers are already on disk at configure time).  tests/codegen,
#   tests/dictionary, and tests/integration still do add_dependencies(...
#   fixpp_codegen_generate) and see the same target name — single source of
#   truth, no competing mechanism.
#
# INTERFACE TARGETS:
#   fixpp::dict::v42       — per-version INTERFACE; INTERFACE_INCLUDE_DIRECTORIES
#   fixpp::dict::v44         → ${CMAKE_BINARY_DIR}/_codegen/include (build-tree)
#   fixpp::dict::v50sp2
#   fixpp::dict::vt11
#   fixpp::dict::dispatch  — shared _dispatch/ headers (Entity 8 / I-11)
#   fixpp::dict::runtime   — alias for the 002 fixpp_dictionary runtime archive
#
# CONFIGURE-TIME MARKER TARGETS (thin, one per version):
#   fixpp::dict::generate-v42, generate-v44, generate-v50sp2, generate-vt11
#   These are add_custom_target(...) markers; actual generation already ran
#   during configure via execute_process.

# ── Guard: skip this module when we are the bootstrap sub-configure ──────────
if(FIXPP_CODEGEN_BOOTSTRAP_RUNNING)
  return()
endif()

# ── Prerequisite: the tool target must have been defined already ─────────────
if(NOT FIXPP_BUILD_CODEGEN_TOOL)
  message(STATUS "[Codegen] FIXPP_BUILD_CODEGEN_TOOL=OFF — skipping configure-time codegen graph")
  return()
endif()

# ── 069-v44-all-families T015 (US3, C1): v44 typed-builder coverage control ──
# CACHE STRING (not option() — the domain is {all, official}, not boolean).
# `all` (default, 83 in-scope FIX44 msgcat='app' builders) vs `official`
# (opt-down to the pre-069 33-builder cost, byte-identical output). Fails
# closed at configure time on any out-of-domain value, before codegen runs.
#
# FR-008 durability note: CI's Tier-1 matrix relies on every *verifying*
# preset staying on the default `all` (no preset in CMakePresets.json
# currently sets this variable — see tier1.yml's post-Configure assertion
# step, which fails loudly if a preset silently overrides to `official`).
set(FIXPP_CODEGEN_V44_FAMILIES "all" CACHE STRING
  "v44 typed-builder coverage: all (default, 83) | official (33)")
set_property(CACHE FIXPP_CODEGEN_V44_FAMILIES PROPERTY STRINGS all official)

if(NOT FIXPP_CODEGEN_V44_FAMILIES STREQUAL "all"
   AND NOT FIXPP_CODEGEN_V44_FAMILIES STREQUAL "official")
  message(FATAL_ERROR
    "[Codegen] FIXPP_CODEGEN_V44_FAMILIES must be 'all' or 'official' "
    "(got '${FIXPP_CODEGEN_V44_FAMILIES}').")
endif()

# ── Paths ─────────────────────────────────────────────────────────────────────
set(FIXPP_CODEGEN_BOOTSTRAP_DIR  "${CMAKE_BINARY_DIR}/_codegen_bootstrap")
set(FIXPP_CODEGEN_OUT_DIR        "${CMAKE_BINARY_DIR}/_codegen/include/fixpp")
set(_xml_dir                     "${CMAKE_SOURCE_DIR}/dictionaries")

# Resolution order for the codegen binary (see BOOTSTRAP MECHANISM above):
#   1. Main build tree ${CMAKE_BINARY_DIR}/bin/fixpp-codegen
#   2. Bootstrap sub-build ${CMAKE_BINARY_DIR}/_codegen_bootstrap/bin/fixpp-codegen
# CMAKE_EXECUTABLE_SUFFIX is "" on POSIX and ".exe" on Windows/MSVC — without it
# the EXISTS check below fails on Windows ("Bootstrap succeeded but … not found")
# even though fixpp-codegen.exe built fine. Both presets are single-config Ninja
# (CMAKE_GENERATOR is inherited by the bootstrap), so there is no per-config
# subdir to account for — only the suffix.
set(_codegen_tool_main       "${CMAKE_BINARY_DIR}/bin/fixpp-codegen${CMAKE_EXECUTABLE_SUFFIX}")
set(_codegen_tool_bootstrap  "${FIXPP_CODEGEN_BOOTSTRAP_DIR}/bin/fixpp-codegen${CMAKE_EXECUTABLE_SUFFIX}")

# ── Locate or build fixpp-codegen ─────────────────────────────────────────────
if(EXISTS "${_codegen_tool_main}")
  # Fast path: main build already has a compiled binary (incremental reconfigures).
  set(_codegen_tool "${_codegen_tool_main}")
  message(STATUS "[Codegen] Using fixpp-codegen from main build: ${_codegen_tool}")
elseif(EXISTS "${_codegen_tool_bootstrap}")
  # Bootstrap binary exists from a previous configure.
  set(_codegen_tool "${_codegen_tool_bootstrap}")
  message(STATUS "[Codegen] Using fixpp-codegen from bootstrap: ${_codegen_tool}")
else()
  # Neither exists — run the bootstrap sub-build.
  message(STATUS "[Codegen] Bootstrap: configuring fixpp-codegen in ${FIXPP_CODEGEN_BOOTSTRAP_DIR}")

  # Inherit the same build type and toolchain so Conan packages are found
  # (the toolchain embeds debug/release-specific Conan package paths).
  set(_bt "${CMAKE_BUILD_TYPE}")
  if(NOT _bt)
    set(_bt "Debug")  # safe default when build type is not single-config
  endif()

  set(_bootstrap_args
    -S "${CMAKE_SOURCE_DIR}"
    -B "${FIXPP_CODEGEN_BOOTSTRAP_DIR}"
    -G "${CMAKE_GENERATOR}"
    -DCMAKE_BUILD_TYPE=${_bt}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DFIXPP_BUILD_TESTS=OFF
    -DFIXPP_BUILD_BENCH=OFF
    -DFIXPP_BUILD_FUZZ=OFF
    -DFIXPP_BUILD_PYTHON=OFF
    -DFIXPP_BUILD_CODEGEN_TOOL=ON
    -DFIXPP_WERROR=OFF
    # Bootstrap guard — prevents the nested configure from re-entering Codegen.cmake
    -DFIXPP_CODEGEN_BOOTSTRAP_RUNNING=ON
    # 046 T014: forward the OTel toggle so the bootstrap sub-configure
    # agrees with the parent's Conan graph (no opentelemetry-cpp when
    # with_otel=False). Without this the consistency check in CMakeLists.txt
    # fires inside the bootstrap sub-build with the wrong default (ON).
    -DFIXPP_BUILD_OTEL=${FIXPP_BUILD_OTEL}
  )

  # Forward the Conan toolchain — required so pugixml / GTest CMakeDeps are found
  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND _bootstrap_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_bootstrap_args}
    RESULT_VARIABLE _cfg_result
    OUTPUT_VARIABLE _cfg_output
    ERROR_VARIABLE  _cfg_error
  )
  if(NOT _cfg_result EQUAL 0)
    message(FATAL_ERROR
      "[Codegen] Bootstrap configure failed (exit ${_cfg_result}):\n"
      "${_cfg_output}\n${_cfg_error}")
  endif()

  message(STATUS "[Codegen] Bootstrap: building fixpp-codegen target")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${FIXPP_CODEGEN_BOOTSTRAP_DIR}"
                               --target fixpp-codegen
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_output
    ERROR_VARIABLE  _build_error
  )
  if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR
      "[Codegen] Bootstrap build failed (exit ${_build_result}):\n"
      "${_build_output}\n${_build_error}")
  endif()

  if(NOT EXISTS "${_codegen_tool_bootstrap}")
    message(FATAL_ERROR
      "[Codegen] Bootstrap succeeded but ${_codegen_tool_bootstrap} not found.")
  endif()
  set(_codegen_tool "${_codegen_tool_bootstrap}")
  message(STATUS "[Codegen] Bootstrap complete: ${_codegen_tool}")
endif()

# ── Configure-time codegen: run the tool for each version ────────────────────
# Generate all versions in one invocation.  Re-run when:
#   - any XML input is newer than the v44 marker (proxy for "all outputs")
#   - any output marker is missing

set(_v42_marker    "${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v42/Messages.hpp")
set(_v44_marker    "${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v44/Messages.hpp")
set(_v50sp2_marker "${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v50sp2/Messages.hpp")
set(_vt11_marker   "${CMAKE_BINARY_DIR}/_codegen/include/fixpp/vt11/Messages.hpp")
# 067-codegen-writer-emitter: Builders.hpp is emitted for v44 ONLY (other
# versions get no Builders.hpp — see emit_builders.cpp scope note).
set(_v44_builders_marker "${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v44/Builders.hpp")

set(_need_generate FALSE)

# 069 T015 (US3) regen-guard invalidation: the marker/xml-mtime checks below
# know nothing about the coverage mode, so switching FIXPP_CODEGEN_V44_FAMILIES
# between configures (all <-> official) would otherwise leave a stale
# Builders.hpp on disk. Track the last-used value in an INTERNAL cache
# variable and force a re-run whenever it differs from the current value.
if(DEFINED FIXPP_CODEGEN_V44_FAMILIES_LAST_USED
   AND NOT "${FIXPP_CODEGEN_V44_FAMILIES_LAST_USED}" STREQUAL "${FIXPP_CODEGEN_V44_FAMILIES}")
  set(_need_generate TRUE)
  message(STATUS
    "[Codegen] FIXPP_CODEGEN_V44_FAMILIES changed "
    "(${FIXPP_CODEGEN_V44_FAMILIES_LAST_USED} -> ${FIXPP_CODEGEN_V44_FAMILIES}); "
    "forcing codegen re-run.")
endif()
set(FIXPP_CODEGEN_V44_FAMILIES_LAST_USED "${FIXPP_CODEGEN_V44_FAMILIES}" CACHE INTERNAL
  "Regen-guard: last FIXPP_CODEGEN_V44_FAMILIES value used for codegen generation.")

# Missing output?
foreach(_marker IN ITEMS "${_v42_marker}" "${_v44_marker}" "${_v50sp2_marker}" "${_vt11_marker}" "${_v44_builders_marker}")
  if(NOT EXISTS "${_marker}")
    set(_need_generate TRUE)
    break()
  endif()
endforeach()

# Any XML input newer than the most recent output?
if(NOT _need_generate)
  foreach(_xml IN ITEMS
      "${_xml_dir}/FIX42.xml"
      "${_xml_dir}/FIX44.xml"
      "${_xml_dir}/FIX50SP2.xml"
      "${_xml_dir}/FIXT11.xml")
    if("${_xml}" IS_NEWER_THAN "${_v44_marker}")
      set(_need_generate TRUE)
      break()
    endif()
  endforeach()
endif()

# 046-atomic-shared-ptr (NFR-017): under -stdlib=libc++ the bootstrap/main
# fixpp-codegen is a libc++-linked HOST tool. At configure-time execution the
# dynamic loader may pick a SYSTEM libc++.so.1 that lacks newer symbols (e.g.
# `vtable for std::pmr::memory_resource`), giving a symbol-lookup error. Derive
# the compiler's OWN libc++ runtime dir and put it on LD_LIBRARY_PATH for the
# codegen run so the matching runtime is found. No-op on a libstdc++ build (the
# launcher stays empty and the tool runs directly).
set(_codegen_run_launcher "")
if(CMAKE_CXX_FLAGS MATCHES "libc\\+\\+" OR CMAKE_EXE_LINKER_FLAGS MATCHES "libc\\+\\+")
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -stdlib=libc++ -print-file-name=libc++.so.1
    OUTPUT_VARIABLE _libcxx_so OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_libcxx_so)
    get_filename_component(_libcxx_dir "${_libcxx_so}" DIRECTORY)
    if(_libcxx_dir AND IS_DIRECTORY "${_libcxx_dir}")
      set(_codegen_run_launcher
        "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${_libcxx_dir}:$ENV{LD_LIBRARY_PATH}")
      message(STATUS "[Codegen] libc++ run path: ${_libcxx_dir}")
    endif()
  endif()
endif()

if(_need_generate)
  message(STATUS "[Codegen] Running fixpp-codegen at configure time → ${FIXPP_CODEGEN_OUT_DIR}")
  file(MAKE_DIRECTORY "${FIXPP_CODEGEN_OUT_DIR}")
  execute_process(
    COMMAND ${_codegen_run_launcher} "${_codegen_tool}"
            --xml "${_xml_dir}/FIX42.xml"    --out "${FIXPP_CODEGEN_OUT_DIR}"
            --xml "${_xml_dir}/FIX44.xml"    --out "${FIXPP_CODEGEN_OUT_DIR}"
            --xml "${_xml_dir}/FIX50SP2.xml" --out "${FIXPP_CODEGEN_OUT_DIR}"
            --xml "${_xml_dir}/FIXT11.xml"   --out "${FIXPP_CODEGEN_OUT_DIR}"
            --families ${FIXPP_CODEGEN_V44_FAMILIES}
    RESULT_VARIABLE _gen_result
    OUTPUT_VARIABLE _gen_output
    ERROR_VARIABLE  _gen_error
  )
  if(NOT _gen_result EQUAL 0)
    message(FATAL_ERROR
      "[Codegen] fixpp-codegen failed (exit ${_gen_result}):\n"
      "${_gen_output}\n${_gen_error}")
  endif()
  message(STATUS "[Codegen] Configure-time codegen complete.")
else()
  message(STATUS "[Codegen] Generated headers up-to-date; skipping re-run.")
endif()

# Verify all expected outputs exist
foreach(_ver IN ITEMS v42 v44 v50sp2 vt11)
  if(NOT EXISTS "${CMAKE_BINARY_DIR}/_codegen/include/fixpp/${_ver}/Messages.hpp")
    message(FATAL_ERROR
      "[Codegen] Expected output missing after configure-time generation: "
      "${CMAKE_BINARY_DIR}/_codegen/include/fixpp/${_ver}/Messages.hpp")
  endif()
endforeach()

# ── fixpp_codegen_generate — no-op build-time marker target ──────────────────
# Headers are already on disk after configure.  This target exists solely so
# that tests/codegen, tests/dictionary, and tests/integration can keep their
# add_dependencies(... fixpp_codegen_generate) lines without modification.
# Single source of truth: THIS file owns the definition (tests/codegen no longer
# defines it).
add_custom_target(fixpp_codegen_generate
  COMMENT "fixpp_codegen_generate: headers generated at configure time (no-op at build)"
)

# ── Per-version INTERFACE library targets ─────────────────────────────────────
# Each target carries INTERFACE_INCLUDE_DIRECTORIES into the build tree only
# (build-tree-only per AC-C4 / [arch §4.2] step 3).
# No install() rules — this is build-scaffolding within 003-dictionary-codegen.

foreach(_ver IN ITEMS v42 v44 v50sp2 vt11)
  set(_tgt "fixpp_dict_${_ver}")
  add_library(${_tgt} INTERFACE)
  add_library("fixpp::dict::${_ver}" ALIAS ${_tgt})
  target_include_directories(${_tgt} INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_codegen/include>"
  )
  add_dependencies(${_tgt} fixpp_codegen_generate)
endforeach()

# fixpp::dict::dispatch — shared _dispatch/ headers (Entity 8 / I-11).
# Lives under the same _codegen/include tree; same include dir covers it.
add_library(fixpp_dict_dispatch INTERFACE)
add_library(fixpp::dict::dispatch ALIAS fixpp_dict_dispatch)
target_include_directories(fixpp_dict_dispatch INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_codegen/include>"
)
add_dependencies(fixpp_dict_dispatch fixpp_codegen_generate)

# fixpp::dict::runtime — thin alias for the 002 runtime dictionary archive.
# Consumers linking fixpp::dict::runtime get the full dictionary + XML loader.
add_library(fixpp_dict_runtime INTERFACE)
add_library(fixpp::dict::runtime ALIAS fixpp_dict_runtime)
target_link_libraries(fixpp_dict_runtime INTERFACE fixpp_dictionary)

# ── fixpp::dict::generate-vXX — configure-time marker targets ────────────────
# Named targets per [2c §7.6]; listed by `cmake --build <dir> --target help`.
# The actual generation ran above via execute_process; these are thin
# dependency markers for future consumers and the T039 CMake-graph validator.
#
# Note: CMake prohibits '::' in add_custom_target names (reserved for ALIAS).
# The canonical Ninja/Make target names use underscores:
#   fixpp_dict_generate_v42, …
# The conceptual [2c §7.6] identifiers fixpp::dict::generate-vXX are used in
# documentation and the T039 cmake -P script test as logical names; the
# concrete CMake target is fixpp_dict_generate_<ver>.

foreach(_ver IN ITEMS v42 v44 v50sp2 vt11)
  add_custom_target("fixpp_dict_generate_${_ver}"
    COMMENT "fixpp::dict::generate-${_ver}: configure-time (headers already on disk)"
    DEPENDS fixpp_codegen_generate
  )
endforeach()

# ── T039: Write graph_props.cmake for the cmake -P build-graph test ──────────
# Captures the configure-time graph state (header paths, INTERFACE target include
# dirs, source-dir and build-dir) into a machine-readable CMake script under the
# build tree only (never the source tree — AC-C4 / DoD §12).
#
# The cmake -P test (tests/codegen/codegen_build_graph_test.cmake) includes this
# file and asserts the recorded values satisfy AC-C4 sub-clauses (a)–(d):
#   (a) headers exist post-configure
#   (b) generation was configure-time (the props file itself is written at
#       configure time; no build-time custom command produced it)
#   (c) INTERFACE_INCLUDE_DIRECTORIES point into the build tree
#   (d) git status --porcelain is clean (asserted by the -P script live)
#
# IMPORTANT: file(GENERATE ...) runs at CMake generate phase, not build phase,
# and $<TARGET_PROPERTY:...> generator expressions are resolved then.  We use
# file(WRITE ...) here (configure phase) and record the include-directory path
# directly (the path is already a CMake variable, not a genex), because:
#   • The INTERFACE_INCLUDE_DIRECTORIES value is set via target_include_directories
#     with $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_codegen/include>, which at
#     generate time resolves to ${CMAKE_BINARY_DIR}/_codegen/include.  We record
#     this known-at-configure-time value directly.
#   • This avoids a file(GENERATE) round-trip that would complicate the -P script's
#     include logic and create a generator-phase dependency.

set(_graph_props_file "${CMAKE_BINARY_DIR}/_codegen/graph_props.cmake")

# Collect the INTERFACE include dir that every per-version target will have
# (they all share the same build-tree root — AC-C2 design note: sharing the
# root means v44's INTERFACE lets you #include v50sp2 headers if you write the
# path explicitly; AC-C2 is satisfied at the build-system level because the
# targets are independent and a consumer only links what it declares).
set(_iface_inc_dir "${CMAKE_BINARY_DIR}/_codegen/include")

set(_props_content
"# GENERATED by cmake/Codegen.cmake at CMake configure time — DO NOT EDIT.
# Consumed by tests/codegen/codegen_build_graph_test.cmake (T039 — cmake -P test).
# Records the configure-time state of the [2c §7.6] codegen graph.
# AC-C4: all values reference the build tree; source tree is never written.

set(FIXPP_CODEGEN_GRAPH_PROPS_VERSION \"1\")

# (a) Marker files that must exist after configure (one per version).
set(FIXPP_CODEGEN_MARKER_v42    \"${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v42/Messages.hpp\")
set(FIXPP_CODEGEN_MARKER_v44    \"${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v44/Messages.hpp\")
set(FIXPP_CODEGEN_MARKER_v50sp2 \"${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v50sp2/Messages.hpp\")
set(FIXPP_CODEGEN_MARKER_vt11   \"${CMAKE_BINARY_DIR}/_codegen/include/fixpp/vt11/Messages.hpp\")
set(FIXPP_CODEGEN_DISPATCH_MARKER \"${CMAKE_BINARY_DIR}/_codegen/include/fixpp/_dispatch/reify_dispatch_fixt.hpp\")
set(FIXPP_CODEGEN_BUILDERS_MARKER_v44 \"${CMAKE_BINARY_DIR}/_codegen/include/fixpp/v44/Builders.hpp\")

# (b) Configure-time flag: generation ran at CMake configure time (not build time).
# This file itself is evidence — it is written by cmake/Codegen.cmake during the
# configure phase (execute_process / file(WRITE)); no Makefile/Ninja rule produces it.
set(FIXPP_CODEGEN_CONFIGURE_TIME TRUE)

# (c) INTERFACE_INCLUDE_DIRECTORIES for each per-version target.
# All per-version targets share the same build-tree root directory.
set(FIXPP_DICT_V42_IFACE_INC    \"${_iface_inc_dir}\")
set(FIXPP_DICT_V44_IFACE_INC    \"${_iface_inc_dir}\")
set(FIXPP_DICT_V50SP2_IFACE_INC \"${_iface_inc_dir}\")
set(FIXPP_DICT_VT11_IFACE_INC   \"${_iface_inc_dir}\")
set(FIXPP_DICT_DISPATCH_IFACE_INC \"${_iface_inc_dir}\")

# Build tree root for path-prefix assertions.
set(FIXPP_CODEGEN_BINARY_DIR \"${CMAKE_BINARY_DIR}\")
set(FIXPP_CODEGEN_SOURCE_DIR \"${CMAKE_SOURCE_DIR}\")
")

file(WRITE "${_graph_props_file}" "${_props_content}")
message(STATUS "[Codegen] Wrote configure-time graph props: ${_graph_props_file}")
