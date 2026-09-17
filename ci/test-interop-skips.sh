#!/usr/bin/env bash
# Regression harness for ci/assert-interop-skips.py (fixpp#431).
#
# WHY THIS EXISTS. The checker is what stands between the skipped cases and a
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
# binary. Every fixture is a hand-built gtest JSON report — the skipped-case
# fixtures carry the same leading `<file>:<line>\n` location line a real
# report does (write_json below), because the checker strips exactly that
# much before matching the reason, and a fixture without it cannot witness
# the strip being load-bearing.
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
#   write_json <path> <suite> <case> <result:COMPLETED|SKIPPED|...> [<message>] [<failed:0|1>]
# COMPLETED + failed=1 emits a `failures` entry; SKIPPED emits a `skipped`
# entry whose message is `<file>:<line>\n<message>\n` — the real shape a gtest
# JSON report uses, location line then reason, which is what makes the
# checker's location-line strip a load-bearing thing for a fixture to prove,
# not a decoration; COMPLETED + failed=0 (the default) emits neither — an
# ordinary pass. Any other <result> (e.g. a deliberately wrong one) is passed
# through verbatim with neither `skipped` nor `failures`.
write_json() {
  local path="$1" suite="$2" case_name="$3" result="$4" msg="${5:-}" failed="${6:-0}"
  python3 - "$path" "$suite" "$case_name" "$result" "$msg" "$failed" <<'PY'
import json, sys
path, suite, name, result, msg, failed = sys.argv[1:7]
tc = {"name": name, "file": "fixture.cpp", "line": 1, "status": "RUN",
      "result": result, "time": "0s", "classname": suite}
if result == "SKIPPED":
    tc["skipped"] = [{"message": f"{tc['file']}:{tc['line']}\n{msg}\n"}]
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

# Writes an executable stub at <bin-dir>/<stem> that, when invoked with
# `--gtest_list_tests --gtest_output=json:PATH`, copies <report> to PATH
# verbatim — the binary's own enumeration then equals whatever the report
# already says, which is what makes every pre-existing (non-L) cell below
# trivially consistent: the mirrored "binary" cannot itself disagree with
# the report it mirrors. It ignores every other argument.
write_mirror_stub() {
  local stub="$1" report="$2"
  cat > "$stub" <<PYEOF
#!/usr/bin/env python3
import shutil, sys
report = "$report"
out = None
for a in sys.argv[1:]:
    if a.startswith("--gtest_output=json:"):
        out = a.split(":", 1)[1]
if out:
    shutil.copyfile(report, out)
PYEOF
  chmod +x "$stub"
}

# For every <json-dir>/*.json report, writes a mirror stub named after its
# stem into <bin-dir>.
make_mirror_bin_dir() {
  local json_dir="$1" bin_dir="$2"
  mkdir -p "$bin_dir"
  local f stem
  for f in "$json_dir"/*.json; do
    [ -e "$f" ] || continue
    stem="$(basename "$f" .json)"
    write_mirror_stub "$bin_dir/$stem" "$f"
  done
}

# $1 = case name, $2 = json-dir, $3 = expected-skips file, $4 = expected-count,
# $5 = expected exit code, $6 = required fragment in stdout+stderr,
# $7 = optional bin-dir (default: a mirror stub per report, see above).
run_check() {
  local name="$1" json_dir="$2" skips_file="$3" count="$4" want_rc="$5" frag="$6"
  local bin_dir="${7:-}"
  local out rc=0
  if [ -z "$bin_dir" ]; then
    bin_dir="$WORK/bin-${PASS}-${FAIL}"
    make_mirror_bin_dir "$json_dir" "$bin_dir"
  fi
  out="$(python3 "$CHECK" --json-dir "$json_dir" --expected-skips "$skips_file" \
           --bin-dir "$bin_dir" \
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
run_check "T8 zero cases executed" "$d" "$WORK/t8-skips.txt" 2 2 "ZERO with status=RUN"

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

# ── T13-T23: RC2 (fixpp#431 Gate B r1, Codex #2/#3/#6) — the classification
# holes a SKIP-must-not-read-as-a-PASS gate exists to close. ────────────────

# ── T13: NOTRUN/SUPPRESSED (gtest's DISABLED_ shape) —
# must NOT read as a pass just because it carries no `failures` array. The
# NOTRUN case sits ALONGSIDE a real RUN case in the SAME report (the actual
# shape: one case in a many-case binary flips to DISABLED_, the rest of that
# binary's cases still run) — so this exercises bad_status_cases, not the
# per-report-zero guard T15 already covers. ──────────────────────────────────
d="$WORK/t13"; mkdir -p "$d"
python3 - "$d/binA.json" <<'PY'
import json, sys
run_case = {"name": "CaseA", "file": "fixture.cpp", "line": 1, "status": "RUN",
            "result": "COMPLETED", "time": "0s", "classname": "Suite"}
disabled_case = {"name": "CaseB", "file": "fixture.cpp", "line": 2, "status": "NOTRUN",
                  "result": "SUPPRESSED", "time": "0s", "classname": "Suite"}
doc = {"tests": 2, "failures": 0, "disabled": 1, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 2, "failures": 0, "disabled": 1,
                        "errors": 0, "testsuite": [run_case, disabled_case]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
: > "$WORK/t13-skips.txt"
run_check "T13 NOTRUN/SUPPRESSED case with disabled:1 is caught, not read as a pass" \
  "$d" "$WORK/t13-skips.txt" 1 1 "did not run with a real result"

# ── T14: an unrecognised `result` value must not read as a pass either ─────
d="$WORK/t14"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA WHATEVER
write_json "$d/binB.json" Suite CaseB COMPLETED
: > "$WORK/t14-skips.txt"
run_check "T14 unrecognised result value is caught" \
  "$d" "$WORK/t14-skips.txt" 2 1 "did not run with a real result"

# ── T15: one report reports zero cases while a SIBLING report is populated —
# the aggregate-only zero guard (T8) cannot see this; the per-report guard can.
# ───────────────────────────────────────────────────────────────────────────
d="$WORK/t15"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA COMPLETED
write_empty_json "$d/binB.json"
: > "$WORK/t15-skips.txt"
run_check "T15 one empty report beside a populated one is caught per-report" \
  "$d" "$WORK/t15-skips.txt" 2 2 "ZERO with status=RUN"

# ── T16-T18: the reason must FULLMATCH after the location strip, not merely
# CONTAIN the allowed reason as a substring. ────────────────────────────────
d="$WORK/t16"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "NEW REASON; $PORT_OK_CPP"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t16-skips.txt"
run_check "T16 reason with a PREFIX before the allowed text is caught" \
  "$d" "$WORK/t16-skips.txt" 2 1 "reason other than a counterparty"

d="$WORK/t17"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP; more"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t17-skips.txt"
run_check "T17 reason with a SUFFIX after the allowed text is caught" \
  "$d" "$WORK/t17-skips.txt" 2 1 "reason other than a counterparty"

d="$WORK/t18"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP
skip:not-applicable fixture dir missing"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t18-skips.txt"
run_check "T18 reason plus an EXTRA LINE after it is caught" \
  "$d" "$WORK/t18-skips.txt" 2 1 "reason other than a counterparty"

# ── T19: a location-only skip message (gtest 1.17's SetUpTestSuite shape:
# message is location ONLY, no reason line at all) is a bad reason, not a pass.
# ───────────────────────────────────────────────────────────────────────────
d="$WORK/t19"; mkdir -p "$d"
python3 - "$d/binA.json" <<'PY'
import json, sys
tc = {"name": "CaseA", "file": "fixture.cpp", "line": 5, "status": "RUN",
      "result": "SKIPPED", "time": "0s", "classname": "Suite",
      "skipped": [{"message": "fixture.cpp:5\n"}]}
doc = {"tests": 1, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 1, "failures": 0, "disabled": 0,
                        "testsuite": [tc]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t19-skips.txt"
run_check "T19 location-only skip message (SetUpTestSuite shape) is caught" \
  "$d" "$WORK/t19-skips.txt" 2 1 "reason other than a counterparty"

# ── T20-T22: structurally malformed but syntactically valid JSON must exit 2
# with a named ::error, not a Python traceback (Codex #6). ──────────────────
d="$WORK/t20"; mkdir -p "$d"
printf '%s' '[]' > "$d/binA.json"
: > "$WORK/t20-skips.txt"
run_check "T20 top-level JSON value is a list, not an object" \
  "$d" "$WORK/t20-skips.txt" 1 2 "not a JSON object"

d="$WORK/t21"; mkdir -p "$d"
printf '%s' '{"testsuites": ["notadict"]}' > "$d/binA.json"
: > "$WORK/t21-skips.txt"
run_check "T21 a testsuites entry is a string, not an object" \
  "$d" "$WORK/t21-skips.txt" 1 2 "testsuites\` entry that is a"

d="$WORK/t22"; mkdir -p "$d"
printf '%s' '{"testsuites": [{"name": "Suite", "testsuite": [{"name": "CaseA", "status": "RUN", "result": "SKIPPED", "skipped": ["juststring"]}]}]}' > "$d/binA.json"
: > "$WORK/t22-skips.txt"
run_check "T22 a skipped entry is a string, not an object with a message" \
  "$d" "$WORK/t22-skips.txt" 1 2 "skipped\` entry that is not a JSON object"

# ── T23: a Windows-style `C:\...` location prefix strips correctly — the
# location-line regex must not be confused by the drive-letter colon. ───────
d="$WORK/t23"; mkdir -p "$d"
python3 - "$d/binA.json" <<PY
import json, sys
msg = "C:\\\\a\\\\fixpp\\\\tests\\\\x.cpp:12\n$PORT_OK_CPP\n"
tc = {"name": "CaseA", "file": "fixture.cpp", "line": 12, "status": "RUN",
      "result": "SKIPPED", "time": "0s", "classname": "Suite",
      "skipped": [{"message": msg}]}
doc = {"tests": 1, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 1, "failures": 0, "disabled": 0,
                        "testsuite": [tc]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
printf 'Suite.CaseA\n' > "$WORK/t23-skips.txt"
run_check "T23 Windows-style C:\\ location prefix strips correctly" \
  "$d" "$WORK/t23-skips.txt" 1 0 "PASS:"

# ── T24-T25: a SKIPPED case with no skip message at all must be a bad
# reason, not silently pass the reason check by iterating zero entries. ─────
d="$WORK/t24"; mkdir -p "$d"
python3 - "$d/binA.json" <<'PY'
import json, sys
tc = {"name": "CaseA", "file": "fixture.cpp", "line": 1, "status": "RUN",
      "result": "SKIPPED", "time": "0s", "classname": "Suite", "skipped": []}
doc = {"tests": 1, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 1, "failures": 0, "disabled": 0,
                        "testsuite": [tc]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t24-skips.txt"
run_check "T24 SKIPPED with an empty skipped array is a bad reason, not a pass" \
  "$d" "$WORK/t24-skips.txt" 2 1 "reason other than a counterparty"

d="$WORK/t25"; mkdir -p "$d"
python3 - "$d/binA.json" <<'PY'
import json, sys
tc = {"name": "CaseA", "file": "fixture.cpp", "line": 1, "status": "RUN",
      "result": "SKIPPED", "time": "0s", "classname": "Suite"}
doc = {"tests": 1, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 1, "failures": 0, "disabled": 0,
                        "testsuite": [tc]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t25-skips.txt"
run_check "T25 SKIPPED with no skipped field at all is a bad reason, not a pass" \
  "$d" "$WORK/t25-skips.txt" 2 1 "reason other than a counterparty"

# ── T26: the SAME Suite.Case id in two different binaries — the skip set
# arithmetic is only meaningful when a case id names one case. ──────────────
d="$WORK/t26"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA COMPLETED
write_json "$d/binB.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
printf 'Suite.CaseA\n' > "$WORK/t26-skips.txt"
run_check "T26 duplicate Suite.Case id across two binaries is rejected" \
  "$d" "$WORK/t26-skips.txt" 2 2 "appears in both"

# ── T27: a `failures` field that is present but not a list must be
# fail-closed, not read as falsy-and-therefore-not-failed. ──────────────────
d="$WORK/t27"; mkdir -p "$d"
python3 - "$d/binA.json" <<'PY'
import json, sys
tc = {"name": "CaseA", "file": "fixture.cpp", "line": 1, "status": "RUN",
      "result": "COMPLETED", "time": "0s", "classname": "Suite", "failures": ""}
doc = {"tests": 1, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 1, "failures": 0, "disabled": 0,
                        "testsuite": [tc]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
: > "$WORK/t27-skips.txt"
run_check "T27 a non-list failures field on a COMPLETED case is fail-closed" \
  "$d" "$WORK/t27-skips.txt" 1 2 "failures\` field that is a"

# ── T28-T29: the skip-message envelope must include both the leading
# `<file>:<line>\n` line and the trailing newline. ───────────────────────────
d="$WORK/t28"; mkdir -p "$d"
python3 - "$d/binA.json" "$PORT_OK_CPP" <<'PY'
import json, sys
msg = sys.argv[2] + "\n"
tc = {"name": "CaseA", "file": "fixture.cpp", "line": 1, "status": "RUN",
      "result": "SKIPPED", "time": "0s", "classname": "Suite",
      "skipped": [{"message": msg}]}
doc = {"tests": 1, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 1, "failures": 0, "disabled": 0,
                       "testsuite": [tc]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t28-skips.txt"
run_check "T28 skip message without a location line is a bad reason" \
  "$d" "$WORK/t28-skips.txt" 2 1 "reason other than a counterparty"

d="$WORK/t29"; mkdir -p "$d"
python3 - "$d/binA.json" "$PORT_OK_CPP" <<'PY'
import json, sys
msg = "fixture.cpp:1\n" + sys.argv[2]
tc = {"name": "CaseA", "file": "fixture.cpp", "line": 1, "status": "RUN",
      "result": "SKIPPED", "time": "0s", "classname": "Suite",
      "skipped": [{"message": msg}]}
doc = {"tests": 1, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 1, "failures": 0, "disabled": 0,
                       "testsuite": [tc]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t29-skips.txt"
run_check "T29 skip message without a trailing newline is a bad reason" \
  "$d" "$WORK/t29-skips.txt" 2 1 "reason other than a counterparty"

# ── T35-T36: the counterparty name and its port token must agree.
d="$WORK/t35"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "quickfix-cpp unavailable: INTEROP_QUICKFIX_J_PORT not set (parent harness did not lease a port)"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t35-skips.txt"
run_check "T35 quickfix-cpp reason carrying the J port token is caught" \
  "$d" "$WORK/t35-skips.txt" 2 1 "reason other than a counterparty"

d="$WORK/t36"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "quickfix-j unavailable: INTEROP_QUICKFIX_CPP_PORT not set (parent harness did not lease a port)"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/t36-skips.txt"
run_check "T36 quickfix-j reason carrying the CPP port token is caught" \
  "$d" "$WORK/t36-skips.txt" 2 1 "reason other than a counterparty"

# ── T37-T39: suite/case identity fields and suite arrays are required.
d="$WORK/t37"; mkdir -p "$d"
printf '%s' '{"testsuites":[{"testsuite":[{"name":"CaseA","status":"RUN","result":"COMPLETED"}]}]}' > "$d/binA.json"
: > "$WORK/t37-skips.txt"
run_check "T37 suite without a name fails closed" \
  "$d" "$WORK/t37-skips.txt" 1 2 "suite without a non-empty \`name\`"

d="$WORK/t38"; mkdir -p "$d"
printf '%s' '{"testsuites":[{"name":"Suite","testsuite":[{"status":"RUN","result":"COMPLETED"}]}]}' > "$d/binA.json"
: > "$WORK/t38-skips.txt"
run_check "T38 case without a name fails closed" \
  "$d" "$WORK/t38-skips.txt" 1 2 "testcase without a non-empty \`name\`"

d="$WORK/t39"; mkdir -p "$d"
printf '%s' '{"testsuites":[{"name":"Suite","testsuite":[{"name":"CaseA","status":"RUN","result":"COMPLETED"}]},{"name":"Sibling"}]}' > "$d/binA.json"
: > "$WORK/t39-skips.txt"
run_check "T39 sibling suite without testsuite fails closed" \
  "$d" "$WORK/t39-skips.txt" 1 2 "has no \`testsuite\` field"

# ── L1-L6: the checker enumerates each binary's own cases with
# --gtest_list_tests, outside its ctest registration, and requires the
# listed and reported Suite.Case sets to be equal in both directions — the
# structural replacement for the deleted ctest-JSON registration scan
# (fixpp#431 Gate B r6). ─────────────────────────────────────────────────────

# ── L1: a case the binary enumerates is absent from the run report (a
# filter/shard/wrapper/launcher omitted it). ────────────────────────────────
d="$WORK/l1"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA COMPLETED
b="$WORK/l1-bin"; mkdir -p "$b"
cat > "$b/binA" <<'PYEOF'
#!/usr/bin/env python3
import json, sys
out = None
for a in sys.argv[1:]:
    if a.startswith("--gtest_output=json:"):
        out = a.split(":", 1)[1]
doc = {"tests": 2, "testsuites": [{"name": "Suite",
       "testsuite": [{"name": "CaseA"}, {"name": "CaseZ"}]}]}
json.dump(doc, open(out, "w"))
PYEOF
chmod +x "$b/binA"
: > "$WORK/l1-skips.txt"
run_check "L1 a case enumerated by the binary is absent from the run report" \
  "$d" "$WORK/l1-skips.txt" 1 1 "enumerated but absent from the run report" "$b"

# ── L2: a case IN the run report that the binary does not enumerate (a
# stale or wrong --bin-dir). ─────────────────────────────────────────────────
d="$WORK/l2"; mkdir -p "$d"
python3 - "$d/binA.json" <<'PY'
import json, sys
mk = lambda n: {"name": n, "file": "fixture.cpp", "line": 1, "status": "RUN",
                "result": "COMPLETED", "time": "0s", "classname": "Suite"}
doc = {"tests": 2, "failures": 0, "disabled": 0, "errors": 0, "name": "AllTests",
       "testsuites": [{"name": "Suite", "tests": 2, "failures": 0, "disabled": 0,
                        "testsuite": [mk("CaseA"), mk("CaseB")]}]}
json.dump(doc, open(sys.argv[1], "w"))
PY
b="$WORK/l2-bin"; mkdir -p "$b"
cat > "$b/binA" <<'PYEOF'
#!/usr/bin/env python3
import json, sys
out = None
for a in sys.argv[1:]:
    if a.startswith("--gtest_output=json:"):
        out = a.split(":", 1)[1]
doc = {"tests": 1, "testsuites": [{"name": "Suite", "testsuite": [{"name": "CaseA"}]}]}
json.dump(doc, open(out, "w"))
PYEOF
chmod +x "$b/binA"
: > "$WORK/l2-skips.txt"
run_check "L2 a case in the run report is not enumerated by the binary" \
  "$d" "$WORK/l2-skips.txt" 1 1 "reported but not enumerated" "$b"

# ── L3: no binary under --bin-dir for a report — fail closed. ──────────────
d="$WORK/l3"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA COMPLETED
b="$WORK/l3-bin"; mkdir -p "$b"
: > "$WORK/l3-skips.txt"
run_check "L3 missing binary under --bin-dir fails closed" \
  "$d" "$WORK/l3-skips.txt" 1 2 "no binary for report" "$b"

# ── L4: the binary cannot be enumerated (a nonzero exit from
# --gtest_list_tests) — fail closed. ────────────────────────────────────────
d="$WORK/l4"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA COMPLETED
b="$WORK/l4-bin"; mkdir -p "$b"
cat > "$b/binA" <<'PYEOF'
#!/usr/bin/env python3
import sys
sys.exit(3)
PYEOF
chmod +x "$b/binA"
: > "$WORK/l4-skips.txt"
run_check "L4 a binary that cannot be enumerated fails closed" \
  "$d" "$WORK/l4-skips.txt" 1 2 "could not enumerate" "$b"

# ── L5: an inherited GTEST_FILTER must be scrubbed from the enumeration's
# own environment — a stub that only omits a case when IT sees GTEST_FILTER
# set proves the checker's child process does not inherit it, and the
# checker still catches the run report's real omission. ────────────────────
d="$WORK/l5"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA COMPLETED
b="$WORK/l5-bin"; mkdir -p "$b"
cat > "$b/binA" <<'PYEOF'
#!/usr/bin/env python3
import json, os, sys
out = None
for a in sys.argv[1:]:
    if a.startswith("--gtest_output=json:"):
        out = a.split(":", 1)[1]
cases = [{"name": "CaseA"}]
if "GTEST_FILTER" not in os.environ:
    cases.append({"name": "CaseB"})
doc = {"tests": len(cases), "testsuites": [{"name": "Suite", "testsuite": cases}]}
json.dump(doc, open(out, "w"))
PYEOF
chmod +x "$b/binA"
: > "$WORK/l5-skips.txt"
GTEST_FILTER='-Suite.CaseB' run_check \
  "L5 an inherited GTEST_FILTER is scrubbed from the enumeration's own environment" \
  "$d" "$WORK/l5-skips.txt" 1 1 "enumerated but absent from the run report" "$b"

# ── L6: pass control — the default mirroring stub (used by every cell above
# that passes no explicit bin-dir) on a clean multi-binary run. ─────────────
d="$WORK/l6"; mkdir -p "$d"
write_json "$d/binA.json" Suite CaseA SKIPPED "$PORT_OK_CPP"
write_json "$d/binB.json" Suite CaseB COMPLETED
printf 'Suite.CaseA\n' > "$WORK/l6-skips.txt"
run_check "L6 the default mirroring stub passes on a clean multi-binary run" \
  "$d" "$WORK/l6-skips.txt" 2 0 "PASS:"

CELLS_DECLARED=40
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
