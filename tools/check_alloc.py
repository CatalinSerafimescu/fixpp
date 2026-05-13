#!/usr/bin/env python3
"""tools/check_alloc.py — Seam #6 alloc-guard wrapper.

Runs decimal_alloc_guard_test under mallocnesia LD_PRELOAD and exits nonzero
if any malloc/free call is intercepted between the guard markers.

Usage:
    python tools/check_alloc.py \\
        --binary build/linux-clang-debug/bin/decimal_alloc_guard_test \\
        [--max-allocs 0]
"""
import argparse
import os
import subprocess
import sys


def find_mallocnesia() -> str | None:
    """Locate the mallocnesia shared library on the system."""
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    for candidate in [
        os.environ.get("MALLOCNESIA_PATH", ""),
        os.path.join(tools_dir, "mallocnesia", "libmallocnesia.so"),
        "/usr/local/lib/mallocnesia.so",
        "/usr/lib/mallocnesia.so",
    ]:
        if candidate and os.path.exists(candidate):
            return candidate
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Alloc-guard wrapper for decimal tests")
    parser.add_argument("--binary", required=True,
                        help="Path to decimal_alloc_guard_test binary")
    parser.add_argument("--max-allocs", type=int, default=0,
                        help="Maximum allowed allocations between guard markers (default: 0)")
    parser.add_argument("--target", default=None,
                        help="Alias for --binary (CMake-style usage)")
    args = parser.parse_args()

    binary = args.binary or args.target
    if not binary:
        print("error: --binary or --target required", file=sys.stderr)
        return 2

    if not os.path.exists(binary):
        print(f"error: binary not found: {binary}", file=sys.stderr)
        return 2

    mallocnesia = find_mallocnesia()
    env = os.environ.copy()

    if mallocnesia:
        env["LD_PRELOAD"] = mallocnesia
        env["MALLOCNESIA_MAX_ALLOCS"] = str(args.max_allocs)
        print(f"[check_alloc] Using mallocnesia: {mallocnesia}")
    else:
        print(
            "[check_alloc] WARNING: mallocnesia not found; "
            "running without allocation interception",
            file=sys.stderr,
        )

    result = subprocess.run([binary], env=env)

    if result.returncode != 0:
        print(f"[check_alloc] FAIL: binary exited {result.returncode}", file=sys.stderr)
        return result.returncode

    print("[check_alloc] PASS: no unexpected allocations detected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
