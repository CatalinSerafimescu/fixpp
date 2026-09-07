#!/usr/bin/env bash
# ci/red-arms/batch19-coroutine-side-get.sh -- the arms that justify #289 batch 19.
#
# ⚠️ A FORCED MISS IS NOT THE ARM THIS SHAPE NEEDS. `ci/pump-seam-arm.sh` proves a
# site's miss branch REPORTS; it says nothing about whether the guard was worth adding,
# because forcing skips the very wait whose absence is the defect. So these arms inject
# the defect and compare the two shapes under it.
#
# THE DEFECT. `fd.get()` at these sites runs INSIDE a coroutine, on the thread that is
# pumping the io_context. If the awaited future is not ready, the block happens inside
# the handler the driver dispatched -- so the driver is wedged too. That is what makes
# this shape different from every other #289 site, and it is the whole justification for
# a coroutine-side primitive:
#
#   * `test_drain_predrain_holder.cpp` bounds itself with a 5 s deadline loop around
#     `ioc.run_for(100ms)`. The 100 ms call never returns, the deadline is never
#     re-tested, and `ASSERT_FALSE(timed_out)` is never reached.
#   * `test_pool_exhaustion_reuse.cpp` drives with `run_to_exhaustion_or_report` --
#     ALREADY the #289 guard for the outer future -- and it is inside `ioc.run()` when
#     the inner `get()` blocks.
#
# So the bounded outer driver, which is the #289 remedy at every caller-side site, does
# not bound this one. ARM 1 is what makes that a measurement instead of an argument.
#
#   ARM 0  unmutated, guarded              -> GREEN. Attribution control.
#   ARM 1  guard REMOVED (the pre-batch
#          shape) + holder release deleted -> WEDGE (timeout). ⚠️ THIS IS THE ARM THAT
#                                             PRICES THE BATCH. Without it "the outer
#                                             driver does not bound this" is a claim
#                                             about asio read off a page.
#   ARM 2  guard IN PLACE + holder release
#          deleted                         -> REPORTS at the site and exits promptly.
#
# ⚠️ ARM 1 AND ARM 2 MUST NOT BE COLLAPSED, and neither may be replaced by starving the
# window. A starved window is RECOVERED by the grace loop -- correctly, that is what the
# grace is for -- so it produces a PASS, not a report. The injected defect has to be one
# no amount of yielding can fix: the holder is never released, so the drain can never
# finalize. Reaching for the window is the obvious mistake and it fails toward green.
#
# ⚠️ EACH ARM CHECKS ITS BINARY'S SHA MOVED. A mutation that failed to apply leaves the
# previous binary in place and the arm grades a tree it did not build
# [[feedback_git_checkout_restore_destroys_uncommitted_work_in_mutation_arms]].
# Restores by `cp` from a copy taken at entry -- never `git checkout --`, which would
# wipe uncommitted work in the same file.
#
# ⚠️ SIBLINGS, AND THE REASON THREE OF THEM GIVE FOR NOT SHARING DOES NOT COVER THIS.
# `ci/red-arms/batch17-genuine-miss.sh`, `batch18-lost-advance.sh` and
# `first-frame-arm-gap.sh` carry the same mktemp/cp-restore trap and binary-SHA guard.
# The forwarded justification -- "both helpers in `ci/pump-arm-common.sh` `exit`, while a
# `run_arm()` must `return 1`" -- is about REUSING those two helpers, which are POPULATION
# guards (`assert_nonempty_population`, `assert_ran_count`). Neither is any part of the
# skeleton being copied, so their `exit` convention was never a constraint on it. An
# earlier revision of this header repeated that reason anyway; it is withdrawn here rather
# than forwarded a fourth time.
#
# The real cost of consolidating is that it changes three scripts that are themselves
# instruments, in a batch that changes none of them -- so it is a follow-up, not a
# rider. This is the FOURTH copy, and `ci/pump-arm-common.sh`'s header already said "two
# is where the third copy is cheap to prevent". Recorded as a decision, with the correct
# reason, so the next reader is not handed the wrong one.
#
# Usage:  ci/red-arms/batch19-coroutine-side-get.sh [PRESET]
set -uo pipefail
# ⚠️ THE FORCING SEAM MUST BE OFF. ARM 2's site is a labelled call, so with
# `FIXPP_FORCE_WINDOW_MISS` set to that label -- which `ci/pump-seam-arm.sh` exports, so
# an operator running both in one shell inherits it -- the guard would take the FORCED
# path and the arm would grade the seam rather than the injected defect.
unset FIXPP_FORCE_WINDOW_MISS

PRESET="${1:-linux-clang-asan}"
cd "$(dirname "$0")/../.." || exit 2

BUILD="build/$PRESET"
[ -d "$BUILD" ] || { echo "no build dir '$BUILD' -- configure it first" >&2; exit 2; }

SRC=tests/sync/test_drain_predrain_holder.cpp
TARGET=sync_pure_tests
CASE='DrainPredrainHolder.HolderUnlocksWhileDrainYields'
LABEL='DrainPredrainHolder::HolderUnlocksWhileDrainYields/drain'
REPORT_TAIL='bounded grace that follows. Site: '
BIN="$BUILD/bin/$TARGET"

# The wedge budget. Derived, not guessed: the cell's own outer bound is a 5 s deadline
# loop, so anything past ~10 s is a wedge and not a slow machine. Doubled again for a
# loaded CI runner.
WEDGE_S="${WEDGE_S:-25}"

BAK=$(mktemp) || exit 2
cp "$SRC" "$BAK"
restore() { cp "$BAK" "$SRC"; rm -f "$BAK"; }
trap restore EXIT

build() { cmake --build "$BUILD" --target "$TARGET" -j"${JOBS:-4}" >/dev/null 2>&1; }
sha()   { sha256sum "$BIN" | cut -d' ' -f1; }

run_case() {  # sets RC and OUT
    OUT=$(timeout "$WEDGE_S" "$BIN" --gtest_filter="$CASE" 2>&1); RC=$?
}

echo "== building the unmutated tree =="
build || { echo "BASE BUILD FAILED"; exit 2; }
BASE_SHA=$(sha)
fails=0

echo
echo "ARM 0 -- unmutated: expect GREEN (attribution control)"
run_case
if [ "$RC" -eq 0 ]; then
    echo "  ok    the cell passes unmutated -- anything below is the injected defect."
else
    echo "  !!BAD the unmutated tree does not pass (exit=$RC); nothing below is attributable."
    printf '%s\n' "$OUT" | tail -12
    exit 1
fi

# ── the mutators ─────────────────────────────────────────────────────────────
# Each asserts its own application. A mutation that silently does not apply is an arm
# that grades correct code and calls it proof.
mutate() {  # $1 = arm name
    cp "$BAK" "$SRC"
    python3 - "$1" "$SRC" <<'PY'
import sys, pathlib
arm, a_path = sys.argv[1], sys.argv[2]
a = pathlib.Path(a_path)
s = a.read_text()

# The injected defect, shared by both arms: the holder is never released, so
# `cancel_and_drain()` can never observe active_holders_count_ == 0 and never finalizes.
# No number of yields fixes this -- which is exactly why the window is not the mutation.
HOLD = "        holder_guard = expected_t<async_lock_guard>{};\n"
assert s.count(HOLD) == 1, f"expected 1 holder release, found {s.count(HOLD)}"
s = s.replace(HOLD, "        // MUTANT: holder never released -- the drain cannot finalize.\n")

if arm == "unguarded":
    # Restore the PRE-BATCH shape at this site: the preserved window, then a bare get().
    start = s.index("        // ── #289 batch 19: the COROUTINE-SIDE `.get()`")
    end = s.index("        fd.get();\n", start) + len("        fd.get();\n")
    s = s[:start] + "        co_await yield_n(8);\n        fd.get();\n" + s[end:]
    print("  mutated: holder release deleted AND the guard removed (pre-batch shape)")
elif arm == "guarded":
    print("  mutated: holder release deleted, guard left in place")
else:
    raise SystemExit(f"unknown arm {arm}")

a.write_text(s)
PY
}

prepare() {  # $1 = arm name; returns 1 if the arm cannot be graded
    mutate "$1" || { echo "  !!BAD the mutator failed"; return 1; }
    build || { echo "  !!BAD build failed for arm '$1'"; return 1; }
    if [ "$(sha)" = "$BASE_SHA" ]; then
        echo "  !!BAD binary unchanged -- the '$1' mutation did not reach it. Inert arm."
        return 1
    fi
    return 0
}

echo
echo "ARM 1 -- pre-batch shape under the injected defect: expect a WEDGE (timeout)"
if prepare unguarded; then
    run_case
    if [ "$RC" -eq 124 ]; then
        echo "  ok    WEDGED at ${WEDGE_S}s -- the 5 s deadline loop did NOT bound it,"
        echo "        because get() blocks the very thread running run_for(100ms)."
    else
        echo "  !!BAD exit=$RC, not a wedge. The premise of this batch is that the outer"
        echo "        driver cannot bound a coroutine-side get(); this says otherwise."
        printf '%s\n' "$OUT" | tail -8 | sed 's/^/        /'
        fails=$((fails+1))
    fi
else
    fails=$((fails+1))
fi

echo
echo "ARM 2 -- guarded, same defect: expect a REPORT naming the site, promptly"
if prepare guarded; then
    run_case
    if [ "$RC" -eq 124 ]; then
        echo "  !!BAD still wedged -- the guard did not bound it."
        fails=$((fails+1))
    elif [ "$RC" -eq 0 ]; then
        echo "  !!BAD GREEN under the injected defect -- the guard reports nothing and the"
        echo "        cell does not discriminate."
        fails=$((fails+1))
    elif grep -qF "$REPORT_TAIL$LABEL" <<<"$OUT"; then
        echo "  ok    RED (exit=$RC) and the report NAMES the site."
        printf '%s\n' "$OUT" | grep -F "$REPORT_TAIL$LABEL" | head -1 | sed 's/^/        /'
    else
        echo "  !!BAD RED (exit=$RC) but no report carrying '$LABEL' -- the failure is"
        echo "        attributable to nothing. Silence here is the blind-spot outcome."
        printf '%s\n' "$OUT" | tail -8 | sed 's/^/        /'
        fails=$((fails+1))
    fi
else
    fails=$((fails+1))
fi

echo
restore; trap - EXIT
build || { echo "!! could not rebuild the restored tree" >&2; exit 1; }
[ "$(sha)" = "$BASE_SHA" ] || { echo "!! restored binary does not match the base SHA" >&2; exit 1; }

if [ "$fails" -ne 0 ]; then
    echo "=== FAILED: $fails of 2 arms" >&2; exit 1
fi
echo "=== PASS: 2/2 arms -- unguarded WEDGES, guarded REPORTS. Tree restored byte-identical."
