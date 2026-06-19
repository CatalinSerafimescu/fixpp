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
definition of an `alloc_guard_start` / `alloc_guard_end` marker.

    Allowed   : __attribute__((weak)) void alloc_guard_start();   (undefined decl)
                if (alloc_guard_start) alloc_guard_start();        (null-checked call)
    Forbidden : __attribute__((weak)) void alloc_guard_start() {}  (local definition)

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
            for m in DEFN.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                offenders.append((p.relative_to(ROOT), line, m.group(0).strip()))

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
