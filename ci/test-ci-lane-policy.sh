#!/usr/bin/env bash
# Regression harness for ci/assert-ci-lane-policy.py (#300 callers, #213 fuzz lane).
#
# Run by the `ci-script-pins` job in tier1.yml, and locally with:
#   ci/test-ci-lane-policy.sh
#
# ── WHY THIS FILE EXISTS ─────────────────────────────────────────────────────
#
# Both invariants the checker asserts were TRUE when they were written and
# pinned by NOTHING. A hostile review of PR #369 named exactly that: the trees
# were complete, but "that completeness is nevertheless an unpinned result".
#
# So the checker exists — and a checker is itself an instrument, which in this
# repo means it is not trusted until it has been seen to report non-zero. Each
# cell below breaks one invariant against a COPY of the real tree and requires
# the named diagnostic, not merely a non-zero exit: a cell that fails for the
# wrong reason is not a passing cell.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CHECK="$HERE/assert-ci-lane-policy.py"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }

# A copy of just the two surfaces the checker reads.
fresh() {
  rm -rf "$WORK/t"
  mkdir -p "$WORK/t/.github/workflows"
  cp "$REPO"/.github/workflows/*.yml "$WORK/t/.github/workflows/"
  cp "$REPO/CMakePresets.json" "$WORK/t/"
}

# $1 = cell, $2 = expected exit, $3 = expected message fragment
expect() {
  local name="$1" want="$2" frag="$3" out rc=0
  out="$(python3 "$CHECK" "$WORK/t" 2>&1)" || rc=$?
  if [ "$rc" -ne "$want" ]; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$name — expected exit $want, got $rc"; return
  fi
  if ! printf '%s\n' "$out" | grep -q -- "$frag"; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$name — exited $rc but WITHOUT '$frag' (it failed for the wrong reason)"; return
  fi
  ok "$name"
}

echo "== ci lane policy =="

# ── T0: the shipped tree satisfies both invariants ───────────────────────────
fresh
expect "T0 the real tree satisfies both invariants" 0 "all invariants hold"

# ── T1: a bare apt install added to a workflow ───────────────────────────────
#
# The #300 escape the wrapper's own harness cannot see: ci/test-apt-guard.sh
# tests the WRAPPER and never looks at the callers, so this leaves all its cells
# green while "every apt-backed install is bounded" quietly becomes false.
fresh
python3 - "$WORK/t/.github/workflows/tier1.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "      - name: Set up oras\n"
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
new = "      - name: Smuggled install\n        run: sudo apt-get install -y cowsay\n\n" + old
p.write_text(s.replace(old, new, 1), encoding="utf-8")
MUT
expect "T1 a bare apt-get install added to a workflow is caught" 1 "UNGUARDED INSTALL"

# ── T2: a bare llvm.sh toolchain install ─────────────────────────────────────
#
# `llvm.sh <N> all` is an apt operation wearing a different hat, and the
# heaviest one. It must be caught by the same census.
fresh
python3 - "$WORK/t/.github/workflows/abi-golden.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "ci/apt-guard.sh llvm-toolchain -- sudo /tmp/llvm.sh 22 all"
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "sudo /tmp/llvm.sh 22 all", 1), encoding="utf-8")
MUT
expect "T2 an llvm.sh install stripped of its guard is caught" 1 "UNGUARDED INSTALL"

# ── T3: the fuzz flag turned off ─────────────────────────────────────────────
#
# THE ESCAPE THE CMAKE GUARD CANNOT COVER. Every corpus replay and the
# zero-registration FATAL_ERROR live under `if(FIXPP_BUILD_FUZZ)`, so flipping
# the flag stops all of them being evaluated rather than tripping any. The lane
# returns to replaying zero seeds with every script gate still green.
fresh
python3 - "$WORK/t/CMakePresets.json" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = '"FIXPP_BUILD_FUZZ": "ON"'
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, '"FIXPP_BUILD_FUZZ": "OFF"', 1), encoding="utf-8")
MUT
expect "T3 FIXPP_BUILD_FUZZ flipped OFF is caught" 1 "FUZZ REPLAYS DISABLED"

# ── T4: the fuzz flag removed entirely ───────────────────────────────────────
fresh
python3 - "$WORK/t/CMakePresets.json" <<'MUT'
import sys, pathlib, re
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = '        "FIXPP_BUILD_FUZZ": "ON",\n'
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "", 1), encoding="utf-8")
MUT
expect "T4 FIXPP_BUILD_FUZZ removed altogether is caught" 1 "FUZZ REPLAYS DISABLED"

# ── T5: the fuzz lane dropped from the matrix ────────────────────────────────
#
# The flag being ON is moot if nothing runs the lane. Same defect, one level up.
fresh
python3 - "$WORK/t/.github/workflows/tier1.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "          - linux-clang-asan\n"
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "", 1), encoding="utf-8")
MUT
expect "T5 the fuzz lane dropped from the tier1 matrix is caught" 1 "NOT IN THE MATRIX"

# ── T6: THE EMPTY SCAN ───────────────────────────────────────────────────────
#
# If the workflows move or the patterns break, "0 violations over 0 sites" must
# not read as a clean tree.
rm -rf "$WORK/t"; mkdir -p "$WORK/t/.github/workflows"
cp "$REPO/CMakePresets.json" "$WORK/t/"
printf 'name: nothing\non: push\njobs: {}\n' > "$WORK/t/.github/workflows/empty.yml"
expect "T6 an empty scan is an instrument failure, not a pass" 2 "ZERO apt-backed install sites"

echo
echo "ci-lane-policy harness: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
