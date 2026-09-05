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

WINDOWS — SAME QUANTITY, DIFFERENT MEMBERSHIP MECHANISM
=======================================================

Windows has no `/proc`, so this lane produced `status=no-procfs-platform` and
`windows-msvc-asan` — the repo's LONGEST job, and an ASan lane, i.e. exactly
where the memory cost of a widening concentrates — was the one lane that could
not be decided on the memory axis at all.  The Linux ASan lanes rose +45 % and
+53 % peak RSS from j=1 to j=4, so "probably fine" is not a reading.

The quantity is UNCHANGED: a sum of per-process resident memory across the
concurrent tree, so the Windows and Linux figures are comparable by
construction.  Only two things differ.

**Membership is by JOB OBJECT, not by parent chain.**  This process assigns
ITSELF to a fresh job object before spawning anything; on Windows a child
inherits its creator's job unless it explicitly breaks away, so every
descendant is a member with no race between `CreateProcess` and the
assignment, and `QueryInformationJobObject(JobObjectBasicProcessIdList)` then
returns the membership EXACTLY.  The parent-chain walk the Linux path uses was
REJECTED here: Windows reuses PIDs aggressively and a dead parent's PID is not
cleared from `th32ParentProcessID`, so a chain walk can adopt an unrelated
process tree — a silent OVER-count that reads as a plausible number.  The
sampler's own PID is the one member excluded from the sum, which is exact
rather than approximate.

**The size is `WorkingSetSize`, NEVER `PeakWorkingSetSize`.**  They are adjacent
fields of the same `PROCESS_MEMORY_COUNTERS`, and summing the per-process PEAKS
would be sum-of-peaks where the question asks for peak-of-sums — the identical
error that got `/usr/bin/time -v` rejected above, measured 37 % high on this
repo's own tsan lane, reintroduced by one word and still looking plausible.

Two Windows-specific ways to UNDER-count, both of which are dispositioned
rather than summed over:

* `QueryInformationJobObject` fills what fits and reports `ERROR_MORE_DATA`
  when the buffer is short, leaving a TRUNCATED list that is structurally
  indistinguishable from a small job.  `NumberOfAssignedProcesses >
  NumberOfProcessIdsInList` is the only tell; the buffer is grown and retried,
  and a list still short after that marks the sample degraded.
* `OpenProcess` denied on a member is an under-count, not an exited process.
  It is counted separately from the benign "PID gone between enumeration and
  open" case, which is the same transient the Linux walk skips.

If either happens at any sample the run reports `status=partial-enumeration`,
which the reader treats as NOT MEASURED — the same disposition as no reading at
all, because a number that silently omits members is worse than none.

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
import pathlib
import shlex
import subprocess
import sys
import threading
import time

IS_WINDOWS = sys.platform == "win32"

# ⚠️ MODULE SCOPE, AND `os.sysconf` DOES NOT EXIST ON WINDOWS. Left unguarded
# this raised at IMPORT — before argparse, before any `--out` file could be
# written — so the driver would see a non-zero exit with no report and no
# disposition, which is the one outcome the fail-loud contract below forbids.
PAGE_SIZE = 0 if IS_WINDOWS else os.sysconf("SC_PAGE_SIZE")


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


# ── Windows backend ──────────────────────────────────────────────────────────
#
# See "WINDOWS — SAME QUANTITY, DIFFERENT MEMBERSHIP MECHANISM" in the header
# for why membership is a job object and why the size is `WorkingSetSize`.

_JobObjectBasicProcessIdList = 3
_PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
_PROCESS_VM_READ = 0x0010
_ERROR_MORE_DATA = 234
_ERROR_ACCESS_DENIED = 5


class WindowsJobTracker:
    """Membership by job object; size by per-process WorkingSetSize.

    `degraded` latches True on a truncated PID list or an access-denied member —
    both are UNDER-counts, and an under-count that is summed anyway is the
    plausible-wrong-number failure this whole instrument exists to refuse.
    """

    def __init__(self) -> None:
        import ctypes
        from ctypes import wintypes

        self._ctypes = ctypes
        self.degraded = False
        self.denied = 0
        self.self_pid = os.getpid()
        self._capacity = 256
        self._idlist_types: dict[int, type] = {}

        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)

        class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD),
                ("PageFaultCount", wintypes.DWORD),
                # ⚠️ Peak* fields are present and are NOT what this reads.
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        self._PMC = PROCESS_MEMORY_COUNTERS

        k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        k32.CreateJobObjectW.restype = wintypes.HANDLE
        k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        k32.AssignProcessToJobObject.restype = wintypes.BOOL
        k32.GetCurrentProcess.restype = wintypes.HANDLE
        k32.QueryInformationJobObject.argtypes = [
            wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
            wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
        k32.QueryInformationJobObject.restype = wintypes.BOOL
        k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        k32.OpenProcess.restype = wintypes.HANDLE
        k32.CloseHandle.argtypes = [wintypes.HANDLE]
        k32.CloseHandle.restype = wintypes.BOOL
        psapi.GetProcessMemoryInfo.argtypes = [
            wintypes.HANDLE, ctypes.POINTER(PROCESS_MEMORY_COUNTERS), wintypes.DWORD]
        psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
        self._k32 = k32
        self._psapi = psapi

        # ⚠️ NO `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. The default is what is
        # wanted: this handle is closed when the sampler exits, and on a
        # kill-on-close job that would take the wrapped command's whole process
        # tree with it. The job is used ONLY as a membership set.
        self.handle = k32.CreateJobObjectW(None, None)
        if not self.handle:
            raise OSError(ctypes.get_last_error(), "CreateJobObjectW failed")
        # Assign OURSELVES, not the child: a child inherits its creator's job,
        # so every descendant is a member with no window between CreateProcess
        # and the assignment in which a grandchild could escape.
        if not k32.AssignProcessToJobObject(self.handle, k32.GetCurrentProcess()):
            raise OSError(ctypes.get_last_error(), "AssignProcessToJobObject failed")

    def _pids(self) -> list[int] | None:
        """Job membership, or None if the list could not be read in full."""
        ctypes = self._ctypes
        from ctypes import wintypes

        for _ in range(4):
            n = self._capacity
            # ⚠️ CACHED BY CAPACITY, not rebuilt per call. This runs once per
            # SAMPLE — ~11 000 times over a 46-minute ctest run — and defining a
            # ctypes.Structure subclass costs ~50x instantiating one, for a type
            # that is identical every time (the capacity only moves on the rare
            # retry). Small in absolute terms, but this is the file whose header
            # says the sampler must not contaminate the number it exists to
            # support, so it does not get to spend it on nothing.
            IdList = self._idlist_types.get(n)
            if IdList is None:
                class IdList(ctypes.Structure):  # noqa: F811
                    _fields_ = [
                        ("NumberOfAssignedProcesses", wintypes.DWORD),
                        ("NumberOfProcessIdsInList", wintypes.DWORD),
                        ("ProcessIdList", ctypes.c_size_t * n),
                    ]

                self._idlist_types[n] = IdList

            buf = IdList()
            ret = wintypes.DWORD(0)
            ok = self._k32.QueryInformationJobObject(
                self.handle, _JobObjectBasicProcessIdList,
                ctypes.byref(buf), ctypes.sizeof(buf), ctypes.byref(ret))
            assigned = buf.NumberOfAssignedProcesses
            listed = buf.NumberOfProcessIdsInList
            # Both tells, because `ok` alone is not one: the call can succeed
            # having written a SHORT list, and a short list is structurally
            # indistinguishable from a small job.
            if ok and assigned <= listed:
                return [int(buf.ProcessIdList[i]) for i in range(listed)]
            err = ctypes.get_last_error()
            if not ok and err != _ERROR_MORE_DATA:
                return None
            self._capacity = max(self._capacity * 2, int(assigned) * 2 + 16)
        return None

    def _working_set(self, pid: int) -> int | None:
        """WorkingSetSize for one member, or None if it could not be read.

        Returns 0 for the benign case — the process exited between enumeration
        and open, the same transient the Linux walk skips. `None` is reserved
        for ACCESS DENIED, which is a live member we failed to measure.
        """
        ctypes = self._ctypes
        for access in (_PROCESS_QUERY_LIMITED_INFORMATION | _PROCESS_VM_READ,
                       _PROCESS_QUERY_LIMITED_INFORMATION):
            h = self._k32.OpenProcess(access, False, pid)
            if h:
                try:
                    pmc = self._PMC()
                    pmc.cb = ctypes.sizeof(pmc)
                    if self._psapi.GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb):
                        return int(pmc.WorkingSetSize)
                    return None
                finally:
                    self._k32.CloseHandle(h)
            if ctypes.get_last_error() != _ERROR_ACCESS_DENIED:
                return 0  # gone between enumeration and open
        return None

    def close(self) -> None:
        """Release the job handle. Safe because the job has no kill-on-close
        limit — see the note in __init__; closing it drops the membership set
        and leaves every member running."""
        if self.handle:
            self._k32.CloseHandle(self.handle)
            self.handle = None

    def snapshot(self) -> tuple[int, int, int]:
        """(total_bytes, max_single_bytes, process_count) over the job."""
        pids = self._pids()
        if pids is None:
            self.degraded = True
            return 0, 0, 0
        total = largest = count = 0
        for pid in pids:
            if pid == self.self_pid:
                continue
            size = self._working_set(pid)
            if size is None:
                self.degraded = True
                self.denied += 1
                continue
            if size:
                total += size
                largest = max(largest, size)
                count += 1
        return total, largest, count


class Sampler(threading.Thread):
    """Platform-independent peak tracking over a platform-specific snapshot.

    `snapshot()` returns (total_bytes, max_single_bytes, process_count) for the
    tree at one instant. Everything below — the peak, the dispositions, the
    report — is identical on both platforms because only the membership walk
    differs, which is what makes the two figures comparable.
    """

    def __init__(self, snapshot, interval_s: float) -> None:
        super().__init__(daemon=True)
        self.snapshot = snapshot
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
                total, largest, count = self.snapshot()
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
    if IS_WINDOWS:
        import ctypes
        from ctypes import wintypes

        class MEMORYSTATUSEX(ctypes.Structure):
            _fields_ = [("dwLength", wintypes.DWORD),
                        ("dwMemoryLoad", wintypes.DWORD),
                        ("ullTotalPhys", ctypes.c_ulonglong),
                        ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong),
                        ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong),
                        ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]

        st = MEMORYSTATUSEX()
        st.dwLength = ctypes.sizeof(st)
        if ctypes.WinDLL("kernel32").GlobalMemoryStatusEx(ctypes.byref(st)):
            return int(st.ullTotalPhys)
        return 0
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


# ── Self-test ────────────────────────────────────────────────────────────────
#
# ⚠️ THIS EXISTS BECAUSE THE WINDOWS PATH HAS NO OTHER ORACLE. It cannot be run
# on the machine it is written on, and the failure mode it must exclude is this
# repo's most recurring one: an instrument that reports a plausible number
# because it could not report anything else.  A size band ALONE does not exclude
# it — one MSVC ASan test binary is hundreds of MiB, so a walk that enumerates
# only the root process still clears any band generous enough to be robust.
#
# So the discriminator is `peak_procs`, and the band is the second arm, not the
# first:
#
#   1. status=ok                      — a reading was taken at all
#   2. peak_procs == children + 1     — the walk found the WHOLE tree; a
#                                       root-only walk reports 1 and reddens
#                                       here no matter how large the root is
#   3. peak_bytes >= 0.8 x children x MiB
#   4. peak_max_single_bytes < peak_bytes
#                                     — the figure is a SUM over concurrent
#                                       members, not one member's peak, which
#                                       is the /usr/bin/time -v error
#   5. the --log carries the child's marker
#                                     — the tee path. On Windows the `--no-peak`
#                                       branch used to own the log via a shell
#                                       redirect; routing it through this
#                                       wrapper is new there, and the verdict
#                                       parses ctest's timing lines out of it,
#                                       so an empty or mangled log would void
#                                       the very lane this instrument unblocks.
#
# The tree is deliberately two levels deep (wrapper -> tree -> leaves): a
# one-level tree cannot distinguish "walks descendants" from "walks the direct
# children of the root".
#
# ⚠️ WHAT THIS SELF-TEST DOES **NOT** DISCRIMINATE: `WorkingSetSize` from
# `PeakWorkingSetSize`. Every process here only GROWS, so a process's current
# working set and its peak coincide and both field choices produce the same
# number. Substituting the wrong field is therefore invisible to arm 3 and to
# arm 4, and no arm below should be read as covering it. An arm that tried
# would have to make a leaf SHRINK, and Windows trims a working set lazily, so
# it would be measuring the memory manager's mood rather than this code. The
# protection there is structural — one named field access, commented at the
# point of use — and this note exists so the gap is not mistaken for coverage.
#
# What the arms DO cover is the AGGREGATION error, which is the one that got
# `/usr/bin/time -v` rejected: arm 4 reddens on max-instead-of-sum (verified by
# mutation on both platforms), because one leaf's figure cannot reach the floor
# that four concurrent leaves clear.

_SELF_TEST_MARKER = "SELFTEST-TREE-READY"


def _self_test_leaf(mib: int, hold_s: float) -> int:
    # bytearray is zero-filled, and a zero page may never become resident, so
    # the pages are TOUCHED. An untouched allocation is exactly the thing RSS
    # correctly refuses to count, and the self-test would then measure nothing.
    buf = bytearray(mib << 20)
    for off in range(0, len(buf), 4096):
        buf[off] = 1
    sys.stdout.write("leaf ready\n")
    sys.stdout.flush()
    time.sleep(hold_s)
    # `buf` is held to here by ordinary scoping — the pages must still be
    # resident when the sampler looks, which is the whole point of this process.
    del buf
    return 0


def _self_test_tree(children: int, mib: int, hold_s: float) -> int:
    me = os.path.abspath(__file__)
    procs = [subprocess.Popen([sys.executable, me, "--self-test-leaf",
                               "--mib", str(mib), "--hold-s", str(hold_s)])
             for _ in range(children)]
    print(f"{_SELF_TEST_MARKER} {children}", flush=True)
    return max((p.wait() for p in procs), default=0)


def _self_test(children: int, mib: int) -> int:
    import tempfile

    hold_s = 6.0
    me = os.path.abspath(__file__)
    work = tempfile.mkdtemp(prefix="peak-rss-selftest-")
    out = os.path.join(work, "peak.env")
    log = os.path.join(work, "cmd.log")

    rc = subprocess.call([sys.executable, me, "--out", out, "--log", log,
                          "--label", "self-test", "--interval-ms", "100", "--",
                          sys.executable, me, "--self-test-tree",
                          "--children", str(children), "--mib", str(mib),
                          "--hold-s", str(hold_s)])

    report: dict[str, str] = {}
    try:
        with open(out) as fh:
            for line in fh:
                key, _, value = line.rstrip("\n").partition("=")
                report[key] = value
    except OSError as exc:
        print(f"FAIL: no report at {out}: {exc}", file=sys.stderr)
        return 1

    print(f"-- wrapped command exit {rc}")
    for key in ("status", "samples", "peak_bytes", "peak_max_single_bytes",
                "peak_procs", "denied_procs", "job_error", "sampler_error",
                "mem_total_bytes"):
        print(f"-- {key}={report.get(key, '<absent>')}")

    want_procs = children + 1
    floor = int(0.8 * children * mib) << 20
    peak = int(report.get("peak_bytes") or 0)
    largest = int(report.get("peak_max_single_bytes") or 0)
    try:
        log_text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        log_text = ""
        print(f"-- log unreadable: {exc}")

    failures = []
    if rc != 0:
        failures.append(f"wrapped command exited {rc}, expected 0")
    if report.get("status") != "ok":
        failures.append(f"status={report.get('status')!r}, expected 'ok'")
    if report.get("peak_procs") != str(want_procs):
        failures.append(
            f"peak_procs={report.get('peak_procs')!r}, expected {want_procs} "
            f"(1 tree + {children} leaves) — the walk did not find the whole tree")
    if peak < floor:
        failures.append(f"peak_bytes={peak} below the {floor} floor "
                        f"(0.8 x {children} x {mib} MiB)")
    if largest >= peak:
        failures.append(f"peak_max_single_bytes={largest} is not below "
                        f"peak_bytes={peak} — the figure is not a sum over "
                        f"concurrent members")
    if _SELF_TEST_MARKER not in log_text:
        failures.append(f"--log ({log}) does not carry {_SELF_TEST_MARKER!r} "
                        f"({len(log_text)} chars) — the tee path is broken, and "
                        f"the verdict parses ctest's timings out of this file")

    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        print(f"-- artifacts kept in {work}", file=sys.stderr)
        return 1
    print(f"PASS: peak {peak / (1 << 30):.2f} GiB over {want_procs} processes, "
          f"largest single {largest / (1 << 20):.0f} MiB")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--out", help="key=value report path")
    parser.add_argument("--log", help="tee the command's combined output here")
    parser.add_argument("--label", default="", help="free-text tag (e.g. the preset)")
    parser.add_argument("--interval-ms", type=int, default=250)
    # One EXCLUSIVE choice, not three independent booleans: dispatched by an
    # if-chain they would let `--self-test --self-test-leaf` through, with the
    # ordering silently picking a winner. argparse rejects it instead.
    role = parser.add_mutually_exclusive_group()
    role.add_argument("--self-test", action="store_true",
                      help="measure a tree of known size and check the reading")
    role.add_argument("--self-test-tree", action="store_true", help=argparse.SUPPRESS)
    role.add_argument("--self-test-leaf", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--children", type=int, default=4)
    parser.add_argument("--mib", type=int, default=64)
    parser.add_argument("--hold-s", type=float, default=6.0)
    parser.add_argument("cmd", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    if args.self_test_leaf:
        return _self_test_leaf(args.mib, args.hold_s)
    if args.self_test_tree:
        return _self_test_tree(args.children, args.mib, args.hold_s)
    if args.self_test:
        return _self_test(args.children, args.mib)
    if not args.out:
        parser.error("--out is required (or use --self-test)")

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

    # ⚠️ BEFORE `Popen`, and it is the ORDER that makes the membership exact:
    # a child inherits its creator's job, so self-assigning first leaves no
    # window in which a grandchild is spawned outside the set. Assigning the
    # CHILD afterwards would leave exactly that window.
    tracker = None
    job_error = ""
    if IS_WINDOWS:
        try:
            tracker = WindowsJobTracker()
        except Exception as exc:  # noqa: BLE001 — any failure means NOT MEASURED
            job_error = repr(exc)

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

        if tracker is not None:
            snapshot = tracker.snapshot
        elif IS_WINDOWS:
            snapshot = None
        else:
            root = proc.pid
            snapshot = lambda: tree_rss(root, *read_proc_table())  # noqa: E731

        sampler = None
        if snapshot is not None:
            sampler = Sampler(snapshot, args.interval_ms / 1000.0)
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
        if sampler is not None:
            sampler.stop_event.set()
            sampler.join(timeout=5.0)
    finally:
        if log_fh is not None:
            log_fh.close()
        if tracker is not None:
            tracker.close()

    if sampler is None:
        disposition = "job-unavailable"
    elif sampler.error is not None:
        disposition = "sampler-died"
    elif sampler.samples == 0:
        disposition = "no-samples"
    elif tracker is not None and tracker.degraded:
        # A truncated PID list or an access-denied member. Both UNDER-count, and
        # the sum still looks like a measurement — so it is dispositioned rather
        # than reported.
        disposition = "partial-enumeration"
    elif sampler.peak == 0:
        disposition = "zero-peak"
    else:
        disposition = "ok"

    # Every numeric field is BLANK rather than 0 when no sampler ran: a zero
    # here is a legitimate reading elsewhere (`status=zero-peak`), so the two
    # must not render alike. Written as one branch rather than a guard per
    # field so a field added to one side is visibly missing from the other.
    if sampler is None:
        measured: dict[str, object] = {
            "samples": 0, "peak_bytes": "", "peak_max_single_bytes": "",
            "peak_procs": "", "peak_at_s": "", "elapsed_s": "", "sampler_error": "",
        }
    else:
        measured = {
            "samples": sampler.samples,
            "peak_bytes": sampler.peak,
            "peak_max_single_bytes": sampler.peak_largest,
            "peak_procs": sampler.peak_procs,
            "peak_at_s": f"{sampler.peak_at:.1f}",
            "elapsed_s": f"{time.monotonic() - sampler.started_at:.1f}",
            "sampler_error": "" if sampler.error is None else repr(sampler.error),
        }

    write_out(
        args.out,
        {
            **base,
            "status": disposition,
            "cmd_status": status,
            **measured,
            "denied_procs": tracker.denied if tracker else 0,
            "job_error": job_error,
        },
    )

    if disposition != "ok":
        # A warning, not an error: the wrapped command's result is the lane's
        # signal and must not be overwritten by an instrument fault.  The
        # NOT MEASURED rendering is ci/peak-memory-report.sh's job — this line
        # exists so the fault is attributable at the step that took it.
        print(
            f"::warning::#266 peak-RSS instrument took no usable measurement "
            f"(status={disposition}, samples={sampler.samples if sampler else 0}, "
            f"peak={sampler.peak if sampler else 0}). "
            f"The ctest --parallel memory criterion is UNMEASURED on this run; "
            f"do not read it as evidence.",
            file=sys.stderr,
        )

    return status


if __name__ == "__main__":
    sys.exit(main())
