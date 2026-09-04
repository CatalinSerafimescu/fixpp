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
# guard precedes it. It is therefore shape-agnostic: it needs no list of helper
# names, and a helper form nobody anticipated cannot hide from it.
#
# ⚠️ THAT PROPERTY IS THE WHOLE POINT, AND IT WAS LEARNED THE EXPENSIVE WAY. The
# first detector written for this class recognised helper SHAPES. It matched a
# bare `run()` but excluded a preceding '.', so `f.drain()` was invisible and it
# reported ZERO for the one file that then HUNG under a forced-miss arm
# (futex_do_wait, 0.0% CPU). Do not "improve" this script by teaching it to find
# pumps.
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
        -h|--help) sed -n '1,50p' "${BASH_SOURCE[0]}"; exit 0 ;;
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
LOOKBACK = 8

_BLOCK = re.compile(r"/\*.*?\*/", re.S)
_LINE = re.compile(r"//[^\n]*")

def blank_comments(text):
    """Blank comment CONTENT but keep newlines, so reported line numbers stay true.

    A token inside a comment is not evidence about the code -- the same rule
    classify-289.py already applies on both its axes. It matters here because the
    #289 migration comments QUOTE the very idiom this sweep looks for
    (`run_for(W); restart(); fut.get()`), so without this every migrated file
    reports its own header block as an unguarded site."""
    text = _BLOCK.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)
    return _LINE.sub(lambda m: " " * len(m.group(0)), text)

def classify(text):
    """-> (guarded, unguarded_rows). Shape-agnostic: anchored on the get()."""
    lines = blank_comments(text).splitlines()
    futs = set()
    for i, l in enumerate(lines):
        blob = " ".join(lines[i:i+3])
        m = re.search(r'auto\s+(\w+)\s*=\s*asio::co_spawn', l)
        if m and "use_future" in blob:
            futs.add(m.group(1))
    guarded, bad = 0, []
    for i, l in enumerate(lines):
        for m in re.finditer(r'(?:^|[^\w.])(\w+)\.get\(\)', l):
            if m.group(1) not in futs:
                continue
            back = "\n".join(lines[max(0, i - LOOKBACK):i])
            if GUARD.search(back):
                guarded += 1
            else:
                bad.append((i + 1, l.strip()))
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
# The migration comments QUOTE the idiom. Without comment-blanking every migrated
# file reports its own header block; this control is the look-alike that bites.
COMMENT_LOOKALIKE = """
    // The `run_for(W); restart(); fut.get()` sites in this file are migrated.
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) { return; }
    auto r = fut.get();
"""
CONTROLS = [
    ("direct   window, unguarded get   -> UNGUARDED", DIRECT_BAD,    0, 1),
    ("INDIRECT window (f.drain())      -> UNGUARDED", INDIRECT_BAD,  0, 1),
    ("indirect window, DOTLESS run()   -> UNGUARDED", DOTLESS_BAD,   0, 1),
    ("guarded by run_window_then_ready -> guarded",   GUARDED_OK,    1, 0),
    ("guarded by a wait_for assertion  -> guarded",   ASSERT_OK,     1, 0),
    ("`.get()` on a non-future         -> ignored",   NOT_A_FUTURE,  0, 0),
    ("idiom quoted in a COMMENT        -> ignored",   COMMENT_LOOKALIKE, 1, 0),
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
    print("SWEEP PROVEN: reports BOTH classes, and sees the indirected window in both spellings.\n")

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
