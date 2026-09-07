#!/usr/bin/env bash
# ci/mock-clock-staging-sweep.sh -- find STAGING WINDOWS before a mock-clock advance.
#
# THE DEFECT, and it is unrecoverable rather than slow. A test that wants a
# mock-clock deadline to fire has to get the coroutine PARKED ON THAT DEADLINE
# first. The usual spelling is a fixed window:
#
#     auto close_fut = co_spawn(ioc, sess.close(graceful), use_future);
#     ioc.run_for(50ms);            // <- STAGING WINDOW, blind
#     ioc.restart();
#     clock->advance(seconds{3});   // <- the discriminator
#     if (!run_window_then_ready(ioc, close_fut, 200ms)) { ... }   // terminal half
#
# If the coroutine has NOT parked when that window returns, the advance lands on a
# timer that is not yet armed and is LOST. Nothing advances the clock again, so no
# later pump rescues it: the terminal half then reports a miss, or wedges, against
# correct code. It surfaced as `session_tc_liveness` failing 1-of-369 on
# `linux-clang-asan` (PR #381's lane), fixed at fixpp `4179da94`.
#
# ⚠️ THE MIGRATION IS AN OBSERVABLE CONDITION, NOT A LONGER WINDOW. A bigger
# `run_for` is what everyone reaches for first and it does not fix anything -- it
# lowers the probability and keeps the failure mode. `pump_until` on a state
# predicate (`sess.state() == LogoutSent` is exactly "Logout emitted, parked, not
# complete") turns the hope into an observation.
#
# ⚠️ THE #316 TRAP, WHICH IS WHY EVERY ROW CARRIES THE DISCRIMINATION AND NOT JUST A
# LOCATION. #316 touched one of these functions, guarded the terminal half, left the
# staging half blind, and wrote a comment justifying that as deliberate. Its PREMISE
# was right -- "close(graceful) must still be PENDING when this returns, so it is a
# staging window, not a completion window" -- and its CONCLUSION was wrong. A
# detector that emits only rows hands the next reader the same trap, so the row text
# states it: still-pending is a requirement ON the staging window, not a licence to
# leave it blind.
#
# ── WHAT THIS CAN AND CANNOT SEE ─────────────────────────────────────────────
#
# Only the SAME-FUNCTION pair. A fixture helper can stage while its caller advances,
# and then no analysis of this kind finds the pair at all. **The row count is a LOWER
# BOUND on the population, not the population** -- say it that way when quoting it,
# the way `ci/pump-get-sweep.sh`'s "3 remaining" should have said "3 that the sweep
# can see".
#
# The enclosing function is found by BRACE MATCHING over comment/literal-blanked
# source (`ci/cxx_blank.py`, the shared lexer -- not a fifth copy), so the SCOPE of the
# search carries no N-line constant and none of the census's blind spot.
# ⚠️ ONE CONSTANT SURVIVES AND IT IS A REAL BLIND SPOT: `MAX_HEAD = 8` bounds how far a
# WRAPPED `while`/`for`/function head may reach back. A head split over more than that
# many lines reads as a plain block, so a hand-rolled pump would be reported as a
# candidate (loud, wrong direction) and a function head would widen the search scope
# (quiet). An earlier revision of this paragraph claimed there was no such constant. What replaces
# it is the brace matcher, whose failure mode is a file it cannot balance. Such a site is
# reported as `UNPARSED` and listed beside the candidates -- NOT dropped, which is the
# only outcome that would be silent. It has never fired on this corpus, so read a zero
# there as untested rather than as proven.
#
# EXHAUSTION-BOUNDED pumps (`poll()`, `poll_one()`, bare `run()`) are NOT candidates
# and that is a claim about mechanism, not a convenience: they return when the
# context has no ready work, so a starved runner makes them SLOWER, never shorter.
# Only a WALL-CLOCK bound (`run_for`, `run_one_for`, `run_until`) can return with the
# staging work still queued. A `run()` before an advance has a different hazard
# (batch 17's) and a different instrument.
#
# Usage:  ci/mock-clock-staging-sweep.sh [--root DIR] [--quiet]
# Exit 0 when the CONTROLS pass; 2 when they do not. The ROW REPORT never gates -- a
# candidate is not a defect and this script cannot tell the difference. What gates is the
# control set, because a reporter whose rules have silently broken reports clean.
set -uo pipefail

fail() { echo "$*" >&2; exit 2; }

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
scan_root="$repo_root"
quiet=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --root)  [ "$#" -ge 2 ] || fail "--root requires an argument"; scan_root="$2"; shift 2 ;;
        --quiet) quiet=1; shift ;;
        -h|--help) awk 'NR==1 || /^#/ {print; next} {exit}' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

command -v python3 >/dev/null || fail "python3 is required"

FIXPP_CI_DIR="$repo_root/ci" python3 - "$scan_root" "$quiet" <<'PY'
import os, re, sys
from pathlib import Path

# ⚠️ RESOLVE THE MODULE FROM THE SCRIPT'S OWN ci/, NOT FROM --root, which may point at
# another worktree -- that would import that tree's blanker to judge this one. Same
# reasoning, verbatim, as `ci/pump-label-uniqueness.sh`.
sys.path.insert(0, os.environ["FIXPP_CI_DIR"])
from cxx_blank import blank_non_code

root, quiet = Path(sys.argv[1]), sys.argv[2] == "1"

# The discriminator: a mock-clock time step. `set_utc_skew` is deliberately absent --
# it moves wall time only and wakes nobody, so it cannot lose an advance.
#
# ⚠️ THE RECEIVER IS THE PART THAT FAILED TOWARD CLEAN. An earlier revision required a
# BARE IDENTIFIER before the `.`/`->`, so `get_clock().advance(3s)` or `clocks[i].advance()`
# after a blind window produced no row, no escalation and exit 0 -- the site vanished
# entirely rather than landing in a bucket. No such receiver exists in `tests/` today
# (`grep -rnE '[)\]]\s*(\.|->)\s*(advance|step_to)\s*\('`), which is a property of the
# tree and not of the instrument; the next file to add one would have been invisible.
# Found by the hostile round, synthetically. Controls straddle it below.
ADVANCE = re.compile(r"(?:\w|\)|\])\s*(?:\.|->)\s*(advance|step_to)\s*\(")
# WALL-CLOCK-bounded pumps: the only shape that can return with staging work queued.
FIXED = re.compile(r"\.(run_for|run_one_for|run_until)\s*\(")
# Exhaustion-bounded: returns on "no ready work", so starvation lengthens it.
EXHAUST = re.compile(r"\.(poll|poll_one|run)\s*\(")
# Condition-bounded: already an observation.
CONDITION = re.compile(r"pump_until\s*\(|pump_until_ready\s*\(|"
                       r"run_window_then_ready\s*\(|run_to_exhaustion_or_report\s*\(")
# ⚠️ A FIXED WINDOW INSIDE A `while`/`for` IS NOT A FIXED WINDOW -- it is a
# HAND-ROLLED `pump_until`, and calling it a candidate is a false positive. This
# rule was added because the sweep's first run reported
# `logout_exchange_test.cpp:888`, whose window sits inside
# `while (sess.state() != LogoutSent && now < deadline) ioc.run_for(20ms);` --
# already an observable staging condition, just not spelled with the primitive.
# It is reported as its own class rather than folded into CONDITION: it is correct
# in MECHANISM and still owes a normalisation (the primitive adds the site label
# the forcing seam needs, and the miss-branch drain).
LOOPHEAD = re.compile(r"^\s*(?:\}\s*)?(?:while|for)\s*\(")
# ⚠️ AND THE HEAD MAY BE WRAPPED. `clang-format` splits a long loop condition, so the
# line that OPENS the body is the tail of the condition, not the `while`. Matching only
# the opening line missed the one real instance in the tree while every synthetic
# control passed -- the same fails-toward-a-finding shape as reading a label after
# formatting. So walk up to `MAX_HEAD` lines and test the JOIN.
MAX_HEAD = 8


def is_head(lines, k, pattern):
    """True when line `k` is (or completes) a head matching `pattern`.

    ⚠️ THE JOIN MUST NOT SWALLOW A COMPLETED STATEMENT, and an earlier revision did.
    `for (int i = 0; i < 3; ++i) { f(i); }` on a line above a genuinely blind window
    joined with the window's own line and matched LOOPHEAD, so a blind site read
    LOOPED-WINDOW -- and LOOPED-WINDOW rows were not even printed, so it vanished. Found
    by the hostile review, synthetically.

    THE RULE, and it has to admit the brace-less body: take the text AFTER the head's
    closing paren. The head is still OPEN over line `k` iff that remainder contains no
    `}` and no `;` at paren-depth 0, allowing one trailing `;` -- because a brace-less
    `while (c) ioc.run_for(20ms);` has the window as its body and ends in exactly that.
    A completed `{ ... }` carries a `}`; a completed `drain(); <window>;` carries a `;`
    that is not the last character. Both are rejected."""
    for s in range(k, max(-1, k - MAX_HEAD), -1):
        joined = " ".join(x.strip() for x in lines[s:k + 1])
        m = pattern.match(joined)
        if not m:
            continue
        # Find the head's closing paren: the first `)` that returns depth to 0.
        depth, cut = 0, None
        for i, ch in enumerate(joined):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    cut = i + 1
                    break
        if cut is None:
            continue
        rest = joined[cut:].rstrip()
        if rest.endswith(";"):
            rest = rest[:-1]
        depth, open_head = 0, True
        for ch in rest:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif depth == 0 and ch in ";}":
                open_head = False
                break
        if open_head:
            return True
    return False


def is_loop_head(lines, k):
    """True when line `k` opens (or continues) a `while`/`for` head."""
    return is_head(lines, k, LOOPHEAD)


# What counts as the head of a FUNCTION body, for deciding how far out to walk. A
# `TEST`/`TEST_F` macro, or a declarator ending in `)` optionally followed by trailing
# specifiers. Deliberately loose: over-matching stops the walk early (narrower scope,
# more escalations), under-matching walks to the outermost block (wider scope, more
# verdicts) -- and the CONTROLS below pin both edges rather than the regex.
FUNCHEAD = re.compile(
    r"^\s*(?:TEST|TEST_F|TEST_P|TYPED_TEST\w*)\s*\(|"
    r"^\s*(?!(?:if|for|while|switch|catch|else|do)\b)[A-Za-z_~][\w:<>,\s\*&\[\]]*"
    r"\([^;]*\)\s*(?:const\s*|noexcept\s*|override\s*|final\s*|->[^{;]*)*$")


def enclosing_spans(blanked, lines, idx):
    """Every brace block containing line `idx`, innermost FIRST.

    ⚠️ ONE BLOCK IS THE WRONG SCOPE AND IT FAILS TOWARD CLEAN. An earlier revision
    used only the innermost, so
    `void f() { ioc.run_for(50ms); if (c) { clock->advance(3s); } }` found no pump in
    the `if` body and reported the verdict then spelled NO-PUMP-IN-FUNCTION (renamed to
    NO-PUMP-IN-SCOPE, because the word had to match the thing) -- a genuinely blind window demoted
    to the escalation bucket, under a verdict that says *function* while meaning
    *block*. Found by the hostile review, synthetically; no live instance existed."""
    offs, pos = [], 0
    for l in lines:
        offs.append(pos)
        pos += len(l) + 1
    target = offs[idx]
    stack, spans = [], []
    for i, ch in enumerate(blanked):
        if ch == "{":
            stack.append(i)
        elif ch == "}":
            if not stack:
                return None
            open_at = stack.pop()
            if open_at < target < i:
                spans.append((open_at, i))
    if not spans:
        return None
    spans.sort(key=lambda sp: -sp[0])  # innermost first
    return [(sum(1 for o in offs if o <= sp[0]) - 1, sp) for sp in spans]


def function_span(blanked, lines, idx):
    """The enclosing FUNCTION body -- the innermost enclosing block whose head reads
    as a function, else the outermost enclosing block."""
    chain = enclosing_spans(blanked, lines, idx)
    if chain is None:
        return None
    for lo, sp in chain:
        if is_head(lines, lo, FUNCHEAD):
            return lo, sp
    return chain[-1]


def enclosing_span(blanked, lines, idx):
    """Line range [lo, hi) of the innermost brace block containing line `idx`.

    Walks the blanked text so a `{` inside a comment or a string cannot open a
    block. Returns None both when the braces do not balance AND when the site has no
    enclosing block; the caller cannot tell those apart and reports `UNPARSED` for
    either, which is deliberate -- both mean "this site was not classified", and a site
    that vanished would be the silent outcome."""
    offs, pos = [], 0
    for l in lines:
        offs.append(pos)
        pos += len(l) + 1
    target = offs[idx]
    stack, span = [], None
    for i, ch in enumerate(blanked):
        if ch == "{":
            stack.append(i)
        elif ch == "}":
            if not stack:
                return None
            open_at = stack.pop()
            if open_at < target < i and (span is None or open_at > span[0]):
                span = (open_at, i)
    if span is None:
        return None
    # Only the OPENING line is needed -- callers use the span to read the block's head
    # and to compare two spans for identity, and the char offsets serve identity
    # directly. Converting the closing offset to a line number as well cost a second
    # O(lines) scan for a value nothing read.
    lo = sum(1 for o in offs if o <= span[0]) - 1
    return lo, span


def classify(path):
    src = path.read_text(errors="replace")
    if "advance(" not in src and "step_to(" not in src:
        return []
    blanked = blank_non_code(src)
    lines = blanked.split("\n")
    raw = src.split("\n")
    out = []
    for i, l in enumerate(lines):
        if not ADVANCE.search(l):
            continue
        span = function_span(blanked, lines, i)
        if span is None:
            out.append((i + 1, raw[i].strip()[:70], "UNPARSED"))
            continue
        lo, _ = span
        # The NEAREST preceding pump inside this block decides the row. Nearest,
        # not "any": a function may stage by condition early and then use a fixed
        # window for something unrelated, and it is the LAST thing to touch the
        # context before the advance that determines whether the sleep is armed.
        verdict = "NO-PUMP-IN-SCOPE"
        for j in range(i - 1, lo - 1, -1):
            if CONDITION.search(lines[j]):
                verdict = "CONDITION"
                break
            if FIXED.search(lines[j]):
                inner = enclosing_span(blanked, lines, j)
                # Two spellings, and missing either one turns a correct site into a
                # candidate: the window in a BRACED loop body, and the window sharing
                # the `while (...) {` line (or a brace-less `while (...) run_for(...);`).
                looped = is_loop_head(lines, j) or (
                    inner is not None and inner != span and is_loop_head(lines, inner[0]))
                verdict = "LOOPED-WINDOW" if looped else "FIXED-WINDOW"
                break
            if EXHAUST.search(lines[j]):
                verdict = "EXHAUSTION"
                break
        out.append((i + 1, raw[i].strip()[:70], verdict))
    return out


# ── CONTROLS, STRADDLING EVERY EDGE THIS DRAWS ───────────────────────────────
# ⚠️ A one-sided control is how batch 15 shipped a lookahead nobody could narrow:
# it pinned the FAR edge only, so every narrowing from 6 to 3 was invisible. Each
# pair below fixes a boundary from BOTH sides -- the last construct that must be
# caught, and the first that must not.
CASES = [
    ("fixed window then advance -> FIXED-WINDOW", """
void f() {
    auto fut = co_spawn(ioc, s.close(), use_future);
    ioc.run_for(50ms);
    ioc.restart();
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    ("condition then advance -> CONDITION", """
void f() {
    auto fut = co_spawn(ioc, s.close(), use_future);
    if (!pump_until(ioc, [&] { return s.state() == LogoutSent; })) { return; }
    clock->advance(seconds{3});
}
""", ["CONDITION"]),
    ("poll() is exhaustion-bounded, not a wall-clock hope", """
void f() {
    co_spawn(ioc, sleeper(), detached);
    ioc.poll();
    clk.advance(15ms);
}
""", ["EXHAUSTION"]),
    ("no pump at all in the function -> NO-PUMP-IN-SCOPE", """
void f() {
    clock->advance(seconds{1});
}
""", ["NO-PUMP-IN-SCOPE"]),
    # The two edges of the ENCLOSING-BLOCK rule, in one fixture.
    ("a fixed window in a SIBLING function does not reach", """
void staged() {
    ioc.run_for(50ms);
}
void other() {
    clock->advance(seconds{3});
}
""", ["NO-PUMP-IN-SCOPE"]),
    ("...but one in the SAME function does, across a nested block", """
void f() {
    ioc.run_for(50ms);
    {
        int x = 0;
        (void)x;
    }
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    # The blanking edge: the idiom quoted in prose must not become a row, and a
    # real one on the very next line must.
    ("the idiom QUOTED IN A COMMENT is not a window", """
void f() {
    // ioc.run_for(50ms);
    clock->advance(seconds{3});
}
""", ["NO-PUMP-IN-SCOPE"]),
    ("...and a comment does not hide the advance BELOW it", """
void f() {
    ioc.run_for(50ms);
    // clock->advance(seconds{1});
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    # ⚠️ THIS CONTROL COULD NOT FAIL until the `clock->advance()` below was added.
    # `classify()` early-returns on a file containing neither "advance(" nor "step_to(",
    # and the fixture had neither -- so widening `ADVANCE` to match `set_utc_skew` left
    # it GREEN. The fixture must reach the classifier for its expectation to mean
    # anything, and it must produce EXACTLY ONE row so an extra one is visible. Found by
    # the hostile review, by mutating the rule and watching nothing go red.
    ("set_utc_skew moves wall time only and is NOT an advance", """
void f() {
    if (!pump_until(ioc, [&] { return ready; })) { return; }
    clk.set_utc_skew(5ms);
    ioc.run_for(50ms);
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    ("NEAREST wins: a condition AFTER a fixed window is the verdict", """
void f() {
    ioc.run_for(50ms);
    if (!pump_until(ioc, [&] { return ready; })) { return; }
    clock->advance(seconds{3});
}
""", ["CONDITION"]),
    ("...and a fixed window AFTER a condition is too", """
void f() {
    if (!pump_until(ioc, [&] { return ready; })) { return; }
    ioc.run_for(50ms);
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    # The LOOPED-WINDOW edge, straddled: a hand-rolled pump_until is not a blind
    # window, and neither spelling of it may read as one -- but a window in a plain
    # nested block still must.
    ("a window in a braced while-body is HAND-ROLLED, not blind", """
void f() {
    while (s.state() != LogoutSent && now() < deadline) {
        ioc.run_for(20ms);
    }
    clock->advance(seconds{3});
}
""", ["LOOPED-WINDOW"]),
    ("LOOPHEAD spelling: a `for`-headed pump loop", """
void f() {
    for (auto t0 = now(); s.state() != LogoutSent && now() < t0 + 10s;) {
        ioc.run_for(20ms);
    }
    clock->advance(seconds{3});
}
""", ["LOOPED-WINDOW"]),
    ("...and so is a brace-less one on the while line", """
void f() {
    while (s.state() != LogoutSent) ioc.run_for(20ms);
    clock->advance(seconds{3});
}
""", ["LOOPED-WINDOW"]),
    ("...but a window in a plain nested block is still blind", """
void f() {
    {
        ioc.run_for(50ms);
    }
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    # ⚠️ THE CONTROL THAT WOULD HAVE CAUGHT THE INSTRUMENT'S OWN BUG. Every case
    # above passed while the sweep still misread `logout_exchange_test.cpp:888`,
    # because the real loop's condition WRAPS and the body-opening line is the tail
    # of that condition rather than the `while`. Synthetic fixtures written by the
    # same hand as the rule share the rule's blind spot -- this one is copied from
    # the shape that broke it.
    # ⚠️ ONE CONTROL PER SPELLING, because a rule that enumerates alternatives is
    # PARAMETERISED and "the controls pass" is a claim about whichever alternative they
    # happened to use. The hostile review deleted `run_one_for`, `run_until`, `poll_one`,
    # bare `run`, `run_window_then_ready`, `pump_until_ready` and
    # `run_to_exhaustion_or_report` from their regexes one at a time and NOT ONE control
    # went red: 8 of 11 spellings were untested. Each row below dies if its spelling is
    # dropped.
    ("FIXED spelling: run_one_for", """
void f() {
    ioc.run_one_for(50ms);
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    ("FIXED spelling: run_until", """
void f() {
    ioc.run_until(deadline);
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    ("EXHAUST spelling: poll_one", """
void f() {
    ioc.poll_one();
    clk.advance(15ms);
}
""", ["EXHAUSTION"]),
    ("EXHAUST spelling: bare run", """
void f() {
    ioc.run();
    clk.advance(15ms);
}
""", ["EXHAUSTION"]),
    ("CONDITION spelling: run_window_then_ready", """
void f() {
    if (!run_window_then_ready(ioc, fut, 200ms, "S")) { return; }
    clock->advance(seconds{3});
}
""", ["CONDITION"]),
    ("CONDITION spelling: pump_until_ready", """
void f() {
    if (!pump_until_ready(ioc, fut, 200ms, "S")) { return; }
    clock->advance(seconds{3});
}
""", ["CONDITION"]),
    ("CONDITION spelling: run_to_exhaustion_or_report", """
void f() {
    if (!run_to_exhaustion_or_report(ioc, fut, "S")) { return; }
    clock->advance(seconds{3});
}
""", ["CONDITION"]),
    # The RECEIVER edge, straddled. A call- or index-expression receiver must still be
    # found; `std::advance` (a free function, no receiver) must still not be.
    ("a call-expression receiver is still an advance", """
void f() {
    ioc.run_for(50ms);
    get_clock().advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    ("...and an indexed receiver is too", """
void f() {
    ioc.run_for(50ms);
    clocks[i]->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    ("...but the free function std::advance is NOT", """
void f() {
    ioc.run_for(50ms);
    std::advance(it, 3);
}
""", []),
    ("ADVANCE spelling: step_to", """
void f() {
    ioc.run_for(50ms);
    clk.step_to(s0 + 40ms);
}
""", ["FIXED-WINDOW"]),
    # The SCOPE edge, straddled. A window in the enclosing FUNCTION must reach an
    # advance nested inside an `if`; a window in a SIBLING function must not.
    ("a window reaches an advance nested in an if-block", """
void f() {
    ioc.run_for(50ms);
    if (cond) {
        clock->advance(seconds{3});
    }
}
""", ["FIXED-WINDOW"]),
    ("...and a COMPLETED loop above a window is not a loop head", """
void f() {
    for (int i = 0; i < 3; ++i) { td.deliver(i); }
    ioc.run_for(50ms);
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
    ("a WRAPPED while head still reads as a loop", """
void f() {
    while (s.state() != LogoutSent &&
           std::chrono::steady_clock::now() < arm_deadline) {
        ioc.run_for(20ms);
    }
    clock->advance(seconds{3});
}
""", ["LOOPED-WINDOW"]),
    ("...and a wrapped IF head does NOT", """
void f() {
    if (s.state() != LogoutSent &&
        std::chrono::steady_clock::now() < arm_deadline) {
        ioc.run_for(20ms);
    }
    clock->advance(seconds{3});
}
""", ["FIXED-WINDOW"]),
]

import tempfile
bad = 0
for name, body, want in CASES:
    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as fh:
        fh.write(body)
        p = Path(fh.name)
    got = [v for _, _, v in classify(p)]
    p.unlink()
    if got == want:
        if not quiet:
            print(f"  ok    {name}")
    else:
        print(f"  !!BAD {name}: expected {want}, got {got}")
        bad += 1
if bad:
    print(f"\nCONTROL FAILED ({bad}) -- the sweep below cannot be trusted. Fix the sweep.")
    sys.exit(2)

files = sorted((root / "tests").rglob("*.cpp")) + sorted((root / "tests").rglob("*.hpp"))
rows, escalate, tally = [], [], {}
for p in files:
    try:
        r = classify(p)
    except OSError:
        continue
    for ln, txt, v in r:
        tally[v] = tally.get(v, 0) + 1
        if v in ("FIXED-WINDOW", "LOOPED-WINDOW", "UNPARSED"):
            rows.append((p.relative_to(root), ln, txt, v))
        elif v == "NO-PUMP-IN-SCOPE":
            escalate.append((p.relative_to(root), ln, txt))

print("\n=== ROWS: a wall-clock staging window before a mock-clock advance ===")
print("  FIXED-WINDOW  = a candidate. LOOPED-WINDOW = a HAND-ROLLED pump, correct in")
print("  mechanism and owing only a normalisation. UNPARSED = not classified at all.")
print("  ⚠️ LOOPED-WINDOW IS LISTED, NOT SUPPRESSED: an earlier revision put it in")
print("  neither bucket, so a site the loop rule MISCLASSIFIED vanished with no row.")
for rel, ln, txt, v in rows:
    print(f"  {v:<13} {rel}:{ln}")
    print(f"                {txt}")
print("\n=== ESCALATION: no pump in the enclosing function ===")
print("  These are NOT candidates and NOT dismissals. The staging may be in a CALLER, which")
print("  no same-function analysis can see, or the sleeper may be parked by construction.")
print("  ⚠️ THEY ARE LISTED RATHER THAN COUNTED ON PURPOSE: an escalation nobody can")
print("  enumerate reads as a clean bill, and this bucket is the larger one.")
for rel, ln, txt in escalate:
    print(f"  {rel}:{ln}")
    print(f"      {txt}")

print("\n=== TALLY (nearest preceding pump, same function) ===")
for k in sorted(tally, key=lambda x: -tally[x]):
    print(f"  {tally[k]:>4}  {k}")
print(f"\nscanned {len(files)} file(s) under tests/")
print("\n⚠️ THE CANDIDATE COUNT IS A LOWER BOUND, NOT A POPULATION. Only same-function")
print("   pairs are visible: a fixture helper that stages while its CALLER advances is")
print("   invisible to this and to any analysis of its kind. Quote it as \"N that this")
print("   sweep can see\".")
print("⚠️ AND A CANDIDATE IS NOT YET A DEFECT. It is one where the advance must fire a")
print("   sleep the window was supposed to arm. An advance whose sleeper is already")
print("   parked by construction is correct with a fixed window above it -- read the")
print("   site, and say which reading you took.")
PY
