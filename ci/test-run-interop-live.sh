#!/usr/bin/env bash
# test-run-interop-live.sh — pin ci/run-interop-live.py's refusals (fixpp#468).
#
# The driver's whole value is that it REFUSES to run a live-interop job whose
# population it cannot account for. A refusal nobody has ever seen fire is not a
# refusal, and this repo's single most recurring defect is an instrument that reports
# clean because it could not report anything else. So every named refusal gets a
# mutant here, and each mutant is checked to produce the SPECIFIC message — not merely
# a non-zero exit, which any typo also produces.
#
# Each case builds a synthetic harness module and fixture population, because driving
# the real 90-id set could only ever exercise the passing path. T0 covers the real one.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
DRIVER="$HERE/run-interop-live.py"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
check() {  # check <name> <want-rc> <want-substring> -- <cmd...>
  local name="$1" want_rc="$2" want="$3"; shift 4
  local out rc
  out="$("$@" 2>&1)"; rc=$?
  if [ "$rc" != "$want_rc" ]; then
    echo "FAIL  $name: exit $rc, wanted $want_rc"; echo "$out" | sed 's/^/      /' | head -5
    fail=$((fail+1)); return
  fi
  if ! printf '%s' "$out" | grep -qF -- "$want"; then
    echo "FAIL  $name: exit $rc as wanted, but the message did not mention: $want"
    echo "$out" | sed 's/^/      /' | head -5
    fail=$((fail+1)); return
  fi
  echo "ok    $name"; pass=$((pass+1))
}

# ── a synthetic harness module: two covered params of one case, plus a lone case ──
mk_harness() {  # mk_harness <dir> <filter>...
  local dir="$1"; shift
  mkdir -p "$dir/tools"
  { echo "import dataclasses"
    echo "@dataclasses.dataclass"
    echo "class C:"
    echo "    gtest_filter: str"
    echo "    counterparty: str = 'quickfix-j'"
    echo "    arm: str = 'validation-off'"
    echo "    binary: str = 'interop_stub'"
    echo "CELLS = {}"
    local i=0
    for f in "$@"; do i=$((i+1)); echo "CELLS['CELL-$i'] = C('$f')"; done
    echo "import os"
    echo "def settle_between_cells():"
    echo "    log = os.environ.get('SETTLE_LOG')"
    echo "    if log:"
    echo "        open(log, 'a').write('settle\\n')"
    echo "def run_cell(*a, **k):"
    echo "    v = os.environ.get('STUB_VERDICT', 'pass')"
    echo "    if v == 'raise': raise RuntimeError('stub blew up')"
    echo "    return {'status': v, '_detail': 'stub'}"
  } > "$dir/tools/run_interop_cell.py"
}

COVERED_A="Fix44/S.C/QFj_init"
COVERED_B="Fix44/S.C/QFj_acc"
LONE="Fix44/T.D/QFj_init"
H="$TMP/h"; mk_harness "$H" "$COVERED_A" "$COVERED_B"

# skip set = the two covered + one uncovered sibling + one lone uncovered case
SKIP="$TMP/skip.txt"
printf '%s\n' "$COVERED_A" "$COVERED_B" "Fix44/S.C/QFcpp_init" "$LONE" > "$SKIP"

EXC="$TMP/exc.txt"
mkexc() { printf '%s\n' "$@" > "$EXC"; }

run() { python3 "$DRIVER" --harness "$H" --build-root "$TMP" \
          --skip-set "$SKIP" --exclusions "$EXC" --list; }

# ── T0: the REAL files, checked by the REAL checker. Without this the suite proves
# only that the driver can say no, never that it says yes to the tree we ship.
#
# ⚠️ --offline-checks, NOT the full reconciliation. This harness runs in
# `ci-script-pins`, whose checkout is the LIBRARY ONLY — no parent repo, no docker,
# no image — so an earlier draft's `--harness "$REPO/../phase-9-harness"` would have
# reddened the pin on every run while passing on the author's machine, where the
# parent tree happens to sit one level up. An arm that can only pass where it was
# written is not an arm. The covered-vs-excluded half needs the image's registry and
# is asserted by the live job itself, before any cell starts, every time it runs.
check "T0 the real exclusion file checks out against the real skip set" 0 \
  "offline checks passed" -- python3 "$DRIVER" --offline-checks
check "T0b offline mode refuses to also take --harness" 2 "does not take --harness" \
  -- python3 "$DRIVER" --offline-checks --harness "$H"

# ── T1: a case that neither runs nor is excluded ──────────────────────────────────
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init"
check "T1 unaccounted case refused" 1 "neither run here nor are excluded" -- run

# ── T2: the full, correct population passes (the positive control for T1) ─────────
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE"
check "T2 accounted population passes" 0 "reconciled:" -- run

# ── T3: an exclusion for a case that DOES run — a stale exemption ─────────────────
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE" \
      "unregistered-tracked $COVERED_A"
check "T3 stale exclusion refused" 1 "a case that a cell now RUNS" -- run

# ── T4: an exclusion naming a case that is not in the skip set at all ─────────────
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE" \
      "unregistered-tracked Fix44/Gone.Case/QFj_init"
check "T4 exclusion for an unknown case refused" 1 "not in the skip set at all" -- run

# ── T5: SKEW — the harness registers a filter this tree's skip set never heard of.
# This is the image-vs-checkout version check; it must not need a version string. ──
H2="$TMP/h2"; mk_harness "$H2" "$COVERED_A" "$COVERED_B" "Fix44/Renamed.Case/QFj_init"
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE"
check "T5 harness/tree skew refused" 1 "harness and this checkout disagree" \
  -- python3 "$DRIVER" --harness "$H2" --build-root "$TMP" --skip-set "$SKIP" \
       --exclusions "$EXC" --list

# ── T6: the per-group structural conditions. The equality in T2 still holds for BOTH
# of these, so without the group checks the exclusion file would be a free pass. ───
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "qfj-only-at-g1 $LONE"
check "T6a qfj-only tag on a case that runs NOWHERE refused" 1 "runs nowhere" -- run

mkexc "unregistered-tracked Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE"
check "T6b unregistered tag on a case with a covered sibling refused" 1 \
  "the debt was partly paid" -- run

# ── T7: fixtures that would make every check above pass vacuously ─────────────────
: > "$TMP/empty.txt"
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE"
check "T7a empty skip set refused, not passed" 2 "refusing to run against an empty population" \
  -- python3 "$DRIVER" --harness "$H" --build-root "$TMP" --skip-set "$TMP/empty.txt" \
       --exclusions "$EXC" --list
check "T7b missing skip-set file refused" 2 "is missing" \
  -- python3 "$DRIVER" --harness "$H" --build-root "$TMP" --skip-set "$TMP/nope.txt" \
       --exclusions "$EXC" --list
H3="$TMP/h3"; mkdir -p "$H3/tools"
check "T7c missing bundled harness refused" 2 "bundled harness driver not found" \
  -- python3 "$DRIVER" --harness "$H3" --build-root "$TMP" --skip-set "$SKIP" \
       --exclusions "$EXC" --list

# ── T8: a malformed or duplicated exclusion line is refused, not skipped ──────────
mkexc "Fix44/S.C/QFcpp_init"
check "T8a untagged exclusion line refused" 2 "malformed exclusion line" -- run
mkexc "invented-tag Fix44/S.C/QFcpp_init"
check "T8b unknown reason tag refused" 2 "malformed exclusion line" -- run
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE" \
      "unregistered-tracked $LONE"
check "T8c duplicate exclusion refused" 2 "duplicate exclusion" -- run

# ── T9: the RUN phase's verdict. The reconciliation above happens before any cell
# runs; these pin what the driver does with the results. This is the claim the whole
# job rests on, and the stub above returned 'pass' unconditionally until it had an
# arm — an omission of exactly the kind this file exists to catch. ────────────────
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE"
runcells() { STUB_VERDICT="$1" python3 "$DRIVER" --harness "$H" --build-root "$TMP" \
               --skip-set "$SKIP" --exclusions "$EXC"; }

check "T9a all cells pass -> job passes" 0 "live interop cell(s) passed" -- runcells pass

# ⚠️ THE headline: interop-smoke tolerates skip:* (FR-023, light tier). Here the
# counterparty is guaranteed present, so a skip means a cell did not run what it
# claims to. Inheriting smoke's shape would let this job pass while testing nothing.
check "T9b a SKIPPED cell fails the job" 1 "did not pass" \
  -- runcells "skip:counterparty-unavailable"
check "T9c a FAILED cell fails the job" 1 "did not pass" -- runcells fail
# A raise must not escape as a traceback and a non-specific exit; it is a cell result.
check "T9d a RAISING cell is reported, not crashed" 1 "raised: RuntimeError" -- runcells raise

# ── T10: the inter-cell settle — a RELIABILITY property, not a nicety. Acceptor
# cells flake on port/process teardown when run back-to-back, which is why the
# harness owns INTER_CELL_SETTLE_S. This driver's first draft wrote its own loop and
# silently omitted it, so both halves get a real arm. ────────────────────────────
# Written WITHOUT the helper, not grep-stripped out of a full one: deleting the
# `def` line alone leaves an orphaned body, and the module then fails to IMPORT —
# which exercises a different refusal (T10c) and silently stops testing this one.
H4="$TMP/h4"; mkdir -p "$H4/tools"
{ echo "import dataclasses"
  echo "@dataclasses.dataclass"
  echo "class C:"
  echo "    gtest_filter: str"
  echo "    counterparty: str = 'quickfix-j'"
  echo "    arm: str = 'validation-off'"
  echo "CELLS = {'CELL-1': C('$COVERED_A'), 'CELL-2': C('$COVERED_B')}"
  echo "def run_cell(*a, **k): return {'status': 'pass', '_detail': 'stub'}"
} > "$H4/tools/run_interop_cell.py"
mkexc "qfj-only-at-g1 Fix44/S.C/QFcpp_init" "unregistered-tracked $LONE"
check "T10a a pre-settle image is REFUSED, not run without the settle" 2 \
  "predates settle_between_cells" \
  -- python3 "$DRIVER" --harness "$H4" --build-root "$TMP" --skip-set "$SKIP" \
       --exclusions "$EXC" --list

# A bundled driver that will not import at all is its own refusal, distinct from the
# one above — and the fixture bug that produced it is how we learned this path
# existed as an unhandled traceback.
H5="$TMP/h5"; mkdir -p "$H5/tools"
printf 'def broken(:\n' > "$H5/tools/run_interop_cell.py"
check "T10c an image whose driver will not import is refused, not a traceback" 2 \
  "failed to import" \
  -- python3 "$DRIVER" --harness "$H5" --build-root "$TMP" --skip-set "$SKIP" \
       --exclusions "$EXC" --list

# Counted, not grepped: the stub's settle appends a line, so this observes the real
# call. Two cells => exactly one settle (between them, never after the last).
SETTLE_LOG="$TMP/settles.txt"; : > "$SETTLE_LOG"
SETTLE_LOG="$SETTLE_LOG" STUB_VERDICT=pass python3 "$DRIVER" --harness "$H" \
  --build-root "$TMP" --skip-set "$SKIP" --exclusions "$EXC" >/dev/null 2>&1
n=$(wc -l < "$SETTLE_LOG")
if [ "$n" = "1" ]; then
  echo "ok    T10b 2 cells -> exactly 1 settle (between, not after the last)"; pass=$((pass+1))
else
  echo "FAIL  T10b 2 cells -> $n settle(s), wanted 1"; fail=$((fail+1))
fi

# ── T11: --list-binaries, which interop-live.yml's build step now depends on. An
# UNTESTED derive step is the failure mode the step exists to remove: the parent
# matrix hardcodes its target list, and a list that silently comes back short builds
# fewer binaries than the run needs, so the cells fail for a reason that looks like
# the product. Two properties, because either alone can be satisfied by nothing:
# it must SPEAK the registry's binaries (deduped, sorted, one line), and it must
# REFUSE rather than print an empty line when the registry names none.
HB="$TMP/hb"; mk_harness "$HB" "$COVERED_A" "$COVERED_B" "$LONE"
python3 - "$HB/tools/run_interop_cell.py" <<'PYEOF'
import sys, pathlib
# two cells share one binary, the third differs and sorts FIRST -- so a driver that
# neither dedups nor sorts cannot produce the expected line by accident.
p = pathlib.Path(sys.argv[1])
p.write_text(p.read_text() + "\nCELLS['CELL-3'].binary = 'interop_aaa'\n")
PYEOF
got="$(python3 "$DRIVER" --harness "$HB" --list-binaries 2>&1)"; rc=$?
if [ "$rc" = 0 ] && [ "$got" = "interop_aaa interop_stub" ]; then
  echo "ok    T11a --list-binaries prints the deduped, sorted binary set on one line"
  pass=$((pass+1))
else
  echo "FAIL  T11a --list-binaries: rc=$rc out=[$got], wanted 'interop_aaa interop_stub'"
  fail=$((fail+1))
fi

HZ="$TMP/hz"; mk_harness "$HZ"
check "T11b a registry naming ZERO binaries is refused, not an empty target list" 2 \
  "refusing to build nothing" -- \
  python3 "$DRIVER" --harness "$HZ" --list-binaries

echo
echo "test-run-interop-live: $pass passed, $fail failed"
[ "$fail" = 0 ]
