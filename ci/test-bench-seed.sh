#!/usr/bin/env bash
# ci/test-bench-seed.sh — regression pin for tools/bench_seed.py (#276).
#
# WHY THIS EXISTS. A baseline's `context.git_sha` is only worth recording if it
# is TRUE: the full SHA of the commit the measurement was taken at, added without
# disturbing a byte Google Benchmark wrote, and never written from a tree whose
# HEAD does not describe the code that produced the numbers. Each of those is a
# cell here, and the readers of checked-in baselines (tools/bench_compare.py) get
# a cell proving the added key does not change what they compute.
#
# HOW. Every cell runs in a fresh scratch repository under mktemp -d (never
# nested in this checkout), against a hand-written fixture in Google Benchmark's
# own formatting — so a json.load/json.dump round trip visibly changes its bytes.
# Then every MUTANT, applied to a temp copy of the helper (or of the comparator
# for the reader cell), must redden the ONE cell that names its defect. A mutant
# whose substitution matched nothing is a harness failure (cmp guard), never a
# pass: a mutation that did not apply proves nothing.
#
# Buildless and hermetic — python3, git and coreutils. No network.
#
# Usage: ci/test-bench-seed.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SEED_REAL="$repo_root/tools/bench_seed.py"
CMP_REAL="$repo_root/tools/bench_compare.py"

fail() { printf 'FAIL: %b\n' "$1" >&2; exit 1; }
command -v python3 >/dev/null || fail "python3 is required"
command -v git >/dev/null     || fail "git is required"
[ -f "$SEED_REAL" ] || fail "helper not found: $SEED_REAL"
[ -f "$CMP_REAL" ]  || fail "comparator not found: $CMP_REAL"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Hermetic git: no inherited repository, no user/system config.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
# Stop repository discovery at the scratch root, so a TMPDIR that sits inside
# some checkout cannot make S8's "not a repository" find one above it.
export GIT_CEILING_DIRECTORIES="$TMP"
G() { git -c user.name=t -c user.email=t@t -c commit.gpgsign=false "$@"; }

# Checked at the end against what actually ran: a commented-out or early-exiting
# cell changes the total, which is exactly why it is machine-checked.
CELLS_DECLARED=27
cells_run=0

SEED="$SEED_REAL"
CMP="$CMP_REAL"

# ── fixtures ─────────────────────────────────────────────────────────────────
# Formatted as Google Benchmark writes it: two-space indent, `%.17g`-style
# exponents, an escaped non-ASCII character. json.dump would rewrite all three.
# The stddev rows are NONZERO on purpose, so S10 tests the added key alone; a
# baseline row of 0.0 is covered by L-ZERO in ci/test-bench-gate.sh.
gbfix() {
  cat > "$1" <<'JSON'
{
  "context": {
    "date": "2026-10-22T08:00:00+00:00",
    "host_name": "hé",
    "executable": "build/linux-clang-release/bench/x/alpha_bench",
    "num_cpus": 4,
    "mhz_per_cpu": 3593,
    "cpu_scaling_enabled": false,
    "load_avg": [1.5,0.25,0],
    "library_build_type": "release"
  },
  "benchmarks": [
    {
      "name": "BM_A_mean",
      "run_name": "BM_A",
      "run_type": "aggregate",
      "repetitions": 3,
      "threads": 1,
      "aggregate_name": "mean",
      "iterations": 1000,
      "real_time": 1.0000000000000000e+02,
      "cpu_time": 1.0000000000000000e+02,
      "time_unit": "ns"
    },
    {
      "name": "BM_A_median",
      "run_name": "BM_A",
      "run_type": "aggregate",
      "repetitions": 3,
      "threads": 1,
      "aggregate_name": "median",
      "iterations": 1000,
      "real_time": 1.0000000000000000e+02,
      "cpu_time": 1.0000000000000000e+02,
      "time_unit": "ns"
    },
    {
      "name": "BM_A_stddev",
      "run_name": "BM_A",
      "run_type": "aggregate",
      "repetitions": 3,
      "threads": 1,
      "aggregate_name": "stddev",
      "iterations": 1000,
      "real_time": 1.5000000000000000e+00,
      "cpu_time": 1.5000000000000000e+00,
      "time_unit": "ns"
    }
  ]
}
JSON
}

# mkrepo <dir> — two commits (so HEAD~1 exists and differs from HEAD), an
# ignored build/ directory, and the fixture as an UNTRACKED current.json at the
# root: the common seeding shape, which must not count as dirty.
mkrepo() {
  local d="$1"
  mkdir -p "$d/bench/baselines"
  G -C "$d" init -q
  printf 'build/\n' > "$d/.gitignore"
  echo 'int a;' > "$d/src.cpp"
  G -C "$d" add .gitignore src.cpp
  G -C "$d" commit -qm one
  echo 'int b;' >> "$d/src.cpp"
  G -C "$d" commit -qam two
  mkdir -p "$d/build"; echo junk > "$d/build/ignored.o"
  gbfix "$d/current.json"
}

fresh() { R="$(mktemp -d "$TMP/r.XXXXXX")"; mkrepo "$R"; OUT="$R/bench/baselines/alpha.json"; }
seedrun() { python3 "$SEED" --repo "$R" "$@"; }
ctx() { python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["context"].get(sys.argv[2]))' "$1" "$2"; }

# ── cells: each returns 0 when its expectation holds, else prints why ────────

# S1 GREEN — a clean tree seeds, and the key is the FULL SHA of HEAD.
S1() {
  fresh
  seedrun "$R/current.json" "$OUT" >"$TMP/o" 2>&1 || { echo "exit $? : $(cat "$TMP/o")"; return 1; }
  local want got; want="$(G -C "$R" rev-parse HEAD)"; got="$(ctx "$OUT" git_sha)"
  [ "$got" = "$want" ] || { echo "git_sha=$got, rev-parse HEAD=$want"; return 1; }
}

# S2 GREEN — every byte but the added key is the input's. Deleting the one line
# the helper adds must reproduce current.json exactly.
S2() {
  fresh
  seedrun "$R/current.json" "$OUT" >"$TMP/o" 2>&1 || { echo "exit $? : $(cat "$TMP/o")"; return 1; }
  python3 - "$R/current.json" "$OUT" "$(G -C "$R" rev-parse HEAD)" <<'PY' || return 1
import sys
cur, out, sha = (open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read(), sys.argv[3])
line = b'\n    "git_sha": "' + sha.encode() + b'",'
if out.count(line) != 1 or out.replace(line, b"", 1) != cur:
    print("baseline is not current.json plus exactly the git_sha line")
    sys.exit(1)
PY
}

# expect_refused <want-substring> <helper args...> — exit 1, the named reason on
# stderr, and NO baseline written.
expect_refused() {
  local want="$1"; shift
  local rc=0
  seedrun "$@" >"$TMP/o" 2>&1 || rc=$?
  [ "$rc" -eq 1 ] || { echo "exit $rc (wanted 1): $(cat "$TMP/o")"; return 1; }
  grep -qF "$want" "$TMP/o" || { echo "refused for the wrong reason (wanted '$want'): $(cat "$TMP/o")"; return 1; }
  [ ! -e "$OUT" ] || { echo "refused but wrote $OUT anyway"; return 1; }
}

# expect_refused_naming <listing-line> <helper args...> — exit 1, the dirty
# listing carries <listing-line> as a whole line, and $OUT, which the cell put
# there itself, still holds the bytes it had before the run.
expect_refused_naming() {
  local want="$1"; shift
  local rc=0 before
  before="$(sha256sum "$OUT")"
  seedrun "$@" >"$TMP/o" 2>&1 || rc=$?
  [ "$rc" -eq 1 ] || { echo "exit $rc (wanted 1): $(cat "$TMP/o")"; return 1; }
  grep -qF "working tree is dirty" "$TMP/o" || { echo "refused for the wrong reason: $(cat "$TMP/o")"; return 1; }
  grep -qxF -- "$want" "$TMP/o" || { echo "listing does not name the line '$want': $(cat "$TMP/o")"; return 1; }
  [ "$(sha256sum "$OUT")" = "$before" ] || { echo "refused but rewrote $OUT anyway"; return 1; }
}

# S3 RED — a tracked modification is refused.
S3() { fresh; echo 'int c;' >> "$R/src.cpp"; expect_refused "working tree is dirty" "$R/current.json" "$OUT"; }

# S4 RED — an untracked, non-ignored file is refused (a file(GLOB) can build it).
S4() { fresh; echo 'int d;' > "$R/new.cpp"; expect_refused "working tree is dirty" "$R/current.json" "$OUT"; }

# S5 GREEN — --allow-dirty seeds, and records that the tree was dirty.
S5() {
  fresh; echo 'int c;' >> "$R/src.cpp"
  seedrun --allow-dirty "$R/current.json" "$OUT" >"$TMP/o" 2>&1 || { echo "exit $? : $(cat "$TMP/o")"; return 1; }
  [ "$(ctx "$OUT" git_dirty)" = "True" ] || { echo "git_dirty=$(ctx "$OUT" git_dirty)"; return 1; }
  [ "$(ctx "$OUT" git_sha)" = "$(G -C "$R" rev-parse HEAD)" ] || { echo "git_sha wrong under --allow-dirty"; return 1; }
}

# S6 GREEN — a CLEAN tree never carries git_dirty, even with --allow-dirty.
S6() {
  fresh
  seedrun --allow-dirty "$R/current.json" "$OUT" >"$TMP/o" 2>&1 || { echo "exit $? : $(cat "$TMP/o")"; return 1; }
  [ "$(ctx "$OUT" git_dirty)" = "None" ] || { echo "clean tree recorded git_dirty=$(ctx "$OUT" git_dirty)"; return 1; }
}

# S7 RED — an input already carrying git_sha is refused, not overwritten.
S7() {
  fresh
  sed -i 's/^    "date"/    "git_sha": "0000000000000000000000000000000000000000",\n&/' "$R/current.json"
  grep -q '"git_sha"' "$R/current.json" || { echo "fixture edit did not apply"; return 1; }
  expect_refused "already carries context.git_sha" "$R/current.json" "$OUT"
}

# S8 RED — outside a repository there is no SHA to record.
S8() {
  fresh; rm -rf "$R/.git"
  expect_refused "failed in" "$R/current.json" "$OUT"
}

# S9 RED — input not shaped like raw Google Benchmark output is refused, not
# guessed at (here an object precedes `context`).
S9() {
  fresh
  python3 - "$R/current.json" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
open(p, "w").write(s.replace('{\n  "context"', '{\n  "meta": {\n    "k": 1\n  },\n  "context"', 1))
PY
  grep -q '"meta"' "$R/current.json" || { echo "fixture edit did not apply"; return 1; }
  expect_refused "does not open with a \`context\` object" "$R/current.json" "$OUT"
}

# S10 GREEN — the readers tolerate the key: tools/bench_compare.py's legacy
# positional mode and --suite tier 3 both still compute the delta against a
# seeded baseline. (--paired reads results directories, never a baseline.)
S10() {
  fresh
  seedrun "$R/current.json" "$OUT" >"$TMP/o" 2>&1 || { echo "seed exit $? : $(cat "$TMP/o")"; return 1; }
  grep -q '"git_sha"' "$OUT" || { echo "seeded baseline has no git_sha"; return 1; }
  local rc=0
  python3 "$CMP" "$OUT" "$R/current.json" >"$TMP/o" 2>&1 || rc=$?
  [ "$rc" -eq 0 ] && grep -qE 'BM_A_median +100\.00 +100\.00 +\+0\.0%' "$TMP/o" \
    || { echo "legacy mode exit $rc: $(cat "$TMP/o")"; return 1; }
  mkdir -p "$R/results"; cp "$R/current.json" "$R/results/alpha_bench.json"
  printf 'bench/x/alpha_bench   gb-json:alpha.json   no\n' > "$R/manifest.txt"
  rc=0
  python3 "$CMP" --suite "$R/results" --baselines-dir "$R/bench/baselines" \
    --manifest "$R/manifest.txt" >"$TMP/o" 2>&1 || rc=$?
  [ "$rc" -eq 0 ] && grep -qF '1 measurement(s) compared; 0 not compared' "$TMP/o" \
    || { echo "--suite exit $rc: $(cat "$TMP/o")"; return 1; }
}

# S11 RED — a tracked source renamed INTO the output path (index column). The
# destination is the exempt output, so the deletion of the source is what makes
# the tree dirty, and the listing must name it.
S11() { fresh; G -C "$R" mv src.cpp bench/baselines/alpha.json; expect_refused_naming "  D  src.cpp" "$R/current.json" "$OUT"; }

# S12 RED — the same rename made in the WORKTREE column: a plain mv, then
# `git add -N` on the destination.
S12() {
  fresh; mv "$R/src.cpp" "$OUT"; G -C "$R" add -N bench/baselines/alpha.json
  expect_refused_naming "   D src.cpp" "$R/current.json" "$OUT"
}

# S13 RED — an existing baseline renamed into the output: the realistic form,
# which hides the removal of the old baseline the same way.
S13() {
  fresh
  echo '{}' > "$R/bench/baselines/old.json"
  G -C "$R" add bench/baselines/old.json; G -C "$R" commit -qm three
  G -C "$R" mv bench/baselines/old.json bench/baselines/alpha.json
  expect_refused_naming "  D  bench/baselines/old.json" "$R/current.json" "$OUT"
}

CELLS=(S1 S2 S3 S4 S5 S6 S7 S8 S9 S10 S11 S12 S13)

# ── pass 1: the real helper, every cell must hold ───────────────────────────
for c in "${CELLS[@]}"; do
  if ! why="$("$c")"; then fail "$c should hold on the real helper but did not: $why"; fi
  echo "GREEN (expected): $c"
  cells_run=$((cells_run + 1))
done

# ── pass 2: mutants, each must redden its own cell ──────────────────────────
# mutant <id> <cell> <file: seed|cmp> <python old> <python new> [<old> <new>]...
# Every (old, new) pair is applied in order; each old must occur exactly once.
mutant() {
  local id="$1" cell="$2" which="$3"; shift 3
  local src dst
  if [ "$which" = seed ]; then src="$SEED_REAL"; else src="$CMP_REAL"; fi
  dst="$TMP/mut.$id.py"
  python3 - "$src" "$dst" "$@" <<'PY'
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
  if cmp -s "$src" "$dst"; then fail "mutant $id did not change the file — a no-op mutant proves nothing"; fi
  local why rc=0
  if [ "$which" = seed ]; then why="$(SEED="$dst" "$cell")" || rc=$?
  else why="$(CMP="$dst" "$cell")" || rc=$?; fi
  [ "$rc" -ne 0 ] || fail "mutant $id SURVIVED cell $cell"
  printf 'RED (expected): %s under mutant %s — %.300s\n' "$cell" "$id" "$why"
  cells_run=$((cells_run + 1))
}

mutant M-HEAD-1    S1  seed '"rev-parse", "--verify", "HEAD^{commit}"' '"rev-parse", "--verify", "HEAD~1^{commit}"'
mutant M-ROUNDTRIP S2  seed '    return out
' '    return (json.dumps(after, indent=2) + "\n").encode()
'
mutant M-NO-DIRTY  S3  seed 'if dirty and not a.allow_dirty:' 'if False:'
mutant M-NO-UNTRK  S4  seed '"--untracked-files=all"' '"--untracked-files=no"'
mutant M-NO-EXEMPT S1  seed '        if os.path.realpath(os.path.join(top, path)) in exempt_real:' '        if False:'
mutant M-NO-DFLAG  S5  seed '    if dirty:
        span +=' '    if False:
        span +='
mutant M-DFLAG-ALW S6  seed 'out = seed(raw, sha, bool(dirty))' 'out = seed(raw, sha, a.allow_dirty)'
mutant M-OVERWRITE S7  seed 'for key in ("git_sha", "git_dirty"):' 'for key in ():'
mutant M-ANY-KEY   S9  seed "_ANCHOR = re.compile(rb'\\A(\\s*\\{\\s*\"context\"" "_ANCHOR = re.compile(rb'\\A(\\s*\\{\\s*\"[a-z]+\""
mutant M-READER    S10 cmp '    rows = [r for r in data.get("benchmarks", []) or [] if isinstance(r, dict)]
    med =' '    if "git_sha" in (data.get("context") or {}):
        return {}
    rows = [r for r in data.get("benchmarks", []) or [] if isinstance(r, dict)]
    med ='

mutant M-GIT-QUIET  S8  seed 'check=True,' 'check=False,'

# The rename mutants. M-RENAME-HEAD: renames reported as one -z record pair, and
# the source entry skipped when the INDEX column says R or C. M-RENAME-2COL reads both columns and still
# skips the source. M-SKIP-DELETED drops deletion records, on the theory that a
# deleted file cannot enter the build.
mutant M-RENAME-HEAD  S11 seed \
  '"-z", "--no-renames",' '"-z",' \
  '    for e in out.split(b"\0"):' '    entries = iter(out.split(b"\0"))
    for e in entries:' \
  '        status, path = e[:2].decode(), e[3:].decode(errors="surrogateescape")
' '        status, path = e[:2].decode(), e[3:].decode(errors="surrogateescape")
        if status[0] in "RC":
            next(entries, None)
'
mutant M-RENAME-2COL  S12 seed \
  '"-z", "--no-renames",' '"-z",' \
  '    for e in out.split(b"\0"):' '    entries = iter(out.split(b"\0"))
    for e in entries:' \
  '        status, path = e[:2].decode(), e[3:].decode(errors="surrogateescape")
' '        status, path = e[:2].decode(), e[3:].decode(errors="surrogateescape")
        if status[0] in "RC" or status[1] in "RC":
            next(entries, None)
'
mutant M-SKIP-DELETED S13 seed \
  '        status, path = e[:2].decode(), e[3:].decode(errors="surrogateescape")
' '        status, path = e[:2].decode(), e[3:].decode(errors="surrogateescape")
        if "D" in status:
            continue
'

# ── declared vs run ──────────────────────────────────────────────────────────
if [ "$cells_run" -ne "$CELLS_DECLARED" ]; then
  fail "declared $CELLS_DECLARED cells but ran $cells_run"
fi
echo
echo "PASS: bench seed pin — $cells_run/$CELLS_DECLARED cells, every mutant reddened its own cell."
