#!/usr/bin/env bash
# CI-side (Tier 3, #240): pull a preset's ccache directory from GHCR → restore
# into CCACHE_DIR.
#
# MUST run BEFORE `Conan install`, not merely before Configure. libc++ has no
# prebuilt Conan Center binaries, so `conan install --build=missing` compiles
# the dependency closure from source through the job-level
# CMAKE_{C,CXX}_COMPILER_LAUNCHER=ccache. Restoring after that point leaves
# every dependency TU a guaranteed miss — the single largest block of compile
# work on this lane, and the one the previous ccache-action ordering was
# specifically written to cover. The constraint is restated at the "Install
# ccache" and "Restore ccache from GHCR" steps in .github/workflows/
# tier3-libcxx.yml — cited by STEP NAME rather than line number on purpose,
# because this PR moved those lines and a stale anchor pointing into a plausible
# neighbouring block is worse than no anchor.
#
# On MISS it exits 0 and the build simply compiles everything, populating a
# cache that the seed step then publishes. A compiler cache that is down must
# NEVER redden a lane whose build and tests pass.
#
# Requires oras on PATH (oras-project/setup-oras@v2), ccache installed, and
# CCACHE_DIR set.
#
# Usage (from the library root, as the workflow's working directory):
#   ci/restore-ccache.sh linux-clang-libc++
#
# ⚠️ `set -uo pipefail` WITHOUT `-e`, deliberately, matching every other script
# in this directory. Most failures here are non-fatal by design, and the
# `note()` idiom below (`[ -n "$X" ] && echo`) returns non-zero on its PASSING
# branch — under `set -e` that would kill the step exactly when nothing is
# wrong. Do not "harden" this line.
set -uo pipefail

PRESET="${1:?usage: restore-ccache.sh <preset>}"
IMAGE="${FIXPP_CCACHE_IMAGE:-ghcr.io/catalinserafimescu/fixpp-ccache}"

# shellcheck source=ci/ccache-cache-key.sh
. "$(dirname "$0")/ccache-cache-key.sh"

emit() { [ -n "${GITHUB_OUTPUT:-}" ] && echo "hit=$1" >> "$GITHUB_OUTPUT"; }
note() { echo "$1"; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }

: "${CCACHE_DIR:?CCACHE_DIR must be set (the workflow sets it job-wide)}"
mkdir -p "$CCACHE_DIR"

if ! ccache_cache_key "$PRESET"; then
  note "ccache-cache MISS ($PRESET, compiler unidentified) → building with an empty cache"
  emit false
  exit 0
fi
TAG="$CCACHE_CACHE_TAG"
echo "ccache-cache: compiler $CCACHE_CACHE_COMPILER ($CCACHE_CACHE_TOOLSET) folded into the tag"

# The HIT path below does `rm -rf "$CCACHE_DIR"`. Everything protecting that
# call lives OUTSIDE this script — the workflow happens to set
# /tmp/fixpp-ccache-<preset>. Make the guard local instead: require at least two
# path components below the root before anything destructive runs.
# `/tmp/fixpp-ccache-x` → two components, fine. `/tmp` → one, refused.
# Carried over verbatim in intent from restore-sccache.sh:38-54; the reasoning
# is not Windows-specific and the failure it prevents is unrecoverable.
DIR_TRIMMED="${CCACHE_DIR%/}"
case "$DIR_TRIMMED" in
  */*/?*) : ;;   # at least two components AND a non-empty leaf
  *)
    echo "::error::CCACHE_DIR=${CCACHE_DIR} is at or directly below a filesystem root. Refusing to run: this script removes that directory on a cache hit."
    exit 1 ;;
esac
case "$DIR_TRIMMED" in
  *..*) echo "::error::CCACHE_DIR=${CCACHE_DIR} contains '..'; refusing to use an unnormalised path as a removable directory."; exit 1 ;;
esac

# STAGE sits beside CCACHE_DIR, not under $WORK, so the swap-in below is a
# rename on the same volume rather than a multi-GB copy across one.
STAGE="${DIR_TRIMMED}.restore.$$"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK" "$STAGE"' EXIT
rm -rf "$STAGE"

# Extract into STAGE and swap in only on success — never straight into the live
# directory. Without `set -e` an unchecked `tar -xf` failure is invisible: a
# truncated archive (whose OCI digest is perfectly valid for its truncated
# bytes) extracts a prefix, returns non-zero, and a naive script goes on to
# report `HIT` over a partial cache. That is worse than a MISS in both
# directions — the summary records a restore that did not happen, and ccache
# serves from a directory nobody verified. Every leg of the condition is
# checked: the pull, the archive's presence, and the extraction itself.
if ! oras pull "$IMAGE:$TAG" -o "$WORK" >/dev/null 2>&1; then
  # MISS: no cache published for this preset+compiler yet, the package is not
  # readable from this context, or GHCR is unreachable. All three are the same
  # disposition — compile from scratch. Deliberately NOT a ::warning:: (see
  # restore-sccache.sh:74-80): a miss here is the pre-seed steady state on any
  # new compiler and costs only the speedup, never correctness — and the stats
  # step reports the resulting 0 % hit rate on the same run, so nothing is
  # hidden by staying quiet.
  note "ccache-cache MISS \`$TAG\` → compiling with an empty cache"
  emit false

# Uncompressed tar on purpose: ccache already stores every entry compressed
# (CCACHE_COMPRESSLEVEL=5 job-wide), so gzipping the archive spends CPU to save
# almost nothing. Must match seed-ccache.sh.
elif ! { [ -f "$WORK/ccache-$PRESET.tar" ] \
         && mkdir -p "$STAGE" \
         && tar -xf "$WORK/ccache-$PRESET.tar" -C "$STAGE"; }; then
  # Pulled, but the payload is not usable. Same disposition as a plain miss: a
  # cache we could not verify is a cache we do not have. The partial tree is
  # discarded rather than swapped in.
  rm -rf "$STAGE"
  note "ccache-cache MISS \`$TAG\` — pulled, but the archive was missing or did not extract cleanly. Treated as no cache; compiling from scratch."
  emit false

else
  rm -rf "$CCACHE_DIR"
  if mv "$STAGE" "$CCACHE_DIR"; then
    note "ccache-cache HIT  \`$TAG\`  ($(du -sh "$CCACHE_DIR" | cut -f1) restored)"
    emit true
  else
    # CCACHE_DIR has already been removed, so recreate it empty rather than let
    # the compiles below run against a directory that may not exist.
    mkdir -p "$CCACHE_DIR"
    note "ccache-cache MISS \`$TAG\` — restored tree could not be moved into ${CCACHE_DIR}; compiling with an empty cache"
    emit false
  fi
fi

# Zero the counters so the stats step measures THIS RUN, not this run plus every
# run that contributed to the restored cache. A restored ccache carries its
# producer's cumulative counters, so without this the reported hit rate is a
# number about the cache's whole history — which reads as a plausible result and
# is not the one #240's acceptance asks for.
#
# ⚠️ NOT fatal, and that is the difference from restore-sccache.sh's
# `--zero-stats`. There the same call doubles as "can the server start", and a
# server that will not start fails every subsequent compile. ccache has no
# server: a failed `-z` costs the MEASUREMENT, not the build. So this warns —
# and says exactly what is no longer trustworthy — instead of reddening a lane
# whose build and tests are fine.
if ! ccache --zero-stats >/dev/null 2>&1; then
  echo "::warning::\`ccache --zero-stats\` failed, so the counters were not reset. The build is unaffected, but the hit rate reported by the ccache statistics step below is cumulative over the restored cache's history and must NOT be read as this run's."
fi
