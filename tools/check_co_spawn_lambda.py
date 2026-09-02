#!/usr/bin/env python3
r"""Recurrence guard for the dangling immediately-invoked lambda coroutine (issue #291).

A lambda coroutine reaches its captures THROUGH THE CLOSURE OBJECT — the coroutine
frame stores the implicit object parameter as a reference, not a copy. So the closure
must outlive the coroutine. Passing the lambda to `co_spawn` as an immediately-invoked
temporary destroys the closure at the end of the full-expression:

    Forbidden : asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { ... }(), tok);
    Required  : asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { ... },   tok);
                                                                  ^ no trailing ()

With the trailing `()` removed, asio stores the callable and invokes it itself, so the
closure lives as long as the coroutine does.

⚠️ THIS IS NOT A LATENT HAZARD THAT ONE FUTURE EDIT WOULD ARM. asio's
`awaitable::promise_type::initial_suspend()` returns `suspend_always`, so the coroutine
body does not begin until AFTER `co_spawn` returns — by which time the temporary closure
is already destroyed. The FIRST body statement that names a capture therefore already
reads through a dead closure. #291's own analysis called this latent; it is not.

WHAT IS FLAGGED — and why it is not a `grep '}(),'`
---------------------------------------------------
A bare regex cannot tell the argument-level invocation from an inner immediately-invoked
lambda nested inside a correctly-passed outer one, cannot tell either from the same bytes
in a comment or a literal, and cannot see the same defect wearing a wrapper. This scanner:

  * splices backslash-newline continuations first (C++ phase 2), keeping a map back to
    original line numbers, so a spliced `*\<newline>/` really does close a comment;
  * strips comments, string / char / raw-string literals, and `#include <...>` paths;
  * matches each `co_spawn(` to its closing paren, and ERRORS OUT if it cannot;
  * tracks real LAMBDA INTRODUCERS (`[...]` followed by a parameter list / trailing
    return / qualifiers and then a body), not bare `{` — `Handler{a, b}` is not a lambda;
  * flags a lambda that is immediately invoked and is NOT nested inside another lambda's
    body, seeing through transparent wrappers: `([&]{...})()` and `std::move([&]{...}())`
    are the same defect as `[&]{...}()`;
  * requires the lambda body to actually be a coroutine (`co_await` / `co_return` /
    `co_yield`). `[&]{ return s.close(); }()` invokes its closure synchronously and is
    safe. The test is over-inclusive within its reach — a nested `co_await` counts — but
    it reads UNPREPROCESSED text, so a macro-expanded keyword defeats it (see below).

It reports DENOMINATORS (`sites parsed`, `sites with a lambda argument`) and fails closed
on each: a scan that parses no call site, or recognises a lambda argument at none of
them, reports the same "0 findings" a clean tree does. These bound the scan's REACH; they
do not make a clean result complete — see the blind-spot list below. Every `co_spawn` TOKEN must also
reconcile against a parsed call site, so a spelling the pattern does not anticipate is a
loud error rather than a silent skip.

⚠️ WHAT THESE FLOORS DO NOT COVER, because it is easy to read them as covering it: they
witness the scan's REACH, not its DETECTOR. Break the invocation test itself and every
floor still passes on a tree that has the defect. `--self-test` is what guards the
detector, which is why both it and the scan run in CI, unconditionally, as a pair. Run
one without the other and you have a gate that cannot fail for the reason it exists.

⚠️ THIS IS A LEXER, AND THESE FORMS DEFEAT IT. Each was MEASURED against this scanner,
compiles under clang, and yields a clean exit-0 scan while carrying the defect. They are
recorded rather than fixed: each needs the preprocessor or the AST, which needs a
compilation database, which this check deliberately does not have — it must run buildless
in an UNGATED job, so it sees the defect during review rather than only at merge. That
trade is the design, not an oversight, and it is the reason the claim here is "catches the
direct immediately-invoked form", NOT "cannot report a false clean":

    // 1. coroutine IILE inside an init-capture — the introducer is skipped whole
    co_spawn(ioc, [a = [&]() -> awaitable<void> { use(cap); co_return; }()]()
                      mutable -> awaitable<void> { co_await std::move(a); }, tok);

    // 2. C++23 lambda attributes, in either position
    co_spawn(ioc, [&] [[gnu::always_inline]] () -> awaitable<void> { ... }(), tok);
    co_spawn(ioc, [&]() [[gnu::always_inline]] -> awaitable<void> { ... }(), tok);

    // 3. a coroutine keyword arriving through a macro
    #define RET co_return
    co_spawn(ioc, [&]() -> awaitable<void> { use(cap); RET; }(), tok);

    // 4. a raw string whose contents phase-2 splicing must NOT have joined
    //    (C++ reverts phase 1/2 inside raw contents; this scanner splices globally)

Forms 1, 2 and 4 would be reached by an AST pass; 3 needs preprocessing. None occurs in
this tree today — verified by two independent reviewers' AST and token-stream censuses —
so what ships is a gate with known holes rather than a gate believed to be complete.

⚠️ OUT OF REACH BY CONSTRUCTION — a named closure invoked at the call site:

    auto lam = [&]() -> asio::awaitable<void> { ... };
    asio::co_spawn(ioc, lam(), asio::detached);   // NOT flagged

This carries the same closure-lifetime requirement, and the tree uses the shape widely.
Whether it is sound depends on whether `lam` outlives the coroutine — a lifetime question,
not a lexical one, so no rule this scanner can express decides it. Deciding it needs an
AST/CFG tool and a compilation database, which is also why this check is lexical: it must
run buildless in an ungated job. Treat a clean result as "no immediately-invoked temporary
closure", never as "no closure-lifetime defect".

    tools/check_co_spawn_lambda.py              scan the tree, exit 1 on any finding
    tools/check_co_spawn_lambda.py --self-test  run the fixture suite (forms, not sites)
"""
import re
import subprocess
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Extensions holding C++ TEXT, not just the ones that happen to hold a co_spawn today —
# leaving one out makes the scan silently blind to a whole file class. This is a LIST, not
# a derivation: it does not cover generated inputs the build produces from templates
# (`*.cpp.in`) or fragments included under another name (`*.def`). Those are outside the
# scan, deliberately and not completely — see the blind-spot list in the docstring.
EXTS = {".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h", ".inl", ".ipp",
        ".tpp", ".ixx", ".cppm"}

CO_SPAWN = re.compile(r"\bco_spawn\s*\(")
# Every `co_spawn` token, call or not. The DIFFERENCE between this and CO_SPAWN is the
# scan's traversal witness: a spelling the call-site regex does not anticipate would
# otherwise be a SILENT skip that costs a site and changes nothing visible.
CO_SPAWN_TOKEN = re.compile(r"\bco_spawn\b")
INCLUDE_ANGLE = re.compile(r"#\s*include\s*<[^>\n]*>")
CORO_KEYWORD = re.compile(r"\bco_(?:await|return|yield)\b")
IDENT_CHAR = re.compile(r"[A-Za-z0-9_]")


class ParseError(Exception):
    """A `co_spawn(` the scanner could not account for. Never swallowed."""


def splice(text: str):
    """C++ phase 2: delete each backslash-newline. Returns (spliced, line_of).

    `line_of[i]` is the 1-based ORIGINAL line of spliced character `i`, so a finding
    still points at a line a human can open. Without this, `/* ... *\\<newline>/` — which
    really does close the comment — leaves the stripper thinking the comment never ends,
    and it blanks the rest of the file into a clean result.
    """
    out = []
    line_of = []
    i, n, line = 0, len(text), 1
    while i < n:
        if text[i] == "\\":
            if i + 1 < n and text[i + 1] == "\n":
                i += 2
                line += 1
                continue
            if i + 2 < n and text[i + 1] == "\r" and text[i + 2] == "\n":
                i += 3
                line += 1
                continue
        out.append(text[i])
        line_of.append(line)
        if text[i] == "\n":
            line += 1
        i += 1
    return "".join(out), line_of


def strip_noncode(text: str) -> str:
    """Blank out comments, literals and include paths, preserving length and newlines."""
    # `#include <asio/co_spawn.hpp>` is a header PATH, not code. Blanked precisely rather
    # than by dropping every preprocessor line — a macro body can hold a real call.
    text = INCLUDE_ANGLE.sub(lambda m: " " * len(m.group(0)), text)

    out = list(text)
    n = len(text)
    i = 0

    def blank(a: int, b: int) -> None:
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        c = text[i]
        if c == "R" and i + 1 < n and text[i + 1] == '"':
            m = re.compile(r'R"([^()\\ ]{0,16})\(').match(text, i)
            if m:
                close = ")" + m.group(1) + '"'
                end = text.find(close, m.end())
                end = n if end < 0 else end + len(close)
                blank(i, end)
                i = end
                continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            end = n if end < 0 else end
            blank(i, end)
            i = end
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            blank(i, end)
            i = end
            continue
        if c == "'":
            # C++14 digit separator (`10'000`, `0x1F'FF`) is NOT a char literal.
            # Distinguish by the token to its left: a separator's token starts with a
            # digit, an encoding prefix (`L'a'`, `u8'a'`) starts with a letter.
            k = i - 1
            while k >= 0 and (text[k].isalnum() or text[k] == "'"):
                k -= 1
            tok = text[k + 1 : i]
            if tok and tok[0].isdigit() and i + 1 < n and text[i + 1].isalnum():
                i += 1
                continue
        if c in ('"', "'"):
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c or text[j] == "\n":
                    break
                j += 1
            end = min(j + 1, n)
            blank(i, end)
            i = end
            continue
        i += 1
    return "".join(out)


def _skip_ws(code: str, i: int) -> int:
    n = len(code)
    while i < n and code[i] in " \t\r\n":
        i += 1
    return i


def _match_pair(code: str, i: int, open_c: str, close_c: str) -> int:
    """`code[i]` is `open_c`; return the index just past its match, or -1."""
    depth = 0
    n = len(code)
    while i < n:
        if code[i] == open_c:
            depth += 1
        elif code[i] == close_c:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def _lambda_body_start(code: str, i: int) -> int:
    """`code[i]` is `[`. If it introduces a lambda, return its body's `{` index, else -1.

    Walks the optional template parameter list, the parameter list, and the qualifier /
    trailing-return run, stopping at the body. `funcs[i](x)` fails here — nothing after
    its parameter list leads to a `{` — and `[[nodiscard]]` is rejected up front.
    """
    n = len(code)
    if i + 1 < n and code[i + 1] == "[":
        return -1  # attribute, not a capture list
    # A capture list never directly follows a callable or a subscriptable expression.
    # This rejects `handlers[i]`, and `new int[n]{}` whose `[n]{}` otherwise reads as an
    # introducer plus body — inflating the very lambda denominator this file had to fix
    # once already.
    k = i - 1
    while k >= 0 and code[k] in " \t\r\n":
        k -= 1
    if k >= 0 and (IDENT_CHAR.match(code[k]) or code[k] in ")]"):
        return -1
    j = _match_pair(code, i, "[", "]")
    if j < 0:
        return -1
    j = _skip_ws(code, j)
    if j < n and code[j] == "<":  # generic lambda template parameter list
        j = _match_pair(code, j, "<", ">")
        if j < 0:
            return -1
        j = _skip_ws(code, j)
    if j < n and code[j] == "(":
        j = _match_pair(code, j, "(", ")")
        if j < 0:
            return -1
        j = _skip_ws(code, j)
    # Qualifiers and a trailing return type may sit between here and the body. Anything
    # that could not appear there means this `[` was not a lambda introducer.
    #
    # ⚠️ A COMMA IS ONLY LEGAL INSIDE ANGLE BRACKETS here (`-> std::pair<int, int>`). At
    # angle depth 0 a comma ends the argument, so accepting one lets a subscript walk
    # into the NEXT argument and adopt its brace: `co_spawn(ioc, m[k], Handler{a, b})`
    # would count `Handler{a, b}` as a lambda. That is the brace-counting denominator
    # defect in a narrower disguise.
    angle = 0
    while j < n and (code[j] != "{" or angle > 0):
        c = code[j]
        if c == "(":  # noexcept(...) / a parenthesised trailing return type
            j = _match_pair(code, j, "(", ")")
            if j < 0:
                return -1
            continue
        if c == "-" and j + 1 < n and code[j + 1] == ">":  # trailing-return arrow
            j += 2
            continue
        if c == "<":
            angle += 1
            j += 1
            continue
        if c == ">":
            if angle == 0:
                return -1
            angle -= 1
            j += 1
            continue
        if c == ",":
            if angle == 0:
                return -1
            j += 1
            continue
        if IDENT_CHAR.match(c) or c in " \t\r\n:*&.":
            j += 1
            continue
        return -1
    return j if j < n and angle == 0 else -1


def _is_using_decl(code: str, i: int) -> bool:
    """Is the `co_spawn` token at `i` part of a `using` DECLARATION?

    `using asio::co_spawn;` names the function without calling it — a declaration, not a
    call site the scan skipped, so it must not trip the traversal witness. The subsequent
    unqualified `co_spawn(` still matches the call pattern and is scanned normally.
    """
    start = max(0, i - 200)
    prefix = code[start:i]
    cut = max(prefix.rfind(";"), prefix.rfind("{"), prefix.rfind("}"), prefix.rfind("\n"))
    stmt = prefix[cut + 1:]
    return stmt.lstrip().startswith("using ")


def scan_text(text: str, relpath: str = "<text>"):
    """Return (findings, sites_parsed, sites_with_lambda_arg).

    A finding is (relpath, original_line, snippet) for one immediately-invoked lambda
    coroutine passed as a `co_spawn` argument.
    """
    spliced, line_of = splice(text)
    code = strip_noncode(spliced)

    def lineno(off: int) -> int:
        return line_of[off] if off < len(line_of) else (line_of[-1] if line_of else 1)

    # Reconcile the two enumerations BEFORE parsing: every `co_spawn` token in code must
    # be a call site the parser will visit. A CONDITION, not a count — it cannot rot.
    call_starts = {m.start() for m in CO_SPAWN.finditer(code)}
    unparsed = [m for m in CO_SPAWN_TOKEN.finditer(code)
                if m.start() not in call_starts and not _is_using_decl(code, m.start())]
    if unparsed:
        where = ", ".join(str(lineno(m.start())) for m in unparsed[:10])
        raise ParseError(
            f"{relpath}: {len(unparsed)} `co_spawn` token(s) in code that the call-site "
            f"pattern does not match (line(s) {where}). Each is a site the scan would "
            f"skip in silence — widen the pattern or explain the spelling."
        )

    findings = []
    sites = 0
    sites_with_lambda = 0
    n = len(code)

    for m in CO_SPAWN.finditer(code):
        sites += 1
        i = m.end()  # just past the '('
        paren = 1  # depth relative to the co_spawn '('
        saw_lambda = False
        stack = []  # open lambda bodies: (body_open_index, paren_depth_at_introducer)

        while i < n:
            c = code[i]
            if c == "[":
                body = _lambda_body_start(code, i)
                if body >= 0:
                    stack.append((body, paren, _grouping_depth(code, i)))
                    i = body + 1
                    continue
            if c == "(":
                paren += 1
            elif c == ")":
                paren -= 1
                if paren == 0:
                    break
            elif c == "{":
                # A brace that is not a lambda body — a braced init-list, a nested
                # block. Tracked so its `}` cannot pop a lambda frame.
                stack.append((-1, paren, 0))
            elif c == "}":
                if not stack:
                    raise ParseError(
                        f"{relpath}: unbalanced '}}' inside co_spawn( at line "
                        f"{lineno(m.start())}"
                    )
                body_open, intro_paren, wraps = stack.pop()
                if body_open >= 0:
                    nested = any(fr[0] >= 0 for fr in stack)
                    if not nested:
                        saw_lambda = True
                        body_text = code[body_open : i + 1]
                        if CORO_KEYWORD.search(body_text) and _is_invoked(
                            code, i + 1, wraps
                        ):
                            findings.append(
                                (relpath, lineno(i), code[i : i + 2].replace("\n", "\\n"))
                            )
            i += 1

        if paren != 0:
            raise ParseError(
                f"{relpath}: unterminated co_spawn( starting at line {lineno(m.start())}"
            )
        if saw_lambda:
            sites_with_lambda += 1

    return findings, sites, sites_with_lambda


def _grouping_depth(code: str, i: int) -> int:
    """How many GROUPING parens open immediately before the introducer at `code[i]`.

    `(` is grouping when what precedes it is not a callable — i.e. not an identifier,
    `)` or `]`. `co_spawn(ioc, ([&]{...})(), t)` has one; `co_spawn(ioc, f([&]{...})(), t)`
    has ZERO, because `f(` is a CALL. That distinction is the whole point: consuming a
    call's `)` made the scanner reject safe code, where the trailing `()` invokes what `f`
    RETURNED, not the lambda.
    """
    depth = 0
    j = i
    while True:
        k = j - 1
        while k >= 0 and code[k] in " \t\r\n":
            k -= 1
        if k < 0 or code[k] != "(":
            return depth
        m = k - 1
        while m >= 0 and code[m] in " \t\r\n":
            m -= 1
        if m >= 0 and (IDENT_CHAR.match(code[m]) or code[m] in ")]"):
            return depth  # a call's paren, not a grouping paren
        depth += 1
        j = k


def _is_invoked(code: str, i: int, wraps: int) -> bool:
    """Is the lambda whose body just closed at `i-1` immediately invoked?

    Sees through GROUPING parens only, and never more of them than actually surround the
    lambda: `([&]{...})()` consumes its one `)` and finds the call;
    `std::move([&]{...}())` needs none. `f([&]{...})()` consumes NOTHING, so the `)` that
    closes `f(`'s argument list stops the walk and the safe form is not flagged.
    """
    n = len(code)
    while True:
        i = _skip_ws(code, i)
        if i >= n:
            return False
        if code[i] == "(":
            return True
        if code[i] == ")" and wraps > 0:
            wraps -= 1
            i += 1
            continue
        return False


def floor_verdict(sites: int, with_lambda: int):
    """Return an error string if the scan's own coverage is unbelievable, else None.

    Both floors are CONDITIONS rather than counts, so neither bakes in today's tree. A
    scan that visited no call site, or that visited call sites but recognised a lambda
    argument at none of them, prints the same "0 immediately-invoked" a clean tree does.

    Lifted out of `main_scan` so the self-test can fire it: a floor whose firing is
    never exercised is a floor nobody has seen work.
    """
    if sites == 0:
        return ("[co-spawn-lambda] ERROR: parsed 0 co_spawn call sites — the scan found "
                "nothing to check. Treat this as a broken instrument, not a clean tree.")
    if with_lambda == 0:
        return (f"[co-spawn-lambda] ERROR: parsed {sites} co_spawn call site(s) but "
                "recognised a lambda argument at none of them. The lambda tracking is "
                "broken; a zero finding count here means nothing.")
    return None


def tracked_sources():
    out = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z"],
        capture_output=True, text=True, check=True,
    ).stdout
    for name in out.split("\0"):
        if name and pathlib.Path(name).suffix in EXTS:
            yield name


def main_scan() -> int:
    findings, sites, with_lambda = [], 0, 0
    files = 0
    for name in tracked_sources():
        # A read failure is FATAL. Swallowing it would let the one file holding a new
        # defect drop out of the universe with no change to the printed result.
        text = (ROOT / name).read_text(encoding="utf-8", errors="replace")
        if "co_spawn" not in text:
            continue
        files += 1
        f, s, wl = scan_text(text, name)
        findings.extend(f)
        sites += s
        with_lambda += wl

    verdict = floor_verdict(sites, with_lambda)
    if verdict is not None:
        print(verdict)
        return 2

    print(f"[co-spawn-lambda] {files} file(s), {sites} co_spawn call site(s) parsed, "
          f"{with_lambda} with a lambda argument, {len(findings)} immediately-invoked.")

    if findings:
        print("[co-spawn-lambda] FAIL: lambda coroutine passed as an immediately-invoked")
        print("  temporary. asio's initial_suspend is suspend_always, so the body does not")
        print("  start until co_spawn has returned and the closure is already destroyed.")
        print("  Drop the trailing '()' and let asio own the closure.")
        for f, ln, snip in findings:
            print(f"    {f}:{ln}: {snip}")
        return 1

    print("[co-spawn-lambda] PASS: every lambda co_spawn argument is passed uninvoked.")
    return 0


# ── self-test ────────────────────────────────────────────────────────────────
# Fixtures are written from the C++ FORMS this check must separate, not copied from the
# sites it was written to catch: a fixture derived from the code under repair certifies
# whatever that code does. Each pins ALL THREE outputs — findings, sites parsed, and
# sites with a lambda argument — because the last two are floors the scan fails closed
# on, and a fixture that ignores them cannot notice when they stop meaning anything.

A = "asio::awaitable<void>"

SELF_TEST_CASES = [
    # (name, source, expected findings, expected sites, expected lambda-sites)
    ("classic trailing }(),",
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}(), asio::detached);", 1, 1, 1),
    ("space before invocation",
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }} (), asio::detached);", 1, 1, 1),
    ("newline before invocation",
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}\n(), asio::detached);", 1, 1, 1),
    ("PARENTHESISED lambda, then invoked",
     f"asio::co_spawn(ioc, ([&]() -> {A} {{ co_return; }})(), asio::detached);", 1, 1, 1),
    ("std::move wrapping an invoked lambda",
     f"asio::co_spawn(ioc, std::move([&]() -> {A} {{ co_return; }}()), asio::detached);",
     1, 1, 1),
    ("correct form, uninvoked",
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}, asio::detached);", 0, 1, 1),
    ("correct form, capture with initializer",
     f"asio::co_spawn(ioc, [p = s]() -> {A} {{ co_return; }}, asio::detached);", 0, 1, 1),
    ("correct form, generic lambda",
     f"asio::co_spawn(ioc, [&]<class T>() -> {A} {{ co_return; }}, asio::detached);",
     0, 1, 1),
    ("correct form, mutable + noexcept qualifiers",
     f"asio::co_spawn(ioc, [&]() mutable noexcept(true) -> {A} {{ co_return; }}, tok);",
     0, 1, 1),
    ("invoked lambda NESTED inside a correctly-passed lambda",
     f"asio::co_spawn(ioc, [&]() -> {A} {{\n"
     "  auto v = [&] { return 1; }();\n"
     "  co_return;\n"
     "}, asio::detached);", 0, 1, 1),
    ("NON-coroutine invoked lambda returning an awaitable is SAFE",
     "asio::co_spawn(ioc, [&] { return s.close(); }(), asio::detached);", 0, 1, 1),
    ("awaitable expression, no lambda at all",
     "asio::co_spawn(ioc, session->close(mode::terminal), asio::use_future);", 0, 1, 0),
    ("braced init-list argument is NOT a lambda",
     "asio::co_spawn(ioc, Handler{a, b}, asio::detached);", 0, 1, 0),
    ("subscript then a braced-init arg does NOT become a lambda",
     "asio::co_spawn(ioc, m[k], Handler{a, b});", 0, 1, 0),
    ("subscript, then a REAL invoked lambda, is still caught",
     f"asio::co_spawn(ioc, m[k], [&]() -> {A} {{ co_return; }}());", 1, 1, 1),
    ("trailing return type with a template comma is still a lambda",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<std::pair<int, int>> {\n"
     "  co_return std::pair<int, int>{1, 2};\n"
     "}, asio::detached);", 0, 1, 1),
    ("SAFE: f(lambda)() invokes what f RETURNED, not the lambda",
     f"asio::co_spawn(ioc, f([&]() -> {A} {{ co_return; }})(), tok);", 0, 1, 1),
    ("new int[n]{} is not an introducer plus a body",
     "asio::co_spawn(ioc, new int[n]{}, tok);", 0, 1, 0),
    ("array subscript followed by a call is NOT a lambda",
     "asio::co_spawn(ioc, handlers[i](x), asio::detached);", 0, 1, 0),
    ("attribute is NOT a capture list",
     f"asio::co_spawn(ioc, [[maybe_unused]] f(), asio::detached);", 0, 1, 0),
    ("offending bytes inside a // comment",
     f"// asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}(), tok);\n"
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}, asio::detached);", 0, 1, 1),
    ("offending bytes inside a /* */ comment",
     f"/* co_spawn(ioc, [&]() -> {A} {{ co_return; }}(), tok); */\n"
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}, asio::detached);", 0, 1, 1),
    ("SPLICED block-comment terminator really closes the comment",
     "/* comment *\\\n/\n"
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_await f(); }}(), asio::detached);", 1, 1, 1),
    ("SPLICED line comment swallows the next physical line",
     "// comment \\\n"
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_await f(); }}(), tok);\n"
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}, asio::detached);", 0, 1, 1),
    ("offending bytes inside a string literal",
     'EXPECT_EQ(msg, "co_spawn(ioc, [&]{ co_return; }(), tok)");', 0, 0, 0),
    ("offending bytes inside a raw string literal",
     'const char* s = R"cpp(co_spawn(ioc, [&]{ co_return; }(), tok);)cpp";', 0, 0, 0),
    ("immediately-invoked lambda in a NON-co_spawn call",
     f"register_handler(ioc, [&]() -> {A} {{ co_return; }}(), tok);", 0, 0, 0),
    ("nested co_spawn: inner is the offender",
     f"asio::co_spawn(ioc, [&]() -> {A} {{\n"
     f"  asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}(), asio::detached);\n"
     "  co_return;\n"
     "}, asio::detached);", 1, 2, 2),
    ("two offenders in one file are both reported",
     f"asio::co_spawn(a, [&]() -> {A} {{ co_return; }}(), asio::detached);\n"
     f"asio::co_spawn(b, [&]() -> {A} {{ co_return; }}(), asio::detached);", 2, 2, 2),
    ("template argument commas do not confuse the scan",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<std::pair<int, int>> {\n"
     "  co_return std::pair<int, int>{1, 2};\n"
     "}(), asio::detached);", 1, 1, 1),
    ("digit separator is not a char literal",
     f"asio::co_spawn(ioc, [&]() -> {A} {{\n"
     "  constexpr std::size_t kCapacity = 10'000;\n"
     "  co_return;\n"
     "}(), asio::detached);", 1, 1, 1),
    # Not redundant with the decimal case, and the difference is the LAYOUT as much as
    # the radix. An implementation testing `text[i-1].isdigit()` instead of walking back
    # to the token start reads the `'` in `0x1F'FF` as a char literal and blanks to
    # end-of-line — which only reaches the `}()` if it shares that line. Written on its
    # own line this fixture let that mutant survive the whole suite; keep it on one line.
    ("hex digit separator is not a char literal (ONE LINE — see above)",
     f"asio::co_spawn(ioc, [&]() -> {A} {{ auto k = 0x1F'FF; co_return; }}(), tok);",
     1, 1, 1),
    ("encoding-prefixed char literal is still a literal",
     f"asio::co_spawn(ioc, [&]() -> {A} {{\n"
     "  char c = L'}';  // co_spawn(x, [&]{ }(), t)\n"
     "  co_return;\n"
     "}, asio::detached);", 0, 1, 1),
    ("apostrophe in a comment does not swallow the call site",
     "// asio's closure lifetime\n"
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}(), asio::detached);", 1, 1, 1),
    ("a `using` DECLARATION is not a skipped call site",
     "using asio::co_spawn;\n"
     f"co_spawn(ioc, [&]() -> {A} {{ co_return; }}(), tok);", 1, 1, 1),
    ("co_spawn in an #include path is not a call site",
     "#include <asio/co_spawn.hpp>\n"
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}(), tok);", 1, 1, 1),
    ("identifier split by a line splice is REJOINED, then scanned",
     f"asio::co_spa\\\nwn(ioc, [&]() -> {A} {{ co_return; }}(), tok);", 1, 1, 1),
    ("a macro body IS still scanned",
     f"#define SPAWN(x) asio::co_spawn(x, [&]() -> {A} {{ co_return; }}(), tok)",
     1, 1, 1),
]

# Inputs that must RAISE rather than contribute zero findings. Each is written as its
# own arm so the failure mode it forces — a silent skip — is forced individually.
SELF_TEST_RAISES = [
    ("unterminated co_spawn(",
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}(),\n"),
    ("bare co_spawn mention with no call",
     "int co_spawn;\n"
     f"asio::co_spawn(ioc, [&]() -> {A} {{ co_return; }}, tok);"),
    ("explicitly-templated co_spawn spelling",
     f"asio::co_spawn<void>(ioc, [&]() -> {A} {{ co_return; }}, tok);"),
]


def main_self_test() -> int:
    failures = 0
    for name, src, want_f, want_s, want_l in SELF_TEST_CASES:
        try:
            findings, sites, lam = scan_text(src, "<fixture>")
        except ParseError as e:
            print(f"  FAIL  {name}: unexpected ParseError: {e}")
            failures += 1
            continue
        got = (len(findings), sites, lam)
        want = (want_f, want_s, want_l)
        if got != want:
            print(f"  FAIL  {name}: expected (findings, sites, lambda-sites) {want}, got {got}")
            failures += 1
        else:
            print(f"  ok    {name}  {got}")

    for name, src in SELF_TEST_RAISES:
        try:
            scan_text(src, "<fixture>")
            print(f"  FAIL  {name}: must raise ParseError, not parse clean")
            failures += 1
        except ParseError:
            print(f"  ok    {name} raises ParseError")

    # The floors: each must FIRE on the shape it names, and stay silent otherwise. A
    # floor that has never been seen to fire is a floor nobody has proven works.
    for name, sites, lam, want_fire in (
        ("no call site parsed at all", 0, 0, True),
        ("call sites parsed but no lambda recognised", 1, 0, True),
        ("a healthy scan trips neither floor", 1, 1, False),
    ):
        fired = floor_verdict(sites, lam) is not None
        if fired != want_fire:
            print(f"  FAIL  floor: {name}: expected fire={want_fire}, got {fired}")
            failures += 1
        else:
            print(f"  ok    floor: {name} (fires={fired})")

    total = len(SELF_TEST_CASES) + len(SELF_TEST_RAISES) + 3
    if failures:
        print(f"[co-spawn-lambda] SELF-TEST FAIL: {failures} of {total} case(s) failed.")
        return 1
    print(f"[co-spawn-lambda] SELF-TEST PASS: {total}/{total}.")
    return 0


if __name__ == "__main__":
    if "--self-test" in sys.argv[1:]:
        sys.exit(main_self_test())
    try:
        sys.exit(main_scan())
    except ParseError as e:
        print(f"[co-spawn-lambda] ERROR: {e}")
        print("  A co_spawn call site could not be parsed. This is a scanner failure,")
        print("  not a clean result — fix the scanner or the source before trusting it.")
        sys.exit(2)
