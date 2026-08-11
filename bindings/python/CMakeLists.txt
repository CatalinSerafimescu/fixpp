# bindings/python/CMakeLists.txt
# SWIG Python bindings — built when FIXPP_BUILD_PYTHON=ON.
# Produces _fixpp.so (or _fixpp.pyd on Windows) + fixpp.py.
# [arch §4.12] — depends on fixpp::capi ONLY; no C++ engine headers leak.

# T005 / PKG-1 / D-3 — SWIG 4.2 PINNED: a 4.0 runner silently regresses the
# limited-API (abi3) heap-type mode.
find_package(SWIG 4.2 REQUIRED)
include(UseSWIG)

# T005 — request the stable-ABI (SABI) development component when available so the
# abi3 wheel links against the limited ABI (Python3::SABIModule). Optional so a
# plain in-tree build on an older CMake/Python still configures with Development.Module.
# scikit-build-core (wheel.py-api="cp310") drives this via SKBUILD_SABI_COMPONENT.
find_package(Python3 REQUIRED COMPONENTS Development.Module
             OPTIONAL_COMPONENTS Development.SABIModule)

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

# T005 — link the limited-ABI (SABI) module when present so the abi3 wheel carries
# no version-specific libpython dependency; else the classic Module target.
if(TARGET Python3::SABIModule)
  set(FIXPP_PY_PYTHON_LINK Python3::SABIModule)
else()
  set(FIXPP_PY_PYTHON_LINK Python3::Module)
endif()
target_link_libraries(fixpp_py PRIVATE
  fixpp_capi
  ${FIXPP_PY_PYTHON_LINK}
)

# T005 / PKG-6 / D-3 — compile the SWIG wrapper against the limited C API (abi3),
# cp310 floor. COMPILE-ONLY: the wheel ABI tag is set by scikit-build-core's
# wheel.py-api="cp310" (pyproject.toml), NOT by this flag (scikit-build-core does
# not auto-detect the limited ABI). The reworked fixpp_py_is_main_interpreter
# (T003) compiles clean at this floor — proven by the T004 abi3 feasibility gate.
target_compile_definitions(fixpp_py PRIVATE Py_LIMITED_API=0x030A0000)

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
# (mutually exclusive asan|ubsan|tsan). For a CONSISTENT instrumentation of the
# static deps (fixpp_capi + the engine libs it pulls in), configure the whole
# tree with the matching sanitizer flag + -DFIXPP_BUILD_PYTHON=ON, e.g.
#   -DFIXPP_ENABLE_ASAN=ON  -DFIXPP_PYTHON_SANITIZER=asan
#   -DFIXPP_ENABLE_UBSAN=ON -DFIXPP_PYTHON_SANITIZER=ubsan
#   -DFIXPP_ENABLE_TSAN=ON  -DFIXPP_PYTHON_SANITIZER=tsan
# the tree flag instruments the deps, this option instruments the module TU
# itself. The static C++ runtime is dropped under a sanitizer (the sanitizer
# runtime supplies its own and -static-libstdc++ fights it) — the sanitizer leg
# is a correctness lane, not the portability artifact (that is verified at PY-005
# with a clean wheel).
set(FIXPP_PYTHON_SANITIZER "none" CACHE STRING
    "Sanitizer for the _fixpp extension: none|asan|ubsan|tsan")
set_property(CACHE FIXPP_PYTHON_SANITIZER PROPERTY STRINGS none asan ubsan tsan)
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
elseif(FIXPP_PYTHON_SANITIZER STREQUAL "ubsan")
  # Mirror the asan leg: instrument the module TU with UBSan and force a SHARED
  # ubsan runtime dependency (-shared-libsan) + rpath so `import _fixpp` into the
  # unsanitized interpreter resolves libclang_rt.ubsan_standalone.so without an
  # LD_PRELOAD. Retires the W-1 / L-054-2 / 053 D-9 "no UBSan leg" waiver.
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -print-runtime-dir
    OUTPUT_VARIABLE FIXPP_CLANG_RUNTIME_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  target_compile_options(fixpp_py PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(fixpp_py PRIVATE -fsanitize=undefined -shared-libsan)
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

# PY-005 / 056 (T007/T008): stage the locator + the _fixpp_data package (the four
# FIX XMLs from the single-source dictionaries/) into lib/ so an in-tree
# PYTHONPATH=lib run resolves fixpp.dictionary_path(...) exactly as the wheel does.
add_custom_command(TARGET fixpp_py POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/fixpp_dict_data.py"
          "${CMAKE_BINARY_DIR}/lib/fixpp_dict_data.py"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${CMAKE_BINARY_DIR}/lib/_fixpp_data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/_fixpp_data/__init__.py"
          "${CMAKE_SOURCE_DIR}/dictionaries/FIX42.xml"
          "${CMAKE_SOURCE_DIR}/dictionaries/FIX44.xml"
          "${CMAKE_SOURCE_DIR}/dictionaries/FIX50SP2.xml"
          "${CMAKE_SOURCE_DIR}/dictionaries/FIXT11.xml"
          "${CMAKE_BINARY_DIR}/lib/_fixpp_data"
  COMMENT "Staging fixpp_dict_data + _fixpp_data XMLs next to _fixpp.so")

set_target_properties(fixpp_py PROPERTIES
  SWIG_USE_TARGET_INCLUDE_DIRECTORIES ON
  # UseSWIG prepends its own "_" to the module basename, so "fixpp" yields the
  # contract-specified low-level module "_fixpp" (-> _fixpp.so, PyInit__fixpp).
  # The prior "_fixpp" double-prepended to "__fixpp" (053: fixpp.Error re-export
  # needs the proxy to import the spec'd _fixpp).
  OUTPUT_NAME "fixpp"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ── T006 / D-4 / LAY-1 — FLAT install layout ────────────────────────────────
# Ship _fixpp.so + fixpp.py + fixpp_oo.py + fixpp_dict_data.py + the _fixpp_data
# package at the site-packages ROOT (flat top-level modules), fixing the latent
# install(... /fixpp) namespace dir that made the import `fixpp.fixpp` and only
# worked under in-tree PYTHONPATH=lib. Under scikit-build-core the wheel platlib
# root is "."; a plain in-tree `cmake --install` uses the real Python3_SITEARCH.
# Under scikit-build-core (the wheel build) SKBUILD is defined and the platlib
# root is "." — the flat top-level destination. A plain in-tree `cmake --install`
# uses the real Python3_SITEARCH when known (the Development.Module component does
# not always populate it without Interpreter); fall back to "." otherwise (in-tree
# runs use PYTHONPATH=lib via the POST_BUILD staging above, not this install).
if(DEFINED SKBUILD OR NOT Python3_SITEARCH)
  set(FIXPP_PY_INSTALL_DIR ".")
else()
  set(FIXPP_PY_INSTALL_DIR "${Python3_SITEARCH}")
endif()

# ── #254 — separate BUILDING the bindings from INSTALLING them ───────────────
# Tier 1's six `linux` matrix legs build the bindings (to run pytest) but must
# NOT install them: two of those legs (`linux-{clang,gcc}-release`) also build
# `fixpp-package`, and the consumer witness stage-installs on all six. With the
# rules below unconditional, folding the bindings into those legs silently puts
# _fixpp.so + the three .py modules + _fixpp_data into
# packages-linux-{clang,gcc}-release — falsifying L-056-4 ("the C++ packages
# contain no Python") and shipping, by accident, exactly the payload that was
# deliberately descoped from #254 (the decision is #257's to make).
#
# ⚠️ DEFAULT ON IS LOAD-BEARING. Feature 056 (LAY-1 / D-4 / T006) decided that a
# plain in-tree `cmake --install` installs the bindings; that is a delivered
# requirement, not an incidental behaviour. Only the six matrix legs pass OFF.
#
# ⚠️ Do NOT "simplify" this to `if(DEFINED SKBUILD)`. That gates the rules on the
# WHEEL build and would silently regress the in-tree path 056 requires — while
# still passing python-wheel-test, which only ever exercises the SKBUILD half.
#
# OFF costs the tests nothing: in-tree pytest imports from
# PYTHONPATH=build/<preset>/lib, staged by the POST_BUILD copy above — not by
# install(). See the comment at the top of this block.
option(FIXPP_INSTALL_PYTHON
       "Install the Python binding artifacts (module, pure-Python modules, _fixpp_data)" ON)

# Report the resolved destination on every configure. FIXPP_PY_INSTALL_DIR has
# three reachable outcomes and they fail in different directions: "." pollutes
# the install-prefix root; an absolute writable SITEARCH makes `cmake --install
# --prefix X` IGNORE the prefix (so a prefix-diff is structurally BLIND to it);
# an absolute non-writable SITEARCH makes the install FATAL_ERROR. Locally the
# "." branch fires, but actions/setup-python runs before Configure in CI, so
# which branch fires there is NOT established — hence reported, not assumed.
message(STATUS "fixpp: FIXPP_INSTALL_PYTHON=${FIXPP_INSTALL_PYTHON}; "
               "Python3_SITEARCH='${Python3_SITEARCH}'; "
               "FIXPP_PY_INSTALL_DIR='${FIXPP_PY_INSTALL_DIR}'")

if(FIXPP_INSTALL_PYTHON)

install(TARGETS fixpp_py DESTINATION "${FIXPP_PY_INSTALL_DIR}")
install(
  FILES "${CMAKE_BINARY_DIR}/lib/fixpp.py"
  DESTINATION "${FIXPP_PY_INSTALL_DIR}"
  OPTIONAL  # generated by SWIG; may be in swig output dir depending on version
)

# PY-004 / 055 + PY-005 / 056 (T006/T008): the pure-Python implementation modules
# (OO layer + dictionary locator), re-exported through `import fixpp`.
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/fixpp_oo.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixpp_dict_data.py"
  DESTINATION "${FIXPP_PY_INSTALL_DIR}"
)

# ── T007 / E-3 / LAY-2 — bundled FIX dictionary data package ────────────────
# _fixpp_data/__init__.py + the four XMLs staged from the submodule dictionaries/
# (single source of truth; never hand-duplicated). The locator resolves them via
# importlib.resources.files("_fixpp_data").
install(
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/_fixpp_data/__init__.py"
    "${CMAKE_SOURCE_DIR}/dictionaries/FIX42.xml"
    "${CMAKE_SOURCE_DIR}/dictionaries/FIX44.xml"
    "${CMAKE_SOURCE_DIR}/dictionaries/FIX50SP2.xml"
    "${CMAKE_SOURCE_DIR}/dictionaries/FIXT11.xml"
  DESTINATION "${FIXPP_PY_INSTALL_DIR}/_fixpp_data"
)

endif()  # FIXPP_INSTALL_PYTHON — see the option() block above (#254 / L-056-4)

# ── #254 / Gate A round 2 — the two install witnesses (R2-P2-1, R2-P2-2) ─────
#
# Exactly ONE of these registers, decided by the option above, and together they
# are jointly exhaustive over every non-SKBUILD build that has the bindings:
#
#   FIXPP_INSTALL_PYTHON=ON  (default, every ordinary build)  -> `present`
#   FIXPP_INSTALL_PYTHON=OFF (tier 1's six `linux` legs)      -> `absent`
#
# That symmetry is the design. Each mode's proven-RED evidence is the OTHER
# configuration of the same tree — no synthetic self-test, no planted files:
#
#   config                    present-witness   absent-witness
#   FIXPP_INSTALL_PYTHON=ON     GREEN             RED (if run)
#   FIXPP_INSTALL_PYTHON=OFF    RED  (if run)     GREEN
#
# ⚠️ `absent` registers NOWHERE until tier1.yml passes -DFIXPP_INSTALL_PYTHON=OFF
# on the six legs. Until then it is inert by construction, not by accident. The
# workflow change must assert that it RAN (`ctest -N` count), not merely that
# ctest was green — the consumer witness's `expected=1` step is the precedent.
#
# UNIX-only: DESTDIR is not supported by CMake's install on Windows, and DESTDIR
# rather than --prefix is the whole point (see the script's header — --prefix is
# structurally blind to the absolute-Python3_SITEARCH destination branch).
#
# Not registered under SKBUILD: the wheel build has no test tree, and the ON-side
# question R2-P2-2 asks is specifically about the NON-SKBUILD in-tree path that
# `python-wheel-test` cannot see.
if(FIXPP_BUILD_TESTS AND UNIX AND NOT DEFINED SKBUILD)
  if(FIXPP_INSTALL_PYTHON)
    set(_fixpp_py_witness_mode "present")
  else()
    set(_fixpp_py_witness_mode "absent")
  endif()

  add_test(
    NAME "fixpp::python::install-${_fixpp_py_witness_mode}-witness"
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXPP_MAIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
      "-DFIXPP_PY_WITNESS_MODE=${_fixpp_py_witness_mode}"
      "-DFIXPP_PY_WITNESS_WORK_DIR=${CMAKE_BINARY_DIR}/_python_install_witness"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_python_install_witness.cmake"
  )
  set_tests_properties("fixpp::python::install-${_fixpp_py_witness_mode}-witness" PROPERTIES
    LABELS "python;install"
    # A full stage-install of the C++ closure plus the python payload. The
    # consumer witness, which additionally CONFIGURES AND BUILDS a sub-project
    # off its staged prefix, sits at 300; this one only installs.
    TIMEOUT 300)

  unset(_fixpp_py_witness_mode)
endif()
