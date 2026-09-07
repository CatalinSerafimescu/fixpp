r"""Blank C++ comments and literals while preserving every offset and newline.

⚠️ WHY THIS IS A MODULE AND NOT ANOTHER COPY. This repo keeps re-deriving this lexer:
`tools/check_sanitizer_test_carveouts.py`'s copy carries the comment "PORTED FROM THE
SIBLING LEXER, which is the point of this comment", i.e. the duplication was already known
and unfixed. ⚠️ **HOW MANY COPIES EXIST TODAY IS A MEASUREMENT, NOT A FACT TO CACHE HERE** --
an earlier revision of this paragraph wrote the number down, which is the defect this file's
own callers exist to catch. Derive it:

    git grep -ln 'def blank_non_code\|strip_comments_preserve_code\|def blank_comments'

What is durable is the CONDITION: `ci/pump-red-arm.sh` needs the STRICTEST variant, because
it REWRITES source at an offset. A line-preserving lexer is not enough for that; only an
offset-preserving one is.

⚠️ THE FAILURE IT PREVENTS FAILS TOWARD CLEAN, which is why it is worth a module. A
forced-MISS arm locates `run_window_then_ready(` after an anchor. #289's own migrated files
quote that idiom INSIDE explanatory comments. Matching raw text can therefore rewrite a
COMMENT: the arm then builds unforced, the test passes, and the driver reports SILENT --
which reads as "the migration does not report" rather than "the driver edited prose".

Ported from `ci/pump-census.sh`, which then DIVERGED from it -- the backslash-CRLF splice
branch existed only here -- and which #289 batch 19 has now folded onto this module. That
divergence is the whole argument for one source, and it is worth recording that it was
found by DIFFING the two bodies, not by either one failing: the census's copy was silently
missing an arm for years and nothing could have said so, because the tree has no CRLF
sources to reveal it.

⚠️ DO NOT WRITE THE NUMBER OF REMAINING COPIES HERE. Two earlier revisions did (one said
"VERBATIM", one said "copy number five") and both went stale without anyone touching this
line. `ci/pump-get-sweep.sh` defines a `blank_comments` that is NOT one of these -- it
blanks comments while KEEPING string literals, which its own controls depend on. Re-derive
the set with the recipe at the top of this file.

OFFSET-PRESERVING CONTRACT, relied on by every rewriting caller: the returned string has
EXACTLY the same length as the input, and a newline in the input is a newline in the output.

⚠️ THE UNIT IS A PYTHON CHARACTER, NOT A BYTE, and the distinction is not pedantry: this
operates on decoded text, so a multi-byte UTF-8 character inside a comment is replaced by a
one-byte space and `len(out) == len(src)` holds in CHARACTERS while the byte counts differ
(`/*e-acute*/X` is 7 bytes in, 6 out). Every caller must therefore index the SAME decoded
string it passed in -- which `ci/pump-red-arm.sh` does. A caller that seeks into the file by
byte offset, or re-reads it as bytes, will land in the wrong place on any file containing
non-ASCII, and this repo's sources are full of non-ASCII comment glyphs.
"""
import re

# Compiled once and matched AT AN OFFSET; see the note at its use site for why the offset
# form is load-bearing and the compile is not.
_RAW = re.compile(r'(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(')


def _is_digit_separator(src: str, i: int) -> bool:
    """Is `src[i]` (an apostrophe) a C++14 digit separator rather than a char literal?

    The standard puts a digit separator BETWEEN digits of a numeric literal, so the test
    is not "is it surrounded by alphanumerics" -- that misreads `u8'0'`, whose apostrophe
    is also preceded by a digit and followed by one. Walk back to the start of the token
    instead and require it to BEGIN with a digit, or with a `.` that a digit follows:
    `10'000`, `0x1F'FF` and `.1'0` do, `u8'0'` does not (its token starts `u`).
    """
    if i == 0 or i + 1 >= len(src):
        return False
    if not (src[i - 1].isalnum() and src[i + 1].isalnum()):
        return False
    j = i - 1
    while j >= 0 and (src[j].isalnum() or src[j] in "'."):
        j -= 1
    k = j + 1
    # A fractional-constant may OPEN with the dot: `.1'0` is a valid literal and
    # `c++ -fsyntax-only` accepts it. Requiring the first character to be a digit
    # rejected it and put the blanker back in the state this predicate exists to
    # prevent. Found by the batch-17 review, not by the controls written with the fix.
    if k < len(src) and src[k] == ".":
        k += 1
    return k < len(src) and src[k].isdigit()


def blank_non_code(source: str) -> str:
    """Blank comments and literals while preserving every newline."""
    out = []
    i = 0
    n = len(source)
    state = "code"
    quote = ""

    def blank(ch: str) -> str:
        return "\n" if ch == "\n" else " "

    while i < n:
        if state == "code":
            # Raw string literals, including u8R"...", uR, UR and LR.
            # ⚠️ `.match(source, i)` -- NOT `re.match(pat, source[i:])`. The slice copies
            # the whole remainder of the file at EVERY code character, which is O(n^2)
            # bytes per file. Measured over the 656 files under tests/ (10.0 MB): one
            # `blank_non_code` pass goes 13.3 s -> 4.1 s, and the two tier-1 gates that
            # call it go 30.2 s -> 10.5 s and 13.2 s -> 4.6 s. Output is byte-identical
            # over all 656 (the two forms can only differ under `^` or a lookbehind, and
            # this pattern has neither). Hoisting `re.compile` alone buys nothing --
            # Python already caches compiled patterns; the copy is the cost.
            raw = _RAW.match(source, i)
            if raw:
                token = raw.group(0)
                delim = raw.group(1)
                end_token = ")" + delim + '"'
                end = source.find(end_token, i + len(token))
                end = n if end < 0 else end + len(end_token)
                out.extend(blank(ch) for ch in source[i:end])
                i = end
                continue

            if source.startswith("//", i):
                out.extend((" ", " "))
                i += 2
                state = "line-comment"
            elif source.startswith("/*", i):
                out.extend((" ", " "))
                i += 2
                state = "block-comment"
            elif source[i] == "'" and _is_digit_separator(source, i):
                # C++14 digit separator (`10'000`, `0x1F'FF`) -- NOT a character
                # literal. Treated as one, it opens a literal that runs to the next
                # apostrophe anywhere in the file and blanks every line between,
                # code included. MEASURED, not hypothesised: one `10'000` in
                # tests/session/read_first_frame_bounded_test.cpp hid TWO labelled
                # seam calls from `ci/pump-label-uniqueness.sh`, which reported
                # "every site label is unique" over a tree it could not fully read.
                # ⚠️ THE DIRECTION IS FAILS-TOWARD-CLEAN FOR EVERY CALLER: the gate
                # sees fewer sites, and `ci/pump-red-arm.sh` -- which REWRITES source
                # at offsets taken from this lexer -- cannot find an anchor it has
                # blanked. Neither reports anything.
                out.append(source[i])
                i += 1
            elif source[i] in ('"', "'"):
                quote = source[i]
                out.append(" ")
                i += 1
                state = "literal"
            else:
                out.append(source[i])
                i += 1

        elif state == "line-comment":
            ch = source[i]
            # Backslash-newline splice (C++ phase 2, applied before comment
            # recognition in phase 3): a `\` immediately followed by a
            # newline continues the line comment onto the next physical
            # line, so the newline must NOT end the comment here. Both
            # characters are still blanked (preserving the physical line
            # count other callers rely on for path:line reporting).
            # ⚠️ CRLF SPLICES TOO. The sibling this was ported from recognised only
            # backslash-LF, so on CRLF input it ENDED the comment at the CR and handed the
            # next physical line back as code: `// hidden \<CRLF>run_window_then_ready(...)`
            # came out of the blanker with the call intact.
            #
            # ⚠️ SCOPE, because the first version of this comment overclaimed it: this is a
            # MODULE-CONTRACT defect, and it is NOT reachable through `ci/pump-red-arm.sh`.
            # That caller reads with `open(path, encoding="utf-8")`, whose universal-newline
            # translation turns CRLF into LF *before* `blank_non_code` sees it (measured: 39
            # bytes on disk, 38 characters as read, no CR), so the pre-existing LF branch
            # already covered it there. The fix stands for any caller reading bytes or
            # passing `newline=""` -- state what it protects, not a consequence it does not
            # have.
            if ch == "\\" and i + 1 < n and source[i + 1] == "\n":
                out.append(blank(ch))
                out.append(blank(source[i + 1]))
                i += 2
                continue
            if ch == "\\" and i + 2 < n and source[i + 1] == "\r" and source[i + 2] == "\n":
                out.append(blank(ch))
                out.append(blank(source[i + 1]))
                out.append(blank(source[i + 2]))
                i += 3
                continue
            out.append(blank(ch))
            i += 1
            if ch == "\n":
                state = "code"


        elif state == "block-comment":
            if source.startswith("*/", i):
                out.extend((" ", " "))
                i += 2
                state = "code"
            else:
                out.append(blank(source[i]))
                i += 1

        else:  # normal string or character literal
            ch = source[i]
            out.append(blank(ch))
            i += 1
            if ch == "\\" and i < n:
                out.append(blank(source[i]))
                i += 1
            elif ch == quote:
                state = "code"

    return "".join(out)


# ── Unevaluated operands ─────────────────────────────────────────────────────
#
# Lives here rather than in `ci/pump-get-sweep.sh`, where it was written (#289 batch
# 18), because `ci/pump-census.sh` carries the SAME `.get()` pattern and had no
# unevaluated blanking at all. That census was clean only because no in-tree
# `decltype(fut.get())` happened to sit inside a `run_for` window -- a property of
# the tree, not of the instrument. Moving it here is #289 batch 19 item 3.
#
# ⚠️ IT IS NOT PART OF `blank_non_code` AND MUST NOT BE FOLDED INTO IT. That one
# blanks text that is not CODE; this blanks text that IS code and is not EVALUATED.
# A caller wanting line-accurate comment blanking usually does NOT want a `decltype`
# operand erased -- `ci/pump-label-uniqueness.sh` harvests string literals, and a
# label inside a `decltype` would still be a real label. Two passes, two decisions.

_DECLTYPE = re.compile(r"\bdecltype\s*\(")

def blank_unevaluated(stmt):
    """Blank `decltype(...)` operands, preserving length.

    ⚠️ `using R = decltype(fut.get());` DOES NOT CALL `get()` -- the operand is
    unevaluated -- yet it matched this sweep's `.get()` pattern and produced a row.
    That row is worse than noise: the idiom is part of #316's SETTLED value-helper
    recipe, so it appears at sites that are already MIGRATED, and the sweep reported
    two of them as `CALLER-ONLY x HELPER` residue in a file whose very next line is
    `run_window_then_ready`. Migrating a value-returning helper therefore RAISED the
    residual, which is the one direction an instrument must never move.

    It also cost two rows a second way: the `since` walk treats any statement naming
    `<fut>.get()` as the end of that future's segment, so a `decltype` line truncated
    the window evidence and could flip RUN-BOUNDED to HELPER.

    Scope, stated so it is not read as more than it is: `decltype` is the only
    unevaluated context this blanks. `sizeof`, `noexcept` and `requires` can hold the
    same call and are NOT handled -- no instance exists in this tree today, and a
    speculative widening would need controls no site justifies. The controls below
    straddle the boundary this DOES draw.

    ⚠️ THE EARLY-OUT IS NOT AN OPTIMISATION TO TASTE: on this corpus ~10^5 statements
    reach here and a couple of DOZEN contain `decltype`, so without it the char-list copy
    runs about ten thousand times per useful call. No exact pair is written down -- an
    earlier revision did, and the numbers rotted inside the same PR when its own migration
    changed the corpus. Re-derive by counting `statements()` output against
    `'decltype' in stmt`. The early-out is exactly safe: `_DECLTYPE` requires that literal
    substring, so no statement the regex would match can be skipped by it."""
    if "decltype" not in stmt:
        return stmt
    out = list(stmt)
    for m in _DECLTYPE.finditer(stmt):
        # ⚠️ FIND THE CLOSING PAREN FIRST, THEN BLANK. Blanking as you walk erases to the
        # END OF THE STRING when the operand never closes -- and that direction is
        # FAILS-TOWARD-CLEAN: a real `.get()` after an unterminated `decltype(` would
        # vanish from the caller's scan with no diagnostic. It cannot happen for a caller
        # that passes SPLICED STATEMENTS (`ci/pump-get-sweep.sh`), where parens balance by
        # construction; it is reachable for a caller that passes a single LINE
        # (`ci/pump-census.sh`, which is line-based), where a wrapped operand closes on a
        # later line. Leaving such an operand ALONE instead costs at worst a false row --
        # loud, and against an EMPTY pin, so someone reads it.
        depth, i, close = 0, m.end() - 1, None
        while i < len(stmt):
            if stmt[i] == "(":
                depth += 1
            elif stmt[i] == ")":
                depth -= 1
                if depth == 0:
                    close = i
                    break
            i += 1
        if close is None:
            continue
        for j in range(m.end() - 1, close):
            out[j] = " "
    return "".join(out)

# ── Self-test ────────────────────────────────────────────────────────────────
#
# ⚠️ EVERY CASE HERE MUST INCLUDE A POSITIVE CONTROL -- a blanker that returns all spaces
# passes every "the call is hidden" assertion and is useless. `real code` is that control.
# Run: python3 ci/cxx_blank.py
if __name__ == "__main__":
    CALL = "run_window_then_ready"
    CASES = [
        # (name, source, must the CALL still be visible after blanking?)
        ("real code",           f"{CALL}(a); // trailing\n",                      True),
        ("line comment",        f"// {CALL}(a)\nint x;\n",                        False),
        ("block comment",       f"/* {CALL}(a) */\nint x;\n",                     False),
        ("string literal",      f'const char* s = "{CALL}(a)";\n',                False),
        ("LF line splice",      f"// hidden \\\n{CALL}(a);\n",                    False),
        ("CRLF line splice",    f"// hidden \\\r\n{CALL}(a);\n",                  False),
        ("raw string",          f'auto r = R"({CALL}(a))";\n',                    False),
        ("comment then code",   f"// note\n{CALL}(a);\n",                         True),
        # ── digit separators. BOTH DIRECTIONS, because the fix is a narrowing and a
        # narrowing that went too far would stop hiding real character literals --
        # the failure this module exists to prevent.
        ("digit separator",     f"int a = 10'000;\n{CALL}(a);\n",                 True),
        ("hex digit separator", f"int a = 0x1F'FF;\n{CALL}(a);\n",                True),
        ("char literal",        f"char c = '\\'';\n// {CALL}(a)\nint x;\n",       False),
        # `u8'0'` has a digit on BOTH sides of the apostrophe and is still a character
        # literal -- the case that rules out the cheap "surrounded by alphanumerics" test.
        ("u8 char literal",     f"auto c = u8'0';\n{CALL}(a);\n",                 True),
        # A fractional-constant OPENS with the dot. `c++ -std=c++23 -fsyntax-only` accepts
        # `double x = .1'0;`. The first version of `_is_digit_separator` required the token
        # to begin with a DIGIT and so ate this one -- caught in review, not by these
        # controls, which is why the case is checked in rather than described.
        ("leading-dot separator", f"double x = .1'0;\n{CALL}(a);\n",                True),
        # ⚠️ THE MEMBER NAME ENDS IN A DIGIT ON PURPOSE, so this control DISCRIMINATES the
        # widening above. Two earlier drafts did not: `obj.f'x'` and `obj.f'"'` both fail
        # the predicate's FIRST guard (the character after `'` is not alphanumeric), so the
        # walk-back never runs and the case passes whatever the walk-back says. Here both
        # neighbours ARE alphanumeric, so a rule loosened to "the token contains a digit"
        # calls this a separator, and the CLOSING apostrophe then opens a literal that
        # swallows the call below. Proven RED against exactly that mutant.
        ("member ending in a digit", f"auto c = obj.f9'0';\n{CALL}(a);\n",           True),
        ("literal still hides", f'auto s = "x{CALL}(a)y";\nint x;\n',             False),
    ]
    ok = True
    for name, src, want in CASES:
        out = blank_non_code(src)
        seen = CALL in out
        same_len = len(out) == len(src)
        same_nl = out.count("\n") == src.count("\n")
        good = seen == want and same_len and same_nl
        ok &= good
        print(f"  {'ok   ' if good else '!!BAD!!'} {name:<18} visible={seen} (want {want})"
              f" len={'=' if same_len else 'DIFFERS'} nl={'=' if same_nl else 'DIFFERS'}")
    import sys
    if not ok:
        sys.exit("\nCONTROL FAILED -- blank_non_code is not trustworthy. Fix before using it.")
    print("\nblank_non_code PROVEN: hides comments and literals, keeps real code, "
          "preserves length and newlines.")

    # ── blank_unevaluated: the SAME straddle the fix shipped with, moved here with it.
    # The operand must vanish AND a real call must survive, IN ONE fixture -- a blanket
    # exclusion (whole statement, whole line, everything after `decltype`) passes the
    # first half and fails the second.
    GET = "fut.get()"
    U_CASES = [
        ("decltype operand vanishes",
         "using R = decltype(fut.get());", False, None),
        ("a real call survives",
         "auto r = fut.get();", True, None),
        ("BOTH in one statement",
         "using R = decltype(fut.get()); auto r = fut.get();", True, 1),
        ("nested parens inside the operand",
         "using R = decltype(f(g(fut.get())));", False, None),
        ("no decltype -> returned unchanged",
         "auto r = fut.get();", True, 1),
        # ⚠️ THE FAILS-TOWARD-CLEAN EDGE. A line-based caller can hand us an operand that
        # never closes. Blanking as we walk would erase the rest of the line INCLUDING the
        # real call; leaving it alone costs a false row instead. Both halves are checked:
        # the trailing call must SURVIVE, and it must survive as a call, not as debris.
        ("UNTERMINATED operand is left alone",
         "using R = decltype(f(fut.get(); auto r = fut.get();", True, 2),
    ]
    u_ok = True
    for name, src, want, want_count in U_CASES:
        out = blank_unevaluated(src)
        seen = GET in out
        cnt = out.count(GET)
        good = (seen == want) and len(out) == len(src) and (
            want_count is None or cnt == want_count)
        u_ok &= good
        print(f"  {'ok   ' if good else '!!BAD!!'} {name:<32} visible={seen} count={cnt}"
              f" len={'=' if len(out) == len(src) else 'DIFFERS'}")
    if not u_ok:
        sys.exit("\nCONTROL FAILED -- blank_unevaluated is not trustworthy.")
    print("blank_unevaluated PROVEN: erases the operand, keeps the real call, "
          "preserves length.")
