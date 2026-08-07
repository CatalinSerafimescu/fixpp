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

# STAGE sits beside SCCACHE_DIR, not under $WORK, so the swap-in below is a
# rename on the same volume rather than a 1 GB copy across one.
STAGE="${DIR_POSIX%/}.restore.$$"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK" "$STAGE"' EXIT
rm -rf "$STAGE"

# Extract into STAGE and swap in only on success — never straight into the live
# directory. Without `set -e` an unchecked `tar -xf` failure was invisible: a
# truncated archive (whose OCI digest is perfectly valid for its truncated
# bytes) extracts a prefix, returns non-zero, and the old code went on to emit
# `HIT` and `hit=true` over a partial cache. That is worse than a MISS in both
# directions — the summary records a restore that did not happen, and sccache
# serves from a directory nobody verified. Every leg of the condition is
# checked: the pull, the archive's presence, and the extraction itself.
if ! oras pull "$IMAGE:$TAG" -o "$WORK" >/dev/null 2>&1; then
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

# Uncompressed tar on purpose: sccache already stores every entry
# zstd-compressed, so gzipping the archive spends minutes of CPU to save almost
# nothing — on an 80-minute build step that is a real trade, not a
# micro-optimisation. Must match seed-sccache.sh.
elif ! { [ -f "$WORK/sccache-$PRESET.tar" ] \
         && mkdir -p "$STAGE" \
         && tar -xf "$WORK/sccache-$PRESET.tar" -C "$STAGE"; }; then
  # Pulled, but the payload is not usable. Same disposition as a plain miss: a
  # cache we could not verify is a cache we do not have. The partial tree is
  # discarded rather than swapped in.
  rm -rf "$STAGE"
  note "sccache-cache MISS \`$TAG\` — pulled, but the archive was missing or did not extract cleanly. Treated as no cache; compiling from scratch."
  emit false

else
  rm -rf "$DIR_POSIX"
  if mv "$STAGE" "$DIR_POSIX"; then
    note "sccache-cache HIT  \`$TAG\`  ($(du -sh "$DIR_POSIX" | cut -f1) restored)"
    emit true
  else
    # DIR_POSIX has already been removed, so recreate it empty rather than let
    # the server below start against a directory that may not exist.
    mkdir -p "$DIR_POSIX"
    note "sccache-cache MISS \`$TAG\` — restored tree could not be moved into ${SCCACHE_DIR}; compiling with an empty cache"
    emit false
  fi
fi

# Start the server on a KNOWN-ZERO counter. Nothing before this point starts one
# (`sccache --version` does not), so today the counters would be zero anyway —
# but "would be anyway" is not a property worth resting the headline measurement
# on, and any future step that touches sccache earlier would silently carry its
# requests into the reported hit rate. --zero-stats also proves here, before an
# 80-minute Build, that the server can start against the directory just restored.
#
# This IS allowed to redden the lane, unlike every other failure in this script.
# A server that will not start is not "the cache is down" — every `sccache
# cl.exe` invocation in the Build step would fail, so the choice is between
# failing here with a step name that says why and failing 3235 times later.
sccache --zero-stats >/dev/null || {
  echo "::error::sccache server would not start against ${SCCACHE_DIR}. Every compile in the Build step invokes it, so this is fatal rather than a lost speedup."
  exit 1
}
