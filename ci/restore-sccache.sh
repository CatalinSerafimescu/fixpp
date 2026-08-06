#!/usr/bin/env bash
# CI-side: pull a preset's sccache disk cache from GHCR → restore into
# SCCACHE_DIR. Runs BEFORE `cmake --preset` (Configure), i.e. before anything
# can start the sccache server, because the cache directory must not be swapped
# out from under a running server.
#
# On MISS it exits 0 and the build simply compiles everything, populating an
# empty cache that the seed step then publishes. A compiler cache that is down
# must NEVER redden a lane whose build and tests pass.
#
# Requires oras on PATH (oras-project/setup-oras@v2) and SCCACHE_DIR set.
#
# Usage (from the library root, as the workflow's working directory):
#   ci/restore-sccache.sh windows-msvc-debug
set -uo pipefail

PRESET="${1:?usage: restore-sccache.sh <preset>}"
IMAGE="ghcr.io/catalinserafimescu/fixpp-sccache"

# shellcheck source=ci/sccache-cache-key.sh
. "$(dirname "$0")/sccache-cache-key.sh"

emit() { [ -n "${GITHUB_OUTPUT:-}" ] && echo "hit=$1" >> "$GITHUB_OUTPUT"; }
note() { echo "$1"; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }

: "${SCCACHE_DIR:?SCCACHE_DIR must be set (the workflow sets it job-wide)}"
DIR_POSIX="$(posixpath "$SCCACHE_DIR")"
mkdir -p "$DIR_POSIX"

if ! sccache_cache_key "$PRESET"; then
  echo "sccache-cache MISS ($PRESET, toolset unidentified) → building with an empty cache"
  emit false
  exit 0
fi
TAG="$SCCACHE_CACHE_TAG"
echo "sccache-cache: toolset $SCCACHE_CACHE_TOOLSET folded into the tag"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

if oras pull "$IMAGE:$TAG" -o "$WORK" >/dev/null 2>&1; then
  # Uncompressed tar on purpose: sccache already stores every entry
  # zstd-compressed, so gzipping the archive spends minutes of CPU to save
  # almost nothing — on an 80-minute build step that is a real trade, not a
  # micro-optimisation. Must match seed-sccache.sh.
  tar -xf "$WORK/sccache-$PRESET.tar" -C "$DIR_POSIX"
  note "sccache-cache HIT  \`$TAG\`  ($(du -sh "$DIR_POSIX" | cut -f1) restored)"
  emit true
else
  # MISS: no cache published for this preset+toolset yet, the package is not
  # readable from this context, or GHCR is unreachable. All three are the same
  # disposition — compile from scratch.
  #
  # Deliberately NOT a ::warning:: the way the Conan miss is. There, a miss
  # means every PR leg rebuilds the whole OTel chain until a maintainer
  # reseeds, so it has to be loud. Here a miss is the pre-seed steady state on
  # any new toolset and costs only the speedup, never correctness — and
  # `sccache --show-stats` reports the resulting 0% hit rate on the same run,
  # so nothing is hidden by staying quiet.
  note "sccache-cache MISS \`$TAG\` → compiling with an empty cache"
  emit false
fi
