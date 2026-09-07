#!/usr/bin/env bash
# ci/red-arms/first-frame-arm-gap.sh -- the arm behind the T1/B6 timebase change.
#
# THE DEFECT CLASS. `read_first_frame_bounded` fixes its deadline at entry
# (`abs_deadline = clock.steady_now() + deadline`) and the mock transport arms its
# latency timer a few instructions later, inside the join. The interval between
# those two arms -- the ARM GAP -- is microseconds of CPU work, and both cells that
# used the REAL clock depended on it staying below `deadline - read_latency`:
#
#   T1  needs the read's expiry to PRECEDE the deadline's (10 ms vs 1 ms => a 9 ms gap
#       inverts the order and the deadline wins a race it must lose).
#   B6  needs at least one read to land inside the window (50 ms vs 7 ms => a 43 ms gap
#       leaves `buf.size() == 0` and the cell fails its own non-vacuity check).
#
# A starved runner stretches that interval. windows-msvc-asan under `ctest --parallel 4`
# failed BOTH on run 34074957982 (`push:main`, f83cf588) with the code under test correct.
#
# ⚠️ WHAT THIS SCRIPT IS FOR, AND WHAT IT IS NOT. It re-derives the mechanism on T1,
# which is the half that reproduces on Linux: T1 needs only a >9 ms gap, B6 needs >43 ms
# and did not reproduce here in ~100 runs. B6's half is re-derived by MUTATION at its own
# cell (the per-iteration re-arm), not by this script -- do not read a green run here as
# evidence about B6.
#
# ⚠️ AND ARM 2's ZERO IS ONLY BELIEVABLE BECAUSE ARM 1 IS NON-ZERO UNDER THE IDENTICAL
# LOAD. Run alone, arm 2 proves nothing: a load that starves nothing produces a clean run
# whatever the constants are. That is this repo's most recurring defect -- an instrument
# that reports clean because it could not report anything else -- so the arms are graded
# as a PAIR and arm 1 failing to go red is itself a FAILURE of this script.
#
#   ARM 0  post-fix construction, NO load        -> GREEN. Attribution control: the
#                                                   binary and the cell are sound, so a
#                                                   red in arm 1 is the load, not a bug.
#   ARM 1  the PRE-FIX cell -- old constants, single
#          attempt, AND the staging check removed  -> at least one RED, and RED for the
#          -- under load                             stated reason (the deadline won).
#   ARM 2  post-fix construction, same load      -> zero RED.
#
# ⚠️ ARM 1 MUST GO RED FOR THE RIGHT REASON. A binary that crashed under load also fails,
# and would read as a reproduction. The arm therefore requires the failure text to name
# the inversion (`must still win the frame`), not merely a non-zero exit. That guard has
# already earned itself once: a first draft reverted only the CONSTANTS and left the new
# staging check in, so the pre-fix arm went red 6/25 with the staging message -- a real
# red, for the wrong mechanism, which without this guard would have been logged as a
# reproduction of the inversion. The check did not exist pre-fix, so reconstructing the
# pre-fix cell means removing it too.
# ⚠️ AND EACH MUTATED BUILD CHECKS ITS BINARY'S SHA MOVED -- a mutation that failed to
# apply leaves the previous binary in place and the arm grades a tree it did not build
# [[feedback_git_checkout_restore_destroys_uncommitted_work_in_mutation_arms]].
#
# ⚠️ ONE LINE IN ARM 1's OUTPUT IS A MUTATION ARTIFACT, not a contradiction. The shipped
# failure message ends "Staging was verified before this ran ... so this is NOT the
# arm-gap starvation" -- true of the shipped cell, and FALSE under arm 1, which deleted
# the staging check on purpose. Read it as "the pre-fix cell had no such verification",
# which is the whole point of the arm.
#
# Restores by `cp` from a copy taken at entry -- never `git checkout --`, which would wipe
# uncommitted work in the same file.
#
# Usage:  ci/red-arms/first-frame-arm-gap.sh [ITERATIONS] [HOGS]
# Needs a configured build dir; override with BUILD_DIR=... (default linux-clang-asan).
set -uo pipefail

ITERATIONS="${1:-25}"
HOGS="${2:-250}"
BUILD_DIR="${BUILD_DIR:-build/linux-clang-asan}"
TARGET=session_read_first_frame_bounded

cd "$(dirname "$0")/../.." || exit 2
SRC=tests/session/read_first_frame_bounded_test.cpp
BIN="$BUILD_DIR/bin/$TARGET"

[ -d "$BUILD_DIR" ] || { echo "no build dir '$BUILD_DIR' -- configure it or set BUILD_DIR"; exit 2; }

BAK=$(mktemp) || exit 2
cp "$SRC" "$BAK"
HOG_PIDS=()
cleanup() {
    [ "${#HOG_PIDS[@]}" -gt 0 ] && kill "${HOG_PIDS[@]}" 2>/dev/null
    cp "$BAK" "$SRC"
    rm -f "$BAK"
}
trap cleanup EXIT

build() {
    cmake --build "$BUILD_DIR" --target "$TARGET" -j"${JOBS:-4}" >/dev/null 2>&1
}

# Run the two real-clock cells N times, echoing every failure body. Prints the count.
run_n() {
    local n="$1" fails=0 out
    for _ in $(seq "$n"); do
        out=$(timeout 300 "$BIN" --gtest_filter='*T1:*B6' 2>&1)
        if grep -q '^\[  FAILED  \]' <<<"$out"; then
            fails=$((fails + 1))
            printf '%s\n' "$out" | sed -n '/^\[ RUN/,/^\[----------\] 2 tests/p'
        fi
    done
    echo "$fails"
}

start_hogs() {
    local i
    for ((i = 0; i < HOGS; i++)); do
        (while :; do :; done) &
        HOG_PIDS+=($!)
    done
}
stop_hogs() {
    [ "${#HOG_PIDS[@]}" -gt 0 ] && kill "${HOG_PIDS[@]}" 2>/dev/null
    HOG_PIDS=()
    wait 2>/dev/null
}

echo "== building the base (post-fix) tree =="
build || { echo "BASE BUILD FAILED"; exit 2; }
BASE_SHA=$(sha256sum "$BIN" | cut -d' ' -f1)

rc=0

echo
echo "== ARM 0 -- post-fix construction, NO load (attribution control) =="
a0=$(run_n 3)
if [ "$a0" -eq 0 ]; then
    echo "ARM 0: GREEN 3/3 -- the binary and the cell are sound unloaded."
else
    echo "ARM 0: FAILED $a0/3 WITHOUT ANY LOAD. Nothing below is attributable to starvation."
    exit 1
fi

echo
echo "== ARM 1 -- PRE-FIX constants (kDeadline=10ms kReadLatency=1ms, single attempt) under $HOGS hogs =="
cp "$BAK" "$SRC"
perl -0pi -e '
  s{constexpr auto kDeadline = std::chrono::milliseconds\{500\};}
   {constexpr auto kDeadline = std::chrono::milliseconds{10};}s;
  s{constexpr auto kReadLatency = std::chrono::milliseconds\{50\};}
   {constexpr auto kReadLatency = std::chrono::milliseconds{1};}s;
  s{constexpr int kAttempts = 4;}{constexpr int kAttempts = 1;}s;
  s#if \(!buf\.empty\(\) \|\| poll_span \+ kReadLatency >= kDeadline\) \{#if (false) {#s;
' "$SRC" || { echo "ARM 1: the mutation script itself failed to run."; exit 2; }
if cmp -s "$BAK" "$SRC"; then
    echo "ARM 1: MUTATION DID NOT APPLY -- the constants were renamed or reshaped. Fix this script."
    exit 2
fi
build || { echo "ARM 1: BUILD FAILED"; exit 2; }
MUT_SHA=$(sha256sum "$BIN" | cut -d' ' -f1)
if [ "$MUT_SHA" = "$BASE_SHA" ]; then
    echo "ARM 1: BINARY UNCHANGED -- the arm would grade the post-fix tree. Inert."
    exit 2
fi
start_hogs
a1_out=$(run_n "$ITERATIONS")
stop_hogs
a1=$(tail -n1 <<<"$a1_out")
printf '%s\n' "$a1_out" | head -n -1
echo "ARM 1: $a1/$ITERATIONS red under load."
if [ "$a1" -eq 0 ]; then
    echo "ARM 1: NO REPRODUCTION. The load did not starve this machine, so ARM 2's zero would"
    echo "       prove nothing. Raise HOGS (arg 2) or ITERATIONS (arg 1) and re-run."
    rc=1
elif ! grep -q 'must still win the frame' <<<"$a1_out"; then
    echo "ARM 1: RED, BUT NOT FOR THE STATED REASON -- no 'must still win the frame' in the"
    echo "       failure text. That is a different defect, not the arm gap. Read the dump above."
    rc=1
else
    echo "ARM 1: reproduced the inversion (the deadline won the race it must lose)."
fi

echo
echo "== ARM 2 -- post-fix construction under the SAME $HOGS hogs =="
cp "$BAK" "$SRC"
build || { echo "ARM 2: BUILD FAILED"; exit 2; }
if [ "$(sha256sum "$BIN" | cut -d' ' -f1)" != "$BASE_SHA" ]; then
    echo "ARM 2: restored binary does not match the base SHA -- the restore is not byte-exact."
    exit 2
fi
start_hogs
a2_out=$(run_n "$ITERATIONS")
stop_hogs
a2=$(tail -n1 <<<"$a2_out")
printf '%s\n' "$a2_out" | head -n -1
echo "ARM 2: $a2/$ITERATIONS red under load."
[ "$a2" -eq 0 ] || rc=1

echo
echo "== VERDICT =="
echo "ARM 0 (no load, post-fix)   : $a0/3 red"
echo "ARM 1 (load, PRE-FIX)       : $a1/$ITERATIONS red   <- must be non-zero"
echo "ARM 2 (load, post-fix)      : $a2/$ITERATIONS red   <- must be zero"
[ "$rc" -eq 0 ] && echo "PASS" || echo "FAIL"
exit "$rc"
