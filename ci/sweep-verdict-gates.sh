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

# ── THE CENSUS, AND WHY IT ACCOUNTS FOR EVERY `.append(` ─────────────────────
#
# ⚠️ THE UNIVERSE SIDE OF THIS SET-DIFFERENCE IS AN INSTRUMENT TOO. The first
# version matched only `instrument|defects|voids`, reported "all 21 gates are
# covered", and was silently blind to two more sinks the verdict had grown:
# `shape.append(` (the A-B-A structural checks, which are copied INTO
# `instrument`) and the log parser's `a.append(` (the anomalies that become an
# INSTRUMENT FAILURE). A census narrower than its own claim is the same defect
# as an uncovered gate, one level out — and it reported a clean sweep with a
# straight face.
#
# So: gates are the sinks below, and EVERY OTHER `.append(` in the file must be
# a receiver named in NON_GATES. An unrecognised one stops the sweep, because it
# is either a new gate this would not sweep or a new accumulator nobody has
# classified — and there is no safe default between those.
GATE_SINKS='instrument|defects|voids|shape|a'
NON_GATES='out|unmatched|unreadable|summaries|reals|durations'

mapfile -t LINES < <(grep -nE "^[[:space:]]*($GATE_SINKS)\.append\(" "$CHECK" | cut -d: -f1)
if [ "${#LINES[@]}" -eq 0 ]; then
  echo "::error::found ZERO gates to neuter. Either the append() shape changed or this"
  echo "sweep's pattern is broken; refusing to report a clean sweep over nothing."
  exit 2
fi

total_appends="$(grep -cE '[A-Za-z_][A-Za-z0-9_]*\.append\(' "$CHECK")"
accounted="$(grep -cE "^[[:space:]]*($GATE_SINKS|$NON_GATES)\.append\(" "$CHECK")"
if [ "$total_appends" -ne "$accounted" ]; then
  echo "::error::${total_appends} \`.append(\` call(s) in $CHECK but only ${accounted} are"
  echo "classified. An unrecognised sink is either a GATE this sweep would not test or an"
  echo "accumulator nobody has classified; there is no safe default. Unclassified:"
  grep -nE '[A-Za-z_][A-Za-z0-9_]*\.append\(' "$CHECK" \
    | grep -vE "^[0-9]+:[[:space:]]*($GATE_SINKS|$NON_GATES)\.append\(" | sed 's/^/  /'
  echo "Add it to GATE_SINKS or NON_GATES in this file, deliberately."
  exit 2
fi
echo "gates found: ${#LINES[@]} (of ${total_appends} appends; the rest are classified non-gates)"

# Prove the harness is green BEFORE any mutation, or every 'redden' below is
# meaningless — the baseline is the control arm.
if ! ( cd "$WORK" && bash ci/test-parallelism-verdict.sh >/dev/null 2>&1 ); then
  echo "::error::the harness is RED on the unmutated copy. Fix that first; a sweep"
  echo "against a failing baseline cannot attribute anything."
  exit 2
fi
echo "baseline: harness green on the unmutated copy"

# ⚠️ A GOLDEN SAMPLE, BECAUSE THIS SWEEP INHERITS THE FAILURE MODE IT EXISTS TO
# CATCH. "The harness went red" is credited as "the gate is covered" — but a
# neuter that BREAKS the checker (a mis-detected statement end, a gate rewritten
# so the paren walk lands wrong) also makes every cell fail, and would be
# credited identically. That is a zero this sweep could not have reported
# otherwise, which is the whole class it was written against.
#
# So each neutered copy is first run against a clean sample that trips NO gate:
# it must still exit 0. A checker that cannot judge a good sample has not had
# one gate removed, it has been broken, and its "coverage" proves nothing.
# shellcheck disable=SC2016  # `$WORK` here is literal TEXT being matched inside
# the harness file, not a variable this script expands.
sed -n '/^cat > "\$WORK\/gen.py"/,/^GEN$/p' "$HERE/test-parallelism-verdict.sh" \
  | sed '1d;$d' > "$WORK/gen.py"
if [ ! -s "$WORK/gen.py" ]; then
  echo "::error::could not extract the sample generator from ci/test-parallelism-verdict.sh."
  echo "Without a golden sample the control arm below is absent, and a broken neuter would"
  echo "read as a covered gate. Refusing to sweep."
  exit 2
fi
python3 "$WORK/gen.py" "$WORK/golden" >/dev/null 2>&1
if ! ( cd "$WORK" && python3 ci/parallelism-verdict.py golden >/dev/null 2>&1 ); then
  echo "::error::the golden sample is not VALID against the unmutated checker, so it cannot"
  echo "serve as a control. Regenerate it."
  exit 2
fi
echo "control: golden sample reads VALID on the unmutated checker"

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
  # The control arm: one gate removed must not stop the checker judging a clean
  # sample. If it does, the neuter broke the checker and any red the harness
  # then shows is not evidence about THIS gate.
  if ! ( cd "$WORK" && python3 ci/parallelism-verdict.py golden >/dev/null 2>&1 ); then
    echo "  !! line $ln — neutering BROKE the checker (the golden sample no longer"
    echo "               reads VALID), so this gate was not actually tested: $text"
    UNCOVERED=$((UNCOVERED + 1)); continue
  fi
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
