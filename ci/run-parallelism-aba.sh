#!/usr/bin/env bash
# Run one lane's A-B-A parallelism sample (#267) into a directory the verdict
# script can judge.
#
#   ci/run-parallelism-aba.sh --preset P --jobs N --out DIR
#                             [--ctest-args "..."] [--subset REGEX] [--no-peak]
#                             [--coverage]
#
# ── THE DESIGN, AND WHY IT IS NOT AN A/B ─────────────────────────────────────
#
# `ci/ctest-parallelism-probe.md` records three `execution.jobs` conclusions
# that were published and then WITHDRAWN, all for the same reason: they compared
# two SEPARATE CI jobs, on lanes whose between-VM wall-clock spread is large
# enough that VM luck alone explains the result. That document holds the
# figures — including a lane whose serial time on one VM was roughly half its
# time on three others, which is where every "no gain" reading for it came
# from. They are not restated here, because a number copied into four files is
# a number that will be corrected in one.
#
# The only design that survived is three passes in ONE job on ONE VM —
# serial, parallel, serial — voided unless the two serial passes agree.  That is
# what this script runs.  Everything else here exists to stop one of the three
# passes quietly measuring something different from the other two.
#
# ── WHAT IS EQUALISED BETWEEN PASSES, AND WHY EACH ONE MATTERS ───────────────
#
#  * `CTestCostData.txt` IS DELETED BEFORE EVERY PASS.  CTest writes per-test
#    durations there and schedules longest-first from them on the NEXT run.
#    Left alone, pass B would inherit pass A's cost data and pack better than
#    any production run ever can — production always starts from a fresh
#    checkout and a fresh build tree, so it never has cost data.  The measured
#    speedup would then be of a configuration that does not ship.
#  * `LastTest.log` IS COPIED OUT AFTER EVERY PASS.  CTest overwrites it, so
#    pass A' would otherwise destroy pass B's — and pass B is the only one whose
#    sanitizer output is interesting (#267 acceptance item 5 is precisely "did a
#    report appear at higher concurrency").
#  * COVERAGE PROFILES GO TO A PER-PASS DIRECTORY.  Three passes writing into
#    one `profiles/` dir merge into one report covering all three, and
#    "coverage identical before and after" becomes vacuously true.
#  * THE PARALLEL LEVEL IS SET ON THE COMMAND LINE, NEVER IN THE ENVIRONMENT.
#    MEASURED, and the obvious alternative is the WRONG one: the CLI flag
#    overrides a preset's `execution: {jobs: N}`; `CTEST_PARALLEL_LEVEL` does
#    NOT — the preset wins. Presets already carrying a `jobs` value are exactly
#    the ones a campaign starts from, so the env-var form would have run all
#    three passes at the preset's level and A-vs-A' would then agree PERFECTLY
#    while nothing was tested. Not left as a remembered measurement: cell S2 of
#    ci/test-parallelism-aba-seam.sh runs both passes against a preset that
#    declares `jobs: 4` and requires the in-flight oracle to read 1 and then N.
#    ci/parallelism-verdict.py checks the achieved level independently as well;
#    this is the other half of the same guard.
#
# ── EXIT STATUS ──────────────────────────────────────────────────────────────
#
# Always 0 unless the script could not run at all.  A ctest pass that FAILS is
# recorded (`ctest_status=` in that pass's meta) and the remaining passes still
# run — a half-collected sample tells you which pass broke, and aborting would
# leave a directory the verdict cannot distinguish from a workflow bug.  The
# judgement is ci/parallelism-verdict.py's job, in one place.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"

# ⚠️ RESOLVE THE INTERPRETER ONCE. `python3` is not guaranteed to be on PATH in
# Git Bash on a windows-2022 runner, where `python` is the name the setup-python
# action puts there — and the Windows lanes are the ones this script was
# extended for. A `python3` that does not resolve would leave every witness file
# unwritten, and the verdict would then VOID every Windows sample for want of an
# observation that was never attempted: a three-suite-run diagnosis of a PATH
# problem.
PY="${PYTHON:-python3}"
command -v "$PY" >/dev/null 2>&1 || PY=python
command -v "$PY" >/dev/null 2>&1 || { echo "::error::no python interpreter on PATH (tried python3, python)"; exit 2; }

PRESET=""; JOBS=""; OUT=""; CTEST_ARGS=""; SUBSET=""; NO_PEAK=0; COVERAGE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --preset)      PRESET="$2"; shift 2 ;;
    --jobs)        JOBS="$2"; shift 2 ;;
    --out)         OUT="$2"; shift 2 ;;
    --ctest-args)  CTEST_ARGS="$2"; shift 2 ;;
    --subset)      SUBSET="$2"; shift 2 ;;
    --no-peak)     NO_PEAK=1; shift ;;
    --coverage)    COVERAGE=1; shift ;;
    *) echo "::error::unknown argument: $1"; exit 2 ;;
  esac
done
if [ -z "$PRESET" ] || [ -z "$JOBS" ] || [ -z "$OUT" ]; then
  echo "::error::usage: $0 --preset P --jobs N --out DIR [--ctest-args ...] [--subset R] [--no-peak] [--coverage]"
  exit 2
fi
mkdir -p "$OUT"

BUILD="$REPO/build/$PRESET"
TMPDIR_CTEST="$BUILD/Testing/Temporary"

# ⚠️ THE PATTERN IS IDENTICAL TO ci/peak-memory-report.sh's; THE QUANTITY IS
# NOT, AND THAT IS DELIBERATE.  Saying they are "kept in step" would overstate
# it: that script reports UNEXPLAINED = raw minus whatever
# ci/expected-sanitizer-reports.txt allowlists for the lane, because it asks an
# ABSOLUTE question ("did a report fire that nobody has accounted for?") and one
# lane emits one by design on every run — an always-on warning is one nobody
# reads.
#
# This records the RAW count, because the question here is RELATIVE: did MORE
# reports appear at higher concurrency than at j=1, in the same job on the same
# VM?  A report that fires by design fires in all three passes and cancels, and
# subtracting the allowlist would additionally hide a SECOND occurrence of the
# allowlisted kind — which under `--parallel` is exactly the finding item 5
# exists for.
#
# What must stay in step is the PATTERN, so the two files agree on what counts
# as a report at all; cell S6 of ci/test-parallelism-aba-seam.sh checks that and
# nothing more.
SAN_PATTERN='WARNING: ThreadSanitizer:|ERROR: (Address|Leak|Memory)Sanitizer:|runtime error:'

# Overridable, matching tools/run_coverage.sh rather than hardcoding the
# versioned names: this is the THIRD copy of the profdata-merge/lcov-export
# recipe in the repo (tools/run_coverage.sh and tier1.yml's coverage job are the
# others), and the override form is the axis on which a hardcoded copy diverges
# first — a toolchain bump would leave this one silently unable to produce a
# digest at all, on the one lane acceptance item 4 is about.
LLVM_PROFDATA="${LLVM_PROFDATA:-llvm-profdata-22}"
LLVM_COV="${LLVM_COV:-llvm-cov-22}"

# Never fatal — a missing witness must degrade the sample toward VOID, not abort
# a campaign mid-lane. But it is ANNOUNCED: a silent `|| true` would leave the
# diagnosis inferable only from the verdict's "0 usable machine witness(es)"
# three suite runs later.
# ⚠️ THE TWO KNOBS BELOW ARE FOR ci/test-parallelism-aba-seam.sh, NOT FOR A
# CAMPAIGN, and they are safe there for a specific reason: the seam check tests
# the PLUMBING — that the driver writes witness files the verdict can read — and
# the calibration's absolute value is never used as an absolute by anything.
# Shrinking it changes nothing the seam asserts, and turns eight witness calls
# into a rounding error on a job that runs on every push.
#
# ⚠️ Setting them in a real campaign WOULD degrade the observation, so it is not
# left to trust: machine-witness.py records `iters` and `repeats` in every
# report, and ci/parallelism-verdict.py discloses on the page when they are
# below the shipped defaults. A weakened witness cannot pass itself off as a
# full one.
#
# The CONDITION, since the cost is what justifies them: eight witness calls run
# per seam check, on a job that runs on every push. If that stops being a
# rounding error, this is the knob — `time ci/test-parallelism-aba-seam.sh`.
WITNESS_ARGS=()
[ -n "${PARALLELISM_WITNESS_ITERS:-}" ]   && WITNESS_ARGS+=(--iters "$PARALLELISM_WITNESS_ITERS")
[ -n "${PARALLELISM_WITNESS_REPEATS:-}" ] && WITNESS_ARGS+=(--repeats "$PARALLELISM_WITNESS_REPEATS")

witness() {
  "$PY" "$HERE/machine-witness.py" --out "$OUT/witness$1.env" --label "$2" \
    ${WITNESS_ARGS+"${WITNESS_ARGS[@]}"} \
    || echo "::warning::#267 machine witness $1 ($2) failed to run; the sample will VOID for want of an observation of the machine."
}

# $1 = pass index, $2 = label, $3 = parallel level
run_pass() {
  local idx="$1" label="$2" par="$3"
  local log="$OUT/pass${idx}.ctest.log"
  local rc=0

  # See the header: production never has cost data, so neither may any pass.
  rm -f "$TMPDIR_CTEST/CTestCostData.txt"

  # `--no-tests=error`: tier1's own test steps carry it, and it matters more
  # here than there. `ctest -R` exits 0 when the filter matches NOTHING, so a
  # mistyped `--subset` would produce three passes of zero tests that agree
  # perfectly with each other — a VOID at best, and a confusing one. This makes
  # the empty selection fail at ctest, where the message says so.
  # `--timeout 1800` is the repo's wedge guard; see tier1.yml's first Test step
  # for why it exists and why it must not be tightened into a performance
  # assertion.
  local -a cmd=(ctest --preset "$PRESET" --output-on-failure --no-tests=error
                --timeout 1800 --parallel "$par")
  # Word-splitting is INTENDED here: --ctest-args carries the lane's own filter
  # (e.g. `-LE packaging`), which must match what the lane really runs or the
  # measurement describes a workload that does not ship.
  # shellcheck disable=SC2206
  [ -n "$CTEST_ARGS" ] && cmd+=($CTEST_ARGS)
  [ -n "$SUBSET" ] && cmd+=(-R "$SUBSET")

  if [ "$COVERAGE" -eq 1 ]; then
    # Per-pass profile directory — see the header.  The value mirrors the one
    # tier1.yml's coverage job sets as a STEP-LEVEL override, which is what CI
    # actually runs; the conan profile's stricter `%m_%p` is not what this lane
    # uses and measuring against it would answer a question nobody asked.
    local prof="$BUILD/profiles-pass${idx}"
    rm -rf "$prof"; mkdir -p "$prof"
    export LLVM_PROFILE_FILE="$prof/default-%p.profraw"
  fi

  echo "── pass ${idx} (${label}): --parallel ${par} ──"
  if [ "$NO_PEAK" -eq 1 ]; then
    # No /proc on this platform, so there is no concurrent-RSS instrument to
    # run.  Say that in the report rather than leaving the file absent: an
    # absent file is indistinguishable from an instrument that crashed, and the
    # verdict must be able to tell "not measurable here" from "went wrong".
    "${cmd[@]}" > "$log" 2>&1 || rc=$?
    cat "$log"
    printf 'label=%s\nstatus=no-procfs-platform\npeak_bytes=\ncmd_status=%s\n' \
      "$PRESET" "$rc" > "$OUT/pass${idx}.peak.env"
  else
    "$PY" "$HERE/measure-peak-rss.py" \
      --out "$OUT/pass${idx}.peak.env" --log "$log" --label "$PRESET" \
      -- "${cmd[@]}" || rc=$?
  fi

  # CTest overwrites this on the next pass — see the header.
  local san="unreadable"
  if [ -r "$TMPDIR_CTEST/LastTest.log" ]; then
    cp "$TMPDIR_CTEST/LastTest.log" "$OUT/pass${idx}.lasttest.log"
    san="$(grep -cE "$SAN_PATTERN" "$OUT/pass${idx}.lasttest.log" 2>/dev/null || true)"
    san="${san:-0}"
  fi

  {
    printf 'preset=%s\nlabel=%s\njobs=%s\norder=%s\n' "$PRESET" "$label" "$par" "$idx"
    printf 'san_count=%s\nsubset=%s\nctest_status=%s\nctest_args=%s\n' \
      "$san" "$SUBSET" "$rc" "$CTEST_ARGS"
  } > "$OUT/pass${idx}.meta"

  if [ "$COVERAGE" -eq 1 ]; then
    coverage_digest "$idx"
  fi
  echo "── pass ${idx} done: ctest exit ${rc}, sanitizer reports ${san} ──"
}

# #267 acceptance item 4.  The comparison the verdict makes needs a digest whose
# only inputs are coverage facts, so the report is put through
# ci/lcov-coverage-key.awk before it is sorted and hashed.
#
# ⚠️ THIS COMMENT USED TO CLAIM THE DIGEST WAS "stable under everything EXCEPT a
# real change in coverage", NAMING ONLY SECTION ORDER AS THE THING BEING
# NORMALISED.  That claim was FALSE and it shipped: the digest was taken over the
# raw report, which carries per-line and per-function EXECUTION COUNTS, and those
# move run-to-run — so the gate voided its own serial baseline on passes that had
# covered the same code, which reads as a suite defect rather than an instrument
# fault.
#
# What is normalised, the scope limit that buys, and the recipe for re-deriving
# any of it live in ci/lcov-coverage-key.awk.  Not re-stated here: two copies of
# a rule is how one of them goes stale, and the figures that used to sit in this
# paragraph were a third copy of numbers from an artifact no longer in reach.
coverage_digest() {
  local idx="$1"
  local prof="$BUILD/profiles-pass${idx}"
  local pd="$BUILD/coverage-pass${idx}.profdata"
  local info="$BUILD/coverage-pass${idx}.lcov"
  local status="ok"

  # `llvm-profdata merge` on an empty glob would merge nothing and still exit 0
  # on some versions — check the input exists first, or a pass with no profiles
  # produces a valid digest of nothing and three such passes "agree".
  local n; n="$(find "$prof" -name '*.profraw' 2>/dev/null | wc -l)"
  if [ "${n:-0}" -eq 0 ]; then
    printf 'status=no-profiles\nprofraw_count=0\n' > "$OUT/pass${idx}.coverage.env"
    echo "::warning::#267 item 4: pass ${idx} produced no .profraw files, so its merged coverage could not be computed."
    return 0
  fi

  "$LLVM_PROFDATA" merge -sparse "$prof"/*.profraw -o "$pd" 2>/dev/null || status="merge-failed"
  if [ "$status" = "ok" ]; then
    # ⚠️ COPIED FROM tier1.yml's `Generate LCOV report` STEP, deliberately
    # including its `-object` enumeration: that step's own comment records that
    # listing only core+capi silently dropped all dictionary/codegen/wire
    # coverage. A digest computed over a different object set is not comparable
    # with the lane's real report, so this must stay in step with it.
    local -a objects=()
    for b in "$BUILD"/bin/*; do
      if [ ! -f "$b" ] || [ ! -x "$b" ]; then continue; fi
      if [ "$(basename "$b")" = fixpp_core_tests ]; then continue; fi
      objects+=(-object "$b")
    done
    "$LLVM_COV" export --format=lcov --instr-profile="$pd" \
      "$BUILD/bin/fixpp_core_tests" "${objects[@]}" include src > "$info" 2>/dev/null \
      || status="export-failed"
  fi

  if [ "$status" != "ok" ] || [ ! -s "$info" ]; then
    printf 'status=%s\nprofraw_count=%s\n' "${status/ok/empty-report}" "$n" \
      > "$OUT/pass${idx}.coverage.env"
    echo "::warning::#267 item 4: pass ${idx} coverage digest unavailable (${status})."
    return 0
  fi

  # ⚠️ THE WHOLE DIGEST IS ONE CALL, DELIBERATELY — see ci/lcov-coverage-digest.sh
  # for what it computes and why it validates rather than trusts. Assembling it
  # here is what let a hostile review construct a mutation that restored the
  # original raw-report defect with every seam cell still green: the cells could
  # only test the awk, never this function's use of it. There is nothing left
  # here to diverge from what the cells exercise.
  #
  # It exits non-zero rather than emitting a digest it cannot stand behind, and
  # that status is CHECKED: this script runs under `set -uo pipefail` and not
  # `set -e`, so an unchecked failure would fall through to a `status=ok` sample.
  local cov
  if ! cov="$("$HERE/lcov-coverage-digest.sh" "$info")"; then
    printf 'status=digest-failed\nprofraw_count=%s\n' "$n" \
      > "$OUT/pass${idx}.coverage.env"
    echo "::warning::#267 item 4: pass ${idx} produced a report but no usable coverage digest."
    cp "$info" "$OUT/pass${idx}.lcov"
    return 0
  fi
  {
    printf 'status=ok\nprofraw_count=%s\n' "$n"
    printf '%s\n' "$cov"
  } > "$OUT/pass${idx}.coverage.env"
  cp "$info" "$OUT/pass${idx}.lcov"
}

witness 0 before-pass-1
run_pass 1 "A"  1
witness 1 after-pass-1
run_pass 2 "B"  "$JOBS"
witness 2 after-pass-2
run_pass 3 "A'" 1
witness 3 after-pass-3

echo "A-B-A sample for ${PRESET} written to ${OUT}"
ls -la "$OUT"
exit 0
