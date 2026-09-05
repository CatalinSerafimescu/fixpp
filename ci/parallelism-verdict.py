#!/usr/bin/env python3
"""Decide whether one lane's A-B-A parallelism sample is EVIDENCE (#267).

    ci/parallelism-verdict.py <run-dir> [--tolerance-pct 5.0]
                                        [--steal-tolerance-ticks 0]
                                        [--calib-tolerance-pct 25.0]

⚠️ The two extra tolerances have no caller today, and that is deliberate rather
than dead: the raw sample is uploaded as a build artifact, so re-judging an
ARCHIVED campaign at a different band is a thing an operator does — and it must
not require editing a version-controlled file that a harness pins. Only
`--tolerance-pct` is wired to a workflow input. The other two default to values
whose derivation is recorded at their definitions below; they are documented
here because an undocumented flag with no caller is indistinguishable from a
forgotten one.

WHY THIS EXISTS
===============

`ci/ctest-parallelism-probe.md` records THREE `execution.jobs` conclusions that
were measured, published, and then WITHDRAWN — `debug`'s, `ubsan`'s, and
`tsan`'s "no gain at j=4".  Every one failed the same way: an A/B across two
jobs, on lanes whose between-VM wall-clock spread swamps the effect.  Two of them
were later re-measured properly and came back POSITIVE (2.10x and 1.79x), so
this is not a story about parallelism being useless — it is a story about a
measurement design.  On that
spread a single unpaired pair of runs cannot distinguish a 1.8x speedup from VM
luck, and the repo has the receipts:

  * One lane's serial time on a single VM was roughly HALF its time on three
    others.  Every "no gain" reading for that lane came from comparing against
    that one.
  * Another lane's j=2-vs-j=4 comparison spans a spread wider than the effect it
    was trying to measure; its "measured no gain" wording is withdrawn in the
    probe document.

The figures behind both live in that document and are deliberately not copied
here — a number in five files is a number that gets corrected in one.

The design that survived is the one `linux-clang-debug` was finally settled
with: three passes in ONE job on ONE VM, serial-parallel-serial, **voided unless
the two serial passes agree** (they agreed to 0.6 %).  This file is that
design's judgement, made mechanical — because the same document shows the
judgement is exactly the part that gets skipped when a number looks convincing.

⚠️ THE RESIDUAL THIS DESIGN DOES NOT CLOSE
==========================================

`tools/bench_compare.py:run_paired` — this repo's OTHER paired same-VM
instrument — documents a real bypass in any A-B-A that checks only A-vs-A':
a transient confined to the middle leg leaves the two outer legs agreeing
perfectly while the middle one is wrong.  Its answer was to measure the base
twice as well, A-B-A-B, so that a transient large enough to matter has to land
on both B legs while sparing both A legs.

**This apparatus does not do that, and the reason is budget, not disagreement.**
A fourth pass on the slowest lane would put it past the 360-minute hosted cap,
and a job that hits the cap is killed before its upload step — losing all four
passes rather than three. Re-derive the margin before assuming it still holds:
the lane's current test-phase duration is in `ci/ctest-parallelism-probe.md`.

What is done instead, and exactly how far it goes:

* **A hypervisor-caused transient confined to the parallel pass IS caught**, by
  differencing `/proc/stat` steal interval by interval rather than across the
  run.  That is the common case on a shared runner and it is attributed to the
  pass it landed on.
* **A non-steal transient confined to the parallel pass is NOT caught** —
  thermal throttling, or a co-tenant whose interference does not register as
  steal.  The calibration witnesses bracket that pass but do not run during it,
  so a transient that begins after one witness and ends before the next is
  invisible to every check here.

That residual is stated rather than closed.  If a lane's result ever turns on a
margin small enough for it to matter, the answer is `run_paired`'s — add the
fourth pass and dispatch that lane alone.

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
import math
import os
import pathlib
import re
import sys

# ── ctest log parsers ────────────────────────────────────────────────────────
#
# ⚠️ THE FIRST THREE ARE PYTHON RE-DERIVATIONS OF awk EXPRESSIONS IN
# ci/peak-memory-report.sh (its RAN / CTEST_REAL / SUM_S block) — semantics for
# semantics, NOT copies, because the two files are not in the same language.
# Saying "copied verbatim" would be a claim this file cannot support, and an
# unsupportable claim in a comment is the defect class this repo spends whole
# Gate B rounds on.
#
# What they inherit is the reason for their SHAPE: both are anchored at both
# ends against a decoy, because `--output-on-failure` puts arbitrary test output
# in this log and a test asserting ON ctest output naturally quotes ctest's
# phrasing.  Two looser forms were tried there and both were defeated by exactly
# that; neither was caught by reading — T14b of ci/test-peak-rss.sh caught them.
#
# ⚠️ THE AGREEMENT IS CHECKED, NOT ASSERTED.  A comment saying "change one,
# change both" is an instruction someone has to follow; cell S4 of
# ci/test-parallelism-aba-seam.sh runs peak-memory-report.sh's awk expressions
# over a REAL ctest log and requires the same three numbers this parser reads
# from it.  That is the only version of this claim that cannot rot.
RAN_RX = re.compile(r"^\d+% tests passed, \d+ tests failed out of (\d+)$")
REAL_RX = re.compile(r"^Total Test time \(real\) =\s+([0-9.]+) sec$")
DUR_RX = re.compile(r"^ *\d+/\d+ +Test +#\d+.*?([0-9.]+) sec$")

# The concurrency oracle.  `Start <n>: <name>` when a test is dispatched;
# `  k/N Test #<n>: ...` when it completes.  Pairing them by TEST NUMBER gives
# the maximum number of tests in flight at once — a direct count of what ctest
# actually scheduled, independent of any timing ratio.
START_RX = re.compile(r"^ *Start +(\d+): ")
DONE_RX = re.compile(r"^ *\d+/\d+ +Test +#(\d+): ")


def parse_ctest_log(path: pathlib.Path) -> dict:
    """Extract the workload, the timing, and the ACHIEVED parallel level.

    ⚠️ EVERY STRUCTURAL EXPECTATION IS CHECKED, AND A LOG THAT VIOLATES ONE IS
    REFUSED RATHER THAN SUMMARISED.  A hostile review demonstrated all three of
    the following against the permissive version this replaces, with a forged
    log built from ordinary test output:

        honest         ran=2 real=1.0 sum=2.0 max_inflight=2
        fake-up        ran=1                  max_inflight=4   <- forged upward
        fake-down      ran=2 sum=1.0          max_inflight=1   <- forged downward
        timing-decoys  ran=999 real=999.0 sum=778.0

    and a log claiming 362 tests while carrying one completion record read
    **VALID**.  That matters because `--output-on-failure` puts arbitrary test
    output in this log, so "a line that looks like ctest's" is not evidence that
    ctest wrote it — and the achieved parallel level is the one thing standing
    between this apparatus and measuring one configuration three times.

    So: exactly one summary, exactly one total-time line, exactly `ran` starts
    and `ran` completions with matching ids, no unmatched completion, and an
    empty in-flight set at EOF.  Anything else sets `anomalies`, and the verdict
    turns that into an INSTRUMENT FAILURE.

    ⚠️ This is deliberately FAIL-CLOSED, and it can fire on an honest log: a
    test that prints a ctest-shaped line would now be refused rather than
    silently mis-parsed.  That is the correct direction — refusing to report on
    a log this parser does not understand costs a re-dispatch; summarising it
    costs a wrong `execution.jobs` decision, which is what this whole issue is
    about.

    ⚠️ IN PRACTICE THE FAIL-CLOSED SIDE IS NARROW, and the reason is structural
    rather than a hope: `--output-on-failure` prints a test's output only when
    that test FAILS, so on an all-green pass no test output reaches this log at
    all.  A pass with a failing test is already an INSTRUMENT FAILURE by the
    `ctest_status` check, so the two dispositions agree.  MEASURED on a real
    ctest run rather than argued: a `WILL_FAIL` test echoing `    Start 99: ...`
    put that line in the log and lifted `max_inflight` from 1 to 2 on a SERIAL
    pass — the forgery is live, not hypothetical — and this parser refused it.

    Because a refusal has to be actionable, the offending lines are quoted: a
    diagnostic that says only "11 Start records against 10 tests" sends the
    reader back to a 300 MB artifact to find out which one.
    """
    out: dict = {"ran": None, "real_s": None, "sum_s": None,
                 "max_inflight": None, "anomalies": [], "read": False}
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return out
    out["read"] = True

    summaries: list[int] = []
    reals: list[float] = []
    durations: list[float] = []
    inflight: set[str] = set()
    peak_inflight = 0
    starts = 0
    completions = 0
    unmatched: list[str] = []
    start_lines: dict[str, str] = {}

    for line in text.splitlines():
        m = RAN_RX.match(line)
        if m:
            summaries.append(int(m.group(1)))
        m = REAL_RX.match(line)
        if m:
            reals.append(float(m.group(1)))
        m = DUR_RX.match(line)
        if m:
            durations.append(float(m.group(1)))

        m = START_RX.match(line)
        if m:
            num = m.group(1)
            if num in inflight:
                unmatched.append(f"test #{num} started twice while already in flight: "
                                 f"{line.strip()!r}")
            starts += 1
            start_lines.setdefault(num, line.strip())
            inflight.add(num)
            peak_inflight = max(peak_inflight, len(inflight))
            continue
        m = DONE_RX.match(line)
        if m:
            completions += 1
            num = m.group(1)
            if num not in inflight:
                unmatched.append(f"test #{num} completed without a matching Start: "
                                 f"{line.strip()!r}")
            inflight.discard(num)

    a = out["anomalies"]
    if len(summaries) != 1:
        a.append(f"{len(summaries)} ctest summary line(s); exactly 1 is expected")
    else:
        out["ran"] = summaries[0]
    if len(reals) != 1:
        a.append(f"{len(reals)} `Total Test time (real)` line(s); exactly 1 is expected")
    else:
        out["real_s"] = reals[0]
    # ⚠️ THE COUNT OF DURATION LINES IS CHECKED BELOW; THE VALUES ARE NOT.
    # `sum_s` drives the achieved-concurrency column and the runner-minutes
    # warning, and a log carrying the right NUMBER of duration lines with wrong
    # values is summarised without complaint. Validating the values would need a
    # second source for them, which does not exist — so this is stated rather
    # than implied away by the decoy-anchoring note above.
    if durations:
        out["sum_s"] = round(sum(durations), 1)
    if peak_inflight > 0:
        out["max_inflight"] = peak_inflight
    if inflight:
        quoted = ", ".join(repr(start_lines[n]) for n in sorted(inflight)[:3])
        a.append(f"{len(inflight)} test(s) started and never completed: {quoted}")
    a.extend(unmatched[:5])

    ran = out["ran"]
    if ran is not None:
        for label, got in (("Start", starts), ("completion", completions),
                           ("per-test duration", len(durations))):
            if got != ran:
                a.append(f"{got} {label} record(s) against a reported {ran} tests")
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


# The shipped band. Named rather than inlined so the disclosure below can say
# what a widened value was widened FROM.
DEFAULT_TOLERANCE_PCT = 5.0


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
        # ⚠️ "PRESENT" MEANS THE LOG WAS THERE, NOT THAT IT PARSED CLEANLY.
        # Keying this on `real_s` conflated a MALFORMED pass with a MISSING one:
        # a log carrying two `Total Test time (real)` lines left `real_s` unset,
        # the pass dropped out of `present`, and the verdict reported "pass2 is
        # missing" about a file that was right there — sending the reader to
        # look for an upload failure instead of at the two summary lines. Both
        # are INSTRUMENT FAILURES, so the exit code was right and the diagnosis
        # was wrong, which is the harder kind to notice.
        return bool(self.meta) and self.log["read"]

    @property
    def wall(self) -> float | None:
        return self.log["real_s"]


def main() -> int:
    # ⚠️ WINDOWS CONSOLES DEFAULT TO cp1252, WHICH CANNOT ENCODE THIS FILE'S
    # OUTPUT. Every table and disposition here carries ⚠️/✅/⛔/🔴, so on the
    # Windows lanes `print(text)` raised UnicodeEncodeError and the step died
    # with a traceback INSTEAD OF A VERDICT — exit 1, which reads as "the
    # measurement failed" rather than "the reporter could not speak". Measured
    # on a real Windows checkout, not inferred; it would have hit every Windows
    # verdict step.
    #
    # `errors="replace"` rather than a hard utf-8: a console that genuinely
    # cannot render a glyph should show a placeholder, never lose the verdict.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass  # not a reconfigurable stream; the text is still written below

    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("run_dir")
    ap.add_argument("--tolerance-pct", type=float, default=DEFAULT_TOLERANCE_PCT,
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

    # ⚠️ NaN DISABLES EVERY GATE IT TOUCHES, SILENTLY. `argparse(type=float)`
    # accepts "nan", the dispatch input is unrestricted text, and every
    # comparison with NaN is False — so `--tolerance-pct nan` made serial passes
    # of 1000 s and 2000 s read VALID. A gate that can be turned off by a typo in
    # a text box is not a gate.
    for name in ("tolerance_pct", "calib_tolerance_pct"):
        value = getattr(args, name)
        if not math.isfinite(value) or value < 0:
            print(f"::error::--{name.replace('_', '-')}={value!r} is not a finite "
                  f"non-negative number. Every comparison against it would be False, which "
                  f"turns the check it governs off without saying so.")
            return 2
    if args.steal_tolerance_ticks < 0:
        print("::error::--steal-tolerance-ticks must be non-negative.")
        return 2

    run_dir = pathlib.Path(args.run_dir)
    if not run_dir.is_dir():
        print(f"::error::#267 verdict: {run_dir} is not a directory. Nothing was measured.")
        return 2

    passes = [Pass(run_dir, i) for i in (1, 2, 3)]
    present = [p for p in passes if p.present]

    # ⚠️ THE PASSES ARE IDENTIFIED BY POSITION, AND THE SHAPE IS ASSERTED.
    # An earlier version keyed counts, sanitizer totals and coverage digests by
    # the `label` field out of each pass's own meta file. A hostile review used
    # that twice: duplicate labels overwrote earlier passes so counts of
    # 362/1/999 read VALID, and a B-A-A ordering read VALID because "two serial
    # and one parallel" was accepted in any order — while the entire design is
    # serial, parallel, serial. A file describing itself is not identification.
    shape: list[str] = []
    for want, p in zip(("A", "B", "A'"), passes):
        if not p.present:
            continue
        if p.meta.get("label") != want:
            shape.append(f"pass{p.index} is labelled {p.meta.get('label')!r}, expected {want!r}")
        if p.meta.get("order") != str(p.index):
            shape.append(f"pass{p.index} records order={p.meta.get('order')!r}")
    if len(present) == 3:
        jobs = [p.jobs for p in passes]
        if not (jobs[0] == 1 and jobs[2] == 1 and jobs[1] > 1):
            shape.append(f"requested levels are {jobs}, not the A-B-A shape 1/N/1 with N > 1")
        for field in ("preset", "subset", "ctest_args"):
            seen = {p.meta.get(field, "") for p in passes}
            if len(seen) > 1:
                shape.append(f"passes disagree on {field}: {sorted(seen)} — they did not "
                             f"measure the same thing")

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
        # `wall` can be None on a present-but-malformed pass — see Pass.present.
        wall = "n/a" if p.wall is None else f"{p.wall:.1f} s"
        out.append(
            f"| {p.label} | {p.index} | {p.jobs} | {wall} "
            f"| {p.log['max_inflight'] or 'n/a'} | {conc} "
            f"| {p.log['sum_s'] or 'n/a'} s | {gib(p.peak.get('peak_bytes', ''))} "
            f"| {p.log['ran'] or 'n/a'} | {p.san or 'n/a'} |")
    out.append("")

    instrument: list[str] = list(shape)
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
        # ⚠️ A ZERO WALL TIME IS NOT A FAST PASS. `base / b.wall if b.wall else
        # 0.0` printed `0.00x` for one — which is not a measurement, it is what
        # the guard against ZeroDivisionError prints. Ten lines below, the
        # memory clause is WITHHELD for exactly this reason ("a fabricated zero
        # next to genuine numbers is worse than a gap"); the same reasoning
        # applies here and the code used to do the opposite.
        if p.wall is not None and p.wall <= 0:
            instrument.append(
                f"{p.label} reports a wall time of {p.wall}. No speedup can be computed from "
                f"it, and a ratio against zero is a formatting artefact, not a result.")
        if p.log["max_inflight"] is None:
            instrument.append(f"{p.label}: no `Start <n>:` lines in the log, so the achieved "
                              f"parallel level could not be observed at all.")
        if p.log["anomalies"]:
            instrument.append(
                f"{p.label}: this ctest log is not structurally well-formed — "
                + "; ".join(p.log["anomalies"][:4]) +
                ". The parser refuses to summarise a log it does not understand: "
                "`--output-on-failure` puts arbitrary test output here, so a line that "
                "LOOKS like ctest's is not evidence ctest wrote it, and a forged or "
                "truncated log can move the achieved parallel level in either direction.")
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
    # MEASURED, and the obvious mitigation is the WRONG one: the CLI flag
    # overrides a preset's `execution: {jobs: N}`, and `CTEST_PARALLEL_LEVEL`
    # does not — the preset wins.  Presets already carrying a `jobs` value are
    # exactly the ones a campaign starts from.  ci/run-parallelism-aba.sh uses
    # the flag for that reason and cell S2 of ci/test-parallelism-aba-seam.sh
    # holds it there; this check is what notices if the guarantee itself ever
    # stops being true.
    for p in present:
        got = p.log["max_inflight"]
        if got is None or p.jobs <= 0:
            continue
        if p.jobs == 1 and got > 1:
            instrument.append(
                f"{p.label} requested `--parallel 1` but ctest scheduled {got} tests "
                f"concurrently. The serial baseline is not serial, so the speedup this "
                f"sample would report is measured against the wrong floor.")
        # ⚠️ `got < p.jobs`, NOT `got <= 1`. The looser form is a total-failure
        # test, and "a little parallelism happened" satisfied it: a pass that
        # requested 4 and never exceeded 2 in flight passed every gate, while
        # the headline divided the speedup by the REQUESTED level — publishing
        # "32 % parallel efficiency" for a `--parallel 4` that never ran, wrong
        # by the ratio requested/achieved. Efficiency is precisely the number an
        # `execution.jobs` decision is argued from. Asking "did ANY parallelism
        # happen" is a forced-MISS arm; the spurious hit is partial widening.
        if p.jobs > 1 and got < p.jobs:
            instrument.append(
                f"{p.label} requested `--parallel {p.jobs}` but ctest never had more than "
                f"{got} test(s) in flight. The widening {'did NOT take effect' if got <= 1 else 'only PARTLY took effect'}, "
                f"so any speedup reported for `--parallel {p.jobs}` describes a level that "
                f"never ran, and the parallel efficiency would be divided by the wrong "
                f"denominator. If this lane genuinely cannot reach {p.jobs} — a suite smaller "
                f"than the level, or RUN_SERIAL/RESOURCE_LOCK properties — then {p.jobs} is the "
                f"wrong level to be measuring, not a detail to disclose.")

    # ── (1) DEFECT — #267 acceptance items 2, 5 and 4 ────────────────────────
    counts = {f"pass{p.index} ({p.label})": p.log["ran"]
              for p in present if p.log["ran"] is not None}
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

    # ⚠️ AN UNPARSABLE COUNT IS AN INSTRUMENT FAILURE, NOT A SKIP. The driver
    # writes the literal `unreadable` when a pass's LastTest.log could not be
    # read, and this used to `continue` past it — which dropped that pass from
    # the dict, so the `all(... in san ...)` guard below removed the ENTIRE
    # item-5 check with nothing on the page. Demonstrated: five sanitizer
    # reports at --parallel 4 against zero serially read as VALID once one
    # pass's count was `unreadable`. Item 4's identical shape was already fixed;
    # this is the sibling that still failed toward clean.
    san = {}
    unreadable = []
    for p in present:
        try:
            san[p.index] = int(p.san)
        except (TypeError, ValueError):
            unreadable.append(f"{p.label} (`san_count={p.san!r}`)")
    if unreadable:
        instrument.append(
            "#267 ACCEPTANCE ITEM 5 WAS NOT DISCHARGED: no sanitizer count for "
            + ", ".join(unreadable) + ". The driver writes `unreadable` when a pass's "
            "LastTest.log could not be read, so the concurrency comparison was never made — "
            "and a report appearing at higher concurrency is a real defect until disproven, "
            "which is not something to lose quietly.")
    par = [p for p in present if p.jobs > 1]
    ser = [p for p in present if p.jobs == 1]
    if par and ser and all(p.index in san for p in par + ser):
        worst_serial = max(san[p.index] for p in ser)
        for p in par:
            if san[p.index] > worst_serial:
                defects.append(
                    f"A SANITIZER REPORT APPEARED AT HIGHER CONCURRENCY: {p.label} "
                    f"(`--parallel {p.jobs}`) emitted {san[p.index]} report(s) against "
                    f"{worst_serial} in the serial pass(es). #267 acceptance item 5 — this is "
                    f"a REAL DEFECT UNTIL DISPROVEN, never a 'parallelism artifact'. "
                    f"Read the pass's LastTest.log before doing anything else with this lane.")

    # Coverage (item 4).  Two comparisons, because only their DIFFERENCE
    # attributes anything: A-vs-A' is this suite's own run-to-run coverage
    # determinism, measured on the same VM at the same concurrency.  A-vs-B is
    # the parallelism effect.  B differing while A and A' agree is attributable
    # to `--parallel`; all three differing is a nondeterministic suite, which is
    # a finding of its own and NOT evidence about parallelism.
    cov = {p.index: p.cov.get("sorted_info_sha256", "") for p in present
           if p.cov.get("sorted_info_sha256")}

    # ⚠️ COVERAGE WAS ATTEMPTED BUT NOT COMPARED IS NOT THE SAME AS "FINE".
    # The driver writes a `pass*.coverage.env` for every pass whenever
    # `--coverage` is given, so its presence means the coverage lane was being
    # measured. If those files carry no digest — no .profraw produced, a merge
    # that failed, an empty report — then item 4 was NOT discharged, and without
    # this the run reports VALID with a real speedup and complete silence about
    # the one criterion the coverage lane exists to satisfy. That is the
    # repo's own #1 failure: a measurement that could not be taken reading as a
    # measurement that came out fine.
    #
    # Found by running it, not by reading it: with `--coverage` on a project
    # that produces no profiles, this file previously printed VALID and said
    # nothing at all.
    # ⚠️ THE DRIVER'S OWN DISPOSITION IS READ, not just "did three hashes turn
    # up". It writes status=no-profiles / merge-failed / export-failed /
    # empty-report precisely so this can tell "coverage was compared and agreed"
    # from "coverage could not be compared" — and a nonempty lcov report can
    # still carry zero coverage facts (`printf 'TN:\n'` hashes fine and has no
    # DA lines), which is why lines_total is required positive too.
    cov_attempted = [p for p in present if p.cov]

    def cov_usable(pas):
        if pas.cov.get("status") != "ok" or not pas.cov.get("sorted_info_sha256"):
            return False
        try:
            return int(pas.cov.get("profraw_count", 0)) > 0 and int(pas.cov.get("lines_total", 0)) > 0
        except ValueError:
            return False

    if cov_attempted and not all(cov_usable(p) for p in cov_attempted):
        why = sorted({p.cov.get("status", "unknown") or "unknown"
                      for p in cov_attempted if not cov_usable(p)})
        voids.append(
            f"#267 ACCEPTANCE ITEM 4 WAS NOT DISCHARGED: merged coverage could not be "
            f"compared. Coverage was attempted on this lane and "
            f"{sum(1 for p in cov_attempted if not cov_usable(p))} of {len(cov_attempted)} "
            f"passes produced no usable digest (`{', '.join(why)}`). Whatever the timing says, "
            f"this run does not show that widening leaves coverage unchanged — and that is the "
            f"one criterion this lane is blocked on, so the sample is not evidence FOR IT. "
            f"A measurement that could not be taken must not read as one that came out fine.")
        cov = {}

    if len(cov) == 3 and ser and par:
        ser_shas = {cov[p.index] for p in ser if p.index in cov}
        par_shas = {cov[p.index] for p in par if p.index in cov}
        serial_agree = len(ser_shas) == 1
        if not serial_agree:
            voids.append(
                "THE TWO SERIAL PASSES PRODUCED DIFFERENT MERGED COVERAGE. That is this suite's "
                "own run-to-run coverage nondeterminism, measured at UNCHANGED concurrency — a "
                "finding of its own, and worth its own issue. It is voided rather than noted "
                "because it means item 4 cannot be attributed to `--parallel` from this sample "
                "in either direction: with a moving baseline there is nothing to compare the "
                "parallel pass against.")
        elif par_shas != ser_shas:
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
        pass                       # already stamped NOT EVIDENCE at the top
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
        # Same class of knob as PARALLELISM_WITNESS_*, and it deserves the same
        # banner: a 2.35x was published over serial passes 20.5 % apart simply
        # by passing --tolerance-pct 100, with the widening visible only as a
        # number beside a green tick.
        if args.tolerance_pct > DEFAULT_TOLERANCE_PCT:
            out.append(f"> ⚠️ **The A-vs-A' tolerance was WIDENED to {args.tolerance_pct:.1f} % "
                       f"from the shipped {DEFAULT_TOLERANCE_PCT:.1f} %.** The check that makes "
                       f"an A-B-A worth running was applied at a band this design does not stand "
                       f"behind, so this sample is not comparable with one judged at the default.")
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
    # ⚠️ FOUR WITNESSES, EACH CARRYING A USABLE MEASUREMENT — not "at least two".
    # A hostile review certified VALID with only witness0 and witness1 present
    # (they bracket pass 1 and say nothing about the parallel pass or the final
    # serial one) and again with four `status=ok` witnesses carrying no
    # measurements at all, because the parse was wrapped in a bare except. That
    # is an opt-in tested against the easy state rather than against its
    # adopters: the design is four witnesses bracketing three passes.
    def usable(w):
        if w.get("status") not in ("ok", "no-procfs"):
            return False
        try:
            return all(math.isfinite(float(w[k])) and float(w[k]) > 0
                       for k in ("calib_1proc_s", "calib_nproc_s"))
        except (KeyError, ValueError):
            return False

    wit_ok = [w for w in wit if usable(w)]
    if len(wit_ok) < 4:
        bad = [f"witness{i}" for i, w in enumerate(wit) if not usable(w)]
        voids.append(
            f"only {len(wit_ok)} of 4 machine witnesses carry a usable observation "
            f"(unusable: {', '.join(bad)}). The design is four witnesses BRACKETING three "
            f"passes — two of them bracket only the first pass and say nothing about the "
            f"parallel one, which is the leg the whole experiment turns on. An absent or "
            f"unreadable witness degrades a sample toward VOID; it never certifies one.")
    else:
        try:
            # ⚠️ THE WITNESS INDEX TRAVELS WITH THE VALUE. A `no-procfs` witness
            # stays in wit_ok (deliberately — it is the normal Windows state)
            # but carries no counter, so filtering it out of a positionally
            # labelled list shifted every later interval by one: a rise during
            # pass 3, a SERIAL pass, was reported as "pass 2 (parallel)" and
            # then annotated with the run_paired bypass diagnosis — the precise
            # wrong conclusion, in the one place this code exists to get right.
            indexed = [(i, int(w["steal_ticks"])) for i, w in enumerate(wit)
                       if usable(w) and w.get("steal_ticks", "").strip()]
            steals = [v for _, v in indexed]
            if not steals:
                out.append("**Machine witness:** steal counter unavailable on this platform — "
                           "calibration drift is the only machine observation here.")
            else:
                # ⚠️ ATTRIBUTED PER PASS, not max-minus-min across the run, and
                # the difference is the whole point.
                #
                # `tools/bench_compare.py:run_paired` works through the bypass
                # this design would otherwise have — Codex found it there, on
                # the repo's other paired instrument:
                #
                #     A1 = 160, B measured during a 20 % throttle = 120, A2 = 160
                #     A-vs-A' = 0 %  ->  "the machine held still"
                #     ...and the B leg is wrong by 25 %.
                #
                # Two serial passes agreeing says NOTHING about a transient
                # confined to the parallel pass between them. Its answer was to
                # measure the base twice as well (A-B-A-B). This apparatus does
                # not, because four passes do not fit the job budget on the lane
                # that most needs measuring (see the residual note below) — so
                # the steal counter is differenced INTERVAL BY INTERVAL, which
                # attributes a hypervisor-caused transient to the pass it landed
                # on. A rise across the B interval alone is exactly the bypass,
                # and is now named as such rather than averaged into a total.
                labels = ["pass 1 (serial)", "pass 2 (parallel)", "pass 3 (serial)"]
                intervals = [(labels[i], hi - lo)
                             for (i, lo), (j, hi) in zip(indexed, indexed[1:])
                             if j == i + 1 and i < len(labels)]
                if len(intervals) < 3:
                    out.append(f"> ⚠️ Steal could not be attributed to every pass — "
                               f"{3 - len(intervals)} interval(s) are not bracketed by two "
                               f"witnesses that both carry the counter, so a transient there "
                               f"would go unnamed.")
                hot = [(w, d) for w, d in intervals if d > args.steal_tolerance_ticks]
                if hot:
                    # ⚠️ THE BYPASS SENTENCE IS CONDITIONAL. Attached to a rise
                    # in a SERIAL pass it sends the reader to the parallel
                    # pass's LastTest.log to find nothing — a correct VOID with
                    # a diagnosis pointing at the wrong leg. The bypass is
                    # specifically about a transient the two serial passes
                    # cannot see, so it applies only when the parallel interval
                    # is the one that moved.
                    bypass = ""
                    if any("parallel" in w for w, _ in hot):
                        bypass = (" ⚠️ This rise is in the PARALLEL pass — the bypass that "
                                  "A-vs-A' agreement cannot see, because the two serial passes "
                                  "would still agree perfectly.")
                    voids.append(
                        "/proc/stat STEAL ROSE DURING " +
                        ", ".join(f"{w} (+{d} ticks)" for w, d in hot) +
                        f" against a tolerance of {args.steal_tolerance_ticks}. Another tenant "
                        f"was on the physical host while that pass ran; `linux-clang-debug`'s "
                        f"trusted sample had steal 0 throughout." + bypass)
                else:
                    out.append("**Machine witness:** steal per interval "
                               + ", ".join(f"{w.split()[1]}=+{d}" for w, d in intervals)
                               + " ✅")
        except (ValueError, KeyError):
            pass
        # A witness run with fewer iterations or repeats than the shipped
        # defaults is a WEAKER observation, and must not pass itself off as a
        # full one — the knobs exist for the seam harness, where the absolute
        # figure is irrelevant, and they would silently degrade a campaign.
        if any(w.get("procs_source") == "UNDETECTED-fallback" for w in wit_ok):
            out.append("> ⚠️ **The witness could not count this machine's CPUs** and fell back to "
                       "a default for its N-proc arm. That figure is not a fact about this "
                       "machine; read the N-proc calibration below as unattributed.")
        # ⚠️ ABSENCE IS AN ANOMALY, NOT A DEFAULT. `.get(key, DEFAULT)` assumed
        # the best about a report that does not say how it was taken — the
        # opposite of what machine-witness.py's own docstring says. Stripping
        # the two keys removed the WEAKENED banner entirely from a genuinely
        # weak witness, which is reachable from any archived artifact written by
        # an older version, and archived artifacts are exactly what an operator
        # re-judges.
        DEFAULT_ITERS, DEFAULT_REPEATS = 3_000_000, 5
        undeclared = [f"witness{i}" for i, w in enumerate(wit)
                      if w in wit_ok and ("iters" not in w or "repeats" not in w)]
        if undeclared:
            out.append(f"> ⚠️ **{', '.join(undeclared)} does not record how it was taken** "
                       f"(no `iters`/`repeats`), so it cannot be read as a full observation. "
                       f"An older witness format, most likely — re-run rather than assume.")
        try:
            got_i = min(int(w["iters"]) for w in wit_ok if "iters" in w)
            got_r = min(int(w["repeats"]) for w in wit_ok if "repeats" in w)
            if got_i < DEFAULT_ITERS or got_r < DEFAULT_REPEATS:
                out.append(f"> ⚠️ **The machine witness ran WEAKENED** (`iters={got_i}` / "
                           f"`repeats={got_r}` against defaults {DEFAULT_ITERS} / "
                           f"{DEFAULT_REPEATS}). Those knobs exist for the seam self-test, "
                           f"where the calibration's magnitude is irrelevant. On a campaign "
                           f"they make the drift check less sensitive than it reports.")
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
    # ⚠️ A RUN THIS FILE DECLARES "NOT EVIDENCE" MUST NOT EXIT 0. The subset
    # banner at the top used to be the only consequence, so a smoke run went
    # green and published a speedup for a sample expressly declared unusable —
    # and a green tick outlives a banner.
    if subset:
        voids.append(
            f"SUBSET RUN (`-R {subset}`): this exercised the apparatus, not the lane's suite, "
            f"so it cannot support an `execution.jobs` decision for any lane. That is what the "
            f"subset input is FOR — a cheap pre-flight — and it is voided rather than merely "
            f"annotated so the exit code says the same thing the banner does.")

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
        speedup = base / b.wall
        eff = speedup / b.jobs * 100.0 if b.jobs else 0.0
        sum_ser = sum(p.log["sum_s"] or 0 for p in ser) / len(ser)
        sum_par = b.log["sum_s"] or 0
        infl = (sum_par - sum_ser) / sum_ser * 100.0 if sum_ser else 0.0
        # ⚠️ THE MEMORY CLAUSE IS WITHHELD, NOT ZEROED, WHEN THERE IS NO READING.
        # `peak_bytes` is EMPTY on the Windows lanes — there is no /proc to
        # sample — and `int("" or 0)` is 0, so a formatted clause would print
        # `peak RSS +0.0 % (0.00 GiB -> 0.00 GiB)` beside a real speedup on a
        # VALID sample.  A fabricated zero next to genuine numbers is worse than
        # a gap: it reads as measured headroom, which is precisely the figure
        # #267 acceptance item 1 exists to require.
        peak_ser = max((int(p.peak.get("peak_bytes") or 0) for p in ser), default=0)
        peak_par = int(b.peak.get("peak_bytes") or 0)
        if peak_ser and peak_par:
            mem = (peak_par - peak_ser) / peak_ser * 100.0
            mem_clause = (f"peak RSS {mem:+.1f} % "
                          f"({gib(str(peak_ser))} -> {gib(str(peak_par))})")
        else:
            why = next((p.peak.get("status") for p in ser + [b]
                        if p.peak.get("status") not in ("ok", None)), "unavailable")
            mem_clause = f"peak RSS **NOT MEASURED** (`{why}`)"
        out.append(f"**`--parallel {b.jobs}` on `{preset}`: {speedup:.2f}x** "
                   f"({base:.1f} s serial -> {b.wall:.1f} s), parallel efficiency "
                   f"{eff:.0f} %, summed test time {infl:+.1f} %, {mem_clause}.")
        if not (peak_ser and peak_par):
            out.append("")
            out.append("> ⚠️ **#267 acceptance item 1 is NOT discharged by this sample.** The "
                       "timing half stands on its own; a widening proposed on it would be "
                       "proposed without the memory reading every other lane is held to. Say "
                       "so in the PR.")
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
