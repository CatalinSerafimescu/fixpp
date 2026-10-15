#!/usr/bin/env bash
# Assert that every #289 site label is UNIQUE across tests/.
#
# WHY THIS GATES
# ─────────────────────────────────────────────────────────────────────────────
# A site label has exactly one job that needs uniqueness: `FIXPP_FORCE_WINDOW_MISS`
# is matched by `std::strcmp` at the seam (`forced_miss_here`), so forcing a label
# fires at EVERY site carrying it. Two sites sharing a label are two failures, not
# one inconvenience:
#
#   - same binary  -> forcing hits both. The arm's verdict is then about whichever
#                     site the test reaches first, and the driver cannot tell.
#   - two binaries -> nothing downstream can tell a duplicate from the LEGITIMATE
#                     multi-binary case, which is a shared header compiled into
#                     several TUs. `ci/pump-seam-arm.sh` locates labels by `strings`
#                     and then runs EVERY binary carrying one (`BINS_FOR` is a list
#                     and `run_label` loops over it), so it does not "pick wrong" --
#                     it cannot distinguish the two situations even in principle.
#                     That is why the check has to live at the SOURCE level: there
#                     is no binary-level observation that could replace it.
#
# The shape that produces one is not exotic: a label named after a FIXTURE HELPER
# (`drive_to_active/open`, `feed/frame`) is unique only by accident, because two
# fixtures in different files routinely carry helpers of the same name -- and the
# accident ends the moment a SHARED HEADER is migrated, since its sites compile
# into every including TU. #289 batch 16 introduced five such collisions in one
# change and caught them only by looking; this exists so the next one cannot ship.
#
# ⚠️ THE OBVIOUS ONE-LINE GREP FOR THIS IS BROKEN, AND IT FAILS TOWARD CLEAN.
# `git grep -oE 'run_window_then_ready\([^;]*"[^"]+"\)'` reads the label only when
# the whole call fits on ONE PHYSICAL LINE, and the call is routinely wrapped with
# the label alone on the continuation line. It reported "no duplicates" over the
# tree that had all five. No ratio is written here -- it is a reading of a moving
# tree and it had already drifted between being measured and being committed. The
# CONDITION is the whole argument: the statement must be SPLICED before the label
# can be read, which is why this is a script and not a grep.
# [[feedback_every_broken_instrument_in_this_repo_fails_toward_clean]]
#
# ⚠️ THE NAMESPACE IS THE SEAM'S, NOT ONE CALL SPELLING'S. `forced_miss_here`
# (tests/support/pump_until_ready.hpp) is reached from BOTH primitives -- from
# `pump_until` and from `run_window_then_ready` -- and `pump_until_ready` forwards a
# `const char* site` into the first. All of them therefore share ONE strcmp
# namespace, so scanning only `run_window_then_ready(` would let a labelled
# `pump_until_ready` site collide with a window-site label and pass this gate green.
# Every spelling of the PRIMITIVES is scanned; each has its own control below.
#
# ⚠️ "EVERY SPELLING THAT REACHES THE SEAM" IS WHAT AN EARLIER DRAFT SAID AND IT IS
# TOO STRONG. A project WRAPPER around a primitive is one lexical call here however
# many callers it has: `InteropEngineFixture::run_until` (tests/interop/support/
# interop_fixture.cpp) forwards to `pump_until`, and its ~two dozen `fx.run_until(...)`
# callers are not seam calls at all -- the single `pump_until(` inside the wrapper is.
# ⚠️ THE ARGUMENT THAT USED TO STAND HERE IS FALSE. It read: "the duplicate verdict
# survives that, and the reason is structural rather than lucky -- a wrapper can only
# introduce a label if it TAKES one, and one that takes one is itself a labelled call this
# gate reads." #405 refuted it by writing the counterexample. A wrapper that takes a
# `const char* site` and FORWARDS it to the seam leaves a NON-LITERAL in the seam call's
# extent: the wrapper read as one UNLABELLED site, and every label its callers pass was
# invisible here. Taking a label and being readable as labelled are different properties,
# and the old sentence conflated them. A duplicate seeded through such a wrapper left this
# gate green, and a real one shipped before it was found by reading (#591).
#
# ⚠️ SO FORWARDERS ARE NOW DERIVED, NOT LISTED (#591). When a seam call's label is not a
# literal, and one of its arguments is a bare identifier naming a `const char*` parameter of
# the innermost function DEFINITION enclosing the call, that function is a forwarder: each
# CALL of it becomes a site, carrying the literal that call passes at the parameter's
# position, in the same label namespace as every direct site. A caller that passes its OWN
# such parameter makes its function a forwarder too, to a fixpoint. No forwarder is named
# anywhere in this file, because a hand-kept list is a private copy of the population, and
# it rots the first time a fixture grows a helper.
#
# What the derivation reads, and what it cannot -- the CONDITION, re-checked by reading
# `--list-unlabelled` (below), never a count:
#   * The callers of a forwarder are read in its own file and, when it is a header, in every
#     file that #includes that header by name. A forwarder whose callers reach it some other
#     way (a declaration in one file and the definition in another) has none read.
#   * A call is the forwarder's only at its arity: between its parameters without a default
#     and all of them. A same-named function of an overlapping arity in that scope is read as
#     the forwarder.
#   * A comma between a `<` and a `>` may separate arguments (comparisons) or not (a
#     template argument list). A call holding one is read as template argument lists only
#     when that fills every parameter, so that splitting any of those commas would exceed
#     the arity. Any other such call is listed UNLABELLED at the call, unless even splitting
#     every comma leaves it short of the required parameters, which no reading can reach. A
#     forwarder's own parameter list is always read as template argument lists, which is
#     wrong only for a comparison in a default argument.
#   * A lambda that takes the label as its own parameter is not a function definition here;
#     its seam call stays unlabelled. So does a constructor whose initialiser list holds the
#     seam call, and a parameter of any other type (`std::string_view`, `std::string`).
#   * ⚠️ THE FLOOR, AND WHERE IT STOPS: a forwarder none of whose calls was read is listed
#     UNLABELLED at its seam call, and a call passing no literal, or whose commas are read
#     two ways as above, is listed UNLABELLED at the call (`via`). A call OUTSIDE the scope
#     in the first bullet is not covered: once any call of that forwarder was read, the
#     out-of-scope call's label reaches neither the duplicate check nor the unlabelled list.
#
# ⚠️ A NAMED CONSTANT IS STILL NOT CHASED. A label held in a variable
# (`static constexpr const char* kSite = "..."; run_window_then_ready(ioc, fut, w, kSite)`)
# is recorded as UNLABELLED rather than rejected. Resolving an identifier needs a symbol
# table, and a detector that resolves the spellings its author thought of is the failure
# mode ci/pump-get-sweep.sh's header already warns against. Disclosed instead. For such a
# site the check to prefer is the RUNTIME one -- force each label with
# FIXPP_FORCE_WINDOW_MISS and assert that no OTHER label fires.
#
# ⚠️ NO COUNT OF SUCH SITES IS WRITTEN HERE, ON PURPOSE. An earlier revision said "no
# migrated site uses that shape today" and one commit falsified it. RE-DERIVE instead:
#
#     bash ci/pump-label-uniqueness.sh --list-unlabelled
#
# which prints every forwarder found, with the number of its calls read, and then every
# unlabelled site; read each one. Use the flag rather than a hand-written grep: a
# line-oriented `grep ... | grep -v '"'` calls every labelled MULTI-LINE call unlabelled,
# because the literal sits on a following line. That over-count was measured before the
# flag existed and it was not marginal -- it was most of the file population.
#
# An UNLABELLED call (`run_window_then_ready(ioc, fut, 100ms)`) is legal -- it is a
# site the forcing seam cannot reach. Those are counted and reported, never failed:
# adopting the seam at them is a separate axis of #289.
#
# Usage:  bash ci/pump-label-uniqueness.sh [--root DIR] [--list-unlabelled]
# Exit 1 on a duplicate label, or on a failed self-test control.
#
# --list-unlabelled prints every forwarder found (#591), then every site this scanner could
# not read a label for, as `file:line`. That is the enumeration the "re-derive it" note
# above calls for; it uses the
# scanner's own extent and comment/string blanking, so it does NOT have the failure mode of
# a hand-written `grep ... | grep -v '\"'`, which reports a labelled MULTI-LINE call as
# unlabelled because the literal sits on the next line.

set -euo pipefail

fail() { echo "pump-label-uniqueness: error: $*" >&2; exit 1; }

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_root="$repo_root"
list_unlabelled=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --root) [ "$#" -ge 2 ] || fail "--root requires an argument"; scan_root="$2"; shift 2 ;;
        --list-unlabelled) list_unlabelled=1; shift ;;
        -h|--help) awk 'NR==1 || /^#/ {print; next} {exit}' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

command -v python3 >/dev/null || fail "python3 is required"

FIXPP_CI_DIR="$repo_root/ci" python3 - "$scan_root" "$list_unlabelled" <<'PY'
import os, re, sys
from pathlib import Path

# ⚠️ RESOLVE THE MODULE FROM THE SCRIPT'S OWN ci/, NOT FROM --root: `--root` may point
# at another worktree, which would import that tree's blanker to judge this one.
sys.path.insert(0, os.environ["FIXPP_CI_DIR"])
from cxx_blank import blank_non_code

root = Path(sys.argv[1])

_STR = re.compile(r'"(?:[^"\\\n]|\\.)*"')
# Every spelling that reaches `forced_miss_here` -- see the header. The list is the
# gate's SCOPE, so widening it is a deliberate act with a control, not a tweak.
CALLS = ("run_window_then_ready(", "run_to_exhaustion_or_report(",
         "pump_until_ready(", "pump_until(", "yield_window_then_ready(")


def _join_adjacent(lits, text, gap_view=None):
    """The label is the TRAILING RUN of adjacent string literals, concatenated.

    ⚠️ NOT `lits[-1]`, and this is a fix, not a refinement. C++ concatenates adjacent
    string literals, and `clang-format` SPLITS a long one at the column limit -- so
    `"Suite::Case/close_fut_a"` becomes `"Suite::Case/" "close_fut_a"` in the source
    while the runtime label is unchanged. Taking the last literal harvested
    `close_fut_a`, i.e. a DIFFERENT string from the one the seam actually compares.
    MEASURED in #289 batch 17: `clang-format` split two labels this way and the gate
    then reported `410 seam sites (409 distinct label(s))` -- a duplicate that does not
    exist.
    ⚠️ AND THE DANGEROUS DIRECTION IS REACHABLE, not just this false alarm: two sites
    carrying the SAME label, one split and one not, harvest as `tail` and `full` and
    read as DISTINCT. That is a real collision the gate would pass. Same defect, other
    sign.

    Adjacency is "nothing but whitespace between them", which is what the C++ rule is.
    A literal separated by a comma is a different ARGUMENT and must not be joined.

    ⚠️ THE GAP IS MEASURED ON A COMMENT-BLANKED VIEW, NOT ON THE RAW SOURCE. C++ joins
    adjacent literals across a COMMENT too, and `lits` has already had comment-resident
    literals filtered out -- so testing the raw gap re-opened exactly the false-clean
    direction this function was written to close: two sites whose runtime label is
    identically `Suite::Case/close_fut_a`, one of them wrapped with `// wrapped for width`
    between the halves, harvested as `full` and `tail` and read as DISTINCT. Measured:
    `4 seam sites (4 distinct)` with the comment, `4 (1 distinct)` without it. Found by
    the batch-17 hostile round, one round after the fix that introduced the gap test.

    ⚠️ AND ONE LIMITATION THAT IS DISCLOSED RATHER THAN FIXED: the harvested value is the
    literal's SOURCE bytes, so `"a\tb"` and a literal tab inside quotes are one runtime
    label and two harvested ones. Decoding escapes here would need the full C++ escape
    grammar and would make the harvest disagree with `strings`-based drivers, which also
    read source-shaped bytes out of the binary. The condition, not a count: a label
    containing a backslash escape is outside what this gate can compare.
    """
    if not lits:
        return None
    # ⚠️ A RAW-STRING LABEL IS HARVESTED WRONG, AND THIS REFUSES RATHER THAN GUESSES.
    # `_STR` matches the `"(A/open)"` inside `R"(A/open)"`, so the harvest would return
    # `(A/open)` while the runtime label is `A/open` -- and a duplicate between
    # `R"(A/open)"` and `"A/open"` would then read as DISTINCT, which is the fails-toward-
    # clean direction this gate exists to close. Nothing in the tree spells a label that
    # way today; supporting it needs the delimiter grammar, and a wrong answer is worse
    # than a refusal. Raised, not returned, so it cannot be mistaken for a label.
    for start, _end, _val in lits:
        if start > 0 and text[start - 1] == "R":
            raise SystemExit("pump-label-uniqueness: error: raw-string site label at offset "
                             f"{start}. Spell the label as an ordinary string literal.")
    view = gap_view if gap_view is not None else text
    out = [lits[-1][2]]
    for k in range(len(lits) - 1, 0, -1):
        gap = view[lits[k - 1][1]:lits[k][0]]
        if gap.strip():
            break
        out.insert(0, lits[k - 1][2])
    return "".join(out)


def calls(text):
    """Yield (line, label_or_None) for every call into the forcing seam.

    Reads forward from the call's own '(' and balances parens, so a call wrapped over
    any number of lines is one call -- the blind spot named in the header.

    ⚠️ THE EXTENT IS FOUND ON A BLANKED COPY AND THE LABEL IS READ FROM THE ORIGINAL.
    `blank_non_code` blanks string literals as well as comments -- which is what makes
    the paren balance trustworthy (a stray '(' in a literal cannot skew it) and is also
    why the label cannot be read from it. The module's offset-preserving contract is
    what lets the two be mixed: an index found on the blanked copy addresses the same
    character of `text`.

    ⚠️ AND THE HARVEST MUST REJECT LITERALS INSIDE COMMENTS, which the first version did
    not: it took `lits[-1]` from the raw original, so a call carrying
    `/* renamed from "OLD/open" */` in its own argument list reported `OLD/open` as the
    label. MEASURED: a tree with two genuinely identical labels, each with such a comment,
    exited 0 saying "every site label is unique". The control that was supposed to cover
    this put the comment on the line BEFORE the call, i.e. outside the extent, so it proved
    a strictly narrower claim than the docstring made.

    The comment span is derived rather than re-lexed: replace every apparent literal with
    an equal-length filler and blank THAT -- with no literals left, whatever `blank_non_code`
    removes is a comment. `//` and `/*` inside a real literal are neutralised by the filler,
    so the comment boundaries come out right without a second lexer."""
    return sorted((s["line"], s["label"]) for s in seam_calls(View(text)))


class View:
    """One source text and the derived views every reader below shares: the blanked copy
    (comments AND literals blanked, for balancing), the comment-only blanked copy (for the
    literal-adjacency gap) and a per-offset is-comment map (to drop commented literals).
    Built once per file, so a forwarder's callers are read without re-lexing."""

    def __init__(self, text):
        self.text = text
        self.blanked = blank_non_code(text)
        filled = _STR.sub(lambda m: "Z" * len(m.group(0)), text)
        self.comment_blanked = blank_non_code(filled)
        self.is_comment = [self.comment_blanked[k] != filled[k] for k in range(len(text))]
        self._braces = None

    def line(self, pos):
        return self.blanked.count("\n", 0, pos) + 1

    def label_in(self, start, end):
        """The label a span of source carries: its trailing run of adjacent literals."""
        lits = [(lm.start(), lm.end(), lm.group(0)[1:-1])
                for lm in _STR.finditer(self.text, start, end)
                if not self.is_comment[lm.start()]]
        return _join_adjacent(lits, self.text, self.comment_blanked)

    def braces(self):
        """[(open, close)] of every brace pair, innermost-last for a given position."""
        if self._braces is None:
            pairs, stack = [], []
            for k, c in enumerate(self.blanked):
                if c == "{":
                    stack.append(k)
                elif c == "}" and stack:
                    pairs.append((stack.pop(), k))
            self._braces = sorted(pairs)
        return self._braces


def _close(blanked, i):
    """The index of the bracket closing the one at `i`, balancing (), [] and {}."""
    depth = 0
    for j in range(i, len(blanked)):
        c = blanked[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return j
    return len(blanked)


def _open(blanked, j):
    """The index of the '(' that the ')' at `j` closes, or -1."""
    depth = 0
    for k in range(j, -1, -1):
        c = blanked[k]
        if c in ")]}":
            depth += 1
        elif c in "([{":
            depth -= 1
            if depth == 0:
                return k
    return -1


def _top_args(blanked, i, j, pairs=()):
    """The (start, end) spans of the top-level arguments between the '(' at `i` and the ')'
    at `j`. Commas inside (), [] and {} do not split -- a lambda argument holds them.
    Commas inside a template argument list split too, unless its `<`..`>` is in `pairs`
    (from `_angles`): `<` is also less-than, so balancing it is a reading, not a fact."""
    inside = set()
    for a, b in pairs:
        inside.update(range(a, b))
    spans, depth, start = [], 0, i + 1
    for k in range(i + 1, j):
        c = blanked[k]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0 and k not in inside:
            spans.append((start, k))
            start = k + 1
    if spans or blanked[start:j].strip():
        spans.append((start, j))
    return spans


def _angles(blanked, i, j):
    """The (`<`, `>`) index pairs at the top level between the '(' at `i` and the ')' at `j`
    that could be a template argument list: each `<` matched with the next unmatched `>`,
    nesting like brackets. `<<`, `<=`, `>=`, the `>` of `->` and a bracket with no partner
    are not paired; none of them can delimit a template argument list."""
    pairs, stack, depth = [], [], 0
    for k in range(i + 1, j):
        c = blanked[k]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif depth != 0 or c not in "<>":
            continue
        elif c == "<":
            if blanked[k + 1:k + 2] not in ("<", "=") and blanked[k - 1] != "<":
                stack.append(k)
        elif blanked[k - 1] != "-" and blanked[k + 1:k + 2] != "=" and stack:
            pairs.append((stack.pop(), k))
    return pairs


def seam_calls(view):
    """Every call into the forcing seam in one file, as a dict: `line`, `label` (or None),
    and `idents`, the arguments that are a bare identifier -- one of which may be a
    `const char*` parameter the enclosing function forwards (see `forwarded_param`)."""
    blanked, out = view.blanked, []
    for call in CALLS:
        for m in re.finditer(re.escape(call), blanked):
            i = m.end() - 1                      # at the '('
            depth, j = 0, i
            while j < len(blanked):
                c = blanked[j]
                if c == "(":
                    depth += 1
                elif c == ")":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            idents = [blanked[a:b].strip() for a, b in _top_args(blanked, i, j)]
            out.append({"pos": m.start(), "line": view.line(m.start()),
                        "label": view.label_in(i, j),
                        "idents": [x for x in idents if _IDENT.fullmatch(x)]})
    return out


# ── FORWARDERS (#591) ────────────────────────────────────────────────────────
# A function that takes the label as a `const char*` parameter and hands it to a seam call
# leaves a NON-LITERAL at that call; the labels live at the function's own callers. These
# readers find such a function from the seam call itself -- nothing names it -- and read
# the literal each caller passes at the parameter's position, to a fixpoint, so a forwarder
# that is called with ITS caller's parameter is followed one level further.
_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_NOT_A_FUNCTION = {"if", "for", "while", "switch", "catch", "return", "sizeof", "decltype",
                   "alignof", "noexcept", "static_assert", "requires", "co_return"}
# What may stand between a function's ')' and its body's '{': cv/ref/exception qualifiers,
# override/final, a lambda's `mutable`, and a trailing return type.
_HEAD_TAIL = re.compile(r"(?:\s|const\b|volatile\b|noexcept\b|override\b|final\b|mutable\b|&|"
                        r"->[\w\s:<>,*&]*)*")


def _char_ptr_param(param, name):
    return re.fullmatch(r"\s*(?:const\s+char|char\s+const)\s*\*\s*(?:const\s+)?"
                        + re.escape(name) + r"\s*(?:=.*)?", param, re.S) is not None


def enclosing_function(view, pos):
    """(name, '(' index, ')' index) of the innermost function DEFINITION whose body holds
    `pos`, or None. A lambda body or a control-flow block is stepped over, so a label
    captured by a lambda inside the function is still that function's parameter."""
    blanked = view.blanked
    for o, c in reversed(view.braces()):
        if not o < pos < c:
            continue
        rp = blanked.rfind(")", 0, o)
        if rp < 0 or not _HEAD_TAIL.fullmatch(blanked[rp + 1:o]):
            continue
        lp = _open(blanked, rp)
        nm = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", blanked[:max(lp, 0)])
        if lp < 0 or nm is None or nm.group(1) in _NOT_A_FUNCTION:
            continue
        return nm.group(1), lp, rp
    return None


def forwarded_param(view, pos, idents):
    """When the seam-side call at `pos` passes, as a bare identifier, a `const char*`
    parameter of its enclosing function, -> the forwarder record; else None."""
    fn = enclosing_function(view, pos)
    if fn is None:
        return None
    name, lp, rp = fn
    # A parameter list holds no comparison outside a default argument, so its angle
    # brackets are read as template argument lists, whose commas separate no parameter.
    params = [view.blanked[a:b]
              for a, b in _top_args(view.blanked, lp, rp, _angles(view.blanked, lp, rp))]
    for k, p in enumerate(params):
        if any(_char_ptr_param(p, x) for x in idents):
            variadic = any("..." in q for q in params)
            return {"fn": name, "lp": lp, "index": k,
                    "required": sum(1 for q in params if "=" not in q and "..." not in q),
                    "total": float("inf") if variadic else len(params)}
    return None


def _includes(view, header_name):
    return re.search(r'^\s*#\s*include\s*[<"](?:[^>"]*/)?' + re.escape(header_name) + r'[>"]',
                     view.text, re.M) is not None


def forwarder_callers(views, def_name, fw):
    """Yield (name, line, label, nested, why) for each call of the forwarder `fw` defined in
    `def_name`: `label` is the literal the caller passes at the parameter's position, else
    None; `nested` is a forwarder record when that argument is itself a `const char*`
    parameter of the caller's enclosing function; `why`, when not None, says why a call
    with neither was not read.

    SCOPE: the defining file, and, when that file is a header, every file that #includes
    it. A call is one of THIS function's only at its arity (between the parameters without
    a default and all of them): a same-named call with fewer arguments, e.g. `ioc.run()`
    for a forwarder named `run`, is not read, and neither is one with more when none of its
    commas is a reading (below). A declaration (`char*` in that argument) is not a call.
    The definition's own head is excluded by position.

    A top-level comma between a `<` and a `>` (`_angles`) makes the split a reading: it
    separates arguments if they are comparisons and does not if they delimit a template
    argument list, and each choice per pair is a reading of its own. Such a call is read
    with every pair as a template argument list only when that split fills every parameter,
    since a reading that splits any of those commas then has more arguments than the
    forwarder takes. Otherwise, unless it has fewer arguments than required under every
    reading (not read, as above), it is yielded with `why` set, so it is listed unlabelled,
    never dropped."""
    def_view = views[def_name]
    scope = [def_name]
    if def_name.endswith((".hpp", ".h")):
        base = def_name.rsplit("/", 1)[-1]
        scope += [n for n, v in views.items() if n != def_name and _includes(v, base)]
    rx = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(fw["fn"]) + r"\s*\(")

    def fits(s):
        return fw["required"] <= len(s) <= fw["total"]

    for name in scope:
        v = views[name]
        for m in rx.finditer(v.blanked):
            i = m.end() - 1
            if v is def_view and i == fw["lp"]:
                continue
            j = _close(v.blanked, i)
            spans = _top_args(v.blanked, i, j)
            if len(spans) < fw["required"]:
                continue                       # no reading of its commas adds an argument
            kept = _top_args(v.blanked, i, j, _angles(v.blanked, i, j))
            if kept != spans:
                if len(kept) != fw["total"]:
                    yield (name, v.line(m.start()), None, None,
                           "arguments split two ways at a '<' or '>'")
                    continue
                spans = kept
            elif not fits(spans):
                continue
            if fw["index"] >= len(spans):
                yield name, v.line(m.start()), None, None, None    # the default is passed
                continue
            a, b = spans[fw["index"]]
            if re.search(r"\bchar\s*\*", v.blanked[a:b]):
                continue                                           # a declaration
            label = v.label_in(a, b)
            nested = None
            if label is None and _IDENT.fullmatch(v.blanked[a:b].strip()):
                nested = forwarded_param(v, m.start(), [v.blanked[a:b].strip()])
            yield name, v.line(m.start()), label, nested, None


# ── SELF-TEST on SYNTHETIC fixtures, straddling the boundary this gate draws ──
# Synthetic, not real files: a control anchored to a real file asserts a contingent
# fact about today's tree, and a later reader cannot tell a rotted anchor from a
# broken instrument.
_ONE_LINE = '''
    if (!run_window_then_ready(ioc, fut, 200ms, "A/open")) { return; }
'''
_WRAPPED = '''
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms,
                                                    "B/open")) {
        return;
    }
'''
_DEEPLY_WRAPPED = '''
    if (!fixpp::test_support::run_window_then_ready(
            ioc, fut, std::chrono::milliseconds{200},
            "C/open")) {
        return;
    }
'''
_UNLABELLED = '''
    if (!run_window_then_ready(ioc, fut, 100ms)) { return; }
'''
_LABEL_IN_A_COMMENT = '''
    // an older revision passed "GHOST/open" here
    if (!run_window_then_ready(ioc, fut, 200ms, "D/open")) { return; }
'''
_NESTED_PARENS = '''
    if (!run_window_then_ready(ioc, fut, std::chrono::milliseconds{200}, "E/open")) { return; }
'''
# One per extra spelling, so widening the CALLS tuple is proven non-zero rather than
# asserted. Without these the widening would be a claim about scope with no witness.
_PUMP_UNTIL_READY = '''
    if (!fixpp::test_support::pump_until_ready(ioc, fut, 5s, "F/open")) { return; }
'''
_RUN_TO_EXHAUSTION = '''
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "J/open")) { return; }
'''
_PUMP_UNTIL = '''
    if (!fixpp::test_support::pump_until(ioc, [&] { return done; }, 5s, 1ms, "G/settle")) { return; }
'''
# The coroutine-side spelling (#289 batch 19). It reaches the SAME `forced_miss_here`
# strcmp namespace, so a label it carries can collide with a window-site label.
_YIELD_WINDOW = '''
    if (!co_await fixpp::test_support::yield_window_then_ready(fd, 8, "L/drain")) { co_return; }
'''
# ⚠️ THE COMMENTED LITERAL MUST COME AFTER THE REAL LABEL, and that is the whole control.
# The harvest takes the LAST literal in the extent, so a comment placed BEFORE the label --
# or on the line above the call, as an earlier control had it -- leaves the right answer
# standing and proves nothing. Only a trailing one discriminates.
_COMMENT_INSIDE_THE_ARGUMENT_LIST = '''
    if (!run_window_then_ready(ioc, fut, 200ms, "I/open"
                               /* was "GHOST/open" */)) {
        return;
    }
'''
# ⚠️ clang-format SPLITS a label that crosses the column limit, and C++ concatenates the
# halves back. The harvest must too -- see `_join_adjacent`. A literal separated by a
# COMMA is a different argument and must NOT be joined, which is the second case.
_SPLIT_LITERAL = '''
    if (!fixpp::test_support::run_window_then_ready(
            ioc, fut, 200ms,
            "Suite::LongCaseNameThatCrossesTheColumnLimit/"
            "close_fut_a")) {
        return;
    }
'''
# ⚠️ C++ joins adjacent literals ACROSS A COMMENT, and the first version of the gap test
# read the RAW source, so a comment between the halves broke the join and re-opened the
# false-clean direction the join was written to close.
_SPLIT_ACROSS_A_COMMENT = '''
    if (!fixpp::test_support::run_window_then_ready(
            ioc, fut, 200ms,
            "Suite::Case/"  // wrapped for width
            "close_fut_a")) {
        return;
    }
'''
_COMMA_SEPARATED_ARGS = '''
    if (!fixpp::test_support::pump_until(ioc, [&] { return done; }, 5s, 1ms, "K/settle")) {
        ADD_FAILURE() << "unrelated";
    }
'''
_LABEL_IN_A_STRING = '''
    ADD_FAILURE() << "the old call was run_window_then_ready(ioc, fut, 200ms, \\"GHOST/open\\")";
    if (!run_window_then_ready(ioc, fut, 200ms, "H/open")) { return; }
'''
_CASES = [
    ("one-line call", _ONE_LINE, ["A/open"]),
    ("wrapped call -- the blind spot", _WRAPPED, ["B/open"]),
    ("deeply wrapped call", _DEEPLY_WRAPPED, ["C/open"]),
    ("unlabelled call is not a label", _UNLABELLED, [None]),
    ("a label inside a comment is not a label", _LABEL_IN_A_COMMENT, ["D/open"]),
    ("nested parens in an argument", _NESTED_PARENS, ["E/open"]),
    ("pump_until_ready reaches the same seam", _PUMP_UNTIL_READY, ["F/open"]),
    ("pump_until reaches the same seam", _PUMP_UNTIL, ["G/settle"]),
    ("run_to_exhaustion_or_report reaches the same seam", _RUN_TO_EXHAUSTION, ["J/open"]),
    ("yield_window_then_ready reaches the same seam", _YIELD_WINDOW, ["L/drain"]),
    ("a SPLIT literal is ONE label", _SPLIT_LITERAL,
     ["Suite::LongCaseNameThatCrossesTheColumnLimit/close_fut_a"]),
    ("a SPLIT literal joined ACROSS A COMMENT", _SPLIT_ACROSS_A_COMMENT, ["Suite::Case/close_fut_a"]),
    ("a comma-separated literal is a different argument", _COMMA_SEPARATED_ARGS, ["K/settle"]),
    ("a call quoted in a STRING is not a call", _LABEL_IN_A_STRING, ["H/open"]),
    # ⚠️ INSIDE the argument list, not on the line before it -- the earlier control put it
    # before the call, outside the extent, and therefore never exercised the harvest.
    ("a literal COMMENTED OUT inside the arg list", _COMMENT_INSIDE_THE_ARGUMENT_LIST, ["I/open"]),
]
# ⚠️ THE REFUSAL IS A CONTROL TOO. A guard that raises is worth exactly as much as a
# guard that returns, and only running it says which one this is.
_RAW_STRING_LABEL = '''
    if (!run_window_then_ready(ioc, fut, 200ms, R"(RAW/open)")) { return; }
'''
try:
    _got = [lab for _, lab in calls(_RAW_STRING_LABEL)]
    print(f"  WRONG raw-string label was accepted as {_got} instead of refused")
    _raw_ok = False
except SystemExit as _e:
    print(f"  ok    a RAW-STRING label is REFUSED, not guessed ({_e})")
    _raw_ok = True

bad = [] if _raw_ok else ["raw-string label was not refused"]
for name, src, want in _CASES:
    got = [lab for _, lab in calls(src)]
    mark = "ok   " if got == want else "WRONG"
    print(f"  {mark} {name:<42} -> {got} (want {want})")
    if got != want:
        bad.append(f"{name}: got {got}, want {want}")


# ⚠️ THE VERDICT IS COMPUTED BY THE FUNCTION THE READING CALLS, not by a twin of it.
# The first draft had a `duplicates(tree)` used only by the controls while the reading
# re-implemented the same accumulate-and-filter inline -- so both controls proved a
# function that produced no shipped verdict. Taking an iterable of (name, text) lets the
# synthetic trees and the real files go through the SAME code.
def tally(items, forwarders_out=None):
    """-> (seen, unlabelled, files) over an iterable of (display_name, source).

    `seen` maps label -> [where...]; a label with more than one entry is a duplicate.
    `unlabelled` is a LIST of `file:line`, not a count -- `--list-unlabelled` prints it,
    because "re-derive the population yourself" is only honest advice if the population
    can actually be enumerated with this scanner rather than with a hand-rolled grep.

    A seam call whose label is a forwarded `const char*` parameter (#591) is not a site of
    its own: each CALL of the forwarder is, carrying the literal that caller passes, into
    the SAME `seen` map as every direct label, so a caller's label collides with a direct
    one as it does at runtime. A caller passing no literal is unlabelled at the caller.
    ⚠️ THE FLOOR: a forwarder none of whose calls was read stays UNLABELLED at its seam
    call, and so does a call whose commas `forwarder_callers` reads two ways. A forwarder
    with no call in scope therefore shows up in the unlabelled list rather than vanishing;
    an out-of-scope call of a forwarder that has a call in scope does not -- see the
    header. `forwarders_out`, when given, receives one row per forwarder for
    `--list-unlabelled`."""
    views = {}
    for name, src in items:
        views[name] = View(src)
    seen, unlabelled = {}, []
    fws = {}                                 # (file, lp) -> record + "seam": [where...]
    queue = []

    def add_forwarder(name, fw, where):
        key = (name, fw["lp"])
        if key not in fws:
            fws[key] = dict(fw, file=name, seam=[], callers=0)
            queue.append(key)
        fws[key]["seam"].append(where)

    for name, v in views.items():
        for s in seam_calls(v):
            where = f"{name}:{s['line']}"
            if s["label"] is not None:
                seen.setdefault(s["label"], []).append(where)
                continue
            fw = forwarded_param(v, s["pos"], s["idents"])
            if fw is None:
                unlabelled.append(where)
            else:
                add_forwarder(name, fw, where)
    while queue:
        key = queue.pop()
        fw = fws[key]
        for cname, cline, label, nested, why in forwarder_callers(views, key[0], fw):
            fw["callers"] += 1
            where = f"{cname}:{cline}"
            if label is not None:
                seen.setdefault(label, []).append(f"{where} (via {fw['fn']})")
            elif nested is not None:
                add_forwarder(cname, nested, where)
            else:
                unlabelled.append(f"{where} (via {fw['fn']}{': ' + why if why else ''})")
    for fw in fws.values():
        if fw["callers"] == 0:
            unlabelled.extend(f"{w} (forwarder {fw['fn']}: no call of it was read)"
                              for w in fw["seam"])
    if forwarders_out is not None:
        forwarders_out.extend(sorted(
            (fw["file"], fw["fn"], fw["index"], fw["callers"], fw["seam"]) for fw in fws.values()))
    return seen, unlabelled, len(views)


def duplicates(seen):
    return {k: v for k, v in sorted(seen.items()) if len(v) > 1}


# The gate's own verdict, in both directions -- a checker that cannot be seen to FAIL
# is not a checker.
if not duplicates(tally([("a.cpp", _ONE_LINE), ("b.cpp", _ONE_LINE)])[0]):
    bad.append("control: a tree with two identical labels reported NO duplicate")
if duplicates(tally([("a.cpp", _ONE_LINE), ("b.cpp", _WRAPPED)])[0]):
    bad.append("control: a tree with two distinct labels reported a duplicate")
print(f"  {'ok   ' if not bad else 'WRONG'} verdict fires on a duplicate tree and is silent on a unique one")

# ── FORWARDER controls (#591), through the same `tally` the reading calls ──
# A fixture method forwarding its `const char* site` to a seam primitive. Nothing below
# names it to the gate: each control proves the derivation, not a list.
_FWD = '''
struct Fixture {
    template <class Pred>
    [[nodiscard]] bool pump_done(Pred pred, const char* site) {
        if (fixpp::test_support::pump_until(ioc, pred, site)) return true;
        ADD_FAILURE() << site;
        return false;
    }
};
'''
_FWD_RUN = '''
template <class T>
expected_t<T> run(asio::awaitable<expected_t<T>> a, const char* site) {
    auto fut = asio::co_spawn(exec, std::move(a), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 500ms, site)) return {};
    return fut.get();
}
bool run(int x, const char* site);
'''
_FWD_NESTED = _FWD + '''
bool outer(int n, const char* where) {
    return f.pump_done([&] { return n > 0; }, where);
}
'''


def _fwd_case(name, tree, want_labels, want_dups, want_unlabelled):
    """One control: the labels read, the duplicates and the unlabelled sites must be exactly
    these. `want_unlabelled` is a list of substrings, one per expected unlabelled entry."""
    seen, unl, _ = tally(tree)
    got_labels = {k: len(v) for k, v in seen.items()}
    got_dups = sorted(duplicates(seen))
    ok_unl = len(unl) == len(want_unlabelled) and all(
        w in u for w, u in zip(want_unlabelled, sorted(unl)))
    if got_labels != want_labels or got_dups != want_dups or not ok_unl:
        print(f"  WRONG forwarder: {name}")
        bad.append(f"forwarder control '{name}': labels {got_labels} (want {want_labels}), "
                   f"duplicates {got_dups} (want {want_dups}), unlabelled {unl} "
                   f"(want one entry containing each of {want_unlabelled})")
    else:
        print(f"  ok    forwarder: {name}")


_fwd_case("two calls of a forwarder passing one literal are a DUPLICATE",
          [("a.cpp", _FWD + '''
    if (!f.pump_done([&] { return a; }, "FW/one")) return;
    if (!f.pump_done([&] { return b; }, "FW/one")) return;
''')], {"FW/one": 2}, ["FW/one"], [])
_fwd_case("distinct literals through a forwarder are read, and it is not unlabelled",
          [("a.cpp", _FWD + '''
    if (!f.pump_done([&] { return a; }, "FW/one")) return;
    if (!f.pump_done([&] { return b; },
                     "FW/two")) return;
''')], {"FW/one": 1, "FW/two": 1}, [], [])
_fwd_case("a forwarder's label COLLIDES with a direct label in another file",
          [("a.cpp", _FWD + '''
    if (!f.pump_done([&] { return a; }, "A/open")) return;
'''), ("b.cpp", _ONE_LINE)], {"A/open": 2}, ["A/open"], [])
_fwd_case("a same-named call of another arity, and a declaration, are not calls of it",
          [("a.cpp", _FWD_RUN + '''
    ioc.run();
    (void)run(sess.open(), "R/one");
''')], {"R/one": 1}, [], [])
_fwd_case("a forwarder called with ITS caller's parameter is followed to the literal",
          [("a.cpp", _FWD_NESTED + '''
    (void)outer(1, "N/one");
    (void)outer(2, "N/two");
''')], {"N/one": 1, "N/two": 1}, [], [])
_fwd_case("FLOOR: a forwarder none of whose calls is read stays unlabelled",
          [("a.cpp", _FWD)], {}, [], ["no call of it was read"])
_fwd_case("a call passing a NAMED CONSTANT is unlabelled at the call",
          [("a.cpp", _FWD + '''
    if (!f.pump_done([&] { return a; }, kSite)) return;
''')], {}, [], ["a.cpp:11 (via pump_done)"])
_fwd_case("a header's forwarder is read in the files that include it, and only there",
          [("support/fx.hpp", _FWD),
           ("c.cpp", '#include "support/fx.hpp"\n    (void)f.pump_done([&] { return a; }, "H/one");\n'),
           ("d.cpp", '    (void)g.pump_done([&] { return a; }, "H/two");\n')],
          {"H/one": 1}, [], [])
# A comma inside a template argument list is not an argument separator. The call holding
# one must be read at the forwarder's arity, or its label silently leaves the map while the
# forwarder's other caller keeps it out of the unlabelled list (#591).
_fwd_case("a template-argument comma in a forwarder call does not hide its label",
          [("a.cpp", _FWD + '''
    (void)f.pump_done(make_pred<A, B>(), "DUP");
    (void)f.pump_done(pred, "UNIQUE");
    if (!run_window_then_ready(ioc, fut, 1s, "DUP")) { return; }
''')], {"DUP": 2, "UNIQUE": 1}, ["DUP"], [])
# A call that splits within the arity either way: the plain split puts a literal from INSIDE
# the first argument at the label's position. Unlabelled, never that literal.
_FWD_DEFAULTED = '''
bool fwd3(Pred pred, const char* site, int n = 0) {
    return fixpp::test_support::pump_until(ioc, pred, site);
}
'''
_fwd_case("a call split two ways within the arity is unlabelled, not misread",
          [("a.cpp", _FWD_DEFAULTED + '''
    (void)fwd3(m<A, B>("Z"), "DUP");
    (void)fwd3(std::vector<int>{}, "OK");
''')], {"OK": 1}, [], ["(via fwd3: arguments split two ways at a '<' or '>')"])
# The forwarder's own parameter list is split the same way: a template-argument comma there
# must not move the label's position or the forwarder's arity.
_fwd_case("a template-argument comma in a forwarder's parameter list",
          [("a.cpp", '''
bool fwdp(std::pair<int, int> p, const char* site) {
    return fixpp::test_support::pump_until(ioc, pred, site);
}
    (void)fwdp(p, "P/one");
    (void)fwdp(std::pair<int, int>{1, 2}, "P/one");
''')], {"P/one": 2}, ["P/one"], [])

if bad:
    sys.exit("\npump-label-uniqueness: CONTROL FAILED -- this gate is not proven at its "
             "own boundary, so a clean reading from it is not evidence:\n  " + "\n  ".join(bad))

# ── The reading ──────────────────────────────────────────────────────────────
def sources():
    for p in sorted((root / "tests").rglob("*")):
        if p.suffix not in (".cpp", ".hpp", ".cc", ".h"):
            continue
        if p.name == "pump_until_ready.hpp":       # the primitive's own declaration
            continue
        yield str(p.relative_to(root)), p.read_text(encoding="utf-8", errors="replace")


forwarders = []
seen, unlabelled, files = tally(sources(), forwarders)
dups = duplicates(seen)

# ⚠️ A GATE THAT SCANNED NOTHING MUST NOT REPORT CLEAN, and the population is SEAM SITES,
# not source files. With a wrong `--root`, or a tree whose sources are not under tests/,
# every loop above is empty and the verdict below is vacuously "unique" at exit 0 -- and the
# synthetic controls cannot see it, because they never touch the filesystem. The first
# version guarded on `files == 0`, which a tree full of C++ containing no seam call passes.
# Same rule `assert_nonempty_population` states in ci/pump-arm-common.sh; not restated here,
# and not callable from inside this heredoc.
labelled = sum(len(v) for v in seen.values())
if labelled == 0:
    sys.exit(f"pump-label-uniqueness: error: scanned {files} file(s) under {root}/tests and "
             f"found {labelled} labelled seam site(s) -- a gate whose population is empty "
             "cannot report clean")

if sys.argv[2] == "1":
    print("\nFORWARDERS (#591) -- a function passing its own `const char*` parameter to a seam")
    print("call; its labels are read at its calls, which are counted as the sites:")
    for fname, fn, index, ncallers, seam in forwarders:
        print(f"  {fname}: {fn}(parameter {index}) at {', '.join(seam)} -- {ncallers} call(s) read")
    print("\nUNLABELLED seam sites (no label literal reached) -- a seam call with no literal")
    print("that is not a forwarder, a forwarder call passing no literal (`via`), or a forwarder")
    print("none of whose calls was read; read each one:")
    for where in unlabelled:
        print(f"  {where}")

print(f"\nscanned {files} file(s) under tests/")
print(f"  LABELLED   seam sites : {labelled}  ({len(seen)} distinct label(s))")
print(f"  UNLABELLED seam sites : {len(unlabelled)}"
      "   <- reachable by no seam arm; a separate #289 axis, not a failure")
print("  ⚠️ the UNLABELLED figure is an UPPER BOUND: `pump_until(` is matched by spelling,")
print("     and tests/sync/test_fifo_across_cycles.cpp defines a LOCAL `pump_until` of its")
print("     own -- a bare poll_one loop that never reaches the seam. Its calls are counted")
print("     here. Nothing lexical distinguishes them; the LABELLED figure and the duplicate")
print("     verdict are unaffected, because that helper takes no label.")

if dups:
    print("\npump-label-uniqueness: error: duplicate site label(s):", file=sys.stderr)
    for lab, where in dups.items():
        print(f"  {lab}", file=sys.stderr)
        for w in where:
            print(f"      {w}", file=sys.stderr)
    print("\nA label is matched by strcmp at the forcing seam, so a duplicate makes "
          "FIXPP_FORCE_WINDOW_MISS fire at more than one site. Qualify each with its "
          "fixture or suite (e.g. `AllowPosDupStripTest::drive_to_active/open`).",
          file=sys.stderr)
    sys.exit(1)

print("\nevery site label is unique.")
PY
