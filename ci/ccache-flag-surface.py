#!/usr/bin/env python3
"""Print the compile-flag surface a ccache lane's tag is keyed on (#482).

    ci/ccache-flag-surface.py host <preset>   # a CMakePresets.json lane
    ci/ccache-flag-surface.py wheel <lane>    # a container (wheel) lane
    ci/ccache-flag-surface.py --list-inputs   # the lane-independent input files

Run from the library root. Prints a normalized text; ci/ccache-cache-key.sh
(`ccache_flag_digest`) hashes it into the tag. Exits non-zero, printing why on
stderr, when any input cannot be read or parsed. A partial surface would be a
STABLE WRONG key that restore and seed agree on, so there is no fallback.
`--list-inputs` prints the files every lane's extract reads (the per-lane
preset and Conan profile are not in it), so a harness can copy exactly those.

── WHY A NORMALIZED EXTRACT, NOT THE FILES' BYTES ─────────────────────────────

The tag must rotate when a flag moves (else the restore HITs, every entry
misses and the 70 % floor in ci/ccache-stats.sh fails the lane) and must NOT
rotate on a comment or whitespace edit (else every such edit costs each lane a
cold re-seed). So the files are parsed, comments and layout are dropped, and
only the commands that can change a compile command line are kept:

  * CMake — the root CMakeLists.txt, every cmake/*.cmake (the root-scope
    modules), and every CMakeLists.txt an `add_subdirectory()` reaches from the
    root, followed whatever `if()` encloses it. Kept: flag commands
    (FLAG_COMMANDS), `set`/`unset`/`string`/`list` of a flag variable
    (FLAG_VAR), property commands naming a flag property, `option()` as name +
    default (its docstring is dropped), `include()`, top-level `return()`, and
    any command that CALLS a flag-bearing function.
    Each is emitted with every head of every enclosing if/elseif/else, loop
    and block, so an edit to a GATING CONDITION rotates the tag too.
    A function or macro is flag-bearing if its body holds any of the above or
    calls another flag-bearing one (fixpoint); its WHOLE normalized body is
    emitted, because control flow inside it (`return()`, `continue()`, an
    exemption check) decides where its flags land.
    VARIABLE CLOSURE: every variable a kept line reads — as `${VAR}`, or as a
    bare name in an if/elseif/while head — pulls in each directory-scope
    `set`/`unset`/`option` of VAR and each `string`/`list` naming VAR, and so
    on to a fixpoint, so a default set in one file and handed to a flag
    command in another rotates the tag. A `set(... CACHE <type> <docstring>)`
    drops its docstring, as `option()` does.
  * host lanes — the preset's cacheVariables and environment resolved through
    `inherits`, and conan/profiles/<preset> with comments dropped (its
    `tools.build:cxxflags` reach every first-party TU via the toolchain).
  * the wheel lane — the scikit-build and cibuildwheel keys of
    bindings/python/pyproject.toml that feed the CMake command line;
    bindings/python/cibw-before-all.sh with whole-line comments and blank lines
    dropped (its `conan install` settings generate the wheel's toolchain file);
    and the `CIBW_ENVIRONMENT` value of the `id: wheel_build` step in
    .github/workflows/tier1.yml, read alone so the rest of that workflow never
    rotates the wheel tag.

── WHAT IS NOT COVERED — a CONDITION to re-check, not an inventory ────────────

A variable written other than by `set`/`unset`/`option`/`string`/`list` (a
value computed by another command, or inside a function that is not itself
kept), a CMake file reached other than through `include()` of cmake/*.cmake or
`add_subdirectory()`, `-D`/environment passed by a workflow step other than the
wheel's `CIBW_ENVIRONMENT`, and header CONTENT.
A trailing comment on a cibw-before-all.sh command line is hashed as part of
that line, which only over-rotates. When a floored lane breaches on a HIT,
check whether its cause lies in one of those; if it does, widen the surface
here rather than dropping GHCR tags by hand.
"""

import json
import posixpath
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
VAR_REF = re.compile(r"\$\{([^${}]+)\}")
BARE_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
CIBW_BEFORE_ALL = "bindings/python/cibw-before-all.sh"
WHEEL_WORKFLOW = ".github/workflows/tier1.yml"
PYPROJECT = "bindings/python/pyproject.toml"


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
    if name == "set" and "CACHE" in args[1:]:
        # set(<var> <value>... CACHE <type> <docstring> [FORCE]): the
        # docstring is prose, not a flag.
        c = args.index("CACHE", 1)
        args = args[:c + 2] + args[c + 3:]
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


def assigns_any(cmd, names):
    """A directory-scope write to a variable in `names` (the closure)."""
    name, args = cmd
    if name in ("set", "unset", "option"):
        return bool(args) and args[0] in names
    if name in ("string", "list"):
        # The output variable's position depends on the sub-command.
        return any(a in names for a in args)
    return False


def read_text(path):
    """A file's text, or SurfaceError. A leading UTF-8 BOM is dropped (CMake
    accepts one); any other undecodable byte is a failure, never a traceback."""
    try:
        return Path(path).read_text(encoding="utf-8-sig")
    except OSError as e:
        raise SurfaceError(f"{path}: {e.strerror}") from None
    except UnicodeDecodeError as e:
        raise SurfaceError(f"{path}: not UTF-8 ({e.reason} at byte {e.start})") from None


def cmake_files():
    """[(path, tree)]: the root CMakeLists.txt, cmake/*.cmake, then every
    CMakeLists.txt reached through add_subdirectory() from the root.

    add_subdirectory() is FOLLOWED rather than the tree globbed: a glob would
    also see a build directory's _deps/, which exists when seed runs and not
    when restore runs, and the two must compute the same key."""
    files = [("CMakeLists.txt", None)] + [
        (str(p), None) for p in sorted(Path("cmake").glob("*.cmake"))]
    trees = []
    for path, _ in files:
        trees.append((path, split_blocks(parse_cmake(read_text(path), path), path)))
    seen = {path for path, _ in trees}
    i = 0
    while i < len(trees):
        path, tree = trees[i]
        i += 1
        if path != "CMakeLists.txt" and not path.endswith("/CMakeLists.txt"):
            continue  # a cmake/*.cmake module is include()d, not a directory
        for name, args in flat_commands(tree):
            if name != "add_subdirectory":
                continue
            if not args:
                raise SurfaceError(f"{path}: add_subdirectory() with no directory")
            sub = args[0].strip('"')
            if "$" in sub or posixpath.isabs(sub):
                raise SurfaceError(
                    f"{path}: add_subdirectory({args[0]}) is not a literal "
                    "relative path, so the files it reaches cannot be listed")
            child = posixpath.normpath(
                posixpath.join(posixpath.dirname(path), sub, "CMakeLists.txt"))
            if child.startswith("../"):
                raise SurfaceError(f"{path}: add_subdirectory({args[0]}) leaves the tree")
            if child in seen:
                continue
            seen.add(child)
            trees.append((child, split_blocks(parse_cmake(read_text(child), child), child)))
    return trees


def cmake_surface(trees):
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

    def reads(cmd, refs):
        """Add the variables `cmd` reads: every `${VAR}`, and, in an
        if/elseif/while head, every argument that is a bare name (CMake
        dereferences those itself)."""
        for a in cmd[1]:
            refs.update(m.group(1) for m in VAR_REF.finditer(a))
            if cmd[0] in ("if", "elseif", "while") and BARE_NAME.fullmatch(a):
                refs.add(a)

    def emit_block_whole(block, indent, out, refs):
        out.append(indent + fmt(block["head"]))
        reads(block["head"], refs)
        for it in block["items"]:
            if isinstance(it, dict):
                emit_block_whole(it, indent + "  ", out, refs)
            elif it[0] == "__branch__":
                out.append(indent + fmt(it[1]))
                reads(it[1], refs)
            else:
                out.append(indent + "  " + fmt(it))
                reads(it, refs)
        out.append(indent + block["close"])

    def walk(path, items, ctx, closure, out, refs):
        heads = []  # branch heads seen so far in the enclosing if-chain
        for it in items:
            if isinstance(it, dict):
                head = it["head"]
                if head[0] in ("function", "macro"):
                    if head[1] and head[1][0].lower() in flagfns:
                        for h in ctx + heads:
                            reads(h, refs)
                        emit_block_whole(it, f"{path}: " + "".join(
                            fmt(h) + " > " for h in ctx + heads), out, refs)
                    continue
                walk(path, it["items"], ctx + heads + [head], closure, out, refs)
            elif it[0] == "__branch__":
                # Only an if() body carries branch markers; every later branch
                # depends on every earlier head of the same chain.
                heads.append(it[1])
            else:
                # Function bodies are never walked here, so a return() seen
                # here ends the FILE early and drops every flag after it.
                if (is_direct_flag(it) or calls_any(it, flagfns)
                        or it[0] == "return" or assigns_any(it, closure)):
                    for c in ctx + heads + [it]:
                        reads(c, refs)
                    out.append(f"{path}: " + " > ".join(
                        fmt(c) for c in ctx + heads + [it]))

    # The variable closure: walk, collect every variable the kept lines read,
    # walk again keeping the writes to those, until no new variable appears.
    closure = set()
    while True:
        out, refs = [], set()
        for path, tree in trees:
            walk(path, tree, [], closure, out, refs)
        if refs <= closure:
            return out
        closure |= refs


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
    for raw in read_text(path).splitlines():
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
    path = PYPROJECT
    try:
        cfg = tomllib.loads(read_text(path))
    except ValueError as e:
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


def wheel_before_all():
    """cibw-before-all.sh minus whole-line comments, blank lines and layout."""
    lines = []
    for raw in read_text(CIBW_BEFORE_ALL).splitlines():
        line = " ".join(raw.split())
        if line and not line.startswith("#"):
            lines.append(f"cibw-before-all: {line}")
    return lines


def wheel_cibw_environment():
    """The CIBW_ENVIRONMENT value of the `id: wheel_build` step, read by text
    so the extractor needs no YAML module and nothing else in the workflow
    reaches the digest. Exactly one such step and one such key, on one line,
    or a failure: a second key is the duplicate-key hazard the workflow warns
    about, and a block scalar is a shape this reader does not parse."""
    text = read_text(WHEEL_WORKFLOW).splitlines()
    ids = [i for i, l in enumerate(text) if re.match(r"^\s*(?:-\s+)?id:\s*wheel_build\s*$", l)]
    if len(ids) != 1:
        raise SurfaceError(f"{WHEEL_WORKFLOW}: expected one 'id: wheel_build' step")
    start = ids[0]
    # The step's own keys sit at the id line's indent; a line indented less
    # (the next `- ` step, or the next job) ends the step.
    indent = len(text[start]) - len(text[start].lstrip(" -"))
    j = start
    while j > 0 and not text[j].lstrip().startswith("- "):
        j -= 1
    end = start + 1
    while end < len(text):
        l = text[end]
        if l.strip() and not l.lstrip().startswith("#") and \
                len(l) - len(l.lstrip(" ")) < indent:
            break
        end += 1
    vals = [m.group(1) for l in text[j:end]
            for m in [re.match(r"^\s*CIBW_ENVIRONMENT:\s*(.*?)\s*$", l)] if m]
    if len(vals) != 1 or vals[0] in ("", "|", ">", "|-", ">-"):
        raise SurfaceError(
            f"{WHEEL_WORKFLOW}: expected one single-line CIBW_ENVIRONMENT in the wheel_build step")
    return [f"cibw-environment {vals[0]}"]


def main(argv):
    listing = argv[1:] == ["--list-inputs"]
    if not listing and (len(argv) != 3 or argv[1] not in ("host", "wheel")
                        or not argv[2]):
        print("usage: ccache-flag-surface.py host <preset> | wheel <lane> | --list-inputs",
              file=sys.stderr)
        return 2
    try:
        trees = cmake_files()
        if listing:
            lines = [p for p, _ in trees] + [PYPROJECT, CIBW_BEFORE_ALL, WHEEL_WORKFLOW]
        else:
            kind, name = argv[1], argv[2]
            lines = cmake_surface(trees)
            if kind == "host":
                lines += resolved_preset(name) + conan_profile(name)
            else:
                lines += wheel_pyproject() + wheel_before_all() + wheel_cibw_environment()
    except SurfaceError as e:
        print(f"ccache-flag-surface: {e}", file=sys.stderr)
        return 1
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
