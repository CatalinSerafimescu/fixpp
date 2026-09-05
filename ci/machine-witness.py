#!/usr/bin/env python3
"""Witness that the MACHINE did not change under an A-B-A experiment (#267).

    ci/machine-witness.py --out <file> [--procs N] [--iters N]

WHY THIS EXISTS
===============

#267's whole difficulty is that this repo's CI lanes have a between-VM
wall-clock spread wide enough that an A/B run as two separate jobs measures VM
luck rather than the change (figures in `ci/ctest-parallelism-probe.md`).  The
answer already recorded in `ci/ctest-parallelism-probe.md` is to run both arms
in ONE job on ONE VM, serial-parallel-serial, and to **void the sample unless
the two serial passes agree**.

But A-vs-A' agreement is a FORCED-MISS ARM.  It fires when the machine drifted;
it cannot fire when the machine was *uniformly* wrong for all three passes — a
noisy neighbour present throughout, a throttled VM, a burst-credit CPU that was
slow from the start.  In that state A and A' agree beautifully and the middle
arm's speedup is measured against a floor that has nothing to do with
production.  So agreement is necessary and not sufficient, and this file is the
other half: an INDEPENDENT observation of the machine, taken between passes,
that does not depend on the suite at all.

That is not a new idea here — it is the design the one trustworthy A-B-A in the
probe document already used.  `linux-clang-debug` was settled by three passes
"with an independent machine witness between each": 1-proc calibration
2.73/2.85/2.82/2.94 s, 4-proc 5.21/5.16/5.11/5.08 s, `/proc/stat` steal 0
throughout.  That evidence is what makes its 2.10 x believable.  The apparatus
for it was ad hoc and was never committed, so the next lane could not be
measured the same way.  This is that apparatus, written down.

WHAT IT MEASURES
================

* **`calib_1proc_s`** — wall time of a fixed, deterministic, CPU-bound loop in
  ONE process.  Sensitive to per-core speed: throttling, a busy neighbour, a
  slower host generation.
* **`calib_nproc_s`** — the SAME loop run in N processes concurrently, timed to
  the last one finishing.  Sensitive to what the 1-proc figure cannot see:
  how much genuine parallel capacity the VM has.  A 4-vCPU runner that is
  really 2 physical cores with SMT reads roughly 2x the 1-proc time here, and
  that ratio is precisely the quantity a `--parallel` decision rests on.
* **`steal_ticks`** — cumulative CPU time stolen by the hypervisor, field 8 of
  `/proc/stat`'s aggregate line.  The verdict differences successive witnesses;
  a non-zero DELTA across a pass means another tenant was on the physical host
  during it, which is the noisy-neighbour case by definition.

WHY A PYTHON LOOP AND NOT A BENCHMARK
=====================================

The quantity wanted is "did this machine's throughput change between pass 1 and
pass 3", not "how fast is this machine in absolute terms".  For a DIFFERENCE, a
workload whose instruction count is identical every time is worth more than a
realistic one: a fixed-iteration integer loop performs byte-identical work on
every invocation, allocates nothing after warm-up, touches no disk, and cannot
be affected by anything the test suite left behind in the page cache.  An
absolute number from it is meaningless and is never used as one.

⚠️ IT MUST STAY CHEAP.  The witness runs between passes of a suite that already
takes 20-70 minutes per pass, four times per lane.  At the default `--iters` it
costs a few seconds per call.  Do not "improve" it into something that measures
memory bandwidth or I/O — a witness that takes minutes changes the experiment it
exists to observe.

FAIL-LOUD CONTRACT
==================

Like `ci/measure-peak-rss.py`, this never fails its caller: exit is always 0 and
every failure path writes an explicit `status=` the verdict renders as NOT
MEASURED.  A missing witness must degrade the sample toward VOID, never toward a
false VALID — so the verdict treats an absent or non-`ok` witness as "the
machine was not observed", not as "the machine was fine".

    status=ok            — both calibrations ran and /proc/stat was readable
    status=no-procfs     — /proc/stat unreadable, which is the NORMAL state on
                           the Windows lanes and not a fault there.  The
                           calibrations are still present and still usable; only
                           the steal half is missing, and the verdict says so
                           rather than treating the witness as absent
    status=calib-failed  — a calibration child could not be started or died
"""

from __future__ import annotations

import argparse
import multiprocessing
import os
import sys
import time

# One "unit" of work: a fixed-iteration integer loop.  Deterministic in the
# instructions executed, so two invocations differ only by the machine.
def burn(iters: int) -> int:
    acc = 0
    for i in range(iters):
        acc = (acc * 1103515245 + 12345) & 0x7FFFFFFF
    return acc


def time_one(iters: int, repeats: int) -> float:
    """BEST of `repeats` timings, not the mean.

    ⚠️ MEASURED, and it is the reason this is not a single timing.  Six
    back-to-back single timings of the 1-proc arm on a busy host spread
    **12.4 %** at 3M iterations and **14.7 %** at 12M — i.e. the spread does NOT
    shrink with a longer run, because it is contention, not sampling error.  A
    witness with that much noise would VOID honest samples at any tolerance
    tight enough to catch real drift, and an apparatus that cries wolf gets its
    tolerance widened until it detects nothing.

    The minimum is the right estimator for the question actually being asked.
    The witness asks "is this machine still capable of what it was capable of
    before" — and every source of error here (a neighbour, a scheduler hiccup, a
    frequency dip) only ever ADDS time.  The best observed run is therefore the
    closest estimate of the machine's capability, and it is stable in a way the
    mean is not.  The 4-proc arm spread 4.9-5.8 % on the same host, so it needs
    fewer repeats to say the same thing.
    """
    best = float("inf")
    for _ in range(repeats):
        t0 = time.monotonic()
        burn(iters)
        best = min(best, time.monotonic() - t0)
    return best


def time_many(iters: int, procs: int, repeats: int, single_arm_s: float) -> float:
    """Wall time until the LAST of `procs` concurrent burners finishes.

    PROCESSES, not threads, and not negotiable: CPython's GIL would serialise
    threads, and this number would then measure nothing about the VM's parallel
    capacity — which is the entire reason the N-proc arm exists.

    ⚠️ `multiprocessing` rather than `os.fork` so this runs on the Windows lanes
    too.  `windows-msvc-asan` is the matrix critical path and has no measurement
    of any kind; a witness that only works on Linux would leave the lane that
    most needs deciding as the one lane that cannot be.  The cost is that
    Windows uses the `spawn` start method, which pays interpreter startup per
    child.  That overhead is a CONSTANT of the platform, so it cancels in the
    first-vs-last DRIFT comparison this figure is used for — and it is never
    used as an absolute, on any platform.
    """
    best = float("inf")
    for _ in range(repeats):
        t0 = time.monotonic()
        workers = [multiprocessing.Process(target=burn, args=(iters,)) for _ in range(procs)]
        for w in workers:
            w.start()
        # ⚠️ BOUNDED, AND EVERY EXIT STATUS IS CHECKED. A hostile review replaced
        # `burn` with `os._exit(7)` and this function returned 0.011 s — a
        # "calibration" in which no work was done at all, recorded as
        # `status=ok`. A crashed, OOM-killed or signalled arm would therefore
        # read as an extraordinarily fast machine, and the verdict would compare
        # it against a real one. The unbounded `join()` was the other half: one
        # stuck child hung the whole campaign with no diagnostic.
        #
        # The budget is derived, not chosen: the single-process arm has already
        # been timed by the caller, and N processes on a machine with at least
        # one core cannot beat it — 10x that, floored, is loose enough never to
        # fire on a slow runner and tight enough that a wedged child is caught.
        deadline = time.monotonic() + max(30.0, single_arm_s * 10.0)
        failed = []
        for w in workers:
            w.join(timeout=max(0.0, deadline - time.monotonic()))
            if w.is_alive():
                failed.append(f"{w.name} still running after {deadline - t0:.0f}s")
                w.terminate()
                w.join(timeout=5.0)
            elif w.exitcode != 0:
                failed.append(f"{w.name} exited {w.exitcode}")
        if failed:
            raise RuntimeError("N-proc calibration children did not complete cleanly: "
                               + "; ".join(failed))
        best = min(best, time.monotonic() - t0)
    return best


def steal_ticks() -> int | None:
    """Field 8 of /proc/stat's aggregate `cpu` line — hypervisor-stolen time.

    Returned as the RAW CUMULATIVE counter, not a delta.  Differencing is the
    verdict's job because only it knows which two witnesses bracket a pass; a
    delta computed here would need this process to persist across the pass.
    """
    try:
        with open("/proc/stat", "r", encoding="ascii") as fh:
            for line in fh:
                if line.startswith("cpu "):
                    fields = line.split()
                    # cpu user nice system idle iowait irq softirq steal ...
                    return int(fields[8]) if len(fields) > 8 else 0
    except (OSError, ValueError, IndexError):
        return None
    return None


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--label", default="", help="free-text tag (e.g. `before-pass-1`)")
    # ⚠️ DETECTED HERE, NOT PASSED IN — and `procs_source` says which happened.
    # The driver used to compute this as
    # `getconf _NPROCESSORS_ONLN || echo 4`, which INVENTS a 4 when detection
    # fails and hands the machine observer a fabricated fact about the machine.
    # The verdict then prints it beside real figures ("4-proc 2.73 -> 2.94 s"),
    # which is the exact thing it refuses to do for peak RSS. An observer that
    # cannot count the CPUs must say so, not guess.
    ap.add_argument("--procs", type=int, default=0,
                    help="concurrency of the N-proc arm; 0 (default) detects it")
    ap.add_argument("--iters", type=int, default=3_000_000,
                    help="loop iterations per burner (~0.6-1 s on a CI runner)")
    ap.add_argument("--repeats", type=int, default=5,
                    help="timings per arm; the BEST is reported (see time_one)")
    args = ap.parse_args()

    # `repeats` is recorded alongside `iters` so the verdict can tell a full
    # observation from a weakened one — a report that does not say how it was
    # taken cannot be judged.
    if args.procs > 0:
        procs, procs_source = args.procs, "override"
    else:
        detected = os.cpu_count()
        procs, procs_source = (detected, "detected") if detected else (4, "UNDETECTED-fallback")

    # `repeats` is recorded alongside `iters` so the verdict can tell a full
    # observation from a weakened one — a report that does not say how it was
    # taken cannot be judged. `procs_source` is there for the same reason.
    fields: dict[str, object] = {"label": args.label, "procs": procs,
                                 "procs_source": procs_source,
                                 "iters": args.iters, "repeats": args.repeats}
    status = "ok"

    try:
        # Warm-up, discarded: the first call pays interpreter warm-up and page
        # faults that the timed calls must not carry.
        burn(args.iters // 10)
        one = time_one(args.iters, args.repeats)
        fields["calib_1proc_s"] = f"{one:.3f}"
        fields["calib_nproc_s"] = \
            f"{time_many(args.iters, procs, max(2, args.repeats // 2), one):.3f}"
    except (OSError, RuntimeError) as exc:
        status = "calib-failed"
        fields["error"] = repr(exc)

    ticks = steal_ticks()
    if ticks is None:
        # Not fatal and not a lie: the calibrations above may be perfectly good.
        # A distinct status is what lets the verdict say WHICH half is missing.
        status = "no-procfs" if status == "ok" else status
        fields["steal_ticks"] = ""
    else:
        fields["steal_ticks"] = ticks

    fields["status"] = status
    fields["mono_s"] = f"{time.monotonic():.3f}"

    tmp = f"{args.out}.tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        for key, value in fields.items():
            fh.write(f"{key}={value}\n")
    os.replace(tmp, args.out)

    # ⚠️ `no-procfs` IS NOT AN INCOMPLETE WITNESS — it is the NORMAL state on the
    # Windows lanes, where the calibrations run fine and only the steal counter
    # is unavailable. Warning "cannot be shown to have run on an unchanging
    # machine" there overstated it on every single Windows pass, which is how a
    # warning becomes one nobody reads. The verdict already treats a no-procfs
    # witness as usable and discloses the missing half itself.
    if status == "no-procfs":
        print(f"::notice::#267 machine witness: no /proc on this platform, so the steal "
              f"counter is unavailable. The calibrations ran; the verdict discloses which "
              f"half is missing.", file=sys.stderr)
    elif status != "ok":
        print(f"::warning::#267 machine witness incomplete (status={status}). The A-B-A "
              f"sample it brackets cannot be shown to have run on an unchanging machine.",
              file=sys.stderr)
    # ALWAYS 0 — see the fail-loud contract above.
    return 0


if __name__ == "__main__":
    sys.exit(main())
