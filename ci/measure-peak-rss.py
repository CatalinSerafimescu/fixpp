#!/usr/bin/env python3
"""Run a command and measure the PEAK CONCURRENT RSS of its whole process tree.

    ci/measure-peak-rss.py --out <file> [--log <file>] [--interval-ms N] -- <cmd> [args...]

#266 — WHY THIS EXISTS, AND WHAT IT REPLACES
============================================

`ci/ctest-parallelism-probe.md`'s acceptance criterion 4 asks for the peak memory
of a `ctest --parallel` run.  The instrument PR #245 installed read cgroup v2's
`memory.peak`.  It produced a reading on **0 of 8** post-merge runs: on a
GitHub-hosted runner the job sits in the ROOT cgroup, and `memory.peak` exists
only on non-root cgroup v2 nodes.  That is structural, not intermittent, so the
criterion could never close.  The probe's REFUSAL to emit a number it could not
read was correct and is preserved here — what changes is the source.

`/usr/bin/time -v` was considered and REJECTED as the primary source: its
`Maximum resident set size` comes from `getrusage(RUSAGE_CHILDREN)`, which
reports the largest SINGLE child's peak, not the SUM of concurrent children.
That is the wrong quantity for a parallelism question, and a plausible-looking
wrong quantity is worse than none.  The local sweep recorded in #229 makes the
size of the error concrete: `linux-clang-tsan`'s largest single process is
1.04 GiB, which naively projects to ~4 GiB at j=4; the measured concurrent total
is 2.53 GiB — the single-process basis is **37 % high**.

So this samples `/proc` and sums the RSS of every process in the tree, which is
the quantity criterion 4 is about, and the SAME quantity the #229 local sweep
measured — the two are directly comparable by construction.

WHAT THE NUMBER IS, PRECISELY
=============================

* **A sum of per-process RSS**, so shared pages (libc, the shared library under
  test, copy-on-write pages after fork) are counted once per process that maps
  them.  The figure therefore OVER-states true physical occupancy.  That is the
  safe direction for a headroom question and it is deliberate — a PSS-based
  figure would read lower and could talk us into a widening the machine cannot
  actually take.  Do not compare it against a PSS or cgroup figure and call the
  difference a regression.
* **RSS is resident memory, not address space.**  A sanitizer's shadow mapping
  is reserved, not resident, and correctly does not appear here.
* **Sampled, not exact.**  A peak lasting less than the sample interval can be
  missed.  The interval is 250 ms by default — the same one the #229 local sweep
  used.  Two repeat runs there agreed to 2.0 % on peak RSS with the peaks 8 s
  apart, i.e. the sampler resolves the real peak rather than catching transients.
* **Tree membership is by PARENT CHAIN**, rebuilt from `/proc/*/stat` on every
  sample, not by process group.  A test binary that calls `setpgid`/`setsid`
  (the codegen tests shell out to `fixpp-codegen`, and CTest itself creates
  process groups on some platforms) would silently drop out of a pgid-keyed
  walk, and a walk that silently under-counts is exactly the failure this whole
  issue is about.

WHY PYTHON AND NOT BASH
=======================

The sampler runs DURING the very wall-clock measurement #267 reads.  A bash
sampler re-forking `cat`/`awk` per process per sample would burn a measurable
share of a 4 vCPU runner and contaminate the number it exists to support.  This
reads `/proc` in-process: ~2-5 ms per sample against a 250 ms interval.

FAIL-LOUD CONTRACT
==================

The exit status is ALWAYS the wrapped command's, so an instrument failure can
never redden a lane and a test failure can never be swallowed.  Instead the
output file always carries an explicit `status=`:

    status=ok           — samples were taken and the peak is non-zero
    status=no-samples   — the command finished before any sample landed
    status=zero-peak    — samples were taken and every one summed to zero
                          (a broken /proc read; NOT a legitimate measurement)
    status=sampler-died — the sampler thread raised partway through, so the
                          figure covers an unstated fraction of the run
    status=start-failed — the command could not be executed at all

Anything other than `ok` is a NOT MEASURED disposition for the reader, and
`ci/peak-memory-report.sh` renders it as such.  There is no path on which this
script writes a number it did not measure, and no path on which it writes
nothing at all.
"""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
import threading
import time

PAGE_SIZE = os.sysconf("SC_PAGE_SIZE")


def read_proc_table() -> tuple[dict[int, int], dict[int, int]]:
    """Return (ppid_by_pid, rss_bytes_by_pid) for every readable process.

    Both maps are built from the SAME /proc walk so the parent chain and the
    sizes describe one instant as closely as /proc allows.  Processes that exit
    mid-walk are skipped rather than raising — a sampler that dies because a
    test finished would report the peak of the first few seconds.
    """
    ppid: dict[int, int] = {}
    rss: dict[int, int] = {}
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        pid = int(name)
        try:
            # `comm` (field 2) may contain spaces and parentheses, so split on
            # the LAST ')' rather than on whitespace — the classic /proc/stat
            # parsing trap.  Fields after it: state(3) ppid(4) pgrp(5) ...
            with open(f"/proc/{pid}/stat", "rb") as fh:
                stat = fh.read()
            after = stat[stat.rindex(b")") + 2 :].split()
            ppid[pid] = int(after[1])
            # statm field 2 is resident set size in PAGES.  Preferred over
            # status:VmRSS because it is a single short line of integers — no
            # unit parsing, and cheaper to read.
            with open(f"/proc/{pid}/statm", "rb") as fh:
                rss[pid] = int(fh.read().split()[1]) * PAGE_SIZE
        except (OSError, ValueError, IndexError):
            continue
    return ppid, rss


def tree_rss(root: int, ppid: dict[int, int], rss: dict[int, int]) -> tuple[int, int, int]:
    """Sum RSS over `root` and all its descendants.

    Returns (total_bytes, max_single_bytes, process_count).
    """
    children: dict[int, list[int]] = {}
    for pid, parent in ppid.items():
        children.setdefault(parent, []).append(pid)

    total = 0
    largest = 0
    count = 0
    stack = [root]
    seen = set()
    while stack:
        pid = stack.pop()
        if pid in seen:
            continue
        seen.add(pid)
        size = rss.get(pid)
        if size is not None:
            total += size
            largest = max(largest, size)
            count += 1
        stack.extend(children.get(pid, ()))
    return total, largest, count


class Sampler(threading.Thread):
    def __init__(self, root: int, interval_s: float) -> None:
        super().__init__(daemon=True)
        self.root = root
        self.interval_s = interval_s
        self.stop_event = threading.Event()
        self.samples = 0
        self.peak = 0
        self.peak_largest = 0
        self.peak_procs = 0
        self.peak_at = 0.0
        self.started_at = time.monotonic()
        # A sampler thread that dies partway leaves `samples` and `peak` looking
        # perfectly healthy — the peak of however much of the run it saw. That is
        # a plausible number over an unstated workload, which is the failure this
        # whole issue is about, so the death is recorded and demoted to a
        # non-`ok` status rather than left to look like a measurement.
        self.error: BaseException | None = None

    def run(self) -> None:
        try:
            while True:
                ppid, rss = read_proc_table()
                total, largest, count = tree_rss(self.root, ppid, rss)
                self.samples += 1
                if total > self.peak:
                    self.peak = total
                    self.peak_largest = largest
                    self.peak_procs = count
                    self.peak_at = time.monotonic() - self.started_at
                if self.stop_event.wait(self.interval_s):
                    return
        except BaseException as exc:  # noqa: BLE001 — deliberately total
            self.error = exc


def mem_total_bytes() -> int:
    try:
        with open("/proc/meminfo") as fh:
            for line in fh:
                if line.startswith("MemTotal:"):
                    return int(line.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    return 0


def write_out(path: str, fields: dict[str, object]) -> None:
    """Write the key=value report.

    Written via a temp file + rename so a reader can never observe a half-written
    report and parse a truncated number as a measurement.
    """
    tmp = path + ".tmp"
    with open(tmp, "w") as fh:
        for key, value in fields.items():
            fh.write(f"{key}={value}\n")
    os.replace(tmp, path)


def main() -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--out", required=True, help="key=value report path")
    parser.add_argument("--log", help="tee the command's combined output here")
    parser.add_argument("--label", default="", help="free-text tag (e.g. the preset)")
    parser.add_argument("--interval-ms", type=int, default=250)
    parser.add_argument("cmd", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    cmd = args.cmd
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        parser.error("no command given; use: --out F -- <cmd> [args...]")

    base = {
        "label": args.label,
        "interval_ms": args.interval_ms,
        "mem_total_bytes": mem_total_bytes(),
        "command": shlex.join(cmd),
    }

    log_fh = open(args.log, "wb") if args.log else None
    try:
        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE if log_fh else None,
                stderr=subprocess.STDOUT if log_fh else None,
            )
        except OSError as exc:
            write_out(args.out, {**base, "status": "start-failed", "error": exc, "cmd_status": 127})
            print(
                f"::error::#266 peak-RSS instrument could not start the wrapped command "
                f"({shlex.join(cmd)}): {exc}. No measurement was taken.",
                file=sys.stderr,
            )
            return 127

        sampler = Sampler(proc.pid, args.interval_ms / 1000.0)
        sampler.start()

        if log_fh is not None:
            assert proc.stdout is not None
            # Byte-for-byte passthrough: the reporting step parses ctest's own
            # timing lines out of this log, so it must not be reformatted.
            for chunk in iter(lambda: proc.stdout.readline(), b""):
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                log_fh.write(chunk)
            proc.stdout.close()

        status = proc.wait()
        sampler.stop_event.set()
        sampler.join(timeout=5.0)
    finally:
        if log_fh is not None:
            log_fh.close()

    if sampler.error is not None:
        disposition = "sampler-died"
    elif sampler.samples == 0:
        disposition = "no-samples"
    elif sampler.peak == 0:
        disposition = "zero-peak"
    else:
        disposition = "ok"

    write_out(
        args.out,
        {
            **base,
            "status": disposition,
            "cmd_status": status,
            "samples": sampler.samples,
            "peak_bytes": sampler.peak,
            "peak_max_single_bytes": sampler.peak_largest,
            "peak_procs": sampler.peak_procs,
            "peak_at_s": f"{sampler.peak_at:.1f}",
            "elapsed_s": f"{time.monotonic() - sampler.started_at:.1f}",
            "sampler_error": "" if sampler.error is None else repr(sampler.error),
        },
    )

    if disposition != "ok":
        # A warning, not an error: the wrapped command's result is the lane's
        # signal and must not be overwritten by an instrument fault.  The
        # NOT MEASURED rendering is ci/peak-memory-report.sh's job — this line
        # exists so the fault is attributable at the step that took it.
        print(
            f"::warning::#266 peak-RSS instrument took no usable measurement "
            f"(status={disposition}, samples={sampler.samples}, peak={sampler.peak}). "
            f"The ctest --parallel memory criterion is UNMEASURED on this run; "
            f"do not read it as evidence.",
            file=sys.stderr,
        )

    return status


if __name__ == "__main__":
    sys.exit(main())
