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
# REAL witness over it. No compiler, no SWIG, no network — cmake and coreutils,
# plus python3 for the #257 package cells (the witness itself fails closed
# without it rather than skipping, so a missing interpreter cannot read green). Nothing is faked inside the witness:
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

# ⚠️ CMake RE-WRAPS message(FATAL_ERROR) text at its own width, so a needle of
# more than a few words is split across lines and a plain `grep -F` misses it —
# which reads as "RED, but not for its own reason" and looks exactly like a
# broken instrument. Match against a whitespace-NORMALISED copy instead, so a
# needle is matched on words rather than on CMake's chosen line breaks.
# (Normalisation only ever makes a match MORE likely, so it cannot turn a
# genuinely-absent positive needle into a pass; the negative-needle cells below
# are the ones that would notice if it over-matched, and they do run.)
grep_norm() {  # <needle> <file>
  tr '\n' ' ' < "$2" | tr -s ' ' | grep -qF -- "$1"
}

command -v cmake >/dev/null || fail "cmake is required"
[ -f "$WITNESS" ] || fail "witness script not found: $WITNESS"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CELLS_DECLARED=13  # good soabi w1 w2 w3 w5 w6 absent-clean absent-stray
                   # + package-{good,xml-leak,bad-relpath,wheel-marker}  (#257)
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
  # The w6 fixture installs THIS path, so the staged `fixpp.py` is a symlink.
  # ⚠️ The link must RESOLVE once staged, or the cell is vacuous: a dangling link
  # fails the plain `EXISTS` test that was already there, and the cell would go
  # RED against the very witness it is supposed to prove was blind. Hence a
  # SAME-DIRECTORY target (`fixpp_oo.py`, which the fixture also installs into the
  # same destination) rather than a path out of the staging tree — measured, the
  # first draft used `../fixpp_oo.py` and proved nothing.
  mkdir -p "$src/link"
  cp "$src/fixpp_oo.py" "$src/link/fixpp_oo.py"
  ln -sf fixpp_oo.py "$src/link/fixpp.py"
  echo "stub" > "$src/cpp/libfixpp.a"
  echo "// stub" > "$src/cpp/fixpp.hpp"

  # ── #257 package-layout markers ────────────────────────────────────────────
  # The generated _fixpp_data/__init__.py, in a correct and a broken variant.
  # PYDIR is lib/python and the dictionaries stage to share/fixpp/dictionaries,
  # so the correct hop out of lib/python/_fixpp_data is ../../../share/...
  mkdir -p "$src/pkgmarker_good" "$src/pkgmarker_bad"
  cat > "$src/pkgmarker_good/__init__.py" <<'MARKER'
import os
DICTIONARY_DIR = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "../../../share/fixpp/dictionaries"))
MARKER
  # ⚠️ THE ARM THAT PRICES THE ROUND-TRIP. Everything is staged correctly except
  # the relative hop, which is the exact failure a changed CMAKE_INSTALL_LIBDIR
  # or a moved payload destination produces. A file-existence check cannot see
  # it — __init__.py is present and importable — so if this cell ever goes GREEN
  # the locator probe has stopped doing anything.
  cat > "$src/pkgmarker_bad/__init__.py" <<'MARKER'
import os
DICTIONARY_DIR = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "../../../share/fixpp/WRONG"))
MARKER
  # The REAL locator, so the probe exercises shipped code rather than a stand-in.
  cp "$repo_root/bindings/python/fixpp_dict_data.py" "$src/real_fixpp_dict_data.py"

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
      w6)
        # `fixpp.py` staged as a SYMLINK to `fixpp_oo.py`. install(FILES)
        # preserves symlinks, so the install has no SWIG wrapper at all — and
        # the pre-fix witness certified it (Gate B round 3, Codex finding 5).
        echo "install(FILES \"\${S}/$module\" DESTINATION \${PYDIR})"
        echo 'install(FILES "${S}/fixpp_oo.py" "${S}/fixpp_dict_data.py" DESTINATION ${PYDIR})'
        echo 'install(FILES "${S}/link/fixpp.py" DESTINATION ${PYDIR})'
        echo 'install(DIRECTORY "${S}/_fixpp_data/" DESTINATION ${PYDIR}/_fixpp_data)'
        ;;
      good|soabi|w3)
        echo "install(FILES \"\${S}/$module\" DESTINATION \${PYDIR})"
        echo 'install(FILES "${S}/fixpp.py" "${S}/fixpp_oo.py" "${S}/fixpp_dict_data.py" DESTINATION ${PYDIR})'
        echo 'install(DIRECTORY "${S}/_fixpp_data/" DESTINATION ${PYDIR}/_fixpp_data)'
        ;;
      # ── #257 package layout: the payload WITHOUT the four bundled XMLs ──────
      # fixpp_dict_data.py is the REAL locator here (not the stub the present-
      # mode layouts use), because these cells drive it for real.
      package-good|package-bad-relpath|package-wheel-marker|package-xml-leak)
        echo "install(FILES \"\${S}/$module\" DESTINATION \${PYDIR})"
        echo 'install(FILES "${S}/fixpp.py" "${S}/fixpp_oo.py" DESTINATION ${PYDIR})'
        echo 'install(FILES "${S}/real_fixpp_dict_data.py" DESTINATION ${PYDIR} RENAME fixpp_dict_data.py)'
        case "$layout" in
          package-good|package-xml-leak)
            echo 'install(FILES "${S}/pkgmarker_good/__init__.py" DESTINATION ${PYDIR}/_fixpp_data)' ;;
          package-bad-relpath)
            echo 'install(FILES "${S}/pkgmarker_bad/__init__.py" DESTINATION ${PYDIR}/_fixpp_data)' ;;
          package-wheel-marker)
            # The WHEEL marker (no DICTIONARY_DIR) shipped into a package tree:
            # the exclusion took effect but the generated twin did not, so the
            # locator has nothing to fall back to.
            echo 'install(FILES "${S}/_fixpp_data/__init__.py" DESTINATION ${PYDIR}/_fixpp_data)' ;;
        esac
        if [ "$layout" = "package-xml-leak" ]; then
          # The exclusion silently stopped working: 1.9 MB of the same four
          # files, twice in one archive.
          echo 'install(FILES "${S}/_fixpp_data/FIX42.xml" "${S}/_fixpp_data/FIX44.xml" "${S}/_fixpp_data/FIX50SP2.xml" "${S}/_fixpp_data/FIXT11.xml" DESTINATION ${PYDIR}/_fixpp_data)'
        fi
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
  grep_norm "$needle" "$out" \
    || { cat "$out" >&2; fail "cell $name [$mode/$layout]: exited 0 without printing '$needle'"; }
  cells_run=$((cells_run + 1))
  echo "  ok  $name [$mode/$layout] — PASS as expected"
}

# A RED cell: nonzero exit, the failure text must name THIS cell's defect, and —
# where a 5th argument is given — must NOT name something the layout staged
# correctly.
#
# ⚠️ The negative needle is what makes "for its own reason" mean anything. Each
# broken layout keeps everything except one thing; a witness that reported the
# whole payload missing would satisfy the positive needle just as well, and the
# cell would read green while the instrument was actually broken. That failure
# shape — an instrument's breakage reading as a finding — has bitten this repo
# before (feedback_silent_empty_recurred_three_times…).
cell_red() {
  local name="$1" mode="$2" layout="$3" needle="$4" not_needle="${5:-}"
  local bld out rc=0
  bld="$(make_fixture "$name" "$layout")" || exit 1
  out="$TMP/$name/witness.log"
  run_witness "$mode" "$bld" "$out" || rc=$?
  [ "$rc" != "0" ] \
    || { cat "$out" >&2; fail "cell $name [$mode/$layout]: expected RED, the witness PASSED — this layout is certified as a working install"; }
  grep_norm "$needle" "$out" \
    || { cat "$out" >&2; fail "cell $name [$mode/$layout]: RED, but not for its own reason — '$needle' is absent from the failure"; }
  if [ -n "$not_needle" ] && grep_norm "$not_needle" "$out"; then
    cat "$out" >&2
    fail "cell $name [$mode/$layout]: RED, but OVER-BROAD — it also reports '$not_needle', which this layout stages CORRECTLY. The witness is failing for more than the seeded defect, so this cell is not evidence that it detects that defect."
  fi
  cells_run=$((cells_run + 1))
  echo "  ok  $name [$mode/$layout] — RED for its own reason, and only that reason"
}

echo "ci/test-python-install-witness.sh — driving $WITNESS"

# ── present: the two layouts that MUST pass ──────────────────────────────────
# `soabi` is not decoration. The module pattern was tightened to reject W3, and
# the tag-carrying name is the one real form that tightening could have broken;
# without this cell the fix could ship as a false RED on every SOABI build.
cell_green good  present good  "python-install-witness [present]: PASS"
cell_green soabi present soabi "_fixpp.cpython-312-x86_64-linux-gnu.so"

# ── present: the three measured escapes, plus the data package ───────────────
cell_red w1 present w1 "fixpp_oo.py (expected at"           "_fixpp_data/FIX42.xml (expected at"
cell_red w2 present w2 "fixpp.py (expected at"              "fixpp_oo.py (expected at"
cell_red w3 present w3 "the extension module"               "fixpp_oo.py (expected at"
cell_red w5 present w5 "_fixpp_data/__init__.py (expected at" "fixpp_oo.py (expected at"
cell_red w6 present w6 "fixpp.py (expected at"              "fixpp_oo.py (expected at"

# ── absent: the L-056-4 side ─────────────────────────────────────────────────
# The clean cell also proves the four XMLs under share/fixpp/dictionaries are NOT
# counted as payload; the stray cell is Gate B round 1's F6 counter-test (a
# Python file on no basename denylist), kept driveable.
cell_green absent-clean absent absent-clean "PASS — 0 payload entries"
cell_red   absent-stray absent absent-stray "fixpp_helpers.py"

# ── #257 package mode: the payload the -release artifacts actually ship ──────
#
# ⚠️ package-good is the ONLY place in this repo that executes the datadir
# fallback in fixpp_dict_data._resource(). The wheel cannot reach that branch by
# construction (it bundles the XMLs, so the first branch always returns), so
# without this cell the branch ships untested.
cell_green package-good package package-good \
  "4 XMLs correctly ABSENT"

# The three ways the package layout breaks, each staged correctly in every
# respect but one.
cell_red package-xml-leak package package-xml-leak \
  "must NOT duplicate the bundled dictionaries" "resolve the bundled dictionaries"
cell_red package-bad-relpath package package-bad-relpath \
  "could NOT resolve the bundled dictionaries" "must NOT duplicate"
cell_red package-wheel-marker package package-wheel-marker \
  "could NOT resolve the bundled dictionaries" "must NOT duplicate"

[ "$cells_run" = "$CELLS_DECLARED" ] \
  || fail "declared $CELLS_DECLARED cells, ran $cells_run"

echo "PASS: ci/test-python-install-witness.sh — $cells_run/$CELLS_DECLARED cells, 5 present-mode escapes, 1 absent-mode leak and 3 package-mode defects proven RED for their own reason"
