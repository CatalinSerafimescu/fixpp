#!/usr/bin/env bash
# Single source of truth for the fixpp-conan-cache OCI tag.
#
# SOURCED (not executed) by BOTH seed-conan-cache.sh and restore-conan-cache.sh.
# The two sides previously each carried their own copy of the key expression with
# a "recomputed identically" comment holding them together; MSVC needs a third
# variant, and a key that seed and restore compute DIFFERENTLY is silently a
# permanent MISS, so the expression lives in exactly one place now.
#
# Call from the library submodule root (needs conanfile.py + conan/profiles/).
# No `set` here on purpose: this is sourced, and a library must not mutate the
# caller's shell options (seed-conan-cache.sh relies on its own `set -e`).

# winpath <posix-path>
#
# `conan` on a Windows runner is a NATIVE Windows binary, but these scripts run
# under Git Bash, where mktemp -d yields `/tmp/tmp.XXXX` — a path the Windows
# Python behind conan cannot open. MSYS sometimes mangles such arguments into
# Windows form on its own and sometimes does not (it depends on the argument's
# shape, and `--opt=/p` forms are exactly the ambiguous case), so convert
# explicitly rather than relying on that. No-op everywhere cygpath is absent,
# i.e. on every Linux lane.
winpath() {
  if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi
}

# conan_cache_key <profile>
#
# Sets: CONAN_CACHE_KEY, CONAN_CACHE_TAG, CONAN_CACHE_TOOLSET ("" off MSVC).
# Returns 1 if an MSVC profile's toolset cannot be identified — see below.
conan_cache_key() {
  local profile="$1"
  local toolset=""

  case "$profile" in
    windows-msvc-*)
      # ── WHY MSVC KEYS ON THE TOOLSET AND THE OTHER PLATFORMS DO NOT ──────────
      #
      # The first MSVC offload attempt (2026-07-19) was merged and then REVERTED
      # (main 327d7665): a cached package restored as a HIT but LNK2038-mismatched
      # the runner's own compile -> LNK1319, 2 mismatches, in the codegen
      # bootstrap link. Conan's `compiler.version=194` captures only the MSVC
      # MAJOR, so it does not distinguish toolsets whose STL is ABI-incompatible.
      #
      # On actions/cache that stayed survivable by accident: LRU eviction kept
      # forcing fresh rebuilds. GHCR never evicts, so the same drift becomes
      # PERMANENT and keeps HITting -- the never-evict property that makes GHCR
      # good for Linux is precisely what makes it dangerous here.
      #
      # Keying on the toolset inverts that: a VS image bump changes the key ->
      # MISS -> rebuild -> auto-reseed. The lane heals itself instead of silently
      # relinking against a foreign STL. This is also why keying beats PINNING:
      # `windows-2022` pins the VS major only (images update in place), and
      # `-vcvars_ver=` can only pin a toolset that is still ON the image, so a
      # hard pin breaks silently the day that toolset rotates out.
      #
      # VCToolsVersion (e.g. 14.44.35207) is the MSVC TOOLSET -- the thing that
      # ships the STL headers and drives the ABI. VSCMD_VER (e.g. 17.14.35) is the
      # VS *instance* version, one level coarser: it is the fallback, not the
      # first choice. Both are exported into the environment by vcvars, so the
      # restore step MUST run AFTER egor-tensin/vs-shell.
      toolset="${VCToolsVersion:-${VSCMD_VER:-}}"

      # FAIL CLOSED. An empty toolset would hash to the same key on every
      # toolset -- i.e. exactly the reverted bug, reintroduced silently. Callers
      # dispose of this differently and deliberately: restore degrades to a MISS
      # (rebuild from source, always correct), seed ABORTS (never publish a
      # package whose key does not identify what built it).
      if [ -z "$toolset" ]; then
        echo "conan-cache: neither VCToolsVersion nor VSCMD_VER is set for '$profile'." >&2
        echo "conan-cache: MSVC packages are keyed on the toolset, so this cannot be" >&2
        echo "conan-cache: keyed safely. Run this AFTER egor-tensin/vs-shell, on a" >&2
        echo "conan-cache: runner -- MSVC is runner-seeded only, never seeded locally." >&2
        return 1
      fi
      ;;
  esac

  CONAN_CACHE_TOOLSET="$toolset"

  # Byte-identical to the previous expression when $toolset is empty (printf '%s'
  # of an empty string appends nothing), so every already-published Linux and
  # libc++ tag keeps hitting. Verified against the LIVE GHCR tag list at the time
  # of this change, which is a stronger check than a self-test could be: a
  # self-test would have to hardcode hashes that legitimately move whenever
  # conanfile.py changes, whereas "the tag we compute is one that exists" is the
  # actual invariant.
  #
  # tr -d '\r': makes the key line-ending-independent, so a CRLF Windows checkout
  # hashes identically to an LF Linux one.
  CONAN_CACHE_KEY="$(
    { cat conanfile.py "conan/profiles/$profile"; printf '%s' "$toolset"; } \
      | tr -d '\r' | sha256sum | cut -c1-16
  )"

  # OCI tags forbid '+' -> sanitize (libc++ -> libcxx); no-op for '+'-free
  # profiles. The toolset lives INSIDE the hash rather than in the tag text so
  # prune-conan-cache.sh's `^<profile>-[0-9a-f]{16}$` pattern still matches MSVC
  # tags; it is recorded as an OCI annotation and logged for inspection.
  CONAN_CACHE_TAG="${profile//+/x}-${CONAN_CACHE_KEY}"
}
