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
# two SEPARATE CI jobs on lanes whose between-VM wall-clock spread is 27-43 %.
# `linux-clang-debug` alone reads 1142 / 1135 / 1151 s serial on three VMs and
# 583 s on a fourth; every "no gain" reading for that lane came from comparing
# against that one fast VM.
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
#    MEASURED on an 8x1 s synthetic suite: `ctest --preset P --parallel 1`
#    overrides a preset's `execution: {jobs: 4}` (8.03 s vs 2.01 s), but
#    `CTEST_PARALLEL_LEVEL=1 ctest --preset P` does NOT (2.01 s — the preset
#    wins).  `linux-clang-debug` and `linux-clang-ubsan` already carry `jobs: 4`,
#    so the env-var form would have run all three passes at 4 on exactly the
#    lanes a campaign starts from — and A vs A' would then agree PERFECTLY.
#    ci/parallelism-verdict.py checks the achieved level independently anyway;
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

# Identical to ci/peak-memory-report.sh's SAN_PATTERN.  Kept in step with it
# deliberately: a sample whose sanitizer counting differs from the standing CI
# gate's would produce a "new" report that is only a different definition.
SAN_PATTERN='WARNING: ThreadSanitizer:|ERROR: (Address|Leak|Memory)Sanitizer:|runtime error:'

# Never fatal — a missing witness must degrade the sample toward VOID, not abort
# a campaign mid-lane. But it is ANNOUNCED: a silent `|| true` would leave the
# diagnosis inferable only from the verdict's "0 usable machine witness(es)"
# three suite runs later.
# ⚠️ THE TWO KNOBS BELOW ARE FOR ci/test-parallelism-aba-seam.sh, NOT FOR A
# CAMPAIGN, and they are safe there for a specific reason: the seam check tests
# the PLUMBING — that the driver writes witness files the verdict can read — and
# the calibration's absolute value is never used as an absolute by anything.
# Shrinking it changes nothing the seam asserts, and turns 8 witness calls from
# ~29 s into a rounding error on a job that runs on every push.
#
# ⚠️ Setting them in a real campaign WOULD degrade the observation, so it is not
# left to trust: machine-witness.py records `iters` and `repeats` in every
# report, and ci/parallelism-verdict.py discloses on the page when they are
# below the shipped defaults. A weakened witness cannot pass itself off as a
# full one.
WITNESS_ARGS=()
[ -n "${PARALLELISM_WITNESS_ITERS:-}" ]   && WITNESS_ARGS+=(--iters "$PARALLELISM_WITNESS_ITERS")
[ -n "${PARALLELISM_WITNESS_REPEATS:-}" ] && WITNESS_ARGS+=(--repeats "$PARALLELISM_WITNESS_REPEATS")

witness() {
  "$PY" "$HERE/machine-witness.py" --out "$OUT/witness$1.env" --label "$2" \
    --procs "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
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

# #267 acceptance item 4.  The comparison the verdict makes needs a digest that
# is stable under everything EXCEPT a real change in coverage, so the lcov
# records are SORTED before hashing: `llvm-cov export` emits per-object sections
# whose order follows the object list and the filesystem, neither of which is a
# coverage fact.  Hashing the raw file would report a difference on every run and
# the check would be discarded as noisy — the usual way a real gate dies.
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

  llvm-profdata-22 merge -sparse "$prof"/*.profraw -o "$pd" 2>/dev/null || status="merge-failed"
  if [ "$status" = "ok" ]; then
    local objects=""
    # ⚠️ COPIED FROM tier1.yml's `Generate LCOV report` STEP, deliberately
    # including its `-object` enumeration: that step's own comment records that
    # listing only core+capi silently dropped all dictionary/codegen/wire
    # coverage. A digest computed over a different object set is not comparable
    # with the lane's real report, so this must stay in step with it.
    for b in "$BUILD"/bin/*; do
      if [ ! -f "$b" ] || [ ! -x "$b" ]; then continue; fi
      [ "$(basename "$b")" = fixpp_core_tests ] && continue
      objects="$objects -object $b"
    done
    # shellcheck disable=SC2086
    llvm-cov-22 export --format=lcov --instr-profile="$pd" \
      "$BUILD/bin/fixpp_core_tests" $objects include src > "$info" 2>/dev/null \
      || status="export-failed"
  fi

  if [ "$status" != "ok" ] || [ ! -s "$info" ]; then
    printf 'status=%s\nprofraw_count=%s\n' "${status/ok/empty-report}" "$n" \
      > "$OUT/pass${idx}.coverage.env"
    echo "::warning::#267 item 4: pass ${idx} coverage digest unavailable (${status})."
    return 0
  fi

  local sha; sha="$(LC_ALL=C sort "$info" | sha256sum | cut -d' ' -f1)"
  {
    printf 'status=ok\nprofraw_count=%s\nsorted_info_sha256=%s\n' "$n" "$sha"
    printf 'lines_covered=%s\n' "$(grep -c '^DA:[0-9]*,[1-9]' "$info" 2>/dev/null || echo 0)"
    printf 'lines_total=%s\n' "$(grep -c '^DA:' "$info" 2>/dev/null || echo 0)"
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
