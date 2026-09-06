#!/usr/bin/env bash
# Find unguarded `.get()` calls on co_spawn futures -- INCLUDING the ones the
# pump census structurally cannot see.
#
# WHY THIS EXISTS, AND WHY IT IS NOT THE CENSUS
# ─────────────────────────────────────────────────────────────────────────────
# `ci/pump-census.sh` anchors on the lexical pair `ioc.run_for(...)` ... `.get()`
# within a six-line window. Its header registers three blind spots; the third is
# that the window may not be lexically present AT ALL, because the pump is
# indirected through a helper:
#
#     auto fut = asio::co_spawn(f.ioc, sess.send(payload), asio::use_future);
#     f.drain();                  // <- the window, behind a member call
#     auto result = fut.get();    // <- unconditional
#
# No lookahead width reaches that: there is nothing to anchor on. This sweep
# starts from the thing that actually blocks -- the `get()` -- and asks whether a
# guard precedes it, so it needs no list of helper names to keep current.
#
# ⚠️ IT IS SHAPE-AGNOSTIC ABOUT THE PUMP. IT IS NOT A C++ PARSER.
# Do not read a clean file as proof. Known limitations, each with a control or a
# named population, because an undisclosed limitation is how this class recurs:
#
#   - Futures held in CONTAINERS are invisible. It recognises `auto NAME =
#     asio::co_spawn(...)`; `futs.push_back(asio::co_spawn(...))` consumed by
#     `for (auto& f : futs) f.get()` registers nothing. Real population exists in
#     tests/sync and the perf harnesses; re-derive with
#     `git grep -n 'push_back(asio::co_spawn\|emplace_back(asio::co_spawn'`.
#   - No aliasing, no `decltype(auto)`, no futures returned from a function.
#   - State resets at each function/TEST boundary, not at each C++ scope, so two
#     sibling blocks in one function share a future's guarded state.
#
# ⚠️ AND ONE FALSE-*POSITIVE* CLASS, WHICH IS THE OPPOSITE DIRECTION FROM EVERY
# LIMITATION ABOVE AND FROM THIS REPO'S USUAL FAILURE, WHICH IS WHY IT SAT
# UNDISCOVERED UNTIL #289 BATCH 16 TRIAGED THE CANDIDATE SET.
# A guard counts only if the guarding statement NAMES the future (false-clean mode 2
# above -- an unrelated nearby guard must not be credited). A readiness test
# indirected through a NAMED PREDICATE therefore does not count, even though it is
# the #289 contract hand-rolled:
#
#     auto all_ready = [&](std::future<void>& f) {
#         return f.wait_for(0ms) == std::future_status::ready;   // names `f`, not `fh`
#     };
#     while (clock::now() < deadline && !(all_ready(fh) && all_ready(f1))) ioc.poll_one();
#     ASSERT_TRUE(all_ready(fh) && all_ready(f1));   // fatal: returns from the TEST
#     fh.get();                                      // <- REPORTED, but unreachable unless ready
#
# ⚠️ DO NOT "FIX" THIS BY CREDITING ANY NEARBY READINESS TOKEN -- that reintroduces
# false-clean mode 2, which is the worse direction. Over-reporting costs a reader's
# time; under-reporting costs a wedged lane. The disclosure IS the remedy.
#
# Three things have to hold before such a site is dismissed, and they are separate
# claims -- the CONDITION, so it cannot rot; no count is written here because a
# count would:
#   (i)   a readiness test really does dominate the `get()`;
#   (ii)  its failure is FATAL IN THE RIGHT SCOPE -- `ASSERT_*` returns from the
#         function it appears in, so one inside a lambda or a helper does NOT
#         protect a `get()` in the caller, and execution falls through;
#   (iii) the miss path still leaves coroutine frames alive on a context that is
#         about to be destroyed -- i.e. whether it needs a DRAIN is a separate
#         question this sweep does not ask and cannot answer.
# Re-derive the population to inspect (candidate files that already use the idiom):
#   git grep -ln 'future_status::ready' -- tests/
#
# ⚠️ DO NOT "IMPROVE" THIS BY TEACHING IT TO RECOGNISE PUMPS. A detector that
# recognises helper SHAPES can only find the shapes its author thought of, and the
# cost of a miss here is a wedged lane rather than a failed assertion (#337 is the
# reference instance). Anchor on the `get()`, which every hazard must reach.
#
# ⚠️ ITS OUTPUT IS A CANDIDATE LIST, NOT A DEFECT LIST, AND MUST NOT BE PINNED.
# A `get()` is only a hazard when THIS thread is the one that must pump the
# context. Where the executor drives itself -- an `asio::thread_pool`, an
# `io_context` with worker threads already inside `run()`, the C ABI's internal
# context -- a bare `get()` is correct and `wait_until.hpp` is the right tool.
# tests/capi, tests/fuzz and the perf harnesses are full of exactly that. So this
# reports, it does not gate: there is no expected-set file and no exit-1 on a
# non-empty result. Judge each row.
#
# Usage:
#   bash ci/pump-get-sweep.sh [--root DIR] [--dir SUBDIR] [--quiet]
#   bash ci/pump-get-sweep.sh --disposition      # the same rows, TRIAGED (see below)
#
# Exits non-zero ONLY if a self-test control fails -- i.e. if the instrument
# cannot be shown to report both classes. A number it cannot stand behind is
# worse than no number.

set -euo pipefail

fail() {
    echo "pump-get-sweep: error: $*" >&2
    exit 1
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_root="$repo_root"
sub="tests"
quiet=0
disposition=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --root)  [ "$#" -ge 2 ] || fail "--root requires an argument";  scan_root="$2"; shift 2 ;;
        --dir)   [ "$#" -ge 2 ] || fail "--dir requires an argument";   sub="$2";       shift 2 ;;
        --quiet) quiet=1; shift ;;
        --disposition) disposition=1; shift ;;
        # Print the header by its STRUCTURE, not by a line count. `sed -n '1,55p'`
        # was here and batch 16's insertion silently truncated the help mid-sentence
        # -- a line number is a RESULT and results rot; a rule does not.
        -h|--help) awk 'NR==1 || /^#/ {print; next} {exit}' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

command -v python3 >/dev/null || fail "python3 is required"

python3 - "$scan_root" "$sub" "$quiet" "$disposition" <<'PY'
import re, sys
from pathlib import Path

root, sub, quiet = Path(sys.argv[1]), sys.argv[2], sys.argv[3] == "1"
disposition = sys.argv[4] == "1"

# ⚠️ `run_to_exhaustion_or_report` is NOT reached by `run_window_then_ready` -- they
# share the tail `then_ready` and nothing else, so a new spelling needs its own
# alternative here and its own control below. Widening this without the control is how a
# migration reads as unguarded and gets "migrated" a second time.
GUARD = re.compile(r"run_window_then_ready|run_to_exhaustion_or_report|"
                   r"pump_until_ready|pump_until\(|"
                   r"wait_for\([^)]*\)\s*[=!]=\s*std::future_status|"
                   r"std::future_status::ready")
# A new function/TEST body resets what we know. Without this, a guarded `fut` in
# one test marks a DIFFERENT test's `fut` guarded -- an affirmative false clean.
BOUNDARY = re.compile(r'^(?:TEST|TEST_F|TEST_P|TYPED_TEST\w*)\s*\(|'
                      r'^[A-Za-z_][\w:<>,\s\*&]*\s+[A-Za-z_]\w*\s*\([^;]*\)\s*\{?\s*$')
MAX_SPLICE = 12          # a statement longer than this is a splice failure, not a statement

_BLOCK = re.compile(r"/\*.*?\*/", re.S)
_LINE = re.compile(r"//[^\n]*")
_STR = re.compile(r'"(?:[^"\\\n]|\\.)*"')

def blank_comments(text):
    """Blank comment CONTENT but keep newlines, so reported line numbers stay true.

    The #289 migration comments QUOTE the very idiom this sweep looks for
    (`run_for(W); restart(); fut.get()`), so without this every migrated file
    reports its own header block as an unguarded site."""
    text = _BLOCK.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)
    return _LINE.sub(lambda m: " " * len(m.group(0)), text)

def depth_text(line):
    """Parens for splicing must ignore those inside string literals -- an
    unbalanced `(` in a literal (e.g. EXPECT_FATAL_FAILURE's message) otherwise
    swallows an entire test body into one 'statement'."""
    return _STR.sub('""', line)

def statements(lines):
    """Yield (start_line_index, spliced_text).

    A declaration may span physical lines -- `auto\\n    fut = asio::co_spawn(...)`
    is one statement -- so anchoring on a single line makes such a future INVISIBLE.
    ⚠️ Splicing must FAIL SAFE: if the terminator is not found within MAX_SPLICE
    lines the depth tracking has gone wrong, so emit the buffered lines singly
    rather than swallowing the region.

    ⚠️ AND EMITTING SINGLY IS **ALSO** A SILENT FALSE CLEAN, ONE LEVEL UP. An earlier
    revision of this docstring said a swallowed region is the false clean, implying the
    single-line fallback is safe. It is not: the lone `auto fut = asio::co_spawn(` no
    longer matches the `auto NAME = asio::co_spawn(...,` pattern, so `fut` never enters
    `known`, and every later `.get()` on it is skipped by the `name not in known` guard.
    The DECLARATION is dropped instead of the region, and the site vanishes with no row
    and no diagnostic.

    ⚠️ THIS IS A LIVE, MEASURED BLIND SPOT, NOT A HYPOTHETICAL. #289 batch 17 shipped a
    residual reading of "3 remaining" that was really "3 THAT THIS SWEEP CAN SEE": five
    live `ioc.run(); fut.get();` sites in caller-only files were invisible because a long
    lambda body pushed the declaration past MAX_SPLICE. The discriminating experiment, and
    the recipe to repeat it, is to collapse the lambda bodies so each declaration fits and
    re-run -- the same sites then report. Re-derive the population:

        git grep -n 'asio::co_spawn(' -- tests/ | ...   # then measure each declaration's span

    ⚠️ RAISING MAX_SPLICE IS NOT OBVIOUSLY THE FIX and must not be done casually: it widens
    the UNIVERSE every #289 count is computed over, so every historical figure in the
    handover and the decision records would stop being comparable. Whoever changes it owes
    a before/after on the whole corpus, not just on the site that motivated it."""
    buf, start, depth = [], None, 0
    for i, l in enumerate(lines):
        if start is None:
            if not l.strip():
                continue
            start = i
        buf.append(l)
        d = depth_text(l)
        depth += d.count("(") - d.count(")")
        done = depth <= 0 and l.rstrip().endswith((";", "{", "}"))
        if done:
            yield start, " ".join(x.strip() for x in buf)
            buf, start, depth = [], None, 0
        elif len(buf) >= MAX_SPLICE:
            for k, b in enumerate(buf):
                yield start + k, b.strip()
            buf, start, depth = [], None, 0
    if buf:
        for k, b in enumerate(buf):
            yield start + k, b.strip()

# ── the disposition axes (--disposition), added by #289 batch 16 ─────────────
# The sweep's rows are CANDIDATES; the criterion that turns one into a defect is
# already stated in this header ("only where THIS thread is the one that must
# pump"). Nobody had applied it, so batch 15 handed over a raw 352 as if it were
# the residual. These two axes apply it SYNTACTICALLY, from the same scan -- one
# definition of "candidate", no second walk to keep in step.
#
#   pump-shape:  RUN-UNBOUNDED  a `.run(` on a name DECLARED here as an
#                               asio::io_context / asio::thread_pool. No window.
#                RUN-BOUNDED    an inline `.run_for(`/`.run_until(`/`.poll(`/`.poll_one(`.
#                HELPER         anything else -- a call whose body holds the pump.
#                               This is the census's blind spot (c).
#   executor:    POOL           declared `asio::thread_pool`.
#                THREADED       a std::thread/jthread/std::async here names `<base>.run(`.
#                THREAD-IN-FILE the file starts threads but this executor is neither
#                               of the above. ESCALATION, NOT A VERDICT -- read the site.
#                CALLER-ONLY    no thread construct anywhere in the file, so nothing
#                               but the calling thread can ever pump.
#
# ⚠️ TWO EDGES, EACH FOUND BY HAND AFTER A WRONG READING, each pinned by a control
# that straddles it:
#   1. `f.run(300)` is a FIXTURE METHOD, not `ioc.run()`. Matching `\.run\(` on any
#      receiver moved 13 of one file's 17 sites into a class labelled "no window" --
#      in a file whose own header says "No ioc.run() calls". The receiver must
#      resolve to a context DECLARED in the file.
#   2. `ioc.run(ec)` is still unbounded, so the match must NOT require empty parens.
#      That spelling is absent from tests/ today, which is exactly why it needs a
#      control rather than a survey -- a survey re-arms itself for the next caller.
# ⚠️ NEITHER AXIS LOOKS AT DRAINS. Whether a miss branch needs one is a per-site
# question about what can be parked there, answered at migration time.
#
# ⚠️ THE EXECUTOR CLASSES ARE FILE-SCOPED WHILE THE GUARD STATE IS BOUNDARY-SCOPED, AND
# THAT ASYMMETRY CAN DISMISS A REAL DEFECT. `ctxnames`/`pools`/`threaded`/`anythread` are
# computed once over the whole file and never reset, because the declarations they read
# (a fixture's `asio::io_context ioc;` member) legitimately live outside any TEST. The
# guard state IS reset at each BOUNDARY, for the reason false-clean mode 3 below records.
# So if one TEST declares `asio::thread_pool pool` (or drives its own `ioc` from a
# `std::thread`) and a LATER test reuses that NAME for a caller-driven context, the later
# site reads POOL or THREADED and is silently dismissed. Reproduced synthetically; no live
# instance found in tests/ -- which is a statement about today's tree, not a property.
# ⚠️ DO NOT READ "THREAD-IN-FILE is escalation, so the direction is safe" AS COVERING THIS.
# An earlier draft of this header, the batch-16 record and the handover all said the error
# direction was one-way. It is not: THREAD-IN-FILE is the safe *fallback*, but POOL and
# THREADED are positive dismissals and a reused name reaches them.
# The fix is NOT to reset the declarations per boundary -- that would lose every fixture
# member. It is to read the site when a dismissal matters.
_CTXDECL = re.compile(r"asio::(?:io_context|thread_pool)\s*&?\s*(\w+)")
_POOLDECL = re.compile(r"asio::thread_pool\s+(\w+)")
_THREADTOK = re.compile(r"std::jthread|std::thread|std::async|asio::thread_pool")
_THREADRUN = re.compile(r"(?:std::jthread|std::thread|std::async)[^;]{0,400}?(\w+)\s*\.run\(", re.S)
# ⚠️ ANY arguments, not empty parens -- edge 2. `run_for(`/`run_until(`/`run_one(`
# cannot match this: none of them contains the literal `.run(`.
_UNBOUNDED = re.compile(r"([\w>.\-]+)\.run\(")
_BOUNDED = re.compile(r"\.run_for\(|\.run_until\(|\.poll\(|\.poll_one\(")


def classify(text):
    """-> (guarded, unguarded_rows). Anchored on the get(), and IDENTITY-CHECKED.

    False-CLEAN modes this must not have, each reproduced before being closed:
      1. a future whose declaration is split across lines went unseen -> statements()
      2. an UNRELATED guard nearby was accepted as this future's guard -> a guard
         only counts if it NAMES the future it guards
      3. state leaking across functions marked a later test's future guarded
         -> BOUNDARY resets
      4. a `continue` after a declaration skipped that statement's own `.get()`
         -> the declaration branch now falls through to the get() scan"""
    blanked = blank_comments(text)
    lines = blanked.splitlines()
    ctxnames = set(_CTXDECL.findall(blanked))
    pools = set(_POOLDECL.findall(blanked))
    threaded = set(_THREADRUN.findall(blanked))
    anythread = bool(_THREADTOK.search(blanked))

    def unbounded(txt):
        return any(m.group(1).replace("->", ".").split(".")[-1] in ctxnames
                   for m in _UNBOUNDED.finditer(txt))

    guarded_state, known, execs, since = {}, set(), {}, {}
    guarded, bad = 0, []
    for start, stmt in statements(lines):
        if BOUNDARY.match(stmt):
            guarded_state, known, execs, since = {}, set(), {}, {}
        m = re.search(r'\bauto\s+(\w+)\s*=\s*asio::co_spawn\s*\(\s*([^,]+?)\s*,', stmt)
        if m and "use_future" in stmt:
            known.add(m.group(1))
            # ⚠️ ONLY AN `auto NAME = co_spawn(...)` RE-BINDING RESETS THE GUARD. A bare
            # `fut = asio::co_spawn(...)` onto an already-declared name does not match the
            # pattern above, so a guard set for the FIRST binding still covers the second
            # future's `get()` -- a false clean. Pre-existing; recorded rather than fixed
            # here, because widening the pattern is a change to a pinned instrument and
            # `ci/test-pump-census.sh` is the harness that would have to grow with it.
            guarded_state[m.group(1)] = False
            execs[m.group(1)] = m.group(2)
            since[m.group(1)] = []
            # NO `continue` here: the same statement may also consume the future.
        if GUARD.search(stmt):
            for name in known:
                if re.search(rf'\b{re.escape(name)}\b', stmt):
                    guarded_state[name] = True
        for name in list(since):
            if not re.search(rf'\b{re.escape(name)}\s*\.get\(\)', stmt):
                since[name].append(stmt)
        for gm in re.finditer(r'(?:^|[^\w.])(\w+)\.get\(\)', stmt):
            name = gm.group(1)
            if name not in known:
                continue
            if guarded_state.get(name):
                guarded += 1
            else:
                ex = execs.get(name, "?")
                base = re.sub(r'\.get_executor\(\)$', '', ex).lstrip('*&') \
                         .replace("->", ".").split('.')[-1]
                ec = ("POOL" if base in pools else
                      "THREADED" if base in threaded else
                      "THREAD-IN-FILE" if anythread else "CALLER-ONLY")
                seg = " ".join(since.get(name, []))
                pc = ("RUN-UNBOUNDED" if unbounded(seg) else
                      "RUN-BOUNDED" if _BOUNDED.search(seg) else "HELPER")
                bad.append((start + 1, lines[start].strip() or stmt[:70], ec, pc))
    return guarded, bad

# ── SELF-TEST on SYNTHETIC fixtures ──────────────────────────────────────────
# Synthetic, not real files: a control anchored to a real file asserts a
# contingent fact about today's tree, and a later reader cannot tell a rotted
# anchor from a broken instrument.
DIRECT_BAD = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    auto r = fut.get();
"""
INDIRECT_BAD = """
    auto fut = asio::co_spawn(f.ioc, sess.send(p), asio::use_future);
    f.drain();
    auto r = fut.get();
"""
DOTLESS_BAD = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    run();
    auto r = fut.get();
"""
GUARDED_OK = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "X");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "X";
        return;
    }
    auto r = fut.get();
"""
# The batch-17 spelling. Its own control because the regex alternative is its own: a
# `run_window_then_ready` control cannot prove this one is credited.
GUARDED_RUN_OK = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, fut, "X")) {
        fixpp::test_support::drain_or_report(ioc, "X");
        ADD_FAILURE() << fixpp::test_support::kRunMiss << "X";
        return;
    }
    auto r = fut.get();
"""
# ... and the shape it replaces, so the pair straddles: an unguarded `ioc.run()` + get()
# must still READ unguarded. Without this the widening above could credit any nearby
# `run(`-ish token and the control above would still pass.
RUN_UNBOUNDED_BAD = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run();
    auto r = fut.get();
"""
ASSERT_OK = """
    auto fut = asio::co_spawn(ioc, fsm.drive(), asio::use_future);
    ioc.run_for(500ms);
    ioc.restart();
    ASSERT_EQ(fut.wait_for(0s), std::future_status::ready);
    (void)fut.get();
"""
NOT_A_FUTURE = """
    auto ptr = make_thing();
    auto r = ptr.get();
"""
COMMENT_LOOKALIKE = """
    // The `run_for(W); restart(); fut.get()` sites in this file are migrated.
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) { return; }
    auto r = fut.get();
"""
# Codex, batch 9: both of these read CLEAN under the first version of this script.
SPLIT_DECL_BAD = """
    auto
        hidden_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    f.drain();
    auto r = hidden_fut.get();
"""
FOREIGN_GUARD_BAD = """
    auto earlier_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, earlier_fut, 200ms)) { return; }
    (void)earlier_fut.get();
    auto target_fut = asio::co_spawn(ioc, sess.send(p), asio::use_future);
    f.drain();
    auto r = target_fut.get();
"""
# Opus, batch 9: these read CLEAN under the FIX for the two above.
CROSS_FUNCTION_BAD = """
TEST_F(Fixture, First) {
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(f.ioc, fut, 200ms)) { return; }
    (void)fut.get();
}
TEST_F(Fixture, Second) {
    auto fut = asio::co_spawn(f.ioc, sess.send(p), asio::use_future);
    f.drain();
    auto r = fut.get();
}
"""
# An unbalanced '(' inside a STRING must not swallow the body that follows it.
STRING_PAREN_BAD = """
    EXPECT_FATAL_FAILURE(helper(), "unbalanced ( inside a literal");
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    f.drain();
    auto r = fut.get();
"""
# ⚠️ THE SPLICE BOUNDARY, STRADDLED. `SPLIT_DECL_BAD` above splits over TWO lines, which
# proves the splice works comfortably INSIDE the limit and says nothing about where it
# gives up. These two are one line either side of MAX_SPLICE: the first is still spliced
# and REPORTS, the second exceeds it and is DROPPED -- the live blind spot, pinned as a
# known limitation rather than left to be rediscovered. If MAX_SPLICE moves, the second
# case starts reporting and this control goes RED, which is the point.
_FILLER = "\n".join(f"        // pad {k}" for k in range(MAX_SPLICE - 3))
SPLICE_AT_LIMIT_BAD = f"""
    auto fut = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {{
{_FILLER}
    }}, asio::use_future);
    ioc.run();
    auto r = fut.get();
"""
_FILLER_OVER = "\n".join(f"        // pad {k}" for k in range(MAX_SPLICE + 2))
SPLICE_OVER_LIMIT_INVISIBLE = f"""
    auto fut = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {{
{_FILLER_OVER}
    }}, asio::use_future);
    ioc.run();
    auto r = fut.get();
"""
# The declaration and its consumption on ONE statement must still be classified.
DECL_AND_GET_BAD = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future); auto r = fut.get();
"""
CONTROLS = [
    ("direct   window, unguarded get   -> UNGUARDED", DIRECT_BAD,        0, 1),
    ("INDIRECT window (f.drain())      -> UNGUARDED", INDIRECT_BAD,      0, 1),
    ("indirect window, DOTLESS run()   -> UNGUARDED", DOTLESS_BAD,       0, 1),
    ("guarded by run_window_then_ready -> guarded",   GUARDED_OK,        1, 0),
    ("guarded by run_to_exhaustion..   -> guarded",   GUARDED_RUN_OK,    1, 0),
    ("bare ioc.run() then get()        -> UNGUARDED", RUN_UNBOUNDED_BAD, 0, 1),
    ("guarded by a wait_for assertion  -> guarded",   ASSERT_OK,         1, 0),
    ("`.get()` on a non-future         -> ignored",   NOT_A_FUTURE,      0, 0),
    ("idiom quoted in a COMMENT        -> ignored",   COMMENT_LOOKALIKE, 1, 0),
    ("declaration SPLIT across lines   -> UNGUARDED", SPLIT_DECL_BAD,    0, 1),
    ("a guard naming a DIFFERENT future-> UNGUARDED", FOREIGN_GUARD_BAD, 1, 1),
    ("guard state LEAKING across tests -> UNGUARDED", CROSS_FUNCTION_BAD,1, 1),
    ("unbalanced '(' in a STRING       -> UNGUARDED", STRING_PAREN_BAD,  0, 1),
    ("declaration and get in ONE stmt  -> UNGUARDED", DECL_AND_GET_BAD,  0, 1),
    ("decl spanning JUST UNDER MAX_SPLICE -> UNGUARDED", SPLICE_AT_LIMIT_BAD,       0, 1),
    ("decl spanning OVER MAX_SPLICE -> INVISIBLE (known)", SPLICE_OVER_LIMIT_INVISIBLE, 0, 0),
]
# The DISPOSITION axes get their own controls, straddling every boundary they
# draw. Same rule as above: synthetic, and each names the class it must produce,
# so a mutation of a rule must move at least one of them.
DISPO_CASES = [
    ("fixture method run(300) is NOT ioc.run()", """
struct F { asio::io_context ioc; void run(int ms = 400) { ioc.run_for(ms); } };
TEST(A, B) {
    F f;
    auto fut = asio::co_spawn(f.ioc, s.open(), asio::use_future);
    f.run(300);
    (void)fut.get();
}
""", ("CALLER-ONLY", "HELPER")),
    ("fixture method run() is NOT ioc.run()", """
struct F { asio::io_context ioc; void run() { ioc.run_for(200ms); } };
TEST(A, B) {
    F f;
    auto fut = asio::co_spawn(f.ioc, s.open(), asio::use_future);
    f.run();
    (void)fut.get();
}
""", ("CALLER-ONLY", "HELPER")),
    ("ioc.run()   -> RUN-UNBOUNDED", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    (void)fut.get();
}
""", ("CALLER-ONLY", "RUN-UNBOUNDED")),
    ("ioc.run(ec) -> RUN-UNBOUNDED (any args)", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run(ec);
    (void)fut.get();
}
""", ("CALLER-ONLY", "RUN-UNBOUNDED")),
    ("f.ioc.run() through a fixture", """
struct F { asio::io_context ioc; };
TEST(A, B) {
    F f;
    auto fut = asio::co_spawn(f.ioc, s.open(), asio::use_future);
    f.ioc.run();
    (void)fut.get();
}
""", ("CALLER-ONLY", "RUN-UNBOUNDED")),
    ("inline run_for -> RUN-BOUNDED", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    (void)fut.get();
}
""", ("CALLER-ONLY", "RUN-BOUNDED")),
    ("thread_pool is self-driving", """
TEST(A, B) {
    asio::thread_pool pool{1};
    auto fut = asio::co_spawn(pool.get_executor(), s.open(), asio::use_future);
    something();
    (void)fut.get();
}
""", ("POOL", "HELPER")),
    ("a worker thread drives THIS context", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    std::thread t([&] { ioc.run(); });
    (void)fut.get();
    t.join();
}
""", ("THREADED", "RUN-UNBOUNDED")),
    ("a thread driving a DIFFERENT context escalates", """
TEST(A, B) {
    asio::io_context ioc_a;
    asio::io_context ioc_b;
    std::thread t([&] { ioc_a.run(); });
    auto fut = asio::co_spawn(ioc_b, s.open(), asio::use_future);
    ioc_b.run();
    (void)fut.get();
    t.join();
}
""", ("THREAD-IN-FILE", "RUN-UNBOUNDED")),
    ("a run() quoted in a COMMENT is not a run", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    // the old shape was ioc.run(); which we no longer do
    f.settle();
    (void)fut.get();
}
""", ("CALLER-ONLY", "HELPER")),
]

ok = True
if not quiet:
    print("=== SELF-TEST: get-anchored sweep (synthetic fixtures) ===")
for name, src, want_g, want_b in CONTROLS:
    g, b = classify(src)
    good = (g == want_g and len(b) == want_b)
    ok &= good
    if not quiet:
        print(f"  {'ok   ' if good else '!!FAIL'} {name}  (guarded={g} unguarded={len(b)})")
for name, src, want in DISPO_CASES:
    rows_ = classify(src)[1]
    got = (rows_[0][2], rows_[0][3]) if len(rows_) == 1 else ("<%d rows>" % len(rows_), "")
    good = got == want
    ok &= good
    if not quiet:
        print(f"  {'ok   ' if good else '!!FAIL'} disposition: {name}  -> {got[0]}/{got[1]}")
if not ok:
    sys.exit("\nCONTROL FAILED -- sweep output is NOT evidence. Fix before trusting a number.")
if not quiet:
    print("SWEEP PROVEN: reports both classes; sees the indirected window in both")
    print("spellings, a split declaration, a foreign guard, and cross-test leakage.\n")

# ── the real scan ────────────────────────────────────────────────────────────
files = sorted(p for p in (root / sub).rglob("*")
               if p.suffix in (".cpp", ".hpp", ".cc", ".h") and p.is_file())
tot_g = tot_b = 0
rows = []
for p in files:
    try:
        g, b = classify(p.read_text(errors="replace"))
    except OSError:
        continue
    tot_g += g; tot_b += len(b)
    if b:
        rows.append((p.relative_to(root), b))

for rel, b in rows:
    print(f"{rel}  ({len(b)} unguarded)")
    for ln, txt, ec, pc in b:
        tag = f"  [{ec} x {pc}]" if disposition else ""
        print(f"    {ln:5d}  {txt[:76]}{tag}")
if disposition:
    import collections
    tab = collections.Counter()
    per = collections.defaultdict(collections.Counter)
    for rel, b in rows:
        for _, _, ec, pc in b:
            tab[(ec, pc)] += 1
            per[(ec, pc)][str(rel)] += 1
    print("\n=== DISPOSITION (executor-class x pump-shape) ===")
    for (ec, pc), n in tab.most_common():
        print(f"  {n:>4}  {ec:<15} {pc}")
    print("\n  READ THE CLASSES, NOT THE TOTAL. A candidate is a defect only where the")
    print("  CALLING thread must pump. CALLER-ONLY is the only executor class that says")
    print("  so on its own; POOL and THREADED say the opposite; THREAD-IN-FILE says READ")
    print("  THE SITE -- it is escalation, not a verdict.")
    for k, why in ((("CALLER-ONLY", "HELPER"),
                    "census blind spot (c) -- the settled recipe applies unchanged"),
                   (("CALLER-ONLY", "RUN-UNBOUNDED"),
                    "NO window at all -- a distinct shape with a distinct remedy")):
        print(f"\n  top files, {k[0]} x {k[1]}  ({why}):")
        for f_, n in per[k].most_common(8):
            print(f"    {n:>4}  {f_}")

print(f"\nscanned {len(files)} file(s) under {sub}/")
print(f"  guarded   .get() on a co_spawn future : {tot_g}")
print(f"  UNGUARDED .get() on a co_spawn future : {tot_b}   <- CANDIDATES, not defects")
print("\nA candidate is a defect only where the CALLING thread must pump the context.")
print("Self-driving executors (thread_pool, worker-driven io_context, the C ABI's")
print("internal context) are correct with a bare get(); see tests/support/wait_until.hpp.")
PY
