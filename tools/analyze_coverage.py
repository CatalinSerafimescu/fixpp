#!/usr/bin/env python3
"""
analyze_coverage.py — parse an lcov .info file and report:
 - aggregate line% / branch% over production code
 - top 20 worst-covered production files
 - per-module rollup
 - files below 90% line OR below 80% branch

Usage: python3 tools/analyze_coverage.py build/linux-clang-coverage/coverage.lcov
"""
import sys
import os
import re
from collections import defaultdict

def parse_lcov(path):
    """
    Parse lcov .info format.
    Merges multiple records for the same source file (deduplication across binaries).
    Returns list of dicts: {file, lines_found, lines_hit, branches_found, branches_hit}
    """
    # per-file: {line_no: max_count}, {(line,block,branch): max_taken}
    file_lines = {}   # file -> {lineno: max_count}
    file_branches = {}  # file -> {(lineno, block, branch): max_taken}

    cur_file = None
    with open(path) as f:
        for line in f:
            line = line.rstrip()
            if line.startswith("SF:"):
                cur_file = line[3:]
                if cur_file not in file_lines:
                    file_lines[cur_file] = {}
                    file_branches[cur_file] = {}
            elif line.startswith("DA:") and cur_file:
                # DA:<line_number>,<execution_count>
                parts = line[3:].split(",")
                lineno = int(parts[0])
                try:
                    count = float(parts[1])
                except ValueError:
                    count = 0
                prev = file_lines[cur_file].get(lineno, 0)
                file_lines[cur_file][lineno] = max(prev, count)
            elif line.startswith("BRDA:") and cur_file:
                # BRDA:<line>,<block>,<branch>,<taken>
                parts = line[5:].split(",")
                key = (int(parts[0]), parts[1], parts[2])
                taken = parts[3]
                if taken == "-":
                    taken_val = -1
                else:
                    try:
                        taken_val = float(taken)
                    except ValueError:
                        taken_val = 0
                prev = file_branches[cur_file].get(key, -1)
                file_branches[cur_file][key] = max(prev, taken_val)
            elif line == "end_of_record":
                cur_file = None

    # Aggregate into records
    records = []
    for filepath in sorted(file_lines.keys()):
        lines = file_lines[filepath]
        branches = file_branches[filepath]
        lf = len(lines)
        lh = sum(1 for v in lines.values() if v > 0)
        bf = sum(1 for v in branches.values() if v >= 0)
        bh = sum(1 for v in branches.values() if v > 0)
        records.append({
            "file": filepath,
            "lines_found": lf, "lines_hit": lh,
            "branches_found": bf, "branches_hit": bh,
        })
    return records


def module_of(filepath, lib_root):
    """Return a module label for a file path."""
    rel = os.path.relpath(filepath, lib_root)
    parts = rel.split(os.sep)
    # parts[0] = include or src, parts[1] = fixpp or module, etc.
    if parts[0] == "include" and len(parts) > 2:
        return parts[2]  # include/fixpp/<module>/...
    elif parts[0] == "src" and len(parts) > 1:
        return parts[1]  # src/<module>/...
    return parts[0]


def is_production(filepath, lib_root):
    """Return True if this file is production code (exclude tests/bench/tools/bindings/generated)."""
    rel = os.path.relpath(filepath, lib_root)
    parts = rel.split(os.sep)
    if not parts:
        return False
    top = parts[0]
    if top not in ("src", "include"):
        return False
    # exclude generated codegen files
    name = os.path.basename(filepath)
    if "_codegen" in name or "Reify" in name or name.endswith("_codegen.hpp"):
        return False
    return True


def pct(hit, found):
    if found == 0:
        return 100.0
    return 100.0 * hit / found


def main():
    if len(sys.argv) < 2:
        print("Usage: analyze_coverage.py <coverage.lcov> [lib_root]")
        sys.exit(1)

    lcov_path = sys.argv[1]
    lib_root = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.dirname(os.path.abspath(lcov_path)))

    records = parse_lcov(lcov_path)
    prod = [r for r in records if is_production(r["file"], lib_root)]

    # ── aggregate ──────────────────────────────────────────────────────────
    tot_lf = sum(r["lines_found"] for r in prod)
    tot_lh = sum(r["lines_hit"] for r in prod)
    tot_bf = sum(r["branches_found"] for r in prod)
    tot_bh = sum(r["branches_hit"] for r in prod)

    print("=" * 70)
    print("AGGREGATE (production code: src/ + include/fixpp/)")
    print("=" * 70)
    print(f"  Lines   : {tot_lh:6d} / {tot_lf:6d}  = {pct(tot_lh, tot_lf):6.2f}%")
    print(f"  Branches: {tot_bh:6d} / {tot_bf:6d}  = {pct(tot_bh, tot_bf):6.2f}%")
    print(f"  Files   : {len(prod)}")
    print()

    # ── per-module rollup ─────────────────────────────────────────────────
    mod_stats = defaultdict(lambda: [0, 0, 0, 0])  # lf, lh, bf, bh
    for r in prod:
        m = module_of(r["file"], lib_root)
        mod_stats[m][0] += r["lines_found"]
        mod_stats[m][1] += r["lines_hit"]
        mod_stats[m][2] += r["branches_found"]
        mod_stats[m][3] += r["branches_hit"]

    print("=" * 70)
    print("PER-MODULE ROLLUP")
    print("=" * 70)
    print(f"  {'Module':<22} {'Line%':>7}  {'Branch%':>8}  {'Lines (hit/tot)':>18}  {'Branches (hit/tot)':>20}")
    print(f"  {'-'*22} {'-'*7}  {'-'*8}  {'-'*18}  {'-'*20}")
    for m in sorted(mod_stats):
        lf, lh, bf, bh = mod_stats[m]
        lp = pct(lh, lf)
        bp = pct(bh, bf)
        flag = ""
        if lp < 90.0:
            flag += " <LINE"
        if bp < 80.0:
            flag += " <BRANCH"
        print(f"  {m:<22} {lp:7.2f}%  {bp:8.2f}%  {lh:6d}/{lf:6d}       {bh:6d}/{bf:6d}{flag}")
    print()

    # ── top 20 worst by line% ─────────────────────────────────────────────
    prod_sorted = sorted(
        prod,
        key=lambda r: (pct(r["lines_hit"], r["lines_found"]), pct(r["branches_hit"], r["branches_found"]))
    )

    print("=" * 70)
    print("TOP 20 WORST-COVERED PRODUCTION FILES (by line%)")
    print("=" * 70)
    print(f"  {'File':<60} {'Line%':>7}  {'Branch%':>8}  {'Uncov':>5}")
    print(f"  {'-'*60} {'-'*7}  {'-'*8}  {'-'*5}")
    for r in prod_sorted[:20]:
        rel = os.path.relpath(r["file"], lib_root)
        lp = pct(r["lines_hit"], r["lines_found"])
        bp = pct(r["branches_hit"], r["branches_found"])
        uncov = r["lines_found"] - r["lines_hit"]
        # truncate long paths
        if len(rel) > 60:
            rel = "..." + rel[-57:]
        print(f"  {rel:<60} {lp:7.2f}%  {bp:8.2f}%  {uncov:5d}")
    print()

    # ── uplift candidates ─────────────────────────────────────────────────
    below = [r for r in prod if pct(r["lines_hit"], r["lines_found"]) < 90.0
             or pct(r["branches_hit"], r["branches_found"]) < 80.0]
    below.sort(key=lambda r: pct(r["lines_hit"], r["lines_found"]))

    print("=" * 70)
    print(f"UPLIFT CANDIDATES (<90% line OR <80% branch)  [{len(below)} files]")
    print("=" * 70)
    print(f"  {'File':<60} {'Line%':>7}  {'Branch%':>8}  {'Uncov':>5}")
    print(f"  {'-'*60} {'-'*7}  {'-'*8}  {'-'*5}")
    for r in below:
        rel = os.path.relpath(r["file"], lib_root)
        lp = pct(r["lines_hit"], r["lines_found"])
        bp = pct(r["branches_hit"], r["branches_found"])
        uncov = r["lines_found"] - r["lines_hit"]
        flags = []
        if lp < 90.0:
            flags.append("<LINE")
        if bp < 80.0:
            flags.append("<BRANCH")
        flag_str = " [" + ",".join(flags) + "]"
        if len(rel) > 60:
            rel = "..." + rel[-57:]
        print(f"  {rel:<60} {lp:7.2f}%  {bp:8.2f}%  {uncov:5d}{flag_str}")
    print()

    print("=" * 70)
    print(f"Files passing (≥90% line AND ≥80% branch): {len(prod) - len(below)} / {len(prod)}")
    print("=" * 70)


if __name__ == "__main__":
    main()
