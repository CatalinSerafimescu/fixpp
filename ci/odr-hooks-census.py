#!/usr/bin/env python3
"""odr-hooks-census — no type definition may change with `FIXPP_TEST_HOOKS` (fixpp#530).

    ci/odr-hooks-census.py --build-dir build/<preset> [--source-dir DIR] [-j N]

WHY. The libraries are compiled WITHOUT `FIXPP_TEST_HOOKS` and some test targets define it. A
class member, friend or base gated on the macro therefore gives every such test program two
definitions of one class: ill-formed, no diagnostic required (#511). No compiler or linker
reports it, so this preprocesses instead.

METHOD. For every file under `include/`, `src/` and `tests/` whose suffix is in HEADER_EXT
(headers, and the `.inl`/`.ipp` definition fragments a TU includes directly) it preprocesses
a one-line TU that includes the file, once without the macro and once with it, and parses
every class/struct/union/enum DEFINITION out of both outputs. A definition is recognised by
its head: before the class key it may carry only an access label, the specifiers in
DECL_SPECIFIERS, `extern` with or without a linkage string, attributes, `using A =` and
template heads, each optionally followed by a requires-clause. A record after any other token
is read as outside any type definition. A `{` inside such a head is an expression, not the
body, when an open `(` or `[` of the head holds it, or an open template list of the template
head, the class name or the base clause, or when it opens a requires-expression of the
requires-clause; a `<` opens a template list only after a name. A definition is identified by
its enclosing scopes plus its name (specialization arguments included). An unnamed class,
struct, union or enumeration is named by the first declarator after its body (`typedef struct
{...} T;`) or by its alias (`using T = struct {...};`), as `<anon:T>`; one with neither is
`<anon>` and paired with the other state by encounter order. The verdict:

  * DIVERGENCE (exit 1): a definition present in BOTH outputs whose tokens differ, head
    included, so a gated `final` or base clause counts as well as a gated member. A gated
    member of a nested class reports the nested class AND every enclosing class.
  * not a divergence: a definition present in only ONE output (a whole struct that exists
    only under the macro has a single definition), and every difference outside a type
    definition, e.g. a gated namespace-scope declaration of a function the library defines
    unconditionally. Both are listed, so a reader can see what the macro changes.

FLAGS come from the build tree's own `compile_commands.json`; nothing is re-rooted. Every C++
entry contributes: the include directories are the union across entries, the defines are the
union across entries, and the remaining options are the ones EVERY entry carries. Warning
options are dropped, and so is every spelling of `FIXPP_TEST_HOOKS` (`-D`, `-D x`, `-Dx=v`,
`-U`): the macro is appended for the second run only. Every other macro is therefore seen in
ONE state, in both runs: defined if any entry defines it, else undefined, and a define the
entries give different values is held at the value of the first entry in path order (cells
T44, T45). A type whose definition depends on the macro together with another macro's OTHER
state is not seen, whether that state holds on another preset or on another target of this
database.

FAILS CLOSED (exit 2), and still prints whatever it found, when:
  * any header fails to preprocess in either state;
  * the database is missing, has no C++ entry, or has no entry under the source dir (it
    belongs to another tree);
  * any of the three roots contributes no header;
  * the built-in positive control is not classified as a divergence. Before scanning, a probe
    header holding one gated member is run through the same compiler and flags. If the macro
    did not take effect (a base flag already defines it, a forced include undefines it) every
    header would compare equal and the scan would be clean by construction; the probe is what
    tells those apart;
  * in a header that changes with the macro, a head under the source dir the rules above
    cannot read: a template list of a class head still open where the head ends (a `<` after
    a name that is not a template, such as `template <int N = M < 3>`, is counted as a list;
    parenthesise the comparison), a requires-clause token outside the clause's grammar, or a
    type body followed by a token no declarator starts with (a brace in the head was read as
    the body). A third-party header cannot depend on the macro, so its heads are not refused;
  * in such a header, two or more definitions under one key in one scope outside every type
    definition, in either state, with at least one under the source dir. This is checked
    anywhere in the header's preprocessed output, including own headers it includes, because
    the pairing would be by encounter order and what identifies the definitions can lie
    outside their bodies, in a declarator or in an enclosing function's label. Give each one a
    distinct name.

Self-test, buildless, with the arms each refusal and verdict needs: `ci/test-odr-hooks-census.sh`.
"""
import argparse
import bisect
import difflib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor

MACRO = "FIXPP_TEST_HOOKS"
ROOTS = ("include", "src", "tests")
HEADER_EXT = (".hpp", ".h", ".inl", ".ipp")
CXX_EXT = (".cpp", ".cc", ".cxx", ".c++", ".C")
LAUNCHERS = {"ccache", "sccache", "distcc", "icecc"}

# Options whose argument is the NEXT token.
PAIRED = {"-I", "-isystem", "-iquote", "-idirafter", "-D", "-U", "-include", "-imacros", "-o",
          "-MF", "-MT", "-MQ", "-x", "-Xclang", "-target", "-isysroot", "--sysroot", "-arch"}
INCLUDE_KINDS = ("-iquote", "-I", "-isystem", "-idirafter")

PROBE = (
    "namespace fixpp_odr_census_probe {\n"
    "struct probe_class {\n"
    "#ifdef " + MACRO + "\n"
    "    int gated_member;\n"
    "#endif\n"
    "};\n"
    "}\n"
)
PROBE_KEY = "fixpp_odr_census_probe::probe_class"


class Refusal(Exception):
    pass


# ── flags ──────────────────────────────────────────────────────────────────────────────────
def entry_args(e):
    args = e.get("arguments")
    if args is None:
        args = shlex.split(e["command"])
    args = list(args)
    while args and os.path.basename(args[0]) in LAUNCHERS:
        args.pop(0)
    return args


def is_macro_flag(name_and_value):
    return name_and_value.split("=", 1)[0] == MACRO


def parse_entry(e):
    """-> (compiler, [(kind, dir)], [(name, '-D...')], [option tuple])."""
    args = entry_args(e)
    if not args:
        raise Refusal(f"empty command for {e.get('file')}")
    d = e.get("directory", ".")
    incs, defs, opts = [], [], []
    i = 1
    while i < len(args):
        a = args[i]
        nxt = args[i + 1] if i + 1 < len(args) else None
        i += 1
        if a in PAIRED:
            if nxt is None:
                raise Refusal(f"{a} without an argument in {e.get('file')}")
            i += 1
            if a in INCLUDE_KINDS:
                incs.append((a, os.path.normpath(os.path.join(d, nxt))))
            elif a == "-D":
                if not is_macro_flag(nxt):
                    defs.append((nxt.split("=", 1)[0], "-D" + nxt))
            elif a in ("-U", "-o", "-MF", "-MT", "-MQ", "-x"):
                pass
            else:
                opts.append((a, nxt))
            continue
        kind = next((k for k in INCLUDE_KINDS if a.startswith(k) and len(a) > len(k)), None)
        if kind:
            incs.append((kind, os.path.normpath(os.path.join(d, a[len(kind):]))))
        elif a.startswith("-D"):
            if not is_macro_flag(a[2:]):
                defs.append((a[2:].split("=", 1)[0], a))
        elif a.startswith(("-U", "-o", "-M", "-W", "-x")) or a in ("-c", "-pedantic",
                                                                  "-pedantic-errors"):
            pass
        elif a.startswith("@"):
            # CMake's C++-module scanning adds `@<obj>.modmap`; for a TU that imports no module
            # it carries no include or define. Any other response file would hide flags.
            if not a.endswith(".modmap"):
                raise Refusal(f"response file {a} in {e.get('file')}: its flags cannot be read")
        elif not a.startswith("-"):
            pass  # the source file
        else:
            opts.append((a,))
    return args[0], incs, defs, opts


def derive_flags(db, source_dir):
    cxx_entries = sorted((e for e in db if e.get("file", "").endswith(CXX_EXT)),
                         key=lambda e: e["file"])
    if not cxx_entries:
        raise Refusal("compile_commands.json has no C++ entry")
    src = os.path.realpath(source_dir) + os.sep
    if not any(os.path.realpath(os.path.join(e.get("directory", "."), e["file"])).startswith(src)
               for e in cxx_entries):
        raise Refusal(f"no compile_commands.json entry lies under {source_dir}: "
                      "the database belongs to another tree")
    compilers, incs, seen_dirs, defs, seen_defs, common = {}, [], set(), [], set(), None
    single = {}  # -std= / -stdlib=: one value per TU, so the most common value is used
    for e in cxx_entries:
        cxx, ei, ed, eo = parse_entry(e)
        compilers[cxx] = compilers.get(cxx, 0) + 1
        for kind, path in ei:
            if path not in seen_dirs:
                seen_dirs.add(path)
                incs.append((kind, path))
        for name, flag in ed:
            if name not in seen_defs:
                seen_defs.add(name)
                defs.append(flag)
        for o in eo:
            if o[0].startswith(("-std=", "-stdlib=")):
                vals = single.setdefault(o[0].split("=", 1)[0], {})
                vals[o[0]] = vals.get(o[0], 0) + 1
        eo = [o for o in eo if not o[0].startswith(("-std=", "-stdlib="))]
        common = eo if common is None else [o for o in common if o in eo]
    if len(compilers) != 1:
        raise Refusal(f"the C++ entries use more than one compiler: {sorted(compilers)}")
    flags = [max(v, key=v.get) for v in single.values()]
    flags += [x for o in common for x in o] + defs
    for kind in INCLUDE_KINDS:
        flags += [x for k, p in incs if k == kind for x in (k, p)]
    return next(iter(compilers)), flags


# ── type definitions ───────────────────────────────────────────────────────────────────────
TOK = re.compile(r'(?:u8|u|U|L)?R"([^(\s]*)\((?:.|\n)*?\)\1"|(?:u8|u|U|L)?"(?:\\.|[^"\\\n])*"'
                 r"|(?:u8|u|U|L)?'(?:\\.|[^'\\\n])*'|::|[A-Za-z_]\w*|\d[\w.']*|\S")
MARKER = re.compile(r'#\s*\d+\s+"([^"]*)"')
CLASS_KEYS = ("class", "struct", "union")
ACCESS = ("public", "private", "protected")
DECL_SPECIFIERS = ("typedef", "__extension__", "static", "constexpr", "constinit", "const",
                   "volatile", "inline", "thread_local", "mutable")


OPEN = ("(", "[", "{")
CLOSE = (")", "]", "}")
IDENT = re.compile(r"[A-Za-z_]\w*$")
LITERAL = re.compile(r"""\d|(?:u8|u|U|L)?R?["']""")
# What may follow the `}` of a type definition: its `;`, or a declarator, which starts with a
# name (a cv-qualifier and an attribute keyword are names too), `*`, `&`, `(` or `[`.
AFTER_BODY = (";", "*", "&", "(", "[")
NO_LIST = "a template argument list in a class head does not close"
NO_READ = "a brace in a class head the census cannot read"


def skip_group(h, i):
    """h[i] is `(`, `[` or `{`: the index after its balanced close (the three nest in one
    another), or None when it is still open at the end of h."""
    depth = 0
    for j in range(i, len(h)):
        if h[j] in OPEN:
            depth += 1
        elif h[j] in CLOSE:
            depth -= 1
            if depth == 0:
                return j + 1
    return None


def angle_depth(h, i, depth=0, stop=False):
    """-> (index, depth) after reading h[i:] for template argument lists, starting at <depth>:
    with <stop>, it stops after the `>` that brings depth to 0, else it reads to the end of h.
    A `(`, `[` or `{` group is skipped whole, `->` is an arrow, and a `<` opens a list only
    after a name; after anything else it is a comparison."""
    while i < len(h):
        t = h[i]
        if t in OPEN:
            j = skip_group(h, i)
            if j is None:
                return len(h), depth
            i = j
            continue
        if t == "-" and i + 1 < len(h) and h[i + 1] == ">":
            i += 2
            continue
        if t == "<" and i > 0 and IDENT.match(h[i - 1]):
            depth += 1
        elif t == ">" and depth:
            depth -= 1
            if depth == 0 and stop:
                return i + 1, 0
        i += 1
    return i, depth


def skip_angle(h, i):
    """h[i] == `<` opening a template parameter or argument list: the index after its `>`, or
    None when it is still open at the end of h."""
    j, depth = angle_depth(h, i + 1, 1, stop=True)
    return None if depth else j


def skip_attrs(h, i):
    while i < len(h):
        if h[i] == "[" and i + 1 < len(h) and h[i + 1] == "[":
            j = skip_group(h, i)
        elif h[i] in ("alignas", "__attribute__", "__declspec", "_Alignas") and i + 1 < len(h) \
                and h[i + 1] == "(":
            j = skip_group(h, i + 1)
        else:
            return i
        if j is None:
            return len(h)
        i = j
    return i


def skip_requires(h, i):
    """h[i] == `requires`, a requires-clause before the class key: its primaries, each a
    parenthesised expression, a requires-expression, an id-expression or a literal, joined by
    `&&` and `||`. -> (index of the first token after the clause, None), or (None, why) when
    the head ends inside it: why is 'requires' where a requires-expression's body opens,
    'angle' inside a template argument list of the clause, and 'unread' where the clause's
    grammar has no such token."""
    n = len(h)
    i += 1
    while True:
        if i >= n:
            return None, "unread"
        if h[i] == "(":
            i = skip_group(h, i)
        elif h[i] == "requires":
            i += 1
            if i < n and h[i] == "(":
                i = skip_group(h, i)
            if i is not None and i >= n:
                return None, "requires"
            i = skip_group(h, i) if i is not None and h[i] == "{" else None
        elif h[i] == "::" or IDENT.match(h[i]):
            if h[i] == "::":
                i += 1
            while i < n and IDENT.match(h[i]):
                i += 1
                if i < n and h[i] == "<":
                    i = skip_angle(h, i)
                    if i is None:
                        return None, "angle"
                if i < n and h[i] == "::":
                    i += 1
                    if i < n and h[i] == "template":
                        i += 1
                else:
                    break
        elif LITERAL.match(h[i]):
            i += 1
        else:
            return None, "unread"
        if i is None:
            return None, "unread"
        if i + 1 < n and h[i] == h[i + 1] and h[i] in ("&", "|"):
            i += 2
            continue
        return (i, None) if i < n else (None, "unread")


def classify_head(h):
    """-> (kind, name) for the tokens before a `{`: read_head()'s, or ('expr', 'paren')."""
    kind, name = read_head(h)
    # A `{` while a `(` or `[` is open is an expression when the head reads as anything but
    # `other`: no declaration scope opens inside either. In an `other` head (a lambda passed
    # to a call) the `{` stays a scope, so a class defined in that lambda is still keyed. A `)`
    # or `]` with none open (a for-statement's tail, after its `;` reset the head) is ignored.
    if kind != "other" and paren_open(h):
        return "expr", "paren"
    return kind, name


def paren_open(h):
    opened = {"(": 0, "[": 0}
    for t in h:
        if t in opened:
            opened[t] += 1
        elif t in (")", "]"):
            k = "(" if t == ")" else "["
            opened[k] = max(0, opened[k] - 1)
    return bool(opened["("] or opened["["])


def read_head(h):
    """-> (kind, name) for the tokens before a `{`, by the class-head grammar.

    kind is 'namespace'; 'type', name being the type's name ("" for an unnamed class, struct,
    union or enumeration, "<anon:A>" for an unnamed one in `using A =`);
    'other'; 'expr' when the `{` opens an expression inside the head rather than a scope (name
    says what holds it: 'angle' or 'requires'); or 'refuse' when the head reads as a class head
    up to a token the rules below give no place (name is the reason)."""
    i, alias = 0, None
    # The tokens a definition head may carry before its class key: an access label, the
    # declaration specifiers of `static struct X {...} x;` and its kin, attributes, a linkage
    # string, and `using A =`. Any other leading token makes the head `other`, so a record
    # defined after one is read as outside any type definition (L-530-1).
    while i < len(h):
        j = skip_attrs(h, i)
        if j != i:
            i = j  # attributes interleaved with the specifiers
        elif h[i] in ACCESS and i + 1 < len(h) and h[i + 1] == ":":
            i += 2
        elif h[i] in DECL_SPECIFIERS:
            i += 1
        elif h[i] == "extern" and i + 1 < len(h) and h[i + 1] in ('"C"', '"C++"'):
            if i + 2 == len(h):
                return "namespace", None  # a linkage block names no scope
            i += 2  # `extern "C" struct X {...} x;`: a linkage string before one declaration
        elif h[i] == "extern":
            i += 1  # extern without a linkage string
        elif h[i] == "using" and i + 2 < len(h) and h[i + 2] == "=":
            alias = h[i + 1]
            i += 3  # `using A = struct Z {...};`
        else:
            break
    if i < len(h) and h[i] == "namespace":
        return "namespace", "".join(t for t in h[skip_attrs(h, i + 1):] if t != "inline")
    # From here on, a `{` inside an open template list of a template head, of the class name or
    # of the base clause is an expression (`template <int N = int{3}>`, `struct X : B<P{1}>`).
    # Template heads come first, each optionally followed by a requires-clause: the out-of-class
    # definition of a member template of a constrained class template carries two. A head that
    # STARTS with `requires` is a nested requirement inside a requires-expression, not a class
    # head, so a requires-clause is read only after a template head.
    templated = False
    while i < len(h) and (h[i] == "template" or templated and h[i] == "requires"):
        if h[i] == "requires":
            i, why = skip_requires(h, i)
            if why == "unread":
                return "refuse", NO_READ
            if why:
                return "expr", why
            templated = False  # one clause per template head
        elif i + 1 < len(h) and h[i + 1] == "<":
            i = skip_angle(h, i + 1)
            if i is None:
                return "expr", "angle"
            templated = True
        else:
            i += 1  # `template` without a list: an explicit instantiation
    i = skip_attrs(h, i)
    if i >= len(h) or h[i] not in CLASS_KEYS + ("enum",):
        return "other", ""
    is_enum = h[i] == "enum"
    i += 1
    if is_enum and i < len(h) and h[i] in ("class", "struct"):
        i += 1
    i = skip_attrs(h, i)
    name = []
    while i < len(h):
        t = h[i]
        if t == "::" or (IDENT.match(t) and t != "final" and (not name or name[-1] == "::")):
            name.append(t)
            i += 1
        elif t == "<" and name:
            j = skip_angle(h, i)
            if j is None:
                return "expr", "angle"
            name += h[i:j]
            i = j
        else:
            break
    if i < len(h) and h[i] == "final":
        i += 1
    if i < len(h) and h[i] != ":":
        return "other", ""  # e.g. `struct X* f()`, `struct X x =`: not a definition head
    if i < len(h) and angle_depth(h, i + 1)[1]:
        return "expr", "angle"  # inside a base clause's template argument list
    if name:
        return "type", " ".join(name).replace(" :: ", "::").replace(" ", "")
    return "type", f"<anon:{alias}>" if alias else ""


def declarator_name(toks, n):
    """toks[n] is the `{` of an unnamed class, struct or union: the first declarator-id after
    its `}` (`typedef struct {...} Name;`, `static struct {...} v;`), or None when none
    follows (an anonymous union)."""
    depth = 0
    for j in range(n, len(toks)):
        depth += (toks[j][0] == "{") - (toks[j][0] == "}")
        if depth == 0:
            break
    else:
        return None
    h = [t for t, _ in toks[j + 1:j + 65]]
    i = 0
    while i < len(h):
        k = skip_attrs(h, i)
        if k != i:
            i = k
        elif h[i] in ("*", "&", "(", "const", "volatile", "__restrict", "__restrict__"):
            i += 1
        else:
            return h[i] if IDENT.match(h[i]) else None
    return None


def parse(text, own=lambda origin: False):
    """-> (lines, origins, defs, line_type, unread) — defs: key -> (first_line, last_line,
    origin, token_text, scope_key, nested), where scope_key is the key before its `#<ordinal>`
    and nested says the definition lies inside another type definition; line_type[i]: key of
    the innermost type definition open at line i, or None; unread: (origin, reason, text) for
    each head, in an origin `own` accepts, that the rules could not read."""
    lines, origins = [], []
    cur = "?"
    for ln in text.split("\n"):
        m = MARKER.match(ln)
        if m:
            cur = m.group(1)
            continue
        if ln.strip() and not ln.lstrip().startswith("#"):
            lines.append(ln)
            origins.append(cur)
    stack, head, head_line, defs, ordinal, line_type, unread = [], [], 0, {}, {}, [], []
    toks_by_frame = []
    expr = 0  # depth of the expression-brace group being read into the head, 0 outside one
    # Tokenised as ONE text, so a raw string literal spanning lines is one token.
    body = "\n".join(lines)
    starts = [0]
    for ln in lines:
        starts.append(starts[-1] + len(ln) + 1)
    toks = [(m.group(0), m.start()) for m in TOK.finditer(body)]

    def reach(li):
        while len(line_type) <= li:
            line_type.append(next((f[1] for f in reversed(stack) if f[0] == "type"), None))

    def refuse(origin, why, text):
        if own(origin):
            unread.append((origin, why, text[:100]))

    def head_ends():
        # A head ends at `;`, at a scope's `}` and at the end of the text. A template list
        # still open there was a comparison counted as a list, and a `{` read as inside it may
        # have been a class body.
        if classify_head(head) == ("expr", "angle"):
            refuse(origins[head_line], NO_LIST, " ".join(head))

    for n, (t, pos) in enumerate(toks):
        li = bisect.bisect_right(starts, pos) - 1
        reach(li)
        for acc in toks_by_frame:
            acc.append(t)
        if expr:
            head.append(t)
            expr += (t == "{") - (t == "}")
            continue
        if t == "{":
            kind, name = classify_head(head)
            if kind == "expr":
                if not head:
                    head_line = li
                head.append(t)
                expr = 1
                continue
            if kind == "refuse":
                refuse(origins[head_line], name, " ".join(head))
                kind = "other"
            path = "::".join(f[2] for f in stack if f[2])
            if kind == "type":
                if name == "":
                    d = declarator_name(toks, n)
                    name = f"<anon:{d}>" if d else "<anon>"
                k = f"{path}::{name}" if path else name
                ordinal[k] = ordinal.get(k, 0) + 1
                key = k if ordinal[k] == 1 else f"{k}#{ordinal[k]}"
                toks_by_frame.append(list(head) + ["{"])
                stack.append(("type", key, name, head_line, origins[head_line],
                              toks_by_frame[-1], k, any(f[0] == "type" for f in stack)))
            elif kind == "namespace":
                label = "" if name is None else name or "<anon-ns>"
                stack.append(("namespace", None, label, li, None, None))
            else:
                stack.append(("other", None, "{" + " ".join(head)[:60] + "}", li, None, None))
            head = []
        elif t == "}":
            head_ends()
            if stack:
                f = stack.pop()
                if f[0] == "type":
                    toks_by_frame.pop()
                    defs[f[1]] = (f[3], li, f[4], " ".join(f[5]), f[6], f[7])
                    nxt = toks[n + 1][0] if n + 1 < len(toks) else ""
                    if nxt not in AFTER_BODY and not IDENT.match(nxt):
                        refuse(f[4], f"a type body is followed by `{nxt}`: a brace in its head "
                               "was read as the body", " ".join(f[5]))
            head = []
        elif t == ";":
            head_ends()
            head = []
        else:
            if not head:
                head_line = li
            head.append(t)
    head_ends()
    reach(len(lines) - 1)
    return lines, origins, defs, line_type, unread


# ── one header ─────────────────────────────────────────────────────────────────────────────
CTX = {}


def pp(tu, extra):
    r = subprocess.run([CTX["cxx"], *CTX["flags"], *extra, "-E", "-x", "c++", tu],
                       capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def census(header):
    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as f:
        f.write(f'#include "{header}"\n')
        tu = f.name
    try:
        rc0, a, e0 = pp(tu, [])
        rc1, b, e1 = pp(tu, ["-D" + MACRO])
    finally:
        os.unlink(tu)
    if rc0 or rc1:
        which = "without and with" if rc0 and rc1 else "without" if rc0 else "with"
        lines = (e0 if rc0 else e1).splitlines()
        err = ([ln for ln in lines if "error" in ln] or lines)[:2]
        return {"header": header, "status": "ERROR", "why": f"fails {which} {MACRO}", "err": err}
    la, oa, da, ta, ua = parse(a, own)
    lb, ob, db, tb, ub = parse(b, own)
    if la == lb:
        return {"header": header, "status": "SAME"}
    unread = sorted(set(ua) | set(ub))
    # Two or more definitions under one key in one scope outside every type definition cannot
    # be paired reliably: encounter order may be shifted by a definition whose identity lives
    # in a following declarator or in an enclosing function label.
    per_key = {}
    for side, d in ((0, da), (1, db)):
        for v in d.values():
            if not v[5]:
                per_key.setdefault(v[4], [0, 0, []])[side] += 1
                per_key[v[4]][2].append(v[2])
    for k, (na, nb, where) in sorted(per_key.items()):
        if max(na, nb) >= 2 and any(own(o) for o in where):
            unread.append((next(o for o in where if own(o)), f"definitions of {k} cannot be "
                           f"paired between the states ({na} without {MACRO}, {nb} with)", ""))
    div, one = {}, {}
    for k in sorted(set(da) | set(db)):
        if k in da and k in db:
            if da[k][3] != db[k][3]:
                body_a = la[da[k][0]:da[k][1] + 1]
                body_b = lb[db[k][0]:db[k][1] + 1]
                delta = [d for d in difflib.ndiff([x.strip() for x in body_a],
                                                  [x.strip() for x in body_b])
                         if d[:1] in "+-"][:6]
                div[k] = (da[k][2], delta)
        else:
            side = "with" if k in db else "without"
            one[k] = ((db if k in db else da)[k][2], side)
    other = []
    sm = difflib.SequenceMatcher(None, la, lb, autojunk=False)
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal":
            continue
        for sign, ls, lo_, lt, lo, hi in (("-", la, oa, ta, i1, i2), ("+", lb, ob, tb, j1, j2)):
            for i in range(lo, hi):
                if lt[i] is None:
                    other.append((lo_[i], sign, ls[i].strip()[:110]))
    return {"header": header, "status": "DIFF", "div": div, "one": one, "other": other,
            "unread": unread}


def own(origin):
    """Whether <origin> lies under the source dir. Only there is a head the rules cannot read
    refused: a third-party header cannot depend on the macro."""
    return os.path.realpath(origin).startswith(CTX["src"])


def init(cxx, flags, src):
    CTX["cxx"], CTX["flags"], CTX["src"] = cxx, flags, os.path.realpath(src) + os.sep


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--source-dir", help="default: CMAKE_HOME_DIRECTORY from CMakeCache.txt")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    o = ap.parse_args()
    refusals = []
    try:
        src = o.source_dir
        if src is None:
            cache = os.path.join(o.build_dir, "CMakeCache.txt")
            try:
                src = next(ln.split("=", 1)[1].strip() for ln in open(cache, encoding="utf-8")
                           if ln.startswith("CMAKE_HOME_DIRECTORY:"))
            except (OSError, StopIteration):
                raise Refusal(f"no CMAKE_HOME_DIRECTORY in {cache}; pass --source-dir")
        src = os.path.abspath(src)
        ccj = os.path.join(o.build_dir, "compile_commands.json")
        try:
            db = json.load(open(ccj, encoding="utf-8"))
        except (OSError, ValueError) as ex:
            raise Refusal(f"cannot read {ccj}: {ex}")
        cxx, flags = derive_flags(db, src)
    except Refusal as ex:
        print(f"REFUSED: {ex}")
        return 2

    headers, per_root = [], {}
    for root in ROOTS:
        found = sorted(os.path.join(dp, f) for dp, _, fs in os.walk(os.path.join(src, root))
                       for f in fs if f.endswith(HEADER_EXT))
        per_root[root] = len(found)
        headers += found
    for root, n in per_root.items():
        if n == 0:
            refusals.append(f"{root}/ contributes no header under {src}")

    init(cxx, flags, src)
    with tempfile.TemporaryDirectory() as td:
        probe = os.path.join(td, "fixpp_odr_census_probe.hpp")
        with open(probe, "w") as f:
            f.write(PROBE)
        r = census(probe)
    if r["status"] != "DIFF" or list(r["div"]) != [PROBE_KEY]:
        refusals.append(f"positive control: a probe class with one gated member was classified "
                        f"{r['status']} {sorted(r.get('div', {}))}, not as a divergence of "
                        f"{PROBE_KEY}; {MACRO} did not take effect, so no verdict is possible"
                        + (f" ({r['why']}: {' | '.join(r['err'])})" if r["status"] == "ERROR"
                           else ""))

    with ProcessPoolExecutor(max(1, o.jobs), initializer=init, initargs=(cxx, flags, src)) as ex:
        results = list(ex.map(census, headers))

    rel = lambda p: os.path.relpath(p, src) if p.startswith(src) else p  # noqa: E731
    errors = [r for r in results if r["status"] == "ERROR"]
    for r in errors:
        print(f"ERROR {rel(r['header'])}: {r['why']}: {' | '.join(r['err'])}")
    unread = sorted({(rel(o), why, text) for r in results if r["status"] == "DIFF"
                     for o, why, text in r["unread"]})
    for origin, why, text in unread:
        print(f"ERROR {origin}: {why}" + (f": {text}" if text else ""))
    div, one, other = {}, {}, {}
    for r in results:
        if r["status"] != "DIFF":
            continue
        for k, (origin, delta) in r["div"].items():
            div.setdefault((rel(origin), k), delta)
        for k, (origin, side) in r["one"].items():
            one.setdefault((rel(origin), k), side)
        for origin, sign, text in r["other"]:
            other.setdefault(rel(origin), set()).add((sign, text))
    print(f"compiler: {cxx}; flags: {len(flags)}; headers scanned: {len(results)} "
          + " ".join(f"{k}/={v}" for k, v in per_root.items())
          + f"; errors: {len(errors)}; differ: {sum(r['status'] == 'DIFF' for r in results)}")
    print("positive control: " + ("ok" if not any("positive control" in x for x in refusals)
                                  else "FAILED"))
    print(f"== DIVERGENCE: a type defined in both states, differently (ODR): {len(div)}")
    for (origin, k), delta in sorted(div.items()):
        print(f"  {origin}: {k}")
        for d in delta:
            print(f"      {d[:118]}")
    print(f"== defined in one state only (one definition, not a divergence): {len(one)}")
    for (origin, k), side in sorted(one.items()):
        print(f"  {origin}: {k} (only {side} {MACRO})")
    print(f"== differences outside any type definition (not a divergence): {len(other)} "
          "origin header(s)")
    for h in sorted(other):
        print(f"  {h}")
        for sign, text in sorted(other[h])[:6]:
            print(f"      {sign} {text}")
    for x in refusals:
        print(f"REFUSED: {x}")
    if refusals or errors or unread:
        return 2
    return 1 if div else 0


if __name__ == "__main__":
    sys.exit(main())
