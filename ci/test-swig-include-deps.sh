#!/usr/bin/env bash
# ci/test-swig-include-deps.sh — pin that the Python binding's SWIG step reruns when a
# header fixpp.i reaches through %include changes (#600).
#
# Without SWIG's own depfile, UseSWIG's custom command depends on fixpp.i alone: an
# incremental tree recompiles the old wrapper and the module serves the old constants.
# A fresh tree regenerates the wrapper whatever the setting, so a lane that configures
# from scratch cannot see this. A mock UseSWIG project stands in for an incremental
# tree, and its fixpp.i %include's a header that defines one constant.
#
# The mock does NOT restate the setting under test. It includes a verbatim SLICE of
# bindings/python/CMakeLists.txt, taken at run time: from the first line that starts
# `set_property(SOURCE fixpp.i` through the line before the file's one
# `swig_add_library(` call, then makes that call itself. The statements production
# runs before the call thus run here in production's order. The slice must be
# straight-line code at control-flow depth zero, and the harness refuses otherwise,
# so its statements run unconditionally in the mock as they do wherever production
# reaches the call. Variables are not carried over: a statement reading a variable
# set above the slice or by the parent project expands empty in the mock. The harness
# also refuses (no leg runs) when `swig_add_library(` does not start exactly one line,
# or when no fixpp.i set_property line precedes it.
#
# Legs, each a cell asserting its own line of output:
#   GREEN  the real slice: build the wrapper, change the constant in the header,
#          rebuild the wrapper; the generated source must carry the new value.
#   RED    the same slice minus its one USE_SWIG_DEPENDENCIES line: the rebuilt
#          wrapper must still carry the old value. It is the positive control that
#          this observable can show a stale wrapper at all.
# Mutants: copies of the real CMakeLists.txt with the line deleted, with TRUE turned
# to FALSE, or with the line moved below the swig_add_library call; each must fail
# the GREEN leg as stale. Refusal cells place control flow inside or around the
# slice, duplicate the call, or remove its properties, and require their own
# refusal message. A mutation whose edit matched nothing is refused by the cmp guard.
#
# Usage: ci/test-swig-include-deps.sh [--cmakelists <file>]
#   --cmakelists runs the two legs only, against the given file (e.g. a revision
#   extracted with `git show <rev>:bindings/python/CMakeLists.txt`), and skips the
#   mutants and the refusal cells.
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

CELLS_DECLARED=10
cells_run=0
OLD=60011
NEW=60012

# slice <CMakeLists.txt> [--drop-deps] — the lines from the first fixpp.i
# set_property through the line before the one swig_add_library( call, verbatim.
# --drop-deps also removes the one USE_SWIG_DEPENDENCIES TRUE line (the RED leg).
# Exits 1 with the reason on stderr when the file does not have that shape.
slice() {
  python3 - "$@" <<'PY'
import re, sys
path, drop = sys.argv[1], "--drop-deps" in sys.argv[2:]
lines = open(path).read().splitlines(keepends=True)
command = lambda pattern, line: (
    not re.match(r"\s*#", line) and re.match(pattern, line, re.IGNORECASE)
)
calls = [i for i, l in enumerate(lines) if command(r"\s*swig_add_library\(", l)]
if len(calls) != 1:
    sys.exit(f"refused: swig_add_library( starts {len(calls)} line(s) of {path}, wanted exactly 1")
props = [i for i in range(calls[0])
         if command(r"\s*set_property\(SOURCE fixpp\.i\b", lines[i])]
if not props:
    sys.exit(f"refused: no set_property(SOURCE fixpp.i line precedes swig_add_library( in {path}")
depth = 0
for line in lines[:props[0]]:
    if command(r"\s*(if|foreach|while|function|macro|block)\s*\(", line):
        depth += 1
    elif command(r"\s*end(if|foreach|while|function|macro|block)\s*\(", line):
        depth -= 1
if depth != 0:
    sys.exit(f"refused: the slice of {path} starts inside a control-flow scope opened above it")
body = lines[props[0]:calls[0]]
for n, line in enumerate(body, props[0] + 1):
    match = command(
        r"\s*(if|elseif|else|endif|foreach|endforeach|while|endwhile|function|endfunction|"
        r"macro|endmacro|block|endblock|return|break|continue)\s*\(", line)
    if match:
        sys.exit(f"refused: the slice of {path} holds control flow "
                 f"(line {n}: {match.group(1)}); it must be straight-line")
if drop:
    deps = [i for i, l in enumerate(body)
            if command(
                r"\s*set_property\(SOURCE fixpp\.i PROPERTY USE_SWIG_DEPENDENCIES TRUE\)\s*$", l)]
    if len(deps) != 1:
        sys.exit(f"refused: the slice of {path} has {len(deps)} USE_SWIG_DEPENDENCIES TRUE line(s), wanted exactly 1")
    del body[deps[0]]
sys.stdout.write("".join(body))
PY
}

# mock <dir> — a UseSWIG project whose fixpp.i %include's a header. It includes
# the slice named by -DMOCK_PROPS just before its own swig_add_library call.
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

# leg <slice-file> <name> — configure the mock, build the wrapper, change the header,
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
  local pf="$TMP/$2.slice.cmake" r
  r="$(slice "$1" 2>&1 >"$pf")" || { echo "$r"; return 1; }
  r="$(leg "$pf" "$2")" || { echo "$r"; return 1; }
  [ "$r" = regenerated ] \
    || { echo "wrapper not regenerated: it kept MOCK_VALUE $OLD after the %include'd header changed. Slice taken from $1:"; cat "$pf"; return 1; }
}

# ── GREEN leg ────────────────────────────────────────────────────────────────
why="$(green "$CMAKELISTS" green)" || fail "GREEN leg did not hold: $why"
echo "GREEN (expected): wrapper regenerated with the slice of $CMAKELISTS"
cells_run=$((cells_run + 1))

# ── RED leg: the same slice without USE_SWIG_DEPENDENCIES ───────────────────
pf="$TMP/red.slice.cmake"
r="$(slice "$CMAKELISTS" --drop-deps 2>&1 >"$pf")" || fail "RED leg: $r"
r="$(leg "$pf" red)" || fail "RED leg errored: $r"
[ "$r" = stale ] || fail "RED leg: without USE_SWIG_DEPENDENCIES the wrapper read '$r', not stale, so this harness cannot see the defect it pins"
echo "RED (expected): without USE_SWIG_DEPENDENCIES the wrapper kept MOCK_VALUE $OLD"
cells_run=$((cells_run + 1))

if [ "$CELLS_ONLY" -eq 1 ]; then
  echo "PASS (cells only, against $CMAKELISTS): $cells_run cells"
  exit 0
fi

# ── Mutants of the REAL file ─────────────────────────────────────────────────
# mkmut <id> <python old> <python new> [<old> <new>]... — writes a copy of the real
# file and prints its path. The (old, new) pairs apply in order; each old must
# occur exactly once.
mkmut() {
  local id="$1" dst="$TMP/CMakeLists.$1.txt"; shift
  python3 - "$REAL" "$dst" "$@" <<'PY' || fail "mutant $id: anchor did not apply"
import sys
src, dst, pairs = sys.argv[1], sys.argv[2], sys.argv[3:]
if not pairs or len(pairs) % 2:
    sys.exit(f"mutant needs (old, new) pairs, got {len(pairs)} argument(s)")
s = open(src).read()
for old, new in zip(pairs[::2], pairs[1::2]):
    if s.count(old) != 1:
        sys.exit(f"mutation anchor occurs {s.count(old)} times, wanted 1: {old!r}")
    s = s.replace(old, new, 1)
open(dst, "w").write(s)
PY
  cmp -s "$REAL" "$dst" && fail "mutant $id did not change the file — a no-op mutant proves nothing"
  echo "$dst"
}

# mutant <id> <python old> <python new> [<old> <new>]... — the copy must fail the
# GREEN leg as stale.
mutant() {
  local id="$1" dst why rc=0
  dst="$(mkmut "$@")" || exit 1
  why="$(green "$dst" "mut-$id")" || rc=$?
  [ "$rc" -ne 0 ] || fail "mutant $id SURVIVED the GREEN leg"
  grep -q "wrapper not regenerated" <<<"$why" || fail "mutant $id failed the GREEN leg for the wrong reason: $why"
  printf 'RED (expected): GREEN leg under mutant %s — %.200s\n' "$id" "$why"
  cells_run=$((cells_run + 1))
}

# refusal <id> <want-substring> <python old> <python new> [<old> <new>]... — the
# GREEN leg must refuse the copy, with <want-substring>, before any build.
refusal() {
  local id="$1" want="$2" dst why rc=0; shift 2
  dst="$(mkmut "$id" "$@")" || exit 1
  why="$(green "$dst" "ref-$id")" || rc=$?
  [ "$rc" -ne 0 ] || fail "refusal cell $id: the GREEN leg accepted the copy"
  grep -qF "$want" <<<"$why" || fail "refusal cell $id failed for the wrong reason (wanted '$want'): $why"
  [ ! -e "$TMP/ref-$id" ] || fail "refusal cell $id: a leg ran before the refusal"
  printf 'RED (expected): GREEN leg refused copy %s — %.200s\n' "$id" "$why"
  cells_run=$((cells_run + 1))
}

DEPS='set_property(SOURCE fixpp.i PROPERTY USE_SWIG_DEPENDENCIES TRUE)
'
CALL_END='  SOURCES fixpp.i
)
'
refusal M-DEPS-IF-FALSE "holds control flow" \
  "$DEPS" "if(FALSE)
${DEPS}endif()
"
refusal R-CTX-CONDITION "holds control flow" \
  "$DEPS" 'if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
'"${DEPS}"'endif()
'
refusal R-OUTER-IF "starts inside a control-flow scope" \
  'set_property(SOURCE fixpp.i PROPERTY CPLUSPLUS ON)
' 'if(FIXPP_PY_COND)
set_property(SOURCE fixpp.i PROPERTY CPLUSPLUS ON)
' \
  "$CALL_END" "${CALL_END}endif()
"
mutant M-DEPS-DELETED "$DEPS" ''
mutant M-DEPS-FALSE 'PROPERTY USE_SWIG_DEPENDENCIES TRUE)' 'PROPERTY USE_SWIG_DEPENDENCIES FALSE)'
mutant M-DEPS-AFTER "$DEPS" '' "$CALL_END" "$CALL_END$DEPS"
refusal R-CALL-TWICE "swig_add_library( starts 2 line(s)" \
  "$CALL_END" "${CALL_END}swig_add_library(fixpp_py_twin TYPE MODULE LANGUAGE python SOURCES fixpp.i)
"
refusal R-NO-PROPERTY "no set_property(SOURCE fixpp.i line precedes swig_add_library(" \
  'set_property(SOURCE fixpp.i PROPERTY CPLUSPLUS ON)
' '' \
  'set_property(SOURCE fixpp.i PROPERTY USE_TARGET_INCLUDE_DIRECTORIES ON)
' '' \
  "$DEPS" ''

[ "$cells_run" -eq "$CELLS_DECLARED" ] || fail "declared $CELLS_DECLARED cells but ran $cells_run"
echo
echo "PASS: SWIG %include dependency pin — $cells_run/$CELLS_DECLARED cells, every mutant reddened the GREEN leg and every refusal fired."
