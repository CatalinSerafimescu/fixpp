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

# The cache directory must be QUIESCENT before archiving. sccache writes entries
# through a background daemon; tarring a live cache directory can capture a
# half-written entry, and a corrupt entry restored on a later run is a compile
# failure rather than a miss. This is the one prerequisite that must not be
# skipped for speed — and it was previously asserted (`|| true`) rather than
# verified, so a daemon that stayed alive still produced a `SEEDED` marker.
#
# ⚠️ Do NOT "fix" this by requiring exit 0. MEASURED with sccache 0.17.0:
#
#     no server running  → rc=2, "sccache: error: couldn't connect to server"
#     server running     → rc=0
#
# and the caller's `sccache statistics` step (tier2.yml) has ALREADY stopped the
# server by the time this runs. Treating non-zero as failure would redden every
# seed on the normal path. Both "I stopped it" and "there was nothing to stop"
# are the state we want; anything else is not.
stop_out="$(sccache --stop-server 2>&1)"; stop_rc=$?
case "$stop_rc:$stop_out" in
  0:*|*:*"couldn't connect to server"*) : ;;   # stopped, or already quiescent
  *)
    note "::error::sccache-cache SEED FAILED for \`$TAG\` — \`sccache --stop-server\` failed in an unrecognised way (rc=$stop_rc): ${stop_out}. Refusing to archive a possibly-live cache directory."
    exit 1 ;;
esac

# …and then VERIFY the process is gone, because neither branch above proves it.
# `rc=0` only means the daemon acknowledged the shutdown request — it may still
# drain in-flight requests for several seconds afterwards — and "couldn't
# connect" is a generic connection failure that a live-but-wedged daemon can
# also produce. Both were accepted as quiescence, which left exactly the
# false-green this check exists to close: a `SEEDED` marker over an archive
# taken while the cache was still being written.
#
# MEASURED on Windows: `tasklist` exits 0 whether or not anything matches, so
# the discriminator is the OUTPUT, never the status — a version of this loop
# keyed on the exit code would pass unconditionally.
# MEASURED under the runner's git-bash, all three cases, before this was written:
#
#   server up          → rc=0, one `sccache.exe` line
#   server down        → rc=0, no matching line
#   broken invocation  → rc=1, no matching line   ← the trap
#
# So the STATUS says whether the instrument worked and the OUTPUT says what it
# found — and they must be read separately. A loop written as
# `while tasklist //NH 2>/dev/null | grep -q …` collapses the two: a tasklist
# that failed produces no output, the loop exits on its first pass, and the
# script prints an affirmative "quiescent" line. That is instrument failure
# reading as a clean result — the same defect as the prune's `2>/dev/null | jq`
# and as the `--stop-server` check above. Not repeating it a third time.
quiescent=unknown
if command -v tasklist >/dev/null 2>&1; then
  waited=0
  while : ; do
    procs="$(tasklist //NH 2>&1)"; tl_rc=$?
    if [ "$tl_rc" != 0 ]; then
      quiescent=unverified
      echo "seed: tasklist failed (rc=$tl_rc) — quiescence NOT independently verified: $(printf '%s' "$procs" | tr -s '\n\r\t ' ' ' | cut -c1-120)"
      break
    fi
    printf '%s' "$procs" | grep -qi '^sccache\.exe' || { quiescent=yes; break; }
    if [ "$waited" -ge 30 ]; then
      note "::error::sccache-cache SEED FAILED for \`$TAG\` — an sccache process was still alive ${waited}s after the shutdown request, so $DIR_POSIX cannot be assumed quiescent. Nothing was published."
      exit 1
    fi
    sleep 2; waited=$((waited + 2))
  done
  [ "$quiescent" = yes ] && echo "seed: no sccache process remains (waited ${waited}s) — $DIR_POSIX is quiescent"
else
  quiescent=unverified
  echo "seed: tasklist unavailable — quiescence NOT independently verified (stop-server rc=$stop_rc)"
fi
# `unverified` is deliberately NOT fatal: on a host where the probe cannot run we
# are no worse off than before this check existed, and reddening a green lane
# over a missing diagnostic tool would be the wrong trade. It is, however, said
# out loud every time rather than silently assumed.

[ -d "$DIR_POSIX" ] || { echo "seed: $DIR_POSIX does not exist — nothing to publish."; exit 0; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# Uncompressed — see restore-sccache.sh. Must match the extract side.
#
# tar's status is CHECKED, and the result re-read with `tar -tf` before it is
# allowed anywhere near GHCR. This script runs without `set -e` (most failures
# here are deliberately non-fatal), which made an unchecked `tar -cf` the one
# path that could publish a TRUNCATED archive and still print the `SEEDED`
# marker the operator gates the warm run on. On Windows a single locked or
# vanished cache entry — antivirus, or the daemon's own churn — is enough:
# tar skips it, exits non-zero, and leaves a perfectly pushable tar behind.
# The listing pass is not redundant with the exit status: it is what catches a
# tar that was truncated after the fact.
if ! tar -cf "$WORK/sccache-$PRESET.tar" -C "$DIR_POSIX" . ; then
  note "::error::sccache-cache SEED FAILED for \`$TAG\` — could not archive $DIR_POSIX. Nothing was published; a following 'warm' run would measure another cold build."
  exit 1
fi
if ! tar -tf "$WORK/sccache-$PRESET.tar" >/dev/null 2>&1; then
  note "::error::sccache-cache SEED FAILED for \`$TAG\` — the archive did not read back cleanly. Nothing was published."
  exit 1
fi

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
# Whether it deletes from here depends on the token the caller supplies: with
# `secrets.GHCR_PAT` (delete:packages) it does; falling back to GITHUB_TOKEN
# (packages:write is read+write only) it cannot, and reports a backlog instead.
# It must stay runnable standalone for exactly that reason. The rolling tag
# means every republish orphans a multi-GB untagged version, so the backlog is
# surfaced into the job summary rather than left to accumulate silently. Full
# evidence in ci/prune-sccache.sh's header.
# Match the marker WITHOUT requiring a count: prune emits `PENDING ?` when it
# could not even list the package, and that is the loudest case, not one to drop
# on a failed [0-9]+ parse.
pending=$("$(dirname "$0")/prune-sccache.sh" "$PRESET" "$TAG" | tee /dev/stderr | sed -n 's/^prune: PENDING \(.*\)$/\1/p' | head -1)
if [ -n "${pending:-}" ]; then
  note "sccache-cache: **dead version(s) could not be reclaimed** from CI — \`$pending\`. Run \`ci/prune-sccache.sh $PRESET $TAG\` locally with a \`delete:packages\` token."
fi
exit 0
