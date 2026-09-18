#!/usr/bin/env python3
"""tools/check_alloc.py — the alloc-guard (seam #6) gate wrapper.

Runs a binary under the mallocnesia LD_PRELOAD interceptor and exits nonzero if any
allocation is intercepted between the guard markers.

    python3 tools/check_alloc.py --binary <bin> [--mallocnesia <so>] [--max-allocs N]

fixpp#448 — WHAT CHANGED AND WHY IT IS NOT COSMETIC.

This wrapper used to FAIL OPEN. When it could not find the interceptor it printed
`WARNING: mallocnesia not found; running without allocation interception` and then
returned the binary's own exit code, so a clean run became `PASS: no unexpected
allocations detected`. No CI lane built the interceptor, so that was the path CI would
have taken had any lane run these gates at all.

Two independent failures had to be closed, because either alone still passes:

 1. THE INTERCEPTOR IS MISSING. Now fatal, unless --allow-missing is passed
    explicitly (local convenience only; never in CI).

 2. THE INTERCEPTOR IS PRESENT BUT NEVER LOADED. `LD_PRELOAD=/nonexistent/x.so` is
    NOT an error — ld.so prints "cannot be preloaded ... ignored" and runs the binary
    UNINSTRUMENTED, which exits 0. An exit code cannot tell that apart from a real
    pass, so this wrapper does not try: it requires the child to leave a WITNESS
    (MALLOCNESIA_WITNESS, written by the interceptor's constructor). No witness ⇒
    the gate did not run ⇒ FAIL, whatever the binary returned.

Measured before the fix: `LD_PRELOAD=/nonexistent/libmallocnesia.so
MALLOCNESIA_MAX_ALLOCS=0 log_alloc_test` prints the ld.so notice, then
`[  PASSED  ] 2 tests`, exit 0.
"""
import argparse
import os
import subprocess
import sys
import tempfile


def find_mallocnesia(explicit: str | None) -> str | None:
    """Locate the interceptor. An explicit path (CMake passes $<TARGET_FILE:mallocnesia>)
    wins and is NOT probed against the fallbacks — a build that names its own artifact
    must not silently fall back to a stale one someone built by hand months ago."""
    if explicit:
        return explicit if os.path.exists(explicit) else None
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
    parser = argparse.ArgumentParser(description="Alloc-guard gate wrapper")
    parser.add_argument("--binary", help="Path to the alloc-guard binary")
    parser.add_argument("--max-allocs", type=int, default=0,
                        help="Maximum allocations allowed between guard markers (default: 0)")
    parser.add_argument("--target", default=None, help="Alias for --binary (CMake-style usage)")
    parser.add_argument("--mallocnesia", default=None,
                        help="Explicit interceptor path; CMake passes $<TARGET_FILE:mallocnesia>.")
    parser.add_argument("--allow-missing", action="store_true",
                        help="Run UNINSTRUMENTED when the interceptor is absent. Local "
                             "convenience only — this is the fail-open behaviour fixpp#448 "
                             "removed, so it must never appear in a CI invocation.")
    args = parser.parse_args()

    binary = args.binary or args.target
    if not binary:
        print("error: --binary or --target required", file=sys.stderr)
        return 2
    if not os.path.exists(binary):
        print(f"error: binary not found: {binary}", file=sys.stderr)
        return 2

    mallocnesia = find_mallocnesia(args.mallocnesia)
    env = os.environ.copy()

    if not mallocnesia:
        if not args.allow_missing:
            where = args.mallocnesia or "the search path (MALLOCNESIA_PATH, tools/mallocnesia/)"
            print(f"[check_alloc] FAIL: mallocnesia interceptor not found at {where}. "
                  f"Refusing to run: without it this gate asserts NOTHING and would report "
                  f"PASS. Build it (target `mallocnesia`), or pass --allow-missing if you "
                  f"deliberately want an uninstrumented local run.", file=sys.stderr)
            return 2
        print("[check_alloc] WARNING: mallocnesia not found and --allow-missing given; "
              "running UNINSTRUMENTED. This run proves nothing.", file=sys.stderr)
        result = subprocess.run([binary], env=env)
        return result.returncode

    with tempfile.TemporaryDirectory() as tmp:
        witness = os.path.join(tmp, "mallocnesia.witness")
        env["LD_PRELOAD"] = mallocnesia
        env["MALLOCNESIA_MAX_ALLOCS"] = str(args.max_allocs)
        env["MALLOCNESIA_WITNESS"] = witness
        print(f"[check_alloc] Using mallocnesia: {mallocnesia} (max-allocs={args.max_allocs})")

        result = subprocess.run([binary], env=env)

        # ⚠️ ORDER MATTERS. The witness is checked BEFORE the exit code, because the
        # case being closed is a binary that exits 0 having never been instrumented.
        # Checking rc first and returning early would step straight over it.
        if not os.path.exists(witness):
            print(f"[check_alloc] FAIL: the interceptor left no witness — it was NOT loaded "
                  f"into {os.path.basename(binary)}, so nothing was intercepted and the "
                  f"binary's exit status ({result.returncode}) says nothing about "
                  f"allocations. ld.so IGNORES an unloadable LD_PRELOAD rather than "
                  f"failing; check that {mallocnesia} is loadable by that binary "
                  f"(architecture, missing deps, noexec mount).", file=sys.stderr)
            return 2

    if result.returncode != 0:
        print(f"[check_alloc] FAIL: binary exited {result.returncode}", file=sys.stderr)
        return result.returncode

    print("[check_alloc] PASS: interception confirmed, no unexpected allocations detected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
