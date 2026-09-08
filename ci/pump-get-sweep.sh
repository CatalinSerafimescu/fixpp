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
#   - Futures held in CONTAINERS were invisible until #289 batch 20. Exactly ONE
#     spelling is tracked now -- `NAME.push_back|emplace_back(asio::co_spawn(...))`
#     consumed by `for (auto& elem : NAME) elem.get()`. That is the CONDITION, and it
#     is narrower than "containers are covered": any other way of reaching the element
#     registers nothing. Re-derive rather than assume, in BOTH directions -- which
#     containers exist, and which of them this spelling actually reaches:
#       git grep -n 'push_back(asio::co_spawn\|emplace_back(asio::co_spawn'
#     ⚠️ No population figure is written here on purpose. An earlier draft of this very
#     line listed the evasions it expected (an index loop, `futs[i].get()`, a moved-from
#     container) -- and a check found NONE of them in the tree. A hypothetical
#     enumeration reads as a finding and is worth less than the recipe above.
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

FIXPP_CI_DIR="$repo_root/ci" python3 - "$scan_root" "$sub" "$quiet" "$disposition" <<'PY'
import os, re, sys
from pathlib import Path

# The shared lexer, not a private copy -- see `ci/cxx_blank.py`'s header. `blank_comments`
# below is still local because this sweep needs comment blanking WITHOUT literal blanking
# (its controls quote the idiom inside strings); `blank_unevaluated` has no such split.
sys.path.insert(0, os.environ["FIXPP_CI_DIR"])
from cxx_blank import (blank_unevaluated, brace_blocks, line_starts,
                       line_index_of)

root, sub, quiet = Path(sys.argv[1]), sys.argv[2], sys.argv[3] == "1"
disposition = sys.argv[4] == "1"

# ⚠️ `run_to_exhaustion_or_report` is NOT reached by `run_window_then_ready` -- they
# share the tail `then_ready` and nothing else, so a new spelling needs its own
# alternative here and its own control below. Widening this without the control is how a
# migration reads as unguarded and gets "migrated" a second time.
# Spelled ONCE. `GUARD` and the DRIVE axis both need this name, and a rename taught to
# only one of them leaves the sweep quietly blind in exactly one axis. No regex
# metacharacters, so the interpolation below expands byte-identically.
_EXHAUST_HELPER = "run_to_exhaustion_or_report"
GUARD = re.compile(rf"run_window_then_ready|{_EXHAUST_HELPER}|"
                   r"yield_window_then_ready|"
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
# ⚠️ ONE LIST OF THREAD SPELLINGS, three consumers. `_THREADTOK` (the deliberately
# broad escalation input for `anythread`) also counts `asio::thread_pool`, which starts
# no thread of its own; `_THREADRUN` and the caller-side filter must NOT. Spelled once so
# a new thread type cannot be taught to one of them and not the others -- the failure
# `_EXHAUST`'s header below already legislates against for run-detectors.
_THREAD_CTOR = r"std::jthread|std::thread|std::async"
_THREAD_CTOR_RE = re.compile(_THREAD_CTOR)
_THREADTOK = re.compile(_THREAD_CTOR + r"|asio::thread_pool")
_THREADRUN = re.compile(rf"(?:{_THREAD_CTOR})[^;]{{0,400}}?(\w+)\s*\.run\(", re.S)
# ⚠️ ANY arguments, not empty parens -- edge 2. `run_for(`/`run_until(`/`run_one(`
# cannot match this: none of them contains the literal `.run(`.
_UNBOUNDED = re.compile(r"([\w>.\-]+)\.run\(")
# ── the SELF-DRIVE axis (batch 22) ───────────────────────────────────────────
# ⚠️ THIS AXIS EXISTS BECAUSE `POOL` IS A POSITIVE DISMISSAL AND HAS NO CLAUSE CHECK.
# `POOL` says "a thread_pool completes the frame without the calling thread", which is
# true only while the pool is still running. Two shapes void it, and both are MEASURED --
# `tests/sync/test_co_spawn_work_guard_contract.cpp` arms 8 and 9, each half proven RED:
#
#   clause S1  the driver must be LIVE at the spawn. A pool retired before the spawn
#              never dispatches the frame.                 -> RETIRED-BEFORE-SPAWN
#   clause S2  `stop()` ABANDONS queued work, so a later join() returns with the frame
#              never started.                              -> STOPPED-BEFORE-GET
#
# `join()` between the spawn and the get is the opposite of a hazard: it BLOCKS until the
# queued work is done, so it dominates the get more strongly than any lexical `run()`.
# It gets its own value rather than being folded into LIVE because the two are reached by
# different reasoning and a reader must not have to guess which one a row took.
#
# ⚠️ SCOPED TO `POOL`, AND THERE ARE **TWO SEPARATE REASONS** -- one structural, one
# empirical. They are about DIFFERENT populations and must not be read as one, because the
# structural reason does not reach the case the empirical one is about.
#
# REASON 1, STRUCTURAL, and it covers `THREADED`: for a pool the driver IS the base, so
# `pool.stop()` names it and the check resolves. A `THREADED` row's driver is a std::thread
# whose variable name this file never captures, and `std::async` has no name at all, so the
# same check would be complete for SOME spellings and silent for the rest. A positive
# dismissal that holds on the spellings it happens to parse is worse than no axis.
# `THREADED` rows report `n/a` and are READ. This reason does not rot.
#
# REASON 2, EMPIRICAL, and it covers only the extension to `drive=EXHAUSTED-OFF-THREAD`.
# ⚠️ REASON 1 DOES NOT APPLY TO IT: that extension needs no driver name at all -- clause S1
# holds structurally there (a run seen only SINCE the spawn was written by a thread
# constructed after it) and clause S2 becomes a `stop()` on the spawn CONTEXT, whose name
# IS known. It was implemented, it was CORRECT, and it was reverted for a property of this
# CORPUS: these tests retire the context on a BAIL-OUT branch the get never reaches, so the
# check escalates nearly every row it covers, and an instrument nobody can act on teaches
# its readers to skip the fraction that mattered too. The underlying capability that is
# missing is BRANCH EXCLUSIVITY -- the same wall #289 batch 21 hit with its clause-2 probe,
# and not a bigger regex.
# ⚠️ REASON 2 IS ABOUT A TREE THAT MOVES, SO RE-DERIVE IT RATHER THAN TRUSTING IT: gate
# `sd` on `ec == 'POOL' or dv == 'EXHAUSTED-OFF-THREAD'` and compare the escalated count
# against the off-thread total the report prints. Ship it only if that ratio has fallen,
# which takes the corpus changing or branch exclusivity arriving -- not the check changing.
#
# ⚠️ THE SHAPES THAT FOOL IT ARE NOT LISTED HERE IN PROSE -- same rule as the DRIVE axis,
# and for the same reason: a prose list shipped a false entry there. They are
# `SELFDRIVE_CASES` below, run on every invocation, each asserting the answer the axis
# actually gives. The live one (`S-f`) is an RAII destructor whose member SHADOWS the
# pool's name, and it errs toward MORE reading, never toward a dismissal.
# ⚠️ ONE PREDICATE, and the verb is CAPTURED so the per-verb query reuses it rather than
# rebuilding a second regex. An earlier draft spelled `\b{base}\.{verb}\(` inline in
# `_retires`, which made clause S1 (which reads `_RETIRE`) and clause S2 (which read the
# inline one) disagree about which spellings retire a pool the moment either was widened --
# silently, with `LIVE`, the DISMISSING value, among the possible splits.
_RETIRE = re.compile(r"\b(\w+)\s*\.\s*(stop|join)\s*\(")


def _retires(seg, base, verb):
    return any(m.group(1) == base and m.group(2) == verb for m in _RETIRE.finditer(seg))


def _exec_class(base, pools, threaded, anythread):
    return ("POOL" if base in pools else
            "THREADED" if base in threaded else
            "THREAD-IN-FILE" if anythread else "CALLER-ONLY")
_BOUNDED = re.compile(r"\.run_for\(|\.run_until\(|\.poll\(|\.poll_one\(")

# ── the DRIVE axis (batch 21) ────────────────────────────────────────────────
# ⚠️ THIS AXIS EXISTS BECAUSE `RUN-UNBOUNDED` DOES NOT SAY *WHICH* CONTEXT RAN.
# `unbounded(seg)` is satisfied by a `.run(` on ANY context declared in the file, and
# the pump-shape axis then reads RUN-UNBOUNDED whether that run drove the context this
# future was spawned on or a different one entirely. For deciding whether the run above
# a site dominates it, that is the whole question.
#
# WHAT MAKES THE ANSWER STRUCTURAL RATHER THAN PER-FILE, and it is measured, not read
# off a header: `asio::co_spawn` holds `execution::outstanding_work.tracked` on the
# SPAWN executor for the frame's whole lifetime (`asio/impl/co_spawn.hpp`,
# `co_spawn_work_guard` / `co_spawn_state`). A live frame is therefore outstanding work
# on that context whatever it is parked on, so a `run()` that returned by EXHAUSTION
# cannot have left it suspended. `tests/sync/test_co_spawn_work_guard_contract.cpp` is
# what establishes this, including the clauses it holds under:
#
#   clause 1  the spawn executor's context IS the driven one   (arm 4 -- one token from
#             arm 1 and it reads the opposite way, which is why `base` is compared)
#   clause 2  the run is not a post-exhaustion no-op            (arm 3 -- `run()` on a
#             stopped context dispatches NOTHING; `restart()` is the site's own to write)
#
# ⚠️ CLAUSE 2 IS NOT DECIDED HERE AND THIS AXIS DOES NOT CLAIM IT. Whether an earlier
# un-restarted `run()` precedes a site is a question about state before the spawn, which
# `seg` (statements SINCE the spawn) cannot see. So EXHAUSTED is an ANNOTATION, in the
# same sense THREAD-IN-FILE is escalation: it says a dominating exhaustion drive on the
# right context is present, not that the site is safe.
# ⚠️ AND `base` IS LEXICAL. An alias, a `&`-bound reference, a strand spelled through
# `.get_executor()` on something else, or a context reached through a fixture member this
# regex does not resolve all read as a different name. For THOSE the error direction is
# toward NO-VISIBLE-EXHAUSTION -- more reading, not less.
#
# ⚠️ THAT IS A PROPERTY OF NAME RESOLUTION, NOT OF THE AXIS, AND READING IT AS THE LATTER
# IS THE MISTAKE. The shapes that produce a FALSE `EXHAUSTED` -- the DISMISSING direction --
# are NOT listed here in prose. They are `LIMIT_CASES` below: real inputs, run through
# `classify()` on every invocation, each asserting the wrong answer the axis actually gives.
#
# ⚠️ THAT IS DELIBERATE AND IT IS THE SECOND ATTEMPT. A prose list stood here and one of its
# five entries -- "sibling blocks reusing a context NAME" -- DID NOT REPRODUCE: it imported
# the guarded-state boundary limitation registered above onto THIS axis, where it does not
# apply, because `since[]` resets at every `_AUTO_SPAWN` binding and not at a boundary. The
# list even claimed each entry had been run. Replacing that entry with a truer one would
# have been the same artifact one round later; a control cannot be false without going RED.
#
# The population these shapes are empty over is TODAY'S TREE, not a property. Re-derive:
#     git grep -n '\.stop()' -- tests/
#
#   drive:  EXHAUSTED             a `<spawn-ctx>.run(` or `run_to_exhaustion_or_report(
#                                 <spawn-ctx>, ...)` appears above the get(). ⚠️ NOT "the
#                                 run dominates the get" -- that is the reading the list
#                                 below falsifies, and it is the phrasing this axis's
#                                 PRINTED summary had to have removed from it.
#           NO-VISIBLE-EXHAUSTION it does not. READ THE SITE.
#
# ⚠️ ONE PREDICATE, over the SAME receiver normalisation `unbounded()` uses. An earlier
# draft spelled `\b{base}\.run\(` inline here, which is a THIRD run-detector with a
# THIRD receiver rule: it accepted `ioc . run(` that `_UNBOUNDED` misses and dropped the
# `->`-qualified receivers `_UNBOUNDED` normalises, so one `run()` token could read
# RUN-UNBOUNDED and NO-VISIBLE-EXHAUSTION at once.
# ⚠️ IT IS DELIBERATELY *NOT* FOLDED INTO `unbounded()`. The helper spelling contains no
# literal `.run(`, so admitting it there would move control 4c from HELPER to
# RUN-UNBOUNDED -- silently re-interpreting an axis whose counts the batch 18/19/20
# records quote. The axes answer different questions; they share only the normalisation.
_EXHAUST = re.compile(rf"([\w>.\-]+)\.run\(|{_EXHAUST_HELPER}\s*\(\s*([\w>.\-]+)\s*,")


def _last_name(tok):
    """The receiver's final segment -- `f->ioc` and `f.ioc` both resolve to `ioc`."""
    return tok.replace("->", ".").split(".")[-1]


def _spawn_base(ex):
    """The declared-context name a spawn executor expression resolves to, or "".

    ⚠️ AN EXPRESSION THE CAPTURE TRUNCATED MUST RESOLVE TO NOTHING, NOT TO ITS PREFIX.
    `_SPAWN_EXEC` stops at the first comma, so a first argument that CONTAINS one is cut
    mid-expression -- and the prefix can still end in a name that resolves. Measured
    counter-example, from a hostile round:

        futs.push_back(asio::co_spawn((s.pool, ioc), waiter(), asio::use_future));

    captures `(s.pool`, whose last segment is `pool`, which `_POOLDECL` has seen -- so the
    row read `POOL`, a POSITIVE DISMISSAL, while the comma operator makes the real executor
    `ioc`. Unbalanced parentheses are the tell that the capture is a fragment; a fragment
    resolves to "", which is in no executor set and in no `ctxnames`, so the row escalates
    and can never read EXHAUSTED. Control `4g`.
    """
    if ex.count("(") != ex.count(")"):
        return ""
    return _last_name(re.sub(r"\.get_executor\(\)$", "", ex).lstrip("*&"))


def exhausts(seg, base, ctxnames):
    r"""Does a run-to-EXHAUSTION on `base` appear in `seg`? Both spellings.

    ⚠️ `base in ctxnames` IS EDGE 1 AGAIN, AND HERE IT GUARDS A POSITIVE DISMISSAL.
    `_UNBOUNDED` carries the same `([\w>.\-]+)\.run\(` and `unbounded()` gates it on the
    receiver resolving to a context DECLARED in the file -- without that, a fixture method
    `f.run(300)` reads as a context run. In the pump-shape axis that mislabels a bucket;
    here it would turn a BOUNDED fixture pump into `EXHAUSTED`, which is a dismissal.
    Latent rather than live -- no candidate row's `base` is a fixture name today -- which
    is exactly why it needs a rule and not a survey.
    """
    if base not in ctxnames:
        return False
    return any(_last_name(m.group(1) or m.group(2)) == base for m in _EXHAUST.finditer(seg))

# ── the CALL-SITE-SCOPE axis (batch 20) ──────────────────────────────────────
# ⚠️ THIS AXIS EXISTS BECAUSE BATCH 19's BUCKET WENT TO 1 AND ITS CLASS DID NOT.
# `CALLER-ONLY x HELPER` counted 9 sites before batch 19 and 1 after, and the record
# said plainly that this was a BUCKET moving, not the class: neither existing axis
# encodes whether the `.get()` runs INSIDE a coroutine. That distinction is not a
# refinement of the other two -- it changes what the outer driver is worth. A blocking
# wait on the io_context's own pumping thread, inside a handler that driver dispatched,
# wedges the driver too, so a bounded outer pump bounds nothing.
#
#   scope:  CORO         the get() lies inside the brace block of a function or lambda
#                        whose return type is `asio::awaitable<...>`.
#           CALLER-SIDE  it does not.
#
# ⚠️ THE DISCRIMINATOR IS STRUCTURAL -- THE RETURN TYPE -- NOT KEYWORD PRESENCE, and
# that is deliberate. "The enclosing scope contains a `co_await`" is satisfied by a TEST
# body that merely SPAWNS a coroutine lambda, so it marks the caller-side get() after
# that lambda's closing brace as CORO. That over-match reads exactly like a survey: it
# reports nearly the whole corpus and discriminates nothing. A return type cannot be
# satisfied from the outside.
#
# ⚠️ NESTING IS THE WHOLE DIFFICULTY, so it is measured by two controls that straddle
# it, not by inspection: a get() INSIDE such a lambda is CORO, and a get() after that
# same lambda's closing brace -- one line later, same TEST -- is CALLER-SIDE.
#
# ⚠️ THE AXIS IS COMPUTED FOR GUARDED ROWS TOO, and that is the only reason a zero here
# is worth anything. Every coroutine-side site is migrated, so they are all guarded and
# would vanish from the candidate list; counting them is what lets a reader see the
# instrument reporting non-zero on real code rather than on a fixture.
# `ci/red-arms/batch20-coroutine-axis.sh` makes that a measurement: it runs THIS sweep
# against the pre-batch-19 corpus, where the same code was unguarded.
_AWAITABLE_INTRO = re.compile(
    r"->\s*(?:asio::)?awaitable\s*<"                       # trailing return (lambdas and fns)
    r"|(?:asio::)?awaitable\s*<[^;{}()]*>\s+[A-Za-z_]\w*\s*\(")  # leading return type


def _body_open(text, start):
    """Offset of the `{` that opens the body of the declarator beginning at `start`,
    or None if that declarator has no body.

    ⚠️ "THE NEXT `{`" IS WRONG IN BOTH DIRECTIONS, and a hostile round produced one
    breaking input for each. Walking from the declarator's start with a PAREN DEPTH is
    what separates them:

      * `awaitable<void> f(std::vector<int> xs = {}) { ... }` -- the braced default
        argument is the next `{`, so the real body was never coloured and the site read
        CALLER-SIDE. **That direction FAILS TOWARD CLEAN**, which is what makes it the
        worse of the two: it is subtracted from the very count this batch reports as 0.
        The default's braces sit inside the parameter list, so a depth test skips them.
      * `awaitable<void> declared_only();` -- a declaration with no body at all, whose
        introducer then coloured the NEXT unrelated block (a following TEST body read
        CORO). A `;` at depth 0 ends the declarator with no body.

    Starting at the MATCH START rather than its end is load-bearing: the leading-return
    spelling's regex consumes the opening paren, so measuring depth from the end would
    begin at depth 1 for one alternative and 0 for the other.
    """
    depth = 0
    for i in range(start, len(text)):
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif depth <= 0:
            if ch == "{":
                return i
            if ch == ";":
                return None
    return None


def coroutine_line_spans(lines):
    """[(open_line_idx, close_line_idx)] for each brace block introduced by an
    `awaitable`-returning function or lambda. Line-granular on purpose: every consumer
    here anchors on a line index, and a finer resolution would buy nothing it can use.

    A block ends at its own closing brace, so a get() BELOW one is outside it -- the
    nesting case controls 2a/2b pin. That property belongs to `brace_blocks`, which
    carries its own control for it.

    ⚠️ THE EARLY-OUT IS THE POINT, NOT A MICRO-OPTIMISATION. Only ~23 % of files under
    tests/ contain `awaitable<` at all, and this sweep is now a tier-1 gate. The first
    draft walked every character of every file twice; it more than doubled the sweep
    (1.02 s -> 2.33 s over 660 files, 10.4M list appends).
    """
    text = "\n".join(lines)
    intros = [m.start() for m in _AWAITABLE_INTRO.finditer(text)]
    if not intros:
        return []
    blocks = brace_blocks(text)
    if blocks is None:      # unbalanced -- claim nothing rather than guess a scope
        return []
    offs = line_starts(lines)
    bodies = {b for b in (_body_open(text, e) for e in intros) if b is not None}
    return [(line_index_of(offs, o), line_index_of(offs, c))
            for o, c in blocks if o in bodies]


# A container of futures, and the loop that consumes it. Batch 19 found three live sites
# of this shape and could not report one of them: `for (auto& f : futs) f.get();` binds
# the receiver to a range-for variable, which the single-binding tracer cannot resolve to
# a `co_spawn`. The alias below is what makes those sites visible.
# ⚠️ IT MUST NOT ADMIT `unique_ptr::get()`. A naive get-anchored probe over the same
# corpus returned 29 hits of which 16 were `dynamic_cast<T*>(client.get())` -- the
# receiver has to trace back to a `co_spawn(..., use_future)` or the axis is noise.
# ⚠️ GROUP 2 IS THE SPAWN EXECUTOR, AND IT WAS MISSING UNTIL #289 BATCH 21. The batch-20
# form captured only the container, and `execs` was then filled with the REMAINDER of the
# push statement -- so `base` for a container row was the tail of
# `ioc, waiter_body(), asio::use_future));`, i.e. `use_future));`. That name is in neither
# `pools` nor `threaded`, so every container row fell through to THREAD-IN-FILE or
# CALLER-ONLY by the value of `anythread` alone. It never produced a false CLEAN -- both
# fallbacks are the escalating ones, and in a thread-free file CALLER-ONLY is also the
# right answer -- but it was safe by ACCIDENT, not by measurement: a container filled from
# a `thread_pool` would have read CALLER-ONLY rather than POOL. Control: `4c`.
_SPAWN_EXEC = r"asio::co_spawn\s*\(\s*([^,]+?)\s*,"
_PUSH_SPAWN = re.compile(r"\b(\w+)\s*\.\s*(?:push_back|emplace_back)\s*\(\s*" + _SPAWN_EXEC)
_AUTO_SPAWN = re.compile(r"\bauto\s+(\w+)\s*=\s*" + _SPAWN_EXEC)
_RANGE_FOR = re.compile(r"\bfor\s*\(\s*(?:const\s+)?auto\s*&?&?\s*(\w+)\s*:\s*(\w+)\s*\)")


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
    spans = coroutine_line_spans(lines)

    def scope_of(line_idx):
        return "CORO" if any(o <= line_idx <= c for o, c in spans) else "CALLER-SIDE"

    ctxnames = set(_CTXDECL.findall(blanked))
    pools = set(_POOLDECL.findall(blanked))
    threaded = set(_THREADRUN.findall(blanked))
    anythread = bool(_THREADTOK.search(blanked))

    def unbounded(txt):
        return any(m.group(1).replace("->", ".").split(".")[-1] in ctxnames
                   for m in _UNBOUNDED.finditer(txt))

    guarded_state, known, execs, since = {}, set(), {}, {}
    # `retired` is the set of names a `.stop(` or `.join(` has been called on SO FAR in
    # this boundary; `retired_at` snapshots it at each spawn, which is the only moment
    # clause S1 is about. Both reset at a BOUNDARY with everything else.
    retired, retired_at = set(), {}
    alias_elem = alias_cont = None
    guarded, bad = [], []
    for start, stmt in statements(lines):
        if BOUNDARY.match(stmt):
            guarded_state, known, execs, since = {}, set(), {}, {}
            retired, retired_at = set(), {}
            alias_elem = alias_cont = None
        # A container filled from `co_spawn(..., use_future)` is tracked under the
        # CONTAINER's name; the range-for below aliases its element onto it.
        pm = _PUSH_SPAWN.search(stmt)
        if pm and "use_future" in stmt:
            name = pm.group(1)
            known.add(name)
            guarded_state.setdefault(name, False)
            # ⚠️ A LIST, NOT A FIRST-WINS SCALAR. The first push's executor used to stand
            # for the container's whole life, so a container pushed pool-then-`ioc` read
            # `POOL` -- a positive dismissal decided by PUSH ORDER. Escalation on
            # disagreement is below; this is the half that keeps the evidence.
            execs.setdefault(name, []).append(pm.group(2))
            # ⚠️ RESET, NOT `setdefault`, AND THIS ONE WAS A LIVE FALSE DISMISSAL.
            # `seg` is "statements since the spawn", and for a container the spawn that
            # matters is the LAST push -- a `run()` between push 1 and push 2 dominates
            # nothing that push 2 queued. With `setdefault` that run stayed in `seg` and
            # the row read `EXHAUSTED`: the DISMISSING direction. Control: `4h`.
            since[name] = []
            retired_at[name] = set(retired)
        # ⚠️ THE ALIAS PERSISTS UNTIL THE NEXT RANGE-FOR OR BOUNDARY, on purpose: the
        # guard and the `.get()` are separate statements inside the loop BODY, so an
        # alias scoped to the `for` statement alone would see neither together.
        fm = _RANGE_FOR.search(stmt)
        if fm:
            alias_elem, alias_cont = ((fm.group(1), fm.group(2))
                                      if fm.group(2) in known else (None, None))
        m = _AUTO_SPAWN.search(stmt)
        if m and "use_future" in stmt:
            known.add(m.group(1))
            # ⚠️ ONLY AN `auto NAME = co_spawn(...)` RE-BINDING RESETS THE GUARD. A bare
            # `fut = asio::co_spawn(...)` onto an already-declared name does not match the
            # pattern above, so a guard set for the FIRST binding still covers the second
            # future's `get()` -- a false clean. Pre-existing; recorded rather than fixed
            # here, because widening the pattern is a change to a pinned instrument and
            # `ci/test-pump-census.sh` is the harness that would have to grow with it.
            guarded_state[m.group(1)] = False
            execs[m.group(1)] = [m.group(2)]
            since[m.group(1)] = []
            retired_at[m.group(1)] = set(retired)
            # NO `continue` here: the same statement may also consume the future.
        if GUARD.search(stmt):
            for name in known:
                if re.search(rf'\b{re.escape(name)}\b', stmt):
                    guarded_state[name] = True
            if alias_elem and re.search(rf'\b{re.escape(alias_elem)}\b', stmt):
                guarded_state[alias_cont] = True
        # Only the CALL sites matter from here down; an unevaluated operand is not one.
        evaluated = blank_unevaluated(stmt)
        for name in list(since):
            if not re.search(rf'\b{re.escape(name)}\s*\.get\(\)', evaluated):
                since[name].append(stmt)
        # After the spawns above, so a `pool.stop()` in the SAME statement as a spawn
        # does not retroactively retire it for that spawn's own snapshot.
        # ⚠️ GATED ON `pools`, AND THE GATE IS EXACT RATHER THAN AN APPROXIMATION: a row
        # reads `POOL` iff its base is in `pools`, and only a `POOL` row ever consults
        # `retired`. With no pool declared in the file there is nothing this scan can
        # change, and it would otherwise run a regex over every statement of every file
        # to serve a minority of them.
        if pools:
            retired.update(rm.group(1) for rm in _RETIRE.finditer(evaluated))
        for gm in re.finditer(r'(?:^|[^\w.])(\w+)\.get\(\)', evaluated):
            # ⚠️ `alias_elem not in known` IS THE SHADOW GUARD, and without it the alias
            # is a FALSE CLEAN. A range-for element is usually a short name (`f`), and a
            # later `auto f = asio::co_spawn(...)` in the same TEST re-uses it -- BOUNDARY
            # does not reset at a block, only at a function/TEST. The new future's
            # `f.get()` was then attributed to the CONTAINER, inheriting the container's
            # guarded state, and vanished from the report. A name that is a known future
            # in its own right is never an alias.
            name = (alias_cont if gm.group(1) == alias_elem and alias_elem not in known
                    else gm.group(1))
            if name not in known:
                continue
            if guarded_state.get(name):
                # The SCOPE of a guarded site is kept, not just its count: it is the
                # only way a reader can see this axis reporting non-zero on real code
                # once every coroutine-side site is migrated.
                guarded.append(scope_of(start))
            else:
                # ⚠️ THE COMBINATION RULE, ONCE, because a row can carry N spawns and every
                # axis below needs one: a value that DISMISSES must hold for EVERY base; a
                # value that ESCALATES fires on ANY. Getting it backwards produces a silent
                # dismissal, which is the defect class this axis exists to remove. That is
                # why `dv` is `all(...)`, `STOPPED-BEFORE-GET` is `any(...)`, and
                # `JOINED-BEFORE-GET` -- a dismissal, not a hazard -- is `all(...)`.
                # A SET: multiplicity and order are never read, and a container pushed 30
                # times on one context would otherwise carry 30 identical entries through
                # every predicate below.
                bases = {_spawn_base(e) for e in (execs.get(name) or ["?"])}
                classes = {_exec_class(b, pools, threaded, anythread) for b in bases}
                # ⚠️ ESCALATE ON DISAGREEMENT, never pick one. A container whose pushes
                # land on executors of different classes has no single answer, and every
                # way of choosing one is a dismissal reached by an accident of order.
                ec = classes.pop() if len(classes) == 1 else "MIXED-EXEC"
                seg_stmts = since.get(name, [])
                seg = " ".join(seg_stmts)
                # ⚠️ `EXHAUSTED` USED TO CONFLATE TWO DIFFERENT DISMISSALS, and the split
                # below is the batch-22 measurement. A `.run(` written inside a
                # thread-construct statement is a run on ANOTHER THREAD: it is still a
                # drive, but "it appears above the get()" is then not program order, so
                # the row is NOT dismissed by batch 21's work-guard argument -- it is
                # dismissed by the SELF-DRIVING one, whose clauses (S1/S2) this file
                # checks only for `POOL`. Reported apart so the rows resting on an
                # UNCHECKED argument are counted rather than hidden inside a green word.
                # Live: tests/sync/test_cancellation_mid_wait.cpp's 11 rows read
                # `EXHAUSTED` purely from `std::thread thread_a([&] { ioc_a.run(); });`.
                caller_seg = " ".join(t for t in seg_stmts if not _THREAD_CTOR_RE.search(t))

                def all_exhaust(text):
                    return all(exhausts(text, b, ctxnames) for b in bases)
                pc = ("RUN-UNBOUNDED" if unbounded(seg) else
                      "RUN-BOUNDED" if _BOUNDED.search(seg) else "HELPER")
                # Must name `base`: a run on a DIFFERENT context dominates nothing
                # (clause 1, arm 4). EVERY base, not any: a container is dominated only
                # if the run covers all of them.
                dv = ("EXHAUSTED" if all_exhaust(caller_seg) else
                      "EXHAUSTED-OFF-THREAD" if all_exhaust(seg) else
                      "NO-VISIBLE-EXHAUSTION")
                if ec != "POOL":
                    sd = "n/a"
                elif any(b in retired_at.get(name, set()) for b in bases):
                    sd = "RETIRED-BEFORE-SPAWN"
                elif any(_retires(seg, b, "stop") for b in bases):
                    sd = "STOPPED-BEFORE-GET"
                elif all(_retires(seg, b, "join") for b in bases):
                    sd = "JOINED-BEFORE-GET"
                else:
                    sd = "LIVE"
                bad.append((start + 1, lines[start].strip() or stmt[:70], ec, pc,
                            scope_of(start), dv, sd))
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
# The batch-19 COROUTINE-SIDE spelling, for the same reason: it is its own regex
# alternative, so no other control can prove it is credited. The straddle is the
# unguarded twin two entries below (`RUN_UNBOUNDED_BAD` and friends) -- a bare
# `co_await yield_n(N); fd.get();` carries no guard token at all and still reads
# UNGUARDED.
GUARDED_YIELD_OK = """
    auto fd = asio::co_spawn(ex, drain(), asio::use_future);
    co_await yield_n(8);
    if (!co_await fixpp::test_support::yield_window_then_ready(fd, 8, "X/drain")) {
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "X/drain";
        co_return;
    }
    fd.get();
"""
YIELD_ONLY_BAD = """
    auto fd = asio::co_spawn(ex, drain(), asio::use_future);
    co_await yield_n(8);
    fd.get();
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
# The `decltype` boundary, straddled in BOTH directions in ONE fixture so a
# blanket exclusion cannot pass it. `decltype(fut.get())` must vanish; the real
# `fut.get()` four lines below it must still be the row. A fix that blanked the
# whole statement, or the whole line, or everything after `decltype`, fails here.
DECLTYPE_UNEVALUATED = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    using R = decltype(fut.get());
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
        return R{std::unexpected(kWindowMissSentinel)};
    }
    return fut.get();
"""
# ... and the NEGATIVE edge: the same idiom with the guard REMOVED. The real
# `get()` must still report, i.e. the exclusion must not have swallowed the
# statement that carries it.
DECLTYPE_STILL_BAD = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    using R = decltype(fut.get());
    ioc.run_for(200ms);
    ioc.restart();
    return fut.get();
"""

CONTROLS = [
    ("decltype operand is NOT a call   -> guarded",   DECLTYPE_UNEVALUATED, 1, 0),
    ("...and the REAL get() still reports",           DECLTYPE_STILL_BAD,   0, 1),
    ("direct   window, unguarded get   -> UNGUARDED", DIRECT_BAD,        0, 1),
    ("INDIRECT window (f.drain())      -> UNGUARDED", INDIRECT_BAD,      0, 1),
    ("indirect window, DOTLESS run()   -> UNGUARDED", DOTLESS_BAD,       0, 1),
    ("guarded by run_window_then_ready -> guarded",   GUARDED_OK,        1, 0),
    ("guarded by run_to_exhaustion..   -> guarded",   GUARDED_RUN_OK,    1, 0),
    ("guarded by yield_window_then_ready-> guarded",  GUARDED_YIELD_OK,  1, 0),
    ("yield_n alone is NOT a guard     -> UNGUARDED", YIELD_ONLY_BAD,    0, 1),
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

# ── controls for the CALL-SITE-SCOPE axis and the container shape (batch 20) ──
# Each case pins ONE claim, and the pair that matters is 2a/2b: the SAME lambda, one
# get() inside its braces and one after them. A scope rule that cannot separate those
# two lines reports the whole corpus CORO and discriminates nothing.
# `want` is (row_count, scope_of_first_row_or_None).
SCOPE_CASES = [
    ("2a  get INSIDE an awaitable lambda            -> CORO", """
TEST(A, B) {
    asio::io_context ioc;
    auto body = [&]() -> asio::awaitable<void> {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        co_await yield_n(4);
        fut.get();
    };
    asio::co_spawn(ioc, body(), asio::detached);
    ioc.run();
}
""", (1, "CORO")),
    ("2b  get AFTER that same lambda's closing brace -> CALLER-SIDE", """
TEST(A, B) {
    asio::io_context ioc;
    auto body = [&]() -> asio::awaitable<void> {
        co_await yield_n(4);
    };
    auto fut = asio::co_spawn(ioc, body(), asio::use_future);
    ioc.run_for(200ms);
    fut.get();
}
""", (1, "CALLER-SIDE")),
    ("2c  leading return type is the same coroutine  -> CORO", """
asio::awaitable<void> drive(asio::io_context& ioc) {
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    co_await yield_n(4);
    fut.get();
}
""", (1, "CORO")),
    ("2d  a coroutine ELSEWHERE in the file does not colour a caller-side get", """
asio::awaitable<void> helper() { co_return; }
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run_for(200ms);
    fut.get();
}
""", (1, "CALLER-SIDE")),
    # 2e/2f are the two inputs a hostile round used to break "the introducer colours the
    # NEXT `{`". They are kept as controls because the two errors go in OPPOSITE
    # directions, and only one of them is loud.
    ("2e  a braced DEFAULT ARGUMENT does not steal the body  -> CORO", """
asio::awaitable<void> f(std::vector<int> xs = {}) {
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run_for(200ms);
    fut.get();
}
""", (1, "CORO")),
    ("2f  a BODILESS declaration colours nothing            -> CALLER-SIDE", """
asio::awaitable<void> declared_only();

TEST(A, B) {
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run_for(200ms);
    fut.get();
}
""", (1, "CALLER-SIDE")),
    ("3a  container + range-for is REPORTED at all", """
TEST(A, B) {
    asio::io_context ioc;
    std::vector<std::future<void>> futs;
    futs.push_back(asio::co_spawn(ioc, s.open(), asio::use_future));
    ioc.run_for(200ms);
    for (auto& f : futs) f.get();
}
""", (1, "CALLER-SIDE")),
    ("3b  ...and a GUARDED one is not", """
TEST(A, B) {
    asio::io_context ioc;
    std::vector<std::future<void>> futs;
    futs.emplace_back(asio::co_spawn(ioc, s.open(), asio::use_future));
    for (auto& f : futs) {
        if (!fixpp::test_support::run_window_then_ready(ioc, f, 200ms)) return;
        f.get();
    }
}
""", (0, None)),
    ("3c  `unique_ptr::get()` inside a coroutine is NOT a future", """
asio::awaitable<void> drive(asio::io_context& ioc) {
    auto client = make_transport();
    auto* tls = dynamic_cast<TlsTransport*>(client.get());
    co_return;
}
""", (0, None)),
    # ⚠️ A CONTROL FOR "a range-for over an UNKNOWN container aliases nothing" WAS
    # WRITTEN HERE AND DELETED, because a mutation arm proved it could not fail: remove
    # the `fm.group(2) in known` test and it still reports 0 rows, since the alias then
    # names a container that fails the `name not in known` check one step later. It read
    # as a control and tested nothing this batch added. The claim that test actually
    # carries is SHADOWING, which is what 3d below pins -- and 3d DOES go red under
    # exactly that mutation.
    ("3d  a range-for element must not SHADOW a known future of the same name", """
TEST(A, B) {
    asio::io_context ioc;
    auto f = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run_for(200ms);
    for (auto& f : owned) use(f);
    f.get();
}
""", (1, "CALLER-SIDE")),
    # ⚠️ 3e IS THE OTHER HALF OF 3d AND IT IS THE ONE THAT FAILS TOWARD CLEAN. Here the
    # shadowing future is declared AFTER the range-for, so the stale alias was still
    # live: the new `f.get()` inherited the CONTAINER's guarded state and vanished from
    # the report entirely. 3d could not catch it -- there the future is declared first.
    ("3e  ...including one declared AFTER the loop (the alias must not outlive it)", """
TEST(A, B) {
    std::vector<std::future<void>> futs;
    futs.push_back(asio::co_spawn(ioc, s.open(), asio::use_future));
    for (auto& f : futs) {
        if (!run_window_then_ready(ioc, f, 200ms)) return;
        f.get();
    }
    auto f = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run_for(200ms);
    f.get();
}
""", (1, "CALLER-SIDE")),
]

# ── controls for the DRIVE axis and the container SPAWN EXECUTOR (batch 21) ──
# The pair that carries this axis is 4a/4b: the SAME two statements, differing only in
# WHICH context the bare run() names. A rule that cannot separate them is not measuring
# domination, it is counting the word `run`.
# `want` is (executor-class, pump-shape, drive).
DRIVE_CASES = [
    ("4a  bare run() on the SPAWN context           -> EXHAUSTED", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    (void)fut.get();
}
""", ("CALLER-ONLY", "RUN-UNBOUNDED", "EXHAUSTED")),
    ("4b  ...the SAME run() on ANOTHER context      -> NO-VISIBLE-EXHAUSTION", """
TEST(A, B) {
    asio::io_context ioc;
    asio::io_context other;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    other.run();
    (void)fut.get();
}
""", ("CALLER-ONLY", "RUN-UNBOUNDED", "NO-VISIBLE-EXHAUSTION")),
    ("4c  run_to_exhaustion_or_report naming ANOTHER future -> EXHAUSTED", """
TEST(A, B) {
    asio::io_context ioc;
    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    std::vector<std::future<void>> futs;
    futs.push_back(asio::co_spawn(ioc, waiter(), asio::use_future));
    if (!run_to_exhaustion_or_report(ioc, fh, "Site")) return;
    for (auto& f : futs) f.get();
}
""", ("CALLER-ONLY", "HELPER", "EXHAUSTED")),
    ("4d  a BOUNDED run_for is not exhaustion       -> NO-VISIBLE-EXHAUSTION", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run_for(200ms);
    (void)fut.get();
}
""", ("CALLER-ONLY", "RUN-BOUNDED", "NO-VISIBLE-EXHAUSTION")),
    # ⚠️ 4e IS THE CONTROL FOR THE `_PUSH_SPAWN` EXECUTOR CAPTURE, and it is the one that
    # would have read a POSITIVE DISMISSAL wrong rather than merely escalating: before
    # batch 21 the container's spawn executor was never parsed, so this case read
    # THREAD-IN-FILE (`asio::thread_pool` sets `anythread`) instead of POOL.
    # ⚠️ 4f IS THE STRADDLE FOR THE `ctxnames` GATE, and it is the one whose failure is a
    # DISMISSAL rather than an escalation: `f.run(300)` is a fixture method, and without the
    # gate its receiver reads as a context run-to-exhaustion. Same edge the pump-shape axis
    # already pays for -- restated here because this axis makes it costlier.
    ("4f  a FIXTURE method run() is not an exhaustion -> NO-VISIBLE-EXHAUSTION", """
struct F {
    asio::io_context ioc;
    auto get_executor() { return ioc.get_executor(); }
    void run(int ms = 400) { ioc.run_for(ms); }
};
TEST(A, B) {
    F f;
    auto fut = asio::co_spawn(f.get_executor(), s.open(), asio::use_future);
    f.run(300);
    (void)fut.get();
}
""", ("CALLER-ONLY", "HELPER", "NO-VISIBLE-EXHAUSTION")),
    # ⚠️ 4g STRADDLES THE CAPTURE'S OWN LIMIT. The first co_spawn argument contains a comma,
    # so `_SPAWN_EXEC` truncates it -- and the fragment `(s.pool` still ends in a declared
    # pool name. Without `_spawn_base`'s balance check this reads POOL: a self-driving
    # DISMISSAL on a row whose executor is actually the caller's `ioc`.
    ("4g  a TRUNCATED executor expression must escalate, not resolve", """
struct S { asio::thread_pool pool{1}; };
TEST(A, B) {
    S s;
    asio::io_context ioc;
    std::vector<std::future<void>> futs;
    futs.push_back(asio::co_spawn((s.pool, ioc), waiter(), asio::use_future));
    ioc.run();
    for (auto& f : futs) f.get();
}
""", ("THREAD-IN-FILE", "RUN-UNBOUNDED", "NO-VISIBLE-EXHAUSTION")),
    ("4e  a container filled from a thread_pool     -> POOL", """
TEST(A, B) {
    asio::thread_pool pool{4};
    std::vector<std::future<void>> futs;
    futs.push_back(asio::co_spawn(pool, s.open(), asio::use_future));
    for (auto& f : futs) f.get();
}
""", ("POOL", "HELPER", "NO-VISIBLE-EXHAUSTION")),
    ("4h  a run BETWEEN two pushes dominates neither", """
TEST(A, B) {
    asio::io_context ioc;
    std::vector<std::future<void>> futs;
    futs.push_back(asio::co_spawn(ioc, s.open(), asio::use_future));
    ioc.run();
    futs.push_back(asio::co_spawn(ioc, s.send(), asio::use_future));
    for (auto& f : futs) f.get();
}
""", ("CALLER-ONLY", "HELPER", "NO-VISIBLE-EXHAUSTION")),
    ("4i  a container pushed onto TWO executor classes escalates", """
TEST(A, B) {
    asio::thread_pool pool{4};
    asio::io_context ioc;
    std::vector<std::future<void>> futs;
    futs.push_back(asio::co_spawn(pool, s.open(), asio::use_future));
    futs.push_back(asio::co_spawn(ioc, s.send(), asio::use_future));
    for (auto& f : futs) f.get();
}
""", ("MIXED-EXEC", "HELPER", "NO-VISIBLE-EXHAUSTION")),
    ("4j  the only run is inside a std::thread -> EXHAUSTED-OFF-THREAD", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    std::thread worker([&] { ioc.run(); });
    (void)fut.get();
    worker.join();
}
""", ("THREADED", "RUN-UNBOUNDED", "EXHAUSTED-OFF-THREAD")),
    ("4k  a caller-side run ALONGSIDE a thread one still reads EXHAUSTED", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    std::thread worker([&] { other.run(); });
    ioc.run();
    (void)fut.get();
    worker.join();
}
""", ("THREAD-IN-FILE", "RUN-UNBOUNDED", "EXHAUSTED")),
]

# ── SELF-DRIVE controls (batch 22) ───────────────────────────────────────────
# The axis is a clause check over a POSITIVE DISMISSAL, so every value it can emit needs
# a case: a value that is never produced is a branch nobody has run.
SELFDRIVE_CASES = [
    ("S-a  a pool retired ABOVE the spawn      -> RETIRED-BEFORE-SPAWN", """
TEST(A, B) {
    asio::thread_pool pool{4};
    pool.join();
    auto fut = asio::co_spawn(pool, s.open(), asio::use_future);
    (void)fut.get();
}
""", "RETIRED-BEFORE-SPAWN"),
    ("S-b  stop() between the spawn and the get -> STOPPED-BEFORE-GET", """
TEST(A, B) {
    asio::thread_pool pool{4};
    auto fut = asio::co_spawn(pool, s.open(), asio::use_future);
    pool.stop();
    (void)fut.get();
}
""", "STOPPED-BEFORE-GET"),
    ("S-c  join() between the spawn and the get -> JOINED-BEFORE-GET", """
TEST(A, B) {
    asio::thread_pool pool{4};
    auto fut = asio::co_spawn(pool, s.open(), asio::use_future);
    pool.join();
    (void)fut.get();
}
""", "JOINED-BEFORE-GET"),
    ("S-d  nothing retires the pool             -> LIVE", """
TEST(A, B) {
    asio::thread_pool pool{4};
    auto fut = asio::co_spawn(pool, s.open(), asio::use_future);
    (void)fut.get();
}
""", "LIVE"),
    # The thread is constructed BEFORE the spawn, so its run is not in `seg` and the
    # row has no visible drive at all -- nothing for either clause to be about.
    ("S-e  a row with no visible drive is not covered -> n/a", """
TEST(A, B) {
    asio::io_context ioc;
    std::thread th([&] { ioc.run(); });
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    (void)fut.get();
}
""", "n/a"),
    # ⚠️ ASSERTS THE WRONG ANSWER, ON PURPOSE -- the same contract as LIMIT_CASES, in the
    # ESCALATING direction. `retired` is lexical, so a stop()/join() written inside a body
    # that runs LATER (an RAII destructor, a lambda) is recorded at the position it is
    # WRITTEN.
    # ⚠️ IT TAKES BOTH HALVES, AND THE FIRST DRAFT OF THIS FIXTURE HAD ONLY ONE -- it
    # named the member `p`, so `retired` gained `p` and the row read LIVE, i.e. the case
    # written to pin a false escalation did not produce one. The member must SHADOW the
    # pool's own name, which is exactly what the live instance does:
    # tests/session/test_test_request_id_cross_session_race.cpp declares
    # `struct stop_pool_on_exit { asio::thread_pool& pool; ... }` above a spawn, and both
    # its rows escalate though the pool is running throughout. No line number on purpose --
    # the gate in tools/check_line_citations.py exists because that number rots unread.
    # Costs reading, never a dismissal -- which is why it is disclosed rather than fixed.
    # A RED here means the axis learned to see the body: delete this case and the sentence
    # in the axis header, in one commit.
    ("S-f  KNOWN LIMIT: retirement inside an RAII destructor body", """
TEST(A, B) {
    asio::thread_pool pool{4};
    struct stop_on_exit {
        asio::thread_pool& pool;
        ~stop_on_exit() {
            pool.stop();
            pool.join();
        }
    } guard{pool};
    auto fut = asio::co_spawn(pool, s.open(), asio::use_future);
    (void)fut.get();
}
""", "RETIRED-BEFORE-SPAWN"),
]

# ── KNOWN-LIMITATION cases for the DRIVE axis (batch 21) ─────────────────────
# ⚠️ THESE ASSERT THE **WRONG** ANSWER, ON PURPOSE. Each is a shape where the axis says
# `EXHAUSTED` and the run does NOT dominate the get -- the DISMISSING direction, which is
# the one that costs. They exist so the disclosure in the DRIVE axis header cannot be false
# without something going RED, after a prose version of this list shipped an entry that did
# not reproduce.
#
# ⚠️ A RED HERE IS NOT A REGRESSION. It means the axis got SHARPER -- one of these now
# reads NO-VISIBLE-EXHAUSTION. That is good news, and the required response is to delete
# the case and the matching sentence in the header, together, in one commit. Fixing the
# axis and leaving the disclosure is how a correct instrument acquires a false header.
LIMIT_CASES = [
    ("L1  `a.ioc` spawned, `b.ioc` run -- one name to `_last_name`", """
struct F { asio::io_context ioc; };
TEST(A, B) {
    F a, b;
    auto fut = asio::co_spawn(a.ioc, s.open(), asio::use_future);
    b.ioc.run();
    (void)fut.get();
}
"""),
    ("L2  a run() in a declared-but-never-invoked lambda", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    auto never = [&] { ioc.run(); };
    (void)fut.get();
}
"""),
    ("L3  a run() in one arm of an `if` the get() does not share", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    if (cond) { ioc.run(); return; }
    (void)fut.get();
}
"""),
    ("L4  a run() on a SHADOWING context in a nested block", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    { asio::io_context ioc; ioc.run(); }
    (void)fut.get();
}
"""),
    ("L5  a run() inside a STRING LITERAL", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    LOG("we used to call ioc.run() here");
    (void)fut.get();
}
"""),
    ("L7  a thread lambda reached through a CONTAINER of threads", """
TEST(A, B) {
    asio::io_context ioc;
    std::vector<std::thread> pool;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    pool.emplace_back([&] { ioc.run(); });
    (void)fut.get();
}
"""),
    ("L6  stop() before the run -- the THIRD way run() returns", """
TEST(A, B) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.stop();
    ioc.run();
    (void)fut.get();
}
"""),
]

ok = True
if not quiet:
    print("=== SELF-TEST: get-anchored sweep (synthetic fixtures) ===")
for name, src, want_g, want_b in CONTROLS:
    g, b = classify(src)
    good = (len(g) == want_g and len(b) == want_b)
    ok &= good
    if not quiet:
        print(f"  {'ok   ' if good else '!!FAIL'} {name}  (guarded={len(g)} unguarded={len(b)})")
for name, src, want in DISPO_CASES:
    rows_ = classify(src)[1]
    got = (rows_[0][2], rows_[0][3]) if len(rows_) == 1 else ("<%d rows>" % len(rows_), "")
    good = got == want
    ok &= good
    if not quiet:
        print(f"  {'ok   ' if good else '!!FAIL'} disposition: {name}  -> {got[0]}/{got[1]}")
for name, src, (want_n, want_sc) in SCOPE_CASES:
    rows_ = classify(src)[1]
    got = (len(rows_), rows_[0][4] if rows_ else None)
    good = got == (want_n, want_sc)
    ok &= good
    if not quiet:
        print(f"  {'ok   ' if good else '!!FAIL'} scope: {name}  -> {got[0]} row(s), {got[1]}")
for name, src, want in DRIVE_CASES:
    rows_ = classify(src)[1]
    got = ((rows_[0][2], rows_[0][3], rows_[0][5]) if len(rows_) == 1
           else ("<%d rows>" % len(rows_), "", ""))
    good = got == want
    ok &= good
    if not quiet:
        print(f"  {'ok   ' if good else '!!FAIL'} drive: {name}  -> {got[0]}/{got[1]}/{got[2]}")
for name, src, want in SELFDRIVE_CASES:
    rows_ = classify(src)[1]
    got = rows_[0][6] if len(rows_) == 1 else "<%d rows>" % len(rows_)
    good = got == want
    ok &= good
    if not quiet:
        print(f"  {'ok   ' if good else '!!FAIL'} selfdrive: {name}  -> {got}")
for name, src in LIMIT_CASES:
    rows_ = classify(src)[1]
    got = rows_[0][5] if len(rows_) == 1 else "<%d rows>" % len(rows_)
    good = got == "EXHAUSTED"
    ok &= good
    if not quiet:
        note = "" if good else "   <- the axis got SHARPER; delete this case AND its header sentence"
        print(f"  {'ok   ' if good else '!!FAIL'} known limit: {name}  -> {got}{note}")
if not ok:
    sys.exit("\nCONTROL FAILED -- sweep output is NOT evidence. Fix before trusting a number.")
if not quiet:
    print("SWEEP PROVEN: reports both classes; sees the indirected window in both")
    print("spellings, a split declaration, a foreign guard, and cross-test leakage.\n")

# ── the real scan ────────────────────────────────────────────────────────────
files = sorted(p for p in (root / sub).rglob("*")
               if p.suffix in (".cpp", ".hpp", ".cc", ".h") and p.is_file())
# ⚠️ A ZERO-FILE CORPUS IS AN ERROR, NOT A CLEAN BILL. Every control above runs on
# SYNTHETIC fixtures, so they all pass on nothing: a wrong `--root` used to print
# "SWEEP PROVEN", "UNGUARDED .get() ... : 0", "scanned 0 file(s)" and exit 0. That is
# this repo's signature defect reached from the direction of the corpus rather than the
# matcher [[feedback_every_broken_instrument_in_this_repo_fails_toward_clean]].
# ⚠️ IT CLOSES THE ZERO CASE ONLY, and says so rather than implying more:
# `brain/failure-classes.md` also asks for a file COUNT assertion and a symlinked-root
# check, and `Path.rglob` does NOT descend a symlinked directory. A small non-zero
# corpus -- a partial checkout, a `--root` one level off, a symlinked test subtree --
# still reports clean. The count is printed for a reader to judge; nothing asserts it.
if not files:
    print(f"NO FILES under {root / sub} -- the walk found nothing. Every control above",
          file=sys.stderr)
    print("passes on synthetic fixtures, so this would have read GREEN over a corpus it",
          file=sys.stderr)
    print("never opened. Check the scan root.", file=sys.stderr)
    sys.exit(2)
tot_g = tot_b = tot_gc = 0
rows = []
for p in files:
    try:
        g, b = classify(p.read_text(errors="replace"))
    except OSError:
        continue
    tot_g += len(g); tot_b += len(b); tot_gc += g.count("CORO")
    if b:
        rows.append((p.relative_to(root), b))

for rel, b in rows:
    print(f"{rel}  ({len(b)} unguarded)")
    for ln, txt, ec, pc, sc, dv, sd in b:
        # ⚠️ THIS TAG IS A MACHINE INTERFACE, NOT DECORATION, and batch 21 broke it by
        # appending a fourth field: `ci/red-arms/batch20-coroutine-axis.sh` matched
        # `x CORO]`, which POSITION made true only while scope was last. Its ARM 0 --
        # the arm ASSERTING a zero -- then passed vacuously, which is this repo's #1
        # defect class landing inside the arm written to prevent it. Keys, so the next
        # axis costs a consumer nothing; consumers must match `scope=CORO`, never a
        # position. ⚠️ ONE SPELLING IS ENOUGH even for a consumer that reads a HISTORICAL
        # corpus: `batch20-coroutine-axis.sh` substitutes the CORPUS, not the script
        # (`git archive $BASE tests` + `cp -r ci`), so both its arms read today's tag.
        # An earlier draft of THIS comment asserted the opposite -- that two dialects
        # were needed "for good" -- and it was wrong; see that arm's own header.
        tag = (f"  [exec={ec} pump={pc} scope={sc} drive={dv} selfdrive={sd}]"
               ) if disposition else ""
        print(f"    {ln:5d}  {txt[:76]}{tag}")
if disposition:
    import collections
    tab = collections.Counter()
    scope_tab = collections.Counter()
    per = collections.defaultdict(collections.Counter)
    drive_tab = collections.Counter()
    selfdrive_tab = collections.Counter()
    for rel, b in rows:
        for _, _, ec, pc, sc, dv, sd in b:
            tab[(ec, pc)] += 1
            per[(ec, pc)][str(rel)] += 1
            scope_tab[sc] += 1
            # ⚠️ ONLY `POOL` IS EXCLUDED NOW, AND THE CHANGE IS THE BATCH-22 POINT.
            # The first draft restricted this to CALLER-ONLY while its printed reason
            # named only POOL and THREADED -- and THREAD-IN-FILE, the LARGEST escalation
            # class, is precisely the one whose reading this annotation orders. Batch 22
            # then put `THREADED` back IN: once `EXHAUSTED` splits off
            # `EXHAUSTED-OFF-THREAD`, this axis stops asking "did the caller also run it"
            # (which decides nothing for a self-driving executor, the old reason for the
            # exclusion) and starts reporting WHICH dismissal a row rests on. That is the
            # question a `THREADED` row most needs answered. `POOL` stays out because the
            # SELF-DRIVE axis answers it there with clause checks instead of an ordering.
            if ec != "POOL":
                drive_tab[(ec, dv)] += 1
            if sd != "n/a":
                selfdrive_tab[sd] += 1
    print("\n=== DISPOSITION (executor-class x pump-shape) ===")
    for (ec, pc), n in tab.most_common():
        print(f"  {n:>4}  {ec:<15} {pc}")
    print("\n=== CALL-SITE SCOPE (the axis batch 19's bucket did not have) ===")
    for sc, n in scope_tab.most_common():
        print(f"  {n:>4}  {sc}")
    print(f"  {tot_gc:>4}  CORO, already GUARDED  <- not a candidate; printed because a")
    print("        zero above is only worth something if this instrument can report")
    print("        non-zero on real code. `ci/red-arms/batch20-coroutine-axis.sh`")
    print("        runs this same sweep against the pre-batch-19 corpus, where the")
    print("        same sites were unguarded, and requires a non-zero there.")
    print("\n  CORO means the get() is inside an `awaitable`-returning function or lambda,")
    print("  so it runs ON the pumping thread and NO outer driver bounds it -- the")
    print("  executor axis cannot express that, which is why this one exists.")
    print("\n=== DRIVE, over every row except POOL ===")
    for (ec, dv), n in sorted(drive_tab.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"  {n:>4}  {ec:<15} {dv}")
    print("  POOL is omitted: the SELF-DRIVE axis below answers the same question there")
    print("  with clause checks rather than with an ordering.")
    print("  EXHAUSTED           a run-to-exhaustion naming the spawn context appears")
    print("                      above the get in a statement that starts NO thread, so")
    print("                      it is the CALLING thread's own program order. This is")
    print("                      the row batch 21's work-guard argument dismisses.")
    print("  EXHAUSTED-OFF-THREAD  the only such run is written inside a thread-construct")
    print("                      statement. Still a drive, but `above` is no longer")
    print("                      program order, so the row is dismissed by the")
    print("                      SELF-DRIVING argument instead -- whose clauses S1/S2 are")
    print("                      checked only for POOL. THESE REST ON AN UNCHECKED")
    print("                      ARGUMENT and are counted here rather than hidden.")
    print("  EXHAUSTED = a run-to-exhaustion NAMING the spawn context appears above the")
    print("  get(). That is a LEXICAL reading and it is NOT the claim `the run dominates")
    print("  the get()` -- `a.ioc` and `b.ioc` are one name here, a `stop()` before the run")
    print("  is invisible, and control flow is not modelled. Each of those reports")
    print("  EXHAUSTED where the get() is NOT dominated: the DISMISSING direction.")
    print("  ⚠️ SO IT IS AN ANNOTATION, NEVER A DISMISSAL. What it buys is an ordering of")
    print("  the reading, not a verdict. The full list of shapes that fool it, and the")
    print("  three clauses the underlying argument holds under, are at the DRIVE axis")
    print("  comment in this file and MEASURED in:")
    print("      tests/sync/test_co_spawn_work_guard_contract.cpp")
    print("\n=== SELF-DRIVE, over the POOL rows only ===")
    for sd_, n in selfdrive_tab.most_common():
        print(f"  {n:>4}  {sd_}")
    print("  `POOL` claims the pool completes the frame without the calling thread. That")
    print("  holds only while the pool is still running, and these are the two shapes that")
    print("  void it -- both MEASURED in tests/sync/test_co_spawn_work_guard_contract.cpp,")
    print("  arms 8 and 9, every half proven RED by mutation:")
    print("    RETIRED-BEFORE-SPAWN  a stop()/join() on the pool ABOVE the spawn (clause")
    print("                          S1). The frame is queued on a driver that is gone.")
    print("    STOPPED-BEFORE-GET    stop() between the spawn and the get (clause S2). It")
    print("                          ABANDONS the queued work; a later join() returns with")
    print("                          the frame never started.")
    print("    JOINED-BEFORE-GET     join() between the spawn and the get. NOT a hazard --")
    print("                          it blocks until the work is done, so it dominates the")
    print("                          get more strongly than any lexical run().")
    print("    LIVE                  neither: the ordinary shape the POOL dismissal names.")
    print("  ⚠️ THE SCAN IS LEXICAL, SO AN ESCALATION IS A QUESTION, NOT A FINDING. A")
    print("  stop()/join() written inside a body that runs LATER -- an RAII destructor, a")
    print("  lambda -- is recorded at the position it is WRITTEN, so a guard DECLARED above")
    print("  a spawn and RUN at scope exit reads RETIRED-BEFORE-SPAWN while the pool is")
    print("  live throughout. It takes a member that SHADOWS the pool\'s own name; fixture")
    print("  `S-f` above pins it. Read the row before believing it, and do not conclude")
    print("  from a false escalation that the axis is not worth reading -- the value that")
    print("  costs is a wrong LIVE, and no case of that is known.")
    print("  ⚠️ EVERY OTHER ROW REPORTS `n/a` AND IS NOT COVERED -- read them. A pool IS")
    print("  its own driver, so `pool.stop()` names it; a thread driving an io_context is")
    print("  a variable this file never captures, and `std::async` has no name at all.")
    print("  Why this is not extended to drive=EXHAUSTED-OFF-THREAD -- which needs no")
    print("  driver name -- is a note to whoever next edits this instrument, and lives at")
    print("  the SELF-DRIVE axis comment in the script rather than on this report.")
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
