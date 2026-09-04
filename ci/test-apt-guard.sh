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

# 3. Normalise every failure to 1, losing the mirror-vs-package distinction.
mutant flatten-exit \
    's|^exit "\$rc"$|exit 1  # MUTANT|' \
    "T6" "collapses 124 and the command's own code into a single opaque failure"

echo
echo "apt-guard harness: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
