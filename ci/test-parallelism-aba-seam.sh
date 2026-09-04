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

SEAM_DECLARED=4
TOTAL=$((PASS + FAIL))
echo
if [ "$TOTAL" -ne "$SEAM_DECLARED" ]; then
  echo "aba-seam: EXECUTION COUNT MISMATCH — ran ${TOTAL} cells, declared ${SEAM_DECLARED}."
  exit 1
fi
echo "aba-seam: ${PASS} passed, ${FAIL} failed (${TOTAL} cells)"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
