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

tmp="$(mktemp -d)" || setup_fail "mktemp -d failed"
trap 'rm -rf "$tmp"' EXIT

checks=0
expected_checks=28
pass() { checks=$((checks + 1)); }

run_capture() {
    output="$("$@" 2>&1)"
    status=$?
}

# Runs $script against a tree that contains ONLY a decoy (no genuine run_for/get()
# pair) and requires it to read EXACTLY ZERO SITES.
#
# ⚠️ THE REQUIRED OUTCOME INVERTED IN #289 BATCH 15, so read the arms below rather than
# remembering this function. It used to require the run to FAIL with pump-census.sh's
# "instrument produced zero sites" diagnostic -- a blanket refusal to read an empty
# measurement as a clean tree. That refusal was correct for the whole life of the
# migration and is now wrong: the direct population IS empty, so zero is the true answer,
# and the guard was replaced by a seeded-positive control inside the scanner. A
# decoy-only tree therefore now EXITS 0 against a zero-row pin.
#
# This is the discriminating assertion for a single blanking class, and it still is: if
# that class's blanking is broken, the decoy is NOT blanked, the scanner finds a real
# (spurious) site, that site diffs against the zero-row pin, and the run fails for exactly
# that reason -- independent of any other fixture or check.
#
# It is also STRICTLY STRONGER than the form it replaced. The old one proved "the run
# failed, and the reason was zero sites". This proves "the scanner emitted exactly zero
# sites AND its seeded-positive control passed", because the control aborts the run before
# any file is scanned -- so an exit 0 here is unreachable unless the scanner has been
# demonstrated able to report non-zero.
#
# ── HISTORY THAT STILL APPLIES (gate-b/r2 finding F5) ────────────────────────
# The "was matched as a real site" arm used to be pinned against $baseline_pin
# ("tests/a.cpp:2"), a path this decoy tree never contains. That was vacuous BOTH ways:
# correct blanking -> zero sites -> nonzero exit; broken blanking -> a spurious site at
# some tests/decoy.cpp:N -> differs from tests/a.cpp:2 -> also nonzero. Nonzero either
# way, so the arm could never redden for its own stated reason.
#
# Fixed by pinning that arm against $3, the EXACT site(s) this decoy would leak if ITS OWN
# targeted blanking mutation were applied (measured by running the real mutant against the
# real fixture). That fix is untouched by the inversion above: it governs the FIRST arm,
# which still requires a nonzero exit against a leak pin.
assert_decoy_alone_yields_zero_sites() {
    local label="$1" decoy_root="$2" leak_sites="$3"
    local leak_pin
    leak_pin="$(mktemp "$tmp/leak-pin.XXXXXX")" ||
        setup_fail "mktemp for $label leak pin failed"
    printf '%s\n' "$leak_sites" >"$leak_pin" ||
        setup_fail "$label leak pin write failed: $leak_pin"

    run_capture bash "$script" --root "$decoy_root" --expected "$leak_pin"
    [ "$status" -ne 0 ] ||
        fail "$label decoy (alone) was matched as a real site: $output"
    pass

    # ⚠️ THIS ARM'S ORACLE CHANGED IN #289 BATCH 15, AND NOT COSMETICALLY. It used to
    # require the literal 'instrument produced zero sites' -- the census's blanket refusal
    # to emit an empty reading. That refusal was correct for the whole life of the
    # migration and is now wrong: the direct population IS empty, so zero is the true
    # answer and the guard was replaced by a seeded-positive control INSIDE the scanner.
    # Deleting the guard therefore deleted this arm's discriminating oracle, and the
    # replacement must not be a weaker one.
    #
    # It is STRONGER than what it replaces. The old form proved only "the run failed, and
    # the reason was zero sites". This proves "the scanner emitted EXACTLY zero sites AND
    # its seeded-positive control passed" -- because the control aborts the run before any
    # file is scanned, an exit 0 here is unreachable unless the scanner is demonstrably
    # able to report non-zero. Blanking that breaks in the dangerous direction (a decoy
    # matched) yields a row, diffs against the zero-row pin, and exits non-zero.
    run_capture bash "$script" --root "$decoy_root" --expected "$zero_row_pin"
    [ "$status" -eq 0 ] ||
        fail "$label decoy (alone) failed for the wrong reason (not blanked correctly): $output"
    pass
}

# ── 1: production control, invoked from a deliberately unrelated cwd ────────
# Proves the script resolves the repo root from its OWN location, not from
# the caller's cwd, and that the production pin the script ships with is
# exactly what the live tree produces. This is ONE falsifiable property, not
# three: pump-census.sh's own internal diff already refuses a mismatched result
# before ever exiting 0 (see its `diff -u "$pin" "$actual"` gate), so "the reading
# matches the pin" is IMPLIED by "exit status 0" here, not independently falsifiable
# ⚠️ THE `[ -s "$actual" ]` GATE THIS USED TO CITE NO LONGER EXISTS -- #289 batch 15
# removed it, because a zero-row reading became legitimate. "Nonempty" is therefore
# no longer implied by exit 0 and is no longer claimed here
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
mkdir -p "$fixture/tests" || setup_fail "mkdir failed: $fixture/tests"

cat >"$fixture/tests/a.cpp" <<'EOF' || setup_fail "fixture write failed: $fixture/tests/a.cpp"
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
printf '%s\n' 'tests/a.cpp:2' >"$baseline_pin" ||
    setup_fail "baseline pin write failed: $baseline_pin"

# A pin with ZERO SITE ROWS but a non-empty FILE. Both halves are load-bearing: the
# census accepts zero rows (the production pin has none) and REJECTS a zero-byte file
# (that is a truncation, not a clean tree), so a fixture pin must carry a header to be
# a valid empty pin at all.
zero_row_pin="$tmp/zero-row.pin"
printf '%s\n' '# intentionally no site rows' >"$zero_row_pin" ||
    setup_fail "zero-row pin write failed: $zero_row_pin"

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
mkdir -p "$comment_decoy/tests" || setup_fail "mkdir failed: $comment_decoy/tests"
cat >"$comment_decoy/tests/decoy.cpp" <<'EOF' || setup_fail "fixture write failed: $comment_decoy/tests/decoy.cpp"
void f() {
    ioc.run_for(1ms);
    /* ioc.run_for(1ms);
       decoy.get(); */
}
EOF
# Leak pin measured by running the real block-comment mutation (disabling
# `elif source.startswith("/*", i):`) against this exact fixture: BOTH the
# outer real run_for (line 2, whose "following" window now sees line 4
# unblanked) and the comment-embedded fake run_for (line 3, same window)
# register — an outer real call plus a still-blanked decoy is already
# covered by checks 2-3 above, so this fixture is kept as-is rather than
# reshaped to leak a single line.
assert_decoy_alone_yields_zero_sites "block-comment" "$comment_decoy" \
    "$(printf 'tests/decoy.cpp:2\ntests/decoy.cpp:3')"

string_decoy="$tmp/string_decoy"
mkdir -p "$string_decoy/tests" || setup_fail "mkdir failed: $string_decoy/tests"
cat >"$string_decoy/tests/decoy.cpp" <<'EOF' || setup_fail "fixture write failed: $string_decoy/tests/decoy.cpp"
void f() {
    ioc.run_for(1ms);
    const char* text = "ioc.run_for(1ms); decoy.get();";
}
EOF
# Leak pin measured against the real quote-open mutation (disabling
# `elif source[i] in ('"', "'"):`): the string is never entered, so line 3
# reads as code; only the outer line-2 run_for finds a get() in its window.
assert_decoy_alone_yields_zero_sites "string-literal" "$string_decoy" \
    'tests/decoy.cpp:2'

# ── 8-9 (gate-b/r1 P2-h): a `\`-continued line comment must stay a comment ──
# across the spliced newline. C++ removes backslash-newline pairs (phase 2)
# BEFORE recognising `//` (phase 3), so `future.get()` on the line after a
# `\`-terminated `//` comment is still inside that comment to the compiler.
# The per-physical-line blanker must splice this the same way, or it reads
# `future.get()` as live code (see ci/pump-census.sh's line-comment state).
continuation_decoy="$tmp/continuation_decoy"
mkdir -p "$continuation_decoy/tests" || setup_fail "mkdir failed: $continuation_decoy/tests"
cat >"$continuation_decoy/tests/decoy.cpp" <<'EOF' || setup_fail "fixture write failed: $continuation_decoy/tests/decoy.cpp"
void f() {
    ioc.run_for(1ms);
    // continued comment \
    decoy.get();
}
EOF
# Leak pin measured against the real backslash-splice mutation (disabling
# the `ch == "\\" and source[i+1] == "\n"` check in the line-comment state):
# the newline ends the comment early, so "decoy.get();" on the next physical
# line reads as code, within the outer line-2 run_for's window.
assert_decoy_alone_yields_zero_sites "backslash-continued line-comment" "$continuation_decoy" \
    'tests/decoy.cpp:2'

# ── gate-b/r2 F4: two more decision-bearing lexer branches had NOTHING that
# could fail on them — proven by mutation: disabling either left all 18
# assertions green (see the gate-b/r2 F4 report for the exact mutants).
# Each decoy below is isolated to ONE branch and its leak pin is measured
# against that branch's own real mutant, following the same pattern as 4-9.

# ── raw string, CUSTOM delimiter, embedding a literal `"` in its body ───────
# `raw:` recognises `(?:u8|u|U|L)?R"delim(...)delim"`. With that branch
# disabled, `R"tag(` is read as plain code: `R` is an ordinary character and
# the following `"` opens an ORDINARY string, which then closes at the
# first literal `"` it finds — the one embedded in "before \" ..." — leaving
# the rest of the (fake) raw-string body, including the decoy run_for/get(),
# as live code. Correct raw-string blanking treats the embedded `"` as
# ordinary content and only closes at the real `)tag"` end token, so nothing
# leaks.
raw_custom_delim_decoy="$tmp/raw_custom_delim_decoy"
mkdir -p "$raw_custom_delim_decoy/tests" || setup_fail "mkdir failed: $raw_custom_delim_decoy/tests"
cat >"$raw_custom_delim_decoy/tests/decoy.cpp" <<'EOF' || setup_fail "fixture write failed: $raw_custom_delim_decoy/tests/decoy.cpp"
void f() {
    const char* text = R"tag(before " ioc.run_for(1ms);
       decoy.get();
    )tag";
}
EOF
assert_decoy_alone_yields_zero_sites "raw-string (custom delimiter)" "$raw_custom_delim_decoy" \
    'tests/decoy.cpp:2'

# ── raw string, PREFIXED (u8R), same embedded-quote mechanism ───────────────
# Proves the optional `(?:u8|u|U|L)?` prefix group is exercised, not just
# the bare `R"..."` form — same targeted mutation as above.
raw_prefixed_decoy="$tmp/raw_prefixed_decoy"
mkdir -p "$raw_prefixed_decoy/tests" || setup_fail "mkdir failed: $raw_prefixed_decoy/tests"
cat >"$raw_prefixed_decoy/tests/decoy.cpp" <<'EOF' || setup_fail "fixture write failed: $raw_prefixed_decoy/tests/decoy.cpp"
void f() {
    const char* text = u8R"tag(before " ioc.run_for(1ms);
       decoy.get();
    )tag";
}
EOF
assert_decoy_alone_yields_zero_sites "raw-string (u8R prefix)" "$raw_prefixed_decoy" \
    'tests/decoy.cpp:2'

# ── ordinary string with an escaped backslash AND an escaped quote ──────────
# The literal-state escape branch (`if ch == "\\" and i < n:`) is what lets
# `\"` inside a normal string continue the literal instead of closing it.
# With that branch disabled, the backslash before the embedded `\"` is
# blanked alone (no lookahead), so the very next character — the escaped
# quote — is read as the REAL closing quote, ending the string early and
# exposing the rest (including the decoy run_for/get(), spliced onto the
# next physical line via a legitimate backslash-newline string
# continuation) as live code.
escaped_string_decoy="$tmp/escaped_string_decoy"
mkdir -p "$escaped_string_decoy/tests" || setup_fail "mkdir failed: $escaped_string_decoy/tests"
cat >"$escaped_string_decoy/tests/decoy.cpp" <<'EOF' || setup_fail "fixture write failed: $escaped_string_decoy/tests/decoy.cpp"
void f() {
    const char* s = "a\\b\" ioc.run_for(1ms); \
trailing.get()";
}
EOF
assert_decoy_alone_yields_zero_sites "escaped quote/backslash in string" "$escaped_string_decoy" \
    'tests/decoy.cpp:2'

# ── character literal containing a quote, incl. an escaped delimiter ────────
# `char q = '"';` alone needs no escape handling (the stored `quote`
# variable already distinguishes `'` from `"`); it is kept here for realism
# per the fixture's own decoy shape. The escaped-`'` literal on the next
# line is what exercises the SAME escape branch as the string decoy above,
# with `'` as the delimiter instead of `"`.
char_quote_decoy="$tmp/char_quote_decoy"
mkdir -p "$char_quote_decoy/tests" || setup_fail "mkdir failed: $char_quote_decoy/tests"
cat >"$char_quote_decoy/tests/decoy.cpp" <<'EOF' || setup_fail "fixture write failed: $char_quote_decoy/tests/decoy.cpp"
void f() {
    char q = '"';
    char c = 'a\\b\' ioc.run_for(1ms); \
trailing.get()';
}
EOF
assert_decoy_alone_yields_zero_sites "character literal with quote" "$char_quote_decoy" \
    'tests/decoy.cpp:3'

# ── add a genuine header site: proves hpp scope and upward drift ────────────
cat >"$fixture/tests/b.hpp" <<'EOF' || setup_fail "fixture write failed: $fixture/tests/b.hpp"
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
    'tests/b.hpp:1' >"$stale_pin" ||
    setup_fail "stale pin write failed: $stale_pin"

rm "$fixture/tests/b.hpp" || setup_fail "fixture cleanup failed: $fixture/tests/b.hpp"

run_capture bash "$script" --root "$fixture" --expected "$stale_pin"
[ "$status" -ne 0 ] ||
    fail "stale high pin passed after a site was removed"
pass

printf '%s\n' "$output" | grep -Fq -- '-tests/b.hpp:1' ||
    fail "downward mutant did not identify the stale pinned site"
pass

# ── 15-16: zero-match tree is an instrument-liveness failure, not "clean" ───
zero="$tmp/zero"
mkdir -p "$zero/tests" || setup_fail "mkdir failed: $zero/tests"
printf '%s\n' 'int no_candidate;' >"$zero/tests/empty.cpp" ||
    setup_fail "zero fixture write failed: $zero/tests/empty.cpp"

run_capture bash "$script" --root "$zero" --expected "$baseline_pin"
[ "$status" -ne 0 ] ||
    fail "zero-match tree passed"
pass

printf '%s\n' "$output" | grep -Fq -- '-tests/a.cpp:2' ||
    fail "zero-match failure was not attributed to the site that went missing: $output"
pass

# ── 17: a zero-match tree against a ZERO-ROW pin is now a legitimate PASS ────
# The counterpart to 15-16, and the assertion that would have caught this batch's
# own regression: emptiness is no longer a failure mode, it is a state the pin can
# express. If a future change reinstates a blanket "zero sites is an error" guard,
# 15-16 keep passing and only THIS goes red.
run_capture bash "$script" --root "$zero" --expected "$zero_row_pin"
[ "$status" -eq 0 ] ||
    fail "a zero-match tree against a zero-row pin must PASS: $output"
pass

# ── 18: the seeded-positive control is LIVE -- break the scanner, it must fire ─
# Without this, every assertion above that now rests on "exit 0 implies the control
# passed" would rest on a control nobody proved could fail. Mutates a COPY; the
# repo script is untouched.
mutant="$tmp/mutant-census.sh"
sed 's/run_re = re\.compile(r"\\.run_for\\s\*\\(")/run_re = re.compile(r"\\.NEVER_MATCHES\\s*\\(")/' \
    "$script" >"$mutant" || setup_fail "mutant write failed"
grep -Fq 'NEVER_MATCHES' "$mutant" ||
    setup_fail "control-liveness mutation did not apply -- the arm would be vacuous"
run_capture bash "$mutant" --root "$fixture" --expected "$baseline_pin"
printf '%s\n' "$output" | grep -Fq 'seeded-positive control FAILED' ||
    fail "a scanner that cannot match anything did not trip its own control: $output"
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
