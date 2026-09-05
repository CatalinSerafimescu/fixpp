#!/usr/bin/env bash
# ci/pump-arm-common.sh — the two non-vacuity guards shared by the #289 forced-miss
# drivers (ci/pump-red-arm.sh, ci/pump-seam-arm.sh).
#
# SOURCE this; do not execute it (except with --self-test, below).
#
# ⚠️ WHY THIS FILE EXISTS AT ONLY TWO COPIES. Both guards defend against this repo's
# most recurring defect — a verification step that reports success because it could not
# have reported anything else — and the second of them is a NAMED standing rule
# ([[feedback_a_verification_sweep_must_assert_an_execution_count]]). They were written
# twice, independently, in the very batch that cites that rule. The repo has already paid
# for letting one shape exist in N places:
# [[feedback_the_nth_copy_lacks_the_witnesses_its_siblings_have]] records four defects of
# a single shape in PR #321, none of them found by reading. Two is where the third copy is
# cheap to prevent; it is not where the drift becomes visible.
#
# ⚠️ THE RATIONALE IS THE PART THAT MUST NOT BE DUPLICATED, not the four lines of `[ ]`.
# A future reader fixing one copy's wording has no way to learn the other exists. Keeping
# one home for the *reason* is the whole return on this extraction — the comparison itself
# is trivial and always was.

# Refuse to proceed on an empty population.
#
# Without this, a comment-only or empty input file builds nothing, runs nothing, prints a
# zero-of-zero summary and exits 0 — a verification step that cannot fail because it never
# ran.
#
#   $1 count   how many items the caller parsed
#   $2 file    the input file, for the message
#   $3 tool    the calling script's name, for the message
#   $4 noun    what the items are ("arms", "labels")
assert_nonempty_population() {
    local count="$1" file="$2" tool="$3" noun="$4"
    if [ "$count" -eq 0 ]; then
        printf '%s: %s contains no %s -- refusing to report success on an empty population\n' \
            "$tool" "$file" "$noun" >&2
        exit 2
    fi
}

# Refuse to report unless the number of items EXECUTED equals the number PARSED.
#
# ⚠️ THIS IS NOT THE SAME CHECK AS THE ONE ABOVE, AND THE ABOVE DOES NOT IMPLY IT.
# The drivers parse their input with more than one reader — an `awk`/`grep` count and a
# `read`/array loop — and those disagree on a final row with NO TRAILING NEWLINE: the
# counter counts it, `read` returns non-zero so the loop body never runs. A one-row file
# without a final newline therefore SATISFIES the non-vacuity guard, runs nothing, and
# exits 0 — defeating that guard on its own terms. Patching the reader closes the
# instance; counting what actually executed closes the class.
#
# ⚠️ THE CALLER DERIVES `ran`, AND THE TWO DRIVERS DERIVE IT DIFFERENTLY ON PURPOSE.
# ci/pump-seam-arm.sh increments a counter at the top of its loop (direct).
# ci/pump-red-arm.sh derives it from OUTCOMES (`pass + ${#NOTES[@]}`), which additionally
# catches an arm that ran but recorded no verdict at all. Neither is a simplification of
# the other, so the derivation stays at the call site and only the assertion is shared.
#
#   $1 ran       how many items actually executed
#   $2 expected  how many the caller parsed
#   $3 tool      the calling script's name, for the message
#   $4 noun      what the items are ("arm(s)", "label(s)")
assert_ran_count() {
    local ran="$1" expected="$2" tool="$3" noun="$4"
    if [ "$ran" -ne "$expected" ]; then
        echo "$tool: parsed ${expected} ${noun} but EXECUTED ${ran} -- refusing to report" >&2
        echo "  (a row with no trailing newline is the usual cause: the parsers disagree)" >&2
        exit 3
    fi
}

# ── Self-test ────────────────────────────────────────────────────────────────
# ⚠️ A GUARD THAT HAS NEVER BEEN SEEN TO FIRE IS NOT A GUARD. Both arms below are
# required: the FIRE arm proves the refusal path is reachable and exits with the documented
# code, the QUIET arm proves the guard does not fire on a healthy population — without
# which a guard that always refused would also "pass" its fire test.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    [ "${1-}" = "--self-test" ] || {
        echo "ci/pump-arm-common.sh is meant to be SOURCED; run with --self-test to check it" >&2
        exit 64
    }
    ok=0 bad=0
    check() {  # $1 = description, $2 = expected exit, shift 2 = command
        local desc="$1" want="$2"; shift 2
        local got=0
        ( "$@" ) >/dev/null 2>&1 || got=$?
        if [ "$got" -eq "$want" ]; then printf '  ok    %-52s exit=%s\n' "$desc" "$got"; ok=$((ok+1))
        else printf '  !!BAD %-52s exit=%s want=%s\n' "$desc" "$got" "$want"; bad=$((bad+1)); fi
    }
    check "empty population FIRES (exit 2)"        2 assert_nonempty_population 0 f.tsv tool arms
    check "non-empty population is QUIET (exit 0)" 0 assert_nonempty_population 3 f.tsv tool arms
    check "short run FIRES (exit 3)"               3 assert_ran_count 2 3 tool "arm(s)"
    check "over-run FIRES too (exit 3)"            3 assert_ran_count 4 3 tool "arm(s)"
    check "exact run is QUIET (exit 0)"            0 assert_ran_count 3 3 tool "arm(s)"
    echo "pump-arm-common self-test: ${ok} ok, ${bad} bad"
    [ "$bad" -eq 0 ]
fi
