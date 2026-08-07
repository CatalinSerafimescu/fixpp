#!/usr/bin/env bash
# Delete STALE GHCR Conan-cache tags for one profile, keeping only $CURRENT_TAG.
# Called at the end of seed-conan-cache.sh (local seed AND the CI push:main save
# step), so a conanfile.py bump doesn't leave old <profile>-<oldkey> tags behind.
#
# Auth: needs a token carrying `delete:packages` + `read:packages`.
#
# ⚠️ Repository Admin does NOT supply that — package deletion is gated on the
# TOKEN's scope, and the previous wording here ("needs delete perms on the
# package — fixpp has Admin") conflated the two. Measured 2026-08-06: this
# script had never deleted anything, from anywhere. Both MSVC-seeding runs left
# their predecessor's tag in place, and the maintainer's own `gh` token returned
# 403 "You need at least delete:packages and read:packages scopes" against a
# nonexistent version id. The single tag per Linux profile was never evidence to
# the contrary — each had simply been seeded once.
#
# In CI, tier2.yml passes GH_TOKEN as `secrets.GHCR_PAT || secrets.GITHUB_TOKEN`;
# GITHUB_TOKEN cannot delete, so pruning is live only while GHCR_PAT exists.
# Locally, `gh auth refresh -h github.com -s delete:packages,read:packages`.
#
# BEST-EFFORT: never exits non-zero, never fails seed.
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

# ── FAIL CLOSED ON A FAILED READ — backported from ci/prune-sccache.sh ───────
#
# The previous shape was `gh api … 2>/dev/null | jq … 2>/dev/null` piped into an
# unchecked `mapfile`. Under it a failed LIST and a genuinely clean package are
# INDISTINGUISHABLE: both yield an empty array, both print "no stale tags", both
# look like success. That is precisely how the bad GHCR token went unnoticed —
# the instrument reported clean because it could not report anything else.
# prune-sccache.sh was fixed for this on 2026-08-06; the backport never happened.
#
# Every read below is status-checked, and any failure exits via bail(), which
# says PENDING rather than "no stale tags".
bail() { echo "prune: PENDING ? — $1; nothing was deleted"; exit 0; }

if ! RAW=$(gh api --paginate "/user/packages/container/$PKG/versions?per_page=100" 2>&1); then
  echo "prune: PENDING ? — could not LIST $PKG versions; nothing was examined, let alone deleted"
  # gh prints a one-line diagnosis AND a pretty-printed JSON body, so the first
  # line is often just `{` — collapse and truncate rather than head -1.
  echo "prune: (api said) $(printf '%s' "$RAW" | tr -s '\n\r\t ' ' ' | cut -c1-200)"
  exit 0
fi

# FLATTEN THE PAGES: `gh api --paginate` emits one top-level array PER PAGE, not
# one merged array. `jq -s add` rather than `gh --slurp` so this does not depend
# on the gh version. Same rationale as prune-sccache.sh.
VERSIONS=$(printf '%s' "$RAW" | jq -s 'add // []' 2>/dev/null)
[ -n "$VERSIONS" ] || bail "could not parse the version list as JSON"

# Guard: every version must carry a real tags array. The old `// []` fallback
# silently reclassified a malformed response as "untagged", which for THIS
# script means "not a stale cache tag" and therefore an under-count — the safe
# direction, but achieved by not looking.
MALFORMED=$(printf '%s' "$VERSIONS" \
  | jq '[ .[] | select((.metadata.container.tags | type) != "array") ] | length' 2>/dev/null)
[ -n "$MALFORMED" ] || bail "could not parse the version list as JSON"
[ "$MALFORMED" = "0" ] || bail "$MALFORMED version(s) carry no tags ARRAY — the API response is not the shape this script classifies on"

# Status-checked, unlike the old `mapfile < <(jq …)`: a jq failure there produced
# an empty STALE, a loop that printed nothing, and a clean-looking exit 0.
if ! STALETXT=$(
  printf '%s' "$VERSIONS" \
  | jq -r --arg p "$SANI" --arg cur "$CURRENT_TAG" '
      .[]
      | . as $v | ($v.metadata.container.tags) as $tags
      | select(($tags | length) > 0)
      | select([ $tags[] | test("^\($p)-[0-9a-f]{16}$") ] | all)
      | select(($tags | index($cur)) | not)
      | "\($v.id)\t\($tags | join(","))"'
); then
  bail "could not classify the version list (jq failed)"
fi
mapfile -t STALE <<< "$STALETXT"

if [ "${#STALE[@]}" -eq 0 ] || [ -z "${STALE[0]:-}" ]; then
  echo "prune: no stale tags for $SANI (current=$CURRENT_TAG)"; exit 0
fi

deleted=0 pending=0
for row in "${STALE[@]}"; do
  [ -z "$row" ] && continue
  vid="${row%%$'\t'*}"; tags="${row#*$'\t'}"
  if [ "${DRY_RUN:-0}" = "1" ]; then
    echo "prune (dry-run): would delete version $vid  tags=[$tags]"
  else
    # Print what the API actually said, for the same reason as prune-sccache.sh:
    # "needs delete perms" was a guess in the position of a diagnosis, and it
    # made two different failures produce identical logs.
    if out=$(gh api --method DELETE "/user/packages/container/$PKG/versions/$vid" 2>&1); then
      echo "prune: deleted stale version $vid  tags=[$tags]"
      deleted=$((deleted + 1))
    else
      echo "prune: REFUSED version $vid  tags=[$tags] — $(printf '%s' "$out" | tr -s '\n\r\t ' ' ' | cut -c1-200)"
      pending=$((pending + 1))
    fi
  fi
done

# Summarise in the same greppable shape as prune-sccache.sh, so one grep covers
# both packages and a silent run is visibly distinct from a clean one.
[ "$deleted" -gt 0 ] && echo "prune: deleted $deleted"
if [ "$pending" -gt 0 ]; then
  echo "prune: PENDING $pending  (see the REFUSED line(s) above for what the API said; run ci/prune-conan-cache.sh locally with a delete:packages token to reclaim)"
fi
exit 0
