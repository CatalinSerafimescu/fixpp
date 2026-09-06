#!/usr/bin/env bash
# ci/red-arms/batch17-genuine-miss.sh -- the ONE arm that justifies #289 batch 17.
#
# WHY A FORCED ARM IS NOT ENOUGH. `ci/pump-seam-arm.sh` proves the miss BRANCH runs and
# reports. It cannot prove the branch is REACHABLE by anything other than the seam -- and
# if it is not, 162 guards buy nothing. This script injects the defect a real edit would
# make, at a real site, and runs BOTH shapes so the pair is the verdict:
#
#   ARM 0  BASE-SHAPED, restart LEFT IN   -> PASSES fast. The attribution control.
#   ARM 1  BASE-SHAPED, restart DELETED    -> WEDGES. exit 124.
#   ARM 2  GUARDED,     restart DELETED    -> REPORTS `kRunMiss`, exit non-zero.
#
# ⚠️ RUNNING ONLY ARM 2 WOULD PROVE THE WRONG THING. "The guard reports" is not "the old
# code was worse"; only arm 1 says the shape being replaced actually hangs. An earlier
# draft of this script ran arm 2 alone while its own header claimed the pair.
#
# THE INJECTED DEFECT is a deleted `ioc.restart()` -- failure mode (b) in
# `run_to_exhaustion_or_report`'s header. These tests reuse ONE io_context across several
# co_spawn/run pairs and hand-write the restart between them. `run()` on a context still
# stopped from the previous `run()` returns IMMEDIATELY having dispatched nothing, so the
# future is never ready and the `get()` below it never returns. One line, and most of the
# files this batch touched hand-write at least one -- re-derive rather than trusting a
# number here:
#   git grep -l 'run_to_exhaustion_or_report' -- tests/ | grep -v support/pump_until_ready.hpp
#   ... then `git grep -l '\.restart()' --` over that list.
# (An earlier revision of this line wrote "21 of the 43". It reads 19: the 21 was counted
# over a list that still included the deferred-only files.)
#
# ⚠️ ARM 1 MUST HANG FOR THE RIGHT REASON. A binary that crashed at startup also fails to
# finish and would read as a hang, so the arm additionally requires the gtest `[ RUN` line
# for the case under test to have been printed before the timeout.
# ⚠️ AND EACH ARM CHECKS ITS BINARY'S SHA MOVED. A mutation that failed to apply leaves the
# previous binary in place and the arm grades a tree it did not build
# [[feedback_git_checkout_restore_destroys_uncommitted_work_in_mutation_arms]].
#
# Restores by `cp` from a copy taken at entry -- never `git checkout --`, which would wipe
# uncommitted work in the same file.
set -uo pipefail
# ⚠️ THE FORCING SEAM MUST BE OFF, OR ARM 2 PASSES FOR THE WRONG REASON. With
# FIXPP_FORCE_WINDOW_MISS set to this site's label -- which is exactly what
# `ci/pump-seam-arm.sh` exports, so an operator running both in one shell can inherit it --
# `run_to_exhaustion_or_report` takes the FORCED path and emits the same report tail and
# label this script greps for. The arm would then be validating the seam rather than the
# deleted `ioc.restart()`, and would say so in the words of a genuine miss.
unset FIXPP_FORCE_WINDOW_MISS
PRESET="${1:-linux-clang-debug}"
TARGET="session_plaintext_factory_mismatch_test"
BIN="build/$PRESET/bin/$TARGET"
FILE="tests/session/test_session_plaintext_factory_mismatch.cpp"
CASE="PlaintextFactoryMismatch.Cell_a_PlaintextProfileWithTlsOverrideRejects"
LABEL="PlaintextFactoryMismatch::Cell_a_PlaintextProfileWithTlsOverrideRejects/close_fut_a"
REPORT_TAIL='would have blocked forever. Site: '
TIMEOUT_S="${TIMEOUT_S:-30}"

[ -f "$BIN" ] || { echo "no such binary: $BIN -- build the preset first" >&2; exit 2; }
orig="$(mktemp)"; cp "$FILE" "$orig"
restore() { cp "$orig" "$FILE"; rm -f "$orig"; }
trap restore EXIT

# ── the two mutations, applied to a pristine copy each time ──────────────────
# `base` also strips the guard, so arm 1 is literally the pre-batch source at that site.
mutate() {   # $1 = "guarded" | "base"
    cp "$orig" "$FILE"
    python3 - "$FILE" "$LABEL" "$1" <<'PY'
import sys, pathlib, re
path, label, mode = sys.argv[1], sys.argv[2], sys.argv[3]
lines = pathlib.Path(path).read_text().split('\n')

# The guard block for THIS label: from its `if (!...run_to_exhaustion_or_report(` line
# through the `}` that closes it. The label may be SPLIT across source lines by
# clang-format (C++ concatenates adjacent literals), so match on a distinctive tail
# rather than on the whole label as one token.
tail = label.rsplit('/', 1)[-1]
gi = None
for k, l in enumerate(lines):
    if 'run_to_exhaustion_or_report(' in l:
        blk = '\n'.join(lines[k:k + 6])
        if tail in blk and 'return;' in blk:
            gi = k
            break
assert gi is not None, "guard block for %s not found" % label
gj = gi
while lines[gj].strip() != '}':
    gj += 1
    assert gj < gi + 12, "guard block did not close within 12 lines"

# The restart that arms this pair: the last bare `ioc.restart();` ABOVE the guard.
ri = max(k for k in range(gi) if lines[k].strip() == 'ioc.restart();')

if mode.startswith('base'):
    lines[gi:gj + 1] = ['    ioc.run();']
if mode != 'base-clean':
    del lines[ri]
pathlib.Path(path).write_text('\n'.join(lines))
print(f"  mutated ({mode}): "
      + ("restart LEFT IN" if mode == 'base-clean' else f"deleted restart at line {ri+1}")
      + (f", guard lines {gi+1}-{gj+1} -> ioc.run();" if mode.startswith('base') else ""))
PY
}

run_arm() {  # $1 = "base-clean" | "base" | "guarded"
    local mode="$1" before after rc out
    mutate "$mode" || return 1
    before=$(sha256sum "$BIN" | cut -d' ' -f1)
    cmake --build "build/$PRESET" --target "$TARGET" -j 6 >/dev/null 2>&1 || {
        echo "  !! build failed for arm '$mode'" >&2; return 1; }
    after=$(sha256sum "$BIN" | cut -d' ' -f1)
    [ "$before" != "$after" ] || {
        echo "  !! binary unchanged -- the '$mode' mutation did not reach it" >&2; return 1; }
    rc=0
    out=$(timeout "$TIMEOUT_S" "$BIN" --gtest_filter="$CASE" 2>&1) || rc=$?
    ARM_RC="$rc"; ARM_OUT="$out"
    return 0
}

fails=0

# ⚠️ ARM 0 IS WHAT MAKES ARM 1 MEAN ANYTHING. Arm 1 accepts "timed out after entering the
# test", which on its own cannot tell the injected defect from a hang somewhere earlier in
# the same case -- acceptor setup, TLS, an unrelated pump. Arm 0 runs the SAME base shape
# with the restart LEFT IN: if that passes quickly, the only difference between the two is
# the deleted line, and arm 1's wedge is attributable to it.
echo "ARM 0 -- BASE-SHAPED, restart LEFT IN: expect a fast PASS (attribution control)"
if run_arm base-clean; then
    if [ "$ARM_RC" -eq 0 ]; then
        echo "  ok    passed with the restart in place -- arm 1's wedge is the deleted line."
    else
        echo "  !!BAD the unmutated base shape did not pass (exit=$ARM_RC); arm 1 would be"
        echo "        unattributable." ; printf '%s\n' "$ARM_OUT" | tail -8; fails=$((fails+1))
    fi
else
    fails=$((fails+1))
fi

echo "ARM 1 -- BASE-SHAPED (no guard, restart deleted): expect a WEDGE"
if run_arm base; then
    if [ "$ARM_RC" -eq 124 ] && grep -q '\[ RUN' <<<"$ARM_OUT"; then
        echo "  ok    WEDGED after entering the test (exit 124). This is what the batch removes."
    elif [ "$ARM_RC" -eq 124 ]; then
        echo "  !!BAD timed out WITHOUT entering the test -- not the hang under study"; fails=$((fails+1))
    else
        echo "  !!BAD did not wedge (exit=$ARM_RC) -- the injected defect does not bite here"
        printf '%s\n' "$ARM_OUT" | tail -8; fails=$((fails+1))
    fi
else
    fails=$((fails+1))
fi

echo "ARM 2 -- GUARDED (this batch's shape, same restart deleted): expect a REPORT"
if run_arm guarded; then
    if grep -qF "$REPORT_TAIL$LABEL" <<<"$ARM_OUT"; then
        if [ "$ARM_RC" -ne 0 ] && [ "$ARM_RC" -ne 124 ]; then
            echo "  ok    REPORTED the miss and exited $ARM_RC instead of hanging."
        elif [ "$ARM_RC" -eq 124 ]; then
            echo "  !!BAD reported, then hung anyway"; fails=$((fails+1))
        else
            echo "  !!BAD reported but exited 0 -- a miss must fail the test"; fails=$((fails+1))
        fi
    else
        echo "  !!BAD no miss report (exit=$ARM_RC)"; printf '%s\n' "$ARM_OUT" | tail -8
        fails=$((fails+1))
    fi
else
    fails=$((fails+1))
fi

# Leave the tree as found AND rebuild, so a caller that runs ctest next does not grade a
# mutant binary. The trap restores the source; only the build needs doing here.
restore; trap - EXIT
cmake --build "build/$PRESET" --target "$TARGET" -j 6 >/dev/null 2>&1 ||
    { echo "!! could not rebuild the restored tree" >&2; exit 1; }

if [ "$fails" -ne 0 ]; then
    echo "=== FAILED: $fails of 3 arms" >&2; exit 1
fi
echo "=== PASS: base WEDGES, guarded REPORTS -- the guard converts a hang into a named failure"
