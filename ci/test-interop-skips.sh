#!/usr/bin/env bash
# Regression harness for ci/assert-interop-skips.py (fixpp#431).
#
# WHY THIS EXISTS. The checker is what stands between "90 skipped cases" and a
# green CI lane — a SKIP must not read as a PASS, and this is the instrument
# that enforces it. Per this repo's rule
# (feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree), an
# assertion nobody has seen fail is not evidence: every cell below drives a
# synthetic gtest-JSON fixture at the real script and checks BOTH the exit
# code and that the diagnostic names the right thing, not merely that it is
# non-zero (a checker that exits non-zero for the wrong reason is not a
# passing cell).
#
# Buildless: python3 + coreutils only, no ctest, no compiler, no real gtest
# binary. Every fixture is a hand-built gtest JSON report (same schema
# `GTEST_OUTPUT=json:` writes, verified against real interop binaries' output
# while this checker was written).
#
# Run by the `ci-script-pins` job in tier1.yml, and locally with:
#   ci/test-interop-skips.sh
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="$HERE/assert-interop-skips.py"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }

# Writes one gtest JSON report containing exactly one testsuite with one case.
#   write_json <path> <suite> <case> <result:COMPLETED|SKIPPED> [<message>] [<failed:0|1>]
# COMPLETED + failed=1 emits a `failures` entry; SKIPPED emits a `skipped`
# entry carrying <message>; COMPLETED + failed=0 (the default) emits neither —
# an ordinary pass.
write_json() {
  local path="$1" suite="$2" case_name="$3" result="$4" msg="${5:-}" failed="${6:-0}"
  python3 - "$path" "$suite" "$case_name" "$result" "$msg" "$failed" <<'PY'
import json, sys
path, suite, name, result, msg, failed = sys.argv[1:7]
tc = {"name": name, "file": "fixture.cpp", "line": 1, "status": "RUN",
      "result": result, "time": "0s", "classname": suite}
if result == "SKIPPED":
    tc["skipped"] = [{"message": msg}]
elif failed == "1":
    tc["failures"] = [{"failure": msg or "fixture failure", "type": ""}]
doc = {"tests": 1, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": suite, "tests": 1, "failures": 0, "disabled": 0,
                        "errors": 0, "testsuite": [tc]}]}
with open(path, "w") as f:
    json.dump(doc, f)
PY
}

# Writes an empty-testsuite report (zero cases) — used by the zero-cases cell.
write_empty_json() {
  local path="$1"
  printf '%s' '{"tests":0,"failures":0,"disabled":0,"errors":0,"name":"AllTests","testsuites":[]}' > "$path"
}

# $1 = case name, $2 = json-dir, $3 = expected-skips file, $4 = expected-count,
# $5 = expected exit code, $6 = required fragment in stdout+stderr.
run_check() {
  local name="$1" json_dir="$2" skips_file="$3" count="$4" want_rc="$5" frag="$6"
  local out rc=0
  out="$(python3 "$CHECK" --json-dir "$json_dir" --expected-skips "$skips_file" \
           --expected-count "$count" 2>&1)" || rc=$?
  if [ "$rc" -ne "$want_rc" ]; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$name — expected exit $want_rc, got $rc"; return
  fi
  if ! printf '%s\n' "$out" | grep -qF -- "$frag"; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$name — exited $rc but WITHOUT '$frag' (failed/passed for the wrong reason)"; return
  fi
  ok "$name"
}

PORT_OK_CPP="quickfix-cpp unavailable: INTEROP_QUICKFIX_CPP_PORT not set (parent harness did not lease a port)"
PORT_OK_J="quickfix-j unavailable: INTEROP_QUICKFIX_J_PORT not set (parent harness did not lease a port)"

echo "== interop skip-set witness (fixpp#431) =="

# ── T1: clean pass ────────────────────────────────────────────────────────
d="$WORK/t1"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t1-skips.txt"
run_check "T1 clean pass" "$d" "$WORK/t1-skips.txt" 2 0 "PASS:"

# ── T2: a case FAILED ─────────────────────────────────────────────────────
d="$WORK/t2"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
write_json "$d/binB.json" Suite CaseB COMPLETED "" 1
printf 'Suite.CaseA\n' > "$WORK/t2-skips.txt"
run_check "T2 a case failed" "$d" "$WORK/t2-skips.txt" 2 1 "FAILED"

# ── T3: an UNEXPECTED skip (skips now, not listed) ───────────────────────
d="$WORK/t3"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
write_json "$d/binB.json" Suite CaseB SKIPPED "$PORT_OK_J"
printf 'Suite.CaseA\n' > "$WORK/t3-skips.txt"
run_check "T3 unexpected skip" "$d" "$WORK/t3-skips.txt" 2 1 "unexpected skips (1"

# ── T4: LISTED but not skipped (ran, or gone) ────────────────────────────
d="$WORK/t4"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\nSuite.CaseZ\n' > "$WORK/t4-skips.txt"
run_check "T4 listed-but-not-skipped" "$d" "$WORK/t4-skips.txt" 2 1 "listed-but-not-skipped (1"

# ── T5: WRONG skip reason (a real probe reason, not port-not-set) ───────
d="$WORK/t5"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "quickfix-cpp unavailable: nothing listening at 127.0.0.1:1"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t5-skips.txt"
run_check "T5 wrong skip reason" "$d" "$WORK/t5-skips.txt" 2 1 "reason other than a counterparty"

# ── T6: MISSING JSON report (fewer files than expected — instrument fail) ─
d="$WORK/t6"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
printf 'Suite.CaseA\n' > "$WORK/t6-skips.txt"
run_check "T6 missing JSON report" "$d" "$WORK/t6-skips.txt" 2 2 "found 1 gtest JSON report(s)"

# ── T7: UNPARSABLE JSON (fail closed) ────────────────────────────────────
d="$WORK/t7"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
printf '{not json' > "$d/binB.json"
printf 'Suite.CaseA\n' > "$WORK/t7-skips.txt"
run_check "T7 unparsable JSON" "$d" "$WORK/t7-skips.txt" 2 2 "did not parse as JSON"

# ── T8: ZERO cases executed across every report ──────────────────────────
d="$WORK/t8"; mkdir -p "$d"
write_empty_json "$d/binA.json"
write_empty_json "$d/binB.json"
: > "$WORK/t8-skips.txt"
run_check "T8 zero cases executed" "$d" "$WORK/t8-skips.txt" 2 2 "ZERO test cases"

# ── T9: positive control — the real file's SHAPE (header comment, blank
# lines, unsorted entries) parses identically to a bare list. Proves the
# parser strips comments/blanks and treats the list as a SET, not an
# order-sensitive sequence — the shape tests/interop/expected-skips-
# without-counterparty.txt actually uses. ──────────────────────────────────
d="$WORK/t9"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
write_json "$d/binB.json" Suite CaseB SKIPPED "$PORT_OK_J"
write_json "$d/binC.json" Suite CaseC COMPLETED
cat > "$WORK/t9-skips.txt" <<'EOF'
# a header comment, like the real file
# a second line

Suite.CaseB
Suite.CaseA

# a trailing comment
EOF
run_check "T9 positive control: comments/blanks/unsorted order" "$d" "$WORK/t9-skips.txt" 3 0 "PASS:"

# ── T10: the vacuity guard — --expected-count 0 must NEVER pass, even
# against zero JSON files (feedback_verification_grep_must_be_proven_
# nonzero_on_the_unfixed_tree's sibling: a derivation landing on zero must
# not be able to pass vacuously). ───────────────────────────────────────────
d="$WORK/t10"; mkdir -p "$d"
: > "$WORK/t10-skips.txt"
run_check "T10 expected-count=0 refused" "$d" "$WORK/t10-skips.txt" 0 2 "refusing to run"

# ── T11: the json-dir does not exist at all ──────────────────────────────
: > "$WORK/t11-skips.txt"
run_check "T11 missing json-dir" "$WORK/does-not-exist" "$WORK/t11-skips.txt" 1 2 "does not exist"

# ── T12: the expected-skips file does not exist ──────────────────────────
d="$WORK/t12"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA COMPLETED
run_check "T12 missing expected-skips file" "$d" "$WORK/does-not-exist.txt" 1 2 "does not exist"

CELLS_DECLARED=12
TOTAL=$((PASS + FAIL))
echo
if [ "$TOTAL" -ne "$CELLS_DECLARED" ]; then
  echo "interop-skips harness: EXECUTION COUNT MISMATCH — ran ${TOTAL} cells, declared ${CELLS_DECLARED}."
  echo "A cell was added or lost without updating CELLS_DECLARED. Refusing to report a result."
  exit 1
fi
echo "interop-skips harness: ${PASS} passed, ${FAIL} failed (${TOTAL} cells)"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
