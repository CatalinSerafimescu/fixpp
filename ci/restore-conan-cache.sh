#!/usr/bin/env bash
# CI-side: pull a profile's Conan cache from GHCR → restore into CONAN_HOME.
# Runs BEFORE `conan install`. On MISS it exits 0 so the caller's existing
# `conan install --build=missing` transparently falls back (build from source).
#
# Public package → anonymous `oras pull`, no token needed.
# Requires oras on PATH (add oras-project/setup-oras@v1 to the job).
#
# Usage (from the library submodule dir, as the workflow's working directory):
#   ./restore-conan-cache.sh linux-clang-release
set -uo pipefail

PROFILE="${1:?usage: restore-conan-cache.sh <profile>}"
IMAGE="ghcr.io/catalinserafimescu/fixpp-conan-cache"

# KEY/TAG come from ci/conan-cache-key.sh — the one place seed and restore share,
# so the two can never drift into a permanent MISS. MSVC profiles additionally
# fold the VS toolset into the key (see that file for why).
# shellcheck source=ci/conan-cache-key.sh
. "$(dirname "$0")/conan-cache-key.sh"

emit() { [ -n "${GITHUB_OUTPUT:-}" ] && echo "hit=$1" >> "$GITHUB_OUTPUT"; }

# An unidentifiable MSVC toolset is dispositioned as a MISS, not as an error:
# falling through to `conan install --build=missing` is always CORRECT, just
# slower. Mirrors the MISS path below rather than inventing a new fatal one.
if ! conan_cache_key "$PROFILE"; then
  echo "conan-cache MISS ($PROFILE, toolset unidentified) → falling back to --build=missing"
  emit false
  exit 0
fi
TAG="$CONAN_CACHE_TAG"
[ -n "$CONAN_CACHE_TOOLSET" ] && echo "conan-cache: MSVC toolset $CONAN_CACHE_TOOLSET folded into the key"

WORK="$(mktemp -d)"
if oras pull "$IMAGE:$TAG" -o "$WORK" >/dev/null 2>&1; then
  conan cache restore "$(winpath "$WORK/conan-$PROFILE.tgz")"
  echo "conan-cache HIT  $TAG"
  emit true
else
  # MISS: conanfile/profile changed (no seeded artifact) or GHCR unreachable.
  # Not fatal — the subsequent `conan install --build=missing` rebuilds. On
  # push:main the "Save Conan cache to GHCR" step then auto-pushes the rebuilt
  # cache (gated on hit=false) so the next run hits.
  echo "conan-cache MISS $TAG  → falling back to --build=missing"

  # An MSVC miss is LOUD, because it is self-inflicted and it does not
  # self-clear on a PR. Seeding is gated to push:main and workflow_dispatch, so
  # after a GitHub image bump changes the toolset, EVERY tier2 PR leg rebuilds
  # the whole OTel/protobuf/abseil chain from source, over and over, until a
  # maintainer dispatches a reseed. Without this the only symptom is one quiet
  # log line and a lane that got slow for no visible reason.
  if [ -n "${CONAN_CACHE_TOOLSET:-}" ]; then
    echo "::warning::No GHCR Conan cache for MSVC toolset ${CONAN_CACHE_TOOLSET} (tag ${TAG}). Every PR leg will rebuild the OTel chain from source until this is reseeded — dispatch tier2.yml on main to republish."
  fi
  emit false
fi
