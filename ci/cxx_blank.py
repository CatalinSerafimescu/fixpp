"""Blank C++ comments and literals while preserving every byte offset and newline.

⚠️ WHY THIS IS A MODULE AND NOT A FIFTH COPY. This lexer already existed FOUR times in
this repo -- `ci/pump-census.sh`, `ci/pump-get-sweep.sh`, `tools/check_sanitizer_test_carveouts.py`
(whose own comment reads "PORTED FROM THE SIBLING LEXER, which is the point of this
comment") and `tools/check_dictionary_snapshot_exclusivity.sh`. `ci/pump-red-arm.sh` needed
a fifth, and it needed the strictest variant: it REWRITES source at a byte offset, so its
lexer must be offset-preserving, not merely line-preserving.

⚠️ THE FAILURE IT PREVENTS FAILS TOWARD CLEAN, which is why it is worth a module. A
forced-MISS arm locates `run_window_then_ready(` after an anchor. #289's own migrated files
quote that idiom INSIDE explanatory comments. Matching raw text can therefore rewrite a
COMMENT: the arm then builds unforced, the test passes, and the driver reports SILENT --
which reads as "the migration does not report" rather than "the driver edited prose".

Ported VERBATIM from `ci/pump-census.sh`. That copy and `pump-get-sweep.sh`'s stay where
they are for now: each is pinned by its own harness (`ci/test-pump-census.sh`), so folding
them in is a change to a tested instrument and belongs in its own PR, not in a batch that
merely needed to stop making copy number five. Consolidating the remaining copies onto this
module is recorded as follow-up.

Offset-preserving contract, relied on by every rewriting caller: the returned string has
EXACTLY the same length as the input, and a newline in the input is a newline in the output.
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
            if ch == "\\" and i + 1 < n and source[i + 1] == "\n":
                out.append(blank(ch))
                out.append(blank(source[i + 1]))
                i += 2
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
