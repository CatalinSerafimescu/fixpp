#!/usr/bin/env python3
"""tools/bench_compare.py — Google Benchmark baseline comparator.

TWO MODES, and the split is load-bearing:

  LEGACY (soft, unchanged):   bench_compare.py <baseline.json> <current.json>
  SUITE  (hard, #209):        bench_compare.py --suite <results-dir>

The legacy positional form is invoked by the `bench_threading_regression` CTest
cell via cmake/run_bench_regression.cmake, and by the quickstarts. It compares a
CI-or-local measurement against a dev-host baseline, so it MUST stay soft —
hardening it would redden a ctest cell on an invalid comparison. Its behaviour is
byte-for-byte what it was before #209.

── SUITE MODE (#209) ─────────────────────────────────────────────────────────
Drives `bench/ci-suite.txt` and asserts. Two axes, only one of them hard:

  Axis A — INSTRUMENT INTEGRITY. Hard `exit 1`. Reads no clock, so it cannot
  flake. This is the axis that catches #263's blind spot #1 — "6 of 8 profiles
  cannot be compared at all", an instrument failure that read as green:

      A1  results file missing / unparseable / empty
      A2  `benchmarks: []` on EITHER side
      A3  benchmark name-set mismatch baseline<->current
      A4  the current run is not a release build
      A5  `cpu_time` null on one side where the other is a number
      A6  an allowlisted binary missing from the build tree  (in run-bench-suite.sh)

  Axis B — TIMING, +/-5% per [const §VIII.2]. Provenance-gated, and REPORTING
  by default (`--timing report`).

  ⚠️ WHY AXIS B IS NOT HARD YET, in one line: every pre-#209 baseline was
  recorded on the WSL2 dev host (num_cpus 8 or 10, or absent) and four wire/*
  baselines are `build_type: debug`, so a CI-vs-baseline wall-clock comparison
  is invalid at ANY band — #263 measured that host drifting -35% against ITSELF
  across two sessions, and this repo has measured 27-43% spread across runners.
  Flipping `--timing enforce` before a noise floor exists would fit a band to a
  single seeded run, which is
  feedback_timing_band_witness_range_admits_the_mutant_it_claims_to_kill.
  The exit criterion for flipping it lives in .specify/ci209-bench-gate.md §6.

  A row is never silently skipped. A baseline that cannot be compared prints its
  DISQUALIFIER by name, because a skipped row is how a gate reads green on
  nothing.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

THRESHOLD_PCT = 5.0  # [const §VIII.2]


# ── legacy soft mode — DO NOT HARDEN (see module docstring) ──────────────────

def load_benchmarks(path: str) -> dict[str, float | None]:
    """Return {name: cpu_time_ns} mapping, or None if the number is absent."""
    try:
        with open(path) as f:
            data = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        print(f"[bench_compare] WARNING: cannot load {path!r}: {exc}")
        return {}

    result: dict[str, float | None] = {}
    for bm in data.get("benchmarks", []):
        name = bm.get("name", "<unnamed>")
        cpu_time = bm.get("cpu_time")  # may be null in stub baseline
        result[name] = cpu_time
    return result


def compare(baseline_path: str, current_path: str) -> int:
    """Compare and print results.  Returns 0 always (SOFT gate)."""
    baseline = load_benchmarks(baseline_path)
    current = load_benchmarks(current_path)

    if not baseline:
        print("[bench_compare] Baseline is empty or missing — skipping comparison.")
        return 0

    if not current:
        print("[bench_compare] Current results are empty or missing — skipping comparison.")
        return 0

    print(f"{'Benchmark':<50} {'Baseline (ns)':>15} {'Current (ns)':>14} {'Delta':>8}")
    print("-" * 92)

    threshold_pct = THRESHOLD_PCT
    violations: list[str] = []

    all_names = sorted(set(baseline) | set(current))
    for name in all_names:
        base_val = baseline.get(name)
        curr_val = current.get(name)

        if base_val is None or curr_val is None:
            base_str = f"{base_val}" if base_val is not None else "null"
            curr_str = f"{curr_val}" if curr_val is not None else "null"
            print(f"  {name:<48} {base_str:>15} {curr_str:>14} {'N/A':>8}")
            continue

        delta_pct = (curr_val - base_val) / base_val * 100.0
        flag = "  <-- REGRESSION" if delta_pct > threshold_pct else ""
        print(f"  {name:<48} {base_val:>15.2f} {curr_val:>14.2f} {delta_pct:>+7.1f}%{flag}")
        if delta_pct > threshold_pct:
            violations.append(f"{name}: {delta_pct:+.1f}%")

    print()
    if violations:
        print(f"[bench_compare] SOFT WARNING — {len(violations)} benchmark(s) regressed >±{threshold_pct}%:")
        for v in violations:
            print(f"  {v}")
        print("[bench_compare] Gate is SOFT this phase — not failing CI.")
    else:
        print("[bench_compare] All benchmarks within ±5% threshold.")

    return 0  # SOFT: legacy path, invalid comparand — see module docstring


# ── suite mode (#209) ────────────────────────────────────────────────────────

class Findings:
    """Collects hard failures so ALL of them are reported, not just the first.

    A gate that dies on the first defect makes the operator re-run CI once per
    defect. Every Axis-A cell is evaluated for every row, then we exit once.
    """

    def __init__(self) -> None:
        self.hard: list[str] = []
        self.notes: list[str] = []

    def fail(self, cell: str, target: str, msg: str) -> None:
        self.hard.append(f"[{cell}] {target}: {msg}")
        print(f"    ::error::[{cell}] {msg}")

    def note(self, msg: str) -> None:
        self.notes.append(msg)


def read_manifest(path: str) -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = []
    with open(path) as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 2:
                raise SystemExit(
                    f"::error::{path}:{lineno}: expected exactly 2 fields "
                    f"(<target> <baseline>), got {len(parts)}: {line!r}"
                )
            rows.append((parts[0], parts[1]))
    if not rows:
        # A manifest that parses to zero rows satisfies every downstream check
        # vacuously — the silent-empty class, three times in one gate previously.
        raise SystemExit(f"::error::{path}: manifest yielded zero rows")
    return rows


def load_json(path: str) -> tuple[dict | None, str | None]:
    """Return (data, error). Never raises."""
    if not os.path.exists(path):
        return None, "file does not exist"
    try:
        with open(path) as f:
            return json.load(f), None
    except json.JSONDecodeError as exc:
        return None, f"not valid JSON: {exc}"
    except OSError as exc:
        return None, f"unreadable: {exc}"


def build_type_of(ctx: dict) -> str | None:
    """Google Benchmark spells this `library_build_type`; older files use `build_type`."""
    for key in ("library_build_type", "build_type"):
        if key in ctx:
            return str(ctx[key]).lower()
    return None


def timing_rows(data: dict) -> dict[str, float | None]:
    """{name: cpu_time} for the rows the timing axis compares.

    With --benchmark_report_aggregates_only the runner emits *_mean/_median/
    _stddev/_cv per benchmark. We compare MEDIAN only: it is the robust
    location estimate, and _stddev/_cv are dispersion (comparing them as if
    they were times is meaningless), while _mean is the one an outlier moves.
    Files with no aggregates at all (every pre-#209 baseline) fall back to the
    plain rows so they can still be name-set checked.
    """
    rows = data.get("benchmarks", []) or []
    medians = {b.get("name", "<unnamed>"): b.get("cpu_time")
               for b in rows if b.get("aggregate_name") == "median"}
    if medians:
        return medians
    return {b.get("name", "<unnamed>"): b.get("cpu_time")
            for b in rows if b.get("aggregate_name") is None}


def provenance_verdict(baseline: dict, current: dict) -> tuple[bool, str]:
    """Is this baseline a VALID comparand for the current run's hardware?

    Returns (comparable, reason). `reason` is printed whether or not the row is
    comparable — an unexplained skip is indistinguishable from a pass.
    """
    bctx = baseline.get("context", {}) or {}
    cctx = current.get("context", {}) or {}

    bprov = bctx.get("fixpp_provenance")
    if not bprov:
        return False, "baseline carries no fixpp_provenance marker (pre-#209, dev-host origin)"

    bt = build_type_of(bctx)
    if bt is not None and bt != "release":
        return False, f"baseline build_type={bt!r}, not release"

    cprov = cctx.get("fixpp_provenance", {}) or {}
    if bprov.get("preset") != cprov.get("preset"):
        return False, (f"preset mismatch: baseline={bprov.get('preset')!r} "
                       f"current={cprov.get('preset')!r}")
    if bprov.get("runner") != cprov.get("runner"):
        return False, (f"runner class mismatch: baseline={bprov.get('runner')!r} "
                       f"current={cprov.get('runner')!r}")

    bcpus, ccpus = bctx.get("num_cpus"), cctx.get("num_cpus")
    if bcpus != ccpus:
        return False, f"num_cpus mismatch: baseline={bcpus} current={ccpus}"

    return True, f"provenance match ({bprov.get('preset')} on {bprov.get('runner')}, num_cpus={ccpus})"


def compare_suite(results_dir: str, baselines_dir: str, manifest: str,
                  timing_mode: str, tolerance: float) -> int:
    rows = read_manifest(manifest)
    f = Findings()

    print(f"=== bench gate (#209) — {len(rows)} allowlisted binaries ===")
    print(f"    results  : {results_dir}")
    print(f"    baselines: {baselines_dir}")
    print(f"    manifest : {manifest}")
    print(f"    timing   : {timing_mode} (tolerance ±{tolerance}%)")
    print()

    comparable_rows = 0
    timing_violations: list[str] = []

    for target, baseline_rel in rows:
        cur_path = os.path.join(results_dir, f"{target}.json")
        base_path = os.path.join(baselines_dir, baseline_rel)
        print(f"--- {target} ---")

        cur, cur_err = load_json(cur_path)
        base, base_err = load_json(base_path)

        # A1 — results file missing / unparseable.
        if cur_err:
            f.fail("A1", target, f"current results {cur_path}: {cur_err}")
            continue
        if base_err:
            f.fail("A1", target, f"baseline {base_path}: {base_err}")
            continue

        cur_bms = cur.get("benchmarks", []) or []
        base_bms = base.get("benchmarks", []) or []

        # A2 — `benchmarks: []` on EITHER side. Two shipped baselines are in
        # exactly this state today; the old comparator printed "skipping
        # comparison" and returned 0.
        empty = False
        if not cur_bms:
            f.fail("A2", target, f"current results {cur_path} contain zero benchmarks")
            empty = True
        if not base_bms:
            f.fail("A2", target, f"baseline {baseline_rel} contains zero benchmarks — "
                                 f"re-seed it from a CI release run")
            empty = True
        if empty:
            continue

        # A4 — the CURRENT run must be a release build. Asserted on the
        # provenance preset (authoritative, stamped by the runner) with
        # build_type as corroboration, because the context key is absent in
        # some google-benchmark versions and an absent key must not read as OK.
        cctx = cur.get("context", {}) or {}
        cprov = cctx.get("fixpp_provenance", {}) or {}
        cpreset = cprov.get("preset")
        if not cpreset:
            f.fail("A4", target, "current results carry no fixpp_provenance.preset "
                                 "— produced outside ci/run-bench-suite.sh?")
        elif "release" not in cpreset:
            f.fail("A4", target, f"current run preset {cpreset!r} is not a release build; "
                                 f"debug timings are ~10-25x release and are not the budgeted quantity")
        cbt = build_type_of(cctx)
        if cbt is not None and cbt != "release":
            f.fail("A4", target, f"current run build_type={cbt!r}, not release")

        cur_t = timing_rows(cur)
        base_t = timing_rows(base)

        # A3 — name-set mismatch. A benchmark deleted or renamed silently stops
        # being measured; under the old comparator it printed one `N/A` line.
        only_base = sorted(set(base_t) - set(cur_t))
        only_cur = sorted(set(cur_t) - set(base_t))
        comparable, reason = provenance_verdict(base, cur)
        if only_base or only_cur:
            # A pre-#209 baseline has no aggregate rows, so its names cannot
            # match a `_median`-suffixed current set. That is a stale-baseline
            # fact, already reported as the provenance disqualifier — reporting
            # it a second time as A3 would be a duplicate finding on one cause.
            if comparable:
                f.fail("A3", target,
                       f"benchmark name-set mismatch — only in baseline: {only_base or 'none'}; "
                       f"only in current: {only_cur or 'none'}")
            else:
                print(f"    A3 deferred: name sets differ, but the baseline is already "
                      f"disqualified ({reason}) — re-seeding fixes both.")

        # A5 — null cpu_time facing a number. Previously printed `N/A` and
        # continued, so a benchmark that started emitting nulls read as green.
        for name in sorted(set(base_t) & set(cur_t)):
            bv, cv = base_t[name], cur_t[name]
            if (bv is None) != (cv is None):
                f.fail("A5", target,
                       f"{name}: cpu_time is null on one side "
                       f"(baseline={bv!r} current={cv!r})")

        # ── Axis B ──
        print(f"    provenance: {'COMPARABLE' if comparable else 'NOT COMPARABLE'} — {reason}")
        if not comparable:
            f.note(f"{target}: timing not compared — {reason}")
            print(f"    {len(cur_t)} benchmark(s) measured; timing axis skipped for this row.")
            continue

        comparable_rows += 1
        shared = sorted(set(base_t) & set(cur_t))
        print(f"    {'Benchmark':<46} {'Base (ns)':>13} {'Cur (ns)':>13} {'Delta':>9}")
        for name in shared:
            bv, cv = base_t[name], cur_t[name]
            if bv is None or cv is None or bv == 0:
                continue
            delta = (cv - bv) / bv * 100.0
            flag = ""
            if delta > tolerance:
                flag = "  <-- REGRESSION"
                timing_violations.append(f"{target}/{name}: {delta:+.1f}%")
            print(f"    {name:<46} {bv:>13.2f} {cv:>13.2f} {delta:>+8.1f}%{flag}")
        print()

    # ── verdict ──
    print()
    print("=== verdict ===")
    if f.notes:
        print(f"Axis B — {len(f.notes)} row(s) NOT timing-compared (each named, none silent):")
        for n in f.notes:
            print(f"  - {n}")
    print(f"Axis B — {comparable_rows} row(s) had a provenance-matched baseline.")

    if timing_violations:
        verb = "FAIL" if timing_mode == "enforce" else "REPORT-ONLY"
        print(f"Axis B — {len(timing_violations)} benchmark(s) over ±{tolerance}% [{verb}]:")
        for v in timing_violations:
            print(f"  - {v}")
        if timing_mode != "enforce":
            print("  (timing axis is REPORT-ONLY — see .specify/ci209-bench-gate.md §6 "
                  "for the criterion that flips it to enforce)")

    if f.hard:
        print()
        print(f"::error::bench gate FAILED — {len(f.hard)} instrument-integrity finding(s):")
        for h in f.hard:
            print(f"  - {h}")
        return 1

    if timing_mode == "enforce" and timing_violations:
        print()
        print(f"::error::bench gate FAILED — {len(timing_violations)} timing regression(s) "
              f"over ±{tolerance}%.")
        return 1

    print("bench gate PASSED (Axis A clean"
          + (f"; Axis B report-only" if timing_mode != "enforce" else "; Axis B clean") + ").")
    return 0


def main(argv: list[str]) -> int:
    # Legacy positional form: exactly two non-flag args. Detected before
    # argparse so the CTest cell's command line keeps working verbatim.
    if len(argv) == 2 and not argv[0].startswith("-"):
        return compare(argv[0], argv[1])

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    p = argparse.ArgumentParser(description="Google Benchmark baseline comparator")
    p.add_argument("--suite", metavar="RESULTS_DIR", required=True,
                   help="directory of <target>.json written by ci/run-bench-suite.sh")
    p.add_argument("--baselines-dir", default=os.path.join(repo_root, "bench", "baselines"))
    p.add_argument("--manifest", default=os.path.join(repo_root, "bench", "ci-suite.txt"))
    p.add_argument("--timing", choices=("report", "enforce"), default="report",
                   help="Axis B disposition; see .specify/ci209-bench-gate.md §6")
    p.add_argument("--tolerance", type=float, default=THRESHOLD_PCT)
    args = p.parse_args(argv)

    return compare_suite(args.suite, args.baselines_dir, args.manifest,
                         args.timing, args.tolerance)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
