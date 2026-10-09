#!/usr/bin/env python3
"""tools/check_asio_recycler_guard.py — fixpp#544 (B35) asio recycler-size guards.

`.specify/544-hot-path-zero-alloc.md` §2.4, "The mechanical guards". asio's
`ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` sizes `asio::detail::thread_info_base`, whose inline
members every TU that includes asio compiles, so every such TU must see the value fixpp
exports. Its one source is `FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` in
include/fixpp/core/detail/asio_recycler_config.hpp. Two checks:

  headers  Every header under --include-root that includes an asio header also includes
           the guard header (the guard header itself excepted). It walks the directory
           itself, so no external tool can fail into an empty, clean list. A header
           "includes asio" when a line opens `#include <asio/` or `#include <asio.hpp`.
           With --expect-reported, it passes only if the reported set is exactly the
           given set: that is the seeded positive control's mode.

  census   Every compile_commands.json entry whose command line carries one of asio's
           include directories also carries `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>`, and
           only that value, with N read from the guard header. It fails closed when no entry
           carries asio's directory, or when a --require-source file is not among the
           entries that do. With --positive-control <source>, it strips the definition from
           that entry in memory and passes only if the check then reports exactly that
           entry.

Exit status: 0 the check holds; 1 it does not; 2 the instrument could not run.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import sys
from collections import Counter
from pathlib import Path

GUARD_REL = "fixpp/core/detail/asio_recycler_config.hpp"
MACRO = "ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE"
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".ipp", ".inl", ".tpp"}

ASIO_INCLUDE = re.compile(r'^\s*#\s*include\s*<asio(/|\.hpp\s*>)')
GUARD_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]' + re.escape(GUARD_REL) + r'[>"]')
GUARD_VALUE = re.compile(r"^#define FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE ([0-9]+)$")


class InstrumentError(Exception):
    """The check could not run; reported as exit 2, never as a pass."""


def read_guard_value(header: Path) -> str:
    try:
        text = header.read_text(encoding="utf-8")
    except OSError as exc:
        raise InstrumentError(f"cannot read the guard header {header}: {exc}") from exc
    values = [m.group(1) for line in text.splitlines() if (m := GUARD_VALUE.match(line))]
    if len(values) != 1:
        raise InstrumentError(
            f"{header}: expected exactly one `#define FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE "
            f"<n>` line, found {len(values)}")
    return values[0]


# ── headers ──────────────────────────────────────────────────────────────────

def header_report(root: Path) -> tuple[list[str], int, int]:
    """Returns (reported, scanned, asio_including), paths relative to root."""
    if not root.is_dir():
        raise InstrumentError(f"--include-root {root} is not a directory")
    reported: list[str] = []
    scanned = 0
    including = 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            path = Path(dirpath) / name
            if path.suffix not in HEADER_SUFFIXES:
                continue
            rel = path.relative_to(root).as_posix()
            scanned += 1
            try:
                lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            except OSError as exc:
                raise InstrumentError(f"cannot read {path}: {exc}") from exc
            if rel == GUARD_REL or not any(ASIO_INCLUDE.match(line) for line in lines):
                continue
            including += 1
            if not any(GUARD_INCLUDE.match(line) for line in lines):
                reported.append(rel)
    if scanned == 0:
        raise InstrumentError(f"no header under {root}")
    return reported, scanned, including


def cmd_headers(args: argparse.Namespace) -> int:
    reported, scanned, including = header_report(Path(args.include_root))
    print(f"[asio-recycler-guard] headers: scanned {scanned}, include asio {including}, "
          f"missing the guard {len(reported)}")
    for rel in reported:
        print(f"[asio-recycler-guard] MISSING the guard include: {rel}")
    if args.expect_reported is not None:
        expected = sorted(args.expect_reported)
        if sorted(reported) != expected:
            print(f"[asio-recycler-guard] FAIL: the seeded set {expected} was not reported "
                  f"exactly; reported {sorted(reported)}")
            return 1
        print("[asio-recycler-guard] PASS: the seeded headers, and only they, were reported")
        return 0
    if including == 0:
        raise InstrumentError(f"no header under {args.include_root} includes asio")
    if reported:
        print("[asio-recycler-guard] FAIL: each header above includes asio but not "
              f"<{GUARD_REL}>")
        return 1
    print("[asio-recycler-guard] PASS")
    return 0


# ── census ───────────────────────────────────────────────────────────────────

def _norm(p: str, base: str) -> str:
    return os.path.normcase(os.path.realpath(os.path.join(base, p)))


def entry_tokens(entry: dict) -> list[str]:
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"], posix=(os.name != "nt"))


INCLUDE_FLAGS = ("-isystem", "-I", "-iquote", "-idirafter", "/I", "-external:I", "/external:I")


def entry_includes_and_values(tokens: list[str], base: str) -> tuple[set[str], list[str]]:
    dirs: set[str] = set()
    values: list[str] = []
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        for flag in INCLUDE_FLAGS:
            if tok == flag and i + 1 < len(tokens):
                dirs.add(_norm(tokens[i + 1], base))
                i += 1
                break
            if tok.startswith(flag) and len(tok) > len(flag):
                dirs.add(_norm(tok[len(flag):], base))
                break
        define = None
        if tok in ("-D", "/D") and i + 1 < len(tokens):
            define = tokens[i + 1]
            i += 1
        elif tok.startswith(("-D", "/D")) and len(tok) > 2:
            define = tok[2:]
        if define is not None:
            name, _, value = define.partition("=")
            if name == MACRO:
                values.append(value)
        i += 1
    return dirs, values


def target_of(tokens: list[str]) -> str:
    for i, tok in enumerate(tokens):
        out = tokens[i + 1] if tok == "-o" and i + 1 < len(tokens) else (
            tok[3:] if tok.startswith("/Fo") else None)
        if out:
            m = re.search(r"CMakeFiles[/\\]([^/\\]+)\.dir[/\\]", out)
            if m:
                return m.group(1)
    return "<unknown target>"


def census(entries: list[dict], asio_dirs: set[str], n: str) -> tuple[list[dict], list[str]]:
    """Returns (matched entries, a failure line per entry that does not carry exactly N)."""
    matched: list[dict] = []
    failures: list[str] = []
    for entry in entries:
        tokens = entry_tokens(entry)
        dirs, values = entry_includes_and_values(tokens, entry.get("directory", "."))
        if not dirs & asio_dirs:
            continue
        matched.append(entry)
        if set(values) != {n}:
            failures.append(f"{entry['file']} ({target_of(tokens)}): {MACRO} values "
                            f"{values or '[absent]'}, expected exactly [{n}]")
    return matched, failures


def strip_definition(entry: dict) -> dict:
    out = dict(entry)
    pattern = re.compile(r"[-/]D\s*" + MACRO + r"(=\S*)?")
    if "arguments" in entry:
        out["arguments"] = [a for a in entry["arguments"]
                            if not re.fullmatch(r"[-/]D" + MACRO + r"(=.*)?", a)]
    else:
        out["command"] = pattern.sub("", entry["command"])
    return out


def cmd_census(args: argparse.Namespace) -> int:
    n = read_guard_value(Path(args.guard_header))
    try:
        entries = json.loads(Path(args.compile_commands).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise InstrumentError(f"cannot read {args.compile_commands}: {exc}") from exc
    if not entries:
        raise InstrumentError(f"{args.compile_commands} holds no entry")
    asio_dirs = {_norm(d, ".") for d in args.asio_include.replace("|", ";").split(";") if d}
    if not asio_dirs:
        raise InstrumentError("--asio-include is empty")

    if args.positive_control:
        idx = [i for i, e in enumerate(entries)
               if _norm(e["file"], e.get("directory", ".")).endswith(
                   os.path.normcase(os.path.normpath(args.positive_control)))]
        if len(idx) != 1:
            raise InstrumentError(f"--positive-control {args.positive_control} matches "
                                  f"{len(idx)} entries, expected 1")
        entries = list(entries)
        entries[idx[0]] = strip_definition(entries[idx[0]])
        _, failures = census(entries, asio_dirs, n)
        print(f"[asio-recycler-census] positive control: stripped {MACRO} from "
              f"{entries[idx[0]]['file']}; the check reported {len(failures)} entries")
        for f in failures:
            print(f"[asio-recycler-census]   {f}")
        if len(failures) == 1 and failures[0].startswith(entries[idx[0]]["file"] + " "):
            print("[asio-recycler-census] PASS: the stripped entry, and only it, was reported")
            return 0
        print("[asio-recycler-census] FAIL: the check did not report exactly the stripped entry")
        return 1

    matched, failures = census(entries, asio_dirs, n)
    print(f"[asio-recycler-census] N={n} from {args.guard_header}; {len(entries)} entries, "
          f"{len(matched)} carry asio's include directory")
    if not matched:
        raise InstrumentError("no entry carries asio's include directory "
                              f"({sorted(asio_dirs)}); the census would examine nothing")
    by_target = Counter(target_of(entry_tokens(e)) for e in matched)
    for tgt, count in sorted(by_target.items()):
        print(f"[asio-recycler-census]   {tgt}: {count}")
    matched_files = {_norm(e["file"], e.get("directory", ".")) for e in matched}
    for req in args.require_source or []:
        want = os.path.normcase(os.path.realpath(req))
        if want not in matched_files:
            print(f"[asio-recycler-census] FAIL: {req} is not among the entries that carry "
                  "asio's include directory")
            return 1
    for f in failures:
        print(f"[asio-recycler-census] MISMATCH {f}")
    if failures:
        return 1
    print(f"[asio-recycler-census] PASS: every entry that carries asio's include directory "
          f"carries {MACRO}={n} and no other value")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("headers")
    h.add_argument("--include-root", required=True)
    h.add_argument("--expect-reported", nargs="*")
    c = sub.add_parser("census")
    c.add_argument("--compile-commands", required=True)
    c.add_argument("--asio-include", required=True,
                   help="asio's include directories, separated by ';' or '|'")
    c.add_argument("--guard-header", required=True)
    c.add_argument("--require-source", action="append")
    c.add_argument("--positive-control")
    args = parser.parse_args()
    try:
        return cmd_headers(args) if args.cmd == "headers" else cmd_census(args)
    except InstrumentError as exc:
        print(f"[asio-recycler-guard] ERROR (the instrument did not run): {exc}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
