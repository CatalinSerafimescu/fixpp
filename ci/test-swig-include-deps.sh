#!/usr/bin/env bash
# ci/test-swig-include-deps.sh — pin that the Python binding's SWIG step reruns when a
# header fixpp.i reaches through %include changes (#600).
#
# Without SWIG's own depfile, UseSWIG's custom command depends on fixpp.i alone: an
# incremental tree recompiles the old wrapper and the module serves the old constants.
# A fresh tree (every CI build) regenerates the wrapper whatever the setting, so no
# configured lane can see this. A mock UseSWIG project stands in for an incremental
# tree, and its fixpp.i %include's a header that defines one constant.
#
# The mock does NOT restate the setting under test. Its source properties on fixpp.i
# are the REAL ones: every `set_property(SOURCE fixpp.i PROPERTY ...)` line of
# bindings/python/CMakeLists.txt, copied verbatim at run time. Deleting or disabling
# the real USE_SWIG_DEPENDENCIES line therefore reddens this harness, and a property
# moved to another spelling is not copied, which also reddens it (toward a false
# RED, never a false GREEN).
#
# Legs, each a cell asserting its own line of output:
#   GREEN  the real properties: build the wrapper, change the constant in the header,
#          rebuild the wrapper; the generated source must carry the new value.
#   RED    the same lines minus USE_SWIG_DEPENDENCIES: the rebuilt wrapper must still
#          carry the old value. It is the positive control that this observable can
#          show a stale wrapper at all.
# Mutants: copies of the real CMakeLists.txt with the line deleted, and with TRUE
# turned to FALSE; each must fail the GREEN leg as stale. A mutant whose edit matched
# nothing is refused (cmp guard), never counted as RED.
#
# Usage: ci/test-swig-include-deps.sh [--cmakelists <file>]
#   --cmakelists runs the two legs only, against the given file (e.g. a revision
#   extracted with `git show <rev>:bindings/python/CMakeLists.txt`), and skips the
#   mutants.
#
# Needs cmake, ninja, a SWIG (`swig4.0` or `swig`) and a C++ compiler, and refuses to
# run without them. The mock pins no SWIG version. Builds the generated wrapper source
# only; compiles nothing.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
REAL="$REPO/bindings/python/CMakeLists.txt"
CMAKELISTS="$REAL"
CELLS_ONLY=0
if [ "${1:-}" = "--cmakelists" ]; then
  [ -n "${2:-}" ] && [ -f "$2" ] || { echo "FAIL: --cmakelists needs an existing file"; exit 1; }
  CMAKELISTS="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; CELLS_ONLY=1
fi

fail() { printf 'FAIL: %b\n' "$1" >&2; exit 1; }
for t in cmake ninja python3; do
  command -v "$t" >/dev/null || fail "$t not found; refusing to skip"
done
command -v swig4.0 >/dev/null || command -v swig >/dev/null \
  || fail "no swig4.0 or swig on PATH; refusing to skip"
command -v "${CXX:-c++}" >/dev/null || fail "no C++ compiler (${CXX:-c++}) on PATH; refusing to skip"
[ -f "$REAL" ] || fail "not found: $REAL"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CELLS_DECLARED=4
cells_run=0
OLD=60011
NEW=60012

# props <CMakeLists.txt> — its fixpp.i source-property lines, verbatim.
props() { grep -E '^[[:space:]]*set_property\(SOURCE fixpp\.i PROPERTY ' "$1"; }

# mock <dir> <props-file> — a UseSWIG project whose fixpp.i %include's a header.
mock() {
  local d="$1"
  mkdir -p "$d/include/mock"
  cat > "$d/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(swig_include_deps_mock LANGUAGES CXX)
find_package(SWIG REQUIRED)
include(UseSWIG)
include("${MOCK_PROPS}")
swig_add_library(fixpp_py TYPE MODULE LANGUAGE python SOURCES fixpp.i)
target_include_directories(fixpp_py PRIVATE "${CMAKE_SOURCE_DIR}/include")
EOF
  printf '%%module fixpp\n%%{\n#include "mock/version.h"\n%%}\n%%include "mock/version.h"\n' \
    > "$d/fixpp.i"
  printf '#define MOCK_VALUE %s\n' "$OLD" > "$d/include/mock/version.h"
}

# leg <props-file> <name> — configure the mock, build the wrapper, change the header,
# rebuild the wrapper. Prints `regenerated` or `stale`; any other outcome is an error
# (non-zero, with the reason on stdout).
leg() {
  local pf="$1" name="$2" src="$TMP/$2/src" b="$TMP/$2/build" out wrap w
  mock "$src"
  out="$(cmake -S "$src" -B "$b" -G Ninja -DMOCK_PROPS="$pf" 2>&1)" \
    || { echo "configure failed: $out"; return 1; }
  # The wrapper's path is UseSWIG's choice and differs across CMake versions, so it
  # is read from the generated graph; exactly one relative output must name it.
  wrap="$(ninja -C "$b" -t targets all | sed -n 's/^\([^/][^:]*PYTHON_wrap\.cxx\): CUSTOM_COMMAND$/\1/p')"
  [ "$(printf '%s\n' "$wrap" | grep -c .)" -eq 1 ] \
    || { echo "expected one *PYTHON_wrap.cxx output in the graph, found: '${wrap}'"; return 1; }
  w="$b/$wrap"
  out="$(ninja -C "$b" "$wrap" 2>&1)" || { echo "first wrapper build failed: $out"; return 1; }
  grep -q "$OLD" "$w" && ! grep -q "$NEW" "$w" \
    || { echo "first build: the wrapper does not carry $OLD alone"; return 1; }
  sleep 1  # the header's mtime must be later than the wrapper's
  printf '#define MOCK_VALUE %s\n' "$NEW" > "$src/include/mock/version.h"
  out="$(ninja -C "$b" "$wrap" 2>&1)" || { echo "rebuild failed: $out"; return 1; }
  if grep -q "$NEW" "$w" && ! grep -q "$OLD" "$w"; then echo regenerated
  elif grep -q "$OLD" "$w" && ! grep -q "$NEW" "$w"; then echo stale
  else echo "the rebuilt wrapper carries neither value alone"; return 1
  fi
}

# green <CMakeLists.txt> <name> — the GREEN leg; returns 0 when it holds, else prints why.
green() {
  local pf="$TMP/$2.props.cmake" r
  props "$1" > "$pf" || { echo "no set_property(SOURCE fixpp.i PROPERTY ...) line in $1"; return 1; }
  r="$(leg "$pf" "$2")" || { echo "$r"; return 1; }
  [ "$r" = regenerated ] \
    || { echo "wrapper not regenerated: it kept MOCK_VALUE $OLD after the %include'd header changed. Properties copied from $1:"; cat "$pf"; return 1; }
}

# ── GREEN leg ────────────────────────────────────────────────────────────────
why="$(green "$CMAKELISTS" green)" || fail "GREEN leg did not hold: $why"
echo "GREEN (expected): wrapper regenerated with the properties of $CMAKELISTS"
cells_run=$((cells_run + 1))

# ── RED leg: the same properties without USE_SWIG_DEPENDENCIES ──────────────
pf="$TMP/red.props.cmake"
props "$CMAKELISTS" | grep -v USE_SWIG_DEPENDENCIES > "$pf"
r="$(leg "$pf" red)" || fail "RED leg errored: $r"
[ "$r" = stale ] || fail "RED leg: without USE_SWIG_DEPENDENCIES the wrapper read '$r', not stale, so this harness cannot see the defect it pins"
echo "RED (expected): without USE_SWIG_DEPENDENCIES the wrapper kept MOCK_VALUE $OLD"
cells_run=$((cells_run + 1))

if [ "$CELLS_ONLY" -eq 1 ]; then
  echo "PASS (cells only, against $CMAKELISTS): $cells_run cells"
  exit 0
fi

# ── Mutants of the REAL file ─────────────────────────────────────────────────
# mutant <id> <python old> <python new>
mutant() {
  local id="$1" old="$2" new="$3" dst="$TMP/CMakeLists.$1.txt" why rc=0
  python3 - "$REAL" "$dst" "$old" "$new" <<'PY' || fail "mutant $id: anchor did not apply"
import sys
src, dst, old, new = sys.argv[1:5]
s = open(src).read()
if s.count(old) != 1:
    sys.exit(f"mutation anchor occurs {s.count(old)} times, wanted 1: {old!r}")
open(dst, "w").write(s.replace(old, new, 1))
PY
  cmp -s "$REAL" "$dst" && fail "mutant $id did not change the file — a no-op mutant proves nothing"
  why="$(green "$dst" "mut-$id")" || rc=$?
  [ "$rc" -ne 0 ] || fail "mutant $id SURVIVED the GREEN leg"
  grep -q "wrapper not regenerated" <<<"$why" || fail "mutant $id failed the GREEN leg for the wrong reason: $why"
  printf 'RED (expected): GREEN leg under mutant %s — %.200s\n' "$id" "$why"
  cells_run=$((cells_run + 1))
}

mutant M-DEPS-DELETED 'set_property(SOURCE fixpp.i PROPERTY USE_SWIG_DEPENDENCIES TRUE)
' ''
mutant M-DEPS-FALSE 'PROPERTY USE_SWIG_DEPENDENCIES TRUE)' 'PROPERTY USE_SWIG_DEPENDENCIES FALSE)'

[ "$cells_run" -eq "$CELLS_DECLARED" ] || fail "declared $CELLS_DECLARED cells but ran $cells_run"
echo
echo "PASS: SWIG %include dependency pin — $cells_run/$CELLS_DECLARED cells, every mutant reddened the GREEN leg."
