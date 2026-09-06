#!/usr/bin/env bash
# ci/red-arms/batch17-genuine-miss.sh -- the ONE arm that justifies #289 batch 17.
#
# WHY A FORCED ARM IS NOT ENOUGH HERE. `ci/pump-seam-arm.sh` proves the miss BRANCH runs
# and reports. It cannot prove the branch is REACHABLE by anything other than the seam --
# and if it is not, 162 guards buy nothing. This script forces the branch the way a real
# defect would, on a real site, and shows the two trees diverge:
#
#   BASE  (guard absent, `ioc.run(); fut.get();`)  -> HANGS. Killed by `timeout`.
#   BATCH (guard present)                          -> REPORTS `kRunMiss` and exits non-zero.
#
# THE INJECTED DEFECT is a deleted `ioc.restart()`, which is failure mode (b) in
# `run_to_exhaustion_or_report`'s header: these tests reuse ONE io_context across several
# co_spawn/run pairs and hand-write the restart between them. `run()` on a context still
# stopped from the previous `run()` returns IMMEDIATELY having dispatched nothing, so the
# future is never ready and the `get()` below it never returns. One line.
#
# ⚠️ THE PASS CONDITION IS THE PAIR, NOT EITHER HALF. A batch-tree report alone does not
# say the old code was worse; a base-tree hang alone does not say the guard catches it.
# ⚠️ AND THE BASE ARM MUST HANG FOR THE RIGHT REASON. It is checked for a `timeout` exit
# (124) AND for having printed the test's `[ RUN      ]` line first -- a binary that
# crashed at startup would also fail to finish, and would read as a hang.
set -uo pipefail
PRESET="${1:-linux-clang-debug}"
BIN="build/$PRESET/bin/session_pure_tests"
FILE="tests/session/test_session_plaintext_factory_mismatch.cpp"
CASE="PlaintextFactoryMismatch.Cell_a_PlaintextProfileWithTlsOverrideRejects"
# The restart that arms the SECOND pair in that test. Its own line, no other statement.
DEFECT='    ioc.restart();'
TIMEOUT_S="${TIMEOUT_S:-25}"

[ -f "$BIN" ] || { echo "no such binary: $BIN -- build the preset first" >&2; exit 2; }
orig=$(mktemp); cp "$FILE" "$orig"
restore() { cp "$orig" "$FILE"; rm -f "$orig"; }
trap restore EXIT

# The line to delete: the FIRST `ioc.restart();` inside that test body.
start=$(grep -n "TEST(PlaintextFactoryMismatch, Cell_a_" "$FILE" | cut -d: -f1)
[ -n "$start" ] || { echo "anchor not found" >&2; exit 2; }
line=$(awk -v s="$start" 'NR>s && $0=="'"$DEFECT"'" {print NR; exit}' "$FILE")
[ -n "$line" ] || { echo "no restart() to delete after line $start" >&2; exit 2; }
echo "injecting: delete $FILE:$line  ($DEFECT)"

python3 - "$FILE" "$line" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); L = p.read_text().split('\n')
i = int(sys.argv[2]) - 1
assert L[i].strip() == 'ioc.restart();', L[i]
del L[i]
p.write_text('\n'.join(L))
PY

before=$(sha256sum "$BIN" | cut -d' ' -f1)
cmake --build "build/$PRESET" --target session_pure_tests -j 6 >/dev/null 2>&1 || {
    echo "!! build failed after injection" >&2; exit 1; }
after=$(sha256sum "$BIN" | cut -d' ' -f1)
[ "$before" != "$after" ] || { echo "!! binary unchanged -- the mutation did not reach it" >&2; exit 1; }

rc=0
out=$(timeout "$TIMEOUT_S" "$BIN" --gtest_filter="$CASE" 2>&1) || rc=$?
echo "--- exit=$rc"
printf '%s\n' "$out" | tail -20

if [ "$rc" -eq 124 ]; then
    if grep -q '\[ RUN' <<<"$out"; then
        echo "=> BASE-SHAPED: the test WEDGED (no guard). This is what the batch removes."
    else
        echo "!! timed out WITHOUT reaching the test -- not the hang under study" >&2; exit 1
    fi
elif grep -qF 'would have blocked forever. Site: ' <<<"$out"; then
    echo "=> GUARDED: the miss REPORTED instead of wedging, exit=$rc (non-zero expected)."
    [ "$rc" -ne 0 ] || { echo "!! reported but exited 0" >&2; exit 1; }
else
    echo "!! neither wedged nor reported -- the injected defect did not bite" >&2; exit 1
fi
