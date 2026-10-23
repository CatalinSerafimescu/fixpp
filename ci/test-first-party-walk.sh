#!/usr/bin/env bash
# ci/test-first-party-walk.sh — pin which targets the deferred policy walks reach (#560):
# fixpp_apply_werror_to_all_targets (#417), fixpp_apply_common_flags_to_all_targets (#481)
# and fixpp_instrument_libraries_for_fuzzing (#508), all fed by
# fixpp_collect_buildsystem_targets() in cmake/Helpers.cmake.
#
# The walks fail toward clean: a target they skip gets fewer flags and no error, and a
# target they wrongly reach gets fixpp's private policy. A configured fixpp tree shows
# only its own layout, so each case here is a throwaway mock project that includes the
# REAL cmake/Helpers.cmake (never a copy of its functions) and defers the three walks
# exactly as the top-level CMakeLists.txt does. The observable is compile_commands.json:
# which source files carry each walk's flag, not what the enumerator returns.
#
# Arms, each a cell asserting its own line of output:
#   positive     the mock's first-party targets carry each walk's flag, standalone and
#                when the mock is itself brought in by FetchContent;
#   fetchcontent no walk reaches a FetchContent dependency, whether its source lies
#                outside the mock or was pointed inside it (under src/, so the fuzz
#                walk's location filter alone would admit it);
#   embedded     added to a superproject, no walk reaches the superproject's targets;
#   out-of-tree  no walk reaches a directory the mock adds with a plain add_subdirectory
#                from outside its own source tree.
# Each mutant below is a copy of Helpers.cmake broken one way, asserted to fail the arm
# it names. Mutants that fail to apply are refused (cmp guard), never counted as RED.
#
# Usage: ci/test-first-party-walk.sh [--helpers <Helpers.cmake>]
#   --helpers runs the cells only, against the given file (e.g. a pre-fix revision
#   extracted with `git show <rev>:cmake/Helpers.cmake`), and skips the mutants.
#
# Buildless: configures the mocks (needs cmake, a C and a C++ compiler), builds nothing.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
HELPERS="$REPO/cmake/Helpers.cmake"
CELLS_ONLY=0
if [ "${1:-}" = "--helpers" ]; then
  [ -n "${2:-}" ] && [ -f "$2" ] || { echo "FAIL: --helpers needs an existing file"; exit 1; }
  HELPERS="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; CELLS_ONLY=1
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

command -v cmake >/dev/null || { echo "FAIL: cmake not found; refusing to skip"; exit 1; }
command -v python3 >/dev/null || { echo "FAIL: python3 not found; refusing to skip"; exit 1; }

# ── Fixtures ─────────────────────────────────────────────────────────────────
F="$TMP/fixtures"
mkdir -p "$F/dep_out" "$F/ext_out" "$F/fixpp/src/core" "$F/fixpp/src/vendor/dep_in" "$F/fixpp/perf" \
         "$F/super/src" "$F/fetcher"
src() { printf 'int %s() { return 0; }\n' "$(basename "${1%.*}")" > "$1"; }

# A dependency whose source lies outside the mock fixpp; C++ plus a C TU, the latter
# because -Werror is added without a language guard.
cat > "$F/dep_out/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(dep_out LANGUAGES C CXX)
add_library(dep_out_cxx STATIC dep_out_cxx.cpp)
add_library(dep_out_c STATIC dep_out_c.c)
EOF
src "$F/dep_out/dep_out_cxx.cpp"; src "$F/dep_out/dep_out_c.c"

echo 'add_library(ext_out STATIC ext_out.cpp)' > "$F/ext_out/CMakeLists.txt"
src "$F/ext_out/ext_out.cpp"

cat > "$F/fixpp/src/vendor/dep_in/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(dep_in LANGUAGES CXX)
add_library(dep_in_cxx STATIC dep_in_cxx.cpp)
EOF
src "$F/fixpp/src/vendor/dep_in/dep_in_cxx.cpp"

# The mock fixpp: a target at its root, a library under src/, and the dependencies
# fetched from a subdirectory, as perf/ fetches HdrHistogram_c.
cat > "$F/fixpp/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(fixpp LANGUAGES CXX)
include("${FIXPP_PROBE_HELPERS}")
cmake_language(DEFER CALL fixpp_apply_werror_to_all_targets)
cmake_language(DEFER CALL fixpp_apply_common_flags_to_all_targets)
cmake_language(DEFER CALL fixpp_instrument_libraries_for_fuzzing)
add_library(fp_root STATIC fp_root.cpp)
add_subdirectory(src/core)
add_subdirectory(perf)
EOF
src "$F/fixpp/fp_root.cpp"
echo 'add_library(fp_core STATIC fp_core.cpp)' > "$F/fixpp/src/core/CMakeLists.txt"
src "$F/fixpp/src/core/fp_core.cpp"
cat > "$F/fixpp/perf/CMakeLists.txt" <<'EOF'
include(FetchContent)
FetchContent_Declare(dep_out SOURCE_DIR "${FIXPP_PROBE_DEP_OUT}")
FetchContent_Declare(dep_in SOURCE_DIR "${PROJECT_SOURCE_DIR}/src/vendor/dep_in")
FetchContent_MakeAvailable(dep_out dep_in)
add_subdirectory("${FIXPP_PROBE_EXT_OUT}" ext_out)
EOF

# A superproject that declares its own src/ library, then embeds the mock.
cat > "$F/super/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(super LANGUAGES CXX)
add_subdirectory(src)
add_subdirectory("${FIXPP_PROBE_MOCK}" fixpp)
EOF
echo 'add_library(super_lib STATIC super_lib.cpp)' > "$F/super/src/CMakeLists.txt"
src "$F/super/src/super_lib.cpp"

# A superproject that brings the mock in by FetchContent, so the mock's own binary
# directory lies under FETCHCONTENT_BASE_DIR.
cat > "$F/fetcher/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(fetcher LANGUAGES CXX)
include(FetchContent)
FetchContent_Declare(fixpp SOURCE_DIR "${FIXPP_PROBE_MOCK}")
FetchContent_MakeAvailable(fixpp)
EOF

# ── Run ──────────────────────────────────────────────────────────────────────
# configure <helpers> <project-dir> <build-dir>; prints cmake's output on failure.
configure() {
  local out
  if ! out="$(cmake -S "$2" -B "$3" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DFIXPP_WERROR=ON \
      -DFIXPP_PROBE_HELPERS="$1" -DFIXPP_PROBE_DEP_OUT="$F/dep_out" -DFIXPP_PROBE_EXT_OUT="$F/ext_out" \
      -DFIXPP_PROBE_MOCK="$F/fixpp" -DFETCHCONTENT_FULLY_DISCONNECTED=ON 2>&1)"; then
    echo "configure of $(basename "$2") failed:"; echo "$out" | sed 's/^/      /' | tail -8
  fi
}

# One line per cell: `ok <arm>: ...` or `FAIL <arm>: ...`. Exits non-zero on any FAIL.
# A cell is <arm> <build> <source-basename> <flag> <want: yes|no>. The expected values
# are spelled out here, not derived from Helpers.cmake.
cells() {  # cells <helpers> <workdir>
  local h="$1" w="$2"
  configure "$h" "$F/fixpp"   "$w/standalone"
  configure "$h" "$F/super"   "$w/embedded"
  configure "$h" "$F/fetcher" "$w/fetched"
  python3 - "$w" <<'EOF'
import json, os, sys
w = sys.argv[1]
WERROR, COMMON, FUZZ = "-Werror", "-Wextra", "-fsanitize=fuzzer-no-link"
ALL = (WERROR, COMMON, FUZZ)
cells = []
def want(arm, build, src, flags, yes):
    for f in flags: cells.append((arm, build, src, f, yes))
# positive: first-party targets are reached. fp_root is outside src/, so no fuzz flag.
want("positive", "standalone", "fp_root.cpp", (WERROR, COMMON), True)
want("positive", "standalone", "fp_core.cpp", ALL, True)
want("positive", "fetched", "fp_core.cpp", ALL, True)
want("positive", "embedded", "fp_core.cpp", ALL, True)
# fetchcontent: no dependency TU carries any walk's flag.
for b in ("standalone", "fetched"):
    for s in ("dep_out_cxx.cpp", "dep_out_c.c", "dep_in_cxx.cpp"):
        want("fetchcontent", b, s, ALL, False)
# embedded: the superproject's own target carries none.
want("embedded", "embedded", "super_lib.cpp", ALL, False)
# out-of-tree: a plain add_subdirectory from outside the mock carries none.
for b in ("standalone", "embedded", "fetched"):
    want("out-of-tree", b, "ext_out.cpp", ALL, False)

db = {}
for b in ("standalone", "embedded", "fetched"):
    p = os.path.join(w, b, "compile_commands.json")
    db[b] = {}
    if os.path.exists(p):
        for e in json.load(open(p)):
            db[b][os.path.basename(e["file"])] = e.get("command") or " ".join(e["arguments"])
failed = 0
for arm, b, s, f, yes in cells:
    cmd = db[b].get(s)
    if cmd is None:
        print(f"FAIL  {arm}: {b}/{s} absent from compile_commands.json"); failed = 1; continue
    has = f in cmd.split()
    if has == yes:
        print(f"ok    {arm}: {b}/{s} {'has' if yes else 'lacks'} {f}")
    else:
        print(f"FAIL  {arm}: {b}/{s} {'lacks' if yes else 'has'} {f}"); failed = 1
sys.exit(failed)
EOF
}

echo "== cells against $HELPERS"
cells "$HELPERS" "$TMP/base"; base_rc=$?
[ "$CELLS_ONLY" = 1 ] && exit "$base_rc"
[ "$base_rc" = 0 ] || { echo "FAIL: cells are RED against the real Helpers.cmake"; exit 1; }

# mutant <name> <arm-it-must-fail> <sed-expression>
mut_pass=0; mut_fail=0
mutant() {
  local name="$1" arm="$2" expr="$3" m="$TMP/mut-$1.cmake" out rc
  sed -e "$expr" "$HELPERS" > "$m"
  if cmp -s "$HELPERS" "$m"; then
    echo "FAIL  mutant $name: did not apply (the pinned line moved?)"; mut_fail=$((mut_fail+1)); return
  fi
  out="$(cells "$m" "$TMP/w-$name" 2>&1)"; rc=$?
  if [ "$rc" != 0 ] && printf '%s\n' "$out" | grep -q "^FAIL  $arm: "; then
    echo "ok    mutant $name: RED on arm '$arm'"; mut_pass=$((mut_pass+1))
  else
    echo "FAIL  mutant $name: arm '$arm' stayed GREEN (exit $rc)"
    printf '%s\n' "$out" | grep '^FAIL' | sed 's/^/      /' | head -5; mut_fail=$((mut_fail+1))
  fi
}

echo "== mutants"
# The walk roots at the superproject when embedded.
mutant root embedded 's|set(_root "${PROJECT_SOURCE_DIR}")|set(_root "${CMAKE_SOURCE_DIR}")|'
# The fuzz walk's location filter roots at the superproject when embedded.
mutant fuzz-root embedded 's|set(_src_root "${PROJECT_SOURCE_DIR}/src")|set(_src_root "${CMAKE_SOURCE_DIR}/src")|'
# No FETCHCONTENT_BASE_DIR test: a dependency pointed inside the tree is walked.
mutant no-fetched fetchcontent 's|if(NOT _in_tree OR _fetched)|if(NOT _in_tree)|'
# No source-root test: a directory added from outside the tree is walked.
mutant no-in-tree out-of-tree 's|if(NOT _in_tree OR _fetched)|if(_fetched)|'
# No rescue for fixpp's own binary directory: a fetched fixpp prunes itself.
mutant no-ours positive 's|          set(_fetched FALSE)|          # mutant|'
# The walk never descends: subdirectory targets drop out, the root's stay.
mutant no-descend positive 's|        list(APPEND _dirs "${_sub}")|        # mutant|'

echo "mutants: $mut_pass RED as named, $mut_fail not"
[ "$mut_fail" = 0 ]
