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

# Printed on BOTH the hit and the miss path, deliberately. The whole point is to
# diff a MISSing run against a HITting one to see which input moved, and that is
# impossible if only the failing side prints its inputs.
echo "conan-cache: key inputs — $CONAN_CACHE_INPUTS"

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

  # ── WHY THIS ANNOTATION FIRES ON EVERY LANE, NOT JUST MSVC (#222) ─────────
  #
  # It used to be wrapped in `if [ -n "$CONAN_CACHE_TOOLSET" ]`, i.e. MSVC only.
  # But conan_cache_key hashes the PROFILE FILE on every platform, so a
  # `conan/profiles/**` touch misses everywhere at once — Tier 1 and Tier 3 just
  # said so at `echo` level. The real cost of such a touch is "every lane in all
  # three tiers rebuilds its Conan deps from source", and the log made that
  # visible on two legs out of fourteen.
  #
  # MSVC still gets a louder sentence — but in the TEXT, not in whether the
  # annotation exists at all.
  #
  # The old message named exactly one cause (the VS image bumped) and prescribed
  # a remediation that is a NO-OP for the common one: on a feature branch that
  # touched the profiles, "dispatch on main to republish" republishes MAIN's key,
  # which this branch can never match. Seeding is gated to push / dispatch, so a
  # feature-branch key is UNSEEDABLE BY DESIGN until merge — and harmless: it
  # self-heals on the first push:main afterwards. Prescribing an action for a
  # situation needing none is what taught readers to ignore this warning.
  msvc_note=""
  if [ -n "${CONAN_CACHE_TOOLSET:-}" ]; then
    msvc_note="%0A ⚠ MSVC payload is the OTel/protobuf/abseil chain, so this leg is the expensive one to miss."
  fi

  # %0A, not literal newlines: GitHub truncates an annotation at the first line.
  echo "::warning::conan-cache MISS ${TAG} — this leg rebuilds its Conan deps from source.%0A\
Tag = sha256(conanfile.py + conan/profiles/${PROFILE}[ + MSVC toolset])[0:16], so a MISS means one of those inputs differs from what is published on GHCR.%0A\
This run's inputs: ${CONAN_CACHE_INPUTS}%0A\
 • This branch touched conanfile.py or conan/profiles/** → EXPECTED, no action. Packages are seeded only on push:main / workflow_dispatch, so a feature-branch key cannot exist yet; it publishes automatically on the first push:main after merge.%0A\
 • Neither input changed on this branch → the runner's toolchain moved (toolset ${CONAN_CACHE_TOOLSET:-n/a}) and main's published package is stale → dispatch this workflow on main to reseed.${msvc_note}"
  emit false
fi
