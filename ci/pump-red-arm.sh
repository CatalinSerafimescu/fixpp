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
#     <TAB> <cmake target> [<TAB> <ctest -R regex>]
# The regex defaults to `^<cmake target>$`; give it only when they differ.
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

# ⚠️ CAP THE BUILD PARALLELISM. `cmake --build` with no -j uses every core, and
# this script issues one build PER ARM, so a long arms file is a sustained
# all-core clang load. A 23 GB box was pushed into the OOM killer by exactly
# that, which killed the driver mid-arm -- and a driver killed mid-arm is the
# one state that can leave a FORCED source and forced binaries behind for the
# next reader to measure. Modest and finishing beats fast and killed.
JOBS="${PUMP_RED_ARM_JOBS:-4}"
TAIL='grace slice. Site: '

pass=0; declare -a NOTES=()

# ⚠️ VALIDATE THE ARMS FILE ONCE, UP FRONT. An earlier revision let the execution loop and
# the cleanup sweep disagree about what a row is: the loop skipped only blanks and `#`,
# while the sweep required `NF>=4`. A 3-field row therefore RAN with an empty target and an
# empty regex -- `cmake --build --target ''` then `ctest -R ''`, which matches the WHOLE
# SUITE -- and was then skipped by the cleanup. Two parsers, two acceptance rules, one file.
bad=$(awk -F'\t' '!/^#/ && NF { if (NF < 4 || NF > 5) printf "    line %d: %d field(s)\n", NR, NF }' "$arms_file")
if [ -n "$bad" ]; then
    printf 'pump-red-arm: %s has malformed row(s) -- an arm needs 4 fields, or 5 with an explicit ctest regex\n' "$arms_file" >&2
    printf '%s\n' "$bad" >&2
    exit 2
fi

# ⚠️ RESTORE FROM A BYTE COPY, NEVER `git checkout -- <file>`. The sources this
# script forces are normally MODIFIED IN THE WORKING TREE -- the migration being
# verified is not committed yet, which is the whole reason it is being verified.
# `git checkout --` would restore them to HEAD, silently deleting that migration
# and leaving every subsequent arm forcing a file that no longer has a miss
# branch to force. The arms would then all report SILENT, and the obvious reading
# of that is "the migration is broken" rather than "the harness ate it".
backup_dir="$(mktemp -d)"
declare -a SNAPPED=()          # original paths, parallel to $backup_dir/N
bkp() { printf '%s/%s' "$backup_dir" "$1"; }

snapshot() {
    local i
    for i in "${!SNAPPED[@]}"; do [ "${SNAPPED[$i]}" = "$1" ] && return 0; done
    SNAPPED+=("$1")
    cp "$1" "$(bkp $(( ${#SNAPPED[@]} - 1 )))"
}
restore() {
    local i
    for i in "${!SNAPPED[@]}"; do
        [ "${SNAPPED[$i]}" = "$1" ] && { cp "$(bkp "$i")" "$1"; return 0; }
    done
    return 1
}

# ⚠️ BACKUPS ARE INDEXED, NOT NAME-MANGLED, AND THE ORIGINAL PATH IS KEPT VERBATIM. The
# obvious encoding -- flatten `a/b.hpp` to `a_b.hpp` -- is LOSSY and cannot be inverted,
# because real paths contain underscores: restoring `group_dispatch_fixture.hpp` that way
# reconstructs `group/dispatch/fixture.hpp` and silently restores NOTHING. Caught before it
# ran; it would have left every forced source in the tree while reporting success.
#
# ⚠️ RESTORE EVERY SNAPSHOT ON THE WAY OUT, NOT ONLY ON THE LOOP'S PATHS. An earlier
# revision restored inside the loop body while the trap merely removed the backup
# directory, so any exit skipping the loop's tail -- Ctrl-C, SIGTERM, `set -u` on a typo --
# left a FORCED source for the next reader to measure. This does NOT cover SIGKILL, which
# is untrappable and is exactly how this script died twice to the OOM killer; that is why
# the parallelism cap above is load-bearing rather than a nicety.
restore_all() {
    local i
    for i in "${!SNAPPED[@]}"; do cp "$(bkp "$i")" "${SNAPPED[$i]}" 2>/dev/null || true; done
}
trap 'restore_all; rm -rf "$backup_dir"' EXIT INT TERM

force_site() {
    # Rewrite the run_window_then_ready call that follows $2 in file $1 so both
    # its window and its grace are zero.
    #
    # ⚠️ SEARCHES A BLANKED COPY, SPLICES THE ORIGINAL. #289's migrated files quote
    # `run_window_then_ready(` inside their explanatory comments, so a raw-text search can
    # land on PROSE. The arm would then build unforced, its test would pass, and this
    # script would report SILENT -- indistinguishable from a miss branch that does not
    # report. `blank_non_code` is offset-preserving, so an offset found on the blanked copy
    # indexes the same byte in the original.
    FIXPP_CI_DIR="$repo_root/ci" python3 - "$1" "$2" <<'PYFORCE'
import os, re, sys
sys.path.insert(0, os.environ["FIXPP_CI_DIR"])
from cxx_blank import blank_non_code

path, anchor = sys.argv[1], sys.argv[2]
src = open(path, encoding="utf-8").read()
code = blank_non_code(src)
assert len(code) == len(src), "lexer broke the offset-preserving contract"

# The anchor is matched on the BLANKED copy too: an anchor that occurs only inside a
# comment is not a site, and checking uniqueness against raw text would not notice.
if code.count(anchor) != 1:
    sys.exit(f"anchor not unique in code (raw={src.count(anchor)}, code={code.count(anchor)}): {anchor!r}")
i = code.index(anchor)
m = re.compile(r"run_window_then_ready\s*\(").search(code, i)
if not m:
    sys.exit(f"no run_window_then_ready after anchor in {path}")

# One scan: balance to the closing paren, recording top-level comma offsets. Only the
# SECOND is needed (the boundary between `fut` and `window`); an earlier revision rebuilt
# every argument as a string and then discarded all but two.
depth, commas, k = 0, [], m.end() - 1
while k < len(code):
    c = code[k]
    if c in "([{":
        depth += 1
    elif c in ")]}":
        depth -= 1
        if depth == 0:
            break
    elif c == "," and depth == 1:
        commas.append(k)
    k += 1
else:
    sys.exit(f"unbalanced run_window_then_ready call in {path}")
if len(commas) < 2:
    sys.exit(f"expected >=3 args to run_window_then_ready in {path}, found {len(commas) + 1}")

zero = " std::chrono::milliseconds{0}"
open(path, "w", encoding="utf-8").write(src[:commas[1]] + f",{zero},{zero}" + src[k:])
PYFORCE
}

while IFS=$'\t' read -r file anchor label target regex; do
    case "$file" in ''|\#*) continue ;; esac
    # Column 5 is optional and defaults to the target's own exact-match regex. It used to be
    # mandatory and was mechanically `^`+target in every row -- two columns carrying one
    # datum, where a typo makes `ctest -R` match NOTHING while the arm still reports a
    # verdict on an empty run.
    regex="${regex:-^$target$}"
    printf '\n=== ARM %s\n    label: %s\n' "$file" "$label"
    snapshot "$file"
    if ! force_site "$file" "$anchor"; then
        echo "    !! FORCE FAILED"; NOTES+=("FORCE-FAILED $label")
        restore "$file"; continue
    fi
    if ! cmake --build "build/$preset" -j "$JOBS" --target "$target" >/tmp/red_build.log 2>&1; then
        echo "    !! BUILD FAILED (see /tmp/red_build.log)"; tail -15 /tmp/red_build.log
        NOTES+=("BUILD-FAILED $label"); restore "$file"; continue
    fi
    out=$(cd "build/$preset" && timeout "$TIMEOUT_S" ctest -R "$regex" --output-on-failure 2>&1)
    rc=$?
    if [ "$rc" -eq 124 ]; then
        echo "    ~~ INCONCLUSIVE: timed out after ${TIMEOUT_S}s -- the pump this arm zeroed is"
        echo "       probably NOT the one the test waits on (indirected pump)."
        NOTES+=("HUNG $label")
    elif printf '%s' "$out" | grep -qF "$TAIL$label"; then
        echo "    RED as required: reported '${TAIL}${label}'"
        pass=$((pass+1))
    else
        echo "    !! NO REPORT -- the miss branch did not announce itself"
        printf '%s\n' "$out" | grep -iE "window|Site:|FAILED|Passed" | head -12
        NOTES+=("SILENT $label")
    fi
    restore "$file"
done < "$arms_file"

# Leave no forced source behind: a restored tree with stale binaries is how a
# later reader measures the forced state and reports phantom failures.
#
# ⚠️ REBUILD ONLY THE TARGETS THIS RUN FORCED, not the whole tree. A full
# `cmake --build` here is minutes of all-core clang for no benefit -- the arms
# only ever touched their own targets -- and on a shared box it is antisocial
# enough to get the whole driver OOM-killed mid-arm, which is the one outcome
# that CAN strand a forced source. Measured: two consecutive kills that way.
#
# ⚠️ WHAT THIS DOES NOT COVER, stated because the gap is real: a forced HEADER is
# seen by every target that includes it, not only by the arm's target. Those other
# targets keep objects built from the forced text until something rebuilds them.
# That is harmless for this script (it never measures them) but NOT harmless for a
# full `ctest` run afterwards. Rebuild the tree before any whole-suite measurement
# that follows an arms run.
echo
echo "rebuilding this run's targets to clear forced binaries..."
for t in $(awk -F'\t' '!/^#/ && NF>=4 {print $4}' "$arms_file" | sort -u); do
    cmake --build "build/$preset" -j "$JOBS" --target "$t" >/dev/null 2>&1
done

echo
# The two counters this used to keep were derivable from NOTES and had to be updated in
# lockstep with it at four sites; a sixth arm category added to one and forgotten in the
# other would have silently changed the exit code. Derive the split from the tags instead.
hung=$(printf '%s\n' "${NOTES[@]-}" | grep -c '^HUNG ' || true)
echo "arms: ${pass} RED-as-required, $(( ${#NOTES[@]} - hung )) FAILED, ${hung} INCONCLUSIVE"
if (( ${#NOTES[@]} )); then printf '  - %s\n' "${NOTES[@]}"; fi
[ "${#NOTES[@]}" -eq 0 ]
