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

Ported from `ci/pump-census.sh` and since DIVERGED: the backslash-CRLF splice branch exists
only here. That divergence is deliberate and is the reason to prefer this module -- but it
means a `diff` against the sibling is no longer expected to be empty, and the sibling still
carries the gap. (An earlier revision of this line said "VERBATIM", which stopped being true
the moment the CRLF branch was added.) That copy and `pump-get-sweep.sh`'s stay where
they are for now: each is pinned by its own harness (`ci/test-pump-census.sh`), so folding
them in is a change to a tested instrument and belongs in its own PR, not in a batch that
merely needed to stop making copy number five. Consolidating the remaining copies onto this
module is recorded as follow-up.

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
            raw = re.match(
                r'(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(',
                source[i:]
            )
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
