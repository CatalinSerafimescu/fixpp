#!/usr/bin/env bash
# ci/test-mallocnesia-sanitizer-match.sh — pin the flag match that keeps the allocation
# gates off a sanitizer build (fixpp#497):
# fixpp_mallocnesia_flags_name_sanitizer() in cmake/FixppMallocnesiaSanitizerMatch.cmake.
#
# A sanitizer's allocator interposes ahead of the interceptor, so a gate registered on a
# sanitizer build passes without measuring. The match deciding that is one regex, and an
# edit to it is checked by nothing else: no configured tree on a CI lane carries a
# sanitizer the guard would miss. So each cell is a flag string with the exact result it
# must give, run under `cmake -P` against the real module, and each mutant below is a
# copy of the module broken one way, asserted to fail the cell that names that break.
#
# Buildless: cmake in script mode only, no configured tree, no compiler.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
MODULE="$REPO/cmake/FixppMallocnesiaSanitizerMatch.cmake"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

command -v cmake >/dev/null || { echo "FAIL: cmake not found; refusing to skip"; exit 1; }

# One line per cell: `ok <cell>` or `FAIL <cell>: got '<x>', wanted '<y>'`; exits non-zero
# when any cell fails. The expected values are spelled out here, not derived from the
# module, so an edit to the module cannot move both sides.
cat > "$TMP/cells.cmake" <<'EOF'
include("${MODULE}")
set(failed 0)
function(cell name flags want)
  fixpp_mallocnesia_flags_name_sanitizer(got "${flags}")
  if(got STREQUAL want)
    message("ok    ${name}")
  else()
    message("FAIL  ${name}: got '${got}', wanted '${want}'")
    set(failed 1 PARENT_SCOPE)
  endif()
endfunction()
cell(genex-leak   "$<$<COMPILE_LANGUAGE:CXX>:-fsanitize=leak>"          "-fsanitize=leak")
cell(genex-config "-O2 $<$<CONFIG:Debug>:-fsanitize=address>"            "-fsanitize=address")
cell(list-memory  "-O2;-fsanitize=memory"                                "-fsanitize=memory")
cell(plain-thread "-O2 -fsanitize=thread -fno-omit-frame-pointer"        "-fsanitize=thread")
cell(no-sanitize  "-fno-sanitize=all -fno-sanitize-recover=all"          "")
cell(option-cov   "-fsanitize-coverage=trace-pc"                         "")
cell(clean        "-O2 -g -Wall"                                         "")
if(failed)
  message(FATAL_ERROR "sanitizer-match cells failed")
endif()
EOF

pass=0; fail=0
run_cells() {  # run_cells <module> — prints the cells' output, returns cmake's rc
  cmake -DMODULE="$1" -P "$TMP/cells.cmake" 2>&1
}

# ── the real module: every cell must pass ─────────────────────────────────────────
out="$(run_cells "$MODULE")"; rc=$?
echo "$out" | grep -E '^(ok|FAIL) '
n_ok="$(grep -c '^ok ' <<<"$out")"
if [ "$rc" = 0 ] && [ "$n_ok" = 7 ]; then
  echo "ok    real module: all cells pass"; pass=$((pass+1))
else
  echo "FAIL  real module: rc $rc, $n_ok cell(s) ok, wanted rc 0 and 7"; fail=$((fail+1))
fi

# ── mutants: a broken copy must fail the cells that name its break, and only those ──
# Each replaces the module's regex, and nothing else, with a broken one.
REGEX='"-fsanitize=[^ ;>]*"'
mutant() {  # mutant <id> <what> <want-failed-cells, space-separated> <replacement regex>
  local id="$1" what="$2" want="$3" copy="$TMP/$1.cmake" out rc got
  python3 - "$MODULE" "$copy" "$REGEX" "$4" <<'PY' || { echo "FAIL  $id: transform did not apply"; fail=$((fail+1)); return; }
import sys
src, dst, old, new = sys.argv[1:5]
t = open(src).read()
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PY
  if cmp -s "$MODULE" "$copy"; then
    echo "FAIL  $id: the mutant is byte-identical to the module"; fail=$((fail+1)); return
  fi
  out="$(run_cells "$copy")"; rc=$?
  got="$(grep -E '^FAIL ' <<<"$out" | sed -E 's/^FAIL +([^:]+):.*/\1/' | sort | xargs)"
  want="$(tr ' ' '\n' <<<"$want" | sort | xargs)"
  if [ "$rc" != 0 ] && [ "$got" = "$want" ]; then
    echo "ok    $id ($what) fails exactly: $got"; pass=$((pass+1))
  else
    echo "FAIL  $id ($what): rc $rc, failed cells '$got', wanted '$want'"; fail=$((fail+1))
  fi
}

# The match anchored to a preceding space or `;`: a sanitizer inside a generator
# expression follows `:`, not a separator, so both genex cells miss. The separator also
# joins the value where the match does fire, so the two separator cells fail with them.
mutant S1 "match anchored to a separator" "genex-leak genex-config list-memory plain-thread" \
  '"[ ;]-fsanitize=[^ ;>]*"'
# `>` dropped from the value's end set: the expression's closing bracket joins the value.
mutant S2 "generator-expression close not ending the value" "genex-leak genex-config" \
  '"-fsanitize=[^ ;]*"'
# Widened to every sanitize-family switch with a value: the negative forms then match.
mutant S3 "every sanitize-family switch matched" "no-sanitize option-cov" \
  '"-f[a-z-]*sanitize[a-z-]*=[^ ;>]*"'
# Never matches: every positive cell fails.
mutant S4 "match never fires" "genex-leak genex-config list-memory plain-thread" \
  '"-fsanitize=NEVER[^ ;>]*"'

echo
echo "test-mallocnesia-sanitizer-match: $pass passed, $fail failed"
[ "$fail" = 0 ]
