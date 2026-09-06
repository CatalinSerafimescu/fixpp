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
# ⚠️ THE DUPLICATE VERDICT SURVIVES THAT, and the reason is structural rather than
# lucky: a wrapper can only introduce a label if it TAKES one, and one that takes one
# is itself a labelled call this gate reads. What the wrapper does distort is the
# UNLABELLED figure, which is a count of lexical call sites and not of reaching paths.
#
# ⚠️ A SECOND SHAPE THE GATE CANNOT SEE: a label passed as a NON-LITERAL
# (`static constexpr const char* kSite = "..."; run_window_then_ready(ioc, fut, w, kSite)`)
# reaches the seam but has no string literal in the call's extent, so it is recorded as
# UNLABELLED rather than rejected. No migrated site uses that shape today. Deliberately
# NOT "fixed" by chasing the constant: resolving an identifier needs a symbol table, and
# a detector that resolves the spellings its author thought of is the failure mode
# ci/pump-get-sweep.sh's header already warns against. Disclosed instead.
#
# An UNLABELLED call (`run_window_then_ready(ioc, fut, 100ms)`) is legal -- it is a
# site the forcing seam cannot reach. Those are counted and reported, never failed:
# adopting the seam at them is a separate axis of #289.
#
# Usage:  bash ci/pump-label-uniqueness.sh [--root DIR]
# Exit 1 on a duplicate label, or on a failed self-test control.

set -euo pipefail

fail() { echo "pump-label-uniqueness: error: $*" >&2; exit 1; }

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_root="$repo_root"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --root) [ "$#" -ge 2 ] || fail "--root requires an argument"; scan_root="$2"; shift 2 ;;
        -h|--help) awk 'NR==1 || /^#/ {print; next} {exit}' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

command -v python3 >/dev/null || fail "python3 is required"

FIXPP_CI_DIR="$repo_root/ci" python3 - "$scan_root" <<'PY'
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
         "pump_until_ready(", "pump_until(")


def _join_adjacent(lits, text):
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
    """
    if not lits:
        return None
    out = [lits[-1][2]]
    for k in range(len(lits) - 1, 0, -1):
        gap = text[lits[k - 1][1]:lits[k][0]]
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
    blanked = blank_non_code(text)
    filled = _STR.sub(lambda m: "Z" * len(m.group(0)), text)
    comment_blanked = blank_non_code(filled)
    is_comment = [comment_blanked[k] != filled[k] for k in range(len(text))]
    out = []
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
            lits = [(lm.start(), lm.end(), lm.group(0)[1:-1])
                    for lm in _STR.finditer(text, i, j)
                    if not is_comment[lm.start()]]
            line = blanked.count("\n", 0, m.start()) + 1
            out.append((line, _join_adjacent(lits, text)))
    return sorted(out)


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
    ("a SPLIT literal is ONE label", _SPLIT_LITERAL,
     ["Suite::LongCaseNameThatCrossesTheColumnLimit/close_fut_a"]),
    ("a comma-separated literal is a different argument", _COMMA_SEPARATED_ARGS, ["K/settle"]),
    ("a call quoted in a STRING is not a call", _LABEL_IN_A_STRING, ["H/open"]),
    # ⚠️ INSIDE the argument list, not on the line before it -- the earlier control put it
    # before the call, outside the extent, and therefore never exercised the harvest.
    ("a literal COMMENTED OUT inside the arg list", _COMMENT_INSIDE_THE_ARGUMENT_LIST, ["I/open"]),
]
bad = []
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
def tally(items):
    """-> (seen, unlabelled, files) over an iterable of (display_name, source).

    `seen` maps label -> [where...]; a label with more than one entry is a duplicate."""
    seen, unlabelled, files = {}, 0, 0
    for name, src in items:
        files += 1
        for ln, lab in calls(src):
            if lab is None:
                unlabelled += 1
            else:
                seen.setdefault(lab, []).append(f"{name}:{ln}")
    return seen, unlabelled, files


def duplicates(seen):
    return {k: v for k, v in sorted(seen.items()) if len(v) > 1}


# The gate's own verdict, in both directions -- a checker that cannot be seen to FAIL
# is not a checker.
if not duplicates(tally([("a.cpp", _ONE_LINE), ("b.cpp", _ONE_LINE)])[0]):
    bad.append("control: a tree with two identical labels reported NO duplicate")
if duplicates(tally([("a.cpp", _ONE_LINE), ("b.cpp", _WRAPPED)])[0]):
    bad.append("control: a tree with two distinct labels reported a duplicate")
print(f"  {'ok   ' if not bad else 'WRONG'} verdict fires on a duplicate tree and is silent on a unique one")

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


seen, unlabelled, files = tally(sources())
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

print(f"\nscanned {files} file(s) under tests/")
print(f"  LABELLED   seam sites : {labelled}  ({len(seen)} distinct label(s))")
print(f"  UNLABELLED seam sites : {unlabelled}"
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
