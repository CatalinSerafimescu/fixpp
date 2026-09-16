#!/usr/bin/env bash
# CI-side (Tier 1, #411): shrink a leg's ccache store to what THIS run used,
# so the archive ci/seed-ccache.sh publishes afterwards is this run's live
# object set rather than everything since the last cleanup.
#
#   ci/trim-ccache-to-run.sh <label>
#
#   label — the preset (e.g. linux-clang-debug). Used only to label this run's
#           disposition line — this script calls no API and does not need it
#           to find anything.
#   env   — CCACHE_DIR.
#
# ── WHY ──────────────────────────────────────────────────────────────────────
#
# A store only ever GROWS to its `max-size`: objects from earlier commits stay
# until ccache's own cleanup, so a leg's store reads as "full" whether or not
# the build still uses what is in it. `max-size` is a ceiling, not a measure of
# need — lowering it below the live set is the 500 MB thrash the workflow
# comment records.
#
# This step answers that without guessing a size: evict every cache file this
# run did not touch. The caller runs it after the build and BEFORE the seed
# step, so the trimmed store is what gets published.
#
# History: Tier 1 first ran this against `hendrikmuhs/ccache-action` entries in
# the shared Actions cache (PR #460). An earlier version there also deleted the
# superseded entry before the action had saved its replacement (Gate B round 1,
# F1) and was removed. Measured on run 35113493891, the trim alone did not bring
# two generations under the 10 GB cap, so the legs moved to GHCR.
#
# ── WHICH FILES THIS RUN TOUCHED ─────────────────────────────────────────────
#
# ci/restore-ccache.sh runs `ccache --zero-stats` at the end of its restore, so
# the counters' `stats_zeroed_timestamp` is the restore time. ccache refreshes a
# file's mtime on every hit, and a restored file keeps the mtime it was
# archived with, so `--evict-older-than <now - zeroed>` keeps exactly the files
# hit or written since the restore. The caller runs this step only after every
# earlier step succeeded, so a failed build never publishes a truncated store.
#
# Fail direction: anything that makes the timestamp unreadable, suggests the
# counters were zeroed AFTER the build (zero calls counted), or puts the
# zeroed timestamp at or after now (clock stepped back, or an unreadable
# clock reading), SKIPS eviction. A skipped eviction publishes the old superset —
# larger, never colder.
#
# Residual (disclosed, not guarded): a backward clock step that leaves
# `now > zeroed` can still evict files touched during the stepped-back
# window. The wall clock cannot detect this; there is no heuristic for it.
#
# ── WHAT NEVER REDDENS ───────────────────────────────────────────────────────
#
# A compiler cache that is down must never redden a lane whose build and tests
# passed: every ccache failure is a `::warning::` and exit 0. Only a WIRING
# error (missing argument or environment) exits non-zero, because a mis-wired
# call would otherwise no-op on every run while looking green.
#
# Verify on any Tier 1 run (a PR run's trimmed store is not published): this
# step's `ccache-evict:` line (MiB before -> after), then the seed step's
# archive size on a push to main.
#
# ⚠️ `set -uo pipefail` WITHOUT `-e` — every failure path is dispositioned.
set -uo pipefail

KEY="${1-}"
if [ -z "$KEY" ] || [ -z "${CCACHE_DIR-}" ]; then
  echo "::error::usage: CCACHE_DIR=<dir> ci/trim-ccache-to-run.sh <label> — got key='${KEY}' CCACHE_DIR='${CCACHE_DIR-}'"
  exit 2
fi

note() { echo "$1"; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }
mib()  { du -sm "$CCACHE_DIR" 2>/dev/null | cut -f1; }

# ── evict what this run did not touch ────────────────────────────────────────
if ! stats="$(ccache --print-stats 2>/dev/null)"; then
  echo "::warning::\`ccache --print-stats\` failed; the unevicted store will be saved."
  note "ccache-evict (${KEY}): FAILED — \`ccache --print-stats\` failed, so the restore time is unreadable; the unevicted store will be saved."
  exit 0
fi
zeroed="$(printf '%s\n' "$stats" | awk -F'\t' '$1 == "stats_zeroed_timestamp" { print $2 }')"
calls="$(printf '%s\n' "$stats" | awk -F'\t' '
  $1 == "direct_cache_hit" || $1 == "preprocessed_cache_hit" || $1 == "cache_miss" { n += $2 }
  END { print n + 0 }')"

case "$zeroed" in
  ''|*[!0-9]*|0)
    note "ccache-evict (${KEY}): SKIPPED — no stats_zeroed_timestamp in \`ccache --print-stats\`, so the restore time is unknown; the unevicted store will be saved." ;;
  *)
    if [ "$calls" -eq 0 ]; then
      note "ccache-evict (${KEY}): SKIPPED — zero compiler calls counted since the counters were zeroed, so they were not zeroed at restore; the unevicted store will be saved."
    else
      now="$(date +%s 2>/dev/null)"
      case "$now" in
        ''|*[!0-9]*)
          note "ccache-evict (${KEY}): SKIPPED — the clock is unreadable ('${now}'); the unevicted store will be saved." ;;
        *)
          if [ "$zeroed" -ge "$now" ]; then
            note "ccache-evict (${KEY}): SKIPPED — stats_zeroed_timestamp ${zeroed} is not before now ${now} (clock stepped back?); evicting would drop files this run used; the unevicted store will be saved."
          else
            age=$(( now - zeroed + 1 ))
            before="$(mib)"
            if ccache --evict-older-than "${age}s" >/dev/null 2>&1; then
              after="$(mib)"
              note "ccache-evict (${KEY}): kept files touched in the last ${age}s (since restore, ${calls} calls) — ${before:-?} MiB -> ${after:-?} MiB"
            else
              echo "::warning::\`ccache --evict-older-than ${age}s\` failed; the unevicted store will be saved."
              note "ccache-evict (${KEY}): FAILED — unevicted store will be saved."
            fi
          fi ;;
      esac
    fi ;;
esac

exit 0
