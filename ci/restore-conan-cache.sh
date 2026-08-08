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

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
if oras pull "$IMAGE:$TAG" -o "$WORK" >/dev/null 2>"$WORK/oras.err"; then
  if conan cache restore "$(winpath "$WORK/conan-$PROFILE.tgz")"; then
    echo "conan-cache HIT  $TAG"
    emit true
  else
    # Pulled, but the payload is not usable. Same disposition as a plain miss: a
    # cache we could not verify is a cache we do not have — mirrors
    # ci/restore-sccache.sh:91-96. `emit false` here is load-bearing: it
    # re-arms the `hit == 'false'` save step so an eligible push:main /
    # dispatch-on-main publisher can attempt reseeding instead of leaving the
    # bad artifact published indefinitely. A failed restore may leave the local
    # Conan cache partially populated; the subsequent
    # `conan install --build=missing` + seed-conan-cache.sh path can repack it
    # regardless, so this is a strict improvement over never replacing the tag
    # at all.
    echo "::warning::conan-cache ${TAG} DOWNLOADED BUT NOT RESTORABLE — the archive downloaded but could not be restored; inspect the Conan error above. Possible causes include an invalid archive or a local cache/filesystem failure. Treating as a MISS so emitting hit=false allows an eligible push:main / dispatch-on-main publisher to attempt reseeding; a HIT here would leave the bad artifact published indefinitely."
    echo "conan-cache MISS $TAG (unrestorable archive) → falling back to --build=missing"
    emit false
  fi
else
  # MISS: conanfile/profile changed (no seeded artifact), GHCR/ORAS retrieval
  # failed, or the registry is unreachable. Not fatal — the subsequent
  # `conan install --build=missing` falls back to normal Conan resolution:
  # downloading prebuilt binaries where available and building the rest from
  # source. On push:main / dispatch-on-main an eligible "Save Conan cache to
  # GHCR" step may then attempt reseeding (gated on hit=false) so the next run
  # can hit.
  echo "conan-cache MISS $TAG  → falling back to --build=missing"
  [ -s "$WORK/oras.err" ] && sed 's/^/conan-cache: oras: /' "$WORK/oras.err"

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
  # which this branch can never match. Seeding is gated to push:main / dispatch
  # on main, so a feature-branch key is UNSEEDABLE BY DESIGN until merge —
  # and harmless: it self-heals on the first push:main afterwards. Prescribing
  # an action for a situation needing none is what taught readers to ignore
  # this warning.
  msvc_note=""
  toolset_clause=""
  if [ -n "${CONAN_CACHE_TOOLSET:-}" ]; then
    msvc_note="%0A ⚠ MSVC payload is the OTel/protobuf/abseil chain, so this leg is the expensive one to miss."
    toolset_clause=" On MSVC the VS toolset is ALSO folded into the key, so a runner image bump (toolset ${CONAN_CACHE_TOOLSET}) moves the tag with both files unchanged — the most likely cause here."
  fi

  # %0A, not literal newlines: GitHub truncates an annotation at the first line.
  echo "::warning::conan-cache MISS ${TAG} — this leg falls back to normal Conan resolution (\`--build=missing\`) — downloading prebuilt binaries where available and building the rest from source.%0A\
Tag = sha256(conanfile.py + conan/profiles/${PROFILE}[ + MSVC toolset])[0:16], so a MISS means either the tag is absent (one of those inputs differs from what is published on GHCR) or the GHCR fetch failed.%0A\
This run's inputs: ${CONAN_CACHE_INPUTS}%0A\
 • This branch touched conanfile.py or conan/profiles/** → EXPECTED, no action. Packages are seeded only on push:main / workflow_dispatch on main, so a feature-branch key cannot exist yet; it publishes automatically on the first push:main after merge.%0A\
 • Neither keyed file changed on this branch → the tag was never seeded on main, or the published package was deleted.${toolset_clause} → dispatch this workflow on main to (re)seed.%0A\
 • CHECK THE \`conan-cache: oras:\` LINES ABOVE BEFORE RESEEDING: \`manifest unknown\` / \`not found\` / \`404\` means the tag really is absent, so one of the two cases above applies. DNS/TLS/auth/timeout/5xx means GHCR was unreachable — infrastructure, NOT an input change, and NO reseed is needed.${msvc_note}"
  emit false
fi
