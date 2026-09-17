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
elif ! printf '%s\n' "$t11_out" | grep -qF "push-trigger check did NOT run"; then
  printf '%s\n' "$t11_out" | sed 's/^/  | /'
  bad "T11 PyYAML absent exited 2 but the push-trigger check did not say it stood down"
else
  ok "T11 PyYAML absent fails closed instead of reporting the all-clear"
fi

# ── T12: a campaign job missing its production lane's job-level env ──────────
#
# ⚠️ NOT HYPOTHETICAL — THIS EXACT OMISSION SHIPPED AND COST A DISPATCH. The
# campaign's `windows` job copied every STEP of tier2's faithfully and none of
# its job-level `env:`; `ci/restore-sccache.sh` then refused with "SCCACHE_DIR
# must be set (the workflow sets it job-wide)", 20 minutes into a build, on the
# lane the campaign most needs. Production fidelity is not only about the step
# list, and "I copied it carefully" is precisely the claim that failed.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import re, sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
new, n = re.subn(r"^      SCCACHE_DIR: .*\n", "", s, count=1, flags=re.M)
assert n == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(new, encoding="utf-8")
MUT
expect "T12 a campaign job missing its lane's job-level env is caught" 1 "MISSING JOB-LEVEL ENV"

# ── T13: the source job renamed out from under the check ─────────────────────
#
# A check whose subject disappears must not report clean. Renaming the tier job
# the campaign mirrors would otherwise leave the comparison silently unmade.
fresh
python3 - "$WORK/t/.github/workflows/tier2.yml" <<'MUT'
import re, sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
new, n = re.subn(r"^  windows:$", "  windows_renamed:", s, count=1, flags=re.M)
assert n == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(new, encoding="utf-8")
MUT
expect "T13 a renamed source job stops the check loudly, not silently" 1 "UNCHECKABLE"

# ── T14: the plan job's lane parser, EXTRACTED FROM THE WORKFLOW ─────────────
#
# ⚠️ THE EMPTY-LIST CASE KILLED A REAL DISPATCH. `to_json` used to end
# `| grep -v '^$' | python3 ...`, and grep exits 1 when its input is empty —
# which under the step's `set -euo pipefail` aborted the whole plan job the
# moment any lane list was left blank. That is the NORMAL way to dispatch: a
# campaign usually names one or two platforms, not three. The first dispatch
# named all three and passed; the second named two and died.
#
# ⚠️ AND IT WAS INVISIBLE LOCALLY — this repo's dev shell is zsh, whose `set -e`
# does not fire on that assignment; CI runs bash. So this cell runs the function
# under an explicit `bash -c`, not in whatever shell the harness was invoked
# from, and the function is EXTRACTED FROM THE WORKFLOW rather than retyped: a
# copy here would drift from the thing that actually runs.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" "$WORK/tojson.sh" <<'EXTRACT'
import re, sys, pathlib
src = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
m = re.search(r"^          to_json\(\) \{\n.*?^          \}", src, re.M | re.S)
assert m, "EXTRACTION FAILED — to_json not found; re-point this cell, do not delete it"
body = "".join(l[10:] for l in m.group(0).splitlines(keepends=True))
assert "python3" in body, "extracted a to_json that does not look like the real one"
pathlib.Path(sys.argv[2]).write_text("set -euo pipefail\n" + body + "\n")
EXTRACT
# ⚠️ BARE ASSIGNMENTS, matching the workflow's `L="$(to_json "$LINUX")"`. The first
# version of this cell put the calls inside `printf` ARGUMENTS, where bash
# DISCARDS a command substitution's exit status — so `set -e` never fired and
# the cell stayed green with the bug deliberately re-introduced. A test whose
# SHAPE differs from production's cannot see production's defect; proving the
# cell reddens is what caught that.
t14_out="$(bash -c 'set -euo pipefail
  . "'"$WORK"'/tojson.sh"
  e="$(to_json "")"
  s1="$(to_json "linux-clang-asan")"
  d="$(to_json "a,a,b")"
  w="$(to_json " b , a ")"
  echo "empty=$e single=$s1 dup=$d ws=$w"
' 2>&1)"; t14_rc=$?
if [ "$t14_rc" -ne 0 ]; then
  printf '%s\n' "$t14_out" | sed 's/^/  | /'
  bad "T14 the shipped to_json ABORTS under bash (exit $t14_rc) — a blank lane list kills the plan job"
elif [ "$t14_out" != 'empty=[] single=["linux-clang-asan"] dup=["a", "b"] ws=["a", "b"]' ]; then
  printf '%s\n' "$t14_out" | sed 's/^/  | /'
  bad "T14 the shipped to_json produced unexpected output"
else
  ok "T14 the shipped to_json handles empty/single/duplicate/whitespace lane lists under bash"
fi

# ── T15: the linux job's ccache restore step deleted ─────────────────────────
#
# #411 Gate B r1 F4 (parallelism-measure half): the campaign's `linux`/`libcxx`
# jobs restore Tier 1's GHCR compiler cache, restore-only, and nothing in this
# repo pinned that at all — ci/test-tier1-python-policy.sh only reads
# tier1.yml. Deleting the restore silently returns the lane to a cold build.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = '''      - name: Restore ccache from GHCR (never published from here)
        run: |
          echo "${{ secrets.GITHUB_TOKEN }}" | oras login ghcr.io -u "${{ github.actor }}" --password-stdin || true
          ci/restore-ccache.sh ${{ matrix.preset }}

'''
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "", 1), encoding="utf-8")
MUT
expect "T15 the parallelism linux job's ccache restore deleted is caught" 1 "CCACHE RESTORE MISWIRED"

# ── T16: a seed call added to a measurement job ──────────────────────────────
#
# A measurement job must never publish to the shared compiler cache — an entry
# it published would be served to a production lane it does not represent.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
start = s.index("\n  linux:\n")
end = s.index("\n  libcxx:\n", start)
before, job, after = s[:start], s[start:end], s[end:]
anchor = "          ci/restore-ccache.sh ${{ matrix.preset }}\n"
assert job.count(anchor) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
seed = "      - name: Save ccache to GHCR (never — measurement must not publish)\n        run: ci/seed-ccache.sh ${{ matrix.preset }}\n"
job = job.replace(anchor, anchor + seed, 1)
p.write_text(before + job + after, encoding="utf-8")
MUT
expect "T16 a seed call added to a parallelism measurement job is caught" 1 "CCACHE SEED IN A MEASUREMENT JOB"

# ── T17: the restore moved after Conan install ───────────────────────────────
#
# restore-ccache.sh refuses once anything has compiled through the launcher;
# moving the restore after Conan install would discard the just-built objects
# (or, if it does not refuse, waste the compile that already happened cold).
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
start = s.index("\n  linux:\n")
end = s.index("\n  libcxx:\n", start)
before, job, after = s[:start], s[start:end], s[end:]
i = job.index("      - name: Restore ccache from GHCR (never published from here)\n")
j = job.index("          ci/restore-ccache.sh ${{ matrix.preset }}\n", i) + len("          ci/restore-ccache.sh ${{ matrix.preset }}\n")
restore = job[i:j]
assert restore, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
job = job[:i] + job[j:]
conan = "      - name: Conan install\n"
assert job.count(conan) == 1, job.count(conan)
k = job.index(conan)
k = job.index("\n\n", k) + 2
job = job[:k] + restore + "\n" + job[k:]
p.write_text(before + job + after, encoding="utf-8")
MUT
expect "T17 the parallelism linux restore moved after Conan install is caught" 1 "CCACHE RESTORE OUT OF ORDER"

# ── T18: if: false added to the linux restore ────────────────────────────────
#
# #411 Gate B r2 F3: the r1 checker found this step by the substring
# `RESTORE_SCRIPT in run`, so a disabled-but-present step still counted as
# "restoring". The step's key set is now compared as a canonical object.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "      - name: Restore ccache from GHCR (never published from here)\n        run: |\n"
new = "      - name: Restore ccache from GHCR (never published from here)\n        if: false\n        run: |\n"
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, new, 1), encoding="utf-8")
MUT
expect "T18 if: false added to the parallelism linux restore is caught" 1 "CCACHE RESTORE KEY SET DRIFT"

# ── T19: exit 0 inserted before the linux restore invocation ────────────────
#
# The restore step still contains the text `ci/restore-ccache.sh`, so the
# substring-only r1 check saw a live restore; the call is unreachable.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
start = s.index("\n  linux:\n")
end = s.index("\n  libcxx:\n", start)
before, job, after = s[:start], s[start:end], s[end:]
old = "          ci/restore-ccache.sh ${{ matrix.preset }}\n"
new = "          exit 0\n          ci/restore-ccache.sh ${{ matrix.preset }}\n"
assert job.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
job = job.replace(old, new, 1)
p.write_text(before + job + after, encoding="utf-8")
MUT
expect "T19 exit 0 inserted before the parallelism linux restore call is caught" 1 "CCACHE RESTORE RUN TEXT DRIFT"

# ── T20: the libcxx restore preset drifts from matrix.preset ────────────────
#
# Only `linux`'s preset argument was ever checked before this round; the
# libcxx job's own call was unpinned.
fresh
python3 - "$WORK/t/.github/workflows/parallelism-measure.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
start = s.index("\n  libcxx:\n")
end = s.index("\n  windows:\n", start)
before, job, after = s[:start], s[start:end], s[end:]
old = "          ci/restore-ccache.sh ${{ matrix.preset }}\n"
new = "          ci/restore-ccache.sh linux-clang-debug\n"
assert job.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
job = job.replace(old, new, 1)
p.write_text(before + job + after, encoding="utf-8")
MUT
expect "T20 the parallelism libcxx restore preset drifts from matrix.preset is caught" 1 "CCACHE RESTORE RUN TEXT DRIFT"

# ── T21-T25: #465 — push-admitting publish guards trust the push trigger ─────
#
# A guard whose `push` arm admits `github.event_name == 'push'` without
# re-checking `github.ref` is main-only only through `on.push.branches`, so a
# widened trigger admits a non-main push the guard still treats as trusted.
# T21 widens tier2's branches to include a feature branch; T22 adds a `tags:`
# key under tier3's push trigger; T23 removes tier3's `branches:`, leaving an
# unfiltered push; T24 is a new, unlisted workflow whose guard still matches
# the idiom with a bare `push` trigger; T25 removes the idiom's literal from
# every workflow, leaving zero guards for the check to find.
fresh
python3 - "$WORK/t/.github/workflows/tier2.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = '  push:\n    branches: ["main"]\n'
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, '  push:\n    branches: ["main", "feature/**"]\n', 1), encoding="utf-8")
MUT
expect "T21 tier2 push trigger broadened to a feature branch is caught" 1 "PUSH TRIGGER NOT MAIN-ONLY: tier2.yml"

fresh
python3 - "$WORK/t/.github/workflows/tier3-libcxx.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = '  push:\n    branches: ["main"]\n'
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, old + '    tags: ["v*"]\n', 1), encoding="utf-8")
MUT
expect "T22 tags: added under tier3's push trigger is caught" 1 "PUSH TRIGGER NOT MAIN-ONLY: tier3-libcxx.yml: on.push carries tags"

# Dropping `branches:` leaves only paths-ignore, which fires on EVERY branch.
fresh
python3 - "$WORK/t/.github/workflows/tier3-libcxx.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = '  push:\n    branches: ["main"]\n'
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, '  push:\n', 1), encoding="utf-8")
MUT
expect "T23 tier3 push trigger with branches: removed is caught" 1 "on.push.branches is None"

# A NEW workflow, not on the roster, is caught while its guard still uses the
# exact `github.event_name` + quoted `push` idiom — nobody has to add it to a
# list.
fresh
cat > "$WORK/t/.github/workflows/new-publisher.yml" <<'WF'
name: new publisher
on: push
jobs:
  seed:
    runs-on: ubuntu-latest
    steps:
      - name: Save ccache to GHCR
        if: github.event_name == 'push' || github.event_name == 'workflow_dispatch'
        run: ci/seed-ccache.sh linux-clang-debug
WF
expect "T24 a new workflow with a push guard and a bare push trigger is caught" 1 "PUSH TRIGGER NOT MAIN-ONLY: new-publisher.yml: \`push\` has no filters"

# Zero push-admitting workflows is an instrument failure, not a pass.
fresh
python3 - "$WORK/t/.github/workflows" <<'MUT'
import sys, pathlib, re
n = 0
for p in pathlib.Path(sys.argv[1]).glob("*.yml"):
    s = p.read_text(encoding="utf-8")
    t, k = re.subn(r"""github\.event_name == 'push'""", "github.event_name == 'pushed'", s)
    n += k
    p.write_text(t, encoding="utf-8")
assert n >= 9, f"MUTATION DID NOT APPLY ({n} sites) — re-point the pattern, do not delete the mutant"
MUT
expect "T25 zero push-admitting workflows is an instrument failure, not a pass" 2 "ZERO workflows whose expressions admit a \`push\` event"

# ── T26: #465 F1 — the roster is checked even when a workflow's guard no
# longer matches the derived idiom ──────────────────────────────────────────
#
# The population used to be derived only: a workflow entered scope while its
# strings paired `github.event_name` with a quoted `push` literal. A guard
# respelled away from that literal removed the workflow from scope even if
# its trigger was widened at the same time. PUSH_TRUSTING_ROSTER closes that:
# tier2.yml is checked whether or not its guard still matches the idiom.
fresh
python3 - "$WORK/t/.github/workflows/tier2.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old_guard = "github.event_name == 'push' ||"
new_guard = "github.event_name != 'pull_request' ||"
n = s.count(old_guard)
assert n == 2, f"MUTATION DID NOT APPLY ({n} sites) — re-point the pattern, do not delete the mutant"
s = s.replace(old_guard, new_guard)
old_branches = '  push:\n    branches: ["main"]\n'
assert s.count(old_branches) == 1, "MUTATION DID NOT APPLY (branches) — re-point the pattern, do not delete the mutant"
s = s.replace(old_branches, '  push:\n    branches: ["main", "feature/**"]\n', 1)
p.write_text(s, encoding="utf-8")
MUT
expect "T26 a roster member is caught even when its guard no longer matches the idiom" 1 "PUSH TRIGGER NOT MAIN-ONLY: tier2.yml"

# ── T27: #465 F2 — a NEW workflow using LIST-FORM `on: [push, ...]` is caught
# by the list-normalisation arm, not treated as vacuous ─────────────────────
fresh
cat > "$WORK/t/.github/workflows/list-form-publisher.yml" <<'WF'
name: list form publisher
on: [push, workflow_dispatch]
jobs:
  seed:
    runs-on: ubuntu-latest
    steps:
      - name: Save ccache to GHCR
        if: github.event_name == 'push' || github.event_name == 'workflow_dispatch'
        run: ci/seed-ccache.sh linux-clang-debug
WF
expect "T27 a new workflow with list-form on: [push, ...] is caught, not read as vacuous" 1 "PUSH TRIGGER NOT MAIN-ONLY: list-form-publisher.yml: \`push\` has no filters"

# ── T6: THE EMPTY SCAN ───────────────────────────────────────────────────────
#
# If the workflows move or the patterns break, "0 violations over 0 sites" must
# not read as a clean tree.
rm -rf "$WORK/t"; mkdir -p "$WORK/t/.github/workflows"
cp "$REPO/CMakePresets.json" "$WORK/t/"
printf 'name: nothing\non: push\njobs: {}\n' > "$WORK/t/.github/workflows/empty.yml"
expect "T6 an empty scan is an instrument failure, not a pass" 2 "ZERO apt-backed install sites"

# ── T28-T34: fixpp#431 Gate B r1 (Codex #5/#4a P2) — the interop gate step's
# static wiring, in each of the three tier workflows. Codex #1's CRLF defect
# and the CR-normalisation/exactly-once/ctest-failure-annotation fixes for it
# are exercised by EXECUTING the extracted run: text in
# ci/test-interop-gate-step.sh; these cells are the static leg-guard/
# continue-on-error/label/pin-read/checker-call/identity shape only. ─────────

# T28 (M-RC1f): the step's `if:` leg guard drifts to a different preset.
fresh
python3 - "$WORK/t/.github/workflows/tier1.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = ('- name: "Interop gate — ctest -L interop, skip set asserted (#431)"\n'
       "        if: matrix.preset == 'linux-clang-release'\n")
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
new = old.replace("linux-clang-release'\n", "linux-clang-debug'\n")
p.write_text(s.replace(old, new, 1), encoding="utf-8")
MUT
expect "T28 tier1 interop gate step's if: preset guard drifts (M-RC1f) is caught" 1 "INTEROP GATE STEP GUARD DRIFT: tier1.yml"

# T29 (M-RC1e): continue-on-error added to the gate step.
fresh
python3 - "$WORK/t/.github/workflows/tier2.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = ('- name: "Interop gate — ctest -L interop, skip set asserted (#431)"\n'
       "        if: matrix.preset == 'windows-msvc-release'\n"
       "        shell: bash\n")
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
new = old + "        continue-on-error: true\n"
p.write_text(s.replace(old, new, 1), encoding="utf-8")
MUT
expect "T29 tier2 interop gate step gains continue-on-error (M-RC1e) is caught" 1 "INTEROP GATE STEP TOLERATES FAILURE: tier2.yml"

# T30 (M-RC1d): the registration ctest call's label drifts to -L interopX.
fresh
python3 - "$WORK/t/.github/workflows/tier3-libcxx.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "ctest --preset ${{ matrix.preset }} -L interop -N"
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, old.replace("-L interop", "-L interopX"), 1), encoding="utf-8")
MUT
expect "T30 tier3 registration ctest call's label drifts to -L interopX (M-RC1d) is caught" 1 "INTEROP GATE STEP LABEL DRIFT: tier3-libcxx.yml"

# T31: the gate step stops reading the pin file at all (hardcodes `expected`).
fresh
python3 - "$WORK/t/.github/workflows/tier1.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = ("          expected=$(tr -d '\\r' < ci/expected-interop-tests.txt \\\n"
       "                       | awk -v p=\"${{ matrix.preset }}\" '$1 == p { print $2; exit }')\n")
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "          expected=30\n", 1), encoding="utf-8")
MUT
expect "T31 tier1 interop gate step stops reading the pin file is caught" 1 "INTEROP GATE STEP PIN READ MISSING: tier1.yml"

# T32: the checker invocation drops --expected-count.
fresh
python3 - "$WORK/t/.github/workflows/tier2.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = '            --expected-count "$binaries"\n'
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "", 1), encoding="utf-8")
MUT
expect "T32 tier2 checker invocation drops --expected-count is caught" 1 "INTEROP GATE STEP CHECKER CALL DRIFT: tier2.yml"

# T33: the gate step is renamed away — zero steps match the pinned name.
fresh
python3 - "$WORK/t/.github/workflows/tier1.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = '- name: "Interop gate — ctest -L interop, skip set asserted (#431)"'
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, '- name: "Interop gate (renamed)"', 1), encoding="utf-8")
MUT
expect "T33 tier1 interop gate step renamed away is caught" 1 "INTEROP GATE STEP MISWIRED: tier1.yml has 0 step(s)"

# T34: tier3's body drifts from tier1's byte-identical text (both run under
# python3 with no cygpath, so they must match exactly).
fresh
python3 - "$WORK/t/.github/workflows/tier3-libcxx.yml" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = 'echo "::error title=Interop gate::ctest -L interop failed on ${{ matrix.preset }}."'
assert s.count(old) == 1, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, 'echo "::error title=Interop gate::ctest failed on ${{ matrix.preset }}."', 1), encoding="utf-8")
MUT
expect "T34 tier3 interop gate body drifts from tier1's byte-identical text is caught" 1 "INTEROP GATE STEP DRIFT: tier1.yml and tier3-libcxx.yml"

# ── The harness's own execution count ────────────────────────────────────────
#
# ⚠️ ADDED WITH THE FOUR NEW CELLS, and the omission is the point: a `cell`
# invocation lost to an editing slip removes a gate SILENTLY, and the tally
# below would still read "N passed, 0 failed" for a smaller N. Both sibling
# harnesses in this directory assert their count; this one did not, and four
# cells were added to it before anyone noticed. T15-T17 (#411 Gate B r1 F4)
# added the parallelism-measure ccache-restore-wiring cells; T18-T20 (#411
# Gate B r2 F3) added the false-greens the r1 checker's substring match still
# admitted (a disabled step, an unreachable call, and libcxx preset drift).
# T21-T25 (#465) added the push-trigger cells for push-admitting publish guards.
# T26 (#465 Gate B r1 F1) added the roster-floor cell — a roster member whose
# guard is respelled away from the idiom must still be caught. T27 (#465 Gate
# B r1 F2) added the list-form `on:` cell the per-line assessment had claimed
# without a driving test. T28-T34 (fixpp#431 Gate B r1) added the interop
# gate step's static wiring cells.
CELLS_DECLARED=36
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
