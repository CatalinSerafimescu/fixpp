#!/usr/bin/env bash
# Delete dead GHCR fixpp-sccache versions for one preset, keeping only the
# current tag. Thin wrapper — all the logic, and all the evidence behind it,
# lives in ci/prune-compiler-cache.sh.
#
#   ci/prune-sccache.sh windows-msvc-debug sccache-windows-msvc-debug-14.44.35207
#   DRY_RUN=1 ci/prune-sccache.sh windows-msvc-debug ''      # list, delete nothing
#
# The wrapper exists so this documented standalone command keeps working, and so
# the TAG GRAMMAR stays next to the key script that mints it
# (ci/sccache-cache-key.sh:75 — `sccache-<preset>-<toolset>`).
#
# ⚠️ The prefix match below is anchored only at the START, which is safe HERE by
# accident of naming: no Tier 2 preset is a prefix of another
# (windows-msvc-{debug,release,asan}). Keep it that way, or anchor the end. The
# ccache side could not rely on that accident and does anchor both — see
# ci/prune-ccache.sh.
#
# BEST-EFFORT: never exits non-zero. See ci/prune-compiler-cache.sh for the
# `prune: PENDING <n>` contract and the delete:packages token requirement.
set -uo pipefail
PRESET="${1:?usage: prune-sccache.sh <preset> <current-tag>}"
CURRENT_TAG="${2-}"

# The preset is interpolated into a REGEX below, so a name carrying a regex
# metacharacter would silently change what matches. Tier 2's presets are
# `[a-z0-9-]` today; refuse rather than assume it stays that way.
case "$PRESET" in
  *[!a-zA-Z0-9_-]*)
    echo "prune: PENDING ? — preset '$PRESET' contains a character that is not safe to interpolate into a tag regex; nothing was examined"
    exit 0 ;;
esac

exec "$(dirname "$0")/prune-compiler-cache.sh" \
  fixpp-sccache "$PRESET" "^sccache-${PRESET}-" "$CURRENT_TAG"
