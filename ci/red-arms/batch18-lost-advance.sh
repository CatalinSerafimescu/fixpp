#!/usr/bin/env bash
# ci/red-arms/batch18-lost-advance.sh -- the arms that justify #289 batch 18.
#
# ⚠️ THE FORCING SEAM CANNOT ARM THIS SHAPE, and reaching for it first is the mistake
# to avoid. `ci/pump-seam-arm.sh` forces a site's MISS branch and proves it reports.
# That is half of it. The other half is that a forced MISS cannot catch a SPURIOUS HIT
# -- a barrier satisfied by something other than the thing it is watching. PR #337 is
# the precedent: a bounded pump let a REAL `asio::steady_timer` complete an awaited
# future, so a lost mock-clock advance went green in 2202 ms while all four forced-miss
# arms passed. So the arms here are not forced misses. They inject the defect.
#
# THE DEFECT. A test that wants a mock-clock deadline to fire must first get the
# coroutine PARKED on it. Staging with a fixed `ioc.run_for(50ms)` is a wall-clock hope;
# if the coroutine has not parked when the window expires, `clock->advance()` lands on a
# timer that is not yet armed and is LOST -- unrecoverable, not slow, because nothing
# advances the clock again. Batch 18 replaces every such window with an OBSERVABLE
# staging condition: `sess.state() == LogoutSent` is exactly "Logout emitted, parked,
# not complete".
#
# ⚠️ THE SECOND SHAPE WAS BUILT, MEASURED, AND NOT SHIPPED -- record it here so nobody
# rebuilds it on the strength of the argument alone. Where no FSM state names "parked"
# (the DETACHED liveness loop), the obvious barrier is the clock's own waiter count, and
# a `mock_clock::pending_sleeps()` accessor was written for it. At BOTH candidate sites
# it failed to discriminate, proven by running rather than by reading:
#   * `HbTrTest.UnansweredTestRequestDisconnects` PASSES with its staging window starved
#     to `run_for(0ms)` -- a candidate, read, and NOT a defect. ⚠️ THE MECHANISM IS THE
#     ARM SHAPE, NOT MONOTONICITY (an earlier revision of this line said monotonicity and
#     that was wrong): `sleep_until` fires immediately when `deadline <= steady`, so an
#     arm taken from a STORED ANCHOR that predates the advance names an instant already
#     passed and is RESCUED, while a NOW-RELATIVE arm names a new future instant and is
#     LOST. The liveness loop is the first kind; `run_logout_phase1` is the second.
#   * `AdminDistinctNowTest.TR_DistinctNow` goes RED when starved, but RED with the
#     barrier IN PLACE too: what starving removed was the TERMINAL collection window,
#     a different and recoverable axis, not the staging.
# So the accessor would have been an assertion that cannot go RED for the class it was
# added for. It was removed. Rebuild it only against a site where a mutation shows it
# load-bearing.
#
#   ARM 0  unmutated                          -> GREEN. Attribution control: a red
#                                                below is the injected defect, not the
#                                                machine.
#   ARM 1  `clock->advance()` DELETED at an
#          armed site                         -> RED. ⚠️ THIS IS THE SPURIOUS-HIT ARM.
#                                                If the cell went GREEN without the
#                                                advance, some REAL timer -- the
#                                                `close_grace` `steady_timer` the mock
#                                                clock does not govern -- would be
#                                                completing it, and the cell would never
#                                                have exercised the mock path at all.
#   ARM 2  the staging PREDICATE forced
#          to `true`                          -> RED. `pump_until` returns immediately
#                                                without pumping, so the coroutine has
#                                                not parked and the advance is lost.
#                                                This reproduces the production failure.
#
# ⚠️ ARM 1 AND ARM 2 MUST NOT BE COLLAPSED. Arm 2 alone would pass against a cell that
# never used the mock clock; arm 1 alone would pass against a cell whose staging is
# vacuous. Each rules out the other's blind spot.
#
# ⚠️ EACH ARM CHECKS ITS BINARY'S SHA MOVED. A mutation that failed to apply leaves the
# previous binary in place and the arm grades a tree it did not build
# [[feedback_git_checkout_restore_destroys_uncommitted_work_in_mutation_arms]].
# Restores by `cp` from copies taken at entry -- never `git checkout --`, which would
# wipe uncommitted work in the same files.
#
# ⚠️ SIBLINGS: `ci/red-arms/batch17-genuine-miss.sh` and
# `ci/red-arms/first-frame-arm-gap.sh` carry the same mktemp/cp-restore trap and
# binary-SHA guard, deliberately unshared. The reason is written in
# `first-frame-arm-gap.sh` (both helpers in `ci/pump-arm-common.sh` `exit`, while a
# `run_arm()` must `return 1` and carry on) -- cite that file, not batch17, which only
# forwards. ⚠️ AND `ci/pump-arm-common.sh`'s own header says "two is where the third copy
# is cheap to prevent". THIS IS THE THIRD COPY, and it has already drifted from the other
# two in one visible way: they check each mutant's SHA against the PREVIOUS binary, this
# one against a single BASE_SHA. Change the idiom in one, open the others.
#
# Usage:  ci/red-arms/batch18-lost-advance.sh [PRESET]
set -uo pipefail
# ⚠️ THE FORCING SEAM MUST BE OFF, OR ARM 2 PASSES FOR THE WRONG REASON -- the same
# hazard `ci/red-arms/batch17-genuine-miss.sh` documents, and it applies here because
# ARM 2's site is a labelled `pump_until` call, which consults `forced_miss_here`. With
# `FIXPP_FORCE_WINDOW_MISS` set to that label -- which `ci/pump-seam-arm.sh` exports, so
# an operator running both in one shell inherits it -- the barrier takes the FORCED path
# and the arm grades the seam rather than the injected defect. It would fail toward a
# loud ARM 0 red rather than a silent pass, but "loud for the wrong reason" is still the
# wrong reason.
unset FIXPP_FORCE_WINDOW_MISS

PRESET="${1:-linux-clang-asan}"
cd "$(dirname "$0")/../.." || exit 2

BUILD="build/$PRESET"
[ -d "$BUILD" ] || { echo "no build dir '$BUILD' -- configure it first" >&2; exit 2; }

# The armed site: one TU, one cell.
SRC=tests/session/durable_before_transmit_test.cpp
TARGET=session_pure_tests
CASE='DurableBeforeTransmitTest.OutboundStoreBeforeTransportSend'
BIN="$BUILD/bin/$TARGET"

BAK=$(mktemp) || exit 2
cp "$SRC" "$BAK"
restore() { cp "$BAK" "$SRC"; rm -f "$BAK"; }
trap restore EXIT

build() { cmake --build "$BUILD" --target "$TARGET" -j"${JOBS:-4}" >/dev/null 2>&1; }
sha()   { sha256sum "$BIN" | cut -d' ' -f1; }

run_case() {  # $1 = gtest filter; sets RC and OUT
    OUT=$(timeout "${TIMEOUT_S:-180}" "$BIN" --gtest_filter="$1" 2>&1); RC=$?
}

echo "== building the unmutated tree =="
build || { echo "BASE BUILD FAILED"; exit 2; }
BASE_SHA=$(sha)
fails=0

echo
echo "ARM 0 -- unmutated: expect GREEN (attribution control)"
run_case "$CASE"
if [ "$RC" -eq 0 ]; then
    echo "  ok    the cell passes unmutated -- a red below is the injected defect."
else
    echo "  !!BAD the unmutated tree does not pass (exit=$RC); nothing below is attributable."
    printf '%s\n' "$OUT" | tail -12
    exit 1
fi

# ── the mutators ─────────────────────────────────────────────────────────────
# Each asserts its own application. A mutation that silently does not apply is an
# arm that grades correct code and calls it proof.
mutate() {  # $1 = arm name
    cp "$BAK" "$SRC"
    python3 - "$1" "$SRC" <<'PY'
import sys, pathlib
arm, a_path = sys.argv[1], sys.argv[2]
a = pathlib.Path(a_path)

if arm == "no-advance":
    s = a.read_text()
    old = "    clock->advance(std::chrono::seconds{3});\n"
    assert s.count(old) == 1, f"expected 1 advance in {a_path}, found {s.count(old)}"
    a.write_text(s.replace(old, "    // MUTANT: advance deleted\n"))
    print("  mutated: clock->advance() deleted")

elif arm == "stage-true":
    s = a.read_text()
    old = "ioc, [&sess] { return sess.state() == fsm_state::LogoutSent; },"
    assert s.count(old) == 1, f"expected 1 staging predicate in {a_path}, found {s.count(old)}"
    a.write_text(s.replace(old, "ioc, [&sess] { (void)&sess; return true; },"))
    print("  mutated: staging predicate forced to true")

else:
    raise SystemExit(f"unknown arm {arm}")
PY
}

run_arm() {  # $1 = arm name, $2 = description
    local name="$1" desc="$2"
    echo
    echo "ARM: $desc"
    mutate "$name" || { echo "  !!BAD the mutator failed"; fails=$((fails+1)); return; }
    build || { echo "  !!BAD build failed for arm '$name'"; fails=$((fails+1)); return; }
    if [ "$(sha)" = "$BASE_SHA" ]; then
        echo "  !!BAD binary unchanged -- the '$name' mutation did not reach it. Inert arm."
        fails=$((fails+1)); return
    fi
    run_case "$CASE"
    if [ "$RC" -ne 0 ]; then
        echo "  ok    RED (exit=$RC) -- the barrier is load-bearing here."
        printf '%s\n' "$OUT" | grep -E '^\[  FAILED  \]|#28[49]:' | head -4 | sed 's/^/        /'
    else
        echo "  !!BAD GREEN under the injected defect -- the cell does not discriminate."
        fails=$((fails+1))
    fi
}

run_arm no-advance \
    "ARM 1 -- clock->advance() DELETED: expect RED (else a REAL timer is completing it)"
run_arm stage-true \
    "ARM 2 -- staging predicate forced true: expect RED (the lost advance)"

echo
restore; trap - EXIT
build || { echo "!! could not rebuild the restored tree" >&2; exit 1; }
[ "$(sha)" = "$BASE_SHA" ] || { echo "!! restored binary does not match the base SHA" >&2; exit 1; }

if [ "$fails" -ne 0 ]; then
    echo "=== FAILED: $fails of 2 arms" >&2; exit 1
fi
echo "=== PASS: 2/2 arms RED, tree restored byte-identical."
