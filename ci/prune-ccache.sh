#!/usr/bin/env bash
# Delete dead GHCR fixpp-ccache versions for one preset, keeping only the
# current tag. Thin wrapper — all the logic, and all the evidence behind it,
# lives in ci/prune-compiler-cache.sh.
#
#   ci/prune-ccache.sh linux-clang-libc++ ccache-linux-clang-libcxx-clang22-15dc124f
#   DRY_RUN=1 ci/prune-ccache.sh linux-clang-libc++ ''     # list, delete nothing
#
# ⚠️ THE TAG GRAMMAR IS NOT RESTATED HERE. It comes from `ccache_tag_regex` in
# ci/ccache-cache-key.sh, beside the expression that MINTS the tags — because a
# producer and a matcher in separate files drift silently: a tag the regex does
# not match is classified as somebody else's and skipped, so nothing is deleted
# and no `prune: PENDING` note ever fires. See that function's header.
#
# `ccache_tag_regex` is pure string work — no compiler probe, no jq — so this
# wrapper stays runnable on a machine that cannot build the preset at all, which
# is exactly the situation a maintainer draining a backlog is in.
#
# BEST-EFFORT: never exits non-zero. See ci/prune-compiler-cache.sh for the
# `prune: PENDING <n>` contract and the delete:packages token requirement.
set -uo pipefail
PRESET="${1:?usage: prune-ccache.sh <preset> <current-tag>}"
CURRENT_TAG="${2-}"

# shellcheck source=ci/ccache-cache-key.sh
. "$(dirname "$0")/ccache-cache-key.sh"

if ! ccache_tag_regex "$PRESET"; then
  echo "prune: PENDING ? — could not build a tag pattern for preset '$PRESET'; nothing was examined"
  exit 0
fi

exec "$(dirname "$0")/prune-compiler-cache.sh" \
  fixpp-ccache "$PRESET" "$CCACHE_TAG_RE" "$CURRENT_TAG"
