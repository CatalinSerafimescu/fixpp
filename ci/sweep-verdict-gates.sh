#!/usr/bin/env bash
# Prove that EVERY gate in ci/parallelism-verdict.py is covered by a named cell.
#
#   ci/sweep-verdict-gates.sh
#
# ── WHY THIS EXISTS ──────────────────────────────────────────────────────────
#
# ci/test-parallelism-verdict.sh asserts its own cell count, so a cell cannot be
# LOST silently. It cannot tell you a gate was ADDED and never covered — the
# count goes up, every cell passes, and the harness certifies a checker one of
# whose gates nothing watches. That is the "certifying a checker that isn't
# there" shape, one level up from the one the harness was written to prevent.
#
# This is the inverse sweep, and it is not a hypothetical: an adversarial review
# ran exactly this against a harness reporting 39/39 green and found TWO gates
# whose removal changed nothing. One was load-bearing and reachable from a typo
# in a dispatch box (a `1,1,1` job shape is not an A-B-A at all, and without that
# gate the page still read VALID). The other was dead code that could not be
# tested because it could not fire.
#
# Method: neuter one `instrument/defects/voids.append(...)` at a time in a COPY,
# run the shipped harness against the neutered checker, and require some cell to
# go red. A gate whose removal leaves the harness green is reported by line and
# by its first line of text.
#
# ⚠️ NOT WIRED INTO CI, DELIBERATELY. It runs the whole harness once per gate,
# so its cost is the harness times the gate count — a few minutes — on a job
# (`ci-script-pins`) that already dominates a pre-gate PR's wall clock and runs
# on every push. Run it BY HAND when adding or changing a gate. The condition it
# checks does not drift on its own: only editing the verdict can break it.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
cp -r "$HERE" "$WORK/ci"
CHECK="$WORK/ci/parallelism-verdict.py"
cp "$CHECK" "$WORK/pristine.py"

mapfile -t LINES < <(grep -n '^\s*\(instrument\|defects\|voids\)\.append(' "$CHECK" | cut -d: -f1)
if [ "${#LINES[@]}" -eq 0 ]; then
  echo "::error::found ZERO gates to neuter. Either the append() shape changed or this"
  echo "sweep's pattern is broken; refusing to report a clean sweep over nothing."
  exit 2
fi
echo "gates found: ${#LINES[@]}"

# Prove the harness is green BEFORE any mutation, or every 'redden' below is
# meaningless — the baseline is the control arm.
if ! ( cd "$WORK" && bash ci/test-parallelism-verdict.sh >/dev/null 2>&1 ); then
  echo "::error::the harness is RED on the unmutated copy. Fix that first; a sweep"
  echo "against a failing baseline cannot attribute anything."
  exit 2
fi
echo "baseline: harness green on the unmutated copy"

UNCOVERED=0
for ln in "${LINES[@]}"; do
  cp "$WORK/pristine.py" "$CHECK"
  # Comment out the whole statement: from the append( line to its closing paren,
  # detected by the first line whose indentation returns to the statement's.
  python3 - "$CHECK" "$ln" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); start = int(sys.argv[2]) - 1
lines = p.read_text().splitlines(keepends=True)
indent = len(lines[start]) - len(lines[start].lstrip())
depth = 0
end = start
for i in range(start, len(lines)):
    depth += lines[i].count("(") - lines[i].count(")")
    end = i
    if depth <= 0:
        break
for i in range(start, end + 1):
    lines[i] = " " * indent + "pass  # NEUTERED\n" if i == start else " " * indent + "#\n"
p.write_text("".join(lines))
MUT
  if cmp -s "$WORK/pristine.py" "$CHECK"; then
    echo "  !! line $ln — MUTATION DID NOT APPLY; this gate was not tested"
    UNCOVERED=$((UNCOVERED + 1)); continue
  fi
  if ! python3 -c "import ast,sys;ast.parse(open(sys.argv[1]).read())" "$CHECK" 2>/dev/null; then
    echo "  !! line $ln — neutered copy does not parse; sweep cannot judge this gate"
    UNCOVERED=$((UNCOVERED + 1)); continue
  fi
  text="$(sed -n "${ln}p" "$WORK/pristine.py" | sed 's/^ *//' | cut -c1-60)"
  if ( cd "$WORK" && bash ci/test-parallelism-verdict.sh >/dev/null 2>&1 ); then
    echo "  UNCOVERED  line $ln: $text"
    UNCOVERED=$((UNCOVERED + 1))
  fi
done

cp "$WORK/pristine.py" "$CHECK"
echo
if [ "$UNCOVERED" -ne 0 ]; then
  echo "sweep: ${UNCOVERED} of ${#LINES[@]} gates can be DELETED with the harness still green."
  echo "Each needs a cell, or — if it cannot fire at all — deleting."
  exit 1
fi
echo "sweep: all ${#LINES[@]} gates are covered by a cell that reddens when they are removed."
exit 0
