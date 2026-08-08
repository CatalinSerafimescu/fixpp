#!/usr/bin/env bash
# Delete dead GHCR fixpp-ccache versions for one preset, keeping only the
# current tag. Thin wrapper — all the logic, and all the evidence behind it,
# lives in ci/prune-compiler-cache.sh.
#
#   ci/prune-ccache.sh linux-clang-libc++ ccache-linux-clang-libcxx-clang22-15dc124f
#   DRY_RUN=1 ci/prune-ccache.sh linux-clang-libc++ ''     # list, delete nothing
#
# ⚠️ THE REGEX IS ANCHORED AT BOTH ENDS, and that is not stylistic.
# `linux-clang-libcxx` is a PREFIX of `linux-clang-libcxx-asan`, so the
# start-anchored form the sccache wrapper uses would make a prune of the plain
# libc++ lane classify all three sanitizer lanes' live caches as "mine" and
# delete them. The end anchor is what keeps the four Tier 3 legs' namespaces
# disjoint.
#
# The grammar mirrored here is ci/ccache-cache-key.sh's:
#
#     ccache-<preset, '+' → 'x'>-clang<major>-<sha8 of `clang++ --version`>
#
# Keep the two in agreement. A tag the key script can mint but this regex does
# not match is not merely unpruned — it is a version that accumulates forever
# while the backlog note stays silent, because a non-matching tag is classified
# as "someone else's" and skipped.
#
# BEST-EFFORT: never exits non-zero. See ci/prune-compiler-cache.sh for the
# `prune: PENDING <n>` contract and the delete:packages token requirement.
set -uo pipefail
PRESET="${1:?usage: prune-ccache.sh <preset> <current-tag>}"
CURRENT_TAG="${2-}"

# Same sanitization the tag itself carries (conan-cache-key.sh:101,
# ccache-cache-key.sh) — OCI tags forbid '+', so `libc++` → `libcxx`.
SAFE_PRESET="${PRESET//+/x}"

# The preset is interpolated into a REGEX below. After the '+' substitution the
# libc++ presets are `[a-z0-9-]`, but refuse rather than assume.
case "$SAFE_PRESET" in
  *[!a-zA-Z0-9_-]*)
    echo "prune: PENDING ? — preset '$PRESET' contains a character that is not safe to interpolate into a tag regex; nothing was examined"
    exit 0 ;;
esac

exec "$(dirname "$0")/prune-compiler-cache.sh" \
  fixpp-ccache "$PRESET" "^ccache-${SAFE_PRESET}-clang[0-9a-z]+-[0-9a-f]+\$" "$CURRENT_TAG"
