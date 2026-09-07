#!/usr/bin/env bash
# ci/red-arms/batch20-coroutine-axis.sh -- can the CALL-SITE-SCOPE axis report non-zero
# on REAL code?
#
# ⚠️ THIS ARM EXISTS BECAUSE BATCH 20's HEADLINE IS A ZERO. `ci/pump-get-sweep.sh
# --disposition` now reports `CORO` unguarded = 0, and this repo's single most recurring
# defect is an instrument that reports clean because it *could not* report anything else
# [[feedback_every_broken_instrument_in_this_repo_fails_toward_clean]]. The sweep's own
# SCOPE_CASES already prove the axis reports CORO -- but on SYNTHETIC fixtures, which a
# broken traversal, a wrong root or a rotted receiver pattern all survive.
#
# THE CONTROL. Run the CURRENT sweep against a corpus where the answer is known and
# non-zero: `tests/` at the commit before batch 19, where the same eleven sites were
# unguarded. Same instrument, older tree. If it cannot see them there, the zero it prints
# on today's tree is worth nothing.
#
# ⚠️ THE INSTRUMENT IS TODAY'S AND THE CORPUS IS HISTORICAL, AND THAT PAIRING IS THE
# POINT. Checking out the whole old tree would run the OLD sweep, which had neither axis
# -- it would measure nothing and pass by construction. So only `tests/` is extracted;
# `ci/` is copied from the working tree.
#
# ⚠️ A PINNED COMMIT IS NOT A ROTTING CITATION. A git object is immutable, so `BASE`
# names a fixed corpus rather than a claim about the current tree -- unlike a line number
# or a count, it cannot go stale [[project_a_comment_records_a_procedure_never_a_result]].
# What it CAN do is become unreachable in a shallow clone; the arm detects that and says
# so rather than reporting a zero.
#
# Usage:  ci/red-arms/batch20-coroutine-axis.sh
set -uo pipefail

cd "$(dirname "$0")/../.." || exit 2

# The merge-base of batch 19 -- i.e. the last commit at which the eleven coroutine-side
# sites in tests/sync were still unguarded.
BASE="${BASE:-ee252dd554eb303c98bda8fd57aeedf3e54861f4}"

if ! git cat-file -e "$BASE^{commit}" 2>/dev/null; then
    echo "!! $BASE is not present locally (shallow clone?). This arm cannot run; that is" >&2
    echo "   a MISSING measurement, not a passing one." >&2
    exit 2
fi

TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT

git archive "$BASE" tests | tar -x -C "$TMP" || { echo "!! could not extract tests/ at $BASE" >&2; exit 2; }
cp -r ci "$TMP/ci" || exit 2

# ⚠️ COUNT THE PER-ROW TAGS, NOT THE SUMMARY BLOCK. The first version of this parsed the
# human-readable "=== CALL-SITE SCOPE ===" table with a four-rule awk state machine keyed
# on exact prose, including a fallback that read a blank line as "zero". Any rewording of
# that section would have made it print nothing, `${now:-0}` would have supplied a 0, and
# ARM 0 -- the arm asserting a zero -- would have PASSED on a parse failure. That is this
# repo's signature defect placed inside the arm written to prevent it. Each unguarded row
# already carries a machine-stable `x CORO]` / `x CALLER-SIDE]` tag, which is what ARM 1
# greps two calls below; `grep -c` prints 0 rather than nothing when there is no match.
coro_unguarded() {  # $1 = root
    bash "$1/ci/pump-get-sweep.sh" --root "$1" --disposition --quiet 2>/dev/null \
        | grep -c 'x CORO\]'
}

fails=0

echo "== ARM 0 -- the current tree: expect ZERO coroutine-side candidates =="
now=$(coro_unguarded "$PWD"); now=${now:-0}
if [ "$now" -eq 0 ]; then
    echo "  ok    CORO unguarded = 0 on the current tree."
else
    echo "  !!BAD CORO unguarded = $now on the current tree -- batch 20 claims 0."
    fails=$((fails+1))
fi

echo
echo "== ARM 1 -- the SAME sweep on the pre-batch-19 corpus: expect NON-ZERO =="
echo "   (this is the arm that makes ARM 0's zero mean something)"
# One sweep, parsed twice. The first draft ran it again just to print the sample rows
# the first run had already produced and thrown away -- ~2.4 s for output it had in hand.
OUT=$(bash "$TMP/ci/pump-get-sweep.sh" --root "$TMP" --disposition --quiet 2>/dev/null)
was=$(printf '%s\n' "$OUT" | grep -c 'x CORO\]')
if [ "$was" -gt 0 ]; then
    echo "  ok    CORO unguarded = $was at $BASE -- the axis reports non-zero on real code."
    printf '%s\n' "$OUT" | grep -F 'x CORO]' | sed 's/^/        /' | head -20
else
    echo "  !!BAD CORO unguarded = 0 at $BASE too. The axis cannot report non-zero on a"
    echo "        corpus where the sites are known to be unguarded, so ARM 0's zero is"
    echo "        a property of the instrument, not of the tree."
    fails=$((fails+1))
fi

echo
if [ "$fails" -ne 0 ]; then
    echo "=== FAILED: $fails of 2 arms" >&2; exit 1
fi
echo "=== PASS: 2/2 -- zero today, non-zero where the defect was. The axis discriminates."
