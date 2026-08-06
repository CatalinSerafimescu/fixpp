#!/usr/bin/env bash
# Delete dead GHCR fixpp-sccache versions for one preset, keeping only $CURRENT_TAG.
#
# Called best-effort at the end of seed-sccache.sh, and — this is the part that
# matters — runnable STANDALONE as a maintenance command:
#
#   ci/prune-sccache.sh windows-msvc-debug sccache-windows-msvc-debug-14.44.35207
#   DRY_RUN=1 ci/prune-sccache.sh windows-msvc-debug ''      # list, delete nothing
#
# Passing an empty CURRENT_TAG keeps nothing — use only when reclaiming a preset
# wholesale. Any non-empty value is protected.
#
# ── ⚠️ THIS WILL NOT DELETE ANYTHING WHEN RUN FROM CI ────────────────────────
#
# `GITHUB_TOKEN`'s `packages: write` is read+write; deleting a package VERSION
# needs `delete:packages`, which it does not carry, and this repo has no PAT
# secret. MEASURED on the sibling package: fixpp-conan-cache's three MSVC
# profiles each hold TWO tags (2026-08-02 and 2026-08-03) — the older survived a
# seed whose prune ran — while every Linux profile holds exactly ONE, because
# those are seeded locally with an admin-scoped `gh`.
#
# That asymmetry costs Conan almost nothing: its tag is content-hashed, so each
# push mints a NEW tag and orphans no manifest (zero untagged versions there).
# This package's tag is ROLLING, so every republish orphans an untagged version
# of several GB. Left alone, CI accumulates them indefinitely.
#
# Hence: run this LOCALLY, periodically. seed-sccache.sh reports the pending
# count into the job summary so the backlog is visible rather than assumed.
#
# BEST-EFFORT: never exits non-zero. Prints a machine-greppable
# `prune: PENDING <n>` line when deletes were refused.
set -uo pipefail
PRESET="${1:?usage: prune-sccache.sh <preset> <current-tag>}"
CURRENT_TAG="${2-}"
PKG=fixpp-sccache

command -v jq >/dev/null 2>&1 || { echo "prune: jq not found — skipping"; exit 0; }
command -v gh >/dev/null 2>&1 || { echo "prune: gh not found — skipping"; exit 0; }

# SAFE BY CONSTRUCTION, mirroring prune-conan-cache.sh:
#  - an UNTAGGED version is unreachable by definition (nothing can pull it), so
#    it is always safe to delete;
#  - a TAGGED version is deleted only when EVERY one of its tags is a
#    this-preset sccache tag AND none of them is $CURRENT_TAG. A version sharing
#    a tag with anything else is left alone.
mapfile -t DEAD < <(
  gh api --paginate "/user/packages/container/$PKG/versions" 2>/dev/null \
  | jq -r --arg p "$PRESET" --arg cur "$CURRENT_TAG" '
      .[]
      | . as $v
      | (($v.metadata.container.tags) // []) as $tags
      | (($tags | length) == 0)                            as $untagged
      | ([ $tags[] | startswith("sccache-\($p)-") ] | all) as $mine
      | (($tags | index($cur)) != null)                    as $is_current
      | select($untagged or ($mine and ($is_current | not)))
      | "\($v.id)\t\(if $untagged then "<untagged>" else ($tags | join(",")) end)"' 2>/dev/null
)

deleted=0 pending=0
for row in "${DEAD[@]:-}"; do
  [ -z "$row" ] && continue
  vid="${row%%$'\t'*}"; tags="${row#*$'\t'}"
  if [ "${DRY_RUN:-0}" = "1" ]; then
    echo "prune (dry-run): would delete version $vid  tags=[$tags]"
    continue
  fi
  if gh api --method DELETE "/user/packages/container/$PKG/versions/$vid" >/dev/null 2>&1; then
    echo "prune: deleted version $vid  tags=[$tags]"
    deleted=$((deleted + 1))
  else
    echo "prune: REFUSED version $vid  tags=[$tags]"
    pending=$((pending + 1))
  fi
done

[ "$deleted" -gt 0 ] && echo "prune: deleted $deleted"
if [ "$pending" -gt 0 ]; then
  # Greppable, and deliberately not silent. The previous shape logged
  # "delete FAILED (non-fatal)" per version, which reads as noise and let the
  # sibling package quietly double its MSVC tag count unnoticed for a week.
  echo "prune: PENDING $pending  (token lacks delete:packages — run ci/prune-sccache.sh locally to reclaim)"
fi
exit 0
