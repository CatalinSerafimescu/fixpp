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
# ── ⚠️ WHETHER THIS DELETES FROM CI DEPENDS ENTIRELY ON THE TOKEN ────────────
#
# Deleting a package VERSION needs `delete:packages`. `GITHUB_TOKEN`'s
# `packages: write` is read+write only and does NOT carry it, so under the
# fallback token this script deletes nothing and reports `prune: PENDING <n>`.
# Since 2026-08-06 the seed steps pass `secrets.GHCR_PAT || secrets.GITHUB_TOKEN`
# and a classic PAT with `delete:packages` + `read:packages` is stored as
# `GHCR_PAT`, so **CI does delete when that secret is present**. Do not read the
# absence of a local prune as proof that nothing in CI can remove a version.
#
# Why this mattered enough to build: MEASURED on the sibling package —
# fixpp-conan-cache's three MSVC profiles each held TWO tags (2026-08-02 and
# 2026-08-03), the older having survived a seed whose prune ran, while every
# Linux profile held exactly ONE because those are seeded locally with a
# delete-scoped `gh`. That asymmetry costs Conan almost nothing: its tag is
# content-hashed, so each push mints a NEW tag and orphans no manifest (zero
# untagged versions there). This package's tag is ROLLING, so every republish
# orphans an untagged version of several GB. Left unreclaimed it accumulates
# indefinitely.
#
# Runnable standalone precisely because the CI path is conditional on a secret:
# seed-sccache.sh reports any pending backlog into the job summary so it is
# visible rather than assumed, and a maintainer can drain it by hand.
#
# BEST-EFFORT: never exits non-zero. Prints a machine-greppable
# `prune: PENDING <n>` line when deletes were refused.
set -uo pipefail
PRESET="${1:?usage: prune-sccache.sh <preset> <current-tag>}"
CURRENT_TAG="${2-}"
PKG=fixpp-sccache

command -v jq >/dev/null 2>&1 || { echo "prune: jq not found — skipping"; exit 0; }
command -v gh >/dev/null 2>&1 || { echo "prune: gh not found — skipping"; exit 0; }

# SAFE UNDER A STATED CONTRACT, mirroring prune-conan-cache.sh:
#  - an UNTAGGED version is unreachable *through this repo's cache contract*,
#    which is tag-only: restore-sccache.sh pulls `$IMAGE:$TAG` and nothing here
#    ever pulls by digest. It is NOT unreachable in general — an OCI manifest
#    can always be pulled by `@sha256:…`, so anyone who pins a digest by hand
#    (e.g. parking a known-good cache for rollback) is outside the contract and
#    this script will reclaim it. Do not pin a digest against this package.
#  - a TAGGED version is deleted only when EVERY one of its tags is a
#    this-preset sccache tag AND none of them is $CURRENT_TAG. A version sharing
#    a tag with anything else is left alone.
#
# FAIL CLOSED on anything unexpected. `.metadata.container.tags // []` silently
# turned a MISSING tags field into a proven-empty one, i.e. into "untagged, safe
# to delete" — which is the one classification that bypasses the $CURRENT_TAG
# guard. A degraded or reshaped API response could therefore delete the live
# cache. Absence of evidence is not evidence of absence: if any version does not
# carry a tags ARRAY, or if $CURRENT_TAG is not found exactly once, this script
# deletes nothing and says so.
# List and parse as SEPARATE steps. Piping the api straight into jq with
# `2>/dev/null` makes a failed call indistinguishable from a clean package: both
# yield an empty DEAD, the loop below prints nothing, and the caller records
# `pending=0`. MEASURED on run 31121588649 — this step printed not one line on
# either seeded leg while the package was in fact carrying two orphaned
# multi-GB versions, which a local run of this same script then deleted. A
# reclaim path that reports "nothing to do" when it could not even ask is worse
# than one that reports a backlog.
if ! RAW=$(gh api --paginate "/user/packages/container/$PKG/versions?per_page=100" 2>&1); then
  echo "prune: PENDING ? — could not LIST $PKG versions; nothing was examined, let alone deleted"
  # gh prints a one-line diagnosis AND a pretty-printed JSON body; the first
  # line is often just `{`, so collapse and truncate rather than head -1.
  echo "prune: (api said) $(printf '%s' "$RAW" | tr -s '\n\r\t ' ' ' | cut -c1-200)"
  exit 0
fi

bail() { echo "prune: PENDING ? — $1; nothing was deleted"; exit 0; }

# FLATTEN THE PAGES. `gh api --paginate` emits each page as its own top-level
# JSON array, not one merged array, and this endpoint pages at 30 by default.
# The streaming `.[]` filter below tolerates that, but the two scalar guards do
# NOT: they would emit one count per page ("0\n0"), every `[ "$x" = "0" ]`
# comparison would fail, and the script would fail closed FOREVER the moment the
# package exceeded one page — locking out the very cleanup that shrinks it.
# `jq -s add` rather than `gh --slurp` so this does not depend on the gh version.
VERSIONS=$(printf '%s' "$RAW" | jq -s 'add // []' 2>/dev/null)
[ -n "$VERSIONS" ] || bail "could not parse the version list as JSON"

# Guard 1: every version must carry a real tags array. No `// []` fallback.
MALFORMED=$(printf '%s' "$VERSIONS" \
  | jq '[ .[] | select((.metadata.container.tags | type) != "array") ] | length' 2>/dev/null)
[ -n "$MALFORMED" ] || bail "could not parse the version list as JSON"
[ "$MALFORMED" = "0" ] || bail "$MALFORMED version(s) carry no tags ARRAY — the API response is not the shape this script classifies on"

# Guard 2: the tag we are protecting must be present exactly once. Zero means we
# are about to prune around a keep-tag that does not exist — the caller computed
# a different key than the one that was published, and every "dead" version in
# that listing is in fact the live cache. Two means the invariant is already
# broken and a human should look. An EMPTY CURRENT_TAG is the documented
# reclaim-a-preset-wholesale mode and skips this guard by design.
if [ -n "$CURRENT_TAG" ]; then
  KEEP=$(printf '%s' "$VERSIONS" \
    | jq --arg cur "$CURRENT_TAG" '[ .[] | select(.metadata.container.tags | index($cur)) ] | length' 2>/dev/null)
  [ "$KEEP" = "1" ] || bail "keep-tag '$CURRENT_TAG' matched ${KEEP:-?} versions, expected exactly 1"
fi

mapfile -t DEAD < <(
  printf '%s' "$VERSIONS" \
  | jq -r --arg p "$PRESET" --arg cur "$CURRENT_TAG" '
      .[]
      | . as $v
      | ($v.metadata.container.tags) as $tags
      | (($tags | length) == 0)                            as $untagged
      | ([ $tags[] | startswith("sccache-\($p)-") ] | all) as $mine
      | (($tags | index($cur)) != null)                    as $is_current
      | select(($is_current | not) and ($untagged or $mine))
      | "\($v.id)\t\(if $untagged then "<untagged>" else ($tags | join(",")) end)"'
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
