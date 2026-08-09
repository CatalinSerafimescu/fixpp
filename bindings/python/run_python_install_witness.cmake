# run_python_install_witness.cmake — #254 / Gate A round 2 R2-P2-1 + R2-P2-2
#
# A SEMANTIC witness for FIXPP_INSTALL_PYTHON: it performs a real
# `cmake --install` into a throwaway staging root and inspects what actually
# landed there. Two modes, mutually exclusive by construction:
#
#   FIXPP_PY_WITNESS_MODE=absent   the OFF side (the six tier-1 `linux` legs).
#                                  Rejects the Python payload ANYWHERE in the
#                                  staged tree. Guards L-056-4.
#   FIXPP_PY_WITNESS_MODE=present  the ON side (every ordinary in-tree build).
#                                  Requires the full payload. Guards feature
#                                  056's LAY-1 / D-4 / T006 in-tree install.
#
# ── Why this exists when tier1.yml already greps cmake_install.cmake ─────────
#
# The §4.5.3 CI step counts `file(INSTALL` lines in the GENERATED
# bindings/python/cmake_install.cmake. That is an implementation-SHAPE pin, and
# Gate A round 2 (R2-P2-1) was right that it certifies less than its stated
# contract: it passes if the payload later moves to `install(CODE)`, to a
# different directory's install script, or to a different destination — and it
# never opens either Release artifact. Both instruments are kept. The grep is
# cheap and runs at Configure time, before anything is built; this one is the
# content gate.
#
# ── ⚠️ DESTDIR, NOT --prefix. This is the correctness point, not a style choice ─
#
# FIXPP_PY_INSTALL_DIR has three reachable outcomes (bindings/python/CMakeLists.txt
# and .specify/ci254-python-fold.md §4.5.2). On the absolute-`Python3_SITEARCH`
# branch the DESTINATION is ABSOLUTE, and `cmake --install --prefix X` IGNORES
# the prefix for an absolute destination: the files land on the real interpreter,
# outside X, and a witness that scans X sees an empty delta and reports PASS.
# That is precisely the blindness R2-P2-1 exists to close, and it would not show
# up locally, where Python3_SITEARCH is empty and the "." branch fires.
#
# DESTDIR prepends to absolute destinations, so it captures BOTH branches. The
# scan below therefore walks the WHOLE staging root, not `${stage}${prefix}` —
# an absolute-SITEARCH payload lands outside the prefix subtree.
#
# (tests/consumer/run_consumer_witness.cmake uses `--prefix`. Do not copy that
# line here; it is answering a different question, about a relocatable C++
# package whose destinations are all prefix-relative.)
#
# Driven via `cmake -P`, mirroring the consumer/packaging witness pattern.
#
# Required:
#   FIXPP_MAIN_BUILD_DIR      the configured+built tree to install
#   FIXPP_PY_WITNESS_MODE     absent | present
#   FIXPP_PY_WITNESS_WORK_DIR staging root (wiped at start)

cmake_minimum_required(VERSION 3.24)

foreach(_v FIXPP_MAIN_BUILD_DIR FIXPP_PY_WITNESS_MODE FIXPP_PY_WITNESS_WORK_DIR)
  if(NOT DEFINED ${_v} OR "${${_v}}" STREQUAL "")
    message(FATAL_ERROR "run_python_install_witness.cmake: ${_v} is required.")
  endif()
endforeach()

if(NOT FIXPP_PY_WITNESS_MODE STREQUAL "absent" AND NOT FIXPP_PY_WITNESS_MODE STREQUAL "present")
  message(FATAL_ERROR "run_python_install_witness.cmake: FIXPP_PY_WITNESS_MODE must be 'absent' or 'present', got '${FIXPP_PY_WITNESS_MODE}'.")
endif()

set(_stage "${FIXPP_PY_WITNESS_WORK_DIR}")
file(REMOVE_RECURSE "${_stage}")
file(MAKE_DIRECTORY "${_stage}")

message(STATUS "python-install-witness [${FIXPP_PY_WITNESS_MODE}]: staging "
               "`cmake --install ${FIXPP_MAIN_BUILD_DIR}` under DESTDIR=${_stage}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "DESTDIR=${_stage}"
          "${CMAKE_COMMAND}" --install "${FIXPP_MAIN_BUILD_DIR}"
  RESULT_VARIABLE _install_rc
  OUTPUT_VARIABLE _install_out
  ERROR_VARIABLE  _install_err
)
if(NOT _install_rc EQUAL 0)
  message(FATAL_ERROR
    "python-install-witness [${FIXPP_PY_WITNESS_MODE}]: `cmake --install` FAILED (rc=${_install_rc}).\n"
    "This is a real finding, not witness noise: the non-writable absolute-SITEARCH branch\n"
    "(§4.5.2 row 3) fails exactly here, and DESTDIR is supposed to make it impossible.\n"
    "--- stdout ---\n${_install_out}\n--- stderr ---\n${_install_err}")
endif()

# The whole staging root. GLOB_RECURSE does not descend into the tree's
# directories-as-entries, so `_fixpp_data` is matched via the files under it.
file(GLOB_RECURSE _staged LIST_DIRECTORIES true "${_stage}/*")
list(LENGTH _staged _staged_n)
if(_staged_n EQUAL 0)
  message(FATAL_ERROR
    "python-install-witness [${FIXPP_PY_WITNESS_MODE}]: the staging root is EMPTY after a\n"
    "successful `cmake --install`. The instrument is broken — an empty scan would make the\n"
    "`absent` mode pass vacuously. (feedback_silent_empty_recurred_three_times…)")
endif()
message(STATUS "python-install-witness: ${_staged_n} staged entries under ${_stage}")

# ── The payload, by NAME component ───────────────────────────────────────────
# Matching on the basename, not on a path substring: the four bundled XMLs
# (FIX42/FIX44/FIX50SP2/FIXT11) also exist in the C++ install and in the source
# tree, so a name match on them would false-positive. They are caught instead by
# the `_fixpp_data` directory that contains them — which is the ONLY place the
# python install rule puts them.
set(_payload_names
  "fixpp.py"              # SWIG-generated (installed OPTIONAL)
  "fixpp_oo.py"           # PY-004 / 055 OO layer
  "fixpp_dict_data.py")   # PY-004 / 055 dictionary locator
# Everything the python rules put under a leading-underscore name — the
# extension module (`_fixpp.so`, or `_fixpp.cpython-*.so` where SOABI is tagged)
# and the `_fixpp_data` package directory with its four XMLs. One prefix covers
# both, and it is deliberately broader than the two literals: a renamed module
# or a sibling `_fixpp_something` is payload too.
set(_payload_prefix "^_fixpp")

set(_hits "")
foreach(_p IN LISTS _staged)
  get_filename_component(_name "${_p}" NAME)
  if(_name IN_LIST _payload_names OR _name MATCHES "${_payload_prefix}")
    list(APPEND _hits "${_p}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _hits)
list(SORT _hits)

if(FIXPP_PY_WITNESS_MODE STREQUAL "absent")
  if(_hits)
    string(REPLACE ";" "\n  " _pretty "${_hits}")
    message(FATAL_ERROR
      "python-install-witness [absent]: this mode asserts FIXPP_INSTALL_PYTHON=OFF suppresses the\n"
      "Python payload, but `cmake --install` STAGED it:\n  ${_pretty}\n\n"
      "On linux-{clang,gcc}-release this payload enters `fixpp-package` and therefore the\n"
      "packages-linux-*-release artifacts, falsifying L-056-4 (\"the C++ consumer deliverable\n"
      "contains no Python\") — the exact accident #254 exists not to have. Whether the packages\n"
      "SHOULD carry Python is #257's decision; shipping it unintentionally is not an option.")
  endif()
  message(STATUS "python-install-witness [absent]: PASS — 0 payload entries in ${_staged_n} staged.")
  file(REMOVE_RECURSE "${_stage}")
  return()
endif()

# ── present ──────────────────────────────────────────────────────────────────
# The install rules are OPTIONAL for fixpp.py only; this witness requires it
# anyway. That is deliberate and it is the point of the mode: 056's LAY-1 says a
# plain in-tree `cmake --install` installs a WORKING binding, and a silently
# skipped fixpp.py ships a module nobody can import.
set(_required
  "fixpp.py"
  "fixpp_oo.py"
  "fixpp_dict_data.py"
  "_fixpp_data/__init__.py"
  "_fixpp_data/FIX42.xml"
  "_fixpp_data/FIX44.xml"
  "_fixpp_data/FIX50SP2.xml"
  "_fixpp_data/FIXT11.xml")

set(_missing "")
foreach(_r IN LISTS _required)
  set(_found FALSE)
  foreach(_p IN LISTS _staged)
    if(_p MATCHES "/${_r}$")
      set(_found TRUE)
      break()
    endif()
  endforeach()
  if(NOT _found)
    list(APPEND _missing "${_r}")
  endif()
endforeach()

# The extension module's basename carries an SOABI tag on some configurations,
# so it is matched by pattern rather than by literal name.
set(_module_found FALSE)
foreach(_p IN LISTS _staged)
  get_filename_component(_name "${_p}" NAME)
  if(_name MATCHES "^_fixpp.*\\.(so|pyd)$")
    set(_module_found TRUE)
    break()
  endif()
endforeach()
if(NOT _module_found)
  list(APPEND _missing "_fixpp*.so (the extension module)")
endif()

if(_missing)
  string(REPLACE ";" "\n  " _pretty "${_missing}")
  string(REPLACE ";" "\n  " _all "${_staged}")
  message(FATAL_ERROR
    "python-install-witness [present]: this mode asserts that with FIXPP_INSTALL_PYTHON at its\n"
    "default ON and SKBUILD undefined, a plain `cmake --install` stages a WORKING binding. It did\n"
    "not stage:\n  ${_pretty}\n\n"
    "This is feature 056's LAY-1 / D-4 / T006 in-tree install path. It is asserted separately from\n"
    "the wheel because `python-wheel-test` exercises ONLY the SKBUILD half — an\n"
    "`if(SKBUILD AND FIXPP_INSTALL_PYTHON)` guard would pass the wheel job AND every OFF-side\n"
    "check while silently regressing this path (Gate A round 2, R2-P2-2).\n\n"
    "--- staged tree ---\n  ${_all}")
endif()

message(STATUS "python-install-witness [present]: PASS — module + 3 .py + _fixpp_data/__init__.py + 4 XMLs staged.")
file(REMOVE_RECURSE "${_stage}")
