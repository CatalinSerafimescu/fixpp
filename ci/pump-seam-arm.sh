#!/usr/bin/env bash
# ci/pump-seam-arm.sh — force a #289 miss branch through the RUNTIME SEAM.
#
# The companion to ci/pump-red-arm.sh, which forces by rewriting source and
# rebuilding ONCE PER SITE. This one exports FIXPP_FORCE_WINDOW_MISS=<label> and
# runs the already-built binary, so a whole batch costs one build and N runs.
#
# ⚠️ IT IS A STRICTLY WEAKER WITNESS AND DOES NOT REPLACE THE TEXTUAL DRIVER.
# It exercises the PRIMITIVE's forced path under a site's label. It cannot see a
# site whose own miss block has the wrong drain flavour or a missing `return`,
# because the block it runs is the same one at every site. Use this for breadth
# and ci/pump-red-arm.sh to spot-check recipe correctness.
#
# ⚠️ SILENCE HAS TWO CAUSES AND ONE OF THEM FAILS TOWARD CLEAN. A run with no
# `kWindowMiss` report can mean the miss branch did not report, or that the label
# matched nothing at all — a typo, a site that passes no label, or a site the run
# never reached. So the primitive ANNOUNCES on stderr before zeroing the window
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
labels_file="${2:?usage: pump-seam-arm.sh <preset> <labels-file>}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
# The two non-vacuity guards live in one place for both drivers; the rationale is the
# part that must not be duplicated. Run `bash ci/pump-arm-common.sh --self-test`.
. "$repo_root/ci/pump-arm-common.sh"
bindir="build/$preset/bin"
[ -d "$bindir" ] || { echo "pump-seam-arm: no $bindir -- build first" >&2; exit 2; }

# A forced miss makes a test fail fast, so a slow arm means a HANG, not a slow box.
# Derived from the primitive's own drain budget (kQuiesceBudget = 5 s) with room
# for a suite of them, NOT taken as a round default.
TIMEOUT_S="${PUMP_SEAM_ARM_TIMEOUT:-180}"

ANNOUNCE='#289 FORCED window miss at site: '
# Match the kWindowMiss TAIL plus the label, never the bare label: the drain's
# residual report streams the same label and would count as a miss report.
REPORT_TAIL='grace slice. Site: '

mapfile -t LABELS < <(grep -vE '^\s*(#|$)' "$labels_file" || true)
assert_nonempty_population "${#LABELS[@]}" "$labels_file" pump-seam-arm labels

red=0; silent=0; nosite=0; inconclusive=0; ran=0
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

run_label() {                      # $1 = label, $2 = "expect-red" | "expect-nosite"
    local label="$1" mode="$2" bins=() b out ann rep
    read -r -a bins <<<"${BINS_FOR["$label"]-}"
    if [ "${#bins[@]}" -eq 0 ]; then
        if [ "$mode" = "expect-nosite" ]; then
            printf '    ok   NEGATIVE CONTROL: %s is in no binary\n' "$label"
            return 0
        fi
        printf '    !!   NO BINARY carries %s -- stale build, or the label never shipped\n' "$label"
        NOTES+=("$label: absent from every binary")
        nosite=$((nosite + 1)); return 1
    fi
    ann=0; rep=0
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
        case "$label" in
            *::*) filt="${label%%::*}.*" ;;
            *)    filt="*.$label" ;;          # a TEST-body label is the CASE name
        esac
        rc=0
        out=$(FIXPP_FORCE_WINDOW_MISS="$label" timeout "$TIMEOUT_S" "$b" \
                  --gtest_filter="$filt" 2>&1) || rc=$?
        if [ "$rc" -ne 124 ] && ! grep -qF "$ANNOUNCE$label" <<<"$out"; then
            rc=0
            out=$(FIXPP_FORCE_WINDOW_MISS="$label" timeout "$TIMEOUT_S" "$b" 2>&1) || rc=$?
        fi
        if [ "$rc" -eq 124 ]; then
            printf '    ~~   INCONCLUSIVE: %s timed out in %s\n' "$label" "$(basename "$b")"
            printf '         a forced miss HANGS when the pump is indirected through a helper\n'
            printf '         (census blind spot (c)) -- that is a finding, not a slow box.\n'
            NOTES+=("$label: TIMEOUT in $(basename "$b")")
            inconclusive=$((inconclusive + 1)); return 1
        fi
        ann=$((ann + $(grep -cF "$ANNOUNCE$label" <<<"$out" || true)))
        rep=$((rep + $(grep -cF "$REPORT_TAIL$label" <<<"$out" || true)))
    done
    if [ "$ann" -eq 0 ]; then
        printf '    !!   NO-SUCH-SITE: %s never fired (label typo, or site unreached)\n' "$label"
        NOTES+=("$label: no announcement")
        nosite=$((nosite + 1)); return 1
    fi
    if [ "$rep" -eq 0 ]; then
        printf '    !!   SILENT: %s forced %dx but its miss branch reported nothing\n' "$label" "$ann"
        NOTES+=("$label: forced but silent")
        silent=$((silent + 1)); return 1
    fi
    printf '    RED  %-46s forced %2d  reported %2d\n' "$label" "$ann" "$rep"
    red=$((red + 1)); return 0
}

echo "=== pump-seam-arm: ${#LABELS[@]} label(s), preset $preset"
for label in "${LABELS[@]}"; do
    ran=$((ran + 1))
    run_label "$label" expect-red || true
done

# ⚠️ THE NEGATIVE CONTROL IS NOT OPTIONAL. Without it, a driver whose env var never
# reaches the binary reports NO-SUCH-SITE for everything and a driver that always
# announces reports RED for everything; both are indistinguishable from a real
# result. This arm proves the NO-SUCH-SITE verdict is reachable.
echo
echo "=== negative control: a label no site carries MUST read NO-SUCH-SITE"
# No counter bookkeeping is needed here: `BINS_FOR` is keyed ONLY from the labels file, so
# this literal never has bins, and `run_label`'s expect-nosite arm returns before touching
# `$nosite`. An earlier revision guarded that with a before/after decrement -- unreachable
# by construction, and unreachable guards are the kind of code that later reads as evidence
# of a hazard that does not exist.
run_label "PumpSeamArm::__no_such_site__" expect-nosite || true

# `ran` is incremented at the top of the loop -- a DIRECT count, deliberately not the
# outcome-derived form ci/pump-red-arm.sh uses. See assert_ran_count's comment.
assert_ran_count "$ran" "${#LABELS[@]}" pump-seam-arm "label(s)"

echo
echo "=== summary: RED=$red SILENT=$silent NO-SUCH-SITE=$nosite INCONCLUSIVE=$inconclusive of ${#LABELS[@]}"
if [ "${#NOTES[@]}" -gt 0 ]; then
    printf '  note: %s\n' "${NOTES[@]}"
fi
[ "$red" -eq "${#LABELS[@]}" ] || exit 1
