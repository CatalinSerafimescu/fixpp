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

# The whole staging root. `LIST_DIRECTORIES true` makes GLOB_RECURSE emit
# DIRECTORY entries as well as files, so `_fixpp_data` is caught as a directory
# entry in its own right. (An earlier version of this comment said it was caught
# "via the files under it" — that is backwards, and it matters: the four bundled
# XMLs do NOT match the payload prefix, so the directory entry is the only thing
# that catches them. Gate B round 1, F6(c).)
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

# ── F6(a) — extensions, not just names ───────────────────────────────────────
# Gate B round 1 measured the gap: the name list above is a CURRENT-basename
# denylist, so a later `fixpp_helpers.py`, a `.pyi` stub, a stray `.pyc`, or an
# SOABI-tagged module renamed off the `_fixpp` prefix all escape it. Rejecting by
# EXTENSION follows provenance across renames and new files.
#
# ⚠️ Validated safe, not assumed: the real OFF-side install stages 258 entries
# with ZERO `.py` / `.pyi` / `.pyc` among them. If a future C++ install rule
# legitimately ships a Python file (a CMake helper script, say), THIS is the line
# that will red — and the right response is to narrow it deliberately, not to
# delete it.
set(_payload_suffix_res
  "\\.pyi?$"                        # .py and .pyi
  "\\.pyc$"
  "\\.cpython-[^/]*\\.(so|pyd)$")   # SOABI-tagged extension modules

set(_hits "")
foreach(_p IN LISTS _staged)
  get_filename_component(_name "${_p}" NAME)
  set(_is_payload FALSE)
  if(_name IN_LIST _payload_names OR _name MATCHES "${_payload_prefix}")
    set(_is_payload TRUE)
  else()
    foreach(_re IN LISTS _payload_suffix_res)
      if(_name MATCHES "${_re}")
        set(_is_payload TRUE)
        break()
      endif()
    endforeach()
  endif()
  if(_is_payload)
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

# ── The module is the ANCHOR, and it is found FIRST ──────────────────────────
#
# Gate B round 2, finding 3. The requirement loop used to search the whole staged
# tree for each required BASENAME (with, at most, a check on its immediate parent
# directory's name). Three broken layouts were MEASURED passing:
#
#   W1  fixpp_oo.py + fixpp_dict_data.py installed to share/unrelated/ — accepted,
#       because "somewhere in the tree" was the whole requirement. `fixpp.py`
#       imports `fixpp_oo`, so that tree is not importable.
#   W2  fixpp.py staged as a DIRECTORY — accepted, because nothing tested type.
#   W3  the module renamed `_fixpp_broken.so` — accepted by `^_fixpp.*\.(so|pyd)$`.
#
# All three certify a non-importable install under a message that says the witness
# "stages a WORKING binding". The fix is to stop asking "does this name exist
# anywhere" and ask "is it BESIDE the module", which is what importability
# actually requires — and it is exactly what the four install() rules in
# bindings/python/CMakeLists.txt promise: all of them share one
# `${FIXPP_PY_INSTALL_DIR}`, with `_fixpp_data` as its immediate child.
#
# ⚠️ Discover the module BEFORE the requirement loop and FAIL CLOSED if it is
# absent. With no module there is no anchor, and an empty anchor would silently
# revert every requirement to "anywhere in the tree" — i.e. re-open W1 on exactly
# the trees where the install is most broken.
#
# ⚠️ Regex-escaping the required names would NOT have been enough, and neither is
# a component tail (the round-1 F4 fix, now superseded): a `$`-anchored suffix
# match still matches ANYWHERE in the tree, and the four bundled XMLs exist in the
# C++ install too. Anchoring at a discovered absolute directory is what makes
# "`FIX42.xml` inside THIS `_fixpp_data`" expressible at all. It also keeps the
# DESTDIR-prefix independence the tail comparison was chosen for: the prefix is
# whatever the discovered module's directory happens to be, never hard-coded.
#
# ⚠️ The module pattern is NARROWER here than `_payload_prefix` above, and the
# asymmetry is deliberate — do not "reconcile" them. `absent` asks "did ANY
# python-shaped thing leak", so it wants the broad `^_fixpp` (a sibling
# `_fixpp_something` is payload too, and C2 depends on that breadth). `present`
# asks "is THE importable module here", and `_fixpp_broken.so` is not it. The two
# names that must pass are `_fixpp.so` and the SOABI-tagged
# `_fixpp.cpython-<tag>.so`.
#
# ⚠️ The triage prescribed `^_fixpp([._].*)?\.(so|pyd)$` for this. It does not
# work: `_` is inside the class, so `_fixpp_broken.so` decomposes as `^_fixpp` +
# `_broken` + `\.so$` and still matches. Measured with `cmake -P` before adopting
# the form below. W3 in ci/test-python-install-witness.sh is the standing proof.
set(_module_re "^_fixpp(\\.cpython-[^/]*)?\\.(so|pyd)$")
set(_module_path "")
foreach(_p IN LISTS _staged)
  get_filename_component(_name "${_p}" NAME)
  if(_name MATCHES "${_module_re}" AND NOT IS_DIRECTORY "${_p}" AND NOT IS_SYMLINK "${_p}")
    set(_module_path "${_p}")
    break()
  endif()
endforeach()

set(_missing "")
if(_module_path STREQUAL "")
  # Fail closed: no anchor, so no co-location claim can be made at all.
  list(APPEND _missing "_fixpp.so / _fixpp.cpython-<tag>.so (the extension module)")
else()
  get_filename_component(_module_dir "${_module_path}" DIRECTORY)
  foreach(_r IN LISTS _required)
    set(_want "${_module_dir}/${_r}")
    # ⚠️ A REGULAR FILE — not a directory, not a symlink. Both exclusions are
    # measured, not defensive:
    #   W2  `fixpp.py` staged as a DIRECTORY full of other files -> was certified.
    #   W6  `fixpp.py` staged as a SYMLINK to `fixpp_oo.py` (Gate B round 3, Codex
    #       finding 5) -> was certified. `install(FILES)` preserves symlinks, so
    #       that install has no SWIG wrapper at `fixpp.py` at all.
    # Rejecting symlinks outright is stricter than resolving them, and deliberately
    # so: the four install() rules promise installed artifacts, not a symlink
    # layout, and validating a link target's identity is a bigger instrument than
    # the contract needs.
    if(NOT EXISTS "${_want}" OR IS_DIRECTORY "${_want}" OR IS_SYMLINK "${_want}")
      list(APPEND _missing "${_r} (expected at ${_want})")
    endif()
  endforeach()
endif()

if(_missing)
  string(REPLACE ";" "\n  " _pretty "${_missing}")
  string(REPLACE ";" "\n  " _all "${_staged}")
  message(FATAL_ERROR
    "python-install-witness [present]: this mode asserts that with FIXPP_INSTALL_PYTHON at its\n"
    "default ON and SKBUILD undefined, a plain `cmake --install` stages a WORKING binding.\n"
    "Each requirement must be a REGULAR FILE at the path shown — beside the extension module,\n"
    "because that is what makes the payload importable and what the four install() rules in\n"
    "bindings/python/CMakeLists.txt promise (one shared FIXPP_PY_INSTALL_DIR). Not satisfied:\n"
    "  ${_pretty}\n\n"
    "This is feature 056's LAY-1 / D-4 / T006 in-tree install path. It is asserted separately from\n"
    "the wheel because `python-wheel-test` exercises ONLY the SKBUILD half — an\n"
    "`if(SKBUILD AND FIXPP_INSTALL_PYTHON)` guard would pass the wheel job AND every OFF-side\n"
    "check while silently regressing this path (Gate A round 2, R2-P2-2).\n\n"
    "--- staged tree ---\n  ${_all}")
endif()

message(STATUS "python-install-witness [present]: PASS — module at ${_module_path}, with 3 .py + "
               "_fixpp_data/{__init__.py,4 XMLs} co-located beside it.")
file(REMOVE_RECURSE "${_stage}")
