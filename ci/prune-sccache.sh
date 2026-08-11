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
# ⚠️ ANCHORED AT BOTH ENDS. It used to be start-only, which was safe only BY
# ACCIDENT of naming — no Tier 2 preset is a prefix of another today
# (windows-msvc-{debug,release,asan}). The day one becomes one
# (`windows-msvc-asan` → `…-asan-lto`), pruning the shorter lane would delete
# the longer lane's LIVE cache, silently and with no test able to catch it.
#
# `[0-9.]+$` is this package's toolset grammar — `sccache-<preset>-<VCToolsVersion
# |VSCMD_VER>`, e.g. `sccache-windows-msvc-debug-14.44.35207`
# (ci/sccache-cache-key.sh:75). Stating the end here rather than in the generic
# pruner is the point: each caller knows its own grammar.
#
# MEASURED before changing it, not reasoned: all three live fixpp-sccache tags
# match this form, so no existing version stopped being classified as
# reclaimable. If the toolset grammar ever gains a non-numeric component, this
# regex must move with it — a tag it does not match is skipped SILENTLY.
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
  fixpp-sccache "$PRESET" "^sccache-${PRESET}-[0-9.]+\$" "$CURRENT_TAG"
