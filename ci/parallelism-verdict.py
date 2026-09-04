#!/usr/bin/env python3
"""Decide whether one lane's A-B-A parallelism sample is EVIDENCE (#267).

    ci/parallelism-verdict.py <run-dir> [--tolerance-pct 5.0]

WHY THIS EXISTS
===============

`ci/ctest-parallelism-probe.md` records THREE `execution.jobs` conclusions that
were measured, published, and then WITHDRAWN — `debug`'s, `ubsan`'s, and
`tsan`'s "no gain at j=4".  Every one failed the same way: an A/B across two
jobs, on lanes whose between-VM wall-clock spread is **27-43 %**.  Two of them
were later re-measured properly and came back POSITIVE (2.10x and 1.79x), so
this is not a story about parallelism being useless — it is a story about a
measurement design.  On that
spread a single unpaired pair of runs cannot distinguish a 1.8x speedup from VM
luck, and the repo has the receipts:

  * `linux-clang-debug` serial reads 1142 / 1135 / 1151 s on three VMs and
    **583 s** on a fourth.  Every "no gain" reading for that lane came from
    comparing against that one fast VM.
  * `linux-clang-tsan`'s j=2-vs-j=4 comparison spans a **43.3 %** spread; its
    "measured no gain at j=4" wording is withdrawn in the probe document.

The design that survived is the one `linux-clang-debug` was finally settled
with: three passes in ONE job on ONE VM, serial-parallel-serial, **voided unless
the two serial passes agree** (they agreed to 0.6 %).  This file is that
design's judgement, made mechanical — because the same document shows the
judgement is exactly the part that gets skipped when a number looks convincing.

WHAT IT REFUSES TO DO
=====================

It will not print a speedup for a sample it cannot stand behind.  A speedup is a
CONCLUSION; the per-pass wall times are FACTS.  On a void sample the facts are
printed and the conclusion is withheld, because a number on the page is read as
a result no matter what caveat sits beside it — which is how three withdrawn
claims got published in the first place.

EXIT STATUS — FOUR MEANINGS, AND `0` IS THE NARROWEST
=====================================================

    0  VALID   — the sample is evidence.  (It may say the widening is not worth
                 it; that is a valid result, not a failure.)
    1  DEFECT  — a real defect surfaced: the passes did not run the same tests,
                 a sanitizer report appeared at higher concurrency, or merged
                 coverage changed under parallelism.  #267 acceptance items 2,
                 4 and 5; all three are "real until disproven" in this repo.
    2  INSTRUMENT FAILURE — the apparatus did not measure what it claims to.
                 Includes the one failure that would otherwise be invisible:
                 the requested `--parallel` level not materialising, which makes
                 every pass identical and the agreement check pass PERFECTLY.
    3  VOID    — measured cleanly, but the machine moved under the experiment,
                 so the sample is not evidence.  Re-dispatch.

⚠️ THE ORDER MATTERS AND IS DELIBERATE: instrument failure, then defect, then
void, then valid.  A defect outranks a void — a sanitizer report that appeared
at j=N is a real defect until disproven whether or not the timing half of the
sample survived.  Voiding it would file the finding under "noisy VM".
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys

# ── ctest log parsers ────────────────────────────────────────────────────────
#
# ⚠️ THE FIRST THREE ARE COPIED VERBATIM FROM ci/peak-memory-report.sh (the
# RAN / CTEST_REAL / SUM_S block).  They are anchored at BOTH ends against a
# decoy, because `--output-on-failure` puts arbitrary test output in this log
# and a test asserting ON ctest output naturally quotes ctest's phrasing.  Both
# looser forms were tried there and both were defeated; neither was caught by
# reading — T14b of ci/test-peak-rss.sh caught them.  Re-deriving them here
# rather than copying would re-open a closed hole, so if you change one, change
# both and re-run that harness.
RAN_RX = re.compile(r"^\d+% tests passed, \d+ tests failed out of (\d+)$")
REAL_RX = re.compile(r"Total Test time \(real\)\s*=\s*([0-9.]+)")
DUR_RX = re.compile(r"^ *\d+/\d+ +Test +#\d+.*?([0-9.]+) sec$")

# The concurrency oracle.  `Start <n>: <name>` when a test is dispatched;
# `  k/N Test #<n>: ...` when it completes.  Pairing them by TEST NUMBER gives
# the maximum number of tests in flight at once — a direct count of what ctest
# actually scheduled, independent of any timing ratio.
START_RX = re.compile(r"^ *Start +(\d+): ")
DONE_RX = re.compile(r"^ *\d+/\d+ +Test +#(\d+): ")


def parse_ctest_log(path: pathlib.Path) -> dict:
    """Extract the workload, the timing, and the ACHIEVED parallel level."""
    out: dict = {"ran": None, "real_s": None, "sum_s": None,
                 "max_inflight": None, "inflight_anomalies": 0}
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return out

    total = 0.0
    inflight: set[str] = set()
    peak_inflight = 0
    anomalies = 0

    for line in text.splitlines():
        # `ran` and `real_s`: LAST match wins — the summary is printed after the
        # tests it summarises, and a re-run inside one log would leave two.
        m = RAN_RX.match(line)
        if m:
            out["ran"] = int(m.group(1))
        m = REAL_RX.search(line)
        if m:
            out["real_s"] = float(m.group(1))
        m = DUR_RX.match(line)
        if m:
            total += float(m.group(1))

        m = START_RX.match(line)
        if m:
            num = m.group(1)
            if num in inflight:
                # A second Start for a test already in flight cannot happen in a
                # well-formed log.  Count it rather than absorbing it: a parser
                # that silently tolerates malformed input reports a plausible
                # number from a log it did not understand.
                anomalies += 1
            inflight.add(num)
            peak_inflight = max(peak_inflight, len(inflight))
            continue
        m = DONE_RX.match(line)
        if m:
            inflight.discard(m.group(1))

    if total > 0:
        out["sum_s"] = round(total, 1)
    if peak_inflight > 0:
        out["max_inflight"] = peak_inflight
    out["inflight_anomalies"] = anomalies
    return out


def read_kv(path: pathlib.Path) -> dict[str, str]:
    """Read a key=value report as DATA.  Never sourced — the peak-RSS report
    carries a `command=` line holding the measured command verbatim."""
    out: dict[str, str] = {}
    try:
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if "=" in line:
                key, _, value = line.partition("=")
                out.setdefault(key.strip(), value)
    except OSError:
        pass
    return out


def pct_diff(a: float, b: float) -> float:
    """Symmetric percentage difference, against the mean.

    Against the mean rather than against `a`: which serial pass is the
    denominator is an arbitrary choice, and a threshold that moves when you swap
    them is a threshold nobody can reason about.
    """
    mid = (a + b) / 2.0
    return abs(a - b) / mid * 100.0 if mid > 0 else float("inf")


def gib(raw: str) -> str:
    try:
        return f"{int(raw) / 1073741824:.2f} GiB"
    except (TypeError, ValueError):
        return "n/a"


class Pass:
    """One ctest pass: its request, what it actually did, and what it cost."""

    def __init__(self, run_dir: pathlib.Path, index: int):
        self.index = index
        self.meta = read_kv(run_dir / f"pass{index}.meta")
        self.peak = read_kv(run_dir / f"pass{index}.peak.env")
        self.log = parse_ctest_log(run_dir / f"pass{index}.ctest.log")
        self.cov = read_kv(run_dir / f"pass{index}.coverage.env")
        self.label = self.meta.get("label", f"pass{index}")
        try:
            self.jobs = int(self.meta.get("jobs", "0"))
        except ValueError:
            self.jobs = 0
        self.san = self.meta.get("san_count", "")
        self.ctest_status = self.meta.get("ctest_status", "")

    @property
    def present(self) -> bool:
        return bool(self.meta) and self.log["real_s"] is not None

    @property
    def wall(self) -> float | None:
        return self.log["real_s"]


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("run_dir")
    ap.add_argument("--tolerance-pct", type=float, default=5.0,
                    help="max allowed disagreement between the two serial passes")
    ap.add_argument("--steal-tolerance-ticks", type=int, default=0,
                    help="cumulative /proc/stat steal permitted across the experiment")
    # ⚠️ 25 %, AND THE LOOSENESS IS THE POINT — it is what this signal can carry.
    #
    # MEASURED: six back-to-back witnesses on a contended host spread **16.0 %**
    # on the 1-proc arm and 8.4 % on the 4-proc arm, using the min-of-5
    # estimator (a single timing spreads 12-15 %, and does NOT tighten with more
    # iterations — it is contention, not sampling error).  The only CI
    # observation available is the trusted `linux-clang-debug` A-B-A, whose
    # 1-proc calibration moved 2.73 -> 2.94 s = **7.4 %** across an experiment
    # everyone agrees was clean.  A 10 % gate would therefore void honest
    # samples routinely, and a gate that cries wolf gets widened until it
    # detects nothing.
    #
    # ⚠️ AND THE DRIFT SIGNAL IS WEAKER THAN IT LOOKS, which is why it is not
    # allowed to be the only thing standing between a sample and VALID.
    # Calibration drift detects the machine CHANGING between passes — which is
    # exactly what A-vs-A' already detects, directly, with the real workload
    # rather than a proxy.  The case it was hoped to add, a machine uniformly
    # degraded for the whole experiment, produces ZERO drift and is invisible to
    # both.  Catching that needs the ABSOLUTE calibration against a known-good
    # band, and no such band exists yet: one CI observation is not a band.  The
    # absolute figures are recorded on every run so the corpus accumulates, and
    # that is stated as an open gap rather than papered over with a threshold
    # nobody can defend.
    ap.add_argument("--calib-tolerance-pct", type=float, default=25.0,
                    help="drift between first and last witness beyond which the sample voids; "
                         "loose on purpose — see the comment above")
    args = ap.parse_args()

    run_dir = pathlib.Path(args.run_dir)
    if not run_dir.is_dir():
        print(f"::error::#267 verdict: {run_dir} is not a directory. Nothing was measured.")
        return 2

    passes = [Pass(run_dir, i) for i in (1, 2, 3)]
    present = [p for p in passes if p.present]

    preset = next((p.meta.get("preset", "") for p in passes if p.meta.get("preset")), "<unknown>")
    subset = next((p.meta.get("subset", "") for p in passes if p.meta.get("subset")), "")

    out: list[str] = []
    out.append(f"## `{preset}` — A-B-A parallelism sample (#267)")
    out.append("")
    if subset:
        out.append(f"> ⚠️ **SMOKE RUN — NOT EVIDENCE.** Ran a subset (`-R {subset}`), not the "
                   f"lane's suite. This exercises the apparatus; it cannot support an "
                   f"`execution.jobs` decision for this lane.")
        out.append("")

    # ── The facts table, printed whatever the verdict ────────────────────────
    out.append("| pass | order | `--parallel` | wall | max in-flight | achieved concurrency "
               "| summed test time | peak RSS | tests | sanitizer reports |")
    out.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for p in passes:
        if not p.present:
            out.append(f"| {p.label} | {p.index} | {p.jobs or '?'} | **ABSENT** | — | — | — | — | — | — |")
            continue
        conc = ("n/a" if not (p.log["sum_s"] and p.wall)
                else f"{p.log['sum_s'] / p.wall:.2f}x")
        out.append(
            f"| {p.label} | {p.index} | {p.jobs} | {p.wall:.1f} s "
            f"| {p.log['max_inflight'] or 'n/a'} | {conc} "
            f"| {p.log['sum_s'] or 'n/a'} s | {gib(p.peak.get('peak_bytes', ''))} "
            f"| {p.log['ran'] or 'n/a'} | {p.san or 'n/a'} |")
    out.append("")

    instrument: list[str] = []
    defects: list[str] = []
    voids: list[str] = []

    # ── (2) INSTRUMENT — did we measure anything at all? ─────────────────────
    if len(present) < 3:
        missing = [f"pass{p.index} ({p.label})" for p in passes if not p.present]
        instrument.append(
            f"{len(present)} of 3 passes produced a parsable ctest log — missing: "
            f"{', '.join(missing)}. An A-B-A sample with a missing arm is not a "
            f"weaker sample, it is not a sample: the whole design is the two serial "
            f"passes bracketing the parallel one.")

    for p in present:
        if p.log["ran"] is None:
            instrument.append(f"{p.label}: ctest printed no `N% tests passed ... out of M` "
                              f"summary, so the workload size is unknown.")
        if p.log["max_inflight"] is None:
            instrument.append(f"{p.label}: no `Start <n>:` lines in the log, so the achieved "
                              f"parallel level could not be observed at all.")
        if p.log["inflight_anomalies"]:
            instrument.append(
                f"{p.label}: {p.log['inflight_anomalies']} malformed Start/complete pairing(s) "
                f"in the ctest log. The in-flight oracle did not understand this log; its "
                f"max-in-flight figure is not trustworthy.")
        peak_status = p.peak.get("status")
        if peak_status and peak_status != "ok":
            out.append(f"> ⚠️ `{p.label}` peak RSS NOT MEASURED "
                       f"(`status={peak_status}`) — #267 acceptance item 1 is not "
                       f"discharged for this pass.")

    # ⚠️ THE SPURIOUS-HIT GUARD, and the reason it is an INSTRUMENT check and
    # not a result.  If `--parallel N` fails to take effect, all three passes run
    # identically: A and A' then agree PERFECTLY, every count matches, no
    # sanitizer moves, and the apparatus reports a beautiful VALID sample saying
    # "no speedup available on this lane" — a wrong conclusion with a clean bill
    # of health.  The A-vs-A' check is a forced-MISS arm and cannot catch it.
    #
    # MEASURED, because the risk is not hypothetical and the obvious mitigation
    # is the wrong one: `ctest --preset P --parallel 1` DOES override a preset's
    # `execution: {jobs: 4}` (8.03 s vs 2.01 s on an 8x1 s synthetic suite), but
    # `CTEST_PARALLEL_LEVEL=1 ctest --preset P` does NOT (2.01 s — the preset
    # wins).  Two lanes already carry `jobs: 4`, so the env-var form would have
    # silently run all three passes at 4 on exactly the lanes a campaign would
    # start from.  The apparatus must use the flag; this check is what notices
    # if that ever stops being true.
    for p in present:
        got = p.log["max_inflight"]
        if got is None or p.jobs <= 0:
            continue
        if p.jobs == 1 and got > 1:
            instrument.append(
                f"{p.label} requested `--parallel 1` but ctest scheduled {got} tests "
                f"concurrently. The serial baseline is not serial, so the speedup this "
                f"sample would report is measured against the wrong floor.")
        if p.jobs > 1 and got <= 1:
            instrument.append(
                f"{p.label} requested `--parallel {p.jobs}` but ctest never had more than "
                f"{got} test in flight. The widening did NOT take effect, so this sample "
                f"measures one configuration three times — and would report `no gain` "
                f"having tested nothing.")

    # ── (1) DEFECT — #267 acceptance items 2, 5 and 4 ────────────────────────
    counts = {p.label: p.log["ran"] for p in present if p.log["ran"] is not None}
    if len(set(counts.values())) > 1:
        defects.append(
            f"THE PASSES DID NOT RUN THE SAME TESTS: {counts}. #267 acceptance item 2 — a "
            f"`--parallel` change that silently drops tests reads as a pure win, because "
            f"the wall clock falls and nothing else moves. Whichever pass is short, the "
            f"speedup between them is not a speedup.")

    # A pass whose ctest FAILED did not run the workload the other two ran, so
    # the comparison between them is not a comparison.  Which bucket it lands in
    # depends on WHICH pass failed, and the distinction is the whole point:
    # failures confined to the parallel pass are a finding ABOUT parallelism;
    # failures elsewhere mean the lane is simply red and nothing here is a
    # parallelism measurement at all.
    failed = [p for p in present if p.ctest_status not in ("", "0")]
    if failed:
        par_failed = [p for p in failed if p.jobs > 1]
        ser_failed = [p for p in failed if p.jobs == 1]
        if par_failed and not ser_failed:
            defects.append(
                f"TESTS FAIL UNDER `--parallel {par_failed[0].jobs}` BUT PASS SERIALLY: "
                f"{par_failed[0].label} exited {par_failed[0].ctest_status} while both serial "
                f"passes were green. That is a defect the widening would introduce, and it is "
                f"real until disproven — read the pass's LastTest.log before anything else.")
        else:
            instrument.append(
                "ctest FAILED in " + ", ".join(f"{p.label} (exit {p.ctest_status})" for p in failed)
                + ". A pass that did not complete its suite is not comparable with one that did, "
                "and a failure outside the parallel pass says the lane is red for reasons that "
                "have nothing to do with `--parallel`. Fix the lane, then measure it.")

    san = {}
    for p in present:
        try:
            san[p.label] = int(p.san)
        except (TypeError, ValueError):
            continue
    par = [p for p in present if p.jobs > 1]
    ser = [p for p in present if p.jobs == 1]
    if par and ser and all(p.label in san for p in par + ser):
        worst_serial = max(san[p.label] for p in ser)
        for p in par:
            if san[p.label] > worst_serial:
                defects.append(
                    f"A SANITIZER REPORT APPEARED AT HIGHER CONCURRENCY: {p.label} "
                    f"(`--parallel {p.jobs}`) emitted {san[p.label]} report(s) against "
                    f"{worst_serial} in the serial pass(es). #267 acceptance item 5 — this is "
                    f"a REAL DEFECT UNTIL DISPROVEN, never a 'parallelism artifact'. "
                    f"Read the pass's LastTest.log before doing anything else with this lane.")

    # Coverage (item 4).  Two comparisons, because only their DIFFERENCE
    # attributes anything: A-vs-A' is this suite's own run-to-run coverage
    # determinism, measured on the same VM at the same concurrency.  A-vs-B is
    # the parallelism effect.  B differing while A and A' agree is attributable
    # to `--parallel`; all three differing is a nondeterministic suite, which is
    # a finding of its own and NOT evidence about parallelism.
    cov = {p.label: p.cov.get("sorted_info_sha256", "") for p in present if p.cov.get("sorted_info_sha256")}
    if len(cov) == 3 and ser and par:
        ser_shas = {p.label: cov[p.label] for p in ser if p.label in cov}
        par_shas = {p.label: cov[p.label] for p in par if p.label in cov}
        serial_agree = len(set(ser_shas.values())) == 1
        if not serial_agree:
            out.append("> ⚠️ **The two SERIAL passes produced different merged coverage.** That is "
                       "this suite's own run-to-run coverage nondeterminism, measured at "
                       "unchanged concurrency — so #267 item 4 cannot be attributed to "
                       "`--parallel` from this sample either way. Worth its own issue.")
        elif set(par_shas.values()) != set(ser_shas.values()):
            defects.append(
                "MERGED COVERAGE CHANGED UNDER PARALLELISM: the two serial passes agree "
                "byte-for-byte on a sorted lcov `.info`, and the parallel pass does not. "
                "#267 acceptance item 4 asks exactly this question and the answer is no. "
                "`%p` in LLVM_PROFILE_FILE was expected to give each process its own "
                "profraw; that expectation is now measured false for this configuration.")

    # ── Is this the LANE's workload, or some other workload? ─────────────────
    #
    # The three passes agreeing with each other says nothing about whether they
    # agree with PRODUCTION.  A measurement job that configures the tree even
    # slightly differently from the shipping lane measures a suite that does not
    # ship, and the speedup it reports is for a configuration nobody runs.
    # `ci/expected-eligible-tests.txt` is the per-lane pin that already exists
    # for exactly this question, read off real CI runs.
    #
    # The disposition is the one the probe document already designed for a basis
    # mismatch, quoted: it "degrades the run to DIAGNOSTIC ONLY, i.e. toward
    # 'not evidence' — never toward a false acceptance".  So VOID, not DEFECT:
    # a count that disagrees with the pin is far more likely to mean this job
    # configured differently than to mean the lane broke.
    #
    # ⚠️ A preset with NO pin line is not a violation — the Windows lanes have
    # never had one.  Absence is disclosed and the check stands down; inventing
    # an expectation for them would manufacture a void on every Windows sample.
    expected_file = pathlib.Path(__file__).resolve().parent / "expected-eligible-tests.txt"
    expected = None
    try:
        for line in expected_file.read_text(encoding="utf-8").splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] == preset and parts[1].isdigit():
                expected = int(parts[1])
                break
    except OSError:
        pass
    if subset:
        pass                       # a subset run is already stamped NOT EVIDENCE
    elif expected is None:
        out.append(f"> ⚠️ No line for `{preset}` in `ci/expected-eligible-tests.txt`, so the "
                   f"workload could not be checked against the lane's production basis. "
                   f"The passes agree with each other; whether they agree with the shipping "
                   f"lane is unverified here.")
    elif counts and set(counts.values()) == {expected}:
        out.append(f"**Workload basis:** {expected} tests, matching "
                   f"`ci/expected-eligible-tests.txt` for this lane ✅")
    elif counts:
        voids.append(
            f"THIS IS NOT THE LANE'S PRODUCTION WORKLOAD: the passes ran {sorted(set(counts.values()))} "
            f"tests against the pinned basis of {expected} for `{preset}`. Whatever was measured, "
            f"it was not the suite that ships — most likely this job configured the tree "
            f"differently from the lane. Reconcile the configuration (or re-record the pin in "
            f"the same commit, if the lane genuinely changed) and re-dispatch; the criterion "
            f"closes on the run AFTER that reconciliation, never on the mismatched one.")

    # ── (3) VOID — did the machine hold still? ───────────────────────────────
    if len(ser) == 2 and all(p.wall for p in ser):
        a, b = ser[0], ser[1]
        drift = pct_diff(a.wall, b.wall)
        verdict_line = (f"serial passes {a.label} {a.wall:.1f} s vs {b.label} {b.wall:.1f} s "
                        f"— **{drift:.1f} %** apart (tolerance {args.tolerance_pct:.1f} %)")
        if drift > args.tolerance_pct:
            voids.append(
                f"THE TWO SERIAL PASSES DISAGREE: {verdict_line}. The machine did not hold "
                f"still, so the middle pass's speedup is measured against a floor that "
                f"moved under it. This is the check that makes an A-B-A worth running; "
                f"`linux-clang-debug`'s trusted 2.10x had its serial passes agree to 0.6 %.")
        else:
            out.append(f"**A-vs-A' agreement:** {verdict_line} ✅")
        out.append("")
    elif len(ser) != 2:
        instrument.append(
            f"expected exactly two serial passes bracketing one parallel pass; found "
            f"{len(ser)} serial and {len(par)} parallel. That is not the A-B-A design.")

    # The independent machine witnesses.  These catch what A-vs-A' cannot: a VM
    # that was uniformly slow throughout, where the two serial passes agree
    # perfectly with each other and with nothing in production.
    wit = [read_kv(run_dir / f"witness{i}.env") for i in range(4)]
    # `no-procfs` is a USABLE witness, not a broken one: it is the normal state
    # on the Windows lanes, where the calibrations run fine and only the steal
    # counter is unavailable.  Requiring `ok` would void every Windows sample
    # for want of a Linux file — on `windows-msvc-asan`, the matrix critical path
    # and the one lane with no measurement of any kind.
    wit_ok = [w for w in wit if w.get("status") in ("ok", "no-procfs")]
    if len(wit_ok) < 2:
        voids.append(
            f"only {len(wit_ok)} usable machine witness(es) — the experiment has no "
            f"independent observation of the machine, so 'the machine did not change' is "
            f"an assumption here rather than a measurement. An absent witness degrades a "
            f"sample toward VOID; it never certifies one.")
    else:
        try:
            steals = [int(w["steal_ticks"]) for w in wit_ok if w.get("steal_ticks", "").strip()]
            if not steals:
                out.append("**Machine witness:** steal counter unavailable on this platform — "
                           "calibration drift is the only machine observation here.")
            else:
                delta = max(steals) - min(steals)
                if delta > args.steal_tolerance_ticks:
                    voids.append(
                        f"/proc/stat STEAL ROSE BY {delta} ticks across the experiment "
                        f"(tolerance {args.steal_tolerance_ticks}). Another tenant was on the "
                        f"physical host while these passes ran. `linux-clang-debug`'s "
                        f"trusted sample had steal 0 throughout.")
                else:
                    out.append(f"**Machine witness:** steal delta {delta} ticks ✅")
        except (ValueError, KeyError):
            pass
        try:
            first = float(wit_ok[0]["calib_1proc_s"])
            last = float(wit_ok[-1]["calib_1proc_s"])
            drift = pct_diff(first, last)
            firstn = float(wit_ok[0]["calib_nproc_s"])
            lastn = float(wit_ok[-1]["calib_nproc_s"])
            driftn = pct_diff(firstn, lastn)
            if max(drift, driftn) > args.calib_tolerance_pct:
                voids.append(
                    f"THE MACHINE'S OWN SPEED DRIFTED: 1-proc calibration {first:.2f} -> "
                    f"{last:.2f} s ({drift:.1f} %), {wit_ok[0].get('procs','N')}-proc "
                    f"{firstn:.2f} -> {lastn:.2f} s ({driftn:.1f} %), against a "
                    f"{args.calib_tolerance_pct:.1f} % tolerance. Measured on a fixed "
                    f"instruction-count loop that does not touch the suite, so this is the "
                    f"VM changing, not the tests.")
            else:
                out.append(f"**Machine witness:** calibration drift {drift:.1f} % (1-proc) / "
                           f"{driftn:.1f} % ({wit_ok[0].get('procs','N')}-proc) ✅")
        except (ValueError, KeyError):
            pass
        out.append("")

    # ── The verdict ──────────────────────────────────────────────────────────
    rc = 0
    if instrument:
        rc = 2
        out.append("### ⛔ INSTRUMENT FAILURE — this run measured nothing usable")
    elif defects:
        rc = 1
        out.append("### 🔴 DEFECT — a real defect surfaced, real until disproven")
    elif voids:
        rc = 3
        out.append("### ⚠️ VOID — measured, but not evidence. Re-dispatch.")
    else:
        rc = 0
        out.append("### ✅ VALID — this sample is evidence")

    out.append("")
    for item in instrument + defects + voids:
        out.append(f"- {item}")
    if instrument or defects or voids:
        out.append("")

    # THE CONCLUSION IS WITHHELD unless the sample earned it.  A speedup printed
    # beside a caveat is read as a speedup; three withdrawn claims in the probe
    # document are what that costs.
    if rc == 0 and ser and par:
        base = sum(p.wall for p in ser) / len(ser)
        b = par[0]
        speedup = base / b.wall if b.wall else 0.0
        eff = speedup / b.jobs * 100.0 if b.jobs else 0.0
        sum_ser = sum(p.log["sum_s"] or 0 for p in ser) / len(ser)
        sum_par = b.log["sum_s"] or 0
        infl = (sum_par - sum_ser) / sum_ser * 100.0 if sum_ser else 0.0
        peak_ser = max((int(p.peak.get("peak_bytes") or 0) for p in ser), default=0)
        peak_par = int(b.peak.get("peak_bytes") or 0)
        mem = ((peak_par - peak_ser) / peak_ser * 100.0) if peak_ser else 0.0
        out.append(f"**`--parallel {b.jobs}` on `{preset}`: {speedup:.2f}x** "
                   f"({base:.1f} s serial -> {b.wall:.1f} s), parallel efficiency "
                   f"{eff:.0f} %, summed test time {infl:+.1f} %, peak RSS {mem:+.1f} % "
                   f"({gib(str(peak_ser))} -> {gib(str(peak_par))}).")
        out.append("")
        # ⚠️ BOTH AXES, ALWAYS.  `linux-clang-tsan`'s failure mode was wall clock
        # barely moving while AGGREGATE runner time rose 44 %.  A lane that
        # finishes sooner while spending more runner-minutes may still be worth
        # it on the critical path and is clearly not worth it elsewhere — the
        # decision cannot be made from the speedup column alone.
        if infl > 25.0:
            out.append(f"> ⚠️ Summed test time rose **{infl:+.1f} %**: this lane finishes sooner "
                       f"but spends more runner-minutes doing it. Worth it on the matrix "
                       f"critical path, not elsewhere. This is the axis `linux-clang-tsan` "
                       f"failed on while its wall clock looked flat.")
        if subset:
            out.append("> ⚠️ Subset run — the figure above describes the subset, not the lane.")

    text = "\n".join(out)
    print(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY", "")
    if summary:
        try:
            with open(summary, "a", encoding="utf-8") as fh:
                fh.write(text + "\n\n")
        except OSError:
            pass
    return rc


if __name__ == "__main__":
    sys.exit(main())
