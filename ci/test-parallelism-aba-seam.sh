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
# leave 24 synthetic cells green while every real campaign returned "0 of 3
# passes", and the cost of learning that is three suite runs on a CI runner.
#
# So this runs the REAL driver against a 12-test synthetic ctest project (~35 s)
# and feeds the REAL output to the REAL verdict.  It is the pre-flight for a
# campaign, not a unit test: it belongs immediately before the expensive jobs.
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

command -v ctest >/dev/null && command -v cmake >/dev/null || {
  echo "::error::ci/test-parallelism-aba-seam.sh needs cmake and ctest on PATH."
  echo "Refusing to report a result without them — a skipped seam check is a silent pass,"
  echo "and this file exists because the synthetic harness cannot see this failure."
  exit 2
}

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
# 12 x 1 s: long enough that --parallel 4 is unambiguously visible in the
# max-in-flight oracle, short enough to run before every campaign.
foreach(i RANGE 1 12)
  add_test(NAME seam_t${i} COMMAND ${CMAKE_COMMAND} -E sleep 1)
endforeach()
CMAKE

# `execution: {jobs: 4}` ON PURPOSE, and it is the sharpest thing this file
# checks.  The driver must beat a preset that already asks for parallelism, or
# pass A is not serial.  MEASURED: `--parallel 1` overrides this (8.03 s vs
# 2.01 s on an 8x1 s suite) but `CTEST_PARALLEL_LEVEL=1` does NOT.  Two shipped
# lanes carry jobs=4, so a regression to the env-var form would corrupt exactly
# the lanes a campaign starts from — and would look perfect, because all three
# passes would agree.
cat > "$WORK/CMakePresets.json" <<'PRESETS'
{"version": 6,
 "configurePresets": [{"name": "seam", "binaryDir": "build/seam"}],
 "testPresets": [{"name": "seam", "configurePreset": "seam", "execution": {"jobs": 4}}]}
PRESETS

cd "$WORK" || exit 2
cmake --preset seam >/dev/null 2>&1 || { echo "::error::seam project failed to configure"; exit 2; }

echo "== A-B-A seam (real driver -> real verdict) =="

if ! bash ci/run-parallelism-aba.sh --preset seam --jobs 4 --out "$WORK/run" >"$WORK/driver.log" 2>&1; then
  sed 's/^/  | /' "$WORK/driver.log"
  bad "S0 the driver ran to completion"
else
  ok "S0 the driver ran to completion"
fi

# The tolerances are the DEFAULTS.  A seam check that relaxed them would not be
# checking the configuration a campaign runs with.
out="$(python3 ci/parallelism-verdict.py "$WORK/run" 2>&1)"; rc=$?
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
  mout="$(python3 ci/parallelism-verdict.py "$WORK/run-mut" 2>&1)"; mrc=$?
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
if [ -z "$awk_ran" ] || [ -z "$awk_real" ] || [ -z "$awk_sum" ]; then
  bad "S4 the shipped awk read NOTHING from a real log (ran='$awk_ran' real='$awk_real' sum='$awk_sum') — the comparison would have been vacuous"
elif [ "$py" = "$awk_ran $awk_real $awk_sum" ]; then
  ok "S4 both parsers agree on the same real log ($py)"
else
  bad "S4 parsers DISAGREE — awk: '$awk_ran $awk_real $awk_sum'  verdict: '$py'"
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
  npout="$(python3 ci/parallelism-verdict.py "$WORK/run-np" 2>&1)"; nprc=$?
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

SEAM_DECLARED=6
TOTAL=$((PASS + FAIL))
echo
if [ "$TOTAL" -ne "$SEAM_DECLARED" ]; then
  echo "aba-seam: EXECUTION COUNT MISMATCH — ran ${TOTAL} cells, declared ${SEAM_DECLARED}."
  exit 1
fi
echo "aba-seam: ${PASS} passed, ${FAIL} failed (${TOTAL} cells)"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
