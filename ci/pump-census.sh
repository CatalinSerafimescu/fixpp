#!/usr/bin/env bash
# Census of lexical run_for(...)+future.get() candidates (#289).
#
# Scope — READ THIS BEFORE TRUSTING THE NUMBER:
#   - tests/**/*.cpp and tests/**/*.hpp
#   - a `.run_for(...)` occurrence
#   - a named receiver's `.get(...)` in one of the six FOLLOWING source lines
#   - comments, block comments, string literals and raw string literals are
#     blanked before matching (so they never match)
#
# This is a census of LEXICAL CANDIDATES, not a hand-validated semantic site
# count, and it covers ONLY this one population. It does NOT cover the
# approximately 68 bare settle pumps or the ~28+ pool-serviced co_spawn sites
# #289 also names — those need their own classifiers with their own targeted
# RED mutants before being folded into any reported number. A classifier known
# to report zero for a population it was never built to see is worse than an
# explicit scope limitation.
#
# ── WHY THIS DOES NOT MATCH THE ISSUE'S "340 / 85" ───────────────────────────
# #289 states 340 sites / 85 files. This script's *.cpp-only reading is 341/83;
# subtracting the one known false positive below gives 340/83. The SITE count
# reconciles; the FILE count does not — the issue's two figures came from
# different sweeps and were never a matched pair. Including *.hpp (in scope by
# design — headers carry real sites the issue's own .cpp-only sweep missed:
# tests/interop/parity/parity_support.hpp and
# tests/session/support/group_dispatch_fixture.hpp) takes the reading to
# 346/85. Do not describe 346 as reconciling with 340; it does not, and
# claiming otherwise misrepresents two different classifiers as one.
#
# ── KNOWN LEXICAL FALSE POSITIVES: NONE, BECAUSE THE POPULATION IS EMPTY ─────
# This section used to name one -- a one-line `void run_ioc() { ioc.run_for(...);
# ioc.restart(); }` helper in tests/session/test_application_lifecycle.cpp, whose
# matching `fut.get()` six lines later was in a DIFFERENT function. That helper was
# deleted in #289 batch 15 (its six call sites each had a future in scope, so the
# indirection inlined away), and with it the false positive.
#
# The paragraph is REPLACED rather than corrected because the interesting part was
# never the site: it was that a one-line helper can manufacture a lexical match
# across a function boundary. That remains true of this scanner and is why the
# seeded-positive control below asserts an EXACT hit set rather than a count.
#
# ── PRESERVED SITES THIS PIN CANNOT SPEAK ABOUT ──────────────────────────────
# A site that is deliberately NOT migrated can still LEAVE this census, because
# migrating a NEIGHBOURING site inserts lines and pushes the `.get()` past the
# six-line lookahead above. Such a site is then invisible here -- absent, and
# indistinguishable from migrated. Both known cases are staging windows, kept
# because the awaited op is REQUIRED to still be pending when the window
# returns (a `clock->advance()` sits between the window and the `get()`);
# migrating one reports a miss on the one outcome the test requires. Measured,
# not reasoned: migrating the second below turns a 2/2 passing binary into 2/2
# FAILED at that exact site.
#
#   tests/session/cancellation_two_phase_test.cpp   (PR #313; 8 rows -> 7 migrated)
#   tests/session/conformance/tc_liveness_test.cpp  (the conformance-directory
#                                                    migration; 6 rows -> 5 migrated,
#                                                    the `close(graceful)` 50 ms
#                                                    staging window in `do_close`)
#
# Each carries a comment at the line itself. Add to this list, do not renumber
# it -- a line number here would rot exactly as the census row did.
#
# A SECOND blind spot, different mechanism: a site whose `.get()` was ALWAYS more
# than six lines below its window was never in the pin at all, so it cannot leave
# it. A file's absence from the pin is therefore never evidence that the file has
# no unguarded `get()` -- only a per-`.get()` sweep answers that.
#
# This is not hypothetical, and the history does not rot: two such sites escaped
# the pin in `tests/session/conformance/tc_logout_test.cpp` (`.get()` seven and
# twenty-one lines below their windows). ONE of them -- `GracefulLogoutTimeout`,
# which runs first -- wedged the linux-clang-ubsan lane of Tier 1 for 86 minutes
# on 2026-08-31; the process died there, so the second site was never reached and
# is not claimed to have wedged that run. It carried the identical unconditional-
# `get()` hazard and was migrated with it. The pin read 230 before and 230 after
# -- it never had an opinion either way.
#
# To re-derive the size of this blind spot, widen the lookahead below and diff
# against this census. Prove the widened scan non-vacuous first: run it over a
# tree that still contains a known far `.get()` and confirm it reports it.
#
# A THIRD blind spot, and WIDENING THE LOOKAHEAD DOES NOT REACH IT: the window is
# not lexically present at all, because the pump is indirected through a HELPER.
#
#     auto fut = asio::co_spawn(f.ioc, sess.send(payload), asio::use_future);
#     f.drain();                  // <- the window, behind a member call
#     auto result = fut.get();    // <- unconditional
#
# There is no `ioc.run_for` for this scanner to anchor on, so no lookahead width
# finds it; blind spot (b)'s widening recipe reports nothing here and that zero
# means only that the anchor is absent. A site of this shape wedges rather than
# failing, so it costs a lane rather than an assertion (#337 is the reference
# instance).
#
# ⚠️ DO NOT DETECT THIS BY RECOGNISING THE HELPER -- a detector that matches helper
# SHAPES can only find the shapes its author thought of. Start from the thing that
# actually blocks instead: every `.get()` on a co_spawn future, asking whether a
# guard precedes it and NAMES that future. `ci/pump-get-sweep.sh` implements it,
# with a control per known evasion; add a control the day a new one is found.
#
# Its size across the tree is NOT recorded here, deliberately: it is a property of
# a moving tree, and that sweep re-derives it in seconds.
#
# ── EXACT SET, NOT A COUNT ────────────────────────────────────────────────────
# Comparison is by exact set equality against ci/expected-pump-sites.txt, in
# BOTH directions. A count alone is satisfied by one site removed plus one
# added, by a site moving between files, or by classifier drift that swaps a
# real site for a false positive — none of which a bare cardinality check can
# see. See ci/expected-eligible-tests.txt:13-19 for why this repo already
# treats a floor/count as weaker than exact equality.
#
# Usage:
#   bash ci/pump-census.sh [--root DIR] [--expected FILE]

set -euo pipefail

fail() {
    echo "pump-census: error: $*" >&2
    exit 1
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_root="$repo_root"
expected="$repo_root/ci/expected-pump-sites.txt"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --root)
            [ "$#" -ge 2 ] || fail "--root requires an argument"
            scan_root="$2"
            shift 2
            ;;
        --expected)
            [ "$#" -ge 2 ] || fail "--expected requires an argument"
            expected="$2"
            shift 2
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
done

[ -d "$scan_root/tests" ] ||
    fail "tests directory not found under scan root: $scan_root"
[ -f "$expected" ] ||
    fail "expected-site pin not found: $expected"

command -v python3 >/dev/null ||
    fail "python3 is required"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
actual="$tmp/actual.txt"

# The pin may carry `#` comments and may hold ZERO site rows. Both are new, and
# both exist for the same reason: the direct population is now empty, so a pin
# that could not express "empty" could not express the truth. A zero-byte file
# would say it ambiguously -- indistinguishable from a truncation -- so the
# emptiness is stated IN the file and the rows are compared after stripping.
pin="$tmp/expected.txt"
sed -e 's/[[:space:]]*#.*$//' -e '/^[[:space:]]*$/d' "$expected" >"$pin"

if [ -s "$pin" ] && grep -Evq '^tests/.+\.(cpp|hpp):[1-9][0-9]*$' "$pin"; then
    fail "expected-site pin contains a malformed row: $expected"
fi

if ! LC_ALL=C sort -u "$pin" | cmp -s - "$pin"; then
    fail "expected-site pin must be sorted and contain no duplicates: $expected"
fi

if ! python3 - "$scan_root" >"$actual" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
tests = root / "tests"

files = sorted(
    list(tests.rglob("*.cpp")) +
    list(tests.rglob("*.hpp"))
)

if not files:
    raise SystemExit(
        f"pump-census: error: no .cpp/.hpp files found under {tests}"
    )

run_re = re.compile(r"\.run_for\s*\(")
get_re = re.compile(
    r"\b[A-Za-z_][A-Za-z_0-9]*\s*\.\s*get\s*\("
)

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

def scan(source):
    """Return the 1-based line of every window whose `.get()` is within six lines."""
    lines = blank_non_code(source).splitlines()
    hits = []
    for index, line in enumerate(lines):
        if not run_re.search(line):
            continue
        following = lines[index + 1:index + 7]
        if any(get_re.search(candidate) for candidate in following):
            hits.append(index + 1)
    return hits


# ── SEEDED-POSITIVE CONTROL ──────────────────────────────────────────────────
# This scanner USED to treat a zero reading as proof it was broken, because for
# the whole life of the #289 migration a zero could only mean that. That is no
# longer true: the direct population it scans is now empty, so the blanket
# refusal would reject the one correct answer and make the pin unmaintainable.
#
# A zero is admissible only from an instrument PROVEN able to report non-zero,
# so the licence comes from a control rather than from trust. The fixture below
# carries one site of each shape the scanner must separate; the assertion is
# EXACT (`!=`), so a scanner that widened into matching the negatives fails here
# too, not just one that stopped matching the positive.
#
# The far-`get()` negative is NOT a defect being tolerated -- it is blind spot
# (b) from this file's header, pinned as a PROPERTY so that widening the
# lookahead is a deliberate act that breaks this control and must be re-argued.
CONTROL = """\
void positive() {
    auto fut = asio::co_spawn(ioc, c(), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    (void)fut.get();
}
void migrated() {
    auto fut = asio::co_spawn(ioc, c(), asio::use_future);
    if (!run_window_then_ready(ioc, fut, 200ms, "x")) { return; }
    (void)fut.get();
}
void commented_out() {
    // ioc.run_for(200ms);
    // ioc.restart();
    // (void)fut.get();
}
void far_get() {
    ioc.run_for(200ms);
    ioc.restart();
    int a = 0;
    int b = 0;
    int c2 = 0;
    int d = 0;
    int e = 0;
    int f2 = 0;
    (void)a; (void)b; (void)c2; (void)d; (void)e; (void)f2;
    (void)fut.get();
}
"""
# Line 3 is `ioc.run_for` in positive(); nothing else may match.
control_hits = scan(CONTROL)
if control_hits != [3]:
    raise SystemExit(
        "pump-census: error: seeded-positive control FAILED "
        f"(expected [3], got {control_hits}); the scanner cannot be trusted to "
        "report either a zero or a non-zero, so no reading is admissible"
    )

sites = []

for path in files:
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise SystemExit(
            f"pump-census: error: cannot read {path}: {exc}"
        )

    rel = path.relative_to(root).as_posix()
    for line_no in scan(source):
        sites.append(f"{rel}:{line_no}")

for site in sorted(set(sites)):
    print(site)
PY
then
    fail "scanner failed"
fi

# NOTE: no `[ -s "$actual" ]` guard. An empty reading is now a legitimate
# outcome -- the direct population is migrated out -- and the licence to believe
# it comes from the seeded-positive control inside the scanner, not from the
# emptiness of this file. Set equality against the pin still catches drift in
# both directions.

# Emit the authoritative current reading even on mismatch.
cat "$actual"

if ! diff -u "$pin" "$actual" >&2; then
    expected_count="$(wc -l <"$pin" | tr -d '[:space:]')"
    actual_count="$(wc -l <"$actual" | tr -d '[:space:]')"
    echo "pump-census: error: census differs from pin" >&2
    echo "pump-census: expected=$expected_count actual=$actual_count" >&2
    echo "pump-census: update ci/expected-pump-sites.txt in the same change" >&2
    exit 1
fi
