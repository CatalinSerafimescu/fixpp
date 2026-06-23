#!/usr/bin/env python3
"""Recurrence guard for the inert alloc-guard marker bug (item 13, 2026-06-19).

The mallocnesia no-alloc gates rely on alloc_guard_start() / alloc_guard_end()
being WEAK UNDEFINED symbols so that LD_PRELOAD=tools/mallocnesia/libmallocnesia.so
can interpose them at runtime.

A LOCAL definition WITH A BODY — e.g. `__attribute__((weak)) void alloc_guard_start() {}`
— binds intra-executable, is never routed through the PLT, and so LD_PRELOAD can
NEVER interpose it: the counting window is never armed and the gate is silently
false-green. (That was exactly the repo-wide bug item 13 fixed.)

This check fails the build if any test/bench source reintroduces a *bodied*
definition of an `alloc_guard_start` / `alloc_guard_end` marker ON THE POSIX PATH.

    Allowed   : __attribute__((weak)) void alloc_guard_start();   (undefined decl)
                if (alloc_guard_start) alloc_guard_start();        (null-checked call)
    Forbidden : __attribute__((weak)) void alloc_guard_start() {}  (local definition)

Windows exception: there is no LD_PRELOAD on Windows, so the cross-platform marker
header (tests/support/alloc_guard_markers.hpp) legitimately defines inline no-op
bodies on the `#ifdef _WIN32` branch. The scan is preprocessor-aware about `_WIN32`
guards and only flags definitions reachable on the POSIX (non-_WIN32) path — so a
bodied def in a `#else` / unguarded region is still caught, anywhere in the tree.

Reference correct pattern: tests/perf/test_transport_read_alloc_guard.cpp.
The only legitimate DEFINITION of these symbols is the preload itself,
tools/mallocnesia/mallocnesia.c, which this scan (tests/, bench/) excludes.
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCAN_DIRS = ["tests", "bench"]
EXTS = {".cpp", ".cc", ".cxx", ".hpp", ".hh", ".h"}

# A function DEFINITION of an arming marker: `void NAME() {` (return type + body).
# Anchoring on the `void` return type avoids false positives on comment mentions
# and on null-checked calls — a comment `// ...alloc_guard_start()` and a call
# `if (alloc_guard_start) alloc_guard_start();` both lack the `void NAME() {` shape.
# (The empty arg list may be spelled `()` or `(void)`.)
DEFN = re.compile(r"\bvoid\s+alloc_guard_(?:start|end)\s*\(\s*(?:void\s*)?\)\s*\{")


def _classify_open(directive: str):
    """Classify a #if/#ifdef/#ifndef line as (is_win32, posix_reachable_in_this_branch).

    Non-_WIN32 conditionals are treated as POSIX-reachable (conservative: we never
    suppress a finding behind an unrelated guard)."""
    s = directive.strip()
    if re.match(r"#\s*ifdef\s+_WIN32\b", s):
        return True, False
    if re.match(r"#\s*ifndef\s+_WIN32\b", s):
        return True, True
    m = re.match(r"#\s*if\b(.*)", s)
    if m and "_WIN32" in m.group(1):
        negated = bool(re.search(r"!\s*defined\s*\(\s*_WIN32", m.group(1)))
        return True, negated
    return False, True


def _find_offenders(text: str, relpath) -> list:
    """Return DEFN matches that are reachable on the POSIX (non-_WIN32) path."""
    offenders = []
    stack = []  # frames: {"is_win32": bool, "cur": bool (POSIX-reachable here)}
    for i, line in enumerate(text.splitlines(), start=1):
        s = line.lstrip()
        if s.startswith("#"):
            if re.match(r"#\s*(ifdef|ifndef|if)\b", s):
                is_win32, reachable = _classify_open(s)
                stack.append({"is_win32": is_win32, "cur": reachable})
            elif re.match(r"#\s*elif\b", s) and stack:
                stack[-1]["cur"] = True  # conservative after an elif
            elif re.match(r"#\s*else\b", s) and stack:
                top = stack[-1]
                top["cur"] = (not top["cur"]) if top["is_win32"] else True
            elif re.match(r"#\s*endif\b", s) and stack:
                stack.pop()
            continue
        if all(f["cur"] for f in stack):
            m = DEFN.search(line)
            if m:
                offenders.append((relpath, i, m.group(0).strip()))
    return offenders


def main() -> int:
    offenders = []
    for d in SCAN_DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for p in base.rglob("*"):
            if p.suffix not in EXTS:
                continue
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            offenders.extend(_find_offenders(text, p.relative_to(ROOT)))

    if offenders:
        print("[alloc-guard-markers] FAIL: bodied alloc_guard_* definition(s) found.")
        print("  A LOCAL definition disables LD_PRELOAD interception -> false-green gate.")
        print("  Use a weak UNDEFINED declaration + null-checked call instead")
        print("  (see tests/perf/test_transport_read_alloc_guard.cpp).")
        for f, ln, snip in offenders:
            print(f"    {f}:{ln}: {snip}")
        return 1

    print("[alloc-guard-markers] PASS: no bodied alloc_guard_* definitions in tests/, bench/.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
