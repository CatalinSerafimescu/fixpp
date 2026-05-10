#!/usr/bin/env python3
"""tools/bench_compare.py — Google Benchmark baseline comparator.

Usage:
    python3 tools/bench_compare.py <baseline.json> <current.json>

Prints ±% per benchmark.  Always exits 0 (SOFT gate until Phase 4 module 1).
[const §VIII.2] — gate becomes hard once Phase 4 module 1 closes.
"""

import json
import sys


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

    threshold_pct = 5.0  # ±5% per [const §VIII.2]; SOFT this phase
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

    return 0  # SOFT: always exit 0 until Phase 4 module 1 closes


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <baseline.json> <current.json>", file=sys.stderr)
        sys.exit(1)
    sys.exit(compare(sys.argv[1], sys.argv[2]))
