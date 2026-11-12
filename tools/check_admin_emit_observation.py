#!/usr/bin/env python3
"""tools/check_admin_emit_observation.py — SC-002 static enumeration gate.

For every engine-originated administrative emit site in session.cpp:
  - build_reject( + build_reject_with_text( + build_logout( callers → must
    have fire_to_admin_( before their next store_then_emit( in the same emit
    block.
  - build_business_message_reject( callers → must have ->toApp( (via
    parse_and_dispatch_) before their next store_then_emit(.
  - Calls of a listed build helper (HELPERS below) are sites too, with the hook
    of the builder the helper wraps.

A builder call inside the definition of a listed helper is not a site. A helper
is listed because it only builds the frame and returns it: the emit, and so the
hook, belong to the helper's caller, and the gate checks every caller instead.
The exemption holds only while that is true, so the gate refuses a listed
helper whose definition contains store_then_emit( or does not close. Only a
definition qualified exactly as Session::<name>( is a listed helper's; a raw
builder call anywhere else, including in a helper not listed, stays a site.

Exits 0 if clean, 1 if any admin emit reaches store_then_emit without the
required observation hook (fire_to_admin_ or ->toApp) appearing between the
builder call and the emit, or if a listed helper is refused.

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


def _strip_cpp_comments(lines: list[str]) -> list[str]:
    """Strip C++ line comments (// ...) and block comments (/* ... */) from a
    list of source lines, returning a parallel list of comment-free lines.

    This prevents a token that appears only inside a comment (e.g. a disabled
    or example call) from being mistaken for a real observation hook.

    Algorithm:
      Walk the text character-by-character tracking block-comment state.
      Replace every character inside a comment with a space (preserving line
      lengths so line numbers remain 1:1 with the originals).
    """
    result: list[str] = []
    in_block = False
    for line in lines:
        out = []
        i = 0
        n = len(line)
        while i < n:
            if in_block:
                if i + 1 < n and line[i] == "*" and line[i + 1] == "/":
                    out.append("  ")  # consume */
                    i += 2
                    in_block = False
                else:
                    out.append(" ")  # mask comment char
                    i += 1
            else:
                if i + 1 < n and line[i] == "/" and line[i + 1] == "/":
                    # Line comment: mask everything from here to end-of-line.
                    out.append(" " * (n - i))
                    break
                elif i + 1 < n and line[i] == "/" and line[i + 1] == "*":
                    out.append("  ")  # consume /*
                    i += 2
                    in_block = True
                elif line[i] in ('"', "'"):
                    # String/char literal: copy verbatim (we don't need to
                    # parse it accurately; false-positives from strings are
                    # acceptable since no hook token appears in a string literal
                    # in session.cpp and we only care about NOT counting comments).
                    out.append(line[i])
                    i += 1
                else:
                    out.append(line[i])
                    i += 1
        result.append("".join(out))
    return result

# Default target file
DEFAULT_TARGET = ROOT / "src" / "session" / "session.cpp"

# Builder tokens and their required observation hook tokens.
# We check that the hook appears between the builder line and the next
# store_then_emit( line.
#
# build_reject(, build_reject_with_text( and build_logout( → must have
# fire_to_admin_( in the interval.
# build_business_message_reject( → must have ->toApp( in the interval.
#
# The mapping is:
#   builder token → (required hook token, description for error message)
ADMIN_BUILDERS: dict[str, tuple[str, str]] = {
    "build_reject(": ("fire_to_admin_(", "fire_to_admin_"),
    # 092-garbled-frame-reject R-5: the Text-carrying Reject builder. Its name does
    # not contain "build_reject(", so it needs its own key.
    "build_reject_with_text(": ("fire_to_admin_(", "fire_to_admin_"),
    "build_logout(": ("fire_to_admin_(", "fire_to_admin_"),
}
APP_BUILDERS: dict[str, tuple[str, str]] = {
    "build_business_message_reject(": ("->toApp(", "->toApp"),
}

# Build helpers: a Session member that wraps a builder, builds the frame and
# returns it without emitting. Each maps to the hook its builder requires.
HELPERS: dict[str, tuple[str, str]] = {
    "build_inbound_reject_": ("fire_to_admin_(", "fire_to_admin_"),
    "build_inbound_business_reject_": ("->toApp(", "->toApp"),
}

ALL_BUILDERS = {
    **ADMIN_BUILDERS,
    **APP_BUILDERS,
    **{f"{name}(": hook for name, hook in HELPERS.items()},
}

# Token that marks the emit point — every builder→emit interval must cross it
# only AFTER the observation hook.
EMIT_TOKEN = "store_then_emit("


_LITERAL = re.compile(r'"(?:\\.|[^"\\])*"' + r"|'(?:\\.|[^'\\])*'")

Pos = tuple[int, int]  # (0-based line, column)


def _mask_literals(line: str) -> str:
    """Blank the inside of string and character literals, keeping the length."""
    return _LITERAL.sub(lambda m: m[0][0] + " " * (len(m[0]) - 2) + m[0][-1], line)


def _helper_spans(
    path: Path, stripped: list[str]
) -> tuple[list[tuple[Pos, Pos]], set[Pos], list[str]]:
    """Return the spans of the listed helpers' definitions, the position of each
    definition's helper name, and a refusal for each definition the exemption
    does not hold for.

    A definition is a `Session::<name>(` not preceded by an identifier
    character or `:`, whose next `{` comes before any `;`. Its span runs from
    `Session::` to the brace that closes that `{`, counted on comment-stripped
    lines with the inside of string and character literals blanked.
    """
    spans: list[tuple[Pos, Pos]] = []
    names: set[Pos] = set()
    refusals: list[str] = []
    masked = [_mask_literals(line) for line in stripped]
    for name in HELPERS:
        pattern = re.compile(r"(?<![\w:])Session::" + re.escape(name) + r"\(")
        for row, line in enumerate(masked):
            for m in pattern.finditer(line):
                text = line[m.end():] + "".join(masked[row + 1:])
                brace, semi = text.find("{"), text.find(";")
                if brace < 0 or (0 <= semi < brace):
                    continue  # a call or a declaration, not a definition
                names.add((row, m.start() + len("Session::")))
                depth = 0
                end: Pos | None = None
                k, col = row, m.end()
                while k < len(masked) and end is None:
                    for c in range(col, len(masked[k])):
                        ch = masked[k][c]
                        if ch == "{":
                            depth += 1
                        elif ch == "}" and depth > 0:
                            depth -= 1
                            if depth == 0:
                                end = (k, c)
                                break
                    k, col = k + 1, 0
                if end is None:
                    refusals.append(
                        f"{path}:{row + 1}: helper 'Session::{name}(' definition does not "
                        f"close — check the site manually"
                    )
                    continue
                start = (row, m.start())
                emits = [
                    (r, c.start())
                    for r in range(start[0], end[0] + 1)
                    for c in re.finditer(re.escape(EMIT_TOKEN), stripped[r])
                    if start <= (r, c.start()) <= end
                ]
                if emits:
                    refusals.append(
                        f"{path}:{row + 1}: helper 'Session::{name}(' definition contains "
                        f"store_then_emit at :{emits[0][0] + 1} — a listed helper must "
                        f"return the frame without emitting"
                    )
                    continue
                spans.append((start, end))
    return spans, names, refusals


def _builder_key(
    line: str, row: int, spans: list[tuple[Pos, Pos]], names: set[Pos]
) -> str | None:
    """Return the builder key of the first builder call on the line that is
    neither inside a listed helper's definition nor that definition's name."""
    for key in ALL_BUILDERS:
        for m in re.finditer(re.escape(key), line):
            pos = (row, m.start())
            if pos in names or any(lo <= pos <= hi for lo, hi in spans):
                continue
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

    Comment stripping: the interval scan uses comment-stripped lines so that
    a hook token appearing only inside a C++ comment (// or /* */) is NOT
    counted as a real observation call. Builder detection uses the original
    lines (builders don't appear in comments in production code).
    """
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    # Parallel comment-stripped version for hook/emit detection.
    stripped = _strip_cpp_comments(lines)

    total_lines = len(lines)

    spans, names, violations = _helper_spans(path, stripped)

    for i, line in enumerate(lines, 1):
        key = _builder_key(line, i - 1, spans, names)
        if key is None:
            continue

        required_hook, hook_name = ALL_BUILDERS[key]

        # Scan forward from the builder line to find store_then_emit.
        # The window bounds the forward pairing, so that a builder is not paired
        # with a later site's emit. It counts code lines (comment-stripped and
        # non-blank): comments between a builder and its emit say nothing about the
        # pairing, and must not push the emit out of the window. A builder whose
        # emit lies beyond the window is flagged. Use comment-stripped lines so
        # tokens inside comments don't count.
        # Exercised by tools/test_check_admin_emit_observation.py.
        SEARCH_WINDOW = 60
        emit_line_no: int | None = None
        hook_found = False

        j = i  # 0-based index of the line after the builder
        code_lines = 0
        while j < total_lines and code_lines < SEARCH_WINDOW:
            scan_line = stripped[j]  # comment-stripped; 0-based
            if scan_line.strip():
                code_lines += 1
            # Check for hook in the interval (before the emit).
            if emit_line_no is None and required_hook in scan_line:
                hook_found = True
            if EMIT_TOKEN in scan_line:
                emit_line_no = j + 1  # 1-based for reporting
                break
            j += 1

        if emit_line_no is None:
            # No store_then_emit found — the builder result is unused or outside window.
            # This would be a structural anomaly; flag it.
            violations.append(
                f"{path}:{i}: builder '{key}' has no store_then_emit within "
                f"{SEARCH_WINDOW} code lines — check the site manually"
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

    print(
        f"[check_admin_emit_observation] OK - all builder->fire_to_admin_/toApp->store_then_emit "
        f"chains verified in {target.name} (SC-002 gate PASS)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
