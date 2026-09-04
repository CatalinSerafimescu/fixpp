#!/usr/bin/env bash
# Regression harness for the #298 parity gate:
#   ci/assert-wheel-test-parity.py
#
# Run by the `ci-script-pins` job in tier1.yml, and locally with:
#   ci/test-wheel-parity.sh
#
# ── WHY THIS FILE EXISTS AT ALL ──────────────────────────────────────────────
#
# #298's own warning is the specification for this file:
#
#   "Whatever the check is, it must be PROVEN TO GO RED on a deliberately
#    introduced divergence before it is trusted. A parity gate that silently
#    passes is worse than none, because it converts an unenforced convention
#    into a falsely enforced one."
#
# So every direction the gate claims to catch gets a cell that breaks exactly
# that direction, against a COPY of the real suite. The copies are real because
# a synthetic two-file fixture would not have the shapes that actually occur —
# a support module with no tests, a `(suite-native)` wheel-only file, a twin the
# README marks as diverging.
#
# ⚠️ A cell that fails for the WRONG REASON is not a passing cell. Each one
# asserts the specific message its direction produces, not merely a non-zero
# exit — a mutation that happens to break the README parse would otherwise read
# as a working gate.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CHECK="${WHEEL_PARITY_CHECK:-$HERE/assert-wheel-test-parity.py}"
SRC="$REPO/bindings/python/tests"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }

# A pristine copy of the real suite for each cell to break independently.
fresh() {
  rm -rf "$WORK/t"
  mkdir -p "$WORK/t"
  cp -r "$SRC/." "$WORK/t/"
  # Drop caches so a stale __pycache__ cannot influence a byte comparison.
  find "$WORK/t" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true
}

# $1 = cell name, $2 = expected exit, $3 = expected message fragment
expect() {
  local name="$1" want="$2" frag="$3" out rc=0
  out="$(python3 "$CHECK" "$WORK/t" 2>&1)" || rc=$?
  if [ "$rc" -ne "$want" ]; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$name — expected exit $want, got $rc"
    return
  fi
  if ! printf '%s\n' "$out" | grep -q -- "$frag"; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$name — exited $rc as expected but WITHOUT the message '$frag' (it failed for the wrong reason)"
    return
  fi
  ok "$name"
}

echo "== wheel-test parity gate =="

# ── T0: the shipped suite satisfies the contract ─────────────────────────────
fresh
expect "T0 the real suite satisfies the contract" 0 "contract holds"

# ── T1: a test present in-tree and ABSENT from the wheel copy ────────────────
#
# #298 item 1's exact shape: `test_msg_get_string_non_utf8_routes_through_
# fixpp_error` existed in-tree and not in the twin, with no exclusion entry, so
# the shipped artifact went untested on that path and nothing said so.
fresh
python3 - "$WORK/t/wheel/test_roundtrip.py" <<'MUT'
import re, sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
m = list(re.finditer(r"^def (test_\w+)", s, re.M))
assert len(m) >= 2, "MUTATION DID NOT APPLY — fewer than two tests to remove one from"
start = m[-1].start()
p.write_text(s[:start], encoding="utf-8")
MUT
expect "T1 a test dropped from the wheel copy is caught" 1 "ABSENT from the wheel copy"

# ── T2: a test present ONLY in the wheel copy ────────────────────────────────
#
# The one-sided-edit direction: `test_reentrancy.py` diverged because a fix went
# into the wheel copy alone, leaving the defect live in the in-tree suite that
# runs on all six tier-1 linux legs.
fresh
printf '\n\ndef test_only_in_the_wheel_copy():\n    pass\n' >> "$WORK/t/wheel/test_lifetime.py"
expect "T2 a test added to the wheel copy alone is caught" 1 "ABSENT in-tree"

# ── T3: an `as-is` row whose copies are no longer identical ──────────────────
#
# A row claiming byte-faithfulness must have it. Note the edit below is a
# COMMENT: the gate must not need the change to be semantic, because a one-sided
# comment edit is exactly how a faithful copy stops being one.
fresh
printf '\n# one-sided edit\n' >> "$WORK/t/wheel/test_reentrancy.py"
expect "T3 an 'as-is' file that stops being byte-identical is caught" 1 "NOT BYTE-FAITHFUL"

# ── T4: a wheel file the README does not enumerate ───────────────────────────
fresh
printf 'def test_smuggled():\n    pass\n' > "$WORK/t/wheel/test_smuggled_in.py"
expect "T4 an unenumerated wheel file is caught" 1 "UNENUMERATED"

# ── T5: a README row naming a file that is not there ─────────────────────────
fresh
rm -f "$WORK/t/wheel/test_locator.py"
expect "T5 a README row with no file behind it is caught" 1 "DANGLING"

# ── T6: THE EMPTY SCAN ───────────────────────────────────────────────────────
#
# If the suites move or the pairing breaks, "0 violations" must not read as a
# pass. Every instrument in this repo that has broken so far failed toward a
# clean result.
rm -rf "$WORK/t"; mkdir -p "$WORK/t/wheel"
cp "$SRC/wheel/README.md" "$WORK/t/wheel/"
expect "T6 an empty scan is an instrument failure, not a pass" 2 "empty scan"

# ── T7: a missing README is refused, not silently skipped ────────────────────
fresh
rm -f "$WORK/t/wheel/README.md"
expect "T7 a missing README is refused (it IS the allowlist)" 2 "IS the allowlist"

# ── T8: THE TABLE REFORMAT — the gate's own weakest link ─────────────────────
#
# MENTION matches a backticked filename anywhere, so conditions (1) and (2)
# survive a reformatted README. ROW needs an exact single-line pipe-table row.
# Before the guard, reformatting the table made ROW match nothing: `as_is` went
# empty, condition (4) stopped firing for EVERY file, and the script printed
# "contract holds" and exited 0 — disabling precisely the check that catches
# #298's original defect, with a fully green self-test suite.
#
# The reformat below is the most ordinary one imaginable: wrapping a row onto a
# continuation line, which renders identically in most Markdown viewers.
fresh
python3 - "$WORK/t/wheel/README.md" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "| `test_smoke.py` | as-is | — |"
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
# Same content, wrapped: ROW's ^\| anchor no longer matches this file's row.
p.write_text(s.replace(old, "| `test_smoke.py`\n  | as-is | — |", 1), encoding="utf-8")
MUT
expect "T8 a reformatted Membership table is an instrument failure, not a pass" 2 "no parsable Membership TABLE ROW"

# ── T9: an unrecognised disposition must not read as "exempt" ────────────────
#
# The other half of the same hole. If an unknown Source token fell through to
# the default, renaming `as-is` to anything else would silently exempt that file
# from byte-identity while leaving the table looking complete.
fresh
python3 - "$WORK/t/wheel/README.md" <<'MUT'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "| `test_smoke.py` | as-is | — |"
assert old in s, "MUTATION DID NOT APPLY — re-point the pattern, do not delete the mutant"
p.write_text(s.replace(old, "| `test_smoke.py` | as-is (verbatim) | — |", 1), encoding="utf-8")
MUT
expect "T9 an unrecognised Source disposition is refused, not treated as exempt" 2 "not one of"

echo
echo "wheel-parity harness: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
