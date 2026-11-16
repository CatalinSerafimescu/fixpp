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

sccache_archive_names "$PRESET"

# 103 (tasks T160): the archive is compressed at this level, one named constant
# (the restore decompresses whatever level the seed used).
SCCACHE_ARCHIVE_ZSTD_LEVEL=3
# Each part is one OCI layer. GHCR documents a per-layer limit of 10 GB
# (docs.github.com, "Working with the Container registry"), read here as
# 10,000,000,000 bytes; a part is that limit less a 500,000,000-byte margin.
# SCCACHE_ARCHIVE_PART_BYTES is a test seam (ci/test-sccache-archive.sh sets a
# small value to get several parts); no workflow sets it.
PART_BYTES="${SCCACHE_ARCHIVE_PART_BYTES:-9500000000}"
case "$PART_BYTES" in
  ''|*[!0-9]*|0) note "::error::sccache-cache SEED FAILED for \`$TAG\` — SCCACHE_ARCHIVE_PART_BYTES='$PART_BYTES' is not a positive byte count. Nothing was published."; exit 1 ;;
esac

for tool in zstd split sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || {
    note "::error::sccache-cache SEED FAILED for \`$TAG\` — \`$tool\` is not on PATH. Nothing was published."
    exit 1
  }
done

WORK="$(sccache_archive_workdir)" || { note "::error::sccache-cache SEED FAILED for \`$TAG\` — no work directory. Nothing was published."; exit 1; }
trap 'rm -rf "$WORK"' EXIT

# One pipeline, so a whole tar or a whole .zst never sits on disk beside the parts.
# Every stage's status is CHECKED. This script runs without `set -e` (most
# failures here are deliberately non-fatal), which made an unchecked `tar -cf` the
# one path that could publish a TRUNCATED archive and still print the `SEEDED`
# marker the operator gates the warm run on. On Windows a single locked or
# vanished cache entry — antivirus, or the daemon's own churn — is enough: tar
# skips it, exits non-zero, and leaves a perfectly pushable archive behind.
tar -cf - -C "$DIR_POSIX" . \
  | zstd -q -"$SCCACHE_ARCHIVE_ZSTD_LEVEL" -T0 -c \
  | ( cd "$WORK" && split -b "$PART_BYTES" -d -a 3 - "$SCCACHE_ARCHIVE_PART_PREFIX" )
pipe=("${PIPESTATUS[@]}")
if [ "${pipe[*]}" != "0 0 0" ]; then
  note "::error::sccache-cache SEED FAILED for \`$TAG\` — could not archive $DIR_POSIX (tar | zstd | split exited ${pipe[*]}). Nothing was published; a following 'warm' run would measure another cold build."
  exit 1
fi

parts=()
while IFS= read -r f; do parts+=("$f"); done < <(cd "$WORK" && ls -1 | grep -E "^${SCCACHE_ARCHIVE_PART_PREFIX//./\\.}[0-9]{3}\$" | sort)
if [ "${#parts[@]}" -eq 0 ]; then
  note "::error::sccache-cache SEED FAILED for \`$TAG\` — the archive produced no part. Nothing was published."
  exit 1
fi

# The listing pass is not redundant with the statuses above: it reads back the
# parts in order, through the same decompression the restore uses, and is what
# catches an archive that was truncated after the fact.
( cd "$WORK" && cat "${parts[@]}" ) | zstd -q -dc | tar -tf - >/dev/null 2>&1
pipe=("${PIPESTATUS[@]}")
if [ "${pipe[*]}" != "0 0 0" ]; then
  note "::error::sccache-cache SEED FAILED for \`$TAG\` — the archive did not read back cleanly (cat | zstd -d | tar -t exited ${pipe[*]}). Nothing was published."
  exit 1
fi

# The manifest: index, name and sha256 of each part, in order. The digest is read
# from stdin so no file-name column (which can carry a mode marker) is parsed.
idx=0
: > "$WORK/$SCCACHE_ARCHIVE_MANIFEST"
for f in "${parts[@]}"; do
  digest="$(sha256sum < "$WORK/$f" | cut -d' ' -f1)"
  case "$digest" in
    *[!0-9a-f]*|'') note "::error::sccache-cache SEED FAILED for \`$TAG\` — could not hash $f. Nothing was published."; exit 1 ;;
  esac
  printf '%03d %s %s\n' "$idx" "$f" "$digest" >> "$WORK/$SCCACHE_ARCHIVE_MANIFEST"
  idx=$((idx + 1))
done

# Report the SIZES BEFORE attempting the push, not after. SCCACHE_CACHE_SIZE is
# set per leg in tier2.yml from the condition its comment states, and is checked
# against these two numbers — and the run most likely to fail its push (the first
# one, before the GHCR package exists) is exactly the run whose sizing datum would
# otherwise be lost. The archive figure is the total of the parts.
note "sccache-cache archive \`$TAG\` — $( (cd "$WORK" && du -ch "${parts[@]}") | tail -1 | cut -f1) archive, $(du -sh "$DIR_POSIX" | cut -f1) on disk (cap \`${SCCACHE_CACHE_SIZE:-default}\`)"

# Push from inside $WORK with RELATIVE filenames so oras' absolute-path guard
# passes and each layer's title is the clean basename (same reason as
# seed-conan-cache.sh). One layer per part, plus the manifest.
push_files=("$SCCACHE_ARCHIVE_MANIFEST:text/plain")
for f in "${parts[@]}"; do push_files+=("$f:application/zstd"); done
( cd "$WORK" && oras push "$IMAGE:$TAG" \
    --artifact-type application/vnd.fixpp.sccache.v1 \
    --annotation "org.opencontainers.image.source=https://github.com/CatalinSerafimescu/fixpp" \
    --annotation "fixpp.preset=$PRESET" \
    --annotation "fixpp.msvc.toolset=$SCCACHE_CACHE_TOOLSET" \
    "${push_files[@]}" ) || {
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
#
# ⚠️ A SECOND COPY OF THIS BLOCK LIVES IN ci/seed-ccache.sh — same eleven lines,
# three literals changed. It points here for the rationale, so this is the
# canonical side: a correction made here must be applied there too. The pointer
# was one-directional until this line existed, which meant whoever edited the
# canonical copy had no indication the other one existed — reproducing the exact
# failure the pointer cites (ci/prune-conan-cache.sh's "FAIL CLOSED ON A FAILED READ" section, "the backport never
# happened"). Note the asymmetry that makes it bite: the COPY is pinned by
# ci/test-ccache-scripts.sh; this one is not pinned by anything.
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
#
# ⚠️ NOT `| tee /dev/stderr |`. Git Bash on the windows runner has no
# `/dev/stderr`, so that form died with `tee: /dev/stderr: No such file or
# directory` (verbatim in jobs 92919721349 / 92919721354) and threw away EVERY
# per-version prune line — the only place the actual API refusal is reported.
# What survived was the count in the job-summary note, which is exactly the
# signal that cannot distinguish "nothing to delete" from "nothing could be
# deleted". Tee to a temp file and cat it: portable, and the file is also what
# the count is parsed from, so the log and the note can never disagree.
# Inside the already-trapped $WORK (this script's `WORK="$(mktemp -d)"`) rather than a fresh `mktemp`: it is
# cleaned up on interruption too, and it cannot leave an unset path behind if
# mktemp fails — this script is `set -uo pipefail`, not `set -e`, so a failed
# `mktemp` would have continued with an empty filename, lost the prune output,
# and emitted no note at all.
prune_log="$WORK/prune.log"

# CAPTURE THE STATUS, do not `|| true` it away. Best-effort must mean "never
# fails the seed", NOT "ignore instrument failure": if the pruner dies before
# emitting its own marker, `|| true` left NO prune disposition in either the log
# or the job summary — a silent hole in the very thing being instrumented.
# Synthesising the marker here keeps the one parse below authoritative.
prune_rc=0
"$(dirname "$0")/prune-sccache.sh" "$PRESET" "$TAG" > "$prune_log" 2>&1 || prune_rc=$?
if [ "$prune_rc" -ne 0 ]; then
  echo "prune: PENDING ? — the prune command exited $prune_rc before reporting a disposition" >> "$prune_log"
fi
cat "$prune_log"
# FIRST TOKEN ONLY. `\(.*\)$` swallowed the whole remainder of the line, so the
# job-summary note rendered the count plus prune's entire parenthetical remedy
# sentence. Pre-existing, and harmless-looking, but the note is the surface a
# maintainer actually reads. `[^ ]*` still matches BOTH shapes the marker takes —
# a count (`PENDING 2`) and the could-not-even-list case (`PENDING ?`), which is
# the loudest one and must never be dropped on a failed [0-9]+ parse.
pending=$(sed -n 's/^prune: PENDING \([^ ]*\).*$/\1/p' "$prune_log" | head -1)
# No `rm` — $WORK's EXIT trap owns it.
if [ -n "${pending:-}" ]; then
  note "sccache-cache: **dead version(s) could not be reclaimed** from CI — \`$pending\`. Run \`ci/prune-sccache.sh $PRESET $TAG\` locally with a \`delete:packages\` token."
fi
exit 0
