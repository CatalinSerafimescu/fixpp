#!/usr/bin/env bash
# Regression pin for bindings/python/run_python_install_witness.cmake.
#
# WHY THIS EXISTS. The witness is the content gate for FIXPP_INSTALL_PYTHON: the
# `absent` cell guards L-056-4 (no Python in the C++ consumer deliverable) and
# the `present` cell guards feature 056's LAY-1 in-tree install. Both are
# registered as ctest cells by bindings/python/CMakeLists.txt, and exactly one
# registers per build — which is a good design for the FAITHFUL path and no
# instrument at all for the BROKEN ones. A ctest cell can only ever show you the
# layout your own tree happens to produce; it cannot show you which WRONG layouts
# the witness would have certified.
#
# It had certified three. Gate B round 2 measured them on the real script:
#
#   W1  fixpp_oo.py + fixpp_dict_data.py installed to an unrelated directory
#       -> PASS. `fixpp.py` does `import fixpp_oo`; that tree cannot be imported.
#   W2  fixpp.py staged as a DIRECTORY, no regular file anywhere -> PASS.
#   W3  the extension module renamed `_fixpp_broken.so` -> PASS.
#
# Each was accepted under a message reading "stages a WORKING binding". The fix
# anchors every requirement at the discovered module's directory; this harness is
# what proves the fix, and what stops R2-P2-2 reaching a fourth Gate B round on a
# hand-run demonstration nobody can re-drive.
#
# HOW. Each cell builds a throwaway `project(NONE)` fixture whose install() rules
# stage a chosen layout out of `file(TOUCH)`-style stub files, then drives the
# REAL witness over it. No compiler, no SWIG, no Python, no network — cmake and
# coreutils only, ~1 s for all eight cells. Nothing is faked inside the witness:
# it runs its own `cmake -E env DESTDIR=... cmake --install`, globs its own
# staging root and reaches its own verdict, exactly as it does under ctest.
#
# ⚠️ RED CELLS ARE CHECKED FOR THEIR OWN REASON, not merely for a nonzero exit.
# A witness that died on a CMake syntax error would otherwise "prove" all six
# negatives at once (feedback_verification_grep_must_be_proven_nonzero_on_the_
# unfixed_tree; feedback_sanitizer_canary_must_be_proven_red).
#
# ⚠️ The declared cell count is asserted against the number that actually ran,
# for the reason recorded in ci/test-tier1-python-policy.sh: a summary line
# claiming N checks where N-1 ran is not something to leave to an eyeball.
#
# Usage: ci/test-python-install-witness.sh [path-to-run_python_install_witness.cmake]
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WITNESS="${1:-$repo_root/bindings/python/run_python_install_witness.cmake}"

fail() { echo "FAIL: $1" >&2; exit 1; }

command -v cmake >/dev/null || fail "cmake is required"
[ -f "$WITNESS" ] || fail "witness script not found: $WITNESS"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CELLS_DECLARED=8   # good soabi w1 w2 w3 w5 absent-clean absent-stray
cells_run=0

# ── fixture ──────────────────────────────────────────────────────────────────
# One source tree per cell. $2 selects the layout; every layout also installs a
# little C++-shaped noise, so `absent` is exercised against a tree that is not
# trivially empty and so the four bundled XMLs appear OUTSIDE `_fixpp_data` too
# (the witness must not count those as payload — that is why it matches the
# `_fixpp_data` DIRECTORY rather than the XML basenames).
make_fixture() {
  local name="$1" layout="$2"
  local src="$TMP/$name/src"
  local bld="$TMP/$name/build"
  mkdir -p "$src/_fixpp_data" "$src/dirpayload" "$src/cpp" "$src/dicts"

  local f
  for f in fixpp.py fixpp_oo.py fixpp_dict_data.py; do
    echo "# stub $f" > "$src/$f"
  done
  echo "# stub" > "$src/_fixpp_data/__init__.py"
  for f in FIX42 FIX44 FIX50SP2 FIXT11; do
    echo "<fix/>" > "$src/_fixpp_data/$f.xml"
    echo "<fix/>" > "$src/dicts/$f.xml"
  done
  echo "stub" > "$src/dirpayload/marker.txt"
  echo "stub" > "$src/cpp/libfixpp.a"
  echo "// stub" > "$src/cpp/fixpp.hpp"

  # The module's staged basename is the only thing that varies between the
  # module cells, so it is a variable rather than three near-identical layouts.
  local module="_fixpp.so"
  case "$layout" in
    soabi) module="_fixpp.cpython-312-x86_64-linux-gnu.so" ;;
    w3)    module="_fixpp_broken.so" ;;
  esac
  echo "ELF stub" > "$src/$module"

  {
    echo 'cmake_minimum_required(VERSION 3.24)'
    echo 'project(fixpp_witness_fixture NONE)'
    echo 'set(S "${CMAKE_CURRENT_SOURCE_DIR}")'
    echo 'set(PYDIR lib/python)'
    # C++-shaped noise, present in every layout including the absent ones.
    echo 'install(FILES "${S}/cpp/libfixpp.a" DESTINATION lib)'
    echo 'install(FILES "${S}/cpp/fixpp.hpp" DESTINATION include/fixpp)'
    echo 'install(DIRECTORY "${S}/dicts/" DESTINATION share/fixpp/dictionaries)'

    case "$layout" in
      absent-clean)
        : # C++ only — nothing else staged.
        ;;
      absent-stray)
        # C2's shape: a Python file the extension-based rejection must catch even
        # though its basename is on no denylist.
        echo 'install(FILES "${S}/fixpp_oo.py" DESTINATION lib RENAME fixpp_helpers.py)'
        ;;
      w1)
        # The two imported modules moved out of the package directory — one
        # ordinary DESTINATION edit away from the real rules.
        echo "install(FILES \"\${S}/$module\" DESTINATION \${PYDIR})"
        echo 'install(FILES "${S}/fixpp.py" DESTINATION ${PYDIR})'
        echo 'install(FILES "${S}/fixpp_oo.py" "${S}/fixpp_dict_data.py" DESTINATION share/unrelated)'
        echo 'install(DIRECTORY "${S}/_fixpp_data/" DESTINATION ${PYDIR}/_fixpp_data)'
        ;;
      w2)
        # fixpp.py staged as a DIRECTORY; no regular file of that name anywhere.
        echo "install(FILES \"\${S}/$module\" DESTINATION \${PYDIR})"
        echo 'install(DIRECTORY "${S}/dirpayload/" DESTINATION ${PYDIR}/fixpp.py)'
        echo 'install(FILES "${S}/fixpp_oo.py" "${S}/fixpp_dict_data.py" DESTINATION ${PYDIR})'
        echo 'install(DIRECTORY "${S}/_fixpp_data/" DESTINATION ${PYDIR}/_fixpp_data)'
        ;;
      w5)
        # The data package installed outside the module's directory.
        echo "install(FILES \"\${S}/$module\" DESTINATION \${PYDIR})"
        echo 'install(FILES "${S}/fixpp.py" "${S}/fixpp_oo.py" "${S}/fixpp_dict_data.py" DESTINATION ${PYDIR})'
        echo 'install(DIRECTORY "${S}/_fixpp_data/" DESTINATION share/unrelated/_fixpp_data)'
        ;;
      good|soabi|w3)
        echo "install(FILES \"\${S}/$module\" DESTINATION \${PYDIR})"
        echo 'install(FILES "${S}/fixpp.py" "${S}/fixpp_oo.py" "${S}/fixpp_dict_data.py" DESTINATION ${PYDIR})'
        echo 'install(DIRECTORY "${S}/_fixpp_data/" DESTINATION ${PYDIR}/_fixpp_data)'
        ;;
      *) fail "make_fixture: unknown layout '$layout'" ;;
    esac
  } > "$src/CMakeLists.txt"

  cmake -S "$src" -B "$bld" > "$TMP/$name/configure.log" 2>&1 \
    || { cat "$TMP/$name/configure.log" >&2; fail "fixture '$name' failed to configure"; }
  echo "$bld"
}

# ── driver ───────────────────────────────────────────────────────────────────
run_witness() {
  local mode="$1" bld="$2" out="$3"
  local rc=0
  cmake -DFIXPP_MAIN_BUILD_DIR="$bld" \
        -DFIXPP_PY_WITNESS_MODE="$mode" \
        -DFIXPP_PY_WITNESS_WORK_DIR="$TMP/stage" \
        -P "$WITNESS" > "$out" 2>&1 || rc=$?
  return "$rc"
}

# A GREEN cell: the witness must exit 0 AND print its own PASS line. Exit 0 with
# no verdict would mean the script returned early somewhere.
cell_green() {
  local name="$1" mode="$2" layout="$3" needle="$4"
  local bld out
  bld="$(make_fixture "$name" "$layout")" || exit 1
  out="$TMP/$name/witness.log"
  run_witness "$mode" "$bld" "$out" \
    || { cat "$out" >&2; fail "cell $name [$mode/$layout]: expected PASS, the witness FAILED"; }
  grep -qF -- "$needle" "$out" \
    || { cat "$out" >&2; fail "cell $name [$mode/$layout]: exited 0 without printing '$needle'"; }
  cells_run=$((cells_run + 1))
  echo "  ok  $name [$mode/$layout] — PASS as expected"
}

# A RED cell: nonzero exit AND the failure text must name THIS cell's defect.
cell_red() {
  local name="$1" mode="$2" layout="$3" needle="$4"
  local bld out rc=0
  bld="$(make_fixture "$name" "$layout")" || exit 1
  out="$TMP/$name/witness.log"
  run_witness "$mode" "$bld" "$out" || rc=$?
  [ "$rc" != "0" ] \
    || { cat "$out" >&2; fail "cell $name [$mode/$layout]: expected RED, the witness PASSED — this layout is certified as a working install"; }
  grep -qF -- "$needle" "$out" \
    || { cat "$out" >&2; fail "cell $name [$mode/$layout]: RED, but not for its own reason — '$needle' is absent from the failure"; }
  cells_run=$((cells_run + 1))
  echo "  ok  $name [$mode/$layout] — RED for its own reason"
}

echo "ci/test-python-install-witness.sh — driving $WITNESS"

# ── present: the two layouts that MUST pass ──────────────────────────────────
# `soabi` is not decoration. The module pattern was tightened to reject W3, and
# the tag-carrying name is the one real form that tightening could have broken;
# without this cell the fix could ship as a false RED on every SOABI build.
cell_green good  present good  "python-install-witness [present]: PASS"
cell_green soabi present soabi "_fixpp.cpython-312-x86_64-linux-gnu.so"

# ── present: the three measured escapes, plus the data package ───────────────
cell_red w1 present w1 "fixpp_oo.py (expected at"
cell_red w2 present w2 "fixpp.py (expected at"
cell_red w3 present w3 "the extension module"
cell_red w5 present w5 "_fixpp_data/__init__.py (expected at"

# ── absent: the L-056-4 side ─────────────────────────────────────────────────
# The clean cell also proves the four XMLs under share/fixpp/dictionaries are NOT
# counted as payload; the stray cell is Gate B round 1's F6 counter-test (a
# Python file on no basename denylist), kept driveable.
cell_green absent-clean absent absent-clean "PASS — 0 payload entries"
cell_red   absent-stray absent absent-stray "fixpp_helpers.py"

[ "$cells_run" = "$CELLS_DECLARED" ] \
  || fail "declared $CELLS_DECLARED cells, ran $cells_run"

echo "PASS: ci/test-python-install-witness.sh — $cells_run/$CELLS_DECLARED cells, 4 present-mode escapes and 1 absent-mode leak proven RED for their own reason"
