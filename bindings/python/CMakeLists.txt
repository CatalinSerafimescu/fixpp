# bindings/python/CMakeLists.txt
# SWIG Python bindings — built when FIXPP_BUILD_PYTHON=ON.
# Produces _fixpp.so (or _fixpp.pyd on Windows) + fixpp.py.
# [arch §4.12] — depends on fixpp::capi ONLY; no C++ engine headers leak.

find_package(SWIG 4.0 REQUIRED)
include(UseSWIG)

find_package(Python3 REQUIRED COMPONENTS Development.Module)

set_property(SOURCE fixpp.i PROPERTY CPLUSPLUS ON)
set_property(SOURCE fixpp.i PROPERTY USE_TARGET_INCLUDE_DIRECTORIES ON)

# Direct SWIG's generated fixpp.py to lib/ so it sits next to _fixpp.so;
# `import fixpp` then loads the wrapper, which loads _fixpp.so from the
# same directory. Without this, fixpp.py lands under CMakeFiles/ and
# PYTHONPATH=lib/ cannot find it.
set(CMAKE_SWIG_OUTDIR "${CMAKE_BINARY_DIR}/lib")

swig_add_library(fixpp_py
  TYPE MODULE
  LANGUAGE python
  SOURCES fixpp.i
)

# Only the C ABI include path is exposed — no C++ headers [arch §9.1]
target_include_directories(fixpp_py PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
)

target_link_libraries(fixpp_py PRIVATE
  fixpp_capi
  Python3::Module
)

# ── T003 / FR-010 / research D-6 — self-contained static link ───────────────
# Link the STATIC fixpp_capi archive (already PIC: src/capi builds it from an
# OBJECT lib with POSITION_INDEPENDENT_CODE ON) plus -static-libstdc++/-libgcc
# on Linux, so _fixpp.so carries no runtime libfixpp_capi.so / libstdc++ SONAME
# dependency. Safe because nothing C++ crosses the extern "C" boundary (D-6):
# the C-ABI thunks catch-all, so the C++ stdlib is fully resolved inside the
# extension and never reaches the interpreter.
#
# ── T004 / SC-004 / research D-9 — sanitizer build path for the extension ────
# FIXPP_PYTHON_SANITIZER selects an instrumented build of the _fixpp extension
# (mutually exclusive asan|tsan). For a CONSISTENT instrumentation of the static
# deps (fixpp_capi + the engine libs it pulls in), configure the whole tree with
# the matching sanitizer preset (linux-clang-asan / linux-clang-tsan) +
# -DFIXPP_BUILD_PYTHON=ON -DFIXPP_PYTHON_SANITIZER=asan|tsan: the preset flags
# instrument the deps, this option instruments the module TU itself. The static
# C++ runtime is dropped under a sanitizer (the asan/tsan runtime supplies its
# own and -static-libstdc++ fights it) — the sanitizer leg is a correctness lane,
# not the portability artifact (that is verified at PY-005 with a clean wheel).
set(FIXPP_PYTHON_SANITIZER "none" CACHE STRING
    "Sanitizer for the _fixpp extension: none|asan|tsan")
set_property(CACHE FIXPP_PYTHON_SANITIZER PROPERTY STRINGS none asan tsan)
option(FIXPP_PY_GIL_CANARY
  "Elide PyGILState_Ensure/Release in the recv trampoline for TSan calibration"
  OFF)

if(FIXPP_PY_GIL_CANARY)
  target_compile_definitions(fixpp_py PRIVATE FIXPP_PY_GIL_CANARY=1)
endif()

# ── T001 / FR-004 / data-model E-4 — GIL-RELEASE canary (PY-002), LOCAL-ONLY ──
# When ON, elides the Py_BEGIN/END_ALLOW_THREADS bands on session_close /
# session_send / engine_destroy so the teardown-vs-in-flight-recv-callback
# scenario DEADLOCKS (test_gil_release_canary.py asserts the hang via a
# subprocess hard timeout). Distinct from FIXPP_PY_GIL_CANARY above (the 053
# reacquire canary -> segfault). DELIBERATE-HANG build — never set in any CI
# preset (FR-013); only its GREEN leg (this option OFF) runs in the matrix.
option(FIXPP_PY_GIL_RELEASE_CANARY
  "Elide the GIL release around the blocking wrappers (PY-002 release canary); LOCAL-ONLY"
  OFF)

if(FIXPP_PY_GIL_RELEASE_CANARY)
  target_compile_definitions(fixpp_py PRIVATE FIXPP_PY_GIL_RELEASE_CANARY=1)
endif()

if(FIXPP_PYTHON_SANITIZER STREQUAL "asan")
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -print-runtime-dir
    OUTPUT_VARIABLE FIXPP_CLANG_RUNTIME_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  target_compile_options(fixpp_py PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  # Python loads _fixpp.so into an unsanitized host process; force a shared ASan
  # runtime dependency on the module itself so import does not rely on LD_PRELOAD.
  target_link_options(fixpp_py PRIVATE -fsanitize=address -shared-libasan)
  set_target_properties(fixpp_py PROPERTIES
    BUILD_RPATH "${FIXPP_CLANG_RUNTIME_DIR}"
    INSTALL_RPATH "${FIXPP_CLANG_RUNTIME_DIR}"
  )
elseif(FIXPP_PYTHON_SANITIZER STREQUAL "tsan")
  target_compile_options(fixpp_py PRIVATE -fsanitize=thread)
  target_link_options(fixpp_py PRIVATE -fsanitize=thread)
elseif(UNIX AND NOT APPLE)
  # Portability link only on the non-sanitized lane.
  target_link_options(fixpp_py PRIVATE -static-libstdc++ -static-libgcc)
endif()

# PY-004 / 055 (T002): place fixpp_oo.py in lib/ next to _fixpp.so + the
# generated fixpp.py so `PYTHONPATH=lib python -c "import fixpp"` finds the OO
# layer the re-export glue imports.
add_custom_command(TARGET fixpp_py POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/fixpp_oo.py"
          "${CMAKE_BINARY_DIR}/lib/fixpp_oo.py"
  COMMENT "Copying fixpp_oo.py next to _fixpp.so")

set_target_properties(fixpp_py PROPERTIES
  SWIG_USE_TARGET_INCLUDE_DIRECTORIES ON
  # UseSWIG prepends its own "_" to the module basename, so "fixpp" yields the
  # contract-specified low-level module "_fixpp" (-> _fixpp.so, PyInit__fixpp).
  # The prior "_fixpp" double-prepended to "__fixpp" (053: fixpp.Error re-export
  # needs the proxy to import the spec'd _fixpp).
  OUTPUT_NAME "fixpp"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Install: place _fixpp.so + fixpp.py together so `import fixpp` works
install(TARGETS fixpp_py DESTINATION "${Python3_SITEARCH}/fixpp")
install(
  FILES "${CMAKE_BINARY_DIR}/lib/fixpp.py"
  DESTINATION "${Python3_SITEARCH}/fixpp"
  OPTIONAL  # generated by SWIG; may be in swig output dir depending on version
)

# PY-004 / 055 (T002): ship the pure-Python OO layer next to _fixpp.so + fixpp.py.
# fixpp.py (re-export glue) imports it; flat substrate stays usable without it.
install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/fixpp_oo.py"
  DESTINATION "${Python3_SITEARCH}/fixpp"
)
