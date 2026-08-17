#!/usr/bin/env python3
"""tools/bench_compare.py — Google Benchmark comparator and CI bench gate.

THREE MODES, and the split is load-bearing:

  LEGACY (soft, unchanged):  bench_compare.py <baseline.json> <current.json>
  SUITE  (tier 1 + 3, #209): bench_compare.py --suite <results-dir>
  PAIRED (tier 2, #209):     bench_compare.py --paired --a1 D --b D --a2 D

The legacy positional form is invoked by the `bench_threading_regression` CTest
cell via cmake/run_bench_regression.cmake, and by the quickstarts. It compares a
measurement against a dev-host baseline, so it MUST stay soft — hardening it
would redden a ctest cell on an invalid comparison. Its behaviour is
byte-for-byte what it was before #209.

── WHY THREE TIERS (.specify/ci209-bench-gate.md) ────────────────────────────
The obvious fix — harden the existing ±5% baseline comparison — is not
available, for a reason that is about the COMPARAND, not the band:

  * 10 of the allowlisted baselines contain NO `cpu_time` field on any row.
    They are hand-authored analysis records (`ceiling_ns`, `ceiling_source`,
    `verdict`, `_timing_is_meaningless_here`), never Google-Benchmark output.
  * 3 more are `benchmarks: []`.
  * every remaining one was recorded on the WSL2 dev host (num_cpus 8 or 10).
    #263 measured that host drifting -35% against ITSELF across two sessions;
    this repo has measured 27-43% spread across CI runners.
  * and re-seeding is circular: seeding from the candidate makes the candidate
    its own comparand, seeding from main canonizes #263's known regression.

So:

  TIER 1 — execution + schema. HARD. Needs no comparand at all, which is why it
           can gate all binaries including the ones with no usable baseline.
  TIER 2 — base-vs-candidate on ONE runner, A-B-A. HARD. The paired same-VM
           instrument this repo has measured to be the only valid one (2.10x
           paired where unpaired read 1.02x). Needs no baseline either.
  TIER 3 — checked-in baselines. INFORMATIONAL, and prints, per row, the named
           reason a row is not comparable. A silently skipped row is how a gate
           reads green on nothing.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys

THRESHOLD_PCT = 5.0  # [const §VIII.2]
PAIRED_BAND_PCT = 50.0  # provisional; see .specify/ci209-bench-gate.md §4 tier 2
# Tighter than the regression band ON PURPOSE — see run_paired's min-vs-min note.
PAIRED_NOISE_BAND_PCT = 20.0


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
        flag = "  <-- REGRESSION" if delta_pct > THRESHOLD_PCT else ""
        print(f"  {name:<48} {base_val:>15.2f} {curr_val:>14.2f} {delta_pct:>+7.1f}%{flag}")
        if delta_pct > THRESHOLD_PCT:
            violations.append(f"{name}: {delta_pct:+.1f}%")

    print()
    if violations:
        print(f"[bench_compare] SOFT WARNING — {len(violations)} benchmark(s) regressed >±{THRESHOLD_PCT}%:")
        for v in violations:
            print(f"  {v}")
        print("[bench_compare] Gate is SOFT this phase — not failing CI.")
    else:
        print("[bench_compare] All benchmarks within ±5% threshold.")

    return 0  # SOFT: legacy path, invalid comparand — see module docstring


# ── shared: manifest + loading ───────────────────────────────────────────────

class Row:
    __slots__ = ("exe_rel", "name", "comparand", "tier2")

    def __init__(self, exe_rel: str, comparand: str, tier2: str) -> None:
        self.exe_rel = exe_rel
        self.name = os.path.basename(exe_rel)
        self.comparand = comparand
        self.tier2 = tier2


def read_manifest(path: str) -> list[Row]:
    rows: list[Row] = []
    with open(path) as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 3:
                raise SystemExit(f"::error::{path}:{lineno}: expected exactly 3 fields "
                                 f"(<exe> <comparand> <tier2>), got {len(parts)}: {line!r}")
            exe_rel, comparand, tier2 = parts
            if tier2 not in ("paired", "no"):
                raise SystemExit(f"::error::{path}:{lineno}: tier-2 field must be "
                                 f"'paired' or 'no', got {tier2!r}")
            if not (comparand.startswith("gb-json:") or comparand.startswith("none:")):
                raise SystemExit(f"::error::{path}:{lineno}: comparand must start with "
                                 f"'gb-json:' or 'none:', got {comparand!r}")
            rows.append(Row(exe_rel, comparand, tier2))
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


# ── TIER 1 — execution + schema validation. No comparand needed. ─────────────

# Rows whose value is a DURATION and must therefore be strictly positive.
# `stddev` and `cv` are DISPERSION: a stddev of exactly 0.0 is the correct
# output for three identical repetitions, and `cv` is a dimensionless ratio.
# Requiring > 0 of them would fire on a perfectly good tree — the cell would be
# the defect. This distinction is the reason T1-5 is split rather than uniform.
_DURATION_AGGREGATES = (None, "mean", "median")
_DISPERSION_AGGREGATES = ("stddev", "cv")

# Google Benchmark's `time_unit` vocabulary. Checked per row (T1-7): an
# unrecognised unit means the number's scale is unknown, which is worse than a
# missing number because it still compares.
_VALID_TIME_UNITS = {"ns", "us", "ms", "s"}


def validate_results(name: str, data: dict) -> list[str]:
    """Tier-1 cells T1-2, T1-4..T1-8. Returns a list of findings (empty == clean)."""
    out: list[str] = []
    rows = data.get("benchmarks")

    # T1-2 — nothing was measured.
    if not isinstance(rows, list):
        out.append(f"[T1-2] {name}: `benchmarks` is not a list (got {type(rows).__name__})")
        return out
    if not rows:
        out.append(f"[T1-2] {name}: `benchmarks` is empty — the binary measured nothing")
        return out

    seen: set[tuple] = set()
    units: set[str] = set()

    for i, row in enumerate(rows):
        if not isinstance(row, dict):
            out.append(f"[T1-4] {name}: row {i} is not an object")
            continue

        # T1-4 — a row with no usable identity.
        rname = row.get("name")
        if not isinstance(rname, str) or not rname:
            out.append(f"[T1-4] {name}: row {i} has a missing or non-string `name`")
            continue

        # T1-8 — Google Benchmark's own error disposition. A benchmark that
        # reported an error still emits a row; without this cell that row is
        # read as a measurement.
        if row.get("error_occurred"):
            out.append(f"[T1-8] {name}/{rname}: error_occurred — "
                       f"{row.get('error_message', '<no message>')!r}")
            continue

        agg = row.get("aggregate_name")
        run_type = row.get("run_type")

        # T1-6 — duplicate identity. A plain {name: value} dict silently
        # collapses these; `dictionary/xml_loader.json` carries 21 rows with 6
        # duplicate names and `threading_baselines.json` 56 rows with 16, so a
        # lost repetition is invisible to set equality.
        ident = (rname, run_type, agg, row.get("repetition_index"))
        if ident in seen:
            out.append(f"[T1-6] {name}/{rname}: duplicate (name, run_type, "
                       f"aggregate_name, repetition_index) identity {ident!r}")
        seen.add(ident)

        # T1-7 — unit validity, PER ROW.
        #
        # ⚠️ THIS CELL USED TO REQUIRE ONE UNIT PER BINARY AND WOULD HAVE
        # REDDENED A CORRECT TREE. `bench/dictionary/table_view_footprint_bench.cpp`
        # deliberately mixes units: five benchmarks carry
        # `->Unit(benchmark::kMicrosecond)` (:118, :134, :193, :218, :231) while
        # `BM_TableView_Sizeof` uses the default ns. A per-binary uniformity rule
        # makes the cell itself the defect — exactly the failure mode the GREEN
        # controls in ci/test-bench-gate.sh exist to catch.
        #
        # What actually matters is that a unit is RECOGNISED, and that a
        # comparison never crosses units for the SAME benchmark name — the
        # latter is enforced per-name at comparison time, not per-file here.
        unit = row.get("time_unit")
        if unit is not None:
            if unit not in _VALID_TIME_UNITS:
                out.append(f"[T1-7] {name}/{rname}: unrecognised time_unit {unit!r} "
                           f"(expected one of {sorted(_VALID_TIME_UNITS)})")
            # Grouped by `run_name` — the BENCHMARK — not by the row's own
            # `name`. Aggregate rows are named BM_X_mean / BM_X_median /
            # BM_X_stddev, each unique, so grouping by `name` puts every row in
            # its own bucket and the check can never fire. Caught by cell
            # T1-7a, which sat green against this exact mutation.
            units.add((str(row.get("run_name") or rname), str(unit)))

        # T1-9 — iteration count. A row reporting zero iterations timed nothing.
        iters = row.get("iterations")
        if iters is not None:
            if isinstance(iters, bool) or not isinstance(iters, int) or iters <= 0:
                out.append(f"[T1-9] {name}/{rname}: `iterations` is not a positive "
                           f"integer ({iters!r})")

        # T1-5 — the measurement itself.
        for key in ("cpu_time", "real_time"):
            if key not in row:
                out.append(f"[T1-5] {name}/{rname}: `{key}` absent")
                continue
            val = row[key]
            if isinstance(val, bool) or not isinstance(val, (int, float)):
                out.append(f"[T1-5] {name}/{rname}: `{key}` is not numeric ({val!r})")
                continue
            if math.isnan(val) or math.isinf(val):
                out.append(f"[T1-5] {name}/{rname}: `{key}` is not finite ({val!r})")
                continue
            if agg in _DISPERSION_AGGREGATES:
                if val < 0:
                    out.append(f"[T1-5] {name}/{rname}: dispersion `{key}` is negative ({val!r})")
            elif agg in _DURATION_AGGREGATES or run_type == "iteration":
                if val <= 0:
                    out.append(f"[T1-5] {name}/{rname}: duration `{key}` is not positive ({val!r})")

    # One benchmark NAME reporting two different units within a single run is
    # still a defect (it makes that row's own aggregates incomparable) — but
    # different names carrying different units is normal and permitted above.
    per_name: dict[str, set[str]] = {}
    for nm, u in units:
        per_name.setdefault(nm, set()).add(u)
    for nm, us in sorted(per_name.items()):
        if len(us) > 1:
            out.append(f"[T1-7] {name}/{nm}: the same benchmark reports multiple "
                       f"time_units in one run: {sorted(us)}")

    return out


def _num(v: object) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def median_rows(data: dict) -> dict[str, tuple[float, str | None]]:
    """{name: (cpu_time, time_unit)} for the rows the timing axes compare.

    MEDIAN only: it is the robust location estimate, `_mean` is the one an
    outlier moves, and `_stddev`/`_cv` are dispersion rather than times. Files
    with no aggregates at all (every pre-#209 baseline) fall back to plain rows.

    The unit travels WITH the value. Benchmarks in one binary legitimately carry
    different units (`table_view_footprint_bench` mixes us and ns on purpose), so
    a comparison that dropped the unit could subtract microseconds from
    nanoseconds and report a 1000x improvement.
    """
    rows = [r for r in data.get("benchmarks", []) or [] if isinstance(r, dict)]
    med = {r["name"]: (r["cpu_time"], r.get("time_unit")) for r in rows
           if r.get("aggregate_name") == "median" and _num(r.get("cpu_time"))
           and isinstance(r.get("name"), str)}
    if med:
        return med
    return {r["name"]: (r["cpu_time"], r.get("time_unit")) for r in rows
            if r.get("aggregate_name") is None and _num(r.get("cpu_time"))
            and isinstance(r.get("name"), str)}


# ── SUITE mode: tier 1 (hard) + tier 3 (informational) ──────────────────────

def run_suite(results_dir: str, baselines_dir: str, manifest: str,
              tolerance: float) -> int:
    rows = read_manifest(manifest)
    hard: list[str] = []

    print(f"=== bench gate (#209) — tier 1 + tier 3 over {len(rows)} binaries ===")
    print(f"    results  : {results_dir}")
    print(f"    baselines: {baselines_dir}")
    print()

    tier3_compared = 0
    tier3_skipped: list[str] = []
    tier3_over: list[str] = []

    for row in rows:
        cur_path = os.path.join(results_dir, f"{row.name}.json")
        print(f"--- {row.name} ---")

        cur, err = load_json(cur_path)
        # T1-1 — missing / unparseable results.
        if err:
            hard.append(f"[T1-1] {row.name}: results {cur_path}: {err}")
            print(f"    ::error::[T1-1] results {cur_path}: {err}")
            continue

        findings = validate_results(row.name, cur)
        if findings:
            for f_ in findings:
                print(f"    ::error::{f_}")
            hard.extend(findings)
        else:
            n = len(cur.get("benchmarks", []))
            print(f"    tier 1: OK ({n} rows validated)")

        # ── tier 3 — informational, and never a silent skip ──
        if row.comparand.startswith("none:"):
            reason = row.comparand.split(":", 1)[1]
            print(f"    tier 3: NOT COMPARED — {reason}")
            tier3_skipped.append(f"{row.name}: {reason}")
            continue

        base_rel = row.comparand.split(":", 1)[1]
        base_path = os.path.join(baselines_dir, base_rel)
        base, berr = load_json(base_path)
        if berr:
            # A declared gb-json comparand that cannot be read is a manifest
            # defect, not an informational note — the manifest asserted it exists.
            hard.append(f"[T1-1] {row.name}: declared baseline {base_rel}: {berr}")
            print(f"    ::error::[T1-1] declared baseline {base_rel}: {berr}")
            continue

        cur_t, base_t = median_rows(cur), median_rows(base)
        shared = sorted(set(cur_t) & set(base_t))
        if not shared:
            print(f"    tier 3: NOT COMPARED — no benchmark name is present in both "
                  f"{base_rel} and this run ({len(base_t)} baseline / {len(cur_t)} current rows)")
            tier3_skipped.append(f"{row.name}: no shared benchmark names with {base_rel}")
            continue

        tier3_compared += 1
        print(f"    tier 3: vs {base_rel} (dev-host baseline — REPORT ONLY)")
        for nm in shared:
            (bv, bu), (cv, cu) = base_t[nm], cur_t[nm]
            if bv == 0:
                continue
            if bu != cu:
                print(f"      {nm:<52} unit changed {bu!r} -> {cu!r}; not compared")
                continue
            d = (cv - bv) / bv * 100.0
            mark = "  <-- over ±%.0f%%" % tolerance if abs(d) > tolerance else ""
            print(f"      {nm:<52} {bv:>12.2f} -> {cv:>12.2f}  {d:>+7.1f}%{mark}")
            if abs(d) > tolerance:
                tier3_over.append(f"{row.name}/{nm}: {d:+.1f}%")

    print()
    print("=== tier 3 summary (informational — [const §VIII.2] ±5%) ===")
    print(f"  {tier3_compared} row(s) compared; {len(tier3_skipped)} not compared, each named:")
    for s in tier3_skipped:
        print(f"    - {s}")
    if tier3_over:
        print(f"  {len(tier3_over)} benchmark(s) outside ±{tolerance}% vs a DEV-HOST baseline:")
        for v in tier3_over:
            print(f"    - {v}")
        print("  ⚠️ NOT a failure: the comparand was recorded on different hardware "
              "(see .specify/ci209-bench-gate.md §2a). The hard timing signal is tier 2.")

    print()
    if hard:
        print(f"::error::bench gate FAILED — {len(hard)} tier-1 finding(s):")
        for h in hard:
            print(f"  - {h}")
        return 1
    print("bench gate: tier 1 PASSED (execution + schema clean on all rows).")
    return 0


# ── PAIRED mode: tier 2 (hard) ──────────────────────────────────────────────

def run_paired(a1_dir: str, b1_dir: str, a2_dir: str, b2_dir: str,
               manifest: str, band: float, noise_band: float) -> int:
    """Candidate (A) vs merge-base (B), measured A-B-A-B on one runner.

    The A-vs-A delta is a noise floor for the CANDIDATE phase. ⚠️ It is NOT
    sufficient on its own, and an A-B-A design that checks only it has a real
    bypass — worked through by Codex round 2:

        true base = 100, true candidate = 160  (a +60% regression)
        A1 = 160,  B measured during a 20% throttle transient = 120,  A2 = 160
        A-vs-A = 0%  -> "informative"
        candidate vs observed base = 33%  -> inside a 50% band -> PASS

    The two A values agreeing says nothing about a transient confined to B. So
    the base is measured TWICE as well (A-B-A-B) and B-vs-B is checked with the
    same rule. A transient large enough to hide a regression has to land on both
    B legs while sparing both A legs.
    """
    rows = [r for r in read_manifest(manifest) if r.tier2 == "paired"]
    if not rows:
        # Vacuity guard: `paired` disappearing from the manifest would make this
        # whole tier silently pass.
        print("::error::tier 2: no manifest row is marked `paired` — the hard "
              "timing axis would assert nothing.")
        return 1

    print(f"=== bench gate (#209) — tier 2, paired base-vs-candidate, {len(rows)} binaries ===")
    print(f"    A1 (candidate, 1st) : {a1_dir}")
    print(f"    B1 (merge-base, 1st): {b1_dir}")
    print(f"    A2 (candidate, 2nd) : {a2_dir}")
    print(f"    B2 (merge-base, 2nd): {b2_dir}")
    print(f"    regression band     : ±{band}%  (PROVISIONAL — see .specify/ci209-bench-gate.md §4)")
    print(f"    noise band          : ±{noise_band}%  (tighter on purpose — see run_paired)")
    print()

    hard: list[str] = []
    uninformative: list[str] = []
    regressions: list[str] = []
    compared = 0

    for row in rows:
        print(f"--- {row.name} ---")
        loaded = {}
        missing = False
        for tag, d in (("A1", a1_dir), ("B1", b1_dir), ("A2", a2_dir), ("B2", b2_dir)):
            data, err = load_json(os.path.join(d, f"{row.name}.json"))
            if err:
                hard.append(f"[T1-1] tier 2 {row.name}: leg {tag}: {err}")
                print(f"    ::error::[T1-1] leg {tag}: {err}")
                missing = True
                continue
            loaded[tag] = median_rows(data)
        if missing:
            continue

        base_names = set(loaded["B1"]) & set(loaded["B2"])
        cand_names = set(loaded["A1"]) & set(loaded["A2"])

        # T2-DEL — a benchmark the BASE measures that the CANDIDATE no longer
        # emits. Under a plain intersection this is a live bypass: rename or
        # delete the slow benchmark and it is simply not compared, so the
        # regression it would have shown disappears with it.
        #
        # The asymmetry is deliberate. Names only in the CANDIDATE are fine —
        # that is a PR adding a benchmark, which must not be an error, or the
        # gate would punish exactly the behaviour Article VIII §3 requires.
        gone = sorted(base_names - cand_names)
        if gone:
            hard.append(f"[T2-DEL] {row.name}: benchmark(s) present in the merge-base but "
                        f"absent from the candidate — renamed or deleted, which removes them "
                        f"from the gate: {gone}")
            print(f"    ::error::[T2-DEL] present in base, absent in candidate: {gone}")

        shared = sorted(base_names & cand_names)
        if not shared:
            hard.append(f"[T2-0] {row.name}: no benchmark name present in all four legs "
                        f"(A1={len(loaded['A1'])} B1={len(loaded['B1'])} "
                        f"A2={len(loaded['A2'])} B2={len(loaded['B2'])})")
            print(f"    ::error::[T2-0] no benchmark name present in all four legs")
            continue

        print(f"    {'Benchmark':<40} {'base':>11} {'cand':>11} {'delta':>8} {'A~A':>7} {'B~B':>7}")
        for nm in shared:
            (a1, ua1), (b1, ub1) = loaded["A1"][nm], loaded["B1"][nm]
            (a2, ua2), (b2, ub2) = loaded["A2"][nm], loaded["B2"][nm]
            if min(a1, b1, a2, b2) <= 0:
                continue
            # A unit change between base and candidate makes the numbers
            # incomparable; subtracting us from ns would read as a 1000x win.
            if len({ua1, ub1, ua2, ub2}) > 1:
                hard.append(f"[T1-7] {row.name}/{nm}: time_unit differs across legs "
                            f"(A1={ua1!r} B1={ub1!r} A2={ua2!r} B2={ub2!r})")
                print(f"    ::error::[T1-7] {nm}: time_unit differs across legs")
                continue
            compared += 1
            # ⚠️ MIN, NOT MEAN, PER TREE — and this is the fix that actually
            # closes Codex round 2's bypass. Averaging the two legs does not:
            # with true base 100 and candidate 160 (+60%), a 20% transient on
            # ONE base leg gives mean-base 110, delta +45.5%, inside a 50% band
            # -> PASS. Cell T2-TRANSIENT held that fixture green against the
            # mean, which is how the hole was found rather than argued.
            #
            # Benchmark noise is ONE-SIDED: contention, throttling and
            # migration only ever make a measurement SLOWER, never faster. So
            # the fastest observation of each tree is its least-contaminated
            # estimate, and min-vs-min removes an inflating transient on either
            # side instead of averaging it into the comparand.
            cand = min(a1, a2)
            base = min(b1, b2)
            noise_a = abs(a2 - a1) / ((a1 + a2) / 2.0) * 100.0
            noise_b = abs(b2 - b1) / ((b1 + b2) / 2.0) * 100.0
            delta = (cand - base) / base * 100.0

            mark = ""
            # The noise gate is DELIBERATELY TIGHTER than the regression band.
            # At the same width it is nearly inert: a transient only has to be
            # big enough to pull the delta under the band, which is far smaller
            # than the band itself.
            if noise_a > noise_band or noise_b > noise_band:
                mark = "  <-- UNINFORMATIVE"
                uninformative.append(f"{row.name}/{nm}: A-vs-A {noise_a:.1f}%, "
                                     f"B-vs-B {noise_b:.1f}% (noise band ±{noise_band}%)")
            elif delta > band:
                mark = "  <-- REGRESSION"
                regressions.append(f"{row.name}/{nm}: {delta:+.1f}%")
            print(f"    {nm:<40} {base:>11.2f} {cand:>11.2f} {delta:>+7.1f}% "
                  f"{noise_a:>6.1f}% {noise_b:>6.1f}%{mark}")
        print()

    print("=== tier 2 verdict ===")
    print(f"  {compared} benchmark row(s) compared across {len(rows)} binaries.")

    if uninformative:
        # UNINFORMATIVE must exit NON-ZERO. A verdict label that still exits 0
        # would have relocated the softness this whole change removes, not
        # eliminated it.
        print(f"::error::tier 2 UNINFORMATIVE — {len(uninformative)} row(s) whose two "
              f"same-tree measurements disagree by more than the band itself. "
              f"The run cannot distinguish a regression from its own noise, so it "
              f"does NOT pass:")
        for u in uninformative:
            print(f"  - {u}")
    if regressions:
        print(f"::error::tier 2 FAILED — {len(regressions)} benchmark(s) slower than the "
              f"merge-base by more than ±{band}%:")
        for r in regressions:
            print(f"  - {r}")
    if hard:
        print(f"::error::tier 2 — {len(hard)} integrity finding(s):")
        for h in hard:
            print(f"  - {h}")

    if hard or uninformative or regressions:
        return 1
    if compared == 0:
        print("::error::tier 2 compared ZERO rows — every paired binary produced no "
              "usable measurement. A gate that measures nothing must not pass.")
        return 1
    print(f"bench gate: tier 2 PASSED (no row slower than merge-base by >±{band}%).")
    return 0


def main(argv: list[str]) -> int:
    # Legacy positional form: exactly two non-flag args. Detected before argparse
    # so the CTest cell's command line keeps working verbatim.
    if len(argv) == 2 and not argv[0].startswith("-"):
        return compare(argv[0], argv[1])

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    p = argparse.ArgumentParser(description="Google Benchmark comparator / CI bench gate")
    p.add_argument("--manifest", default=os.path.join(repo_root, "bench", "ci-suite.txt"))
    p.add_argument("--baselines-dir", default=os.path.join(repo_root, "bench", "baselines"))
    p.add_argument("--suite", metavar="RESULTS_DIR",
                   help="tier 1 + tier 3 over a directory written by ci/run-bench-suite.sh")
    p.add_argument("--paired", action="store_true", help="tier 2 base-vs-candidate")
    p.add_argument("--a1", help="tier 2: candidate results, first measurement")
    p.add_argument("--b1", help="tier 2: merge-base results, first measurement")
    p.add_argument("--a2", help="tier 2: candidate results, second measurement")
    p.add_argument("--b2", help="tier 2: merge-base results, second measurement")
    p.add_argument("--band", type=float, default=PAIRED_BAND_PCT)
    p.add_argument("--noise-band", type=float, default=PAIRED_NOISE_BAND_PCT,
                   help="tier 2: same-tree spread above which a run is UNINFORMATIVE")
    p.add_argument("--tolerance", type=float, default=THRESHOLD_PCT)
    args = p.parse_args(argv)

    if args.paired:
        if not (args.a1 and args.b1 and args.a2 and args.b2):
            p.error("--paired requires --a1, --b1, --a2 and --b2 (A-B-A-B)")
        return run_paired(args.a1, args.b1, args.a2, args.b2, args.manifest,
                          args.band, args.noise_band)
    if args.suite:
        return run_suite(args.suite, args.baselines_dir, args.manifest, args.tolerance)
    p.error("one of --suite or --paired is required")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
