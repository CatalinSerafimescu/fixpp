#!/usr/bin/env python3
"""Print the compile-flag surface a ccache lane's tag is keyed on (#482).

    ci/ccache-flag-surface.py host <preset>   # a CMakePresets.json lane
    ci/ccache-flag-surface.py wheel <lane>    # a container (wheel) lane

Run from the library root. Prints a normalized text; ci/ccache-cache-key.sh
(`ccache_flag_digest`) hashes it into the tag. Exits non-zero, printing why on
stderr, when any input cannot be read or parsed. A partial surface would be a
STABLE WRONG key that restore and seed agree on, so there is no fallback.

── WHY A NORMALIZED EXTRACT, NOT THE FILES' BYTES ─────────────────────────────

The tag must rotate when a flag moves (else the restore HITs, every entry
misses and the 70 % floor in ci/ccache-stats.sh fails the lane) and must NOT
rotate on a comment or whitespace edit (else every such edit costs each lane a
cold re-seed). So the files are parsed, comments and layout are dropped, and
only the commands that can change a compile command line are kept:

  * CMake — the root CMakeLists.txt and every cmake/*.cmake (the root-scope
    modules): flag commands (FLAG_COMMANDS), `set`/`unset`/`string`/`list` of a
    flag variable (FLAG_VAR), property commands naming a flag property,
    `option()` as name + default (its docstring is dropped), `include()`,
    top-level `return()`, and any command that CALLS a flag-bearing function.
    Each is emitted with every head of every enclosing if/elseif/else, loop
    and block, so an edit to a GATING CONDITION rotates the tag too.
    A function or macro is flag-bearing if its body holds any of the above or
    calls another flag-bearing one (fixpoint); its WHOLE normalized body is
    emitted, because control flow inside it (`return()`, `continue()`, an
    exemption check) decides where its flags land.
  * host lanes — the preset's cacheVariables and environment resolved through
    `inherits`, and conan/profiles/<preset> with comments dropped (its
    `tools.build:cxxflags` reach every first-party TU via the toolchain).
  * the wheel lane — the scikit-build and cibuildwheel keys of
    bindings/python/pyproject.toml that feed the CMake command line.

── WHAT IS NOT COVERED — a CONDITION to re-check, not an inventory ────────────

Anything outside those files: flags set in a subdirectory CMakeLists (including
a PUBLIC definition that propagates to dependents), flags reaching a flag
command through a non-flag variable set elsewhere, `-D`/environment passed by a
workflow step, Conan settings passed on a command line (the wheel's
cibw-before-all.sh), and header CONTENT. When a floored lane breaches on a HIT,
check whether its cause lies in one of those; if it does, widen the surface
here rather than dropping GHCR tags by hand.
"""

import json
import re
import sys
from pathlib import Path

FLAG_COMMANDS = {
    "add_compile_options", "add_compile_definitions", "add_definitions",
    "remove_definitions", "target_compile_options",
    "target_compile_definitions", "target_compile_features",
}
FLAG_VAR = re.compile(
    r"^CMAKE_(?:(?:C|CXX)_(?:STANDARD|STANDARD_REQUIRED|EXTENSIONS|FLAGS\w*|"
    r"VISIBILITY_PRESET|SCAN_FOR_MODULES)|POSITION_INDEPENDENT_CODE|"
    r"VISIBILITY_INLINES_HIDDEN|INTERPROCEDURAL_OPTIMIZATION\w*|BUILD_TYPE)$")
FLAG_PROPERTY = re.compile(
    r"^(?:COMPILE_OPTIONS|COMPILE_DEFINITIONS|COMPILE_FLAGS|"
    r"POSITION_INDEPENDENT_CODE|(?:C|CXX)_(?:STANDARD|EXTENSIONS))$")
PROPERTY_COMMANDS = {"set_property", "set_target_properties",
                     "set_directory_properties", "set_source_files_properties"}
OPENERS = {"if": "endif", "foreach": "endforeach", "while": "endwhile",
           "block": "endblock", "function": "endfunction", "macro": "endmacro"}
CLOSERS = set(OPENERS.values())
BRANCHES = {"elseif", "else"}


class SurfaceError(Exception):
    pass


# ── CMake tokenizer ──────────────────────────────────────────────────────────

def _bracket_close(text, i):
    """If text[i:] opens a bracket `[=*[`, return (end index after the
    matching close, close string); else None."""
    m = re.match(r"\[(=*)\[", text[i:])
    if not m:
        return None
    close = "]" + m.group(1) + "]"
    end = text.find(close, i + m.end())
    if end < 0:
        raise SurfaceError("unterminated bracket argument or comment")
    return end + len(close)


def parse_cmake(text, where):
    """Return [(name_lower, [arg tokens])], comments and layout dropped.

    Quoted and bracket arguments are kept verbatim; unquoted text is split on
    whitespace; nested parentheses are kept as tokens. Unbalanced input raises.
    """
    cmds = []
    i, n = 0, len(text)
    try:
        while i < n:
            c = text[i]
            if c.isspace():
                i += 1
            elif c == "#":
                end = _bracket_close(text, i + 1)
                if end is not None:
                    i = end
                else:
                    nl = text.find("\n", i)
                    i = n if nl < 0 else nl + 1
            else:
                m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[i:])
                if not m:
                    raise SurfaceError(f"unexpected character {c!r}")
                name = m.group(0).lower()
                i += m.end()
                while i < n and text[i] in " \t":
                    i += 1
                if i >= n or text[i] != "(":
                    raise SurfaceError(f"command '{name}' has no '('")
                i += 1
                args, depth, tok = [], 1, ""
                while True:
                    if i >= n:
                        raise SurfaceError(f"unbalanced '(' in '{name}'")
                    c = text[i]
                    if c == "#":
                        end = _bracket_close(text, i + 1)
                        if end is not None:
                            i = end
                        else:
                            nl = text.find("\n", i)
                            i = n if nl < 0 else nl + 1
                        if tok:
                            args.append(tok)
                            tok = ""
                        continue
                    if c == '"':
                        j = i + 1
                        while j < n and text[j] != '"':
                            j += 2 if text[j] == "\\" else 1
                        if j >= n:
                            raise SurfaceError(f"unterminated quote in '{name}'")
                        tok += text[i:j + 1]
                        i = j + 1
                        continue
                    if c == "[" and not tok:
                        end = _bracket_close(text, i)
                        if end is not None:
                            args.append(text[i:end])
                            i = end
                            continue
                    if c.isspace() or c in "()":
                        if tok:
                            args.append(tok)
                            tok = ""
                        if c == "(":
                            depth += 1
                            args.append("(")
                        elif c == ")":
                            depth -= 1
                            if depth == 0:
                                i += 1
                                break
                            args.append(")")
                        i += 1
                        continue
                    if c == "\\" and i + 1 < n:
                        tok += text[i:i + 2]
                        i += 2
                        continue
                    tok += c
                    i += 1
                cmds.append((name, args))
    except SurfaceError as e:
        raise SurfaceError(f"{where}: {e}") from None
    return cmds


def fmt(cmd):
    name, args = cmd
    if name == "option" and len(args) >= 1:
        # Name + default only: the docstring is prose, not a flag.
        default = args[2] if len(args) >= 3 else "OFF"
        return f"option {args[0]} {default}"
    return " ".join([name] + args)


def split_blocks(cmds, where):
    """Nest the flat command list: a block is (head, [branch heads], body
    items). Body items are commands or nested blocks."""
    root = []
    stack = [(None, root)]
    for cmd in cmds:
        name = cmd[0]
        if name in OPENERS:
            block = {"head": cmd, "close": OPENERS[name], "items": []}
            stack[-1][1].append(block)
            stack.append((block, block["items"]))
        elif name in BRANCHES:
            if stack[-1][0] is None or stack[-1][0]["head"][0] != "if":
                raise SurfaceError(f"{where}: '{name}' outside an if()")
            stack[-1][1].append(("__branch__", cmd))
        elif name in CLOSERS:
            if stack[-1][0] is None or stack[-1][0]["close"] != name:
                raise SurfaceError(f"{where}: unmatched '{name}'")
            stack.pop()
        else:
            stack[-1][1].append(cmd)
    if len(stack) != 1:
        raise SurfaceError(f"{where}: unterminated '{stack[-1][0]['head'][0]}'")
    return root


def flat_commands(items):
    for it in items:
        if isinstance(it, dict):
            yield it["head"]
            yield from flat_commands(it["items"])
        elif it[0] == "__branch__":
            yield it[1]
        else:
            yield it


def is_direct_flag(cmd):
    name, args = cmd
    if name in FLAG_COMMANDS or name in ("option", "include"):
        return True
    if name in ("set", "unset") and args and FLAG_VAR.match(args[0]):
        return True
    if name in ("string", "list") and len(args) >= 2 and FLAG_VAR.match(args[1]):
        return True
    if name in PROPERTY_COMMANDS and any(FLAG_PROPERTY.match(a) for a in args):
        return True
    return False


def calls_any(cmd, fns):
    # A direct call, or a name passed as an argument (cmake_language(CALL|DEFER)).
    return cmd[0] in fns or any(a.lower() in fns for a in cmd[1])


def cmake_surface(files):
    trees = []
    for path in files:
        try:
            text = Path(path).read_text(encoding="utf-8")
        except OSError as e:
            raise SurfaceError(f"{path}: {e.strerror}") from None
        trees.append((path, split_blocks(parse_cmake(text, path), path)))

    # Every function/macro definition, wherever it sits.
    defs = {}

    def collect(items):
        for it in items:
            if isinstance(it, dict):
                if it["head"][0] in ("function", "macro") and it["head"][1]:
                    defs[it["head"][1][0].lower()] = it
                collect(it["items"])
    for _, tree in trees:
        collect(tree)

    flagfns = set()
    changed = True
    while changed:
        changed = False
        for fname, block in defs.items():
            if fname in flagfns:
                continue
            body = list(flat_commands(block["items"]))
            if any(is_direct_flag(c) or calls_any(c, flagfns) for c in body):
                flagfns.add(fname)
                changed = True

    out = []

    def emit_block_whole(block, indent):
        out.append(indent + fmt(block["head"]))
        for it in block["items"]:
            if isinstance(it, dict):
                emit_block_whole(it, indent + "  ")
            elif it[0] == "__branch__":
                out.append(indent + fmt(it[1]))
            else:
                out.append(indent + "  " + fmt(it))
        out.append(indent + block["close"])

    def walk(path, items, ctx):
        heads = []  # branch heads seen so far in the enclosing if-chain
        for it in items:
            if isinstance(it, dict):
                head = it["head"]
                if head[0] in ("function", "macro"):
                    if head[1] and head[1][0].lower() in flagfns:
                        emit_block_whole(it, f"{path}: " + "".join(
                            h + " > " for h in ctx + heads))
                    continue
                walk(path, it["items"], ctx + heads + [fmt(head)])
            elif it[0] == "__branch__":
                # Only an if() body carries branch markers; every later branch
                # depends on every earlier head of the same chain.
                heads.append(fmt(it[1]))
            else:
                # Function bodies are never walked here, so a return() seen
                # here ends the FILE early and drops every flag after it.
                if (is_direct_flag(it) or calls_any(it, flagfns)
                        or it[0] == "return"):
                    out.append(f"{path}: " + " > ".join(ctx + heads + [fmt(it)]))

    for path, tree in trees:
        walk(path, tree, [])
    return out


# ── non-CMake inputs ─────────────────────────────────────────────────────────

def resolved_preset(preset):
    try:
        data = json.loads(Path("CMakePresets.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise SurfaceError(f"CMakePresets.json: {e}") from None
    by_name = {p.get("name"): p for p in data.get("configurePresets", [])}

    def resolve(name, seen):
        if name not in by_name:
            raise SurfaceError(f"CMakePresets.json: no configure preset '{name}'")
        if name in seen:
            raise SurfaceError(f"CMakePresets.json: inherits cycle at '{name}'")
        p = by_name[name]
        parents = p.get("inherits", [])
        if isinstance(parents, str):
            parents = [parents]
        cache, env = {}, {}
        # CMake: the preset's own value wins, then parents in listed order.
        for parent in reversed(parents):
            pc, pe = resolve(parent, seen | {name})
            cache.update(pc)
            env.update(pe)
        cache.update(p.get("cacheVariables") or {})
        env.update(p.get("environment") or {})
        return cache, env

    cache, env = resolve(preset, frozenset())
    return [
        "preset.cacheVariables " + json.dumps(cache, sort_keys=True),
        "preset.environment " + json.dumps(env, sort_keys=True),
    ]


def conan_profile(preset):
    path = Path("conan/profiles") / preset
    if not path.is_file():
        return [f"conan-profile {preset}: absent"]
    lines = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" in line:
            k, v = line.split("=", 1)
            line = k.strip() + "=" + " ".join(v.split())
        lines.append(f"conan-profile {preset}: {line}")
    return lines


def wheel_pyproject():
    import tomllib
    path = "bindings/python/pyproject.toml"
    try:
        with open(path, "rb") as f:
            cfg = tomllib.load(f)
    except (OSError, ValueError) as e:
        raise SurfaceError(f"{path}: {e}") from None
    tool = cfg.get("tool", {})
    skb = tool.get("scikit-build", {})
    cmake = skb.get("cmake", {})
    cibw = tool.get("cibuildwheel", {})
    picked = {
        "scikit-build.build-dir": skb.get("build-dir"),
        "scikit-build.cmake.source-dir": cmake.get("source-dir"),
        "scikit-build.cmake.build-type": cmake.get("build-type"),
        "scikit-build.cmake.define": cmake.get("define"),
        "scikit-build.cmake.args": cmake.get("args"),
        "cibuildwheel.config-settings": cibw.get("config-settings"),
        "cibuildwheel.environment": cibw.get("environment"),
        "cibuildwheel.linux.environment": cibw.get("linux", {}).get("environment"),
    }
    return ["pyproject " + json.dumps(picked, sort_keys=True)]


def main(argv):
    if len(argv) != 3 or argv[1] not in ("host", "wheel") or not argv[2]:
        print("usage: ccache-flag-surface.py host <preset> | wheel <lane>",
              file=sys.stderr)
        return 2
    kind, name = argv[1], argv[2]
    files = ["CMakeLists.txt"] + sorted(str(p) for p in Path("cmake").glob("*.cmake"))
    try:
        lines = cmake_surface(files)
        if kind == "host":
            lines += resolved_preset(name) + conan_profile(name)
        else:
            lines += wheel_pyproject()
    except SurfaceError as e:
        print(f"ccache-flag-surface: {e}", file=sys.stderr)
        return 1
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
