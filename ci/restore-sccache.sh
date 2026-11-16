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

# The HIT path below does `rm -rf "$DIR_POSIX"`. Everything protecting that call
# lives OUTSIDE this script — the workflow happens to set `C:\sccache`. Make the
# guard local instead: a drive root converts to `/c` (or `/c/`), which
# `rm --preserve-root` does NOT protect the way it protects `/`, so a one-line
# workflow edit would recursively delete the runner's system drive. Require at
# least two path components below the root before anything destructive runs.
# `C:\sccache` → `/c/sccache`: two components, fine. `C:\` → `/c`: one, refused.
DIR_TRIMMED="${DIR_POSIX%/}"
case "$DIR_TRIMMED" in
  */*/?*) : ;;   # at least two components AND a non-empty leaf
  *)
    echo "::error::SCCACHE_DIR=${SCCACHE_DIR} resolves to '${DIR_POSIX}', which is at or directly below a filesystem root. Refusing to run: this script removes that directory on a cache hit, and 'rm -rf /c' is not something --preserve-root stops."
    exit 1 ;;
esac
case "$DIR_TRIMMED" in
  *..*) echo "::error::SCCACHE_DIR=${SCCACHE_DIR} contains '..'; refusing to use an unnormalised path as a removable directory."; exit 1 ;;
esac

sccache_archive_names "$PRESET"

# STAGE sits beside SCCACHE_DIR, not under $WORK, so the swap-in below is a
# rename on the same volume rather than a multi-GB copy across one. $WORK, which
# holds the pulled parts, is on the workspace drive (sccache_archive_workdir).
STAGE="${DIR_POSIX%/}.restore.$$"
WORK="$(sccache_archive_workdir)" || WORK=""
trap 'rm -rf "$WORK" "$STAGE"' EXIT
rm -rf "$STAGE"

# extract_parts — the 103 format (tasks T160): the manifest's parts, checked
# against it, decompressed and extracted into STAGE. Prints a MISS reason and
# returns 1 on any failure; STAGE is then discarded by the caller.
extract_parts() {
  local m="$WORK/$SCCACHE_ARCHIVE_MANIFEST" line idx name digest want n=0
  local pat="${SCCACHE_ARCHIVE_PART_PREFIX//./\\.}"
  local -a parts=()
  for tool in zstd sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || { echo "\`$tool\` is not on PATH"; return 1; }
  done
  # The manifest comes from the registry: every name must be the fixed pattern at
  # its own index, in order from 000, which also rules out any path in it.
  while IFS= read -r line || [ -n "$line" ]; do
    if ! printf '%s\n' "$line" | grep -Eq "^[0-9]{3} ${pat}[0-9]{3} [0-9a-f]{64}\$"; then
      echo "the manifest is malformed at line $((n + 1))"; return 1
    fi
    idx="${line%% *}"; name="${line#* }"; digest="${name#* }"; name="${name%% *}"
    want="$(printf '%03d' "$n")"
    if [ "$idx" != "$want" ] || [ "$name" != "${SCCACHE_ARCHIVE_PART_PREFIX}${want}" ]; then
      echo "the manifest lists $name at index $idx where part $want belongs"; return 1
    fi
    if [ ! -f "$WORK/$name" ]; then
      echo "part $name, listed in the manifest, was not pulled"; return 1
    fi
    if [ "$(sha256sum < "$WORK/$name" | cut -d' ' -f1)" != "$digest" ]; then
      echo "part $name does not match the manifest"; return 1
    fi
    parts+=("$WORK/$name"); n=$((n + 1))
  done < "$m"
  [ "$n" -gt 0 ] || { echo "the manifest lists no part"; return 1; }
  cat "${parts[@]}" | zstd -q -t
  local -a pipe=("${PIPESTATUS[@]}")
  [ "${pipe[*]}" = "0 0" ] || { echo "the parts did not decompress cleanly (cat | zstd -t exited ${pipe[*]})"; return 1; }
  mkdir -p "$STAGE" || { echo "could not create the staging directory"; return 1; }
  cat "${parts[@]}" | zstd -q -dc | tar -xf - -C "$STAGE"
  pipe=("${PIPESTATUS[@]}")
  [ "${pipe[*]}" = "0 0 0" ] || { echo "the archive did not extract cleanly (cat | zstd -d | tar -x exited ${pipe[*]})"; return 1; }
  # The parts are no longer needed; free their space before the swap.
  rm -f "${parts[@]}"
  return 0
}

# Extract into STAGE and swap in only on success — never straight into the live
# directory. Without `set -e` an unchecked extraction failure was invisible: a
# truncated archive (whose OCI digest is perfectly valid for its truncated
# bytes) extracts a prefix, returns non-zero, and the old code went on to emit
# `HIT` and `hit=true` over a partial cache. That is worse than a MISS in both
# directions — the summary records a restore that did not happen, and sccache
# serves from a directory nobody verified. Every leg of the condition is
# checked: the pull, the archive's presence, and the extraction itself.
miss_reason=""
if [ -z "$WORK" ]; then
  note "sccache-cache MISS \`$TAG\` — no work directory for the pull; compiling with an empty cache"
  emit false
elif ! oras pull "$IMAGE:$TAG" -o "$WORK" >/dev/null 2>&1; then
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
else
  # Format detection: a manifest is the 103 format; else the legacy single-layer
  # uncompressed tar a pre-103 seed wrote, read as before; else nothing usable.
  if [ -f "$WORK/$SCCACHE_ARCHIVE_MANIFEST" ]; then
    miss_reason="$(extract_parts)" || true
    [ -z "$miss_reason" ] || miss_reason="pulled, but $miss_reason"
  elif [ -f "$WORK/$SCCACHE_ARCHIVE_LEGACY" ]; then
    { mkdir -p "$STAGE" && tar -xf "$WORK/$SCCACHE_ARCHIVE_LEGACY" -C "$STAGE"; } \
      || miss_reason="pulled, but the legacy archive did not extract cleanly"
  else
    miss_reason="pulled, but the archive was missing"
  fi
  if [ -n "$miss_reason" ]; then
    # Same disposition as a plain miss: a cache we could not verify is a cache we
    # do not have. The partial tree is discarded rather than swapped in.
    rm -rf "$STAGE"
    note "sccache-cache MISS \`$TAG\` — ${miss_reason}. Treated as no cache; compiling from scratch."
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
