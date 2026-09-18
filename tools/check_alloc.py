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
    parser.add_argument("--expect-violation", action="store_true",
                        help="POSITIVE CONTROL. Invert the verdict: this binary is "
                             "supposed to allocate inside its guard window, so the run "
                             "passes ONLY if interception was active AND the interceptor "
                             "reported the violation. See the block comment below for why "
                             "this is not the same as ctest's WILL_FAIL.")
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

        # stderr is captured ONLY for the positive control, which has to read the
        # interceptor's verdict out of it; every other gate streams straight through.
        # `result.stderr` is None when not piped, so one call covers both.
        # Popen, not run(): the witness is checked against the DIRECT CHILD's pid, so a
        # note written by some grandchild that inherited the env var cannot stand in for
        # the process actually under test.
        proc = subprocess.Popen([binary], env=env, text=True,
                                stderr=subprocess.PIPE if args.expect_violation else None)
        _, captured_stderr = proc.communicate()   # (stdout, stderr) — in that order
        child_pid = proc.pid
        result = subprocess.CompletedProcess([binary], proc.returncode, None, captured_stderr)
        sys.stderr.write(captured_stderr or "")

        # ⚠️ ORDER MATTERS. The witness is checked BEFORE the exit code, because the
        # case being closed is a binary that exits 0 having never been instrumented.
        # Checking rc first and returning early would step straight over it.
        notes = set()
        if os.path.exists(witness):
            for line in open(witness, encoding="utf-8", errors="replace"):
                what, _, pid = line.strip().partition(" ")
                # Only the direct child counts (see Popen above).
                if pid == str(child_pid):
                    notes.add(what)

        if "loaded" not in notes:
            print(f"[check_alloc] FAIL: the interceptor left no witness — it was NOT loaded "
                  f"into {os.path.basename(binary)}, so nothing was intercepted and the "
                  f"binary's exit status ({result.returncode}) says nothing about "
                  f"allocations. ld.so IGNORES an unloadable LD_PRELOAD rather than "
                  f"failing; check that {mallocnesia} is loadable by that binary "
                  f"(architecture, missing deps, noexec mount).", file=sys.stderr)
            return 2

        # ⚠️ LOADED IS NOT INTERPOSED, and requiring only "loaded" was this check's own
        # false-green. The guard markers are WEAK UNDEFINED in the test binaries, so any
        # STRONG definition in the link closure beats the preload: the constructor still
        # runs and still writes "loaded" while g_active is never set and every allocation
        # is ignored. A sanitizer's allocator produces the same split. The start/end
        # notes are written by OUR marker definitions and by nothing else, so they are
        # what actually proves this binary's window was measured.
        missing = {"start", "end"} - notes
        if missing:
            print(f"[check_alloc] FAIL: the interceptor loaded into "
                  f"{os.path.basename(binary)} but its guard markers did NOT run "
                  f"({', '.join(sorted(missing))} never reached). Either the binary never "
                  f"entered its guarded window, or something in its link closure defines "
                  f"alloc_guard_start/alloc_guard_end STRONGLY and beat the preload — in "
                  f"which case nothing was measured and a zero count means nothing. "
                  f"`nm -C {binary} | grep alloc_guard` names the culprit.", file=sys.stderr)
            return 2

    # ⚠️ WHY THIS IS NOT ctest's WILL_FAIL.
    #
    # The obvious way to prove the gate can go RED is a binary that allocates on purpose,
    # registered WILL_FAIL. It does not work HERE, and it fails in the silent direction:
    # WILL_FAIL accepts ANY nonzero exit, and this wrapper returns 2 when the interceptor
    # is missing or was never loaded. So on precisely the lane where interception has
    # broken — the case the control exists to detect — the control still passes.
    # That is `an arm whose forced defect stays green measures its own setup`.
    #
    # So the control is inverted HERE instead, where all three facts are visible, and it
    # demands all three: interception was ACTIVE (witness above), the binary FAILED, and
    # it failed for the RIGHT REASON (the interceptor said so). Any other combination is
    # a broken control, not a pass.
    if args.expect_violation:
        said_so = "[mallocnesia] FAIL" in (result.stderr or "")
        if result.returncode == 0:
            print("[check_alloc] FAIL: this binary allocates inside its guard window on "
                  "purpose, but the run SUCCEEDED. Interception was active (witness "
                  "present) yet the planted allocation was not caught — the gate is not "
                  "measuring what it claims.", file=sys.stderr)
            return 1
        if not said_so:
            print(f"[check_alloc] FAIL: the binary exited {result.returncode}, but the "
                  f"interceptor never reported a violation. A nonzero exit for some OTHER "
                  f"reason (a crash, a gtest assertion) would satisfy a WILL_FAIL entry "
                  f"and prove nothing; this control requires the interceptor's own "
                  f"verdict.", file=sys.stderr)
            return 1
        print("[check_alloc] PASS (positive control): interception active, planted "
              "allocation detected and reported by the interceptor.")
        return 0

    if result.returncode != 0:
        print(f"[check_alloc] FAIL: binary exited {result.returncode}", file=sys.stderr)
        return result.returncode

    print("[check_alloc] PASS: interception confirmed (loaded + markers ran), "
              "no unexpected allocations detected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
