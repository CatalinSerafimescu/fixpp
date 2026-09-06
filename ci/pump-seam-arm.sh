#!/usr/bin/env bash
# ci/pump-seam-arm.sh — force a #289 miss branch through the RUNTIME SEAM.
#
# The companion to ci/pump-red-arm.sh, which forces by rewriting source and
# rebuilding ONCE PER SITE. This one exports FIXPP_FORCE_WINDOW_MISS=<label> and
# runs the already-built binary, so a whole batch costs one build and N runs.
#
# ⚠️ IT IS A WEAKER WITNESS AND DOES NOT REPLACE THE TEXTUAL DRIVER. Use this for
# breadth and ci/pump-red-arm.sh to spot-check recipe correctness.
#
# ⚠️ DO NOT RESTATE HERE WHAT FORCING DOES AND DOES NOT EXERCISE. That statement
# belongs with the seam, at `run_window_then_ready` in
# tests/support/pump_until_ready.hpp, and it has been WRONG IN BOTH DIRECTIONS in
# the past -- a copy of it here is a third place to keep true and the one nobody
# updates. Read it there.
#
# ⚠️ SILENCE HAS TWO CAUSES AND ONE OF THEM FAILS TOWARD CLEAN. A run with no
# `kWindowMiss` report can mean the miss branch did not report, or that the label
# matched nothing at all — a typo, a site that passes no label, or a site the run
# never reached. So the primitive ANNOUNCES on stderr before it pumps
# (`kWindowMissForced`), and this script requires that line. No announcement is
# reported as NO-SUCH-SITE, never as a pass.
#
# ⚠️ A LABEL IS LOCATED BY `strings` ON THE BINARY, NOT BY A SOURCE GREP. A stale
# binary is this procedure's only silent failure mode and it fails toward clean:
# a binary built before the migration contains no label, and asking the SOURCE
# whether the label exists would answer yes while the binary that runs answers no.
# [[feedback_a_distinctive_phrase_grep_misses_the_bound_seam]]
#
# Usage:  bash ci/pump-seam-arm.sh <preset> <labels-file>
#
# labels-file: one site label per line; '#' comments and blank lines ignored.
set -euo pipefail

preset="${1:?usage: pump-seam-arm.sh <preset> <labels-file>}"
labels_file="${2:?usage: pump-seam-arm.sh <preset> <labels-file> | <preset> --self-test}"

# ── --self-test: exercise the NO-SUCH-SITE verdict path ──────────────────────
# ⚠️ THE INERTNESS CONTROL BELOW DOES NOT COVER THIS. It proves the seam stays quiet for a
# label it was not given; it never produces a NO-SUCH-SITE verdict, so both of that verdict's
# paths (empty candidate set, and announced-nothing) were exercised by nothing at all. This
# re-invokes the driver on a label no binary can carry and requires exactly that verdict.
if [ "$labels_file" = "--self-test" ]; then
    st_tmp="$(mktemp)"; trap 'rm -f "$st_tmp"' EXIT
    printf '%s\n' '# synthetic: no binary carries this' 'PumpSeamArm::__self_test_absent__' > "$st_tmp"
    # ⚠️ `set -e` is on and the nested run is EXPECTED to exit non-zero (that is half of what
    # this control asserts), so the status must be captured with `||`, never left to `$?`
    # after a bare assignment -- that form aborts the script before anything prints.
    st_rc=0
    st_out=$(PUMP_SEAM_ARM_SELFTEST=1 "$0" "$preset" "$st_tmp" 2>&1) || st_rc=$?
    ok=0; bad=0
    if grep -q "NO-SUCH-SITE=1" <<<"$st_out"; then
        echo "  ok    an absent label reads NO-SUCH-SITE=1"; ok=$((ok+1))
    else
        echo "  !!BAD an absent label did NOT read NO-SUCH-SITE=1"; bad=$((bad+1))
        printf '%s\n' "$st_out" | sed 's/^/        | /' | tail -6
    fi
    if [ "$st_rc" -ne 0 ]; then
        echo "  ok    the run exits non-zero when not every label went RED (exit=$st_rc)"; ok=$((ok+1))
    else
        echo "  !!BAD the run exited 0 despite a non-RED verdict"; bad=$((bad+1))
    fi
    echo "pump-seam-arm self-test: ${ok} ok, ${bad} bad"
    [ "$bad" -eq 0 ]; exit $?
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
# The two non-vacuity guards live in one place for both drivers; the rationale is the
# part that must not be duplicated. Run `bash ci/pump-arm-common.sh --self-test`.
. "$repo_root/ci/pump-arm-common.sh"
bindir="build/$preset/bin"
[ -d "$bindir" ] || { echo "pump-seam-arm: no $bindir -- build first" >&2; exit 2; }

# ⚠️ DERIVE THIS FROM THE COMPETING QUANTITY, DO NOT TAKE A ROUND DEFAULT. A forced miss
# is NOT fast: every miss runs the primitive's drain, bounded by `kQuiesceBudget` = 5 s, and
# one label can be forced once per test that reaches it. Batch 11 measured a single label
# forced 48 times legitimately, so a flat 180 s admits only ~36 misses and turns a healthy
# arm into a false timeout -- which is what it did, three times.
# 600 s = 120 forced misses at the full drain budget, which is comfortably above the
# measured maximum. Raise it if a batch's max forced count approaches that.
TIMEOUT_S="${PUMP_SEAM_ARM_TIMEOUT:-600}"

ANNOUNCE='#289 FORCED window miss at site: '
# Match a miss-report TAIL plus the label, never the bare label: the drain's
# residual report streams the same label and would count as a miss report.
#
# TWO tails, because the seam now fires at BOTH primitives and they report different
# text -- `kWindowMiss` for `run_window_then_ready`, `kPumpBudgetMiss` for `pump_until*`.
# A driver matching only the first would read every budget site as SILENT, which is this
# script's fails-toward-clean verdict rather than an error.
#
# Two `-F` patterns rather than one regex alternation because both tails contain a `.`
# that a regex would match as any character, and `grep -F` removes the question entirely.
# ⚠️ An earlier revision justified this with "`rg` returns 0 matches for any `|`
# alternation". That is a claim about a DIFFERENT tool -- this line calls `grep` -- and it
# does not hold here now either (measured: `rg` 14.1.1, an alternation, exit 0, matches
# printed). A borrowed caveat is not a reason.
# ⚠️ MATCHING BOTH TAILS IS ALSO THIS DRIVER'S BLIND SPOT, and it runs the opposite way
# from every other asymmetry recorded here. Accepting either constant means a site that
# calls `run_window_then_ready` but reports `kPumpBudgetMiss` -- a plausible copy-paste
# between the two recipes -- reads RED here and SILENT under `ci/pump-red-arm.sh`, which
# matches only the window tail. There the TEXTUAL driver is right and this one fails toward
# clean. Not fixed, because narrowing the match by primitive needs the label->primitive map
# this driver deliberately does not keep (it locates labels by `strings` over binaries).
# The check that closes it is `new-site-labels.py`'s occurrence control plus reading the
# call; #289 batch 14 resolved all 31 of its sites that way and found no mismatch.
REPORT_TAIL='grace slice. Site: '
REPORT_TAIL2='bounded-pump budget. Site: '
# `kDrainResidual` from tests/support/pump_until_ready.hpp -- the SAME string
# `ci/pump-red-arm.sh` matches. Mirrored here deliberately rather than left to that driver.
#
# ⚠️ WHY IT HAS TO BE IN *THIS* DRIVER TOO, which it was not until batch 13 and which was a
# coverage gap that batch INTRODUCED. Before it, every seam-forceable site was also
# rewritable by `ci/pump-red-arm.sh`, so a drain that failed to quiesce was caught there and
# this driver's blindness cost nothing. Batch 13 added three `pump_until_ready` sites that
# the textual driver CANNOT rewrite -- it mutates `run_window_then_ready` calls -- so the
# seam became their ONLY driver. And a residual is exactly what one of them measured: the
# `engine.stop()`-then-drain remedy at `G2EnablementWitness/send_nos` exists because forcing
# it emitted this string. Without the check, a regression of that remedy reports plain RED
# (the miss branch still announces and still reports) and says nothing about the condition
# the remedy was written to remove.
# ⚠️ Requiring a residual's ABSENCE cannot manufacture a false finding -- a residual is real
# output -- and it is matched WITH the label, because an arm may run a whole binary and
# another test's residual is not this arm's verdict.
RESIDUAL='#289: the io_context did not run out of work within the teardown drain'

mapfile -t LABELS < <(grep -vE '^\s*(#|$)' "$labels_file" || true)
assert_nonempty_population "${#LABELS[@]}" "$labels_file" pump-seam-arm labels

red=0; silent=0; nosite=0; inconclusive=0; ran=0; wedged=0; residual=0
declare -a NOTES=()

# ── Index label -> binaries with ONE `strings` pass per binary ───────────────
# Asking `strings | grep` per (label, binary) is O(labels x binaries) process
# spawns -- ~8k for this batch. `grep -oF -f` prints each matching substring, so a
# single pass per binary yields exactly the labels that binary carries.
# Substring, not whole-line: the linker may suffix-merge string literals, so a label
# can share a `strings` line with a longer one.
tmp_labels="$(mktemp)"; trap 'rm -f "$tmp_labels"' EXIT
printf '%s\n' "${LABELS[@]}" > "$tmp_labels"
# ⚠️ PARALLEL, because this is ~10 GB of `strings` over ~339 binaries -- measured at ~2m10s
# serially, and it is pure independent reads. Each worker prints "<label>\t<binary>" lines;
# the reducer below is what mutates state, so there is no shared-state hazard.
declare -A BINS_FOR=()
scan_one() {
    b="$1"
    [ -x "$b" ] && [ -f "$b" ] || exit 0
    strings -a "$b" 2>/dev/null | grep -oF -f "$PUMP_LABELS_FILE" 2>/dev/null | sort -u |
        while IFS= read -r hit; do [ -n "$hit" ] && printf '%s\t%s\n' "$hit" "$b"; done
    exit 0
}
export -f scan_one
export PUMP_LABELS_FILE="$tmp_labels"
while IFS=$'\t' read -r hit b; do
    [ -n "$hit" ] || continue
    BINS_FOR["$hit"]="${BINS_FOR["$hit"]-} $b"
done < <(printf '%s\n' "$bindir"/* | xargs -r -P "$(nproc)" -I{} bash -c 'scan_one "$@"' _ {} || true)

run_label() {                      # $1 = label
    local label="$1" bins=() b out ann rep timed_out
    read -r -a bins <<<"${BINS_FOR["$label"]-}"
    if [ "${#bins[@]}" -eq 0 ]; then
        printf '    !!   NO BINARY carries %s -- stale build, or the label never shipped\n' "$label"
        NOTES+=("$label: absent from every binary")
        nosite=$((nosite + 1)); return 1
    fi
    ann=0; rep=0; timed_out=0; resid=''
    for b in "${bins[@]}"; do
        # A forced binary EXITS NON-ZERO by construction (its tests fail), so the
        # exit status must be captured, not tested by `if` -- `$?` after an `if`
        # is the `if`'s status, which is always 0 and would hide every timeout.
        # ⚠️ SCOPE THE RUN, THEN FALL BACK -- never let the filter decide NO-SUCH-SITE.
        # A `Fixture::phase` label names a gtest FIXTURE, so `--gtest_filter=Fixture.*` runs
        # the ~5 tests that can reach the site instead of the binary's ~438 (measured 3.0s vs
        # 35.8s on session_pure_tests). But a helper CAN be reached from another fixture (a
        # base class, a free function), and a filter that misses would report NO-SUCH-SITE --
        # a WRONG verdict dressed as a finding. So a filtered run that sees no announcement is
        # retried unfiltered, and only then believed. The fast path stays fast; the slow path
        # is only paid where the guess was wrong.
        # ⚠️ STRIP THE `/phase` SUFFIX BEFORE GUESSING, AND LEAVE THE STEM OPEN-ENDED.
        # A TEST-body label is `<case-stem>/<phase>`, and a `/` appears in a gtest name
        # only for a PARAMETERISED test -- so `*.Stem/phase` matched nothing and every
        # such arm took the unfiltered fallback. The trailing `*` is the second half and
        # is not optional: a label is a READABLE stem, not the gtest name, so
        # `W2_StoreWinsDown` must still reach `RefreshOnLogon.W2_StoreWinsDown_RED`, and
        # `gtest_filter` has no implicit trailing wildcard. Over-matching costs a few
        # extra tests in one binary; under-matching costs the whole fallback run.
        case "$label" in
            *::*) filt="${label%%::*}.*" ;;
            *)    filt="*.${label%%/*}*" ;;   # a TEST-body label is <case-stem>/<phase>
        esac
        rc=0
        out=$(FIXPP_FORCE_WINDOW_MISS="$label" timeout "$TIMEOUT_S" "$b" \
                  --gtest_filter="$filt" 2>&1) || rc=$?
        if [ "$rc" -ne 124 ] && ! grep -qF "$ANNOUNCE$label" <<<"$out"; then
            rc=0
            out=$(FIXPP_FORCE_WINDOW_MISS="$label" timeout "$TIMEOUT_S" "$b" 2>&1) || rc=$?
        fi
        if [ "$rc" -eq 124 ]; then
            # ⚠️ A TIMEOUT MUST NOT DISCARD THE OUTPUT. The partial output already says
            # whether the miss branch REPORTED before the process wedged, and those are two
            # different findings:
            #   reported, then hung -> the ARM is RED. The hang is somewhere AFTER the site,
            #       and in this batch it was always the same thing: an UNMIGRATED
            #       `run_for(); get()` later in the same test, which is exactly the #289
            #       hazard -- a missed window plus an unconditional get() is a wedge. That
            #       is evidence FOR the remaining migration, not against this site.
            #   never reported     -> genuinely inconclusive, in one of two ways the
            #       printout keeps apart by the ANNOUNCE count: announced-then-hung means
            #       the site's own miss block wedged before reporting, usually a drain
            #       that does not quiesce; never-announced means the label did not fire in
            #       this binary at all, so the wedge belongs to something else entirely
            #       (census blind spot (c)).
            # An earlier revision collapsed both into INCONCLUSIVE and cost a manual
            # per-test bisect to separate them.
            local t_ann t_rep
            t_ann=$(grep -cF "$ANNOUNCE$label" <<<"$out" || true)
            t_rep=$(grep -cF -e "$REPORT_TAIL$label" -e "$REPORT_TAIL2$label" <<<"$out" || true)
            if [ "$t_ann" -gt 0 ] && [ "$t_rep" -gt 0 ]; then
                printf '    RED* %-46s forced %2d  reported %2d  THEN HUNG in %s\n' \
                    "$label" "$t_ann" "$t_rep" "$(basename "$b")"
                printf '         the miss branch REPORTED; the wedge is later in the run.\n'
                printf '         last test started: %s\n' \
                    "$(grep -E '^\[ RUN' <<<"$out" | tail -1 | sed 's/^\[ RUN *\] *//')"
                NOTES+=("$label: reported, then the run wedged in $(basename "$b") -- \
look for an UNMIGRATED run_for/get after this site")
                red=$((red + 1)); wedged=$((wedged + 1)); return 0
            fi
            printf '    ~~   INCONCLUSIVE: %s timed out in %s with NO report (announced %s)\n' \
                "$label" "$(basename "$b")" "$t_ann"
            printf '         announced>0 -> the miss block wedged before reporting (a drain that\n'
            printf '           does not quiesce); announced=0 -> the label never fired here, so\n'
            printf '           the wedge is not this arm (census blind spot (c)).\n'
            printf '         Either way it is a finding, not a slow box.\n'
            # ⚠️ DO NOT RETURN -- a timeout in ONE binary must not decide the LABEL's verdict.
            # Binaries are found by SUBSTRING on `strings`, so a shorter label can pull in a
            # binary that merely CONTAINS it inside a longer one (`RejectFixture::feed` is a
            # suffix of `BusinessRejectFixture::feed`). Such a binary passes no seam label, can
            # never announce, and if it wedges first an earlier revision reported INCONCLUSIVE
            # for an arm whose real binary would have gone RED. Record it and keep going; the
            # verdict is decided below, after every candidate has had its turn.
            timed_out=$((timed_out + 1))
            NOTES+=("$label: TIMEOUT with no report in $(basename "$b")")
            continue
        fi
        ann=$((ann + $(grep -cF "$ANNOUNCE$label" <<<"$out" || true)))
        rep=$((rep + $(grep -cF -e "$REPORT_TAIL$label" -e "$REPORT_TAIL2$label" <<<"$out" || true)))
        # Two herestrings, not a pipeline: `set -o pipefail` plus a pipeline into `grep` is
        # the SIGPIPE trap this repo has already paid for once.
        resid_all=$(grep -F "$RESIDUAL" <<<"$out" || true)
        resid="$resid$(grep -F "Site: $label" <<<"$resid_all" || true)"
    done
    if [ "$ann" -eq 0 ]; then
        if [ "$timed_out" -gt 0 ]; then
            printf '    ~~   INCONCLUSIVE: %s never announced, and %d candidate binary/ies wedged\n' \
                "$label" "$timed_out"
            inconclusive=$((inconclusive + 1)); return 1
        fi
        printf '    !!   NO-SUCH-SITE: %s never fired (label typo, or site unreached)\n' "$label"
        NOTES+=("$label: no announcement")
        nosite=$((nosite + 1)); return 1
    fi
    if [ "$rep" -eq 0 ]; then
        printf '    !!   SILENT: %s forced %dx but its miss branch reported nothing\n' "$label" "$ann"
        NOTES+=("$label: forced but silent")
        silent=$((silent + 1)); return 1
    fi
    # ⚠️ PROVEN TO FIRE, and not by reading. This check is not in the `--self-test` below
    # because exercising it needs a SOURCE MUTATION and a rebuild, which that cheap
    # no-build self-test cannot do. It was proven by a paired control instead: delete the
    # `engine.stop()`-then-drain remedy at `G2EnablementWitness/send_nos` ONLY, rebuild
    # target `g2_enablement_witness_019_test`, and run both g2 labels. Expected and
    # measured: `RED=1 ... RESIDUAL=1 of 2` -- the mutated site demoted, its untouched
    # sibling still RED. Re-derive it that way; the recipe is the evidence, the numbers rot.
    if [ -n "$resid" ]; then
        printf '    !!   RED, BUT THE DRAIN LEFT A RESIDUAL: %s reported AND did not quiesce.\n' "$label"
        echo   "         What to ask, in order, is written ONCE at the primitive: see"
        echo   "         WHAT A RESIDUAL VERDICT MEANS in tests/support/pump_until_ready.hpp."
        printf '%s\n' "$resid" | head -3
        NOTES+=("$label: RESIDUAL")
        residual=$((residual + 1)); return 1
    fi
    if [ "$timed_out" -gt 0 ]; then
        # RED, but a candidate binary wedged on the way. Count it: the summary line is the
        # only place a wedge is visible, and "RED=N of N" with a hidden wedge is the
        # fails-toward-clean shape this driver exists to avoid.
        printf '    RED  %-46s forced %2d  reported %2d  (%d candidate binary/ies wedged)\n' \
            "$label" "$ann" "$rep" "$timed_out"
        red=$((red + 1)); wedged=$((wedged + 1)); return 0
    fi
    printf '    RED  %-46s forced %2d  reported %2d\n' "$label" "$ann" "$rep"
    red=$((red + 1)); return 0
}

echo "=== pump-seam-arm: ${#LABELS[@]} label(s), preset $preset"
for label in "${LABELS[@]}"; do
    run_label "$label" || true
done

# ⚠️ THE NEGATIVE CONTROL IS NOT OPTIONAL. Without it, a seam that announces
# UNCONDITIONALLY would report RED for everything, which is indistinguishable from a real
# result. What follows proves the seam is INERT for a label it was not given -- it does NOT
# produce a NO-SUCH-SITE verdict, and an earlier revision of this comment claimed it did,
# describing an arm that had already been replaced. See `--self-test` for the control that
# does exercise the NO-SUCH-SITE path.
echo
echo "=== negative control: a REAL binary, a label it does not carry, MUST stay silent"
# ⚠️ THIS CONTROL MUST EXECUTE A BINARY. An earlier revision "controlled" the NO-SUCH-SITE
# verdict by forcing a made-up label -- but `BINS_FOR` is keyed only from the labels file, so
# that label provably has no binaries and `run_label` returned WITHOUT RUNNING ANYTHING. It
# therefore proved only that a lookup in an empty map is empty. It could not detect the two
# failure modes that matter: an env var that never reaches the binary (everything would read
# NO-SUCH-SITE) or a seam that announces unconditionally (everything would read RED).
#
# So: take a binary this run already proved carries a real label, run it with a label it does
# NOT carry, and require ZERO announcements from a run that otherwise passes. Paired with the
# RED arms above (env var matches -> announcement), that is both directions of the seam.
probe_bin=""; probe_filter=""
for l in "${LABELS[@]}"; do
    read -r -a _pb <<<"${BINS_FOR["$l"]-}"
    if [ "${#_pb[@]}" -gt 0 ]; then
        probe_bin="${_pb[0]}"
        case "$l" in *::*) probe_filter="${l%%::*}.*" ;; *) probe_filter="*" ;; esac
        break
    fi
done
if [ -z "$probe_bin" ]; then
    if [ -n "${PUMP_SEAM_ARM_SELFTEST:-}" ]; then
        # The self-test deliberately supplies a label no binary carries, so there is nothing
        # to probe with. Skipping is correct HERE and only here.
        echo "    --   inertness control skipped (self-test supplies no real label)"
        probe_bin=""
    else
        echo "    !!   no binary carried ANY label -- the negative control cannot run" >&2
        exit 4
    fi
fi
if [ -n "$probe_bin" ]; then
nc_rc=0
nc_out=$(FIXPP_FORCE_WINDOW_MISS="PumpSeamArm::__no_such_site__" timeout "$TIMEOUT_S" \
             "$probe_bin" --gtest_filter="$probe_filter" 2>&1) || nc_rc=$?
nc_ann=$(grep -cF "$ANNOUNCE" <<<"$nc_out" || true)
if [ "$nc_rc" -ne 0 ] || [ "$nc_ann" -ne 0 ]; then
    printf '    !!BAD NEGATIVE CONTROL: %s exit=%s announcements=%s (want exit=0, 0)\n' \
        "$(basename "$probe_bin")" "$nc_rc" "$nc_ann" >&2
    echo "         a non-matching label must force NOTHING; the seam may be announcing" >&2
    echo "         unconditionally, which would make every RED above meaningless." >&2
    exit 4
fi
printf '    ok   NEGATIVE CONTROL: %s ran clean with a non-matching label, 0 announcements\n' \
    "$(basename "$probe_bin")"
fi

# ⚠️ DERIVED FROM OUTCOMES, NOT COUNTED IN THE LOOP. An earlier revision incremented `ran`
# at the top of the loop body, which made this assertion an IDENTITY -- `ran` was
# `${#LABELS[@]}` by construction and the check could not fail. Summing the verdicts instead
# means a label that falls out of `run_label` through some path that records nothing makes
# the totals disagree, which is the whole point. Same reasoning as ci/pump-red-arm.sh's
# `pass + ${#NOTES[@]}`. [[feedback_a_verification_sweep_must_assert_an_execution_count]]
#
# ⚠️ EVERY TERMINAL VERDICT MUST APPEAR IN THIS SUM. Adding a category and forgetting it
# here does not under-report that category -- it makes the ASSERTION fire and the whole run
# refuse to report, which is loud but blames the wrong thing ("a row with no trailing
# newline is the usual cause"). MEASURED, not hypothetical: `residual` was added in batch 13
# and omitted here, and the first mutant that produced one turned a correct RESIDUAL verdict
# into "parsed 2 label(s) but EXECUTED 1". Failing loudly for the wrong reason is still a
# wrong diagnosis; the sum is the list of ways `run_label` can end, and it has to be
# complete. Derive it from the function, not from memory:
#   grep -n 'return [01]$' ci/pump-seam-arm.sh
ran=$(( red + silent + nosite + inconclusive + residual ))
assert_ran_count "$ran" "${#LABELS[@]}" pump-seam-arm "label(s)"

echo
# ⚠️ THE WEDGE COUNT IS ON THE SUMMARY LINE ON PURPOSE. A RED* arm PASSES -- the branch it
# tests reported -- so without this the only trace of a wedged run is a note, and a summary
# that reads "RED=N of N" with a hidden wedge is the fails-toward-clean shape.
# ⚠️ EVERY NON-RED CATEGORY IS ON THIS LINE, because the summary is the only place a
# reader looks. A counter that exists but is not printed is the fails-toward-clean shape --
# "RED=N of N" would be true while an arm was demoted for a residual.
echo "=== summary: RED=$red (of which $wedged reported THEN WEDGED) SILENT=$silent" \
     "RESIDUAL=$residual NO-SUCH-SITE=$nosite INCONCLUSIVE=$inconclusive of ${#LABELS[@]}"
if [ "${#NOTES[@]}" -gt 0 ]; then
    printf '  note: %s\n' "${NOTES[@]}"
fi
[ "$red" -eq "${#LABELS[@]}" ] || exit 1
