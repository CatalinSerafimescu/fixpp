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
    echo "CELLS = {}"
    local i=0
    for f in "$@"; do i=$((i+1)); echo "CELLS['CELL-$i'] = C('$f')"; done
    echo "def run_cell(*a, **k): return {'status': 'pass', '_detail': 'stub'}"
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

# ── T0: the REAL population reconciles. Without this the suite proves only that the
# driver can say no, never that it says yes to the tree we actually ship. ──────────
check "T0 real population reconciles" 0 "reconciled:" \
  -- python3 "$DRIVER" --harness "$REPO/../phase-9-harness" --build-root "$REPO/build" --list

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

echo
echo "test-run-interop-live: $pass passed, $fail failed"
[ "$fail" = 0 ]
