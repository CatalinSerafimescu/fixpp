#!/usr/bin/env bash
# End-to-end seam check for the #267 measurement apparatus.
#
#   ci/test-parallelism-aba-seam.sh          # needs cmake + ctest on PATH
#
# ── WHY A SEPARATE HARNESS ───────────────────────────────────────────────────
#
# `ci/test-parallelism-verdict.sh` proves every gate in the verdict can fire.
# It does that against a SYNTHETIC sample, which is what makes it cheap enough
# to run on every push — and is also its blind spot: it never proves that
# `ci/run-parallelism-aba.sh` writes files of the shape the verdict reads.
#
# Two sweeps narrowing on different axes both report clean.  The verdict harness
# varies the CONTENT of a sample the verdict is known to parse; this varies
# nothing and checks the one thing that harness assumes — that the producer and
# the consumer agree at all.  A field renamed on one side of that seam would
# leave every synthetic cell green while every real campaign returned "0 of 3
# passes", and the cost of learning that is three suite runs on a CI runner.
#
# So this runs the REAL driver against a 12-test synthetic ctest project and
# feeds the REAL output to the REAL verdict.  It is the pre-flight for a
# campaign, not a unit test: it belongs immediately before the expensive jobs.
#
# ⚠️ IT RUNS ON EVERY PUSH, so its cost is a standing concern rather than a
# one-off. The cost CONDITION: two driver runs (one per platform branch) of
# three ctest passes each, plus eight machine-witness calls. Both are shrunk
# deliberately — short sleeps, and PARALLELISM_WITNESS_{ITERS,REPEATS} below.
# Re-derive rather than assume: `time ci/test-parallelism-aba-seam.sh`.
#
# ⚠️ AND IT MUST PROVE ITS OWN NEGATIVE.  A seam check that only ever runs the
# happy path would pass if the verdict accepted anything at all, so cell S3
# corrupts one field of the produced sample and requires the verdict to reject
# it.  Without that, "the halves agree" is a claim about a checker nobody
# watched refuse.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }

if ! command -v ctest >/dev/null || ! command -v cmake >/dev/null; then
  echo "::error::ci/test-parallelism-aba-seam.sh needs cmake and ctest on PATH."
  echo "Refusing to report a result without them — a skipped seam check is a silent pass,"
  echo "and this file exists because the synthetic harness cannot see this failure."
  exit 2
fi

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/ci"
# A directory that LOOKS like the repo to the driver: it resolves its root by
# walking up from its own location, so the scripts have to live under $WORK/ci.
for f in run-parallelism-aba.sh machine-witness.py measure-peak-rss.py parallelism-verdict.py; do
  cp "$HERE/$f" "$WORK/ci/$f"
done

cat > "$WORK/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(aba_seam NONE)
enable_testing()
# 12 x 0.4 s. TWELVE is the load-bearing number, not the duration: the
# max-in-flight oracle needs enough concurrent tests that 4 is unambiguously
# distinguishable from 1. The sleep only has to exceed process startup.
foreach(i RANGE 1 12)
  add_test(NAME seam_t${i} COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
endforeach()
CMAKE

# `execution: {jobs: 4}` ON PURPOSE, and it is the sharpest thing this file
# checks. The driver must beat a preset that already asks for parallelism, or
# pass A is not serial — and presets already carrying a `jobs` value are exactly
# the ones a campaign starts from, so a regression there would look PERFECT:
# all three passes would agree with each other while nothing was tested.
#
# ⚠️ THIS IS THE MEASUREMENT, not a record of one. The CLI flag overriding a
# preset (and `CTEST_PARALLEL_LEVEL` failing to) is not written down as a
# remembered number anywhere in this repo; cell S2 below re-establishes it on
# every run, against a preset that declares `jobs: 4`.
cat > "$WORK/CMakePresets.json" <<'PRESETS'
{"version": 6,
 "configurePresets": [{"name": "seam", "binaryDir": "build/seam"}],
 "testPresets": [{"name": "seam", "configurePreset": "seam", "execution": {"jobs": 4}}]}
PRESETS

# See ci/run-parallelism-aba.sh for why shrinking the witness is sound HERE and
# not in a campaign: this file checks the plumbing, and the calibration's
# magnitude is irrelevant to that. The verdict discloses the weakening on the
# page regardless, so it can never be mistaken for a full observation.
export PARALLELISM_WITNESS_ITERS=200000
export PARALLELISM_WITNESS_REPEATS=2

cd "$WORK" || exit 2
cmake --preset seam >/dev/null 2>&1 || { echo "::error::seam project failed to configure"; exit 2; }

echo "== A-B-A seam (real driver -> real verdict) =="

if ! bash ci/run-parallelism-aba.sh --preset seam --jobs 4 --out "$WORK/run" >"$WORK/driver.log" 2>&1; then
  sed 's/^/  | /' "$WORK/driver.log"
  bad "S0 the driver ran to completion"
else
  ok "S0 the driver ran to completion"
fi

# ⚠️ THE CALIBRATION TOLERANCE IS RELAXED HERE, AND ONLY IT. This file
# deliberately runs a WEAKENED witness (200k iterations, 2 repeats) so 8 witness
# calls cost a rounding error on a job that runs on every push. A weakened
# witness has correspondingly higher variance — and judging it against a
# tolerance calibrated for the full-strength one made this cell FLAKY, observed
# voiding on a contended host between two otherwise identical runs. An
# intermittent red on ci-script-pins is worse than no cell at all: it trains
# everyone to re-run.
#
# It is the tolerance the weakened input cannot support, so it is the tolerance
# that moves. Nothing else is relaxed, and this does not make the accept
# vacuous: the verdict's tolerances are pinned by T1/T9/T10/T28 of
# ci/test-parallelism-verdict.sh, where the inputs are synthetic and exact, and
# cell S3 below proves this same invocation still REJECTS a corrupted sample.
# What this file checks is the plumbing — that the driver writes what the
# verdict reads — which is exactly what those synthetic cells cannot see.
VERDICT_ARGS=(--calib-tolerance-pct 500)

# The witness files themselves, asserted directly rather than inferred from the
# verdict's happiness: a verdict that stopped reading them would otherwise still
# say VALID.
missing=""
for w in 0 1 2 3; do
  [ -s "$WORK/run/witness${w}.env" ] || missing="$missing witness${w}.env"
  grep -q '^status=' "$WORK/run/witness${w}.env" 2>/dev/null || missing="$missing witness${w}.env(no-status)"
done
if [ -n "$missing" ]; then
  bad "S1a the driver wrote all four machine witnesses —missing:$missing"
else
  ok "S1a the driver wrote all four machine witnesses, each with a status"
fi

out="$(python3 ci/parallelism-verdict.py "$WORK/run" "${VERDICT_ARGS[@]}" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -qF "VALID — this sample is evidence"; then
  ok "S1 the verdict accepts what the driver produced"
else
  printf '%s\n' "$out" | sed 's/^/  | /'
  bad "S1 the verdict accepts what the driver produced — exit $rc"
fi

# THE ASSERTION THIS FILE EXISTS FOR.  `--parallel 1` must have beaten the
# preset's `jobs: 4`, and `--parallel 4` must have taken effect.  Anything else
# means all three passes ran the same configuration.
a="$(sed -n 's/^ *Start /S/p' "$WORK/run/pass1.ctest.log" | wc -l)"
if printf '%s' "$out" | grep -qE '^\| A \| 1 \| 1 \|.*\| 1 \|' \
   && printf '%s' "$out" | grep -qE '^\| B \| 2 \| 4 \|.*\| 4 \|'; then
  ok "S2 --parallel beat the preset: A ran 1-in-flight, B ran 4 (${a} starts logged)"
else
  printf '%s\n' "$out" | sed 's/^/  | /'
  bad "S2 --parallel beat the preset — the in-flight oracle did not read 1 and 4"
fi

# S3: the negative. Corrupt one produced field and require a rejection, so S1's
# green means the verdict inspected the sample rather than accepting anything.
cp -r "$WORK/run" "$WORK/run-mut"
before="$(md5sum < "$WORK/run-mut/pass2.meta")"
sed -i 's/^san_count=.*/san_count=7/' "$WORK/run-mut/pass2.meta"
if [ "$before" = "$(md5sum < "$WORK/run-mut/pass2.meta")" ]; then
  bad "S3 MUTATION DID NOT APPLY — re-point it, do not delete the cell"
else
  mout="$(python3 ci/parallelism-verdict.py "$WORK/run-mut" "${VERDICT_ARGS[@]}" 2>&1)"; mrc=$?
  if [ "$mrc" -eq 1 ] && printf '%s' "$mout" | grep -qF "SANITIZER REPORT APPEARED"; then
    ok "S3 a corrupted sample is REJECTED (the accept in S1 was a judgement)"
  else
    printf '%s\n' "$mout" | sed 's/^/  | /'
    bad "S3 a corrupted sample was not rejected — exit $mrc"
  fi
fi

# ── S4: THE TWO PARSERS OF THE SAME LOG MUST AGREE ───────────────────────────
#
# ci/parallelism-verdict.py re-derives three quantities that ci/peak-memory-
# report.sh already reads out of a ctest log — the executed count, the total
# real time, and the summed per-test durations. They are in different languages
# (Python re vs awk), so they are re-derivations, not copies, and a comment
# saying "if you change one, change both" is an instruction someone has to
# remember. This runs the shipped awk over the REAL log the driver just produced
# and requires the same three numbers. It is the version of that claim that
# cannot rot.
LOG="$WORK/run/pass1.ctest.log"
awk_ran="$(awk 'match($0, /^[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+$/) { m = $0; sub(/.* out of /, "", m); n = m } END { print n }' "$LOG")"
awk_real="$(awk -F'= *' '/Total Test time \(real\)/ { t = $2 } END { gsub(/[^0-9.]/, "", t); print t }' "$LOG")"
awk_sum="$(awk '/^ *[0-9]+\/[0-9]+ +Test +#[0-9]+/ && match($0, /[0-9.]+ sec$/) { s += substr($0, RSTART, RLENGTH-4) } END { if (s > 0) printf "%.1f", s }' "$LOG")"
py="$(cd "$WORK" && python3 -c '
import pathlib, sys
sys.path.insert(0, "ci")
import importlib.util
spec = importlib.util.spec_from_file_location("v", "ci/parallelism-verdict.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
d = m.parse_ctest_log(pathlib.Path(sys.argv[1]))
print(f"{d["ran"]} {d["real_s"]} {d["sum_s"]}")' "$LOG")"
# ⚠️ The awk figures must be NON-EMPTY first. Two empty strings compare equal,
# so a broken awk would agree with a broken parser and this cell would pass
# having compared nothing.
#
# ⚠️ AND THE COMPARISON IS NUMERIC, NOT TEXTUAL. It was a string compare, and it
# went red on main for a ctest total of `4.90`: awk's gsub preserves the log's
# trailing zero, Python's float() drops it, so `4.90` != `4.9` as text while the
# two parsers agreed perfectly on the VALUE. It passed locally only because the
# runs happened to produce timings without a trailing zero — a cell whose verdict
# depended on the decimal representation of a wall time. What this cell exists to
# check is that the two parsers read the same NUMBERS out of one log; comparing
# their formatting is a different and worthless question.
if [ -z "$awk_ran" ] || [ -z "$awk_real" ] || [ -z "$awk_sum" ]; then
  bad "S4 the shipped awk read NOTHING from a real log (ran='$awk_ran' real='$awk_real' sum='$awk_sum') — the comparison would have been vacuous"
else
  # shellcheck disable=SC2086  # $py is three fields and MUST word-split into
  # three argv entries; quoting it would pass one string and the compare would
  # silently become "1 vs 3 values".
  s4_verdict="$(python3 -c '
import sys
awk = sys.argv[1:4]
py  = sys.argv[4:7]
try:
    a = [float(x) for x in awk]
    b = [float(x) for x in py]
except ValueError as exc:
    print(f"UNPARSABLE {exc}"); raise SystemExit(0)
print("AGREE" if a == b else f"DISAGREE awk={awk} verdict={py}")' \
    "$awk_ran" "$awk_real" "$awk_sum" $py)"
  case "$s4_verdict" in
    AGREE) ok "S4 both parsers agree on the same real log (ran=$awk_ran real=$awk_real sum=$awk_sum)" ;;
    *)     bad "S4 $s4_verdict" ;;
  esac
fi

# ── S6: SAN_PATTERN must equal the shipped CI gate's ─────────────────────────
#
# ci/run-parallelism-aba.sh counts sanitizer reports with a pattern copied from
# ci/peak-memory-report.sh, and the agreement was written as a comment saying
# "kept in step". That is an instruction someone has to follow. The consequence
# if they do not is subtle and bad: a campaign would report a "new report at
# higher concurrency" that is only a DIFFERENT DEFINITION of a report — and
# item 5 makes such a finding a real defect until disproven, so a spurious one
# costs a real investigation.
#
# ⚠️ THIS CELL CHECKS THE PATTERN AND NOTHING MORE, and the distinction matters
# because the two files deliberately report DIFFERENT QUANTITIES from it: the CI
# gate subtracts ci/expected-sanitizer-reports.txt to get an UNEXPLAINED count,
# the driver keeps the raw one. Both are right for their own question
# (absolute vs pass-to-pass) — see the driver's own comment. Asserting more than
# pattern equality here would pin an agreement that does not exist.
#
# Same standard as S4 above, applied to the other duplicated constant.
a="$(sed -n "s/^SAN_PATTERN=//p" "$HERE/run-parallelism-aba.sh")"
b="$(sed -n "s/^SAN_PATTERN=//p" "$HERE/peak-memory-report.sh")"
if [ -z "$a" ] || [ -z "$b" ]; then
  bad "S6 SAN_PATTERN not found in one or both files (aba='$a' report='$b') — the comparison would have been vacuous"
elif [ "$a" = "$b" ]; then
  ok "S6 SAN_PATTERN agrees with ci/peak-memory-report.sh"
else
  bad "S6 SAN_PATTERN DISAGREES — aba: $a  report: $b"
fi

# ── S7: THE COVERAGE DIGEST MUST DISTINGUISH, AND MUST STILL COLLIDE ─────────
#
# Acceptance item 4 turns entirely on this digest, so it needs BOTH directions
# proven — a canonicalisation that never collides is as useless as one that
# always does.
#
#  * DISTINGUISH: a hostile review showed two genuinely different lcov reports
#    hashing IDENTICALLY under a plain `sort`. Move `DA:1,1` from a.cc to b.cc
#    and `DA:2,0` the other way and the sorted line MULTISET is unchanged —
#    coverage migrates between files while all three passes "agree", which is
#    the one thing item 4 exists to detect. Every line is now keyed by its `SF:`
#    record before sorting.
#  * STILL COLLIDE: the sort is not decoration. `llvm-cov export` emits
#    per-object sections in an order that follows the object list and the
#    filesystem, neither of which is a coverage fact; hashing raw would report a
#    difference on every run until the check was discarded as noisy.
#
# ⚠️ THE RECIPE IS NOT RETYPED HERE — the cell runs ci/lcov-coverage-key.awk,
# the same file the driver runs. It used to be a hand-copied one-liner guarded
# by a grep asserting the driver still contained the identical text, which is a
# byte-identity check on two copies: it proves they agree, never that either is
# right. The grep survives in a narrower role below — that the driver actually
# INVOKES this file — because a cell testing an awk the driver has stopped
# calling is the failure the old guard was built for.
# ⚠️ `canon` RUNS THE SHIPPED DIGEST, NOT A REPRODUCTION OF IT. It used to run
# the key awk and hash the result here, with a grep asserting the driver still
# mentioned that awk. A hostile review produced the mutation that beats that
# arrangement — leave the awk call in place, hash `$info` instead of the keyed
# output — which restores the original defect while the grep, all of S9's arms,
# S10 and the execution tally stay green. A grep proves a call EXISTS, never
# that its RESULT is used. The digest now lives behind one entry point that both
# sides run, so a driver-only divergence is not expressible.
canon() {
  local f; f="$(mktemp)"; cat > "$f"
  "$HERE/lcov-coverage-digest.sh" "$f" | sed -n 's/^sorted_info_sha256=//p'
  rm -f "$f"
}
# The remaining grep is narrower and still worth keeping: it asserts the DRIVER
# reaches the same entry point. If it stops, these cells are exercising code
# nothing runs.
#
# ⚠️ MATCHES THE INVOCATION FORM, AND ACCEPTS ONE OR MORE. A bare
# `lcov-coverage-digest.sh` also matched the driver's COMMENT about it, so the
# `-ne 1` test below reddened at 2 the moment the file was mentioned in prose —
# a cell failing on documentation. Counting call sites exactly would break again
# the day a second legitimate call appears; presence is the property meant.
# shellcheck disable=SC2016  # `$HERE` is the driver's, not this shell's — it must stay literal
recipe="$(grep -c '"\$HERE/lcov-coverage-digest.sh"' "$HERE/run-parallelism-aba.sh")"
A="$(printf 'SF:a.cc\nDA:1,1\nend_of_record\nSF:b.cc\nDA:2,0\nend_of_record\n' | canon)"
B="$(printf 'SF:a.cc\nDA:2,0\nend_of_record\nSF:b.cc\nDA:1,1\nend_of_record\n' | canon)"
C="$(printf 'SF:b.cc\nDA:2,0\nend_of_record\nSF:a.cc\nDA:1,1\nend_of_record\n' | canon)"
if [ "${recipe:-0}" -lt 1 ]; then
  bad "S7 the driver's canonicalisation recipe was not found (${recipe:-0} matches) — this cell is testing a copy, not the shipped code"
elif [ "$A" = "$B" ]; then
  bad "S7 coverage MIGRATING BETWEEN FILES produced an identical digest ($A)"
elif [ "$A" != "$C" ]; then
  bad "S7 a mere RECORD-ORDER difference changed the digest ($A vs $C) — every run would differ"
else
  ok "S7 the coverage digest distinguishes moved coverage and still ignores record order"
fi

# ── S5: THE --no-peak PATH, i.e. the Windows lanes ───────────────────────────
#
# `windows-msvc-asan` is the matrix critical path and the lane a campaign most
# needs to decide, and it is the ONLY lane whose driver branch nothing else
# exercises. Two things have to hold: the missing peak reading is DISPOSITIONED
# rather than absent, and no fabricated `0.00 GiB` reaches a VALID summary —
# `int("" or 0)` is 0, so that clause formats perfectly well from nothing.
rm -rf "$WORK/run-np"
if ! bash ci/run-parallelism-aba.sh --preset seam --jobs 4 --out "$WORK/run-np" --no-peak      >"$WORK/driver-np.log" 2>&1; then
  sed 's/^/  | /' "$WORK/driver-np.log"
  bad "S5 the --no-peak driver path ran to completion"
else
  npout="$(python3 ci/parallelism-verdict.py "$WORK/run-np" "${VERDICT_ARGS[@]}" 2>&1)"; nprc=$?
  if ! grep -q '^status=no-procfs-platform$' "$WORK/run-np/pass2.peak.env"; then
    sed 's/^/  | /' "$WORK/run-np/pass2.peak.env"
    bad "S5 --no-peak did not disposition the missing reading"
  elif [ "$nprc" -ne 0 ]; then
    printf '%s
' "$npout" | sed 's/^/  | /'
    bad "S5 a --no-peak sample did not reach VALID (exit $nprc)"
  elif printf '%s' "$npout" | grep -qF "0.00 GiB"; then
    printf '%s
' "$npout" | sed 's/^/  | /'
    bad "S5 a fabricated 0.00 GiB reached a VALID --no-peak summary"
  elif ! printf '%s' "$npout" | grep -qF "NOT MEASURED"; then
    printf '%s
' "$npout" | sed 's/^/  | /'
    bad "S5 the missing peak reading was not declared NOT MEASURED"
  else
    ok "S5 --no-peak: VALID, peak declared NOT MEASURED, no fabricated zero"
  fi
fi

# ── S8: THE DECOY, ON A REAL ctest RUN ───────────────────────────────────────
#
# Every other check of the log parser is synthetic — strings this file wrote,
# fed to a function. This one makes ctest itself produce the hostile input, and
# it is the cell that showed the attack is LIVE rather than theoretical: a test
# echoing `    Start 99: ...` puts that line in the log under
# `--output-on-failure` and lifts max_inflight from 1 to 2 on a SERIAL pass. A
# forged parallel level is exactly what the A-vs-A' check cannot see.
#
# It also pins the narrowness of the fail-closed side, which is the reason the
# strict parser is affordable at all: --output-on-failure prints a test's output
# only when that test FAILS, so on an all-green pass no test output reaches this
# log — and a pass with a failing test is already an INSTRUMENT FAILURE by the
# ctest_status check. The two dispositions agree.
mkdir -p "$WORK/decoy"
cat > "$WORK/decoy/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(decoy NONE)
enable_testing()
foreach(i RANGE 1 4)
  add_test(NAME d${i} COMMAND ${CMAKE_COMMAND} -E sleep 0.2)
endforeach()
add_test(NAME decoy COMMAND ${CMAKE_COMMAND} -E echo "    Start 99: fake_test")
set_tests_properties(decoy PROPERTIES WILL_FAIL TRUE)
add_test(NAME realfail COMMAND ${CMAKE_COMMAND} -E false)
CMAKE
cat > "$WORK/decoy/CMakePresets.json" <<'CMAKE'
{"version": 6,
 "configurePresets": [{"name": "d", "binaryDir": "build/d"}],
 "testPresets": [{"name": "d", "configurePreset": "d"}]}
CMAKE
( cd "$WORK/decoy" && cmake --preset d >/dev/null 2>&1   && ctest --preset d --output-on-failure --parallel 1 > "$WORK/decoy/log.txt" 2>&1 )
if ! grep -q 'Start 99' "$WORK/decoy/log.txt"; then
  bad "S8 the decoy line never reached the ctest log — this cell tested nothing"
else
  parsed="$(cd "$WORK" && python3 -c '
import importlib.util, pathlib, sys
spec = importlib.util.spec_from_file_location("v", "ci/parallelism-verdict.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
d = m.parse_ctest_log(pathlib.Path(sys.argv[1]))
print(len(d["anomalies"]), d["max_inflight"])' "$WORK/decoy/log.txt")"
  n_anom="${parsed%% *}"; infl="${parsed##* }"
  if [ "$infl" = "1" ]; then
    bad "S8 the decoy did not forge the parallel level (max_inflight=$infl) — the cell is vacuous, re-point it"
  elif [ "${n_anom:-0}" -eq 0 ]; then
    bad "S8 a REAL ctest log carrying a forged Start line parsed CLEAN (max_inflight=$infl)"
  else
    ok "S8 a real ctest log's decoy forged max_inflight to $infl and was REFUSED ($n_anom anomalies)"
  fi
fi

# ── S9: EXECUTION COUNTS MUST COLLIDE, COVERED/UNCOVERED MUST NOT ───────────
#
# The defect S9 exists for shipped and voided a real sample. The digest was
# taken over the raw lcov, which carries per-line and per-function EXECUTION
# COUNTS; those move run-to-run for any timing-dependent loop, so two SERIAL
# passes that had covered exactly the same code disagreed and item 4 voided its
# own baseline. The condition and the re-derivation recipe are in
# ci/lcov-coverage-key.awk; the figures are deliberately not copied to a third
# file, and "it would have voided EVERY sample" was an overgeneralisation from
# one observed pair.
#
# ⚠️ BOTH ARMS OR NEITHER. A normalisation that maps everything to the same
# value collides on the counts AND on a real coverage change, and reports clean
# forever — strictly worse than the over-voiding it replaced, because an
# over-voiding gate is loud. The second arm is what forbids that.
cov_a="$(printf 'SF:a.cc\nDA:1,1\nDA:2,7\nFNDA:3,f\nend_of_record\n'   | canon)"
cov_b="$(printf 'SF:a.cc\nDA:1,9\nDA:2,4000\nFNDA:1,f\nend_of_record\n' | canon)"
cov_c="$(printf 'SF:a.cc\nDA:1,1\nDA:2,0\nFNDA:3,f\nend_of_record\n'    | canon)"
cov_d="$(printf 'SF:a.cc\nDA:1,1\nDA:2,7\nFNDA:0,f\nend_of_record\n'    | canon)"
if [ "$cov_a" != "$cov_b" ]; then
  bad "S9 two reports differing ONLY in execution counts produced different digests ($cov_a vs $cov_b) — every coverage sample would void"
elif [ "$cov_a" = "$cov_c" ]; then
  bad "S9 a line going COVERED -> UNCOVERED did not change the digest ($cov_a) — the normalisation is inert"
elif [ "$cov_a" = "$cov_d" ]; then
  bad "S9 a function going COVERED -> UNCOVERED did not change the digest ($cov_a) — the normalisation is inert"
else
  ok "S9 the digest ignores execution counts and still sees covered -> uncovered"
fi

# ── S10: THE BRANCH-DATA EXCLUSION IS A DECLARED SCOPE LIMIT ────────────────
#
# ci/lcov-coverage-key.awk drops `BRDA:`/`BRH:`/`BRF:` because branch coverage in
# this suite has been observed to move between passes at FIXED concurrency while
# line and function coverage did not. Including it leaves the baseline unable to
# agree with itself. The condition, and how to re-derive it on any sample, are in
# the awk; the numbers are not repeated here or there.
#
# That is a real limit: a widening that changed ONLY branch coverage is invisible
# to item 4. This cell exists so the limit is a TESTED property rather than a
# paragraph — if someone re-includes branch data to "improve" the digest, the
# false void comes back and this cell is where they are told why.
#
# ⚠️ WHAT THIS CELL DOES NOT DO. It says nothing about `branch_records_in_digest`
# being emitted: it compares two digests, and deleting that key from the driver
# leaves it green. The claim that it "pins the disclosure" was here and was
# false. Nothing asserts the disclosure is present — an accepted gap, recorded
# rather than papered over, and tolerable only because the key is COUNTED from
# the hashed bytes, so it can be absent but cannot be wrong.
# ⚠️ ONE ARM, NOT TWO. This cell shipped with a second arm that grepped the
# driver's source for the literal `branch_records_in_digest=0` — which is the
# byte-identity-between-two-copies pattern S7's own rewrite above removes, added
# back in the same change that removed it. It also checked nothing arm 1 does
# not: deleting the exclusion rule from the awk reddens arm 1 on its own
# (verified). The driver now COUNTS that key from the hashed bytes instead of
# asserting it, so there is no literal left to spell-check.
br_a="$(printf 'SF:a.cc\nDA:1,1\nBRDA:1,0,0,5\nBRH:1\nBRF:1\nend_of_record\n' | canon)"
br_b="$(printf 'SF:a.cc\nDA:1,1\nBRDA:1,0,0,0\nBRH:0\nBRF:1\nend_of_record\n' | canon)"
if [ "$br_a" != "$br_b" ]; then
  bad "S10 a flipped BRANCH taken-bit changed the digest — branch data is back in the key and the baseline will void against itself"
else
  ok "S10 branch data is excluded from the digest"
fi

SEAM_DECLARED=12
TOTAL=$((PASS + FAIL))
echo
if [ "$TOTAL" -ne "$SEAM_DECLARED" ]; then
  echo "aba-seam: EXECUTION COUNT MISMATCH — ran ${TOTAL} cells, declared ${SEAM_DECLARED}."
  exit 1
fi
echo "aba-seam: ${PASS} passed, ${FAIL} failed (${TOTAL} cells)"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
