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

# ── T7: the campaign gets an automatic trigger ───────────────────────────────
#
# .github/workflows/parallelism-measure.yml runs each named lane's suite THREE
# times — ~77 min per pass on the slowest lane. One `push:` key copy-pasted in
# from a sibling workflow multiplies the CI bill and NOTHING goes red to say so:
# the runs all succeed. Its trigger block is a correctness property, which is why
# it is asserted rather than trusted.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "on:\n  workflow_dispatch:\n"
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "on:\n  push:\n    branches: [main]\n  workflow_dispatch:\n", 1),
             encoding="utf-8")
MUT
expect "T7 an automatic trigger on the campaign workflow is caught" 1 "NO LONGER DISPATCH-ONLY"

# ── T8: the `on:` key is YAML 1.1's boolean True, not the string "on" ─────────
#
# THE TRAP THIS CELL PINS. `yaml.safe_load` resolves a bare `on:` key to the
# BOOLEAN True. A check that looked up only doc["on"] would find nothing,
# conclude the workflow had no triggers, and pass — silently, forever. Quoting
# the key turns it back into a string, which must ALSO be handled; if either
# lookup is lost in a future edit, this cell reddens.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import re, sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
# Replace the WHOLE trigger block, not just its first two lines: leaving the
# `inputs:` mapping orphaned produces a file that does not parse, which is a
# different finding (T8b) and would not test the quoted-key path at all.
new, n = re.subn(r"(?ms)^on:\n.*?(?=^permissions:)",
                 '"on":\n  schedule:\n    - cron: "0 3 * * *"\n\n', s)
assert n == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(new, encoding="utf-8")
MUT
expect "T8 a quoted \"on\" key is still read (YAML 1.1 boolean trap)" 1 "NO LONGER DISPATCH-ONLY"

# ── T8b: a trigger block that does not parse ─────────────────────────────────
#
# Found by a mutant, not by reading: an unparsable workflow raised a traceback
# out of the checker instead of being dispositioned. It is exactly the state
# where "dispatch-only" cannot be asserted — and a workflow that does not parse
# does not run at all — so it must fail closed.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "on:\n  workflow_dispatch:\n"
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "on:\n  schedule:\n    - cron: \"0 3 * * *\"\n", 1), encoding="utf-8")
MUT
expect "T8b an unparsable trigger block fails closed, not with a traceback" 1 "does not parse as YAML"

# ── T9: the campaign workflow is retired ─────────────────────────────────────
#
# Absence must NOT be a violation — retiring a one-off campaign is a legitimate
# thing to do — but it must not be silent either. A check that reports clean
# over a subject that is not there is the failure this whole directory exists to
# remove, so it discloses and stands down.
fresh
rm -f "$WORK/t/.github/workflows/parallelism-measure.yml"
expect "T9 a retired campaign workflow stands down with a disclosure" 0 "check stood down"

# ── T10: the sccache pin bumped in one file and not the other ────────────────
#
# `parallelism-measure.yml` duplicates tier2.yml's `Install sccache` step, pinned
# version and SHA-256 included — the repo has no composite actions, so the tier
# workflows already duplicate their setup between themselves. What must not be
# duplicated silently is a PIN: a bump applied to one file and not the other
# still builds, on a different sccache than the lane it mirrors, and the stale
# copy is whichever file the bumper was not looking at.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "ver=v0.17.0"
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "ver=v0.18.0", 1), encoding="utf-8")
MUT
expect "T10 an sccache pin bumped in one file only is caught" 1 "SCCACHE PIN DISAGREEMENT"

# ── T11: PyYAML missing must not read as "all invariants hold" ───────────────
#
# The campaign-trigger check needs PyYAML, and with it absent it warned and
# returned while main() printed the all-clear and exited 0 — over a tree whose
# campaign workflow was push:-triggered. A `::warning::` does not fail a job.
#
# ⚠️ AND IT WAS NOT LIVE ONLY BY STEP ORDERING. PyYAML reaches ci-script-pins
# from an UNRELATED earlier step in tier1.yml, which pip-installs it for a
# different pin entirely. Reorder or retire that step and this invariant would
# have stood down in silence.
fresh
YAMLGONE="$WORK/noyaml"; mkdir -p "$YAMLGONE"
echo 'raise ImportError("PyYAML deliberately unavailable for cell T11")' > "$YAMLGONE/yaml.py"
t11_out="$(PYTHONPATH="$YAMLGONE" python3 "$CHECK" "$WORK/t" 2>&1)"; t11_rc=$?
if [ "$t11_rc" -ne 2 ]; then
  printf '%s\n' "$t11_out" | sed 's/^/  | /'
  bad "T11 PyYAML absent — expected exit 2, got $t11_rc"
# ⚠️ ANCHORED TO THE SUMMARY LINE. An unanchored `grep -F "all invariants hold"`
# matched the checker's own ERROR message, which QUOTES the phrase it is
# refusing to print — the probe tripping over its own diagnostic, and a cell
# that reds on a correct tree. Only the summary line means the check passed.
elif printf '%s\n' "$t11_out" | grep -q "^ci lane policy: all invariants hold"; then
  printf '%s\n' "$t11_out" | sed 's/^/  | /'
  bad "T11 PyYAML absent still reported the all-clear"
elif ! printf '%s\n' "$t11_out" | grep -qF "could not be evaluated"; then
  printf '%s\n' "$t11_out" | sed 's/^/  | /'
  bad "T11 PyYAML absent exited 2 but without saying which check did not run"
else
  ok "T11 PyYAML absent fails closed instead of reporting the all-clear"
fi

# ── T6: THE EMPTY SCAN ───────────────────────────────────────────────────────
#
# If the workflows move or the patterns break, "0 violations over 0 sites" must
# not read as a clean tree.
rm -rf "$WORK/t"; mkdir -p "$WORK/t/.github/workflows"
cp "$REPO/CMakePresets.json" "$WORK/t/"
printf 'name: nothing\non: push\njobs: {}\n' > "$WORK/t/.github/workflows/empty.yml"
expect "T6 an empty scan is an instrument failure, not a pass" 2 "ZERO apt-backed install sites"

# ── The harness's own execution count ────────────────────────────────────────
#
# ⚠️ ADDED WITH THE FOUR NEW CELLS, and the omission is the point: a `cell`
# invocation lost to an editing slip removes a gate SILENTLY, and the tally
# below would still read "N passed, 0 failed" for a smaller N. Both sibling
# harnesses in this directory assert their count; this one did not, and four
# cells were added to it before anyone noticed.
CELLS_DECLARED=13
TOTAL=$((PASS + FAIL))
echo
if [ "$TOTAL" -ne "$CELLS_DECLARED" ]; then
  echo "ci-lane-policy harness: EXECUTION COUNT MISMATCH — ran ${TOTAL} cells, declared ${CELLS_DECLARED}."
  echo "A cell was added or lost without updating CELLS_DECLARED. Refusing to report a result."
  exit 1
fi
echo "ci-lane-policy harness: ${PASS} passed, ${FAIL} failed (${TOTAL} cells)"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
