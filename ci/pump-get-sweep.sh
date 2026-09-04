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

while [ "$#" -gt 0 ]; do
    case "$1" in
        --root)  [ "$#" -ge 2 ] || fail "--root requires an argument";  scan_root="$2"; shift 2 ;;
        --dir)   [ "$#" -ge 2 ] || fail "--dir requires an argument";   sub="$2";       shift 2 ;;
        --quiet) quiet=1; shift ;;
        -h|--help) sed -n '1,55p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

command -v python3 >/dev/null || fail "python3 is required"

python3 - "$scan_root" "$sub" "$quiet" <<'PY'
import re, sys
from pathlib import Path

root, sub, quiet = Path(sys.argv[1]), sys.argv[2], sys.argv[3] == "1"

GUARD = re.compile(r"run_window_then_ready|pump_until_ready|pump_until\(|"
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
    rather than swallowing the region. A swallowed region is a silent false clean."""
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
    lines = blank_comments(text).splitlines()
    guarded_state, known = {}, set()
    guarded, bad = 0, []
    for start, stmt in statements(lines):
        if BOUNDARY.match(stmt):
            guarded_state, known = {}, set()
        m = re.search(r'\bauto\s+(\w+)\s*=\s*asio::co_spawn', stmt)
        if m and "use_future" in stmt:
            known.add(m.group(1))
            guarded_state[m.group(1)] = False      # re-binding resets the guard
            # NO `continue` here: the same statement may also consume the future.
        if GUARD.search(stmt):
            for name in known:
                if re.search(rf'\b{re.escape(name)}\b', stmt):
                    guarded_state[name] = True
        for gm in re.finditer(r'(?:^|[^\w.])(\w+)\.get\(\)', stmt):
            name = gm.group(1)
            if name not in known:
                continue
            if guarded_state.get(name):
                guarded += 1
            else:
                bad.append((start + 1, lines[start].strip() or stmt[:70]))
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
# The declaration and its consumption on ONE statement must still be classified.
DECL_AND_GET_BAD = """
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future); auto r = fut.get();
"""
CONTROLS = [
    ("direct   window, unguarded get   -> UNGUARDED", DIRECT_BAD,        0, 1),
    ("INDIRECT window (f.drain())      -> UNGUARDED", INDIRECT_BAD,      0, 1),
    ("indirect window, DOTLESS run()   -> UNGUARDED", DOTLESS_BAD,       0, 1),
    ("guarded by run_window_then_ready -> guarded",   GUARDED_OK,        1, 0),
    ("guarded by a wait_for assertion  -> guarded",   ASSERT_OK,         1, 0),
    ("`.get()` on a non-future         -> ignored",   NOT_A_FUTURE,      0, 0),
    ("idiom quoted in a COMMENT        -> ignored",   COMMENT_LOOKALIKE, 1, 0),
    ("declaration SPLIT across lines   -> UNGUARDED", SPLIT_DECL_BAD,    0, 1),
    ("a guard naming a DIFFERENT future-> UNGUARDED", FOREIGN_GUARD_BAD, 1, 1),
    ("guard state LEAKING across tests -> UNGUARDED", CROSS_FUNCTION_BAD,1, 1),
    ("unbalanced '(' in a STRING       -> UNGUARDED", STRING_PAREN_BAD,  0, 1),
    ("declaration and get in ONE stmt  -> UNGUARDED", DECL_AND_GET_BAD,  0, 1),
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
    for ln, txt in b:
        print(f"    {ln:5d}  {txt[:76]}")
print(f"\nscanned {len(files)} file(s) under {sub}/")
print(f"  guarded   .get() on a co_spawn future : {tot_g}")
print(f"  UNGUARDED .get() on a co_spawn future : {tot_b}   <- CANDIDATES, not defects")
print("\nA candidate is a defect only where the CALLING thread must pump the context.")
print("Self-driving executors (thread_pool, worker-driven io_context, the C ABI's")
print("internal context) are correct with a bare get(); see tests/support/wait_until.hpp.")
PY
