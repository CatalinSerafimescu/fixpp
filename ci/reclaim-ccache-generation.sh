#!/usr/bin/env bash
# CI-side (Tier 1, #411): shrink a `hendrikmuhs/ccache-action` entry to what THIS
# run used, and delete the entry it supersedes BEFORE the action's post step
# saves the replacement.
#
#   ci/reclaim-ccache-generation.sh <action-key>
#
#   action-key — the action's `key:` input, verbatim (e.g. tier1-linux-clang-debug).
#                The action stores `ccache-<action-key>-<ISO8601>`.
#   env        — GH_TOKEN (actions: write), REPO (owner/name), REF (github.ref),
#                CCACHE_DIR.
#
# ── WHY ──────────────────────────────────────────────────────────────────────
#
# All Tier 1 ccache entries share the repository's Actions-cache cap. Two things
# pushed one generation past it (#411):
#
#  1. A store only ever GROWS to its `max-size`. Objects from earlier commits
#     stay until ccache's own cleanup, so a leg's entry reads as "full" whether
#     or not the build still uses what is in it. `max-size` is a ceiling, not a
#     measure of need — lowering it below the live set is the 500 MB thrash the
#     workflow comment records.
#  2. cache-cleanup.yml reclaims a superseded key only when the WHOLE TIER
#     completes, so for the length of a `main` run the old and new generations
#     are both resident.
#
# Step 1 below answers (1) without guessing a size: evict every cache file this
# run did not touch. Step 2 answers (2) per leg, at the latest point a normal
# step can run — the action's save is a POST step, so no step can follow it.
#
# ── STEP 1: WHICH FILES THIS RUN TOUCHED ─────────────────────────────────────
#
# ccache-action runs `ccache -z` right after its restore, so the counters'
# `stats_zeroed_timestamp` is the restore time. ccache refreshes a file's mtime
# on every hit, and a restored file keeps the mtime it was archived with, so
# `--evict-older-than <now - zeroed>` keeps exactly the files hit or written
# since the restore. (This is the action's own `evict-old-files: job` rule; it
# is done here instead because the action applies it on EVERY save — including
# a cancelled or failed run's, which would publish a truncated store. The caller
# runs this step only after every earlier step succeeded.)
#
# Fail direction: anything that makes the timestamp unreadable, or suggests the
# counters were zeroed AFTER the build (zero calls counted), SKIPS eviction. A
# skipped eviction saves the old superset — larger, never colder.
#
# ── STEP 2: THE DOUBLE-HOLD ──────────────────────────────────────────────────
#
# Every entry of this key on REF is, at this moment, from an EARLIER run: this
# run's own entry does not exist until the post step. Deleting them leaves the
# leg with no entry on REF only until that post step finishes.
#
# Exact group match, not prefix: `ccache-tier1-linux-clang-debug-` is also a
# prefix of a hypothetical `ccache-tier1-linux-clang-debug-py-…`, so the
# remainder must be exactly the ISO8601 stamp (same rule as cache-cleanup.yml;
# no `{n}` quantifiers, for mawk).
#
# ── WHAT NEVER REDDENS ───────────────────────────────────────────────────────
#
# A compiler cache that is down must never redden a lane whose build and tests
# passed: every API or ccache failure is a `::warning::` and exit 0. Only a
# WIRING error (missing argument or environment) exits non-zero, because a
# mis-wired call would otherwise no-op on every run while looking green.
#
# Verify on a push run: this step's `ccache-evict:` line (MiB before -> after)
# and `ccache-reclaim:` line, then the action's post-step `ccache -s`, then
# `gh api "repos/<owner>/<repo>/actions/caches?per_page=100"`.
#
# ⚠️ `set -uo pipefail` WITHOUT `-e` — every failure path is dispositioned.
set -uo pipefail

KEY="${1-}"
if [ -z "$KEY" ] || [ -z "${REPO-}" ] || [ -z "${REF-}" ] || [ -z "${CCACHE_DIR-}" ]; then
  echo "::error::usage: REPO=<owner/name> REF=<github.ref> CCACHE_DIR=<dir> GH_TOKEN=… ci/reclaim-ccache-generation.sh <action-key> — got key='${KEY}' REPO='${REPO-}' REF='${REF-}' CCACHE_DIR='${CCACHE_DIR-}'"
  exit 2
fi

note() { echo "$1"; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }
mib()  { du -sm "$CCACHE_DIR" 2>/dev/null | cut -f1; }

# ── Step 1: evict what this run did not touch ────────────────────────────────
stats="$(ccache --print-stats 2>/dev/null)" || stats=""
zeroed="$(printf '%s\n' "$stats" | awk -F'\t' '$1 == "stats_zeroed_timestamp" { print $2 }')"
calls="$(printf '%s\n' "$stats" | awk -F'\t' '
  $1 == "direct_cache_hit" || $1 == "preprocessed_cache_hit" || $1 == "cache_miss" { n += $2 }
  END { print n + 0 }')"

case "$zeroed" in
  ''|*[!0-9]*|0)
    note "ccache-evict: SKIPPED — no stats_zeroed_timestamp in \`ccache --print-stats\`, so the restore time is unknown; the unevicted store will be saved." ;;
  *)
    if [ "$calls" -eq 0 ]; then
      note "ccache-evict: SKIPPED — zero compiler calls counted since the counters were zeroed, so they were not zeroed at restore; the unevicted store will be saved."
    else
      age=$(( $(date +%s) - zeroed + 1 ))
      before="$(mib)"
      if ccache --evict-older-than "${age}s" >/dev/null 2>&1; then
        note "ccache-evict: kept files touched in the last ${age}s (since restore, ${calls} calls) — ${before:-?} MiB -> $(mib || true) MiB"
      else
        echo "::warning::\`ccache --evict-older-than ${age}s\` failed; the unevicted store will be saved."
        note "ccache-evict: FAILED — unevicted store will be saved."
      fi
    fi ;;
esac

# ── Step 2: delete the superseded entry before the replacement is saved ──────
prefix="ccache-${KEY}-"
if ! listing="$(gh api --paginate "repos/${REPO}/actions/caches?per_page=100&ref=${REF}&key=${prefix//+/%2B}" \
                  --jq '.actions_caches[] | "\(.id)\t\(.key)"' 2>/dev/null)"; then
  echo "::warning::could not list Actions caches for ${prefix}* on ${REF}; nothing reclaimed (cache-cleanup.yml's tier-end sweep remains the backstop)."
  note "ccache-reclaim: SKIPPED — listing failed."
  exit 0
fi

doomed="$(printf '%s\n' "$listing" | awk -F'\t' -v p="$prefix" '
  index($2, p) == 1 {
    s = substr($2, length(p) + 1)
    if (s ~ /^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9](\.[0-9]+)?Z$/)
      print $1 "\t" $2
  }')"

if [ -z "$doomed" ]; then
  note "ccache-reclaim: no earlier ${prefix}<stamp> entry on ${REF} — nothing to reclaim."
  exit 0
fi

count=0; failed=0
while IFS=$'\t' read -r id key; do
  [ -n "${id:-}" ] || continue
  # `</dev/null`: inside `while read`, a command can swallow the loop's stdin.
  if gh api --method DELETE "repos/${REPO}/actions/caches/${id}" </dev/null >/dev/null 2>&1; then
    echo "  deleted  ${key}"
    count=$((count + 1))
  else
    echo "::warning::failed to delete cache ${id} (${key})"
    failed=$((failed + 1))
  fi
done <<< "$doomed"

note "ccache-reclaim: deleted ${count} superseded ${prefix}<stamp> entr$([ "$count" -eq 1 ] && echo y || echo ies) on ${REF}, ${failed} failed."
exit 0
