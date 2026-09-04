#!/usr/bin/env bash
# Regression harness for the #300 install bound:
#   ci/apt-guard.sh — the timeout + retry + attribution wrapper
#
# Run by the `ci-script-pins` job in tier1.yml, and locally with:
#   ci/test-apt-guard.sh
#
# ── WHY THIS FILE EXISTS AT ALL ──────────────────────────────────────────────
#
# #300 is a bug report about an install step that hangs and burns a 240-minute
# job timeout while nothing goes red. The fix is a bound. A bound that has
# never been seen to FIRE is not known to be able to fire — this repo's single
# most recurring defect is an instrument that reports clean because it could
# not report anything else. So every claim apt-guard.sh makes is pinned here by
# a case shown to FAIL when the claim is false, not merely by a case that passes.
#
# ⚠️ THE DISCRIMINATIONS, not just the assertions. Three cells exist because
# the OBVIOUS wrong implementation passes a naive test:
#
#   * A wrapper that runs `timeout` WITHOUT `--kill-after` bounds a cooperative
#     command and is decorative against the real one. Every caller invokes apt
#     through `sudo`, and sudo does not forward SIGTERM to its child. T3 uses a
#     fixture that IGNORES SIGTERM — the sudo-shaped case — and pins that it is
#     still killed. T2 alone passes against the broken form.
#   * A retry wrapped AROUND an unbounded command multiplies the hang instead of
#     bounding it, which is the issue's own load-bearing warning. T5 pins the
#     TOTAL elapsed time against attempts x budget, so a retry that skips the
#     timeout is caught by the clock rather than by reading the code.
#   * Exit 124 (mirror wedged) and the command's own code (package missing) need
#     different responses from whoever reads the failure. T6 pins that they stay
#     distinguishable; a wrapper that normalises everything to 1 passes T2.
#
# The MUTANT section at the bottom closes the loop: every cell named above is
# re-run against a copy of apt-guard.sh that breaks exactly the property it
# claims to pin, and the harness must go RED at that cell.
#
# ⚠️ IF A MUTANT REPORTS `the mutation did NOT apply`, THE FIX IS TO RE-POINT THE
# PATTERN, not to delete the mutant. The mutations are `sed` expressions matching
# exact lines of the script, indentation included, so any edit to a targeted line
# breaks its pattern. A silently non-applying mutation would read as a thorough
# harness that tested nothing.
#
# No apt, no network and no sudo are required: every fixture is a synthetic
# command, because `ci-script-pins` runs on a bare runner in seconds.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Overridable so the MUTANT section can re-invoke this same file against
# deliberately broken copies. Default is the shipped script.
GUARD="${APT_GUARD_SCRIPT:-${HERE}/apt-guard.sh}"
REPO_ROOT="$(cd "$HERE/.." && pwd)"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
check(){ if [ "$2" = "$3" ]; then ok "$1 ($2)"; else bad "$1 — expected '$3', got '$2'"; fi; }

# ── Cell filter — a COST fix, not a style preference ────────────────────────
#
# Each mutant re-invokes this file against a broken copy. Replaying ALL cells
# was measured at ~120 s for the suite, because two of the three mutants DISABLE
# THE BOUND — so cells that normally finish in 2-5 s fall through to their
# fixtures' full sleep instead. That roughly DOUBLED the `ci-script-pins` job's
# documented ~117 s, for replays whose results are discarded: mutant() greps the
# log for its OWN named cell and ignores everything else.
#
# So a mutant runs exactly the cell it claims to pin. That is also the more
# honest scoring: a mutant asserting "T3 goes RED" should be judged on T3, not
# on whatever else its broken copy happens to disturb.
run_cell() { [ -z "${APT_GUARD_ONLY:-}" ] || [ "${APT_GUARD_ONLY}" = "$1" ]; }

# Wall-clock of a command, in whole seconds. The bound is a TIME claim, so two
# cells below can only be pinned by the clock.
elapsed() {
    local t0 t1
    t0=$(date +%s)
    "$@" >/dev/null 2>&1 || true
    t1=$(date +%s)
    echo $(( t1 - t0 ))
}

# ── Fixtures ─────────────────────────────────────────────────────────────────

# Ignores SIGTERM, exactly as a hung `sudo apt-get` does from the wrapper's
# point of view. Only SIGKILL ends it. This is the fixture that discriminates
# --kill-after from a bare timeout.
cat > "$WORK/deaf.sh" <<'SH'
#!/usr/bin/env bash
trap '' TERM INT
sleep 15
SH

# Cooperative hang: dies on SIGTERM. A bare `timeout` handles this one, which is
# precisely why it cannot be the only hang fixture.
cat > "$WORK/hang.sh" <<'SH'
#!/usr/bin/env bash
sleep 15
SH

# Fails a recorded number of times, then succeeds — pins that the retry is real
# and that it stops once the command works.
cat > "$WORK/flaky.sh" <<'SH'
#!/usr/bin/env bash
# $1 = state file, $2 = how many attempts must fail before success
n=0
[ -f "$1" ] && n=$(cat "$1")
n=$(( n + 1 ))
echo "$n" > "$1"
[ "$n" -gt "$2" ] && exit 0
exit 7
SH

# Fails every time with a distinctive non-124 code — pins exit-code fidelity.
cat > "$WORK/nope.sh" <<'SH'
#!/usr/bin/env bash
exit 42
SH

# Attempt 1 hangs; every later attempt fails fast with 42. The ONLY fixture that
# mixes outcomes ACROSS attempts — every other cell runs with ATTEMPTS=1, which
# is why a sticky timeout flag went undetected until a review demonstrated it.
cat > "$WORK/mixed.sh" <<'SH'
#!/usr/bin/env bash
n=0; [ -f "$1" ] && n=$(cat "$1"); n=$(( n + 1 )); echo "$n" > "$1"
[ "$n" -eq 1 ] && sleep 30
exit 42
SH

# Exits 124 IMMEDIATELY and by its own choice — 124 is `timeout`'s code, so a
# wrapper that reads the exit code alone would call this a wedged mirror.
cat > "$WORK/self124.sh" <<'SH'
#!/usr/bin/env bash
exit 124
SH

chmod +x "$WORK"/*.sh

echo "== apt-guard.sh =="

# ── T1: the happy path is transparent ────────────────────────────────────────
if run_cell T1; then
    rc=0; "$GUARD" t1 -- true >/dev/null 2>&1 || rc=$?
    check "T1 a succeeding command exits 0" "$rc" "0"
fi

# ── T2: a hang is BOUNDED — the load-bearing half of #300 ────────────────────
#
# Without this the step runs to the 240-minute job timeout. The fixture sleeps
# 15s; the budget is 2s. If the wrapper does not bound, this cell hangs the
# harness rather than failing it, which is itself the signal.
if run_cell T2; then
    rc=0
    APT_GUARD_TIMEOUT=2 APT_GUARD_ATTEMPTS=1 "$GUARD" t2 -- "$WORK/hang.sh" >/dev/null 2>&1 || rc=$?
    check "T2 a hanging command is bounded and reports timeout" "$rc" "124"
fi

# ── T3: THE discrimination — a SIGTERM-deaf command is still killed ──────────
#
# Every real caller goes through `sudo`, which does not forward SIGTERM. A
# wrapper using bare `timeout` bounds hang.sh (T2 passes) and leaves deaf.sh
# running for the fixture's full sleep. The clock is the only honest instrument
# here — the bound is a TIME claim and cannot be read off the exit code.
if run_cell T3; then
    sec=$(APT_GUARD_TIMEOUT=2 APT_GUARD_KILL_AFTER=3 APT_GUARD_ATTEMPTS=1 elapsed "$GUARD" t3 -- "$WORK/deaf.sh")
    if [ "$sec" -lt 10 ]; then
        ok "T3 a SIGTERM-deaf command is force-killed (${sec}s, budget 2s + 3s kill grace)"
    else
        bad "T3 a SIGTERM-deaf command outlived its bound (${sec}s) — --kill-after is missing or ineffective"
    fi

    # ⚠️ ELAPSED TIME ALONE WAS NOT ENOUGH, and this cell shipped that way. It
    # proved the kill HAPPENED and never that it was REPORTED correctly — a
    # forced-MISS arm that cannot catch a spurious HIT. GNU timeout returns 124
    # when SIGTERM sufficed but 137 when --kill-after escalated, so the one path
    # this flag exists for was being logged as an ordinary command failure
    # instead of a wedged mirror, defeating #300's attribution half. The status
    # and the diagnostic are now asserted alongside the clock.
    rc=0
    out=$(APT_GUARD_TIMEOUT=2 APT_GUARD_KILL_AFTER=1 APT_GUARD_ATTEMPTS=1 \
            "$GUARD" t3b -- "$WORK/deaf.sh" 2>&1) || rc=$?
    check "T3 a SIGTERM-deaf timeout still reports the documented 124" "$rc" "124"
    if echo "$out" | grep -qi 'wedged or degraded apt mirror'; then
        ok "T3 a SIGTERM-deaf timeout is attributed to the MIRROR, not to the package set"
    else
        bad "T3 a SIGTERM-deaf timeout was not attributed to the mirror — it reads as an ordinary command failure, which is the attribution defect #300 is about"
    fi

    # The other direction: a command that exits 124 BY ITSELF, fast, is not a
    # timeout. Without the elapsed check, keying on the exit code alone would
    # label it a wedged mirror and send the next reader down the wrong path.
    rc=0
    out=$(APT_GUARD_TIMEOUT=30 APT_GUARD_ATTEMPTS=1 "$GUARD" t3c -- "$WORK/self124.sh" 2>&1) || rc=$?
    check "T3 a command exiting 124 on its own keeps its code" "$rc" "124"
    if echo "$out" | grep -qi 'wedged or degraded apt mirror'; then
        bad "T3 a fast, self-inflicted 124 was mislabelled as a wedged mirror — the watchdog flag is being inferred from the exit code"
    else
        ok "T3 a fast, self-inflicted 124 is NOT mislabelled as a mirror"
    fi
fi

# ── T4: the retry is real, and it stops on success ───────────────────────────
if run_cell T4; then
    rc=0
    APT_GUARD_BACKOFF=0 APT_GUARD_ATTEMPTS=3 \
        "$GUARD" t4 -- "$WORK/flaky.sh" "$WORK/t4.state" 2 >/dev/null 2>&1 || rc=$?
    check "T4 a command failing twice then succeeding exits 0" "$rc" "0"
    check "T4 it stopped at the first success (3 invocations)" "$(cat "$WORK/t4.state")" "3"
fi

# ── T5: the retry does NOT multiply the hang ─────────────────────────────────
#
# The issue's explicit warning: "a retry without a timeout multiplies the hang
# rather than bounding it." Pinned on the clock, not by reading the code. Three
# attempts at a 2s budget must land near 6s (+ kill grace), NOT near 3 x 15s.
if run_cell T5; then
    sec=$(APT_GUARD_TIMEOUT=2 APT_GUARD_ATTEMPTS=3 APT_GUARD_BACKOFF=0 \
            elapsed "$GUARD" t5 -- "$WORK/hang.sh")
    if [ "$sec" -lt 20 ]; then
        ok "T5 every attempt is bounded, so retries stay bounded (${sec}s for 3 x 2s)"
    else
        bad "T5 total elapsed ${sec}s — the retry is wrapped around an UNBOUNDED command"
    fi
fi

# ── T6: a timeout and an ordinary failure stay distinguishable ───────────────
#
# 124 means the mirror; 42 means the package set. A wrapper that normalises
# both to 1 still passes T2, and sends the next reader down the wrong path.
if run_cell T6; then
    rc=0
    APT_GUARD_ATTEMPTS=1 "$GUARD" t6 -- "$WORK/nope.sh" >/dev/null 2>&1 || rc=$?
    check "T6 an ordinary failure preserves the command's own exit code" "$rc" "42"
fi

# ── T7: attribution — the failure names apt, not "the build" ─────────────────
#
# The other half of #300: the hang currently reads as a build failure because
# nothing in the log says apt. Proven NON-EMPTY here so a later grep returning
# 0 is known to mean absence rather than a broken pattern.
if run_cell T7; then
    out=$(APT_GUARD_TIMEOUT=2 APT_GUARD_ATTEMPTS=1 "$GUARD" my-label -- "$WORK/hang.sh" 2>&1 || true)
    if echo "$out" | grep -q '::error::apt-guard \[my-label\] FAILED'; then
        ok "T7 the failure is attributed to apt-guard and names the label"
    else
        bad "T7 no attributed ::error:: line — the failure would read as a build failure"
    fi
    if echo "$out" | grep -qi 'wedged or degraded apt mirror'; then
        ok "T7b the timeout case says MIRROR rather than a generic failure"
    else
        bad "T7b the timeout case does not name the mirror"
    fi
fi

# ── T8: misuse is loud ───────────────────────────────────────────────────────
if run_cell T8; then
    rc=0; "$GUARD" onlylabel >/dev/null 2>&1 || rc=$?
    check "T8 too few arguments exits 2" "$rc" "2"
    rc=0; "$GUARD" label notdashdash true >/dev/null 2>&1 || rc=$?
    check "T8b a missing '--' separator exits 2" "$rc" "2"
fi

# ── T9: THE DEPLOYED DEFAULT BUDGET, which no other cell exercises ──────────
#
# Every timing cell above sets APT_GUARD_TIMEOUT explicitly, and NO workflow call
# site sets it at all (`grep -rn APT_GUARD_TIMEOUT .github/workflows/` is empty).
# So the `case "$LABEL"` dispatch — the value all 18 production sites actually
# run with — was pinned by nothing: a review set both defaults to 24 hours,
# restoring #300's unbounded hang at every real call site, and this harness
# stayed 19/19 green.
#
# ⚠️ An opt-in written against the EASY state is not evidence about its adopters.
# The banner already prints the budget, so this asserts the deployed value.
if run_cell T9; then
    out=$(APT_GUARD_ATTEMPTS=1 "$GUARD" llvm-toolchain -- true 2>&1)
    if echo "$out" | grep -q '(bound 900s)'; then
        ok "T9 llvm-toolchain keeps its wide default budget"
    else
        bad "T9 llvm-toolchain's default budget changed — a full toolchain install needs the wide one, and no call site passes it explicitly"
    fi
    out=$(APT_GUARD_ATTEMPTS=1 "$GUARD" apt-install -- true 2>&1)
    if echo "$out" | grep -q '(bound 300s)'; then
        ok "T9 an ordinary install keeps the tight default budget"
    else
        bad "T9 the ordinary default budget changed — this is the value all non-llvm call sites run with"
    fi
fi

# ── T10: mixed outcomes ACROSS attempts ─────────────────────────────────────
#
# A transient timeout followed by a genuine, reproducible failure must report the
# FINAL attempt, which is what the EXIT CODES block promises. A sticky flag made
# the guard print "all N attempts exceeded Ns" directly beneath its own log
# saying otherwise, and return 124 where the real answer was 42.
if run_cell T10; then
    rc=0
    out=$(APT_GUARD_TIMEOUT=2 APT_GUARD_ATTEMPTS=3 APT_GUARD_BACKOFF=0 \
            "$GUARD" t10 -- "$WORK/mixed.sh" "$WORK/t10.state" 2>&1) || rc=$?
    check "T10 a transient timeout then a real failure exits with the REAL code" "$rc" "42"
    if echo "$out" | grep -qi 'wedged or degraded apt mirror'; then
        bad "T10 the final verdict blamed the mirror although the last attempts failed for their own reason — the flag is sticky across attempts"
    else
        ok "T10 the final verdict is not poisoned by an earlier attempt's timeout"
    fi
fi

# ── T11: the labels the WORKFLOWS pass are labels the dispatch knows ────────
#
# A typo (`llvm-toolchian`) silently downgrades 900s to 300s and then reads as a
# slow mirror. Derived from the workflows, so a new label must be added here
# deliberately rather than defaulting quietly.
if run_cell T11; then
    known="apt-install apt-update llvm-toolchain"
    # ⚠️ COMMENT LINES EXCLUDED. Without this the probe matched prose about the
    # wrapper ("ci/apt-guard.sh tests the WRAPPER") and reported `tests` as an
    # unknown label — a false RED, and a reminder that a census over source text
    # has to say what counts as a call before it can count them.
    used=$(grep -rh 'apt-guard\.sh' "$REPO_ROOT/.github/workflows/" 2>/dev/null \
             | grep -v '^[[:space:]]*#' \
             | grep -o 'apt-guard\.sh [a-z-]* --' \
             | awk '{print $2}' | sort -u)
    if [ -z "$used" ]; then
        bad "T11 found ZERO apt-guard labels in the workflows — the probe is broken, not the tree"
    else
        unknown=""
        for l in $used; do
            case " $known " in *" $l "*) ;; *) unknown="$unknown $l" ;; esac
        done
        if [ -n "$unknown" ]; then
            bad "T11 workflows pass label(s) the case dispatch does not know:$unknown — they fall to the default budget silently"
        else
            ok "T11 every label the workflows pass is known to the budget dispatch ($(echo $used | tr '\n' ' '))"
        fi
    fi
fi

# ═════ MUTANTS ═══════════════════════════════════════════════════════════════
#
# Each mutant breaks exactly one property and names the cell that must go RED.
# The `cmp -s` guard is load-bearing: a sed that matches nothing leaves an
# identical copy, and the mutant would then "pass" by testing the shipped
# script — reading as a thorough harness that tested nothing.
echo
echo "== mutants (each must turn its named cell RED) =="

mutant() {
    local name="$1" sedexpr="$2" cell="$3" why="$4"
    local copy="$WORK/mutant-$name.sh"
    cp "$GUARD" "$copy"
    sed -i "$sedexpr" "$copy"
    if cmp -s "$GUARD" "$copy"; then
        bad "MUTANT $name — the mutation did NOT apply (re-point the pattern, do not delete the mutant)"
        return
    fi
    chmod +x "$copy"
    # Re-run THIS harness against the broken copy and require it to fail.
    if APT_GUARD_SCRIPT="$copy" APT_GUARD_ONLY="$cell" "$BASH_SOURCE" >"$WORK/mutant-$name.log" 2>&1; then
        bad "MUTANT $name — harness stayed GREEN against a script that $why (expected $cell to fail)"
    else
        if grep -q "FAIL  $cell" "$WORK/mutant-$name.log"; then
            ok "MUTANT $name — $cell went RED as required ($why)"
        else
            bad "MUTANT $name — harness failed, but NOT at $cell (it failed elsewhere; the cell does not pin what it claims)"
        fi
    fi
}

# Guard against infinite recursion: the mutant runs re-enter this file.
if [ "${APT_GUARD_IN_MUTANT:-0}" = "1" ]; then
    echo
    echo "apt-guard harness: ${PASS} passed, ${FAIL} failed"
    [ "$FAIL" -eq 0 ] || exit 1
    exit 0
fi
export APT_GUARD_IN_MUTANT=1

# 1. Drop --kill-after. Bounds a cooperative command, decorative against the
#    SIGTERM-deaf one every `sudo` caller actually produces.
mutant no-kill-after \
    's|timeout --kill-after="\${APT_GUARD_KILL_AFTER}s"|timeout|' \
    "T3" "bounds only cooperative commands, leaving a sudo-shaped hang alive"

# 2. Remove the bound entirely, keeping the retry. This is precisely the shape
#    the issue warns about: the retry then multiplies the hang.
mutant no-timeout \
    's|^    timeout --kill-after=.*$|    "$@"|' \
    "T2" "retries an UNBOUNDED command — the hang-multiplying shape #300 warns about"

# 4. THE DEFECT THIS PR SHIPPED AND A REVIEW CAUGHT: infer the timeout from the
#    raw exit code instead of tracking whether the watchdog fired. Passes the
#    clock half of T3 (the kill still happens) and misreports the sudo-shaped
#    case, which returns 137 rather than 124.
mutant infer-timeout-from-exit-code \
    's|^    if \[ "\$rc" -eq 124 \] \|\| \[ "\$rc" -eq 137 \]; then$|    if [ "$rc" -eq 124 ]; then|' \
    "T3" "classifies a wedged mirror from the raw exit code, so a SIGTERM-deaf hang (137) reads as an ordinary failure"

# 5. Widen the DEPLOYED default budget back to an unbounded-in-practice value.
#    Every timing cell overrides it, so only T9 sees this.
mutant widen-default-budget \
    's|^  llvm-toolchain) _apt_guard_default_timeout=900 ;;$|  llvm-toolchain) _apt_guard_default_timeout=86400 ;;|' \
    "T9" "restores an effectively unbounded budget at every real llvm-toolchain call site"

# 6. Make the timeout flag STICKY across attempts — the defect a review found in
#    this PR's own first fix round.
mutant sticky-timeout-flag \
    's|^if \[ "\$attempt_timed_out" -eq 1 \]; then$|if [ "$attempt_timed_out" -eq 1 ] \|\| [ "$attempt" -gt 1 ]; then|' \
    "T10" "lets an earlier attempt's timeout poison the final verdict and exit code"

# 3. Normalise every failure to 1, losing the mirror-vs-package distinction.
mutant flatten-exit \
    's|^exit "\$rc"$|exit 1  # MUTANT|' \
    "T6" "collapses 124 and the command's own code into a single opaque failure"

echo
echo "apt-guard harness: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
