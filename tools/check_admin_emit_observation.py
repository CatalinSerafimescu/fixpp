#!/usr/bin/env python3
"""tools/check_admin_emit_observation.py — SC-002 static enumeration gate.

For every engine-originated administrative emit site in session.cpp:
  - build_reject( + build_logout( callers → must have fire_to_admin_( before
    their next store_then_emit( in the same emit block.
  - build_business_message_reject( callers → must have ->toApp( (via
    parse_and_dispatch_) before their next store_then_emit(.

Exits 0 if clean, 1 if any admin emit reaches store_then_emit without the
required observation hook (fire_to_admin_ or ->toApp) appearing between the
builder call and the emit.

Anchors: spec.md SC-002/FR-005/FR-001; tasks.md T041; research.md Decision-1.
Usage:
  python3 tools/check_admin_emit_observation.py [path/to/session.cpp]
  (default: src/session/session.cpp relative to this script's parent)
"""

import re
import sys
from pathlib import Path


# Root of the library (tools/ is a sibling of src/)
ROOT = Path(__file__).parent.parent

# Default target file
DEFAULT_TARGET = ROOT / "src" / "session" / "session.cpp"

# Builder tokens and their required observation hook tokens.
# We check that the hook appears between the builder line and the next
# store_then_emit( line.
#
# build_reject( and build_logout( → must have fire_to_admin_( in the interval.
# build_business_message_reject( → must have ->toApp( in the interval.
#
# The mapping is:
#   builder token → (required hook token, description for error message)
ADMIN_BUILDERS: dict[str, tuple[str, str]] = {
    "build_reject(": ("fire_to_admin_(", "fire_to_admin_"),
    "build_logout(": ("fire_to_admin_(", "fire_to_admin_"),
}
APP_BUILDERS: dict[str, tuple[str, str]] = {
    "build_business_message_reject(": ("->toApp(", "->toApp"),
}

ALL_BUILDERS = {**ADMIN_BUILDERS, **APP_BUILDERS}

# Token that marks the emit point — every builder→emit interval must cross it
# only AFTER the observation hook.
EMIT_TOKEN = "store_then_emit("


def _builder_key(line: str) -> str | None:
    """Return the matching builder key if line contains a known builder call."""
    for key in ALL_BUILDERS:
        if key in line:
            return key
    return None


def check_file(path: Path) -> list[str]:
    """Check session.cpp for unwired admin emit sites.

    Algorithm:
      For each line containing a builder call:
        1. Record the builder type (admin or app) → required hook.
        2. Scan forward from the builder line to find the next store_then_emit(.
        3. In the interval (builder_line, store_then_emit_line), check whether
           the required hook token appears.
        4. If no store_then_emit is found after the builder, flag the site as
           an error (the builder must eventually emit).
        5. If the hook is absent in the interval, flag the site.

    This is forward-scan-only (no brace parsing) which is robust against
    formatting changes and tolerates nested expressions. The constraint
    "hook must appear between builder and its emit" is a strictly stronger
    check than "hook exists somewhere in the function" and correctly catches
    a future unwired site.
    """
    violations: list[str] = []

    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    total_lines = len(lines)

    for i, line in enumerate(lines, 1):
        key = _builder_key(line)
        if key is None:
            continue

        required_hook, hook_name = ALL_BUILDERS[key]

        # Scan forward from the builder line to find store_then_emit.
        # Search up to 60 lines ahead (all current emit blocks are <20 lines).
        SEARCH_WINDOW = 60
        emit_line_no: int | None = None
        hook_found = False

        for j in range(i, min(i + SEARCH_WINDOW, total_lines)):
            scan_line = lines[j]  # 0-based; line i corresponds to lines[i-1]
            # Check for hook in the interval (before the emit).
            if emit_line_no is None and required_hook in scan_line:
                hook_found = True
            if EMIT_TOKEN in scan_line:
                emit_line_no = j + 1  # 1-based for reporting
                break

        if emit_line_no is None:
            # No store_then_emit found — the builder result is unused or outside window.
            # This would be a structural anomaly; flag it.
            violations.append(
                f"{path}:{i}: builder '{key}' has no store_then_emit within "
                f"{SEARCH_WINDOW} lines — check the site manually"
            )
            continue

        if not hook_found:
            violations.append(
                f"{path}:{i}: admin/app emit site uses '{key}' but "
                f"'{hook_name}(' was NOT found before store_then_emit at :{emit_line_no} "
                f"— SC-002/FR-001/FR-005 violation (unwired observation)"
            )

    return violations


def main() -> int:
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_TARGET

    if not target.exists():
        print(f"[check_admin_emit_observation] ERROR — file not found: {target}")
        return 1

    violations = check_file(target)

    if violations:
        print(
            f"[check_admin_emit_observation] FAIL — {len(violations)} unwired emit site(s) in "
            f"{target}:"
        )
        for v in violations:
            print(f"  {v}")
        return 1

    # Count for informational output (not the gate — the gate is per-site structural).
    admin_count = sum(1 for key in ADMIN_BUILDERS for _ in [None])
    print(
        f"[check_admin_emit_observation] OK — all builder→fire_to_admin_/toApp→store_then_emit "
        f"chains verified in {target.name} (SC-002 gate PASS)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
