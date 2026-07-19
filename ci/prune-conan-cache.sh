#!/usr/bin/env bash
# Delete STALE GHCR Conan-cache tags for one profile, keeping only $CURRENT_TAG.
# Called at the end of seed-conan-cache.sh (local seed AND the CI push:main save
# step), so a conanfile.py bump doesn't leave old <profile>-<oldkey> tags behind.
#
# Auth: `gh` authenticated locally, or GH_TOKEN in CI (needs delete perms on the
# package — fixpp has Admin). BEST-EFFORT: never exits non-zero, never fails seed.
# DRY_RUN=1 → print what would be deleted, delete nothing.
#
# SAFE BY CONSTRUCTION:
#  - matches ONLY `^<sanitized-profile>-[0-9a-f]{16}$` (the exact 16-hex key tag),
#    so `linux-clang-libcxx` never matches `linux-clang-libcxx-tsan-*` etc.;
#  - only prunes a version whose tags are ALL such this-profile cache tags;
#  - always excludes $CURRENT_TAG.
set -uo pipefail
PROFILE="${1:?usage: prune-conan-cache.sh <profile> <current-tag>}"
CURRENT_TAG="${2:?usage: prune-conan-cache.sh <profile> <current-tag>}"
PKG=fixpp-conan-cache
SANI="${PROFILE//+/x}"

command -v jq  >/dev/null 2>&1 || { echo "prune: jq not found — skipping";  exit 0; }
command -v gh  >/dev/null 2>&1 || { echo "prune: gh not found — skipping";  exit 0; }

mapfile -t STALE < <(
  gh api --paginate "/user/packages/container/$PKG/versions" 2>/dev/null \
  | jq -r --arg p "$SANI" --arg cur "$CURRENT_TAG" '
      .[]
      | . as $v | (($v.metadata.container.tags) // []) as $tags
      | select(($tags | length) > 0)
      | select([ $tags[] | test("^\($p)-[0-9a-f]{16}$") ] | all)
      | select(($tags | index($cur)) | not)
      | "\($v.id)\t\($tags | join(","))"' 2>/dev/null
)

if [ "${#STALE[@]}" -eq 0 ] || [ -z "${STALE[0]:-}" ]; then
  echo "prune: no stale tags for $SANI (current=$CURRENT_TAG)"; exit 0
fi

for row in "${STALE[@]}"; do
  [ -z "$row" ] && continue
  vid="${row%%$'\t'*}"; tags="${row#*$'\t'}"
  if [ "${DRY_RUN:-0}" = "1" ]; then
    echo "prune (dry-run): would delete version $vid  tags=[$tags]"
  else
    echo "prune: deleting stale version $vid  tags=[$tags]"
    gh api --method DELETE "/user/packages/container/$PKG/versions/$vid" >/dev/null 2>&1 \
      && echo "  deleted" || echo "  delete FAILED (non-fatal — needs delete perms)"
  fi
done
exit 0
