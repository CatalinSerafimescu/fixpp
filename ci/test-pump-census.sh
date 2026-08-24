#!/usr/bin/env bash
# Regression pin for ci/pump-census.sh (#289).
#
# Wired into the `ci-script-pins` job, the only pre-merge signal these scripts
# get: all three tier matrices skip until both gate labels land, so a checker
# exercised only by the matrix is unexercised at review time.
#
# This repo's rule for instruments is that a passing run proves nothing until
# the instrument has been shown able to FAIL, for the stated reason, on a tree
# where the defect is present. Every numbered case below drives a deliberately
# WRONG fixture or a deliberately WRONG pin through the real script and
# requires it to redden for the named reason — not just any reason. Two of
# these are load-bearing in a way the others are not: the added-site and
# removed-site cases prove EXACT SET equality (not a floor, and not a
# ceiling) — a count-only comparison would pass a fixture that lost one site
# and gained a different one, and an upward-only comparison would leave a
# stale pin permanently green after a legitimate downward migration.
#
# Usage:  bash ci/test-pump-census.sh
set -uo pipefail

# ── failure model ───────────────────────────────────────────────────────────
# `setup_fail` aborts immediately: a missing script or unreadable pin makes
# every later assertion meaningless, so continuing would report noise.
#
# `fail` (assertions) RECORDS and CONTINUES. This is load-bearing, not a
# style choice. Gate B round 1 found that a fail-fast harness cannot prove its
# later assertions are independently falsifiable: the production control runs
# first, so mutating (say) block-comment blanking aborted at the production
# check and the block-comment assertion NEVER RAN. Its named diagnostic was
# therefore unreachable, which is exactly the "assertion that cannot fail for
# its own named reason" class this harness exists to catch. Accumulating lets
# every named assertion report in the same run as the mutation that breaks it.
failures=0

setup_fail() {
    echo "FAIL(setup): $*" >&2
    exit 1
}

fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}
# NOTE: fail() does NOT touch $checks. Every assertion is written as
#   [ cond ] || fail "..."
#   pass
# so `pass` runs unconditionally and $checks already counts assertions REACHED,
# pass or fail. Incrementing here too would double-count a failing assertion.

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$here/.." && pwd)"
script="$here/pump-census.sh"
production_pin="$here/expected-pump-sites.txt"

[ -f "$script" ] || setup_fail "missing census script: $script"
[ -s "$production_pin" ] || setup_fail "production pin is missing or empty"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

checks=0
expected_checks=18
pass() { checks=$((checks + 1)); }

run_capture() {
    output="$("$@" 2>&1)"
    status=$?
}

# Runs $script against a tree that contains ONLY a decoy (no genuine
# run_for/get() pair) and requires it to reach the "instrument produced zero
# sites" liveness diagnostic — the pump-census.sh guard that refuses to read
# an empty measurement as a clean tree (see the zero-match-tree case below).
# This is the discriminating assertion for a single blanking class: if that
# class's blanking is broken, the decoy is NOT blanked, the scanner finds a
# real (spurious) site, the zero-sites branch is never reached, and this
# fails for exactly that reason — independent of any other fixture or check.
assert_decoy_alone_yields_zero_sites() {
    local label="$1" decoy_root="$2"
    run_capture bash "$script" --root "$decoy_root" --expected "$baseline_pin"
    [ "$status" -ne 0 ] ||
        fail "$label decoy (alone) was matched as a real site: $output"
    pass

    printf '%s\n' "$output" | grep -Fq 'instrument produced zero sites' ||
        fail "$label decoy (alone) failed for the wrong reason (not blanked correctly): $output"
    pass
}

# ── 1: production control, invoked from a deliberately unrelated cwd ────────
# Proves the script resolves the repo root from its OWN location, not from
# the caller's cwd, and that the production pin the script ships with is
# exactly what the live tree produces. This is ONE falsifiable property, not
# three: pump-census.sh's own internal diff already refuses a nonempty/
# mismatched result before ever exiting 0 (see its `[ -s "$actual" ]` and
# `diff -u "$expected" "$actual"` gates), so "nonempty" and "count matches
# the pin" are IMPLIED by "exit status 0" here, not independently falsifiable
# — a prior revision counted them as three separate assertions that could
# never fail for their own reason (gate-b/r1 finding #1).
#
# ⚠️ THIS CHECK DEPENDS ON ci/expected-pump-sites.txt MATCHING THE LIVE TREE
# AT THE INSTANT THIS RUNS. That is intentional — it is the production gate,
# not a synthetic fixture — but it means this check goes RED (loudly, with
# the exact `path:line` diff) whenever an unrelated concurrent edit shifts a
# line number in a file the census also matches. That is the pin's job
# working correctly, not a defect in this harness: a stale pin must fail
# loudly rather than being silently tolerated. See #289 batch B3 hand-off —
# the pin was generated once from the tree as found and is regenerated by
# the orchestrator after all migration batches land, not by this script.
output="$(cd "$tmp" && bash "$script" 2>&1)"
status=$?
[ "$status" -eq 0 ] ||
    fail "production census failed from a different cwd: $output"
pass

# ── fixture: a known-good site, a comment/string decoy, and a too-far decoy ─
fixture="$tmp/fixture"
mkdir -p "$fixture/tests"

cat >"$fixture/tests/a.cpp" <<'EOF'
void baseline() {
    ioc.run_for(1ms);
    ioc.restart();
    fut.get();
}

/* ioc.run_for(1ms);
   ignored.get(); */
const char* text = "ioc.run_for(1ms); ignored.get();";

void too_far() {
    ioc.run_for(1ms);
    one();
    two();
    three();
    four();
    five();
    six();
    late.get();
}
EOF

baseline_pin="$tmp/baseline.pin"
printf '%s\n' 'tests/a.cpp:2' >"$baseline_pin"

# ── 2-3: known-good fixture produces EXACTLY the expected path:line ─────────
run_capture bash "$script" --root "$fixture" --expected "$baseline_pin"
[ "$status" -eq 0 ] ||
    fail "known-good fixture failed: $output"
pass

[ "$output" = "tests/a.cpp:2" ] ||
    fail "fixture matched the wrong set (comment/string/7-lines-away decoys must NOT match): $output"
pass

# ── 4-7: comments and string literals, each in its OWN fixture invoked ──────
# separately, so a mutation of ONE blanking path reaches THIS assertion,
# naming that class, rather than dying behind check 3's blanket exact-match
# with a generic diagnostic (gate-b/r1 finding #1 — checks 6-7 of a prior
# revision could never independently fail, because check 3 above already
# `fail()`s and exits before they are ever reached on a leak).
comment_decoy="$tmp/comment_decoy"
mkdir -p "$comment_decoy/tests"
cat >"$comment_decoy/tests/decoy.cpp" <<'EOF'
void f() {
    ioc.run_for(1ms);
    /* ioc.run_for(1ms);
       decoy.get(); */
}
EOF
assert_decoy_alone_yields_zero_sites "block-comment" "$comment_decoy"

string_decoy="$tmp/string_decoy"
mkdir -p "$string_decoy/tests"
cat >"$string_decoy/tests/decoy.cpp" <<'EOF'
void f() {
    ioc.run_for(1ms);
    const char* text = "ioc.run_for(1ms); decoy.get();";
}
EOF
assert_decoy_alone_yields_zero_sites "string-literal" "$string_decoy"

# ── 8-9 (gate-b/r1 P2-h): a `\`-continued line comment must stay a comment ──
# across the spliced newline. C++ removes backslash-newline pairs (phase 2)
# BEFORE recognising `//` (phase 3), so `future.get()` on the line after a
# `\`-terminated `//` comment is still inside that comment to the compiler.
# The per-physical-line blanker must splice this the same way, or it reads
# `future.get()` as live code (see ci/pump-census.sh's line-comment state).
continuation_decoy="$tmp/continuation_decoy"
mkdir -p "$continuation_decoy/tests"
cat >"$continuation_decoy/tests/decoy.cpp" <<'EOF'
void f() {
    ioc.run_for(1ms);
    // continued comment \
    decoy.get();
}
EOF
assert_decoy_alone_yields_zero_sites "backslash-continued line-comment" "$continuation_decoy"

# ── add a genuine header site: proves hpp scope and upward drift ────────────
cat >"$fixture/tests/b.hpp" <<'EOF'
void added() { ioc.run_for(1ms);
    added_future.get();
}
EOF

# ── 10-12: an ADDED site makes the census exit nonzero, naming the site ──────
run_capture bash "$script" --root "$fixture" --expected "$baseline_pin"
[ "$status" -ne 0 ] ||
    fail "added .hpp site did not make the census RED"
pass

printf '%s\n' "$output" | grep -Fq \
    'pump-census: error: census differs from pin' ||
    fail "added-site mutant failed without the census-mismatch diagnostic"
pass

printf '%s\n' "$output" | grep -Fq '+tests/b.hpp:1' ||
    fail "added-site mutant did not identify tests/b.hpp:1 — .hpp scope or diff naming broken"
pass

# ── 13-14: a stale (too-high) pin also fails — proves downward ratcheting ───
# (Q3: exact-set equality forces a legitimate migration's pin down, not just
# a floor that only catches upward drift.)
stale_pin="$tmp/stale.pin"
printf '%s\n' \
    'tests/a.cpp:2' \
    'tests/b.hpp:1' >"$stale_pin"

rm "$fixture/tests/b.hpp"

run_capture bash "$script" --root "$fixture" --expected "$stale_pin"
[ "$status" -ne 0 ] ||
    fail "stale high pin passed after a site was removed"
pass

printf '%s\n' "$output" | grep -Fq -- '-tests/b.hpp:1' ||
    fail "downward mutant did not identify the stale pinned site"
pass

# ── 15-16: zero-match tree is an instrument-liveness failure, not "clean" ───
zero="$tmp/zero"
mkdir -p "$zero/tests"
printf '%s\n' 'int no_candidate;' >"$zero/tests/empty.cpp"

run_capture bash "$script" --root "$zero" --expected "$baseline_pin"
[ "$status" -ne 0 ] ||
    fail "zero-match tree passed"
pass

printf '%s\n' "$output" | grep -Fq \
    'instrument produced zero sites' ||
    fail "zero-match failure was not attributed to instrument liveness"
pass

# ── 17-18: missing/wrongly-rooted tests/ fails loudly, not silently-empty ───
run_capture bash "$script" \
    --root "$tmp/does-not-exist" \
    --expected "$baseline_pin"

[ "$status" -ne 0 ] ||
    fail "missing scan root passed"
pass

printf '%s\n' "$output" | grep -Fq 'tests directory not found' ||
    fail "missing-root failure lacked an attributed diagnostic"
pass

# ── self-assert the declared assertion count ────────────────────────────────
# Not itself counted in $checks: a check that increments its own tally after
# reading it would compare against a total that has not yet included itself.
#
# $checks counts assertions REACHED: every call site's trailing `pass` runs
# unconditionally now that fail() no longer aborts, so this cannot be
# satisfied by an early exit — the whole point of the accumulating model.
if [ "$checks" -ne "$expected_checks" ]; then
    echo "FAIL: reached $checks assertions; declared $expected_checks" >&2
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "FAILED: $failures of $checks pump-census assertions" >&2
    exit 1
fi

echo "PASS: $checks pump-census assertions"
