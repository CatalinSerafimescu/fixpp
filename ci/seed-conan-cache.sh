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

PROFILE="${1:?usage: seed-conan-cache.sh <profile> [extra conan args] — e.g. linux-clang-libc++ -o 'fixpp/*:with_otel=False'}"
shift
EXTRA=("$@")   # lane-specific conan args — MUST match the CI lane's `conan install`
               # so the packed package_id equals what CI resolves. Examples:
               #   libc++ lanes : -o 'fixpp/*:with_otel=False'
               #   MSVC lanes   : -o 'fixpp/*:with_otel=False' -s:b compiler.cppstd=20
IMAGE="ghcr.io/catalinserafimescu/fixpp-conan-cache"

# CWD-based (same as restore-conan-cache.sh) — no path magic.
[ -f conanfile.py ] && [ -f "conan/profiles/$PROFILE" ] || {
  echo "run from the library submodule root (need conanfile.py + conan/profiles/$PROFILE)" >&2; exit 2; }

# KEY: recomputed identically by restore-conan-cache.sh — plain sha256sum, NOT
# GitHub hashFiles(), so both sides match without depending on Actions functions.
KEY="$(cat conanfile.py "conan/profiles/$PROFILE" | tr -d '\r' | sha256sum | cut -c1-16)"
# OCI tags forbid '+', so libc++ profiles must be sanitized (libc++ -> libcxx).
# No-op for '+' -free profiles → existing tags unchanged. The .tgz FILENAME keeps
# the raw profile ('+' is legal in filenames); only the tag is sanitized.
TAG="${PROFILE//+/x}-${KEY}"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# 1. Resolve the EXACT profile+options-locked package set. `conan graph info`
#    (NOT `conan install`) resolves the graph WITHOUT running generators, so it
#    (a) needs no toolchain — a Windows/MSVC profile resolves fine from Linux via
#    CONAN_HOME=/mnt/c, and (b) never builds. If a binary is "Missing", the local
#    cache lacks that package_id — build that lane locally first, then re-run.
conan graph info . -pr:a "conan/profiles/$PROFILE" "${EXTRA[@]}" --format=json > "$WORK/graph.json"

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

# 4. Prune this profile's stale tags (keep only the one just pushed). Best-effort.
"$(dirname "$0")/prune-conan-cache.sh" "$PROFILE" "$TAG" || true
