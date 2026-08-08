#!/usr/bin/env bash
# Single source of truth for the fixpp-ccache OCI tag (Tier 3, #240).
#
# SOURCED (not executed) by BOTH seed-ccache.sh and restore-ccache.sh, for the
# same reason ci/sccache-cache-key.sh and ci/conan-cache-key.sh exist: a key the
# two sides compute DIFFERENTLY is silently a permanent MISS, and a permanent
# MISS on a compiler cache is indistinguishable from "ccache didn't help" —
# which is the exact conclusion #240 is trying to change.
#
# No `set` here on purpose: this is sourced, and a library must not mutate the
# caller's shell options.
#
# ── WHY THE TAG IS COARSE (preset + compiler identity) AND NOT CONTENT-HASHED ─
#
# Identical reasoning to ci/sccache-cache-key.sh, and it is worth restating
# because the Conan tag next door does the opposite. The Conan tag hashes
# conanfile.py + the profile because a Conan package is opaque: a wrong HIT
# relinks against a foreign STL, which is what forced the 2026-07-19 revert
# (main 327d7665). ccache has no such failure mode. Its own entry hash covers
# the compiler (CCACHE_COMPILERCHECK=content is set job-wide in
# tier3-libcxx.yml), the full argument list and the preprocessed source, so a
# stale entry cannot be mistakenly reused — it simply does not match and the TU
# recompiles.
#
# That inverts the design goal. A content-hashed tag would MISS on every commit
# that touches the source, i.e. exactly the commits this cache exists to speed
# up. The tag therefore has to be as STABLE as possible so a PR leg can pull
# what main last published; ccache then decides, per TU, what still applies.
#
# ── WHY THE COMPILER IDENTITY IS STILL IN THE TAG ────────────────────────────
#
# Not for correctness — for cost, and for one diagnostic.
#
# COST: after an apt.llvm.org rebuild, EVERY entry in a cache built by the old
# clang misses INTERNALLY (the compiler is part of ccache's hash under
# `compilercheck=content`), so restoring it downloads ~2 GB to achieve nothing.
# Folding the compiler's self-reported identity into the tag converts that slow
# useless HIT into a fast MISS, and the next push:main / dispatch republishes
# under the new tag.
#
# DIAGNOSTIC: `llvm.sh 22` can silently fall back to an earlier clang major.
# With one rolling tag that shows up as a 0 % hit rate and nothing else — a
# symptom this repo has already paid for once. With the compiler in the tag it
# shows up as a NEW TAG in the GHCR listing, which is a fact rather than an
# inference.
#
# ── WHAT IS DELIBERATELY *NOT* IN THE TAG ────────────────────────────────────
#
# The ccache version. ccache versions its own on-disk entry format and ignores
# what it cannot read, so a version bump degrades to internal misses that heal
# on the next seed — the same shape as any other miss, and not worth a tag
# dimension whose only effect would be to discard a still-usable cache whenever
# the runner image bumps a package.

# ccache_cache_key <preset>
#
# Sets: CCACHE_CACHE_TAG, CCACHE_CACHE_COMPILER, CCACHE_CACHE_TOOLSET.
# Returns 1 if the compiler behind the preset cannot be identified.
#
# Note the asymmetry with conan-cache-key.sh, and that it is deliberate: there,
# an unidentifiable toolset is a SAFETY failure and seed ABORTS. Here it is only
# an efficiency question, so both sides degrade to "no cache this run" — never
# fatal, because a compiler cache that is down must never redden a lane whose
# build and tests pass.
ccache_cache_key() {
  local preset="$1"

  # Read the compiler OUT OF THE PRESET rather than assuming `clang++`.
  #
  # The four libc++ presets do not agree: linux-clang-libc++ uses the
  # unversioned `clang++`, the three sanitizer presets pin `clang++-22`. On the
  # runner those resolve to the same binary (the install step force-symlinks
  # the highest installed clang++-N onto /usr/bin/clang++), but "they happen to
  # agree today" is not a property to key a cache on — and off the runner they
  # do not agree at all, which is what makes this script testable locally.
  #
  # Only cacheVariables set DIRECTLY on the preset are read; an inherited
  # compiler yields an empty value and therefore a MISS. That is the fail-closed
  # direction (cost, not correctness), and it is loud: restore says so in the
  # job summary. If a future preset moves the compiler into `_base`, this
  # function needs an inheritance walk, not a default.
  if ! command -v jq >/dev/null 2>&1; then
    echo "ccache-cache: jq not found — cannot resolve the compiler for '$preset'." >&2
    return 1
  fi
  if [ ! -f CMakePresets.json ]; then
    echo "ccache-cache: CMakePresets.json not found; run this from the library root." >&2
    return 1
  fi

  # cacheVariables values are either a bare string or {"type":…,"value":…}.
  CCACHE_CACHE_COMPILER="$(
    jq -r --arg p "$preset" '
      .configurePresets[]?
      | select(.name == $p)
      | .cacheVariables.CMAKE_CXX_COMPILER
      | if type == "object" then .value else . end
      | select(type == "string" and length > 0)
    ' CMakePresets.json 2>/dev/null | head -1
  )"

  if [ -z "$CCACHE_CACHE_COMPILER" ]; then
    echo "ccache-cache: preset '$preset' declares no CMAKE_CXX_COMPILER of its own." >&2
    return 1
  fi

  # `--version` is the compiler's SELF-REPORTED identity, and that is what is
  # being keyed on — not the binary's content hash.
  #
  # Content-hashing the binary would roll the tag on every apt update that
  # relinks clang without changing the compiler at all, discarding a warm 2 GB
  # cache for nothing. The version string moves when the compiler moves: for
  # apt.llvm.org builds it carries the upstream commit
  # (`clang version 22.1.2 (…llvm-project <sha>)`), which is finer than the
  # release number and coarser than the file bytes. It is a PROXY, stated as
  # one — ccache's own `compilercheck=content` remains the correctness
  # authority, and a proxy that is occasionally too coarse costs internal
  # misses, never a wrong object.
  local vout
  if ! vout="$("$CCACHE_CACHE_COMPILER" --version 2>&1)" || [ -z "$vout" ]; then
    echo "ccache-cache: '$CCACHE_CACHE_COMPILER --version' failed or printed nothing for '$preset'." >&2
    return 1
  fi

  local major digest
  # First `NN` following the word `version`. Readability only — the digest below
  # is what discriminates. An unparseable banner yields `unknown`, which is
  # still a valid, stable tag component rather than a failure.
  major="$(printf '%s' "$vout" | sed -n 's/.*version[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p' | head -1)"
  [ -n "$major" ] || major=unknown
  digest="$(printf '%s' "$vout" | sha256sum | cut -c1-8)"

  CCACHE_CACHE_TOOLSET="clang${major}-${digest}"

  # OCI tags allow [A-Za-z0-9._-] and must NOT contain '+', so `libc++` has to
  # be sanitized — `linux-clang-libc++-asan` → `linux-clang-libcxx-asan`. Same
  # substitution and the same reason as conan-cache-key.sh:101; keep the two in
  # agreement if either ever changes.
  CCACHE_CACHE_TAG="ccache-${preset//+/x}-${CCACHE_CACHE_TOOLSET}"
}
