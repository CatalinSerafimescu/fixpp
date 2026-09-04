#!/usr/bin/env bash
# Force each migrated #289 site's MISS branch and prove it reports.
#
# WHY THIS EXISTS, and why the arms are forced ONE AT A TIME. A migrated site's
# miss branch is dead code under normal execution: the window is preserved, so
# it never misses. "The tests still pass" is therefore evidence about the HIT
# path only, and says nothing about whether the miss branch reports, drains, or
# even compiles into something reachable. The only way to learn that is to make
# the window miss on purpose and watch for the report.
#
# ⚠️ ONE ARM AT A TIME IS NOT PEDANTRY -- IT IS THE WHOLE METHOD. PR #316 forced
# all fifteen of its arms in one build and covered SEVEN, because the first miss
# on a code path returns and every later site on that path is never reached. Two
# sites in one helper, or a helper called twice by a driver, mask each other
# exactly this way. This script rebuilds and re-runs per arm for that reason;
# the cost is the point.
#
# ⚠️ ZERO BOTH DURATIONS. `run_window_then_ready(ioc, fut, window, grace)`
# defaults `grace` to `kPumpSlice`, so zeroing only the window leaves a live
# grace slice that will usually still satisfy the future -- a vacuous arm that
# passes without ever taking the branch it claims to test.
#
# ⚠️ MATCH ON THE MESSAGE TAIL, NOT THE BARE LABEL. The drain's residual report
# streams the SAME site label, so grepping the label alone cannot tell a
# window-miss report from a drain-residual report and would go green on either.
# `kWindowMiss` ends "...grace slice. Site: " -- that tail plus the label is what
# identifies this branch and nothing else.
#
# A HANG IS NOT A FAILURE TO REPORT -- it is a different finding, and this script
# keeps them apart. A forced miss can hang instead of reporting when the site's
# pump is INDIRECTED through a helper (census blind spot (c)): the run window this
# script zeroed is not the one the test actually waits on. That is worth knowing
# per-arm rather than being averaged into a pass/fail, so a timeout is reported
# as INCONCLUSIVE and named.
#
# ⚠️ THIS SCRIPT IS AN INSTRUMENT. A run of N REDs proves it RAN; it does not prove
# it can tell a reporting site from a silent one. Before believing a batch's REDs,
# SEED A FAILURE and require the driver to catch it -- the procedure, not last
# run's result, because a pasted result rots and a procedure cannot:
#
#   1. pick any arm in the batch's .tsv
#   2. delete the `ADD_FAILURE()` line at that site (leave the drain and the
#      `return` -- the point is a site that MISSES SILENTLY, not one that fails
#      to compile)
#   3. run this script with only that arm
#   4. require the report to be `SILENT`, not RED and not INCONCLUSIVE
#   5. restore the site and re-run to confirm it goes back to RED
#
# Step 5 matters as much as step 4: a driver that reports SILENT unconditionally
# also passes step 4.
#
# Usage:  bash ci/pump-red-arm.sh <preset> <arms-file>
#
# arms-file: one arm per line, TAB-separated, '#' comments and blanks ignored
#     <source-path> <TAB> <unique anchor substring> <TAB> <expected label>
#     <TAB> <cmake target> <TAB> <ctest -R regex>
# The anchor must occur EXACTLY ONCE in the source file and must be on or just
# above the `run_window_then_ready(` call to force.
set -uo pipefail

preset="${1:?usage: pump-red-arm.sh <preset> <arms-file>}"
arms_file="${2:?usage: pump-red-arm.sh <preset> <arms-file>}"
repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root" || exit 2

# Observation timeout. DERIVED, not picked: the primitives grant a bounded pump
# budget (`kPumpBudget` 10s) and a drain quiescence budget (`kQuiesceBudget` 5s),
# so a correct forced miss finishes well inside ~15s of legitimate waiting even
# on a loaded box. 180s sits far above that -- slow is never mistaken for hung --
# and far below "never", so an indirected pump is caught rather than waited on.
# If those constants change, change this with them.
TIMEOUT_S="${PUMP_RED_ARM_TIMEOUT:-180}"
TAIL='grace slice. Site: '

pass=0; failed=0; inconclusive=0; declare -a NOTES=()

# ⚠️ RESTORE FROM A BYTE COPY, NEVER `git checkout -- <file>`. The sources this
# script forces are normally MODIFIED IN THE WORKING TREE -- the migration being
# verified is not committed yet, which is the whole reason it is being verified.
# `git checkout --` would restore them to HEAD, silently deleting that migration
# and leaving every subsequent arm forcing a file that no longer has a miss
# branch to force. The arms would then all report SILENT, and the obvious reading
# of that is "the migration is broken" rather than "the harness ate it".
backup_dir="$(mktemp -d)"
trap 'rm -rf "$backup_dir"' EXIT
snapshot() {
    local dest="$backup_dir/$(printf '%s' "$1" | tr / _)"
    [ -f "$dest" ] || cp "$1" "$dest"
}
restore() { cp "$backup_dir/$(printf '%s' "$1" | tr / _)" "$1"; }

force_site() {
    # Rewrite the run_window_then_ready call that follows $2 in file $1 so both
    # its window and its grace are zero.
    python3 - "$1" "$2" <<'PY'
import re, sys
path, anchor = sys.argv[1], sys.argv[2]
src = open(path, encoding="utf-8").read()
if src.count(anchor) != 1:
    sys.exit(f"anchor not unique ({src.count(anchor)} hits): {anchor!r}")
i = src.index(anchor)
m = re.compile(r"run_window_then_ready\s*\(").search(src, i)
if not m:
    sys.exit(f"no run_window_then_ready after anchor in {path}")
# balance parens from the opening one
k, depth = m.end() - 1, 0
while k < len(src):
    if src[k] == "(": depth += 1
    elif src[k] == ")":
        depth -= 1
        if depth == 0: break
    k += 1
else:
    sys.exit("unbalanced call")
inner = src[m.end():k]
# args split at depth 0
parts, d, cur = [], 0, ""
for ch in inner:
    if ch in "(<[": d += 1
    elif ch in ")>]": d -= 1
    if ch == "," and d == 0:
        parts.append(cur); cur = ""
    else:
        cur += ch
parts.append(cur)
if len(parts) < 3:
    sys.exit(f"expected >=3 args, got {len(parts)}: {inner!r}")
forced = f"{parts[0]},{parts[1]}, std::chrono::milliseconds{{0}}, std::chrono::milliseconds{{0}}"
open(path, "w", encoding="utf-8").write(src[:m.end()] + forced + src[k:])
PY
}

while IFS=$'\t' read -r file anchor label target regex; do
    case "$file" in ''|\#*) continue ;; esac
    printf '\n=== ARM %s\n    label: %s\n' "$file" "$label"
    snapshot "$file"
    if ! force_site "$file" "$anchor"; then
        echo "    !! FORCE FAILED"; failed=$((failed+1)); NOTES+=("FORCE-FAILED $label")
        restore "$file"; continue
    fi
    if ! cmake --build "build/$preset" --target "$target" >/tmp/red_build.log 2>&1; then
        echo "    !! BUILD FAILED (see /tmp/red_build.log)"; tail -15 /tmp/red_build.log
        failed=$((failed+1)); NOTES+=("BUILD-FAILED $label"); restore "$file"; continue
    fi
    out=$(cd "build/$preset" && timeout "$TIMEOUT_S" ctest -R "$regex" --output-on-failure 2>&1)
    rc=$?
    if [ "$rc" -eq 124 ]; then
        echo "    ~~ INCONCLUSIVE: timed out after ${TIMEOUT_S}s -- the pump this arm zeroed is"
        echo "       probably NOT the one the test waits on (indirected pump)."
        inconclusive=$((inconclusive+1)); NOTES+=("HUNG $label")
    elif printf '%s' "$out" | grep -qF "$TAIL$label"; then
        echo "    RED as required: reported '${TAIL}${label}'"
        pass=$((pass+1))
    else
        echo "    !! NO REPORT -- the miss branch did not announce itself"
        printf '%s\n' "$out" | grep -iE "window|Site:|FAILED|Passed" | head -12
        failed=$((failed+1)); NOTES+=("SILENT $label")
    fi
    restore "$file"
done < "$arms_file"

# Leave no forced source behind: a restored tree with stale binaries is how a
# later reader measures the forced state and reports phantom failures.
echo
echo "rebuilding to clear forced binaries..."
cmake --build "build/$preset" >/dev/null 2>&1

echo
echo "arms: ${pass} RED-as-required, ${failed} FAILED, ${inconclusive} INCONCLUSIVE"
for n in "${NOTES[@]:-}"; do [ -n "$n" ] && echo "  - $n"; done
[ "$failed" -eq 0 ] && [ "$inconclusive" -eq 0 ]
