#!/usr/bin/env bash
# Seed a profile's Conan binaries from the LOCAL cache → GHCR (run LOCALLY, once,
# when you're ready — after `conan install` for the profile has populated the
# local cache). Reuses what's already on disk; does NOT rebuild.
#
# Prereqs:
#   - oras CLI installed + `oras login ghcr.io -u <you> -p <PAT:packages:write>`
#   - the profile's packages already present in the local Conan cache
#     (host clang-22.1.2 / gcc-13 on ubuntu-24.04 WSL userland → package_id
#      matches the ubuntu-24.04 runner; MSVC: run with
#      CONAN_HOME=/mnt/c/Users/Catalin/.conan2)
#
# Run from the library submodule ROOT (where conanfile.py + conan/profiles/ live):
#   cd <library>; ci/seed-conan-cache.sh linux-clang-release
#   cd <library>; CONAN_HOME=/mnt/c/Users/Catalin/.conan2 ci/seed-conan-cache.sh windows-msvc-release
set -euo pipefail

PROFILE="${1:?usage: seed-conan-cache.sh <profile>   e.g. linux-clang-release}"
IMAGE="ghcr.io/catalinserafimescu/fixpp-conan-cache"

# CWD-based (same as restore-conan-cache.sh) — no path magic.
[ -f conanfile.py ] && [ -f "conan/profiles/$PROFILE" ] || {
  echo "run from the library submodule root (need conanfile.py + conan/profiles/$PROFILE)" >&2; exit 2; }

# KEY: recomputed identically by restore-conan-cache.sh — plain sha256sum, NOT
# GitHub hashFiles(), so both sides match without depending on Actions functions.
KEY="$(cat conanfile.py "conan/profiles/$PROFILE" | sha256sum | cut -c1-16)"
TAG="${PROFILE}-${KEY}"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# 1. Resolve the EXACT profile-locked package set from the local cache (no build).
#    If this errors "missing binary", the local cache is incomplete for this
#    profile — build it locally first (conan install ... --build=missing), then
#    re-run. We deliberately do NOT pass --build=missing here: the point is to
#    pack what already exists, never to rebuild.
conan install . -pr:a "conan/profiles/$PROFILE" --format=json -of "$WORK/gen" > "$WORK/graph.json"

# 2. Turn the graph into a package list, then pack ONLY those package_ids
#    (a bare `conan cache save '<ref>:*'` would also grab stale clang-18 ids).
conan list --graph="$WORK/graph.json" --graph-binaries="*" --format=json > "$WORK/pkglist.json"
conan cache save --list="$WORK/pkglist.json" --file="$WORK/conan-$PROFILE.tgz"

# 3. Push to GHCR as a public OCI artifact, linked to the fixpp repo.
#    Push from inside $WORK with a RELATIVE filename so oras' absolute-path guard
#    passes and the artifact title is the clean basename (not a random /tmp path).
( cd "$WORK" && oras push "$IMAGE:$TAG" \
    --artifact-type application/vnd.fixpp.conan-cache.v1 \
    --annotation "org.opencontainers.image.source=https://github.com/CatalinSerafimescu/fixpp" \
    --annotation "fixpp.profile=$PROFILE" \
    "conan-$PROFILE.tgz:application/gzip" )

echo "seeded $IMAGE:$TAG   ($(du -h "$WORK/conan-$PROFILE.tgz" | cut -f1))"
