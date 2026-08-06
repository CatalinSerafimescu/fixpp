#!/usr/bin/env bash
# CI-side: publish this run's sccache disk cache to GHCR under a rolling
# preset+toolset tag. Runs AFTER Build (so the cache is populated) and BEFORE
# the ctest steps (so a red test does not withhold a cache that is purely about
# compilation).
#
# Gated by the caller to push:main / workflow_dispatch — both maintainer
# triggered, never a PR — mirroring the Conan save step. Unlike Conan, gating
# here is NOT a safety property: sccache entries are keyed on the preprocessed
# source, so a cache published from a feature branch cannot make another branch
# compile the wrong thing, only miss. The gate exists so PR legs stay read-only
# against a cache whose provenance is main, i.e. so a PR cannot spend a
# maintainer's storage.
#
# Requires oras on PATH and an `oras login ghcr.io` already performed by the
# caller. Best-effort by contract: the caller sets continue-on-error, because
# publishing is an optimisation and a GHCR hiccup must not redden a green lane.
#
# Usage (from the library root):
#   ci/seed-sccache.sh windows-msvc-debug
set -uo pipefail

PRESET="${1:?usage: seed-sccache.sh <preset>}"
IMAGE="ghcr.io/catalinserafimescu/fixpp-sccache"
PKG=fixpp-sccache

# shellcheck source=ci/sccache-cache-key.sh
. "$(dirname "$0")/sccache-cache-key.sh"

note() { echo "$1"; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }

: "${SCCACHE_DIR:?SCCACHE_DIR must be set (the workflow sets it job-wide)}"
DIR_POSIX="$(posixpath "$SCCACHE_DIR")"

if ! sccache_cache_key "$PRESET"; then
  echo "seed: no identifiable toolset — nothing published." >&2
  exit 0
fi
TAG="$SCCACHE_CACHE_TAG"

# Stop the server BEFORE archiving. sccache writes entries through a background
# daemon; tarring a live cache directory can capture a half-written entry or a
# lock file, and a corrupt entry restored on a later run is a compile failure
# rather than a miss. This is the one step that must not be skipped for speed.
sccache --stop-server >/dev/null 2>&1 || true

[ -d "$DIR_POSIX" ] || { echo "seed: $DIR_POSIX does not exist — nothing to publish."; exit 0; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# Uncompressed — see restore-sccache.sh. Must match the extract side.
tar -cf "$WORK/sccache-$PRESET.tar" -C "$DIR_POSIX" .

# Report the SIZES BEFORE attempting the push, not after. SCCACHE_CACHE_SIZE was
# set from an estimate and is meant to be tuned from these two numbers — and the
# run most likely to fail its push (the first one, before the GHCR package
# exists) is exactly the run whose sizing datum would otherwise be lost.
note "sccache-cache archive \`$TAG\` — $(du -h "$WORK/sccache-$PRESET.tar" | cut -f1) archive, $(du -sh "$DIR_POSIX" | cut -f1) on disk (cap \`${SCCACHE_CACHE_SIZE:-default}\`)"

# Push from inside $WORK with a RELATIVE filename so oras' absolute-path guard
# passes and the artifact title is the clean basename (same reason as
# seed-conan-cache.sh).
( cd "$WORK" && oras push "$IMAGE:$TAG" \
    --artifact-type application/vnd.fixpp.sccache.v1 \
    --annotation "org.opencontainers.image.source=https://github.com/CatalinSerafimescu/fixpp" \
    --annotation "fixpp.preset=$PRESET" \
    --annotation "fixpp.msvc.toolset=$SCCACHE_CACHE_TOOLSET" \
    "sccache-$PRESET.tar:application/x-tar" ) || {
  # The caller sets continue-on-error, so this exit is INVISIBLE in the step's
  # status. Shout into the summary instead — a silently unseeded cache makes the
  # next run a second COLD build that reads as a warm one, which would corrupt
  # the A/B this probe exists to produce.
  note "::error::sccache-cache SEED FAILED for \`$TAG\` — the cache was NOT published. A following 'warm' run would measure another cold build."
  exit 1
}

# The literal token a warm-run dispatch is gated on. Do not reword without
# updating ci/tier2-sccache-probe.md, which tells the operator to grep for it.
note "sccache-cache SEEDED \`$TAG\`"

# ── Prune, best-effort — delegated so it is also runnable STANDALONE ─────────
# It has to be runnable standalone because it will NOT delete anything from here:
# GITHUB_TOKEN carries packages:write (read+write), not delete:packages. The
# rolling tag means every republish orphans a multi-GB untagged version, so the
# backlog is surfaced into the job summary rather than left to accumulate
# silently. Full evidence in ci/prune-sccache.sh's header.
pending=$("$(dirname "$0")/prune-sccache.sh" "$PRESET" "$TAG" | tee /dev/stderr | sed -n 's/^prune: PENDING \([0-9]*\).*/\1/p')
if [ -n "${pending:-}" ] && [ "$pending" -gt 0 ]; then
  note "sccache-cache: **$pending dead version(s) could not be reclaimed** from CI (token lacks \`delete:packages\`). Run \`ci/prune-sccache.sh $PRESET $TAG\` locally."
fi
exit 0
