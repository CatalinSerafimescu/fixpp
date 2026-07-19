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

# Same KEY basis as seed-conan-cache.sh (plain sha256sum of conanfile + profile).
# tr -d '\r': make the key line-ending-independent so a CRLF Windows checkout
# hashes identically to the LF Linux seed (no-op on Linux → existing tags stay valid).
KEY="$(cat conanfile.py "conan/profiles/$PROFILE" | tr -d '\r' | sha256sum | cut -c1-16)"
# OCI tags forbid '+' → sanitize (libc++ -> libcxx); no-op for '+'-free profiles.
TAG="${PROFILE//+/x}-${KEY}"

emit() { [ -n "${GITHUB_OUTPUT:-}" ] && echo "hit=$1" >> "$GITHUB_OUTPUT"; }

WORK="$(mktemp -d)"
if oras pull "$IMAGE:$TAG" -o "$WORK" >/dev/null 2>&1; then
  conan cache restore "$WORK/conan-$PROFILE.tgz"
  echo "conan-cache HIT  $TAG"
  emit true
else
  # MISS: conanfile/profile changed (no seeded artifact) or GHCR unreachable.
  # Not fatal — the subsequent `conan install --build=missing` rebuilds. On
  # push:main the "Save Conan cache to GHCR" step then auto-pushes the rebuilt
  # cache (gated on hit=false) so the next run hits.
  echo "conan-cache MISS $TAG  → falling back to --build=missing"
  emit false
fi
