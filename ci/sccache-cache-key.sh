#!/usr/bin/env bash
# Single source of truth for the fixpp-sccache OCI tag.
#
# SOURCED (not executed) by BOTH seed-sccache.sh and restore-sccache.sh, for the
# same reason ci/conan-cache-key.sh exists: a key the two sides compute
# DIFFERENTLY is silently a permanent MISS, and a permanent MISS on a compiler
# cache is indistinguishable from "sccache didn't help" — the exact conclusion
# this probe is trying to measure.
#
# No `set` here on purpose: this is sourced, and a library must not mutate the
# caller's shell options.

# winpath / posixpath — sccache.exe and tar disagree about path syntax.
#
# sccache is a NATIVE Windows binary and needs `C:\...`; the msys `tar` that
# ships with Git Bash needs `/c/...`. These scripts run under Git Bash on the
# runner, so both conversions are needed and neither is optional. No-ops
# wherever cygpath is absent (i.e. every non-Windows lane), which keeps the
# scripts readable rather than branching on the platform.
winpath()   { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi; }
posixpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -u "$1"; else printf '%s' "$1"; fi; }

# sccache_cache_key <preset>
#
# Sets: SCCACHE_CACHE_TAG, SCCACHE_CACHE_TOOLSET.
# Returns 1 if the MSVC toolset cannot be identified.
#
# ── WHY THE TAG IS COARSE (preset + toolset) AND NOT CONTENT-HASHED ──────────
#
# The Conan tag hashes conanfile.py + the profile because a Conan package is
# opaque: a wrong HIT relinks against a foreign STL, which is what forced the
# 2026-07-19 revert (main 327d7665). sccache has no such failure mode. Its own
# entry hash covers the compiler binary, the full argument list and the
# PREPROCESSED source, so a stale entry cannot be mistakenly reused — it simply
# does not match, and the TU recompiles. Correctness is sccache's job here, not
# the tag's.
#
# That inverts the design goal. A content-hashed tag would MISS on every commit
# that touches the source, i.e. exactly the commits this cache exists to speed
# up. The tag therefore has to be as STABLE as possible so a PR leg can pull the
# cache main last published; sccache then decides, per TU, what still applies.
#
# ── WHY THE TOOLSET IS STILL IN THE TAG ──────────────────────────────────────
#
# Not for correctness — for cost. After a GitHub image bump, EVERY entry in a
# cache built by the old toolset misses internally (the compiler binary is part
# of sccache's hash), so restoring it downloads multiple GB to achieve nothing.
# Folding VCToolsVersion into the tag converts that slow useless HIT into a fast
# MISS, and the next push:main / dispatch republishes under the new tag.
#
# Note the asymmetry with Conan, and that it is deliberate: there, an
# unidentifiable toolset is a SAFETY failure and seed aborts. Here it is only an
# efficiency question, so both sides degrade to "no cache this run" — never
# fatal, because a compiler cache that is down must never redden a lane whose
# build and tests pass.
sccache_cache_key() {
  local preset="$1"

  # VCToolsVersion is the MSVC toolset (ships the STL, drives the ABI);
  # VSCMD_VER is the coarser VS *instance* version and is the fallback. Both are
  # exported by vcvars, so this must run AFTER egor-tensin/vs-shell — the same
  # ordering constraint ci/conan-cache-key.sh documents.
  SCCACHE_CACHE_TOOLSET="${VCToolsVersion:-${VSCMD_VER:-}}"

  if [ -z "$SCCACHE_CACHE_TOOLSET" ]; then
    echo "sccache-cache: neither VCToolsVersion nor VSCMD_VER is set for '$preset'." >&2
    echo "sccache-cache: run this AFTER egor-tensin/vs-shell." >&2
    return 1
  fi

  # OCI tags allow [A-Za-z0-9._-] and must not lead with '.' or '-', so a
  # dotted toolset (14.44.35207) needs no sanitizing. Kept as literal text
  # rather than hashed so the tag list in GHCR is readable: `sccache-
  # windows-msvc-asan-14.44.35207` says what it is without a lookup table.
  SCCACHE_CACHE_TAG="sccache-${preset}-${SCCACHE_CACHE_TOOLSET}"
}
