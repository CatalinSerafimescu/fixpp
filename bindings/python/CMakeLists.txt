# bindings/python/CMakeLists.txt
# SWIG Python bindings — built when FIXPP_BUILD_PYTHON=ON.
# Produces _fixpp.so (or _fixpp.pyd on Windows) + fixpp.py.
# [arch §4.12] — depends on fixpp::capi ONLY; no C++ engine headers leak.

# T005 / PKG-1 / D-3 — SWIG 4.2 minimum: a 4.0 runner silently regresses the
# limited-API (abi3) heap-type mode.
#
# ⚠️ THE #296 `<4.5` UPPER BOUND IS LIFTED HERE, together with its twin in
# bindings/python/pyproject.toml — the two were documented as liftable only as a
# pair, and that is how they moved.
#
# It is NOT lifted because #296 was root-caused. It was not. It is lifted because
# capping had made #296 UNFALSIFIABLE: the crash was seen on SWIG 4.5.0 × CPython
# 3.11, and with the cap in force CI built 4.4.1 — so no run could produce
# evidence either way, and every green wheel lane was green for a reason
# unrelated to the question. Retiring the 3.10/3.11 legs alone would not have
# fixed that; it removes the OTHER axis of the same reproducing pair.
#
# ⚠️ SO A RED WHEEL LANE HERE IS A RESULT, NOT A FLAKE. Read it before re-capping:
# the #296 signature is a SEGFAULT during garbage collection with no `FAILED`
# line at all (the interpreter dies before pytest reports), which is why grepping
# for test names on that leg returns empty and looks fine.
#
# 4.2 minimum stays: a 4.0 runner silently regresses the limited-API (abi3)
# heap-type mode (T005 / PKG-1 / D-3).
find_package(SWIG 4.2 REQUIRED)
include(UseSWIG)

# T005 — request the stable-ABI (SABI) development component when available so the
# abi3 wheel links against the limited ABI (Python3::SABIModule). Optional so a
# plain in-tree build on an older CMake/Python still configures with Development.Module.
# scikit-build-core (wheel.py-api="cp312") drives this via SKBUILD_SABI_COMPONENT.
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
# cp312 floor. COMPILE-ONLY: the wheel ABI tag is set by scikit-build-core's
# wheel.py-api="cp312" (pyproject.toml), NOT by this flag (scikit-build-core does
# not auto-detect the limited ABI). The two must move TOGETHER: this flag decides
# which C API the wrapper may call, the pyproject key decides what the wheel
# CLAIMS to run on, and a wheel tagged below its compile floor is an import crash
# on the versions it claims.
#
# ⚠️ The floor was 0x030A0000 (cp310) through v1.0. It moved with the retirement
# of the 3.10 and 3.11 install-test legs — an abi3 floor BELOW the oldest tested
# interpreter is a claim nothing exercises.
#
# TO RE-VERIFY after any change to fixpp.i or a SWIG bump — the T004 feasibility
# check, reproduced verbatim (needs python3-dev headers for the floor version):
#
#   swig -c++ -python -Iinclude -o /tmp/w.cxx bindings/python/fixpp.i
#   g++ -fsyntax-only -std=c++23 -DPy_LIMITED_API=0x030C0000 \
#       -I/usr/include/python3.12 -Iinclude /tmp/w.cxx
#
# ⚠️ A CLEAN result there is worth nothing until the check is shown able to fail —
# this repo's most recurring defect is an instrument that reports clean because it
# could not report anything else. Force it: a TU calling a NON-limited-API symbol
# (`PyUnicode_DATA` is one) must ERROR under the -D and compile clean without it.
target_compile_definitions(fixpp_py PRIVATE Py_LIMITED_API=0x030C0000)

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
# ── #257 — the THIRD layout: a prefix-relative destination for the packages ──
#
# The two branches below answer "where does an interpreter expect to import
# from". That is the right question for an in-tree `cmake --install` and the
# wrong one for a RELOCATABLE C++ package, which is what #257 decided to ship:
#
#   sitearch (default) - 056 LAY-1 / D-4 / T006. A plain in-tree install lands on
#                        the real interpreter (or "." when SITEARCH is unknown,
#                        and under SKBUILD where the wheel platlib root IS ".").
#                        UNCHANGED — this is a delivered requirement, not an
#                        incidental behaviour, so #257 must not repoint it.
#   package            - the `-release` packaging legs. Prefix-relative, so
#                        --prefix / DESTDIR / CPack staging all work.
#
# ⚠️ WHY "." WAS NOT SIMPLY REUSED for the packages, which is what CI resolves
# today. Measured on tier1 run 34216189194, linux-gcc-release: `Python3_SITEARCH
# is EMPTY on the matrix legs`, so the existing code already yields ".". That
# would scatter _fixpp.so, three .py modules and _fixpp_data/ across the package
# PREFIX ROOT, beside include/ and lib/ — and it would be INHERITED rather than
# chosen, which is precisely what #257 says not to do ("Any shipping decision
# must pin the destination, not inherit it"). It is also fragile in the
# direction that fails silently: a leg that ever DID resolve SITEARCH would
# install to an absolute path, which `cmake --install --prefix X` IGNORES.
set(FIXPP_PY_INSTALL_LAYOUT "sitearch" CACHE STRING
    "Where FIXPP_INSTALL_PYTHON puts the payload: sitearch|package")
set_property(CACHE FIXPP_PY_INSTALL_LAYOUT PROPERTY STRINGS sitearch package)

# The C++ install already puts the dictionaries here (CMakeLists.txt, T033 /
# FR-018a). Named once, consumed by both the relative-path computation and the
# generated marker's docstring.
set(FIXPP_PY_DICT_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/fixpp/dictionaries")

if(FIXPP_PY_INSTALL_LAYOUT STREQUAL "package")
  set(FIXPP_PY_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/fixpp/python")
elseif(FIXPP_PY_INSTALL_LAYOUT STREQUAL "sitearch")
  if(DEFINED SKBUILD OR NOT Python3_SITEARCH)
    set(FIXPP_PY_INSTALL_DIR ".")
  else()
    set(FIXPP_PY_INSTALL_DIR "${Python3_SITEARCH}")
  endif()
else()
  # Fail closed. A typo must not silently fall through to a layout the caller
  # did not ask for — that is how a payload lands somewhere nobody inspects.
  message(FATAL_ERROR
    "FIXPP_PY_INSTALL_LAYOUT must be 'sitearch' or 'package', got "
    "'${FIXPP_PY_INSTALL_LAYOUT}'.")
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

# ⚠️ PROMOTED OUT OF DIRECTORY SCOPE (#257). tests/packaging/ is a SIBLING
# directory, not a child, so a plain set() here is invisible to it — and the
# packaging witness needs this exact path to anchor its "the payload must not
# duplicate the bundled XMLs" search. INTERNAL rather than a user-facing cache
# entry: it is derived from FIXPP_PY_INSTALL_LAYOUT above, and a user who
# overrode it directly would silently desynchronise the install rules from the
# witness that checks them.
set(FIXPP_PY_INSTALL_DIR "${FIXPP_PY_INSTALL_DIR}" CACHE INTERNAL
    "Resolved destination for the Python payload (derived; do not set directly)" FORCE)

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
#
# ── #257 — the package layout ships the marker WITHOUT the XMLs ──────────────
# The C++ install already puts all of dictionaries/ under
# ${FIXPP_PY_DICT_INSTALL_DIR}, so bundling the same four files here would ship
# 1.9 MB twice in one archive, in two locations, with no stated precedence.
# Instead a GENERATED __init__.py carries DICTIONARY_DIR (computed relative to
# itself, so the package stays relocatable) and fixpp_dict_data's locator falls
# back to it.
#
# ⚠️ THAT FALLBACK IS A BRANCH THE WHEEL NEVER EXECUTES, which is exactly the
# shape that ships untested. It is witnessed by FIXPP_PY_WITNESS_MODE=package,
# which resolves a real dictionary through the real locator against a staged
# tree — not by asserting the file exists.
if(FIXPP_PY_INSTALL_LAYOUT STREQUAL "package")
  # The relative hop between the two install destinations. Both are made
  # absolute with a leading "/" purely so file(RELATIVE_PATH) can subtract
  # them; the RESULT is prefix-independent, which is the point.
  file(RELATIVE_PATH FIXPP_PY_DICT_RELPATH
       "/${FIXPP_PY_INSTALL_DIR}/_fixpp_data"
       "/${FIXPP_PY_DICT_INSTALL_DIR}")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/_fixpp_data/package_init.py.in"
    "${CMAKE_CURRENT_BINARY_DIR}/package_layout/_fixpp_data/__init__.py"
    @ONLY)
  message(STATUS "fixpp: python payload -> '${FIXPP_PY_INSTALL_DIR}'; "
                 "dictionaries NOT duplicated, reached via "
                 "'${FIXPP_PY_DICT_RELPATH}'")
  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/package_layout/_fixpp_data/__init__.py"
    DESTINATION "${FIXPP_PY_INSTALL_DIR}/_fixpp_data"
  )
else()
  install(
    FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/_fixpp_data/__init__.py"
      "${CMAKE_SOURCE_DIR}/dictionaries/FIX42.xml"
      "${CMAKE_SOURCE_DIR}/dictionaries/FIX44.xml"
      "${CMAKE_SOURCE_DIR}/dictionaries/FIX50SP2.xml"
      "${CMAKE_SOURCE_DIR}/dictionaries/FIXT11.xml"
    DESTINATION "${FIXPP_PY_INSTALL_DIR}/_fixpp_data"
  )
endif()

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
  # #257 adds a THIRD mode. The three are jointly exhaustive and mutually
  # exclusive over every non-SKBUILD build that has the bindings:
  #
  #   OFF                        -> absent   (four `linux` matrix legs)
  #   ON  + layout=sitearch      -> present  (default; 056 LAY-1 in-tree)
  #   ON  + layout=package       -> package  (the two `-release` packaging legs)
  #
  # `package` is NOT a relaxation of `present`. It asserts a DIFFERENT tree:
  # the payload beside the module at a prefix-relative destination, the four
  # XMLs deliberately ABSENT, and — the part no file-existence check can reach —
  # the locator actually resolving a dictionary through the datadir fallback.
  if(NOT FIXPP_INSTALL_PYTHON)
    set(_fixpp_py_witness_mode "absent")
  elseif(FIXPP_PY_INSTALL_LAYOUT STREQUAL "package")
    set(_fixpp_py_witness_mode "package")
  else()
    set(_fixpp_py_witness_mode "present")
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
