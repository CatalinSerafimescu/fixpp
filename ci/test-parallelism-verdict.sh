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
import argparse, hashlib, pathlib

ap = argparse.ArgumentParser()
ap.add_argument("dir")
ap.add_argument("--ran", default="361,361,361")
ap.add_argument("--wall", default="1140.0,540.0,1135.0")
ap.add_argument("--inflight", default="1,4,1")
ap.add_argument("--sum", default="1130.0,1400.0,1125.0")
ap.add_argument("--san", default="0,0,0")
ap.add_argument("--peak", default="700000000,930000000,700000000")
ap.add_argument("--peak-status", default="ok,ok,ok")
ap.add_argument("--cov", default="")              # 3 tags; empty = no coverage files
ap.add_argument("--cov-status", default="")       # write coverage.env with NO digest
ap.add_argument("--cov-profraw", default="")      # 3 per-pass .profraw counts
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
ap.add_argument("--dup-summary", type=int, default=0)   # pass index that prints TWO summaries
ap.add_argument("--dup-real", type=int, default=0)      # pass index that prints TWO total-time lines
ap.add_argument("--order-drift", type=int, default=0)   # pass index recording the wrong order
ap.add_argument("--truncate-done", type=int, default=0) # pass index keeping only 1 completion
ap.add_argument("--forge-inflight", type=int, default=0)# pass index injecting bare Start lines
ap.add_argument("--labels", default="")           # override the A,B,A' labels
ap.add_argument("--jobs-shape", default="")       # override the 1,N,1 requested levels
ap.add_argument("--preset-drift", type=int, default=0)  # pass index recording another preset
ap.add_argument("--witness-undeclared", action="store_true")  # omit iters/repeats
ap.add_argument("--procfs-gap", type=int, default=-1)   # witness index with no steal counter
a = ap.parse_args()

d = pathlib.Path(a.dir); d.mkdir(parents=True, exist_ok=True)
split = lambda s: s.split(",")
LABELS = split(a.labels) if a.labels else ["A", "B", "A'"]
# The A-B-A shape itself: cells that need requested-vs-achieved to disagree
# vary --inflight, never this.
JOBS = [int(x) for x in split(a.jobs_shape)] if a.jobs_shape else [1, 4, 1]

for i in range(1, 4):
    if i == a.drop_pass:
        continue
    k = i - 1
    ran = int(split(a.ran)[k]); wall = float(split(a.wall)[k])
    jobs = JOBS[k]; infl = int(split(a.inflight)[k])
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
    if i == a.truncate_done:
        # A log that CLAIMS `ran` tests while carrying one completion record —
        # the shape a truncated upload takes, and the shape a forger would use
        # to make a pass look fast.
        seen = 0
        kept = []
        for ln in lines:
            if " Test #" in ln:
                seen += 1
                if seen > 1:
                    continue
            kept.append(ln)
        lines = kept
    if i == a.forge_inflight:
        # Bare `Start` lines with no matching completion: ordinary test output
        # under --output-on-failure, mimicking ctest's own format to inflate the
        # apparent parallel level.
        lines = lines[:1] + [f"    Start {ran + j}: forged_{j}" for j in range(1, 5)] + lines[1:]
    lines += ["", f"100% tests passed, 0 tests failed out of {ran}", "",
              f"Total Test time (real) = {wall:8.2f} sec"]
    if i == a.dup_summary:
        lines += [f"100% tests passed, 0 tests failed out of {ran}"]
    if i == a.dup_real:
        lines += [f"Total Test time (real) = {wall * 2:8.2f} sec"]
    (d / f"pass{i}.ctest.log").write_text("\n".join(lines) + "\n")

    preset = a.preset + ("-other" if i == a.preset_drift else "")
    order = (i + 1) if i == a.order_drift else i
    (d / f"pass{i}.meta").write_text(
        f"preset={preset}\nlabel={LABELS[k]}\njobs={jobs}\norder={order}\n"
        f"san_count={split(a.san)[k]}\nsubset={a.subset}\n"
        f"ctest_status={split(a.status)[k]}\n")
    (d / f"pass{i}.peak.env").write_text(
        f"label=linux-clang-demo\nstatus={split(a.peak_status)[k]}\n"
        f"peak_bytes={split(a.peak)[k]}\npeak_max_single_bytes=400000000\n"
        f"mem_total_bytes=16766894080\npeak_procs=6\ncmd_status=0\n")

    if a.cov_status:
        # Coverage attempted, no digest produced — the shape a real coverage
        # lane takes when its profiles fail to appear.
        (d / f"pass{i}.coverage.env").write_text(
            f"status={a.cov_status}\nprofraw_count=0\n")
    elif a.cov:
        tag = split(a.cov)[k]
        sha = hashlib.sha256(tag.encode()).hexdigest()
        nraw = split(a.cov_profraw)[k] if a.cov_profraw else "42"
        (d / f"pass{i}.coverage.env").write_text(
            f"status=ok\nprofraw_count={nraw}\nsorted_info_sha256={sha}\n"
            f"lines_covered=1000\nlines_total=1200\n")

for w in range(a.witnesses):
    one = split(a.calib)[w]
    many = f"{float(one) * 1.9:.3f}" if one else ""
    declared = ("" if a.witness_undeclared
                else f"iters={a.witness_iters}\nrepeats={a.witness_repeats}\n")
    gap = (w == a.procfs_gap)
    (d / f"witness{w}.env").write_text(
        f"label=w{w}\nprocs=4\n{declared}"
        f"calib_1proc_s={one}\n"
        f"calib_nproc_s={many}\n"
        f"steal_ticks={'' if (gap or a.witness_status == 'no-procfs') else split(a.steal)[w]}\n"
        f"status={'no-procfs' if gap else a.witness_status}\nmono_s=100.0\n")
GEN

# $1 = cell name, $2 = expected exit, $3 = expected fragment, rest = gen flags.
# Anything after a literal `--` is passed to the CHECKER instead of the generator,
# so a cell can exercise a tolerance flag without a second harness.
cell() {
  local name="$1" want="$2" frag="$3"; shift 3
  local -a gen=() chk=()
  local seen=0
  for arg in "$@"; do
    if [ "$seen" = 0 ] && [ "$arg" = "--" ]; then seen=1; continue; fi
    if [ "$seen" = 0 ]; then gen+=("$arg"); else chk+=("$arg"); fi
  done
  rm -rf "$WORK/run"
  if ! python3 "$WORK/gen.py" "$WORK/run" ${gen+"${gen[@]}"} 2>&1; then
    bad "$name — the GENERATOR failed, so this cell tested nothing"; return
  fi
  local out rc=0
  out="$(python3 "$CHECK" "$WORK/run" ${chk+"${chk[@]}"} 2>&1)" || rc=$?
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
# ⚠️ FOUR, not "at least two". A hostile review certified VALID with only
# witness0 and witness1 — they bracket pass 1 and say nothing about the parallel
# pass, which is the leg the experiment turns on.
cell "T8 fewer than four witnesses VOIDs the sample" 3 "of 4 machine witnesses" \
  --witnesses 1
cell "T8b two witnesses bracket only the FIRST pass and do not certify" 3 \
  "of 4 machine witnesses" --witnesses 2
cell "T8c witnesses with no usable measurement do not count as witnesses" 3 \
  "of 4 machine witnesses" --calib ,,,

# ── T33-T35: pass identity is POSITIONAL, and the shape is asserted ──────────
#
# Counts, sanitizer totals and coverage were keyed by each pass's own `label`
# field. A hostile review used that twice: duplicate labels overwrote earlier
# passes so counts of 362/1/999 read VALID, and a B-A-A ordering read VALID
# because "two serial and one parallel" was accepted in any order — while the
# whole design is serial, parallel, serial. A file describing itself is not
# identification.
cell "T33 a mislabelled pass is refused" 2 "is labelled" --labels "A,A,A'"
cell "T34 a B-A-A ordering is refused" 2 "not the A-B-A shape" --jobs-shape "4,1,1"
cell "T35 passes that disagree on the preset did not measure the same thing" 2 \
  "did not measure the same thing" --preset-drift 1

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
# Not blamed on parallelism — but not evidence either: with a moving serial
# baseline there is nothing to compare the parallel pass against.
cell "T12 coverage differing between the two SERIAL passes VOIDs, and is not blamed on parallelism" 3 \
  "DIFFERENT MERGED COVERAGE" --cov one,two,three

# ── T55/T56/T57: did the PARALLEL pass collect every profile? ────────────────
#
# `%p` names a .profraw per PROCESS. The failure it does not cover is two test
# processes sharing a name under `ctest --parallel`, which drops one test's
# profile. That loss is invisible to every other check: lines_total is
# unchanged, status stays ok, profraw_count stays positive, and the coverage it
# removes reads as the same run-to-run digest wobble T12 already voids on — so
# a real clobber would be filed as "the suite is nondeterministic" and never
# looked at again.
#
# ⚠️ THE COUNT COMPARISON IS THE ONLY ONE OF THESE THAT SURVIVES T12'S VOID.
# In campaign run 33977674899 all three passes reported 367 profraw with three
# DIFFERENT digests — so this answers "did j=4 lose anything" on precisely the
# sample where item 4 itself could not be discharged.
cell "T55 a SHORTFALL of profraw in the parallel pass is a DEFECT" 1 \
  "PROFILES WERE LOST UNDER PARALLELISM" --cov same,same,same --cov-profraw 367,340,367
# Equal counts must NOT accuse, and must say so positively — a guard that only
# ever speaks when it fires leaves "silent" meaning both "fine" and "never ran".
cell "T56 equal profraw counts report collection as complete, not silence" 0 \
  "Every pass collected 367" --cov same,same,same --cov-profraw 367,367,367
# Only a parallel SHORTFALL is profile loss. A HIGHER parallel count is
# disclosed, not accused: more processes is not the failure mode, and a gate
# firing in both directions on a merely-expected-stable number voids honest
# samples.
cell "T57 a HIGHER parallel profraw count is disclosed, not called a defect" 0 \
  "profraw counts differ across passes" --cov same,same,same --cov-profraw 367,371,367

# ── T29: COVERAGE ATTEMPTED BUT NOT COMPARED IS NOT "FINE" ───────────────────
#
# Found by RUNNING the driver with --coverage against a project that produces no
# profiles, not by reading the code: the verdict printed VALID with a real
# speedup and said nothing at all about item 4 — the one criterion the coverage
# lane is blocked on. A measurement that could not be taken reading as a
# measurement that came out fine is this repo's #1 recurring defect, and it had
# reappeared here inside the apparatus written to prevent it.
cell "T29 coverage attempted but not compared VOIDs, it is not silent" 3 \
  "ITEM 4 WAS NOT DISCHARGED" --cov-status no-profiles

# ── T13: the in-flight oracle's own blind spot, made loud ────────────────────
cell "T13 a log with no Start lines is an INSTRUMENT FAILURE" 2 \
  "could not be observed at all" --no-start 2
cell "T14 a malformed Start/complete pairing is an INSTRUMENT FAILURE" 2 \
  "not structurally well-formed" --dup-start 2

# ── T30-T32: A FORGED OR TRUNCATED LOG ───────────────────────────────────────
#
# `--output-on-failure` puts arbitrary TEST OUTPUT in this log, so a line that
# LOOKS like ctest's is not evidence that ctest wrote it. A hostile review built
# all three of these and the permissive parser summarised every one — including
# a log claiming 362 tests while carrying a single completion record, which read
# VALID. The achieved parallel level is the one thing standing between this
# apparatus and measuring one configuration three times, so the parser is
# fail-closed: exactly one summary, exactly `ran` starts and completions with
# matching ids, nothing left in flight.
cell "T30 a log with TWO ctest summaries is refused" 2 \
  "exactly 1 is expected" --dup-summary 2
cell "T31 a log claiming N tests with one completion record is refused" 2 \
  "completion record(s) against a reported" --truncate-done 2
cell "T32 injected Start lines cannot forge the parallel level upward" 2 \
  "started and never completed" --forge-inflight 1

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
# ⚠️ EXIT 3, NOT 0. The banner alone used to be the whole consequence, so a
# smoke run went GREEN and published a speedup for a sample this file expressly
# declares unusable — and a green tick outlives a banner.
cell "T17 a subset run VOIDS, it does not merely carry a banner" 3 "SUBSET RUN" \
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

# ── T36-T44: the second adversarial round ────────────────────────────────────
#
# An independent Opus review, run in parallel with the Codex one from a session
# that could not see its work, returned a DISJOINT set. Each cell below is a
# state that read VALID, or a conclusion published from a measurement that was
# never made.

# ⚠️ ITEM 5 COULD BE SWITCHED OFF BY ONE WORD. The driver writes the literal
# `unreadable` when a pass's LastTest.log cannot be read; the verdict skipped
# past it, which dropped that pass from the dict and removed the ENTIRE
# comparison — five sanitizer reports at --parallel 4 against zero serially read
# as VALID. Item 4's identical shape had already been fixed; this sibling had
# not.
cell "T36 an unparsable sanitizer count is an INSTRUMENT FAILURE, not a skip" 2 \
  "ITEM 5 WAS NOT DISCHARGED" --san 0,unreadable,0
cell "T36b ...and it does not hide a real report behind it" 2 \
  "ITEM 5 WAS NOT DISCHARGED" --san unreadable,5,0

# ⚠️ "A LITTLE PARALLELISM HAPPENED" SATISFIED THE GUARD. `got <= 1` is a
# total-failure test; a pass that requested 4 and never exceeded 2 in flight
# passed everything, and the headline divided the speedup by the REQUESTED
# level — publishing a parallel efficiency wrong by the ratio requested/achieved,
# which is the number an execution.jobs decision is argued from.
cell "T37 a PARTIAL widening is refused, not averaged into the headline" 2 \
  "only PARTLY took effect" --inflight 1,2,1 --wall 1140.0,900.0,1135.0

# ⚠️ THE A-B-A SHAPE ITSELF WAS UNTESTED. Found by neutering each gate in turn
# and re-running this harness: 18 of 20 reddened a named cell; this one did not.
# `inputs.jobs` is a free-text dispatch box, so `1,1,1` is a typo away.
cell "T38 three serial passes are not an A-B-A" 2 "not the A-B-A design" \
  --jobs-shape 1,1,1 --inflight 1,1,1
cell "T39 three parallel passes are not an A-B-A" 2 "not the A-B-A design" \
  --jobs-shape 4,4,4 --inflight 4,4,4
cell "T40 one serial pass is not an A-B-A" 2 "not the A-B-A design" \
  --jobs-shape 1,4,4 --inflight 1,4,4

# ⚠️ A WIDENED TOLERANCE TURNED THE CENTRAL GATE OFF WITH NO BANNER. A 2.35x was
# published over serial passes 20.5 % apart simply by passing --tolerance-pct
# 100. Same class of knob as PARALLELISM_WITNESS_*, which does get a banner.
cell "T41 a widened A-vs-A' tolerance is disclosed" 0 "tolerance was WIDENED" \
  --wall 1140.0,540.0,1400.0 -- --tolerance-pct 100

# ⚠️ A REPORT THAT DOES NOT SAY HOW IT WAS TAKEN CANNOT BE JUDGED. `.get(key,
# DEFAULT)` assumed the best: stripping iters/repeats removed the WEAKENED
# banner from a genuinely weak witness. Reachable from any archived artifact
# written by an older version — and archived artifacts are what operators
# re-judge.
cell "T42 a witness that does not record how it was taken is disclosed" 0 \
  "does not record how it was taken" --witness-undeclared

# ⚠️ A FALSE ATTRIBUTION IN THE ONE PLACE THE CODE EXISTS TO GET RIGHT. A
# `no-procfs` witness stays usable but carries no counter, so filtering it out of
# a positionally labelled list shifted every later interval: a rise during pass 3,
# a SERIAL pass, was reported as the parallel one and annotated with the
# run_paired bypass diagnosis.
cell "T43 an unbracketed steal interval is declared" 3 \
  "could not be attributed to every pass" --procfs-gap 1 --steal 100,100,100,140
# ⚠️ THE ATTRIBUTION ITSELF, both directions. Declaring the gap is only half of
# it: the defect was that the rise got attached to the WRONG pass, and a cell
# that only checks the disclosure would still pass with the misattribution
# live. T43b requires the serial pass to be named; T43c requires the parallel
# one NOT to be, which is the assertion the positive form cannot make.
cell "T43b a rise in a SERIAL pass names that pass" 3 \
  "STEAL ROSE DURING pass 3 (serial)" --procfs-gap 1 --steal 100,100,100,140
rm -rf "$WORK/run"
python3 "$WORK/gen.py" "$WORK/run" --procfs-gap 1 --steal 100,100,100,140
out="$(python3 "$CHECK" "$WORK/run" 2>&1)"; rc=$?
if [ "$rc" -eq 3 ] && ! printf '%s' "$out" | grep -qF "pass 2 (parallel)"; then
  ok "T43c a serial-pass rise does not name the parallel pass or its bypass"
else
  printf '%s\n' "$out" | sed 's/^/  | /'
  bad "T43c a serial-pass rise still named the parallel pass (exit $rc)"
fi
# ...and the parallel case must still get the bypass sentence it is for.
cell "T43d a rise in the PARALLEL pass still carries the bypass diagnosis" 3 \
  "This rise is in the PARALLEL pass" --steal 100,100,140,140

# ⚠️ 0.00x IS NOT A MEASUREMENT, it is what the ZeroDivisionError guard prints —
# beside a real serial time, on a VALID sample. The memory clause ten lines away
# withholds for exactly this reason.
cell "T44 a zero wall time is refused rather than printed as 0.00x" 2 \
  "wall time of 0.0" --wall 1140.0,0.0,1135.0

# ── T45/T46: found by BROADENING THE SWEEP'S OWN CENSUS ─────────────────────
#
# The sweep first matched only `instrument|defects|voids` appends and reported
# "all 21 gates are covered". The verdict had grown two more sinks it could not
# see — `shape.append(` and the log parser's `a.append(` — so a census narrower
# than its own claim was reporting a clean sweep with a straight face. That is
# the same defect as an uncovered gate, one level out. Widening it to 29 gates
# surfaced these two immediately.
#
# A second `Total Test time (real)` is not the same shape as a second summary
# (T30): a truncated-and-reappended artifact, or two ctest invocations teed into
# one file, produces one and not the other, and `real_s` is the denominator of
# every concurrency figure on the page.
cell "T45 a log with TWO total-time lines is refused" 2 \
  "Total Test time (real)\` line(s); exactly 1 is expected" --dup-real 2
# T33 checks the LABEL; the `order` field is a second, independent claim a pass
# makes about its own position, and identity here is positional.
cell "T46 a pass whose order field disagrees with its position is refused" 2 \
  "records order=" --order-drift 2

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
MUTANTS_DECLARED=57
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
