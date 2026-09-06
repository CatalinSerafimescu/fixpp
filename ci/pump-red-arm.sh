#!/usr/bin/env bash
# Force each migrated #289 site's MISS branch and prove it reports.
#
# WHY THIS EXISTS, and why the arms are forced ONE AT A TIME. A migrated site's
# miss branch is dead code under normal execution: the window is preserved, so
# it never misses. "The tests still pass" is therefore evidence about the HIT
# path only, and says nothing about whether the miss branch reports, drains, or
# even compiles into something reachable. The only way to learn that is to force
# the miss VERDICT on purpose and watch for the report -- see the ⚠️ block below;
# forcing does NOT shrink the window, and this sentence said that it did.
#
# ⚠️ ONE ARM AT A TIME IS NOT PEDANTRY -- IT IS THE WHOLE METHOD. PR #316 forced
# all fifteen of its arms in one build and covered SEVEN, because the first miss
# on a code path returns and every later site on that path is never reached. Two
# sites in one helper, or a helper called twice by a driver, mask each other
# exactly this way. This script rebuilds and re-runs per arm for that reason;
# the cost is the point.
#
# ⚠️ AN ARM DOES BOTH: IT ZEROES BOTH DURATIONS *AND* FORCES THE VERDICT. Neither
# alone is sufficient and they fix different defects, so do not drop either:
#   - `((void)run_window_then_ready(<args>), false)` forces the verdict. Without it
#     a site whose future is ALREADY READY when its window opens returns true
#     however small the window is, the miss branch never runs, and this script
#     calls that SILENT -- a WRONG VERDICT against correct code.
#   - zeroing `window` and `grace` stops the call DISPATCHING, so the drain faces a
#     LIVE SUSPENDED FRAME. Without it the real window completes the coroutine at
#     every site that normally completes in-window, and the drain's LIFETIME
#     obligation -- resuming a frame whose inputs may already be dead (#301, #313,
#     #316) -- goes unexercised. Witnessed by `PumpWindowMiss.FeedMissDrainsWhile-
#     CallerTemporaryAlive`, which passes zero for BOTH durations for this reason.
#     Zeroing only the window leaves `grace = kPumpSlice` live and is not enough.
#     ⚠️ It does NOT improve the odds of catching a wrong drain FLAVOUR: that needs a
#     clock-bound frame, and a real window does not advance a mock clock, so such a
#     frame stays suspended either way. An earlier revision of this block said it did.
# The seam obeys the same rule by returning without dispatching; see the ⚠️ block at
# `run_window_then_ready`'s definition in `tests/support/pump_until_ready.hpp`.
#
# ⚠️ MATCH ON THE MESSAGE TAIL, NOT THE BARE LABEL. The drain's residual report
# streams the SAME site label, so grepping the label alone cannot tell a
# window-miss report from a drain-residual report and would go green on either.
# `kWindowMiss` ends "...grace slice. Site: " -- that tail plus the label is what
# identifies this branch and nothing else.
#
# A HANG IS NOT A FAILURE TO REPORT -- it is a different finding, and this script
# keeps them apart. A forced miss can hang instead of reporting when the site's
# pump is INDIRECTED through a helper (census blind spot (c)): the call this
# script forced is not the one the test actually waits on. That is worth knowing
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
#     <TAB> <cmake target> [<TAB> <ctest -R regex> [<TAB> <exec stem>]]
# The regex defaults to `^<cmake target>$`; give it only when they differ.
#
# ⚠️ COLUMN 6 EXISTS BECAUSE A LABEL HAS TWO JOBS AND THEY CAN DIVERGE. The label is
# matched EXACTLY by the forcing seam (`std::strcmp`), and its text before the first
# `/` is ALSO used, by CONTAINMENT, to assert the arm's own test actually ran. Those
# coincide only while labels are named after tests. #289 batch 15 migrated sites that
# live in HELPERS shared by many tests -- `run_sync` has 31 callers -- so there is no
# single test name to put in the label, and every such arm would report INCONCLUSIVE
# for a reason that is about naming rather than about the site. Column 6 supplies the
# gtest-name substring to assert instead; it defaults to the label's stem, so every
# pre-existing arms file keeps its exact previous behaviour.
# The anchor must occur EXACTLY ONCE in the source file and must be on or just
# above the `run_window_then_ready(` call to force.
set -uo pipefail

preset="${1:?usage: pump-red-arm.sh <preset> <arms-file>}"
arms_file="${2:?usage: pump-red-arm.sh <preset> <arms-file>}"

# ⚠️ SELF-TEST, because ci/pump-arm-common.sh states the rule this guard was exempt from:
# "A GUARD THAT HAS NEVER BEEN SEEN TO FIRE IS NOT A GUARD. Both arms are required."
# The arms-file validator above shipped in batch 13 with neither arm, and that exemption is
# exactly how its first draft reached review with a fails-toward-clean hole in it (an empty
# `n_` made `[ "" -eq 0 ]` a bash error, so every row passed silently).
#
# Runs with PUMP_RED_ARM_VALIDATE_ONLY so no arm builds or mutates anything.
#   bash ci/pump-red-arm.sh <preset> --self-test
if [ "$arms_file" = "--self-test" ]; then
    ok=0; bad=0
    st_dir=$(mktemp -d); trap 'rm -rf "$st_dir"' EXIT
    real=$(cd "build/$preset" 2>/dev/null && ctest -N 2>/dev/null \
             | sed -n 's/^ *Test *#[0-9]*: *//p' | head -1)
    [ -n "$real" ] || { echo "pump-red-arm self-test: build/$preset registers no tests" >&2; exit 2; }
    run_st() { PUMP_RED_ARM_VALIDATE_ONLY=1 bash "$0" "$preset" "$1" >"$st_dir/o" 2>&1; echo $?; }
    chk() { # <desc> <want-rc> <file> <must-contain-or-empty>
        local rc; rc=$(run_st "$3")
        if [ "$rc" = "$2" ] && { [ -z "$4" ] || grep -qF "$4" "$st_dir/o"; }; then
            printf '  ok    %-56s rc=%s\n' "$1" "$rc"; ok=$((ok+1))
        else
            printf '  !!BAD %-56s rc=%s (want %s)\n' "$1" "$rc" "$2"; sed 's/^/        /' "$st_dir/o"; bad=$((bad+1))
        fi
    }
    # FIRE: a regex that selects nothing must abort BEFORE any build.
    printf 'tests/x.cpp\tanchor\tL_FIRE\ttgt\t^__no_such_ctest__$\n' > "$st_dir/fire.tsv"
    chk "a non-matching ctest regex FIRES" 2 "$st_dir/fire.tsv" "selects ZERO tests"
    # FIRE: the same row with NO TRAILING NEWLINE must still be seen (`read` skips it without
    # the `|| [ -n "$f_" ]` guard, while the awk check above sees it -- two parsers, one file).
    printf 'tests/x.cpp\tanchor\tL_NOEOL\ttgt\t^__no_such_ctest__$' > "$st_dir/noeol.tsv"
    chk "an UNTERMINATED final row is still validated" 2 "$st_dir/noeol.tsv" "L_NOEOL"
    # QUIET: a row naming a really-registered test must pass validation.
    printf 'tests/x.cpp\tanchor\tL_QUIET\ttgt\t^%s$\n' "$real" > "$st_dir/quiet.tsv"
    chk "a registered ctest name is QUIET" 0 "$st_dir/quiet.tsv" "validated"
    # FIRE: a build dir with no tests must say so, and NOT blame the arms file.
    mkdir -p "$st_dir/empty/build/$preset"
    ( cd "$st_dir/empty" && git init -q . 2>/dev/null
      PUMP_RED_ARM_VALIDATE_ONLY=1 bash "$OLDPWD/$0" "$preset" "$OLDPWD/$st_dir/quiet.tsv" ) \
        >"$st_dir/o" 2>&1; e_rc=$?
    if [ "$e_rc" = 2 ] && grep -q "registers NO tests at all" "$st_dir/o"; then
        printf '  ok    %-56s rc=%s\n' "an EMPTY build dir is not blamed on the arms file" "$e_rc"; ok=$((ok+1))
    else
        printf '  !!BAD %-56s rc=%s\n' "an EMPTY build dir is not blamed on the arms file" "$e_rc"
        sed 's/^/        /' "$st_dir/o"; bad=$((bad+1))
    fi
    # QUIET: a SIX-field row (explicit exec stem) must validate exactly like a five-field
    # one. ⚠️ THIS IS THE ARM FOR THE TWO-PARSERS DEFECT, not a formality: the validation
    # loop reads the row with `read -r`, which packs every leftover field into the LAST
    # name. Before the sixth name was added there, this row's regex arrived as
    # "^<test>$<TAB><stem>", selected zero tests, and the run aborted BLAMING THE ARMS
    # FILE -- a wrong verdict against a correct row. Deleting the `_rest` sink from that `read`
    # line turns this arm rc=0 -> rc=2, which is how it was proven to fire.
    printf 'tests/x.cpp\tanchor\tL_SIX\ttgt\t^%s$\tSomeGtestName\n' "$real" > "$st_dir/six.tsv"
    chk "a SIX-field row (explicit exec stem) is QUIET" 0 "$st_dir/six.tsv" "validated"
    # FIRE: seven fields is malformed -- the upper bound moved 5 -> 6, it did not vanish.
    printf 'tests/x.cpp\tanchor\tL_SEVEN\ttgt\t^%s$\tStem\textra\n' "$real" > "$st_dir/seven.tsv"
    chk "a SEVEN-field row is malformed" 2 "$st_dir/seven.tsv" "malformed row"
    echo "pump-red-arm self-test: $ok ok, $bad bad"
    [ "$bad" -eq 0 ]
    exit $?
fi
repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root" || exit 2
# Shared with ci/pump-seam-arm.sh so the two non-vacuity guards cannot drift apart.
. "$repo_root/ci/pump-arm-common.sh"

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
# `kDrainResidual` from tests/support/pump_until_ready.hpp. The forced branch runs the site's
# DRAIN as well as its report, and a drain that does not quiesce emits this immediately before
# the window-miss report -- on the SAME output line as `Site: <label>`, since neither constant
# contains a newline. Matching only $TAIL folded that into a pass and threw it away.
RESIDUAL='#289: the io_context did not run out of work within the teardown drain'

pass=0; wedged=0; declare -a NOTES=()

# ⚠️ AN EMPTY ARM POPULATION IS NOT A PASS. Without this, a comment-only or empty TSV
# builds nothing, runs nothing, prints "0 RED-as-required, 0 FAILED" and exits 0 -- a
# verification step that cannot fail because it never ran. This repo's most recurring
# defect is exactly that shape.
n_arms=$(awk -F'\t' '!/^#/ && NF' "$arms_file" | wc -l)
assert_nonempty_population "$n_arms" "$arms_file" pump-red-arm arms

# ⚠️ VALIDATE THE ARMS FILE ONCE, UP FRONT. An earlier revision let the execution loop and
# the cleanup sweep disagree about what a row is: the loop skipped only blanks and `#`,
# while the sweep required `NF>=4`. A 3-field row therefore RAN with an empty target and an
# empty regex -- `cmake --build --target ''` then `ctest -R ''`, which matches the WHOLE
# SUITE -- and was then skipped by the cleanup. Two parsers, two acceptance rules, one file.
bad=$(awk -F'\t' '!/^#/ && NF { if (NF < 4 || NF > 6) printf "    line %d: %d field(s)\n", NR, NF }' "$arms_file")
if [ -n "$bad" ]; then
    printf 'pump-red-arm: %s has malformed row(s) -- an arm needs 4 fields, 5 with an explicit ctest regex, or 6 with an explicit exec stem\n' "$arms_file" >&2
    printf '%s\n' "$bad" >&2
    exit 2
fi

# ⚠️ A REGEX THAT SELECTS ZERO TESTS MUST NOT BE READ AS "the miss branch was silent",
# AND UNTIL BATCH 13 IT WAS. `ctest -R <no match>` prints "No tests were found" and exits
# 0, so the arm's output carries no report and the SILENT branch fires -- A WRONG VERDICT
# AGAINST CORRECT CODE, the same failure direction as the SIGPIPE bug further down. It
# cost 4 of 7 arms in batch 13, every one of which had already gone RED under the seam
# driver; the contradiction between two drivers is what exposed it, not reading.
#
# ⚠️ THE DEFAULT `^<target>$` IS THE TRAP, NOT A TYPO -- but not for the reason an earlier
# revision of this comment gave, and the correction matters because that reason was the
# only stated justification for keeping column 5 manual.
#
# WHAT IS TRUE, and it is what makes the default wrong: a cmake TARGET and a registered
# ctest NAME are different strings for 8 of the 10 files this batch touches. Example:
# source `test_019_g2_enablement_witness.cpp` -> target `g2_enablement_witness_019_test`
# -> ctest name `g2_enablement_witness_019`. Derive it, do not trust the ratio:
#   ctest --show-only=json-v1 | jq -r '.tests[]|[.name,(.command[0]|split("/")|last)]|@tsv'
#
# ⚠️ WHAT IS FALSE, twice over, and was written here as "MEASURED": that the target and the
# EXECUTABLE are also independent, and that matching `basename(command[0])` against the
# target "resolves NOTHING". This tree sets no `OUTPUT_NAME` anywhere
# (`grep -rn OUTPUT_NAME tests/ cmake/ CMakeLists.txt` is empty), so target == executable
# basename for every one of those 10 files, and that join resolves ALL of them. The
# derivation the comment called impossible is the one that works. It would map
# target -> ctest name mechanically and could replace column 5; it is not done here only
# because that is a change of shape, not because it cannot be done.
# ⚠️ A third error in the same sentence: it called the SOURCE STEM a "target". The author
# then typed that stem into `cmake --build --target` and got `ninja: error: unknown
# target`. A comment warning about confusable namespaces had the namespaces confused.
# ⚠️ AND A FOURTH: the sentence that used to stand HERE repeated the very claim the
# paragraph above labels FALSE -- that the `basename(command[0])` join "resolves NOTHING".
# It survived the correction because that correction was written above it instead of
# replacing it, so the file asserted a thing and its negation eight lines apart. Found by
# a #289 batch-15 reviewer, deleted rather than reworded: a claim about what a join
# resolves is a RESULT, and the operative conclusion needs none of it.
# So column 5 stays explicit; what changes is that a wrong value is now caught HERE.
#
# ⚠️ CHECKED UP FRONT, FOR EVERY ROW, BEFORE THE FIRST MUTATION -- not per-arm inside the
# loop. Per-arm, a bad row in position 6 is only reported after five rebuild-and-run
# cycles have already spent their time, and the run has already left the tree mutated
# once. An unforced tree is the cheaper state to bail from.
#
# ⚠️ WHAT THIS DOES *NOT* CATCH, stated because "the arms file is validated" would
# overclaim it: a regex selecting the WRONG test still selects something and passes here.
# It bounds the class where the selection is EMPTY, which is the one that reads as clean.
#
# ⚠️ AND THE GUARD ITSELF MUST NOT FAIL TOWARD CLEAN, which the first draft did. It read
# `n_=$(cd "build/$preset" && ctest ... | grep -c ...)` and tested `[ "$n_" -eq 0 ]`. When
# the build directory does not exist the `cd` fails, the substitution yields the EMPTY
# STRING, and `[ "" -eq 0 ]` is a bash *error* that evaluates non-zero -- so every row
# silently passed the check that exists to stop rows passing silently. The build dir is
# now asserted once, and a non-numeric count is a hard failure rather than a false pass.
[ -d "build/$preset" ] || {
    printf 'pump-red-arm: build/%s does not exist -- configure and build it first\n' "$preset" >&2
    exit 2
}
# ⚠️ SEPARATE "this build knows no tests at all" FROM "this ROW names a test that does not
# exist", because the second message is a WRONG CAUSE for the first and sends the reader to
# edit a correct arms file. An unbuilt-but-configured tree selects zero for EVERY row.
all_tests=$(cd "build/$preset" && ctest -N 2>/dev/null | grep -c '^ *Test *#')
if [ "${all_tests:-0}" -eq 0 ]; then
    printf 'pump-red-arm: build/%s registers NO tests at all -- build it first.\n' "$preset" >&2
    printf '  (Not an arms-file defect: every row would select zero here.)\n' >&2
    exit 2
fi
nosuch=""
# ⚠️ `|| [ -n "$f_" ]` IS LOAD-BEARING. `read` returns non-zero on a final line with NO
# TRAILING NEWLINE, so without it the last row is never validated -- while the `awk` check
# 40 lines above DOES see it. That is the exact two-parsers-one-file defect the comment on
# that check describes, and the one `assert_ran_count` in ci/pump-arm-common.sh exists to
# close; reintroducing it one block below would defer the catch until after every build,
# defeating this guard's whole reason to run up front.
# ⚠️ SIX NAMES, NOT FIVE. `read` puts every leftover field into the LAST name, so a
# 5-name read over a 6-column row would hand this validator a regex of
# "<regex><TAB><exec stem>" -- which selects nothing, and would abort the run blaming
# the arms file. That is precisely the two-parsers-one-file defect the comment above
# describes, so the sixth name is read here even though this loop never uses it. It is
# named `_rest`, not after column 6: it is a SINK for everything past the regex, so the
# guard keeps working if a seventh column is ever added.
while IFS=$'\t' read -r f_ a_ l_ tgt_ rx_ _rest || [ -n "$f_" ]; do
    case "$f_" in ''|\#*) continue ;; esac
    rx_="${rx_:-^$tgt_$}"
    n_=$(cd "build/$preset" && ctest -N -R "$rx_" 2>/dev/null | grep -c '^ *Test *#')
    case "$n_" in
        ''|*[!0-9]*)
            printf 'pump-red-arm: could not count tests for %s (got %s) -- refusing to guess\n' \
                   "$l_" "${n_:-<empty>}" >&2
            exit 2 ;;
        0) nosuch+="    $l_ -- \`ctest -R '$rx_'\` selects ZERO tests"$'\n' ;;
    esac
done < "$arms_file"
if [ -n "$nosuch" ]; then
    printf 'pump-red-arm: %s has row(s) whose ctest regex matches nothing:\n' "$arms_file" >&2
    printf '%s' "$nosuch" >&2
    printf '  This is an ARMS-FILE defect, not a site defect. Put the registered ctest name\n' >&2
    printf '  in column 5 -- list them with `ctest -N` in build/%s.\n' "$preset" >&2
    exit 2
fi

# Validation ends here. `PUMP_RED_ARM_VALIDATE_ONLY=1` stops before the first mutation so
# the guards above can be exercised WITHOUT a build -- which is what makes the self-test
# below cheap enough to have both arms. It is a test seam, not a mode for normal use.
[ -n "${PUMP_RED_ARM_VALIDATE_ONLY:-}" ] && { echo "pump-red-arm: arms file validated"; exit 0; }

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
# ⚠️ A FAILED RESTORE MUST BE LOUD. Discarding cp's status means a full disk or an
# unwritable backup dir leaves a FORCED source with no signal at all -- on a script whose own
# header says that content is unrecoverable from git.
restore() {
    local i d
    for i in "${!SNAPPED[@]}"; do
        if [ "${SNAPPED[$i]}" = "$1" ]; then
            d=$(bkp "$i")
            if ! cp "$d" "$1"; then
                echo "    !! RESTORE FAILED for $1 -- it may still hold a forced branch" >&2
                NOTES+=("RESTORE-FAILED $1")
                return 1
            fi
            return 0
        fi
    done
    NOTES+=("NO-SNAPSHOT $1")
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
# ⚠️ A SIGNAL TRAP MUST TERMINATE. Bash RESUMES the script after a trapped INT/TERM
# handler returns, so sharing one handler with EXIT was actively harmful: the handler
# restored the sources and deleted $backup_dir, then the loop carried on to the next arm --
# where `snapshot` skips a path already in SNAPPED and `restore` finds no backup file and
# silently does nothing. The run would then end having FORCED a source with no copy of it
# anywhere, and for the uncommitted migration this script exists to verify, that content is
# unrecoverable from git. Verified: `trap "echo TRAPPED" INT; sleep 5; echo CONTINUED`
# prints BOTH.
trap 'restore_all; rm -rf "$backup_dir"' EXIT
# ⚠️ `exit` FROM THE HANDLER SKIPS THE CLEANUP REBUILD, and restoring sources is only half
# the invariant. The EXIT trap restores every snapshot, but the "rebuild this run's targets"
# loop lives in the main body and never runs -- so an interrupted run leaves CORRECT sources
# beside a binary built from FORCED text. That is exactly the state this repo already
# lost 1h39m to: a later reader measures the forced binary and reports a defect that does not
# exist. Sources are safe, so the remedy is to say so loudly rather than to start a build
# while the user is trying to stop us.
#
# ⚠️ AND NEITHER THAT WARNING NOR THE CLEANUP REBUILD PROTECTS A CONCURRENT READER. Both
# guard the NEXT run; there is no lock. While an arm is between its build and its restore,
# ANY other process measuring `build/<preset>` sees a forced binary with correct sources
# beside it -- a plausible red, in 0 ms, with nothing on screen to say why. Observed during
# #289 batch 14: a reviewer running `ctest -R '^session_pure_tests$' -V` mid-arm got a
# `***Failed` carrying this batch's own miss report and came within one check of filing it.
# The tell was the 0 ms; the check was `pgrep -af pump-red-arm` plus comparing the binary's
# mtime against the source's. On a shared box, do that before believing a red you did not
# start. Stated rather than fixed: a lockfile would have to be honoured by every reader,
# including ones that predate it, so it would buy less than the habit does.
warn_stale_binaries() {
    local t
    echo "pump-red-arm: sources restored, but the cleanup rebuild did NOT run." >&2
    echo "  These targets may still hold a FORCED branch -- rebuild before measuring:" >&2
    for t in $(awk -F'\t' '!/^#/ && NF>=4 {print $4}' "$arms_file" | sort -u); do
        echo "    cmake --build build/$preset --target $t" >&2
    done
}
trap 'echo; echo "pump-red-arm: interrupted"; warn_stale_binaries; exit 130' INT
trap 'echo; echo "pump-red-arm: terminated"; warn_stale_binaries; exit 143' TERM

force_site() {
    # Rewrite the run_window_then_ready call that follows $2 in file $1 into
    # `((void)<the call, with window and grace zeroed>, false)` -- see the ⚠️ block
    # at the top for why BOTH halves are needed.
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
# NOTE: universal newlines. CRLF input is read as LF and written back as LF, so forcing a
# site in a CRLF-line-ended file rewrites the WHOLE file's line endings, not just the call.
# This repo is LF-only, so it is a note rather than a guard -- but a caller in a CRLF repo
# must open with newline="" at both ends.
src = open(path, encoding="utf-8").read()
code = blank_non_code(src)
assert len(code) == len(src), "lexer broke the offset-preserving contract"

# The anchor is matched on the BLANKED copy too: an anchor that occurs only inside a
# comment is not a site, and checking uniqueness against raw text would not notice.
if code.count(anchor) != 1:
    sys.exit(f"anchor not unique in code (raw={src.count(anchor)}, code={code.count(anchor)}): {anchor!r}")
i = code.index(anchor)
# ⚠️ THE NAMESPACE QUALIFIER IS PART OF THE EXPRESSION AND MUST BE INSIDE THE WRAPPER.
# Every migrated site spells `fixpp::test_support::run_window_then_ready(...)`, and
# inserting `((void)` at the bare function name would emit
# `fixpp::test_support::((void)run_window_then_ready(...), false)` -- a compile error, i.e.
# a BUILD-FAILED arm rather than a wrong verdict, but an arm lost all the same.
m = re.compile(r"(?:[A-Za-z_]\w*\s*::\s*)*run_window_then_ready\s*\(").search(code, i)
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

# ⚠️ TWO EDITS, ONE WRITE, IN THIS ORDER. The durations are replaced first (stopping the
# call from dispatching, which is what keeps the awaited coroutine suspended for the
# drain), then the whole call is wrapped (forcing the verdict, which is what works at a
# site that is already ready). Splicing right-to-left keeps every offset valid: `k` and
# `commas[1]` index the ORIGINAL string, so the wrapper's tail must be appended after the
# argument rewrite rather than computed against a shifted buffer.
#
# The zeroed call keeps args 1-2 and DROPS anything after `grace`, including the site
# label. That is deliberate and harmless: the label is a defaulted parameter, and what this
# driver matches is the `ADD_FAILURE` at the site, not the seam's announcement.
#
# `(void)` discards the [[nodiscard]] result; the comma operator then yields false, so the
# caller's `if (!...)` miss branch always runs.
zero = " std::chrono::milliseconds{0}"
forced_call = src[m.start() : commas[1]] + f",{zero},{zero}" + src[k]
spliced = "((void)" + forced_call + ", false)"

# ⚠️ ASSERT THIS DRIVER'S OWN CONTRACT, because nothing else in the tree does. An arm must
# produce BOTH halves -- zeroed durations AND the wrapper -- and each fixes a different
# defect (see the ⚠️ block at the top of this script). `FeedMissDrainsWhileCallerTemporary-
# Alive` pins the zero-duration property AT THE PRIMITIVE, inside its own test body; it
# never goes through this driver. So a future "simplification" of this function back to
# wrap-only, or to zero-only, would pass every check in the repo, and the only symptom
# would be a batch of arms that quietly stopped exercising what they claim to.
#
# ⚠️ CHECKED ON THE BYTES ABOUT TO BE WRITTEN, NOT ON A VARIABLE. Two earlier forms of this
# check were both self-confirming, one level apart:
#   (1) a grep over the rewritten FILE -- satisfied by any pre-existing occurrence elsewhere
#       in the source, including this script's own prose quoted in a comment;
#   (2) asserting on `spliced` itself -- but `spliced` is built as `"((void)" + ... +
#       ", false)"` on the line above, so `startswith`/`endswith` were TAUTOLOGIES. They
#       fire when someone edits the line that BUILDS the string, which is the simplification
#       scenario this exists for, but NOT when someone edits the `write()` call to emit
#       `forced_call` instead. That mutant passed all three assertions.
# The contract is a property of the OUTPUT, so the assertion reads the output slice. And the
# duration check is scoped to the exact inserted span rather than counted over the whole
# region: counting would also see a caller's own ` std::chrono::milliseconds{0}` argument and
# fire spuriously -- exact in both directions is what the paragraph above promises.
out = src[: m.start()] + spliced + src[k + 1 :]
region = out[m.start() : m.start() + len(spliced)]
durations = f",{zero},{zero}"
at = len("((void)") + (commas[1] - m.start())
assert region.startswith("((void)"), "wrapper half missing from the written bytes"
assert region.endswith(", false)"), "forced verdict missing from the written bytes"
assert region[at : at + len(durations)] == durations, (
    f"zeroed durations missing from the written bytes: {region[at : at + len(durations)]!r}"
)

open(path, "w", encoding="utf-8").write(out)
PYFORCE
}

while IFS=$'\t' read -r file anchor label target regex exec_stem; do
    case "$file" in ''|\#*) continue ;; esac
    # Column 5 is optional and defaults to the target's own exact-match regex. It used to be
    # mandatory and was mechanically `^`+target in every row -- two columns carrying one
    # datum, where a typo makes `ctest -R` match NOTHING while the arm still reports a
    # verdict on an empty run.
    regex="${regex:-^$target$}"
    printf '\n=== ARM %s\n    label: %s\n' "$file" "$label"
    # Every row's regex was proven to select at least one test up front, before any
    # mutation -- see the hoisted check above. Nothing re-checks it here.
    snapshot "$file"
    if ! force_site "$file" "$anchor"; then
        echo "    !! FORCE FAILED"; NOTES+=("FORCE-FAILED $label")
        restore "$file"; continue
    fi
    if ! cmake --build "build/$preset" -j "$JOBS" --target "$target" >/tmp/red_build.log 2>&1; then
        echo "    !! BUILD FAILED (see /tmp/red_build.log)"; tail -15 /tmp/red_build.log
        NOTES+=("BUILD-FAILED $label"); restore "$file"; continue
    fi
    # ⚠️ `-V`, NOT `--output-on-failure`, AND THE DIFFERENCE IS A WRONG VERDICT. A gtest
    # `GTEST_SKIP()` test PASSES at the ctest level, and `--output-on-failure` prints nothing
    # for a passing test -- so a skipped arm produced no `$TAIL$label`, fell through to the
    # else, and was reported SILENT ("the miss branch did not announce itself") against
    # correct code, with a diagnostic dump that printed "100% tests passed" underneath it.
    # `ci/pump-seam-arm.sh` calls the same situation NO-SUCH-SITE, which is accurate; the
    # STRONGER witness was the wrong one. Measured on `live_identity_binding` with
    # FIXPP_TLS_FIXTURE_DIR pointed at a nonexistent dir: `--output-on-failure` yields 0
    # occurrences of `SKIPPED`, `-V` yields 3.
    # ⚠️ The header used to tell the reader to "check the arm's ctest output for SKIPPED" --
    # a check that could not fire, inside the paragraph written to catch this failure.
    # [[feedback_every_broken_instrument_in_this_repo_fails_toward_clean]]
    # ⚠️ TWO CONDITIONS THAT MAKE `-V` SAFE, NEITHER OF WHICH BELONGS TO THIS DRIVER.
    # `-V` prints every test's stdout, including tests that PASS -- and this repo has tests
    # whose whole job is to make the drain and miss reports fire while passing
    # (tests/session/test_quiesce_on_exit_residual.cpp, compiled into session_pure_tests,
    # the largest binary any arm here runs). They do not pollute the match because
    # `EXPECT_NONFATAL_FAILURE` installs a reporter that CONSUMES the failure before it
    # reaches stdout. Measured on that binary under `-V`: the report tail appears once (the
    # arm's own) and kDrainResidual zero times, while the three witness suites do run.
    #   (a) That safety is the WRAPPER's, not this driver's. A future witness that reports
    #       outside `EXPECT_NONFATAL_FAILURE` -- or writes to stderr the way the seam's
    #       `kWindowMissForced` announcement does -- WILL land in this output, and the label
    #       narrowing is then the only defence left.
    #   (b) `grep -F "$TAIL$label"` is a SUBSTRING test, so a label that is a strict PREFIX
    #       of another label in the same binary false-matches and reads RED on a site it
    #       never exercised. `new-site-labels.py` surveys this (it checks equality AND
    #       prefix containment); at the time of writing it reports pairs that exist tree-wide
    #       among older bare-stem labels, none of them in one binary with their longer twin.
    #       Re-derive before adding a label that extends an existing one.
    out=$(cd "build/$preset" && timeout "$TIMEOUT_S" ctest -R "$regex" -V 2>&1)
    rc=$?
    if [ "$rc" -eq 124 ]; then
        # ⚠️ A TIMEOUT IS THREE OUTCOMES, NOT ONE, AND COLLAPSING THEM FAILS TOWARD A WRONG
        # VERDICT AGAINST CORRECT CODE. This branch used to print ONE unconditional cause
        # ("the call this arm forced is probably NOT the one the test waits on") and add a
        # failing NOTE. But `$out` holds the partial output, and it decides which case this
        # is -- `ci/pump-seam-arm.sh` has split it three ways since batch 11, whose header
        # records that collapsing them "cost a manual per-test bisect to separate them".
        # The two drivers gave the SAME run OPPOSITE verdicts: a site that reported
        # correctly and then met an unrelated UNMIGRATED `run_for(); ... get()` later in the
        # same test read RED under the seam and INCONCLUSIVE-with-a-wrong-cause here, and
        # only here did it flip the exit status. Latent, not live, when this was written --
        # batch 14 ran 6 arms with 0 timeouts -- but the failure direction is the same one
        # the SIGPIPE note below documents.
        # [[feedback_the_nth_copy_lacks_the_witnesses_its_siblings_have]]
        t_rep=$(grep -cF "$TAIL$label" <<<"$out" || true)
        if [ "$t_rep" -gt 0 ]; then
            echo "    RED* $label -- reported ($t_rep), THEN the run wedged."
            echo "       The miss branch did its job; the wedge is LATER in the run. Look for"
            echo "       an UNMIGRATED run_for/get after this site in the same test."
            echo "       last test started: $(grep -E '^\[ RUN' <<<"$out" | tail -1 | sed 's/^\[ RUN *\] *//')"
            pass=$((pass+1)); wedged=$((wedged+1))
            restore "$file"; continue
        fi
        echo "    ~~ INCONCLUSIVE: timed out after ${TIMEOUT_S}s with NO report for this label."
        echo "       Either the miss block wedged BEFORE reporting (a drain that does not"
        echo "       quiesce), or the call this arm forced is not the one the test waits on"
        echo "       (indirected pump). Either way it is a finding, not a slow box."
        NOTES+=("HUNG $label")
    # ⚠️ HERESTRING, NOT A PIPELINE -- AND THIS WAS A REAL FALSE "SILENT".
    # `set -o pipefail` is on. `grep -q` exits at the FIRST match and closes the pipe, so
    # `printf` dies with SIGPIPE (141) and pipefail makes the PIPELINE non-zero even though
    # the match succeeded. The bug is SIZE-DEPENDENT: with little output printf finishes
    # before grep exits and the pipeline reads 0, which is why batch 10's small arms never
    # saw it. Against a 438-test binary it fired every time, and it fails toward the WRONG
    # VERDICT -- three correctly-reporting sites were called SILENT, i.e. "the miss branch
    # did not announce itself", which would have sent someone to fix migrations that were
    # already right. Reproduce: `set -o pipefail; printf '%s' "$big" | grep -qF x` -> 141.
    # [[feedback_every_broken_instrument_in_this_repo_fails_toward_clean]]
    elif grep -qF "$TAIL$label" <<<"$out"; then
        # ⚠️ THE WINDOW-MISS REPORT IS NOT THE WHOLE VERDICT, and taking it as one made this
        # script unable to see the defect it exists to spot-check. Its own header says the
        # textual driver is what witnesses RECIPE correctness; the recipe includes the drain
        # FLAVOUR, and a `drain_or_report` where the site needed `cancel_and_drain_or_report`
        # leaves the context with work and emits $RESIDUAL -- after which the site still
        # reports the window miss, so the tail matched and the arm read clean.
        # A residual is real output, so requiring its ABSENCE cannot manufacture a false
        # finding; and it is matched WITH the label, because an arm may run a whole binary
        # and another test's residual is not this arm's verdict.
        # ⚠️ THIS CHECK IS NOT COUPLED TO THE ZEROING, though an earlier revision of this
        # comment said it was. A residual needs a frame the drain cannot quiesce, which in
        # this tree means one blocked on a MOCK-CLOCK sleep -- and a real window does not
        # advance a mock clock, so such a frame is still blocked either way. The check works
        # under both forms; it is the zeroing's LIFETIME argument (see the top) that needs
        # non-dispatch, not this.
        # ⚠️ Two herestrings, not a pipeline: `set -o pipefail` plus a pipeline into `grep`
        # is the SIGPIPE trap documented in the branch above.
        resid_lines=$(grep -F "$RESIDUAL" <<<"$out" || true)
        resid=$(grep -F "Site: $label" <<<"$resid_lines" || true)
        if [ -n "$resid" ]; then
            echo "    !! RED, BUT THE DRAIN LEFT A RESIDUAL -- the miss branch reported AND"
            echo "       the drain did not quiesce. What to ask, in order, is written ONCE at"
            echo "       the primitive: see WHAT A RESIDUAL VERDICT MEANS in"
            echo "       tests/support/pump_until_ready.hpp. Do not paraphrase it here."
            printf '%s\n' "$resid" | head -3
            NOTES+=("RESIDUAL $label")
        else
            echo "    RED as required: reported '${TAIL}${label}'"
            pass=$((pass+1))
        fi
    elif true; then
        # ⚠️ ASK WHETHER THE ARM'S OWN TEST RAN. A binary-wide `grep '[  SKIPPED ]'` was the
        # first version of this branch and it MIS-DIAGNOSES A REAL DEFECT: an arm runs a
        # whole binary, so an unrelated test skipping makes a genuinely SILENT site read
        # SKIPPED. Reachable in this tree, measured: `ctest -R '^credentials_rotated_emit$'`
        # runs six tests, of which only `LiveTlsRotationEmitRealFingerprint` can skip, while
        # spotcheck rows 1-2 arm two that cannot. On a box with no readable TLS fixtures a
        # broken row-1 site would have been reported as a missing fixture -- sending the
        # reader to install certs instead of to the migration. The exit code stayed non-zero,
        # so this was a wrong DIAGNOSIS rather than a false green; that is the same direction
        # as the SIGPIPE defect below, which "would have sent someone to fix migrations that
        # were already right".
        #
        # The positive form is also strictly stronger than a skip test: requiring the arm's
        # own test to have STARTED catches "the ctest regex selected the wrong test", which
        # the up-front validator explicitly does not cover -- a regex selecting the WRONG
        # test still selects something and passes there.
        # [[feedback_a_verification_sweep_must_assert_an_execution_count]]
        #
        # The stem is the label text before the first `/`. It is matched by CONTAINMENT, not
        # equality: a label is a READABLE stem and need not be the gtest name (see
        # ci/pump-seam-arm.sh's filter comment -- `W2_StoreWinsDown` lives in
        # `RefreshOnLogon.W2_StoreWinsDown_RED`). Where the stem matches NOTHING the arm is
        # INCONCLUSIVE, not SILENT: this driver then cannot say whether the site ran at all,
        # and guessing would be the failure this branch exists to remove.
        # Column 6 wins where given; otherwise the label's stem, exactly as before.
        stem="${exec_stem:-${label%%/*}}"
        started=$(grep -F '[ RUN' <<<"$out" | grep -cF "$stem" || true)
        skipped_here=$(grep -F '[  SKIPPED ]' <<<"$out" | grep -cF "$stem" || true)
        # ⚠️ ORDER: SKIPPED IS TESTED FIRST, because gtest prints `[ RUN ]` BEFORE the body
        # runs, so a test that reaches `GTEST_SKIP()` HAS a RUN line. An earlier form of this
        # branch asked "did it start?" first and reported a skipped arm as SILENT -- the very
        # mis-diagnosis it was written to remove, inverted. Caught by its own control, which
        # is the only reason it is not in this file: forcing an arm with
        # FIXPP_TLS_FIXTURE_DIR=/nonexistent-dir must print SKIPPED, not "the arm's test RAN".
        if [ "$skipped_here" -gt 0 ]; then
            echo "    ~~ SKIPPED -- this arm's own test did not execute, so it witnesses"
            echo "       NOTHING. Usually a missing/unreadable fixture. NOT a finding about"
            echo "       the site."
            printf '%s\n' "$out" | grep -F '[  SKIPPED ]' | grep -F "$stem" | sed 's/^/         /' | head -3
            NOTES+=("SKIPPED $label")
        elif [ "$started" -eq 0 ]; then
            echo "    ~~ INCONCLUSIVE: no '[ RUN ]' line matched the label stem '$stem', so"
            echo "       this driver cannot say whether the armed site ran. Either the ctest"
            echo "       regex selected the wrong test, or the label stem is not part of the"
            echo "       gtest name. If the site lives in a helper shared by many tests, the"
            echo "       label CANNOT name one test -- give column 6 (exec stem) instead of"
            echo "       renaming the label. Resolve by hand; do NOT read it as SILENT."
            printf '%s\n' "$out" | grep -F '[ RUN' | sed 's/^/         /' | head -5
            NOTES+=("HUNG $label")
        else
            echo "    !! NO REPORT -- the arm's test RAN ($started) and the miss branch did"
            echo "       not announce itself. This IS a finding about the site."
            printf '%s\n' "$out" | grep -iE "window|Site:|FAILED|Passed" | head -8
            NOTES+=("SILENT $label")
        fi
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
    # ⚠️ A FAILED CLEANUP REBUILD IS A FINDING, NOT A SHRUG. Unchecked, the script exits 0
    # with every arm RED while the target binary still holds the forced branch -- and the
    # next reader measures that binary. It goes in NOTES so it also flips the exit code.
    if ! cmake --build "build/$preset" -j "$JOBS" --target "$t" >/dev/null 2>&1; then
        echo "  !! CLEANUP REBUILD FAILED for $t -- its binary may still hold a forced branch"
        NOTES+=("CLEANUP-REBUILD-FAILED $t")
    fi
done

echo
# The two counters this used to keep were derivable from NOTES and had to be updated in
# lockstep with it at four sites; a sixth arm category added to one and forgotten in the
# other would have silently changed the exit code. Derive the split from the tags instead.
hung=$(printf '%s\n' "${NOTES[@]-}" | grep -c '^HUNG ' || true)
skipped=$(printf '%s\n' "${NOTES[@]-}" | grep -c '^SKIPPED ' || true)
# ⚠️ ASSERT AN EXECUTION COUNT. Three parsers read this file -- two `awk`s and the `read`
# loop -- and they disagree on a row with NO TRAILING NEWLINE: `awk` counts it, `read` returns
# non-zero so the loop body never runs. A 1-arm file without a final newline therefore
# satisfied the non-vacuity guard, ran nothing, and exited 0 -- defeating that guard on its
# own terms. Patching `read` closes this instance; counting what actually executed closes the
# class, which is the repo's standing rule for verification sweeps.
# OUTCOME-derived on purpose: an arm that ran but recorded no verdict is caught here and
# would not be by a loop counter. See assert_ran_count's comment.
ran=$(( pass + ${#NOTES[@]} ))
assert_ran_count "$ran" "$n_arms" pump-red-arm "arm(s)"
# ⚠️ `wedged` AND `skipped` ARE ON THIS LINE ON PURPOSE. A summary reading "N
# RED-as-required, 0 FAILED" while an arm wedged, or while an arm never ran, is the
# fails-toward-clean shape -- which is why ci/pump-seam-arm.sh prints its own `wedged`.
echo "arms: ${pass} RED-as-required (of which ${wedged} reported THEN WEDGED), \
$(( ${#NOTES[@]} - hung - skipped )) FAILED, ${hung} INCONCLUSIVE, ${skipped} SKIPPED"
if (( ${#NOTES[@]} )); then printf '  - %s\n' "${NOTES[@]}"; fi
[ "${#NOTES[@]}" -eq 0 ]
