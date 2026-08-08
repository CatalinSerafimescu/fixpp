#!/usr/bin/env bash
# CI-side (Tier 3, #240): publish this run's ccache directory to GHCR under a
# rolling preset+compiler tag. Runs AFTER Build (so the cache is populated) and
# BEFORE the ctest steps (so a red test does not withhold a cache that is purely
# about compilation).
#
# Gated by the caller to push:main / workflow_dispatch-on-main — both maintainer
# triggered, never a PR — mirroring the Conan save step next to it.
#
# ⚠️ THE GATE'S REASON IS NOT THE CONAN GATE'S REASON, and the plan that
# scheduled this work stated the wrong one ("the key is content-derived, so a PR
# computes main's tag"). That describes the CONAN tag. This tag is deliberately
# COARSE AND ROLLING (see ci/ccache-cache-key.sh) so a PR leg can pull what main
# published — which means EVERY branch computes the SAME tag no matter what it
# changed. The gate is therefore MORE load-bearing here than on the Conan step,
# not less: without it a dispatch from any branch overwrites, at the identical
# tag, the artifact main reads next.
#
# It is a COST property, not a safety one: ccache keys entries on the compiler,
# the argument list and the preprocessed source, so a foreign entry is a MISS,
# never a wrong object. What it costs is main's warm cache being replaced by a
# feature branch's, making the next push:main legs rebuild cold.
#
# Requires oras on PATH and an `oras login ghcr.io` already performed by the
# caller. Best-effort by contract: the caller sets continue-on-error, because
# publishing is an optimisation and a GHCR hiccup must not redden a green lane.
#
# ── WHAT THIS DELIBERATELY DOES *NOT* DO, AND WHY ────────────────────────────
#
# seed-sccache.sh spends ~60 lines stopping the sccache daemon and then PROVING
# it is gone, because sccache writes entries through a background server and
# tarring a live cache directory can capture a half-written entry. ccache has no
# daemon: every write happens synchronously inside the compiler invocation that
# caused it, and the Build step has completed by the time this runs. There is no
# process to stop, so there is nothing to verify — the quiescence argument is
# discharged by the architecture, not skipped. If a future change ever puts a
# ccache background process in this lane (`ccache --daemon`-style inotify
# helpers, a parallel test step that compiles), this reasoning stops holding and
# the sccache-side check has to be ported.
#
# Usage (from the library root):
#   ci/seed-ccache.sh linux-clang-libc++
#
# ⚠️ `set -uo pipefail` WITHOUT `-e` — see restore-ccache.sh's header.
set -uo pipefail

PRESET="${1:?usage: seed-ccache.sh <preset>}"
IMAGE="${FIXPP_CCACHE_IMAGE:-ghcr.io/catalinserafimescu/fixpp-ccache}"

# shellcheck source=ci/ccache-cache-key.sh
. "$(dirname "$0")/ccache-cache-key.sh"

note() { echo "$1"; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }

: "${CCACHE_DIR:?CCACHE_DIR must be set (the workflow sets it job-wide)}"

if ! ccache_cache_key "$PRESET"; then
  note "ccache-cache: no identifiable compiler for \`$PRESET\` — nothing published, so the next run will be cold."
  exit 0
fi
TAG="$CCACHE_CACHE_TAG"

[ -d "$CCACHE_DIR" ] || { note "ccache-cache: $CCACHE_DIR does not exist — nothing to publish."; exit 0; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# Uncompressed — see restore-ccache.sh. Must match the extract side.
#
# tar's status is CHECKED, and the result re-read with `tar -tf` before it is
# allowed anywhere near GHCR. This script runs without `set -e` (most failures
# here are deliberately non-fatal), which makes an unchecked `tar -cf` the one
# path that could publish a TRUNCATED archive and still print the `SEEDED`
# marker. The listing pass is not redundant with the exit status: it is what
# catches a tar that was truncated after the fact.
if ! tar -cf "$WORK/ccache-$PRESET.tar" -C "$CCACHE_DIR" . ; then
  note "::error::ccache-cache SEED FAILED for \`$TAG\` — could not archive $CCACHE_DIR. Nothing was published; a following 'warm' run would measure another cold build."
  exit 1
fi
if ! tar -tf "$WORK/ccache-$PRESET.tar" >/dev/null 2>&1; then
  note "::error::ccache-cache SEED FAILED for \`$TAG\` — the archive did not read back cleanly. Nothing was published."
  exit 1
fi

# ── THE SIZING DATUM #240 ASKS FOR — REPORTED BEFORE THE PUSH, NOT AFTER ─────
#
# #240's acceptance wants "the first GHCR tarball size recorded as the UNCAPPED
# DEMAND MEASUREMENT that replaces the circular 460 MB figure" — circular
# because the old `~460 MB each` comment in tier3-libcxx.yml measured what the
# 500M cap ALLOWED, then cited it as evidence the cap was big enough.
#
# Reported before the push for the same reason seed-sccache.sh:140-144 does: the
# run most likely to FAIL its push is the first one, before the GHCR package
# exists — exactly the run whose sizing datum would otherwise be lost.
#
# `du` on the directory, not just the tarball: the tar is uncompressed over
# entries ccache already compressed (CCACHE_COMPRESSLEVEL=5 job-wide), so the two
# numbers should track each other closely. If they ever diverge sharply, the
# compression config moved and the transfer cost changed with it.
note "ccache-cache archive \`$TAG\` — $(du -h "$WORK/ccache-$PRESET.tar" | cut -f1) archive, $(du -sh "$CCACHE_DIR" | cut -f1) on disk (cap \`${CCACHE_MAXSIZE:-ccache default}\`)"

# Push from inside $WORK with a RELATIVE filename so oras' absolute-path guard
# passes and the artifact title is the clean basename (same reason as
# seed-conan-cache.sh).
( cd "$WORK" && oras push "$IMAGE:$TAG" \
    --artifact-type application/vnd.fixpp.ccache.v1 \
    --annotation "org.opencontainers.image.source=https://github.com/CatalinSerafimescu/fixpp" \
    --annotation "fixpp.preset=$PRESET" \
    --annotation "fixpp.compiler=$CCACHE_CACHE_COMPILER" \
    --annotation "fixpp.compiler.id=$CCACHE_CACHE_TOOLSET" \
    "ccache-$PRESET.tar:application/x-tar" ) || {
  # The caller sets continue-on-error, so this exit is INVISIBLE in the step's
  # status. Shout into the summary instead — a silently unseeded cache makes the
  # next run a second COLD build that reads as a warm one.
  note "::error::ccache-cache SEED FAILED for \`$TAG\` — the cache was NOT published. A following 'warm' run would measure another cold build."
  exit 1
}

# The literal token a warm-run dispatch is gated on. Do not reword without
# updating the plan's acceptance section, which tells the operator to grep for it.
note "ccache-cache SEEDED \`$TAG\`"

# ── Prune, best-effort — delegated so it is also runnable STANDALONE ─────────
#
# ⚠️ THE RATIONALE FOR THIS BLOCK IS NOT RESTATED HERE — IT IS CANONICAL IN
# ci/seed-sccache.sh (the "Prune, best-effort" tail). This block is the same
# eleven lines with three literals changed (the pruner's name, the `ccache-cache`
# label, the remedy sentence), and every non-obvious choice in it — `$WORK`
# rather than a fresh `mktemp`, capturing the status instead of `|| true`,
# matching the marker WITHOUT requiring a numeric count, `[^ ]*` rather than
# `\(.*\)$` — is argued there against the incident that produced it.
#
# Deliberately a POINTER rather than a copy. Duplicating ~35 lines of evidence
# comments is precisely how a correction lands on one side only, and this repo
# has already paid that bill once in this very directory:
# ci/prune-conan-cache.sh:45 — "prune-sccache.sh was fixed for this on
# 2026-08-06; the backport never happened." Read the two blocks together before
# changing either.
#
# The CODE is deliberately not shared, and that is a narrower decision than it
# looks: extracting it would edit ci/seed-sccache.sh, which this PR does not
# touch and which has no harness, so the Tier 2 copy would be unpinned at the
# moment of the edit. Recorded as a follow-up with ci/prune-conan-cache.sh's
# fold-in rather than taken here.
#
# What IS specific to this tier: the rolling tag means every republish orphans a
# multi-GB untagged version, and there are FOUR legs doing it per push:main —
# which is what makes the backlog note load-bearing here rather than tidy.
prune_log="$WORK/prune.log"

prune_rc=0
"$(dirname "$0")/prune-ccache.sh" "$PRESET" "$TAG" > "$prune_log" 2>&1 || prune_rc=$?
if [ "$prune_rc" -ne 0 ]; then
  echo "prune: PENDING ? — the prune command exited $prune_rc before reporting a disposition" >> "$prune_log"
fi
cat "$prune_log"
pending=$(sed -n 's/^prune: PENDING \([^ ]*\).*$/\1/p' "$prune_log" | head -1)
# No `rm` — $WORK's EXIT trap owns it.
if [ -n "${pending:-}" ]; then
  note "ccache-cache: **dead version(s) could not be reclaimed** from CI — \`$pending\`. Run \`ci/prune-ccache.sh $PRESET $TAG\` locally with a \`delete:packages\` token."
fi
exit 0
