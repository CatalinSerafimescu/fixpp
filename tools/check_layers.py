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
    # 044-toml-session-config: opt-in adjacent loader target (fixpp_config_toml).
    # Nothing in core or session links it; it links them — not the reverse.
    # 045-observability-config: config → log added (logger/sink headers); the
    # conditional fixpp_log_otlp link is a CMake-only edge, not an include edge.
    "config":     {"core", "session", "dictionary", "tls", "transport", "log"},
    "capi":       {"session", "wire", "dictionary", "transport", "tls", "log",
                   "otel", "tap", "core"},
    "service":    {"capi", "service"},   # service may include fixpp/service/ (own interface)
    "python":     {"capi"},
    "c":          {"capi"},
}

# Explicit FORBIDDEN edges per [arch §2.3] (017-log-otel D1 remediation).
# Negative-edge assertions that must hold REGARDLESS of the ALLOWED whitelist
# above. `otel ↛ transport`: the otel module is an observability leaf (depends
# only on core + log) and must NEVER include from transport. Today the
# constraint holds implicitly (transport is absent from otel's ALLOWED set), but
# a future edit that adds "transport" to otel's ALLOWED set would silently
# re-permit the edge. Encoding it here makes the negative edge explicit and
# grep-able; the self-consistency check in main() fails fast if ALLOWED ever
# widens to contain a FORBIDDEN edge.
FORBIDDEN: dict[str, set[str]] = {
    "otel": {"transport"},
}

# Regex to match #include "fixpp/<module>/..." or #include <fixpp/<module>/...>
INCLUDE_RE = re.compile(r'#\s*include\s+[<"](fixpp/(\w+)/[^>"]*)[>"]')

# Some module directory names differ from their include-path namespace.
# `dictionary/` ships its public surface under `fixpp/dict/` for ergonomic
# C++ namespaces (`fixpp::dict`) per `[arch §4.2]` / 002-dictionary-xml-loader.
# Normalize the include-path name to the architectural module name so the
# allowed-edge whitelist agrees with the source-layout module.
INCLUDE_NAMESPACE_ALIASES: dict[str, str] = {
    "dict": "dictionary",
}

# Also detect includes of fix/c_api.h (allowed everywhere except in engine internals)
CAPI_RE = re.compile(r'#\s*include\s+[<"](fix/c_api[^>"]*)[>"]')

# ── dictionary↔wire BRIDGE-SURFACE carve-out — [arch §2.4] RC#3 amendment ────
# (2026-05-15, [const §XX]; applied at 003-dictionary-codegen re-/plan.)
# The bridge surface compiles against BOTH `dictionary` and `wire` and is NOT
# a `dictionary` module edge — it is header-only template glue
# (wire::MessageView<Index> is a compile-time template parameter); no
# `dictionary` .cpp links `wire`, so the acyclic graph of [arch §2.3] is
# preserved. The generated fixpp::vXX::* tree is already covered by the §2.4
# carve-out and lives in the build tree (not scanned). The hand-written bridge
# headers + the vendored frozen wire-contract stub are exempted HERE so a
# scanned src/dictionary bridge .cpp (e.g. an out-of-line reify.cpp that pulls
# the wire contract) is classified as bridge, not as a `dictionary → wire`
# violation. This is an EXACT documented file-list — it does not weaken the
# `dictionary | core` whitelist for any other dict/ header.
#
# NOTE: this scanner walks src/ + bindings/ only (not include/), so the
# hand-written include/fixpp/dict/reify.hpp / field_traits.hpp are not directly
# scanned today; the load-bearing RC#3 resolution is the [arch §2.4] rule.
# Extending the scan to include/ is tracked as a non-blocking follow-up
# (plan.md "Re-/plan (RC resolution)" → RC#3); when it lands, these same
# exempt paths apply.
BRIDGE_EXEMPT_INCLUDES: set[str] = {
    "fixpp/wire/message_view_contract.hpp",  # vendored frozen R6 stub
}
# Source files that ARE the dict↔wire bridge (relative to ROOT). A bridge file
# may include the wire-contract stub above without it counting as a violation.
BRIDGE_SOURCE_FILES: set[str] = {
    "src/dictionary/reify.cpp",            # out-of-line reify bridge (if any; D-5)
    "src/dictionary/version_registry.cpp",  # out-of-line registry bits (if any; D-5)
    "src/dictionary/field_traits.cpp",     # out-of-line field_traits bridge (RC#1/RC#3; D-5)
                                            # field_traits.cpp implements from_field_view
                                            # which takes wire::field_view — uses the vendored
                                            # frozen stub via message_view_contract.hpp
}


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

    # Is this file part of the [arch §2.4] dict↔wire bridge surface?
    try:
        rel = path.relative_to(ROOT).as_posix()
    except ValueError:
        rel = path.as_posix()
    is_bridge_source = rel in BRIDGE_SOURCE_FILES

    with open(path, encoding="utf-8", errors="replace") as f:
        for lineno, line in enumerate(f, 1):
            m = INCLUDE_RE.search(line)
            if not m:
                continue
            included_module = m.group(2)
            included_module = INCLUDE_NAMESPACE_ALIASES.get(included_module, included_module)
            if included_module == module:
                continue  # self-include is always allowed
            # [arch §2.4] RC#3 bridge carve-out: a bridge source file may pull
            # the exempt wire-contract include without it counting as an edge.
            if is_bridge_source and m.group(1) in BRIDGE_EXEMPT_INCLUDES:
                continue
            if included_module not in allowed_modules:
                violations.append(
                    f"{path}:{lineno}: module '{module}' includes "
                    f"'{m.group(1)}' (module '{included_module}'), "
                    f"which is not in its allowed-edge whitelist {sorted(allowed_modules)}"
                )


def main() -> int:
    violations: list[str] = []

    # Self-consistency guard ([arch §2.3] negative edges; 017 D1): no FORBIDDEN
    # edge may appear in the ALLOWED whitelist. Catches an accidental widening
    # (e.g. someone adding "transport" to otel's ALLOWED set) at the source,
    # before any file is even scanned.
    for mod, banned in FORBIDDEN.items():
        leaked = banned & ALLOWED.get(mod, set())
        if leaked:
            print(f"[check_layers] FAIL — module '{mod}' ALLOWED set contains "
                  f"FORBIDDEN edge(s) {sorted(leaked)} ([arch §2.3] negative edge)")
            return 1

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
