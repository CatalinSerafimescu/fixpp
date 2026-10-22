#!/usr/bin/env bash
# ci/test-packaging-instrumented-skip.sh — pin when fixpp::packaging::clean-env may
# read Skipped (#529).
#
# The clean-environment witness links a consumer with no sanitizer runtime, so on an
# instrumented build it prints a SKIPPED line and stops, and the registration's
# SKIP_REGULAR_EXPRESSION turns that into Skipped. Both directions can fail toward
# green: a clean build classified as instrumented reads Skipped with the witness never
# run, and ctest exits 0 on Skipped. No CI lane registers the packaging tier on an
# instrumented build, so neither direction is exercised by a configured tree.
#
# Each cell configures a throwaway project that add_subdirectory's the REAL
# tests/packaging/CMakeLists.txt (never a copy of its logic), with the instrumentation
# options set as a preset would set them, then reads the registration
# (`ctest --show-only=json-v1`) and runs the test. On a clean configuration the witness
# goes past its guard and stops at T043, since the mock installs nothing; that FATAL
# line is the marker that it ran.
#
# Cells, each a check asserting its own line of output:
#   clean      no option on: not Skipped, no SKIPPED line, the witness ran, and the
#              registration carries no SKIP_REGULAR_EXPRESSION;
#   asan ubsan tsan
#              that option on: the registration passes it to the witness and carries
#              the skip regex, the SKIPPED line names it, the test reads Skipped, and
#              the line does not cite B-508-2;
#   fuzz       FIXPP_BUILD_FUZZ on: as above, and the line cites B-508-2;
#   lookalike  the witness run directly: the exact FIXPP_BUILD_FUZZ=ON token gets the
#              cite, a token that merely contains it does not.
# Each mutant is a copy of tests/packaging/ broken one way, run against the one cell it
# names, which must fail for that cell's own named reason. A mutant whose edit matched
# nothing is refused, never counted as RED.
#
# Usage: ci/test-packaging-instrumented-skip.sh [--packaging-dir <dir>]
#   --packaging-dir runs the cells only, against the given copy of tests/packaging/
#   (e.g. a revision extracted with `git archive <rev> tests/packaging`), and skips
#   the mutants.
#
# Needs cmake, ctest, make and a C++ compiler, and refuses to run without them.
# Configures only; the witness's own install of the empty mock is the only work it does.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
REAL="$REPO/tests/packaging"
PKG="$REAL"
CELLS_ONLY=0
if [ "${1:-}" = "--packaging-dir" ]; then
  [ -n "${2:-}" ] && [ -f "$2/CMakeLists.txt" ] || { echo "FAIL: --packaging-dir needs a directory holding CMakeLists.txt"; exit 1; }
  PKG="$(cd "$2" && pwd)"; CELLS_ONLY=1
fi

fail() { printf 'FAIL: %b\n' "$1" >&2; exit 1; }
for t in cmake ctest make python3; do
  command -v "$t" >/dev/null || fail "$t not found; refusing to skip"
done
command -v "${CXX:-c++}" >/dev/null || fail "no C++ compiler (${CXX:-c++}) on PATH; refusing to skip"
[ -f "$REAL/CMakeLists.txt" ] && [ -f "$REAL/run_clean_env_witness.cmake" ] || fail "not found: $REAL"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
NAME='fixpp::packaging::clean-env'
SKIPLINE='fixpp::packaging::clean-env SKIPPED (instrumented build: '

CELLS_DECLARED=18
cells_run=0

mkdir -p "$TMP/mock"
cat > "$TMP/mock/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(fixpp VERSION 0.0.1 LANGUAGES CXX)
enable_testing()
add_subdirectory("${MOCK_PKG_DIR}" packaging)
EOF

# configure <build-dir> [-D...] — the mock against $PKG; sets B, and prints why on failure.
configure() {
  B="$1"; shift
  local out
  out="$(cmake -S "$TMP/mock" -B "$B" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
         -DMOCK_PKG_DIR="$PKG" "$@" 2>&1)" || { echo "configure failed: $out"; return 1; }
}

# reg <field> — read the clean-env registration from `ctest --show-only=json-v1`:
#   skipre   the SKIP_REGULAR_EXPRESSION value, or empty when the property is absent;
#   instr    the value the command passes as -DFIXPP_INSTRUMENTED_BY=, or <absent>.
reg() {
  ctest --test-dir "$B" --show-only=json-v1 2>/dev/null | python3 -c '
import json, sys
field, name = sys.argv[1], sys.argv[2]
tests = [t for t in json.load(sys.stdin)["tests"] if t["name"] == name]
if len(tests) != 1:
    sys.exit(f"{len(tests)} registrations named {name}")
t = tests[0]
if field == "skipre":
    v = [p["value"] for p in t.get("properties", []) if p["name"] == "SKIP_REGULAR_EXPRESSION"]
    print(";".join(v[0]) if v and isinstance(v[0], list) else (v[0] if v else ""))
else:
    a = [c[len("-DFIXPP_INSTRUMENTED_BY="):] for c in t["command"] if c.startswith("-DFIXPP_INSTRUMENTED_BY=")]
    print(a[0] if len(a) == 1 else "<absent>")
' "$1" "$NAME"
}

# runtest — run the registration; sets OUT (full output) and STATUS (Passed, Failed,
# Skipped, or the unparsed line).
runtest() {
  OUT="$(ctest --test-dir "$B" -R "^${NAME}\$" -V 2>&1)"
  local line
  line="$(grep -E "Test +#[0-9]+: ${NAME} " <<<"$OUT" | tail -n 1)"
  case "$line" in
    *Skipped*) STATUS=Skipped ;;
    *Failed*)  STATUS=Failed ;;
    *Passed*)  STATUS=Passed ;;
    *)         STATUS="unparsed: $line" ;;
  esac
}

# ── cells: each returns 0 when its expectation holds, else prints why ────────

C_clean() {
  configure "$TMP/b.clean.$RANDOM$RANDOM" || return 1
  runtest
  [ "$STATUS" != Skipped ] || { echo "a clean build reads Skipped"; return 1; }
  ! grep -qF "$SKIPLINE" <<<"$OUT" || { echo "a clean build printed the SKIPPED line: $(grep -F "$SKIPLINE" <<<"$OUT")"; return 1; }
  grep -qF "T043: no installed package files" <<<"$OUT" \
    || { echo "the witness did not run past its guard (status $STATUS): $(tail -n 5 <<<"$OUT")"; return 1; }
  [ -z "$(reg skipre)" ] || { echo "a clean registration carries SKIP_REGULAR_EXPRESSION '$(reg skipre)'"; return 1; }
}

# instrumented <option> <cite: yes|no>
instrumented() {
  local opt="$1" cite="$2"
  configure "$TMP/b.$opt.$RANDOM$RANDOM" "-D$opt=ON" || return 1
  [ "$(reg instr)" = "$opt=ON" ] \
    || { echo "the registration passes FIXPP_INSTRUMENTED_BY='$(reg instr)', not '$opt=ON'"; return 1; }
  [ -n "$(reg skipre)" ] || { echo "an instrumented registration carries no SKIP_REGULAR_EXPRESSION"; return 1; }
  runtest
  grep -qF "${SKIPLINE}${opt}=ON)" <<<"$OUT" \
    || { echo "no SKIPPED line naming $opt=ON (status $STATUS): $(tail -n 5 <<<"$OUT")"; return 1; }
  [ "$STATUS" = Skipped ] || { echo "an instrumented build reads $STATUS, not Skipped"; return 1; }
  if [ "$cite" = yes ]; then
    grep -F "$SKIPLINE" <<<"$OUT" | grep -qF "(B-508-2)" || { echo "the fuzz SKIPPED line does not cite (B-508-2)"; return 1; }
  else
    ! grep -F "$SKIPLINE" <<<"$OUT" | grep -qF "B-508-2" || { echo "a $opt SKIPPED line cites B-508-2"; return 1; }
  fi
}
C_asan()  { instrumented FIXPP_ENABLE_ASAN no; }
C_ubsan() { instrumented FIXPP_ENABLE_UBSAN no; }
C_tsan()  { instrumented FIXPP_ENABLE_TSAN no; }
C_fuzz()  { instrumented FIXPP_BUILD_FUZZ yes; }

# witness <FIXPP_INSTRUMENTED_BY> — the witness run directly; prints its output.
witness() {
  cmake -DFIXPP_MAIN_BUILD_DIR="$TMP/nowhere" -DFIXPP_CONSUMER_SRC_DIR="$TMP/nowhere" \
        -DFIXPP_WORK_DIR="$TMP/w.$RANDOM$RANDOM" -DFIXPP_CXX_COMPILER=c++ -DFIXPP_BUILD_TYPE=Release \
        "-DFIXPP_INSTRUMENTED_BY=$1" -P "$PKG/run_clean_env_witness.cmake" 2>&1
}
C_lookalike() {
  local o
  o="$(witness 'FIXPP_ENABLE_ASAN=ON FIXPP_BUILD_FUZZ=ON')"
  grep -F "$SKIPLINE" <<<"$o" | grep -qF "(B-508-2)" \
    || { echo "control: the exact FIXPP_BUILD_FUZZ=ON token got no cite: $o"; return 1; }
  o="$(witness 'XFIXPP_BUILD_FUZZ=ONX')"
  grep -qF "$SKIPLINE" <<<"$o" || { echo "the lookalike token printed no SKIPPED line: $o"; return 1; }
  ! grep -F "$SKIPLINE" <<<"$o" | grep -qF "B-508-2" \
    || { echo "a token merely containing FIXPP_BUILD_FUZZ=ON got the cite: $(grep -F "$SKIPLINE" <<<"$o")"; return 1; }
}

CELLS=(C_clean C_asan C_ubsan C_tsan C_fuzz C_lookalike)

# ── pass 1: the real tests/packaging/, every cell must hold ─────────────────
for c in "${CELLS[@]}"; do
  why="$("$c")" || fail "$c should hold on $PKG but did not: $why"
  echo "GREEN (expected): $c"
  cells_run=$((cells_run + 1))
done

if [ "$CELLS_ONLY" -eq 1 ]; then
  echo "PASS (cells only, against $PKG): $cells_run cells"
  exit 0
fi

# ── pass 2: mutants of a copy of tests/packaging/ ───────────────────────────
# mutant <id> <cell> <reason> (<file> <old> <new>)... — <file> is relative to
# tests/packaging/; every old must occur exactly once in its file.
mutant() {
  local id="$1" cell="$2" reason="$3"; shift 3
  local d="$TMP/pkg.$id" why rc=0
  mkdir -p "$d"
  cp "$REAL/CMakeLists.txt" "$REAL/run_clean_env_witness.cmake" "$d/"
  python3 - "$d" "$@" <<'PY' || fail "mutant $id: an anchor did not apply"
import sys
d, rest = sys.argv[1], sys.argv[2:]
if not rest or len(rest) % 3:
    sys.exit(f"mutant needs (file, old, new) triples, got {len(rest)} argument(s)")
for f, old, new in zip(rest[::3], rest[1::3], rest[2::3]):
    p = f"{d}/{f}"
    s = open(p).read()
    if s.count(old) != 1:
        sys.exit(f"{f}: mutation anchor occurs {s.count(old)} times, wanted 1: {old!r}")
    open(p, "w").write(s.replace(old, new, 1))
PY
  if cmp -s "$REAL/CMakeLists.txt" "$d/CMakeLists.txt" \
     && cmp -s "$REAL/run_clean_env_witness.cmake" "$d/run_clean_env_witness.cmake"; then
    fail "mutant $id changed nothing — a no-op mutant proves nothing"
  fi
  why="$(PKG="$d" "$cell")" || rc=$?
  [ "$rc" -ne 0 ] || fail "mutant $id SURVIVED cell $cell"
  grep -qF -- "$reason" <<<"$why" || fail "mutant $id failed $cell for the wrong reason (wanted '$reason'): $why"
  printf 'RED (expected): %s under mutant %s — %.240s\n' "$cell" "$id" "$why"
  cells_run=$((cells_run + 1))
}

W=run_clean_env_witness.cmake
L=CMakeLists.txt
GUARD='if(NOT FIXPP_INSTRUMENTED_BY STREQUAL "")'
CONDREGEX='if(NOT _fixpp_pkg_instrumented STREQUAL "")
  set_tests_properties(fixpp::packaging::clean-env PROPERTIES
    SKIP_REGULAR_EXPRESSION'

# The witness never skips.
mutant K-GUARD-REMOVED C_asan "no SKIPPED line naming" \
  "$W" "$GUARD" 'if(FALSE)'
# The witness skips exactly the clean builds. The clean registration has no skip regex,
# so the line meets the FATAL_ERROR and the test reads Failed, not Skipped.
mutant K-GUARD-INVERTED C_clean "a clean build printed the SKIPPED line" \
  "$W" "$GUARD" 'if(FIXPP_INSTRUMENTED_BY STREQUAL "")'
# No skip regex on any registration.
mutant K-REGEX-DROPPED C_asan "carries no SKIP_REGULAR_EXPRESSION" \
  "$L" "$CONDREGEX" 'if(FALSE)
  set_tests_properties(fixpp::packaging::clean-env PROPERTIES
    SKIP_REGULAR_EXPRESSION'
# The skip regex on every registration, the shape before the condition was added.
mutant K-REGEX-UNCONDITIONAL C_clean "a clean registration carries SKIP_REGULAR_EXPRESSION" \
  "$L" "$CONDREGEX" 'if(TRUE)
  set_tests_properties(fixpp::packaging::clean-env PROPERTIES
    SKIP_REGULAR_EXPRESSION'
# The new direction: an unconditional skip regex turns a misclassified clean build's
# failure into Skipped.
mutant K-REGEX-UNCOND-INVERTED C_clean "a clean build reads Skipped" \
  "$L" "$CONDREGEX" 'if(TRUE)
  set_tests_properties(fixpp::packaging::clean-env PROPERTIES
    SKIP_REGULAR_EXPRESSION' \
  "$W" "$GUARD" 'if(FIXPP_INSTRUMENTED_BY STREQUAL "")'
# The derivation names no option.
mutant K-DERIVATION-EMPTY C_asan "the registration passes FIXPP_INSTRUMENTED_BY=''" \
  "$L" 'list(APPEND _fixpp_pkg_instrumented "${_opt}=ON")' ''
# The derivation names every option whether set or not: a clean build misclassified.
mutant K-DERIVATION-ALWAYS C_clean "a clean build reads Skipped" \
  "$L" 'if(${_opt})' 'if(TRUE)'
# The cite on every SKIPPED line.
mutant K-CITE-ALWAYS C_asan "SKIPPED line cites B-508-2" \
  "$W" '  set(_skip_cite "")' '  set(_skip_cite " (B-508-2)")'
# The cite on none.
mutant K-CITE-NEVER C_fuzz "does not cite (B-508-2)" \
  "$W" '    set(_skip_cite " (B-508-2)")' '    set(_skip_cite "")'
# The cite on a substring match.
mutant K-CITE-SUBSTRING C_lookalike "merely containing FIXPP_BUILD_FUZZ=ON got the cite" \
  "$W" 'if(" ${FIXPP_INSTRUMENTED_BY} " MATCHES " FIXPP_BUILD_FUZZ=ON ")' \
       'if("${FIXPP_INSTRUMENTED_BY}" MATCHES "FIXPP_BUILD_FUZZ=ON")'

# The witness stops before T043 without printing the line.
mutant K-WITNESS-EARLY-EXIT C_clean "the witness did not run past its guard" \
  "$W" 'set(_stage    "${FIXPP_WORK_DIR}/stage")' 'message(FATAL_ERROR "stopped early")'
# The SKIPPED line stops naming the options.
mutant K-LINE-UNNAMED C_tsan "no SKIPPED line naming FIXPP_ENABLE_TSAN=ON" \
  "$W" '"${FIXPP_INSTRUMENTED_BY}): a consumer' '"): a consumer'

[ "$cells_run" -eq "$CELLS_DECLARED" ] || fail "declared $CELLS_DECLARED cells but ran $cells_run"
echo
echo "PASS: packaging instrumented-skip pin — $cells_run/$CELLS_DECLARED cells, every mutant reddened its own cell for its own reason."
