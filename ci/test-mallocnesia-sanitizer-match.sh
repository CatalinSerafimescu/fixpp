#!/usr/bin/env bash
# ci/test-mallocnesia-sanitizer-match.sh — pin the flag match that decides whether the
# allocation gates register (fixpp#497):
# fixpp_mallocnesia_flags_name_sanitizer() in cmake/FixppMallocnesiaSanitizerMatch.cmake,
# and the registration decision in cmake/FixppMallocnesia.cmake that applies it.
#
# A sanitizer's allocator interposes ahead of the interceptor, so a gate registered on a
# sanitizer build passes without measuring. The match deciding that is one regex, and a
# configured tree exercises it only on the flags that tree carries. So each cell is a
# flag string with the exact result it must give, run under `cmake -P` against the real
# module, and each mutant below is a copy of the module broken one way, asserted to fail
# the cell that names that break.
#
# Buildless: no configured tree of the repo. The match cells run cmake in script mode;
# the registration cells configure a throwaway project, which needs a C and a C++
# compiler but builds nothing.
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
cell(empty        "-fsanitize= -O2"                                      "")
cell(genex-empty  "$<$<CONFIG:Debug>:-fsanitize=>"                       "")
cell(genex-if     "$<IF:$<CONFIG:Debug>,-fsanitize=address,>"            "-fsanitize=address,")
cell(first        "-fsanitize=address -O2"                               "-fsanitize=address")
cell(macro        "-DNOTE=-fsanitize=address -O2"                        "")
cell(path         "-I/opt/x-fsanitize=y/include"                         "")
cell(shell        "SHELL:-fsanitize=address -fno-omit-frame-pointer"     "-fsanitize=address")
# The contract's stated edges (B-497-1): a switch after `:` or `,` inside another argument
# matches, and a switch right after a quote does not. Neither is a target; a matcher change
# that moves one must change the text that states it.
cell(edge-colon   "-DNOTE=x:-fsanitize=address"                          "-fsanitize=address")
cell(edge-comma   "-DNOTE=x,-fsanitize=memory"                           "-fsanitize=memory")
cell(edge-dquote  "\"-fsanitize=leak\""                                  "")
cell(edge-squote  "-O2 '-fsanitize=leak'"                                "")
if("x-fsanitize=stale" MATCHES "(x)(-fsanitize=.*)")
endif()
cell(stale-match  "-O2"                                                  "")
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
if [ "$rc" = 0 ] && [ "$n_ok" = 19 ]; then
  echo "ok    real module: all cells pass"; pass=$((pass+1))
else
  echo "FAIL  real module: rc $rc, $n_ok cell(s) ok, wanted rc 0 and 19"; fail=$((fail+1))
fi

# ── mutants: a broken copy must fail the cells that name its break, and only those ──
# A mutant is the module with its regex swapped for a broken one.
REGEX='"(^|[ \t;:,>])(-fsanitize=[^ ;>]+)"'
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

mutant S1 "colon not a boundary" "genex-leak genex-config shell edge-colon" \
  '"(^|[ \t;,>])(-fsanitize=[^ ;>]+)"'
# `>` dropped from the value's end set: the expression's closing bracket joins the value,
# and an empty switch inside an expression gains `>` as a value.
mutant S2 "generator-expression close not ending the value" \
  "genex-leak genex-config genex-empty genex-if" \
  '"(^|[ \t;:,>])(-fsanitize=[^ ;]+)"'
# Widened to every sanitize-family switch with a value: the negative forms then match.
mutant S3 "every sanitize-family switch matched" "no-sanitize option-cov" \
  '"(^|[ \t;:,>])(-f[a-z-]*sanitize[a-z-]*=[^ ;>]+)"'
# Never matches: every positive cell fails.
mutant S4 "match never fires" "genex-leak genex-config list-memory plain-thread genex-if first shell edge-colon edge-comma" \
  '"(^|[ \t;:,>])(-fsanitize=NEVER[^ ;>]+)"'
# An empty value accepted: a bare `-fsanitize=`, which enables nothing, keeps the gates off.
mutant S5 "empty value matched" "empty genex-empty" \
  '"(^|[ \t;:,>])(-fsanitize=[^ ;>]*)"'
# No boundary: a switch embedded in another argument matches.
mutant S6 "no boundary before the switch" "macro path edge-dquote edge-squote" \
  '"()(-fsanitize=[^ ;>]+)"'
# A comma not a boundary: a switch in a generator expression's IF branch is missed.
mutant S7 "comma not a boundary" "genex-if edge-comma" \
  '"(^|[ \t;:>])(-fsanitize=[^ ;>]+)"'
# The start not a boundary: a switch that opens the flags is missed.
mutant S8 "start not a boundary" "first" \
  '"([ \t;:,>])(-fsanitize=[^ ;>]+)"'

# ── the registration DECISION: the real cmake/FixppMallocnesia.cmake, configured ────
# The cells above pin the match. These pin the decision that uses it: which flags are
# read, that C is enabled before they are, that the predicate is called, and that its
# result decides registration. Each cell configures a throwaway project that includes the
# real modules the way the top-level CMakeLists.txt does (Sanitizers.cmake first),
# declares one target and one fixpp_add_mallocnesia_test(), and counts the tests ctest
# lists. Configure only, so no sanitizer runtime is needed; for the same reason the
# compiler checks build a static library rather than link an executable.
# Needs cmake plus a C and a C++ compiler, which the cells above do not.
WIRING="$REPO/cmake/FixppMallocnesia.cmake"
mkdir -p "$TMP/proj/cmake" "$TMP/proj/tools/mallocnesia"
cp "$REPO/tools/mallocnesia/mallocnesia.c" "$TMP/proj/tools/mallocnesia/"
cat > "$TMP/proj/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(mn_registration LANGUAGES CXX)
enable_testing()
include(cmake/Sanitizers.cmake)
include(cmake/FixppMallocnesia.cmake)
file(WRITE "${CMAKE_BINARY_DIR}/gated.cpp" "int main() { return 0; }\n")
add_executable(gated "${CMAKE_BINARY_DIR}/gated.cpp")
fixpp_add_mallocnesia_test(NAME gated_mallocnesia TARGET gated)
EOF

# reg_cell <name> <want-gates> <env-assignment>... -- <cmake-arg>...
# One line: `ok <name>` or `FAIL <name>: …`. A count ctest did not print is a FAIL, not 0.
reg_cell() {
  local name="$1" want="$2" b="$TMP/b-$1" n
  shift 2
  local envs=()
  while [ "$1" != "--" ]; do envs+=("$1"); shift; done
  shift
  if ! env CCACHE_DISABLE=1 "${envs[@]}" cmake -S "$TMP/proj" -B "$b" \
         -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY "$@" >"$b.log" 2>&1; then
    echo "FAIL  $name: configure failed"; tail -3 "$b.log" | sed 's/^/      /'; rm -rf "$b"; return
  fi
  n="$(ctest --test-dir "$b" -N | sed -n 's/^Total Tests: //p')"
  rm -rf "$b"
  if [ "$n" = "$want" ]; then echo "ok    $name"; else echo "FAIL  $name: ${n:-no count} gate(s), wanted $want"; fi
}
# reg_cells <dir holding the three modules> — every cell, concurrently (each configure
# has its own build dir), reported in a fixed order.
reg_cells() {
  cp "$1/FixppMallocnesia.cmake" "$1/FixppMallocnesiaSanitizerMatch.cmake" \
     "$1/Sanitizers.cmake" "$TMP/proj/cmake/"
  local r
  r="$(mktemp -d "$TMP/cells.XXXX")"
  reg_cell clean       1 -- >"$r/1" &
  reg_cell cxx-leak    0 -- -DCMAKE_CXX_FLAGS=-fsanitize=leak >"$r/2" &
  reg_cell c-leak      0 -- -DCMAKE_C_FLAGS=-fsanitize=leak >"$r/3" &
  reg_cell env-cflags  0 CFLAGS=-fsanitize=leak -- >"$r/4" &
  reg_cell exe-ld-leak 0 -- -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=leak >"$r/5" &
  reg_cell dir-asan    0 -- -DFIXPP_ENABLE_ASAN=ON >"$r/6" &
  reg_cell cfg-release 0 -- -DCMAKE_BUILD_TYPE=Release \
                            -DCMAKE_CXX_FLAGS_RELEASE=-fsanitize=memory >"$r/7" &
  wait
  cat "$r"/1 "$r"/2 "$r"/3 "$r"/4 "$r"/5 "$r"/6 "$r"/7
}
N_REG_CELLS=7

if command -v cc >/dev/null && command -v c++ >/dev/null; then
  mkdir -p "$TMP/mod-real"
  cp "$WIRING" "$MODULE" "$REPO/cmake/Sanitizers.cmake" "$TMP/mod-real/"
  out="$(reg_cells "$TMP/mod-real")"
  echo "$out" | sed 's/^/      /'
  n_ok="$(grep -c '^ok ' <<<"$out")"
  if [ "$n_ok" = "$N_REG_CELLS" ]; then
    echo "ok    real registration: all cells pass"; pass=$((pass+1))
  else
    echo "FAIL  real registration: $n_ok cell(s) ok, wanted $N_REG_CELLS"; fail=$((fail+1))
  fi

  # wmutant <id> <what> <want-failed-cells> <old> <new> [<old> <new>]... — the real
  # wiring with each <old> replaced by its <new>; must fail exactly the cells that name
  # its break.
  wmutant() {
    local id="$1" what="$2" want="$3" d="$TMP/mod-$1" out got
    shift 3
    mkdir -p "$d"; cp "$MODULE" "$REPO/cmake/Sanitizers.cmake" "$d/"
    python3 - "$WIRING" "$d/FixppMallocnesia.cmake" "$@" <<'PY' || { echo "FAIL  $id: transform did not apply"; fail=$((fail+1)); return; }
import sys
src, dst, pairs = sys.argv[1], sys.argv[2], sys.argv[3:]
assert pairs and len(pairs) % 2 == 0
t = open(src, encoding="utf-8").read()
for old, new in zip(pairs[::2], pairs[1::2]):
    assert t.count(old) == 1, (old, t.count(old))
    t = t.replace(old, new)
open(dst, "w", encoding="utf-8").write(t)
PY
    if cmp -s "$WIRING" "$d/FixppMallocnesia.cmake"; then
      echo "FAIL  $id: the mutant is byte-identical to the module"; fail=$((fail+1)); return
    fi
    out="$(reg_cells "$d")"
    got="$(grep -E '^FAIL ' <<<"$out" | sed -E 's/^FAIL +([^:]+):.*/\1/' | sort | xargs)"
    want="$(tr ' ' '\n' <<<"$want" | sort | xargs)"
    if [ "$got" = "$want" ]; then
      echo "ok    $id ($what) fails exactly: $got"; pass=$((pass+1))
    else
      echo "FAIL  $id ($what): failed cells '$got', wanted '$want'"; fail=$((fail+1))
    fi
  }
  ALL_SAN="cxx-leak c-leak env-cflags exe-ld-leak dir-asan cfg-release"
  wmutant M1 "the predicate's result ignored" "$ALL_SAN" \
    'if(UNIX AND NOT APPLE AND NOT _mn_sanitizer)' 'if(UNIX AND NOT APPLE)'
  wmutant M2 "the predicate never called" "$ALL_SAN" \
    'fixpp_mallocnesia_flags_name_sanitizer(_mn_sanitizer "${_mn_flags}")' ''
  wmutant M3 "CMAKE_CXX_FLAGS not read" "cxx-leak cfg-release" \
    'set(_mn_flag_vars CMAKE_C_FLAGS CMAKE_CXX_FLAGS ' 'set(_mn_flag_vars CMAKE_C_FLAGS '
  wmutant M4 "directory options not read" "dir-asan" \
    'string(APPEND _mn_flags " ${_mn_compile_options} ${_mn_link_options}")' ''
  wmutant M5 "per-config flag forms not read" "cfg-release" \
    'string(APPEND _mn_flags " ${${_mn_var}_${_mn_config}}")' ''
  wmutant M6 "C enabled after the flags are read" "env-cflags" \
    'if(UNIX AND NOT APPLE)
  enable_language(C)
endif()
set(_mn_flag_vars' 'set(_mn_flag_vars' \
    'if(UNIX AND NOT APPLE AND NOT _mn_sanitizer)' 'enable_language(C)
if(UNIX AND NOT APPLE AND NOT _mn_sanitizer)'
  wmutant M7 "glibc probe inverted" "clean" \
    '__GLIBC__' '__GLIBC_NOT__'
else
  echo "FAIL  registration cells: no C or C++ compiler; refusing to skip"; fail=$((fail+1))
fi

echo
echo "test-mallocnesia-sanitizer-match: $pass passed, $fail failed"
[ "$fail" = 0 ]
