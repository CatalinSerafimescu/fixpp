#!/usr/bin/env bash
# Regression harness for ci/parallelism-verdict.py (#267 A-B-A campaign).
#
# Run by the `ci-script-pins` job in tier1.yml, and locally with:
#   ci/test-parallelism-verdict.sh
#
# ── WHY THIS FILE EXISTS ─────────────────────────────────────────────────────
#
# The verdict script decides whether an `execution.jobs` change has evidence
# behind it.  This repo's most expensive recurring defect is an instrument that
# runs, exits 0, and measures nothing — and `ci/ctest-parallelism-probe.md`
# records three parallelism conclusions that were published and then WITHDRAWN,
# so this particular question has already been got wrong three times by people
# reading numbers a checker had not been proven able to reject.
#
# So: every gate the verdict applies is exercised here against a SYNTHETIC
# sample built from scratch, and each cell requires the NAMED diagnostic and the
# NAMED exit code.  A cell that merely exits non-zero is not a passing cell —
# a checker that fails for the wrong reason is indistinguishable from one that
# works, right up until it matters.
#
# The synthetic sample is deliberate, not a shortcut: the real thing costs three
# suite runs on a CI runner, which is exactly why nobody re-derives it.  Cell T0
# proves the generator produces a sample the checker ACCEPTS, so every later
# cell's failure is attributable to the one thing that cell changed.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="$HERE/parallelism-verdict.py"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }

# ── The sample generator ─────────────────────────────────────────────────────
#
# Emits a complete run directory: three passes (meta / peak.env / ctest.log)
# plus four machine witnesses.  Every knob a cell needs to break is a flag, so
# a cell is one line and the thing it changed is visible in that line.
cat > "$WORK/gen.py" <<'GEN'
import argparse, hashlib, os, pathlib, sys

ap = argparse.ArgumentParser()
ap.add_argument("dir")
ap.add_argument("--ran", default="361,361,361")
ap.add_argument("--wall", default="1140.0,540.0,1135.0")
ap.add_argument("--jobs", default="1,4,1")
ap.add_argument("--inflight", default="1,4,1")
ap.add_argument("--sum", default="1130.0,1400.0,1125.0")
ap.add_argument("--san", default="0,0,0")
ap.add_argument("--peak", default="700000000,930000000,700000000")
ap.add_argument("--peak-status", default="ok,ok,ok")
ap.add_argument("--cov", default="")              # 3 tags; empty = no coverage files
ap.add_argument("--steal", default="100,100,100,100")
ap.add_argument("--calib", default="2.70,2.72,2.74,2.76")
ap.add_argument("--witnesses", type=int, default=4)
ap.add_argument("--witness-status", default="ok")
ap.add_argument("--status", default="0,0,0")
ap.add_argument("--preset", default="linux-clang-demo")
ap.add_argument("--witness-iters", default="3000000")
ap.add_argument("--witness-repeats", default="5")
ap.add_argument("--subset", default="")
ap.add_argument("--drop-pass", type=int, default=0)
ap.add_argument("--no-start", type=int, default=0)   # pass index whose Start lines vanish
ap.add_argument("--dup-start", type=int, default=0)  # pass index that re-Starts a live test
a = ap.parse_args()

d = pathlib.Path(a.dir); d.mkdir(parents=True, exist_ok=True)
LABELS = ["A", "B", "A'"]
split = lambda s: s.split(",")

for i in range(1, 4):
    if i == a.drop_pass:
        continue
    k = i - 1
    ran = int(split(a.ran)[k]); wall = float(split(a.wall)[k])
    jobs = int(split(a.jobs)[k]); infl = int(split(a.inflight)[k])
    total = float(split(a.sum)[k]); per = total / ran

    lines = ["Test project /build/preset"]
    n = 1
    done = 0
    while n <= ran:
        batch = list(range(n, min(n + infl, ran + 1)))
        for t in batch:
            lines.append(f"    Start {t}: test_{t}")
        if i == a.dup_start and n == 1:
            # A second Start for a test already in flight: a log the in-flight
            # oracle cannot have understood.
            lines.append(f"    Start {batch[0]}: test_{batch[0]}")
        for t in batch:
            done += 1
            lines.append(f"{done:5d}/{ran} Test #{t}: test_{t} "
                         f"{'.' * 20}   Passed {per:8.2f} sec")
        n += len(batch)
    if i == a.no_start:
        lines = [ln for ln in lines if not ln.lstrip().startswith("Start ")]
    lines += ["", f"100% tests passed, 0 tests failed out of {ran}", "",
              f"Total Test time (real) = {wall:8.2f} sec"]
    (d / f"pass{i}.ctest.log").write_text("\n".join(lines) + "\n")

    (d / f"pass{i}.meta").write_text(
        f"preset={a.preset}\nlabel={LABELS[k]}\njobs={jobs}\norder={i}\n"
        f"san_count={split(a.san)[k]}\nsubset={a.subset}\n"
        f"ctest_status={split(a.status)[k]}\n")
    (d / f"pass{i}.peak.env").write_text(
        f"label=linux-clang-demo\nstatus={split(a.peak_status)[k]}\n"
        f"peak_bytes={split(a.peak)[k]}\npeak_max_single_bytes=400000000\n"
        f"mem_total_bytes=16766894080\npeak_procs=6\ncmd_status=0\n")

    if a.cov:
        tag = split(a.cov)[k]
        sha = hashlib.sha256(tag.encode()).hexdigest()
        (d / f"pass{i}.coverage.env").write_text(
            f"sorted_info_sha256={sha}\nlines_covered=1000\nlines_total=1200\n")

for w in range(a.witnesses):
    (d / f"witness{w}.env").write_text(
        f"label=w{w}\nprocs=4\niters={a.witness_iters}\nrepeats={a.witness_repeats}\n"
        f"calib_1proc_s={split(a.calib)[w]}\n"
        f"calib_nproc_s={float(split(a.calib)[w]) * 1.9:.3f}\n"
        f"steal_ticks={'' if a.witness_status == 'no-procfs' else split(a.steal)[w]}\n"
        f"status={a.witness_status}\nmono_s=100.0\n")
GEN

# $1 = cell name, $2 = expected exit, $3 = expected fragment, rest = gen flags
cell() {
  local name="$1" want="$2" frag="$3"; shift 3
  rm -rf "$WORK/run"
  if ! python3 "$WORK/gen.py" "$WORK/run" "$@" 2>&1; then
    bad "$name — the GENERATOR failed, so this cell tested nothing"; return
  fi
  local out rc=0
  out="$(python3 "$CHECK" "$WORK/run" 2>&1)" || rc=$?
  if [ "$rc" -ne "$want" ]; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$name — expected exit $want, got $rc"; return
  fi
  if ! printf '%s\n' "$out" | grep -qF -- "$frag"; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$name — exited $rc but WITHOUT '$frag' (it failed for the wrong reason)"; return
  fi
  ok "$name"
}

echo "== parallelism verdict =="

# ── T0: THE GOLDEN SAMPLE IS ACCEPTED ────────────────────────────────────────
#
# Every later cell attributes its failure to the one knob it turned, which is
# only sound if the untouched sample passes. A harness whose baseline is already
# red proves nothing about what its mutants demonstrate.
cell "T0 a clean A-B-A sample is VALID" 0 "✅ VALID — this sample is evidence"

# ── T0b: the accepted sample actually reports the speedup ────────────────────
cell "T0b a VALID sample publishes its speedup" 0 "2.11x"

# ── T1: THE CHECK THE WHOLE DESIGN EXISTS FOR ────────────────────────────────
#
# Serial passes 1140 s and 1400 s = 20.6 % apart. Without this the middle pass
# is compared against a floor that moved under it — the exact error behind all
# three withdrawn conclusions in ci/ctest-parallelism-probe.md.
cell "T1 serial passes that disagree VOID the sample" 3 "THE TWO SERIAL PASSES DISAGREE" \
  --wall 1140.0,540.0,1400.0

# ── T2: acceptance item 2 — a silent test drop reads as a pure win ───────────
cell "T2 differing test counts are a DEFECT" 1 "DID NOT RUN THE SAME TESTS" \
  --ran 361,349,361

# ── T3: acceptance item 5 — real until disproven ─────────────────────────────
cell "T3 a sanitizer report appearing at higher concurrency is a DEFECT" 1 \
  "A SANITIZER REPORT APPEARED AT HIGHER CONCURRENCY" --san 0,1,0

# ── T4: THE SPURIOUS-HIT GUARD ───────────────────────────────────────────────
#
# The one failure A-vs-A' cannot catch: if `--parallel N` does not take effect,
# all three passes are identical, the agreement check passes PERFECTLY, and the
# apparatus reports "no gain available on this lane" having tested one
# configuration three times. Measured and real: `CTEST_PARALLEL_LEVEL` does NOT
# override a preset's `execution.jobs`, and two lanes already carry jobs=4.
cell "T4 a parallel pass that never ran parallel is an INSTRUMENT FAILURE" 2 \
  "The widening did NOT take effect" --inflight 1,1,1 --wall 1140.0,1140.0,1135.0

# ── T5: the mirror image — a serial baseline that was not serial ─────────────
cell "T5 a serial pass that ran parallel is an INSTRUMENT FAILURE" 2 \
  "The serial baseline is not serial" --inflight 4,4,1

# ── T6: a missing arm is not a weaker sample, it is not a sample ─────────────
cell "T6 a missing pass is an INSTRUMENT FAILURE" 2 "2 of 3 passes" --drop-pass 3

# ── T7: THE EMPTY RUN ────────────────────────────────────────────────────────
#
# If the workflow's artifact paths ever drift, "no findings over no passes" must
# not read as a clean sample.
rm -rf "$WORK/run"; mkdir -p "$WORK/run"
out="$(python3 "$CHECK" "$WORK/run" 2>&1)"; rc=$?
if [ "$rc" -eq 2 ] && printf '%s\n' "$out" | grep -qF "0 of 3 passes"; then
  ok "T7 an empty run directory is an instrument failure, not a pass"
else
  printf '%s\n' "$out" | sed 's/^/  | /'
  bad "T7 an empty run directory — expected exit 2 with '0 of 3 passes', got $rc"
fi

# ── T8: an absent witness degrades toward VOID, never certifies ──────────────
cell "T8 no machine witnesses VOIDs the sample" 3 "no independent observation" \
  --witnesses 1

# ── T9/T10: what A-vs-A' agreement CANNOT see ────────────────────────────────
#
# A VM that was uniformly degraded throughout has two serial passes that agree
# beautifully with each other and with nothing in production. Only an
# independent observation of the machine catches it.
cell "T9 a rise in /proc/stat steal VOIDs the sample" 3 "STEAL ROSE" \
  --steal 100,100,140,140
# 2.70 -> 4.00 s is 38.7 %, well clear of the 16.0 % noise floor measured on a
# contended host — a threshold set inside that floor would void honest samples.
cell "T10 calibration drift VOIDs the sample" 3 "THE MACHINE'S OWN SPEED DRIFTED" \
  --calib 2.70,3.00,3.50,4.00

# ── T11/T12: acceptance item 4, and the attribution that makes it mean something
cell "T11 coverage differing only under parallelism is a DEFECT" 1 \
  "MERGED COVERAGE CHANGED UNDER PARALLELISM" --cov same,other,same
# Both serial passes disagreeing is the SUITE being nondeterministic, measured
# at unchanged concurrency — a finding of its own, and NOT attributable to
# --parallel. Reporting it as a parallelism defect would be a false accusation.
cell "T12 coverage differing between the two SERIAL passes is not blamed on parallelism" 0 \
  "run-to-run coverage nondeterminism" --cov one,two,three

# ── T13: the in-flight oracle's own blind spot, made loud ────────────────────
cell "T13 a log with no Start lines is an INSTRUMENT FAILURE" 2 \
  "could not be observed at all" --no-start 2
cell "T14 a malformed Start/complete pairing is an INSTRUMENT FAILURE" 2 \
  "did not understand this log" --dup-start 2

# ── T15: PRECEDENCE — a defect outranks a void ───────────────────────────────
#
# A sanitizer report that appeared at j=N is real until disproven whether or not
# the timing half of the sample survived. Voiding it first would file the
# finding under "noisy VM" and lose it.
cell "T15 a defect outranks a void" 1 "A SANITIZER REPORT APPEARED" \
  --san 0,1,0 --wall 1140.0,540.0,1400.0
# ...and an instrument failure outranks both, because a run that measured
# nothing cannot support a finding either.
cell "T16 an instrument failure outranks a defect" 2 "The widening did NOT take effect" \
  --san 0,1,0 --inflight 1,1,1

# ── T17: a subset run must not be readable as lane evidence ──────────────────
cell "T17 a subset run is stamped SMOKE — NOT EVIDENCE" 0 "SMOKE RUN — NOT EVIDENCE" \
  --subset "session_.*"

# ── T18: a missing peak reading is disclosed, not silently omitted ───────────
cell "T18 an unmeasured peak RSS is disclosed" 0 "peak RSS NOT MEASURED" \
  --peak-status ok,no-samples,ok

# ── T26/T27: THE WINDOWS SUMMARY MUST NOT INVENT A MEMORY FIGURE ─────────────
#
# `peak_bytes` is EMPTY on the Windows lanes — there is no /proc to sample — and
# `int("" or 0)` is 0. A formatted clause would print
# `peak RSS +0.0 % (0.00 GiB -> 0.00 GiB)` beside a real speedup on a VALID
# sample: a fabricated zero next to genuine numbers, read as measured headroom,
# which is the exact figure acceptance item 1 exists to require. T27 is the one
# that matters — it asserts the fabricated string is ABSENT, which no assertion
# about the correct string can do.
cell "T26 a sample with no peak reading says NOT MEASURED in its summary" 0 \
  "peak RSS **NOT MEASURED**" --peak-status no-procfs-platform,no-procfs-platform,no-procfs-platform \
  --peak ,,
rm -rf "$WORK/run"
python3 "$WORK/gen.py" "$WORK/run" --peak-status no-procfs-platform,no-procfs-platform,no-procfs-platform --peak ,,
out="$(python3 "$CHECK" "$WORK/run" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ] && ! printf '%s' "$out" | grep -qF "0.00 GiB"; then
  ok "T27 a sample with no peak reading prints NO fabricated 0.00 GiB"
else
  printf '%s\n' "$out" | sed 's/^/  | /'
  bad "T27 a fabricated 0.00 GiB reached the summary (exit $rc)"
fi

# ── T19: THE COST AXIS IS NOT DECORATION ─────────────────────────────────────
#
# linux-clang-tsan's failure mode was wall clock barely moving while AGGREGATE
# runner time rose 44 %. A sample that speeds up while inflating summed time
# must say so on the page, or the decision gets made on the wrong axis.
cell "T19 an inflated summed test time is called out on a VALID sample" 0 \
  "spends more runner-minutes" --sum 1130.0,1700.0,1125.0

# ── T23/T24: is it the LANE's workload, or some other workload? ──────────────
#
# Three passes agreeing with each other says nothing about agreeing with
# PRODUCTION. `linux-clang-asan` is pinned at 362 in ci/expected-eligible-tests
# .txt; a job that configures the tree differently measures a suite that does
# not ship. The disposition is the probe document's own designed one for a basis
# mismatch — DIAGNOSTIC ONLY, i.e. toward "not evidence", never toward a false
# acceptance.
cell "T23 a count that disagrees with the lane's pinned basis VOIDs the sample" 3 \
  "NOT THE LANE'S PRODUCTION WORKLOAD" --preset linux-clang-asan --ran 300,300,300
cell "T24 a count matching the pinned basis is confirmed on the page" 0 \
  "matching \`ci/expected-eligible-tests.txt\`" --preset linux-clang-asan --ran 362,362,362
# A preset with no pin line must NOT void — the Windows lanes have never had one,
# and inventing an expectation for them would void every Windows sample.
cell "T25 a preset with no pinned basis is disclosed, not voided" 0 \
  "could not be checked against the lane's production basis" --preset windows-msvc-asan

# ── T21/T22: a pass that did not complete its suite ──────────────────────────
#
# The bucket depends on WHICH pass failed, and that distinction is the point: a
# failure confined to the parallel pass is a finding ABOUT parallelism; a failure
# anywhere else means the lane is red for its own reasons and nothing here is a
# parallelism measurement.
cell "T21 tests failing ONLY under --parallel is a DEFECT" 1 \
  "TESTS FAIL UNDER \`--parallel 4\` BUT PASS SERIALLY" --status 0,8,0
cell "T22 tests failing in a SERIAL pass is an INSTRUMENT FAILURE" 2 \
  "Fix the lane, then measure it" --status 0,0,8

# ── T28: A WEAKENED WITNESS MUST NOT PASS AS A FULL ONE ──────────────────────
#
# ci/run-parallelism-aba.sh honours PARALLELISM_WITNESS_{ITERS,REPEATS} so the
# seam self-test can run 8 witness calls cheaply — sound there, because that file
# checks the plumbing and the calibration's magnitude is irrelevant to it. Set on
# a campaign the same knobs would quietly make the drift check less sensitive
# than the page claims. So it is disclosed rather than trusted.
cell "T28 a witness run below the shipped defaults is disclosed" 0 \
  "machine witness ran WEAKENED" --witness-iters 200000 --witness-repeats 2

# ── T20: THE WINDOWS SHAPE ───────────────────────────────────────────────────
#
# `windows-msvc-asan` is the matrix critical path, has no measurement of any
# kind, and is the lane a campaign most needs to decide. There is no /proc
# there, so the witness reports `no-procfs` and carries no steal counter. If
# that were treated as an absent witness, every Windows sample would VOID for
# want of a Linux file — the apparatus would refuse to measure the one lane it
# was most wanted for.
cell "T20 a witness with no /proc is usable, not absent" 0 \
  "steal counter unavailable on this platform" --witness-status no-procfs

# ── The harness's own execution count ────────────────────────────────────────
#
# A sweep must assert how many cells ran: a `cell` invocation lost to an editing
# slip removes a gate silently, and the tally below would still read "N passed,
# 0 failed" for a smaller N.
MUTANTS_DECLARED=30
TOTAL=$((PASS + FAIL))
echo
if [ "$TOTAL" -ne "$MUTANTS_DECLARED" ]; then
  echo "parallelism-verdict harness: EXECUTION COUNT MISMATCH — ran ${TOTAL} cells, declared ${MUTANTS_DECLARED}."
  echo "A cell was added or lost without updating MUTANTS_DECLARED. Refusing to report a result."
  exit 1
fi
echo "parallelism-verdict harness: ${PASS} passed, ${FAIL} failed (${TOTAL} cells)"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
