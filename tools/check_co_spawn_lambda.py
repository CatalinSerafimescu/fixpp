#!/usr/bin/env python3
"""Recurrence guard for the dangling immediately-invoked lambda coroutine (issue #291).

A lambda coroutine reaches its captures THROUGH THE CLOSURE OBJECT — the coroutine
frame does not copy them. So the closure must outlive the coroutine. Passing the
lambda to `co_spawn` as an immediately-invoked temporary destroys the closure at the
end of the full-expression, i.e. the moment `co_spawn` returns, while the coroutine
is still suspended at its first `co_await`:

    Forbidden : asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { ... }(), tok);
    Required  : asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { ... },   tok);
                                                                  ^ no trailing ()

With the trailing `()` removed, asio stores the callable and invokes it itself, so
the closure lives as long as the coroutine does.

Any statement AFTER a suspension point that touches a capture is then an immediate
use-after-free, with no diagnostic and no compiler warning. The two known instances
(PR #290's, and issue #291's) were both latent-not-firing for exactly one reason:
nothing after the suspension point happened to touch a capture. That is one edit
away from a real UAF, which is why this is a lexical guard rather than a review note.

WHAT IS FLAGGED — and why it is not a `grep '}(),'`
---------------------------------------------------
A bare regex over the file cannot tell the argument-level invocation (the defect)
from an inner immediately-invoked lambda nested inside a correctly-passed outer
lambda (perfectly fine), and cannot tell either from the same bytes inside a comment
or a string. This scanner therefore:

  * strips comments, string / char / raw-string literals first (line structure kept);
  * matches each `co_spawn(` to its closing paren, and ERRORS OUT if it cannot —
    an unmatched call site is a parse failure, never a silent skip;
  * flags `}` followed by `(` ONLY at the top level of the argument list, i.e. at
    paren-depth 1 relative to the `co_spawn(` and brace-depth 0. An inner
    `[&]{ ... }()` inside a lambda body sits at brace-depth >= 1 and is not flagged.

It reports a DENOMINATOR (`sites parsed`, `sites with a lambda argument`), not just
a hit count: a scanner that silently parsed zero call sites would otherwise report
exactly the same "PASS" as a clean tree.

    tools/check_co_spawn_lambda.py              scan the tree, exit 1 on any finding
    tools/check_co_spawn_lambda.py --self-test  run the fixture suite (forms, not sites)
"""
import re
import subprocess
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXTS = {".cpp", ".cc", ".cxx", ".hpp", ".hh", ".h"}

CO_SPAWN = re.compile(r"\bco_spawn\s*\(")
# Every `co_spawn` token, call or not. The DIFFERENCE between this and CO_SPAWN
# is the scan's traversal witness: a spelling the call-site regex does not
# anticipate (`co_spawn<T>(`, a macro, a stray mention) would otherwise be a
# SILENT skip that costs a site and changes nothing visible in the output.
CO_SPAWN_TOKEN = re.compile(r"\bco_spawn\b")
INCLUDE_ANGLE = re.compile(r"#\s*include\s*<[^>\n]*>")


class ParseError(Exception):
    """A `co_spawn(` whose argument list does not close. Never swallowed."""


def strip_noncode(text: str) -> str:
    """Blank out comments and literals, preserving length and line breaks.

    Every removed byte becomes a space (newlines kept), so byte offsets and line
    numbers in the result still address the original file.
    """
    # `#include <asio/co_spawn.hpp>` is a header PATH, not code. Blanked first
    # (precisely, not by dropping every preprocessor line — a macro body can hold
    # a real call). The quoted form is already covered by literal blanking below.
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
        # raw string: R"delim( ... )delim"
        if c == "R" and i + 1 < n and text[i + 1] == '"':
            m = re.compile(r'R"([^()\\ ]{0,16})\(').match(text, i)
            if m:
                close = ')' + m.group(1) + '"'
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
            # Distinguish by the token to its left: a separator's token starts with
            # a digit, whereas an encoding prefix (`L'a'`, `u8'a'`) starts with a
            # letter. Getting this wrong blanks out real code to end of line.
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


def scan_text(text: str, relpath: str = "<text>"):
    """Return (findings, sites_parsed, sites_with_lambda_arg).

    A finding is (relpath, line, snippet) for one immediately-invoked lambda passed
    as a `co_spawn` argument.
    """
    code = strip_noncode(text)
    findings = []
    sites = 0
    sites_with_lambda = 0

    # Reconcile the two enumerations BEFORE parsing: every `co_spawn` token in
    # code must be a call site the parser will visit. This is a CONDITION, not a
    # count — it holds at any tree size and cannot go stale.
    call_starts = {m.start() for m in CO_SPAWN.finditer(code)}
    unparsed = [m for m in CO_SPAWN_TOKEN.finditer(code) if m.start() not in call_starts]
    if unparsed:
        lines = ", ".join(str(code.count("\n", 0, m.start()) + 1) for m in unparsed[:10])
        raise ParseError(
            f"{relpath}: {len(unparsed)} `co_spawn` token(s) in code that the call-site "
            f"pattern does not match (line(s) {lines}). Each is a site the scan would "
            f"skip in silence — widen the pattern or explain the spelling."
        )

    for m in CO_SPAWN.finditer(code):
        sites += 1
        i = m.end()  # just past the '('
        paren = 1  # depth relative to the co_spawn '('
        brace = 0
        saw_lambda = False
        n = len(code)
        while i < n:
            c = code[i]
            if c == "(":
                paren += 1
            elif c == ")":
                paren -= 1
                if paren == 0:
                    break
            elif c == "{":
                if paren == 1 and brace == 0:
                    saw_lambda = True
                brace += 1
            elif c == "}":
                brace -= 1
                if brace < 0:
                    raise ParseError(
                        f"{relpath}: unbalanced '}}' inside co_spawn( at offset {m.start()}"
                    )
                if paren == 1 and brace == 0:
                    # Argument-level lambda body just closed. An invocation here is
                    # the defect; anything else (`,` or `)`) is the correct form.
                    j = i + 1
                    while j < n and code[j] in " \t\r\n":
                        j += 1
                    if j < n and code[j] == "(":
                        line = code.count("\n", 0, i) + 1
                        snippet = text[i : j + 2].replace("\n", "\\n")
                        findings.append((relpath, line, snippet))
            i += 1
        if paren != 0:
            raise ParseError(
                f"{relpath}: unterminated co_spawn( starting at line "
                f"{code.count(chr(10), 0, m.start()) + 1}"
            )
        if saw_lambda:
            sites_with_lambda += 1

    return findings, sites, sites_with_lambda


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
        try:
            text = (ROOT / name).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if "co_spawn" not in text:
            continue
        files += 1
        f, s, wl = scan_text(text, name)
        findings.extend(f)
        sites += s
        with_lambda += wl

    # Two floors, both CONDITIONS rather than counts, so neither bakes in today's
    # tree. A scan that visited no call site, or that visited call sites but
    # recognised a lambda argument at none of them (the brace tracking broken),
    # reports the same "0 immediately-invoked" a genuinely clean tree does.
    if sites == 0:
        print("[co-spawn-lambda] ERROR: parsed 0 co_spawn call sites — the scan found "
              "nothing to check. Treat this as a broken instrument, not a clean tree.")
        return 2
    if with_lambda == 0:
        print(f"[co-spawn-lambda] ERROR: parsed {sites} co_spawn call site(s) but "
              "recognised a lambda argument at none of them. The brace tracking is "
              "broken; a zero finding count here means nothing.")
        return 2

    print(f"[co-spawn-lambda] {files} file(s), {sites} co_spawn call site(s) parsed, "
          f"{with_lambda} with a lambda argument, {len(findings)} immediately-invoked.")

    if findings:
        print("[co-spawn-lambda] FAIL: lambda coroutine passed as an immediately-invoked")
        print("  temporary. The closure dies when co_spawn returns, while the coroutine is")
        print("  still suspended. Drop the trailing '()' and let asio own the closure.")
        for f, ln, snip in findings:
            print(f"    {f}:{ln}: {snip}")
        return 1

    print("[co-spawn-lambda] PASS: every lambda co_spawn argument is passed uninvoked.")
    return 0


# ── self-test ────────────────────────────────────────────────────────────────
# Fixtures are written from the C++ FORMS this check must separate, not copied
# from the sites it was written to catch: a fixture derived from the code under
# repair certifies whatever that code does.

SELF_TEST_CASES = [
    # (name, source, expected number of findings)
    ("classic trailing }(),",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }(), asio::detached);", 1),
    ("space before invocation",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; } (), asio::detached);", 1),
    ("newline before invocation",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }\n(), asio::detached);", 1),
    ("correct form, uninvoked",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }, asio::detached);", 0),
    ("correct form, capture list with initializer",
     "asio::co_spawn(ioc, [p = s]() -> asio::awaitable<void> { co_return; }, asio::detached);", 0),
    ("invoked lambda NESTED inside a correctly-passed lambda",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {\n"
     "  auto v = [&] { return 1; }();\n"
     "  co_return;\n"
     "}, asio::detached);", 0),
    ("invoked lambda as the LAST argument",
     "asio::co_spawn(ioc, member(), [&](std::exception_ptr) { }());", 1),
    ("awaitable expression, no lambda at all",
     "asio::co_spawn(ioc, session->close(mode::terminal), asio::use_future);", 0),
    ("braced init-list argument is not an invocation",
     "asio::co_spawn(ioc, Handler{a, b}, asio::detached);", 0),
    ("offending bytes inside a // comment",
     "// asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { }(), asio::detached);\n"
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }, asio::detached);", 0),
    ("offending bytes inside a /* */ comment",
     "/* co_spawn(ioc, [&]{ }(), tok); */\n"
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }, asio::detached);", 0),
    ("offending bytes inside a string literal",
     'EXPECT_EQ(msg, "co_spawn(ioc, [&]{ }(), tok)");', 0),
    ("offending bytes inside a raw string literal",
     'const char* s = R"cpp(co_spawn(ioc, [&]{ }(), tok);)cpp";', 0),
    ("immediately-invoked lambda in a NON-co_spawn call",
     "register_handler(ioc, [&]() { return 1; }(), tok);", 0),
    ("nested co_spawn: inner is the offender",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {\n"
     "  asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }(), asio::detached);\n"
     "  co_return;\n"
     "}, asio::detached);", 1),
    ("two offenders in one file are both reported",
     "asio::co_spawn(a, [&]() -> asio::awaitable<void> { co_return; }(), asio::detached);\n"
     "asio::co_spawn(b, [&]() -> asio::awaitable<void> { co_return; }(), asio::detached);", 2),
    ("template argument commas do not confuse the scan",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<std::pair<int, int>> {\n"
     "  co_return std::pair<int, int>{1, 2};\n"
     "}(), asio::detached);", 1),
    ("digit separator is not a char literal",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {\n"
     "  constexpr std::size_t kCapacity = 10'000;\n"
     "  co_return;\n"
     "}(), asio::detached);", 1),
    # Kept alongside the decimal case though both reach the same branch TODAY: an
    # implementation testing `text[i-1].isdigit()` instead of walking back to the
    # token start passes decimal and fails hex. The fixture kills that mutant.
    ("hex digit separator is not a char literal",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {\n"
     "  constexpr auto kMask = 0x1F'FF;\n"
     "  co_return;\n"
     "}(), asio::detached);", 1),
    ("encoding-prefixed char literal is still a literal",
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {\n"
     "  char c = L'}';  // co_spawn(x, [&]{ }(), t)\n"
     "  co_return;\n"
     "}, asio::detached);", 0),
    ("co_spawn in an #include path is not a call site",
     "#include <asio/co_spawn.hpp>\n"
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }(), tok);", 1),
    ("a macro body IS still scanned",
     "#define SPAWN(x) asio::co_spawn(x, [&]() -> asio::awaitable<void> { co_return; }(), tok)",
     1),
    ("apostrophe in a comment does not swallow the call site",
     "// asio's closure lifetime\n"
     "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }(), asio::detached);", 1),
]


def main_self_test() -> int:
    failures = 0
    for name, src, expected in SELF_TEST_CASES:
        try:
            findings, sites, _ = scan_text(src, "<fixture>")
        except ParseError as e:
            print(f"  FAIL  {name}: unexpected ParseError: {e}")
            failures += 1
            continue
        got = len(findings)
        if got != expected:
            print(f"  FAIL  {name}: expected {expected} finding(s), got {got} "
                  f"(sites parsed: {sites})")
            failures += 1
        else:
            print(f"  ok    {name}  ({got} finding(s), {sites} site(s))")

    # The universe side is an instrument too: an unclosed call site must ERROR,
    # not silently contribute zero findings.
    try:
        scan_text("asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }(),\n",
                  "<fixture>")
        print("  FAIL  unterminated co_spawn( must raise ParseError, not parse clean")
        failures += 1
    except ParseError:
        print("  ok    unterminated co_spawn( raises ParseError")

    # A `co_spawn` token the call-site pattern misses must RAISE, not contribute
    # zero findings — the traversal witness. Written as two arms so the failure
    # mode (silent skip) is forced individually.
    for name, src in (
        ("bare co_spawn mention with no call",
         "// see co_spawn\nint co_spawn;\n"
         "asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { co_return; }, tok);"),
        ("explicitly-templated co_spawn spelling",
         "asio::co_spawn<void>(ioc, [&]() -> asio::awaitable<void> { co_return; }, tok);"),
    ):
        try:
            scan_text(src, "<fixture>")
            print(f"  FAIL  {name}: must raise ParseError, not parse clean")
            failures += 1
        except ParseError:
            print(f"  ok    {name} raises ParseError")

    total = len(SELF_TEST_CASES) + 3
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
