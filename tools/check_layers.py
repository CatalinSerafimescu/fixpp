#!/usr/bin/env python3
"""tools/check_layers.py — Architecture layer enforcement.

Walks src/ and bindings/ looking for #include lines that violate the
allowed-edge whitelist from [arch §2.3].

Exits 0 if clean, 1 if any violation is found.
"""

import re
import sys
from pathlib import Path

# Root of the library source (tools/ is a sibling of src/)
ROOT = Path(__file__).parent.parent

# Allowed include edges per [arch §2.3]
# Key = module name; Value = set of modules it may include from.
# An empty set means the module may include from nothing project-internal.
ALLOWED: dict[str, set[str]] = {
    "core":       set(),
    "dictionary": {"core"},
    "wire":       {"core", "dictionary"},
    "tls":        {"core"},
    "transport":  {"core", "tls", "log"},
    "log":        {"core"},
    "otel":       {"core", "log"},
    "tap":        {"core", "wire", "log"},
    "session":    {"core", "dictionary", "wire", "transport", "log", "otel"},
    "capi":       {"session", "wire", "dictionary", "transport", "tls", "log",
                   "otel", "tap", "core"},
    "service":    {"capi", "service"},   # service may include fixpp/service/ (own interface)
    "python":     {"capi"},
    "c":          {"capi"},
}

# Regex to match #include "fixpp/<module>/..." or #include <fixpp/<module>/...>
INCLUDE_RE = re.compile(r'#\s*include\s+[<"](fixpp/(\w+)/[^>"]*)[>"]')

# Also detect includes of fix/c_api.h (allowed everywhere except in engine internals)
CAPI_RE = re.compile(r'#\s*include\s+[<"](fix/c_api[^>"]*)[>"]')


def module_of_path(path: Path) -> str | None:
    """Return the module name for a source file, or None if unknown."""
    parts = path.parts
    # src/<module>/...
    try:
        src_idx = parts.index("src")
        return parts[src_idx + 1]
    except (ValueError, IndexError):
        pass
    # bindings/python/... or bindings/c/...
    try:
        bind_idx = parts.index("bindings")
        return parts[bind_idx + 1]
    except (ValueError, IndexError):
        pass
    return None


def check_file(path: Path, violations: list[str]) -> None:
    module = module_of_path(path)
    if module is None:
        return  # not in a known module directory

    allowed_modules = ALLOWED.get(module, set())

    with open(path, encoding="utf-8", errors="replace") as f:
        for lineno, line in enumerate(f, 1):
            m = INCLUDE_RE.search(line)
            if not m:
                continue
            included_module = m.group(2)
            if included_module == module:
                continue  # self-include is always allowed
            if included_module not in allowed_modules:
                violations.append(
                    f"{path}:{lineno}: module '{module}' includes "
                    f"'{m.group(1)}' (module '{included_module}'), "
                    f"which is not in its allowed-edge whitelist {sorted(allowed_modules)}"
                )


def main() -> int:
    violations: list[str] = []

    # Scan src/ and bindings/
    for pattern in ("src/**/*.cpp", "src/**/*.hpp", "src/**/*.h",
                    "bindings/**/*.cpp", "bindings/**/*.hpp", "bindings/**/*.h"):
        for path in sorted(ROOT.glob(pattern)):
            check_file(path, violations)

    if violations:
        print(f"[check_layers] FAIL — {len(violations)} layer violation(s):")
        for v in violations:
            print(f"  {v}")
        return 1

    print("[check_layers] OK — no layer violations found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
