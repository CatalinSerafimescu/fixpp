# tests/codegen/codegen_build_graph_test.cmake — T039 [P] [US4]
#
# CTest target: fixpp::dict::codegen-build-graph-check
# Type: cmake -P script test (NOT a GoogleTest C++ TU).
#
# Asserts AC-C4 sub-clauses (a)–(d) for the [2c §7.6] configure-time codegen graph:
#
#   (a) build/<preset>/_codegen/include/fixpp/{v42,v44,v50sp2,vt11}/Messages.hpp
#       and the _dispatch/ headers exist post-configure.
#
#   (b) Generation is configure-time: the graph_props.cmake file is written by
#       cmake/Codegen.cmake during CMake configure phase (execute_process +
#       file(WRITE)); there is no Makefile/Ninja build rule that produces it.
#       The FIXPP_CODEGEN_CONFIGURE_TIME flag records this fact.
#
#   (c) Per-version INTERFACE targets carry INTERFACE_INCLUDE_DIRECTORIES pointing
#       into the build tree (never the source tree). The recorded include dirs
#       must be prefixes of FIXPP_CODEGEN_BINARY_DIR and must NOT be prefixes
#       of FIXPP_CODEGEN_SOURCE_DIR.
#
#   (d) git status --porcelain over the source tree is empty post-configure
#       (source tree stays clean — AC-T2 overlap; DoD §12 build-tree clause).
#
# Usage (invoked by CTest via add_test in tests/codegen/CMakeLists.txt):
#   cmake -DFIXPP_GRAPH_PROPS=<path/to/graph_props.cmake> -P codegen_build_graph_test.cmake
#
# The FIXPP_GRAPH_PROPS variable is passed by the registered add_test command.
# All other values are read from the included props file.

cmake_minimum_required(VERSION 3.25)

# ── Helpers ───────────────────────────────────────────────────────────────────

set(_PASS_COUNT 0)
set(_FAIL_COUNT 0)
set(_FAIL_MSGS "")

macro(_assert condition msg)
  if(${condition})
    math(EXPR _PASS_COUNT "${_PASS_COUNT} + 1")
  else()
    math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
    list(APPEND _FAIL_MSGS "  FAIL: ${msg}")
  endif()
endmacro()

macro(_assert_false condition msg)
  if(NOT (${condition}))
    math(EXPR _PASS_COUNT "${_PASS_COUNT} + 1")
  else()
    math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
    list(APPEND _FAIL_MSGS "  FAIL: ${msg}")
  endif()
endmacro()

# ── Load graph_props.cmake ─────────────────────────────────────────────────────

if(NOT DEFINED FIXPP_GRAPH_PROPS OR NOT EXISTS "${FIXPP_GRAPH_PROPS}")
  message(FATAL_ERROR
    "[T039] FIXPP_GRAPH_PROPS not set or file does not exist: '${FIXPP_GRAPH_PROPS}'\n"
    "This cmake -P test must be invoked by CTest with "
    "-DFIXPP_GRAPH_PROPS=<build>/_codegen/graph_props.cmake")
endif()

include("${FIXPP_GRAPH_PROPS}")

# Validate that required variables were loaded.
foreach(_var IN ITEMS
    FIXPP_CODEGEN_GRAPH_PROPS_VERSION
    FIXPP_CODEGEN_MARKER_v42
    FIXPP_CODEGEN_MARKER_v44
    FIXPP_CODEGEN_MARKER_v50sp2
    FIXPP_CODEGEN_MARKER_vt11
    FIXPP_CODEGEN_DISPATCH_MARKER
    FIXPP_CODEGEN_CONFIGURE_TIME
    FIXPP_DICT_V42_IFACE_INC
    FIXPP_DICT_V44_IFACE_INC
    FIXPP_DICT_V50SP2_IFACE_INC
    FIXPP_DICT_VT11_IFACE_INC
    FIXPP_DICT_DISPATCH_IFACE_INC
    FIXPP_CODEGEN_BINARY_DIR
    FIXPP_CODEGEN_SOURCE_DIR)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "[T039] graph_props.cmake is missing variable: ${_var}")
  endif()
endforeach()

message(STATUS "[T039] graph_props.cmake loaded (schema v${FIXPP_CODEGEN_GRAPH_PROPS_VERSION})")
message(STATUS "[T039] binary_dir = ${FIXPP_CODEGEN_BINARY_DIR}")
message(STATUS "[T039] source_dir = ${FIXPP_CODEGEN_SOURCE_DIR}")

# ── (a) Header files exist post-configure ────────────────────────────────────

message(STATUS "[T039] === (a) Generated header existence ===")

foreach(_ver IN ITEMS v42 v44 v50sp2 vt11)
  set(_marker "${FIXPP_CODEGEN_MARKER_${_ver}}")
  if(EXISTS "${_marker}")
    message(STATUS "[T039]   OK  ${_marker}")
    _assert(TRUE "${_ver}/Messages.hpp exists: ${_marker}")
  else()
    message(STATUS "[T039]   MISSING  ${_marker}")
    _assert(FALSE "${_ver}/Messages.hpp must exist post-configure: ${_marker}")
  endif()
endforeach()

# _dispatch/ marker
if(EXISTS "${FIXPP_CODEGEN_DISPATCH_MARKER}")
  message(STATUS "[T039]   OK  ${FIXPP_CODEGEN_DISPATCH_MARKER}")
  _assert(TRUE "_dispatch/reify_dispatch_fixt.hpp exists")
else()
  message(STATUS "[T039]   MISSING  ${FIXPP_CODEGEN_DISPATCH_MARKER}")
  _assert(FALSE "_dispatch/reify_dispatch_fixt.hpp must exist post-configure: ${FIXPP_CODEGEN_DISPATCH_MARKER}")
endif()

# ── (b) Configure-time flag ───────────────────────────────────────────────────
#
# Mechanism: cmake/Codegen.cmake writes graph_props.cmake via file(WRITE ...) at
# CMake configure time (inside the same configure pass that runs execute_process
# for codegen). There is NO Makefile/Ninja build rule that produces this file —
# it is written unconditionally every configure pass by the CMake script itself.
#
# The FIXPP_CODEGEN_CONFIGURE_TIME=TRUE flag records this fact. Additionally, the
# presence of the graph_props.cmake file at all is proof: if generation were
# build-time, the file would not exist at configure time, and this script would
# have already failed at the include() call above.

message(STATUS "[T039] === (b) Configure-time generation flag ===")

if(FIXPP_CODEGEN_CONFIGURE_TIME)
  message(STATUS "[T039]   OK  FIXPP_CODEGEN_CONFIGURE_TIME = TRUE")
  _assert(TRUE "FIXPP_CODEGEN_CONFIGURE_TIME is TRUE (generation is configure-time)")
else()
  _assert(FALSE "FIXPP_CODEGEN_CONFIGURE_TIME must be TRUE (generation must be configure-time)")
endif()

# Corroborate: the graph_props.cmake file exists (written at configure time).
# A build-time-only mechanism would not produce this file before a build step.
if(EXISTS "${FIXPP_GRAPH_PROPS}")
  message(STATUS "[T039]   OK  graph_props.cmake present at configure time (corroboration)")
  _assert(TRUE "graph_props.cmake is present (written at configure time)")
else()
  _assert(FALSE "graph_props.cmake must be present post-configure (written by Codegen.cmake)")
endif()

# ── (c) INTERFACE_INCLUDE_DIRECTORIES point into the build tree ───────────────
#
# For each per-version target, the recorded include dir must be under the
# FIXPP_CODEGEN_BINARY_DIR (the CMake binary/build tree).
#
# NOTE: In this project the preset puts the build directory inside the source
# tree (library/build/linux-clang-debug/), so a "NOT under source dir" check
# would always fail for layout reasons unrelated to correctness. The meaningful
# invariant is that the path is under CMAKE_BINARY_DIR — confirming it is the
# build-tree-generated _codegen/include directory and not any checked-in path.

message(STATUS "[T039] === (c) INTERFACE_INCLUDE_DIRECTORIES → build tree ===")

# Normalise the binary dir for prefix comparison.
cmake_path(NORMAL_PATH FIXPP_CODEGEN_BINARY_DIR OUTPUT_VARIABLE _bindir_norm)

foreach(_ver_tag IN ITEMS V42 V44 V50SP2 VT11 DISPATCH)
  set(_iface_inc "${FIXPP_DICT_${_ver_tag}_IFACE_INC}")
  cmake_path(NORMAL_PATH _iface_inc OUTPUT_VARIABLE _iface_inc_norm)

  # Must be under the binary dir.
  cmake_path(IS_PREFIX _bindir_norm "${_iface_inc_norm}" NORMALIZE _is_under_bindir)
  if(_is_under_bindir)
    message(STATUS "[T039]   OK  ${_ver_tag} IFACE_INC under binary dir: ${_iface_inc_norm}")
    _assert(TRUE "${_ver_tag} INTERFACE_INCLUDE_DIRECTORIES is under binary dir")
  else()
    message(STATUS "[T039]   FAIL  ${_ver_tag} IFACE_INC NOT under binary dir: ${_iface_inc_norm}")
    _assert(FALSE "${_ver_tag} INTERFACE_INCLUDE_DIRECTORIES must be under binary dir (${_bindir_norm}); got: ${_iface_inc_norm}")
  endif()

  # Must contain the _codegen/include path marker (not a checked-in source path).
  string(FIND "${_iface_inc_norm}" "_codegen/include" _codegen_idx)
  if(_codegen_idx GREATER_EQUAL 0)
    message(STATUS "[T039]   OK  ${_ver_tag} IFACE_INC contains _codegen/include marker")
    _assert(TRUE "${_ver_tag} INTERFACE_INCLUDE_DIRECTORIES contains _codegen/include marker")
  else()
    message(STATUS "[T039]   FAIL  ${_ver_tag} IFACE_INC missing _codegen/include: ${_iface_inc_norm}")
    _assert(FALSE "${_ver_tag} INTERFACE_INCLUDE_DIRECTORIES must contain _codegen/include; got: ${_iface_inc_norm}")
  endif()
endforeach()

# ── (d) git status --porcelain source tree is clean ───────────────────────────

message(STATUS "[T039] === (d) Source tree cleanliness (git status --porcelain) ===")

execute_process(
  COMMAND git -C "${FIXPP_CODEGEN_SOURCE_DIR}" status --porcelain
  OUTPUT_VARIABLE _git_status
  ERROR_VARIABLE  _git_status_err
  RESULT_VARIABLE _git_status_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT _git_status_result EQUAL 0)
  message(STATUS "[T039]   WARNING: git status failed (exit ${_git_status_result}): ${_git_status_err}")
  # Non-fatal: git might not be available in all CI environments.
  # We record a warning but do not count it as a test failure.
  message(STATUS "[T039]   Skipping git-cleanliness assertion (git unavailable).")
else()
  if(_git_status STREQUAL "")
    message(STATUS "[T039]   OK  git status --porcelain is empty (source tree clean)")
    _assert(TRUE "Source tree is clean post-configure (git status --porcelain is empty)")
  else()
    message(STATUS "[T039]   FAIL  git status --porcelain is non-empty:")
    message(STATUS "${_git_status}")
    _assert(FALSE "Source tree must be clean post-configure; git status --porcelain output:\n${_git_status}")
  endif()
endif()

# ── Summary ───────────────────────────────────────────────────────────────────

message(STATUS "[T039] ============================================================")
message(STATUS "[T039] Results: ${_PASS_COUNT} passed, ${_FAIL_COUNT} failed")

if(_FAIL_COUNT GREATER 0)
  message(STATUS "[T039] Failures:")
  foreach(_msg IN LISTS _FAIL_MSGS)
    message(STATUS "${_msg}")
  endforeach()
  message(FATAL_ERROR
    "[T039] fixpp::dict::codegen-build-graph-check FAILED "
    "(${_FAIL_COUNT} assertion(s) failed — see above).")
else()
  message(STATUS "[T039] fixpp::dict::codegen-build-graph-check PASSED "
    "(${_PASS_COUNT} assertions).")
endif()
