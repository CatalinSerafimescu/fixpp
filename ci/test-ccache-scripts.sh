#!/usr/bin/env bash
# Regression pin for the Tier 3 ccache scripts (#240).
#
# ⚠️ THIS DOES NOT CLOSE #248, but #248 has SHRUNK TWICE since this was written.
# #248 was specifically about extracting TIER 1's in-workflow ccache probes (the
# ~170 lines of `run:` script around the `python-bindings` job) into a tested
# ci/ script. #254 deleted that job, so those `run:` blocks no longer exist —
# the extraction target is gone rather than done. #254 also paid a down payment
# on #248's actual thesis by extracting the preset->sanitizer mapping into
# ci/derive-python-sanitizer.sh, driven directly by
# ci/test-tier1-python-policy.sh. What lands HERE is the same pattern applied to
# new code, and ci/ccache-stats.sh is still shaped to absorb whatever tier-1
# probes remain. Do not read a green run of this file as evidence about #248.
#
# Shims `oras`, `ccache`, `gh`, `curl` and the compiler on a temp PATH, builds a
# throwaway CMakePresets.json, and drives the REAL scripts —
# ci/{ccache-cache-key,restore-ccache,seed-ccache,ccache-stats,wheel-ccache-ident,assert-wheel-image,install-ccache}.sh — through
# every disposition each one can reach.
#
# ── WHY EVERY CASE ASSERTS TWO THINGS ────────────────────────────────────────
#
# Exit status AND the expected disposition line. Status alone is green on the
# broken tree and on the fixed one, which is exactly how PR #247's defects hid:
# GitHub Actions runs a `run:` block as `bash -e {0}` — a SCRIPT FILE — where an
# arithmetic-expansion error is NON-FATAL, so the step exits 0 having printed no
# disposition at all. This harness therefore invokes each script the same way
# Actions does (`bash <file>`, never `bash -c`), and asserts what was printed.
#
# ── PROVING THIS HARNESS IS NOT VACUOUS ──────────────────────────────────────
#
# Run it against an older copy of a script and it must go RED. Each shim also
# validates argv and prints SHIM-VIOLATION on anything it does not recognise, so
# a script that stops calling `oras` (or calls it with a broken reference) fails
# the case it occurred in rather than coinciding with an expected exit code.
#
# Usage: ci/test-ccache-scripts.sh [path-to-ci-dir]
#   (defaults to the in-tree ci/)
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CI_DIR="${1:-$repo_root/ci}"
IMAGE="ghcr.io/catalinserafimescu/fixpp-ccache"

pass=0
fail() { echo "FAIL: $1" >&2; exit 1; }
ok()   { pass=$((pass + 1)); echo "  ✓ $1"; }

sandbox="$(mktemp -d)"
shim_dir="$sandbox/bin"
mkdir -p "$shim_dir"
trap 'rm -rf "$sandbox"' EXIT

# ── The fake compiler ────────────────────────────────────────────────────────
# Its `--version` output is what the tag is keyed on, so the harness controls
# the tag by controlling this. `fixpp-fake-clang-missing` is deliberately NOT
# created, which is how the unidentifiable-compiler path is reached.
cat > "$shim_dir/fixpp-fake-clang" <<'SHIM'
#!/usr/bin/env bash
[ "${1:-}" = "--version" ] || { echo "SHIM-VIOLATION: compiler $*" >&2; exit 2; }
printf 'clang version 22.1.2 (https://github.com/llvm/llvm-project deadbeef)\n'
SHIM
chmod +x "$shim_dir/fixpp-fake-clang"

cat > "$sandbox/CMakePresets.json" <<'JSON'
{
  "version": 6,
  "configurePresets": [
    { "name": "fake-libc++",        "cacheVariables": { "CMAKE_CXX_COMPILER": "fixpp-fake-clang" } },
    { "name": "fake-libc++-asan",   "cacheVariables": { "CMAKE_CXX_COMPILER": "fixpp-fake-clang" } },
    { "name": "fake-no-compiler",   "cacheVariables": { "CMAKE_C_COMPILER": "cc" } },
    { "name": "fake-gone-compiler", "cacheVariables": { "CMAKE_CXX_COMPILER": "fixpp-fake-clang-missing" } }
  ]
}
JSON

expected_tag() {
  (
    cd "$sandbox" || exit 1
    PATH="$shim_dir:$PATH"
    # shellcheck source=ci/ccache-cache-key.sh
    . "$CI_DIR/ccache-cache-key.sh"
    ccache_cache_key "$1" >/dev/null 2>&1 || exit 1
    printf '%s' "$CCACHE_CACHE_TAG"
  )
}

# ── oras shim ────────────────────────────────────────────────────────────────
# pull: writes the archive the restore script expects, under whichever -o dir it
#       was told (so a wrong -o in the real script surfaces as a MISS, not a
#       silent pass). FAKE_ORAS_PULL_MODE: ok | fail | garbage | missing-file
# push: validates the reference and that the named tar exists in cwd.
cat > "$shim_dir/oras" <<'SHIM'
#!/usr/bin/env bash
case "${1:-}" in
  pull)
    ref="${2:-}"; [ "${3:-}" = "-o" ] || { echo "SHIM-VIOLATION: oras $*" >&2; exit 2; }
    dir="${4:-}"; [ -n "$dir" ] || { echo "SHIM-VIOLATION: oras $*" >&2; exit 2; }
    [ "$ref" = "${FAKE_EXPECTED_REF:-}" ] || { echo "SHIM-VIOLATION: oras pull ref '$ref' != '${FAKE_EXPECTED_REF:-}'" >&2; exit 2; }
    case "${FAKE_ORAS_PULL_MODE:-ok}" in
      fail)         echo "Error: unexpected status: 404 Not Found" >&2; exit 1 ;;
      missing-file) mkdir -p "$dir"; exit 0 ;;
      garbage)      mkdir -p "$dir"; printf 'not a tar at all\n' > "$dir/ccache-${FAKE_PRESET}.tar"; exit 0 ;;
      ok)
        mkdir -p "$dir/payload/aa" || exit 2
        printf 'restored-entry\n' > "$dir/payload/aa/entry" || exit 2
        tar -cf "$dir/ccache-${FAKE_PRESET}.tar" -C "$dir/payload" . || exit 2
        rm -rf "$dir/payload"
        exit 0 ;;
      *) echo "SHIM-VIOLATION: unknown FAKE_ORAS_PULL_MODE" >&2; exit 2 ;;
    esac ;;
  push)
    ref="${2:-}"
    [ "$ref" = "${FAKE_EXPECTED_REF:-}" ] || { echo "SHIM-VIOLATION: oras push ref '$ref' != '${FAKE_EXPECTED_REF:-}'" >&2; exit 2; }
    tarspec="${!#}"; tarfile="${tarspec%%:*}"
    [ -f "$tarfile" ] || { echo "SHIM-VIOLATION: oras push '$tarfile' does not exist in $PWD" >&2; exit 2; }
    [ "${FAKE_ORAS_PUSH_EXIT:-0}" = "0" ] || { echo "Error: denied" >&2; exit "${FAKE_ORAS_PUSH_EXIT}"; }
    printf '%s\n' "$tarfile" >> "${FAKE_PUSH_RECORD:-/dev/null}"
    exit 0 ;;
  *) echo "SHIM-VIOLATION: oras $*" >&2; exit 2 ;;
esac
SHIM
chmod +x "$shim_dir/oras"

# ── ccache shim ──────────────────────────────────────────────────────────────
cat > "$shim_dir/ccache" <<'SHIM'
#!/usr/bin/env bash
case "${1:-}" in
  --zero-stats)  exit "${FAKE_ZERO_EXIT:-0}" ;;
  --show-stats)  printf '%s\n' "${FAKE_SHOW_STATS_OUT:-cacheable calls: 0}"; exit "${FAKE_SHOW_STATS_EXIT:-0}" ;;
  --print-stats)
    [ "${FAKE_PRINT_STATS_EXIT:-0}" = "0" ] || exit "${FAKE_PRINT_STATS_EXIT}"
    # No fixture set => a pristine cache, all counters zero. That is what a
    # fresh runner reads, and it is the happy path for restore-ccache.sh's
    # "nothing has compiled yet" precondition — so the DEFAULT here must be the
    # passing case, or that guard would never be exercised green.
    if [ -z "${FAKE_STATS_FILE:-}" ]; then
      printf 'direct_cache_hit\t0\npreprocessed_cache_hit\t0\ncache_miss\t0\n'
      exit 0
    fi
    cat "$FAKE_STATS_FILE" ;;
  *) echo "SHIM-VIOLATION: ccache $*" >&2; exit 2 ;;
esac
SHIM
chmod +x "$shim_dir/ccache"

# ── mv shim ──────────────────────────────────────────────────────────────────
# Pass-through unless FAKE_MV_EXIT says otherwise. It exists for ONE case: the
# swap-in failing after CCACHE_DIR has already been removed. That branch was
# added defensively and no other stimulus reaches it — a mutant flipping its
# disposition from MISS to HIT survived the whole harness until this shim was
# written.
cat > "$shim_dir/mv" <<'SHIM'
#!/usr/bin/env bash
if [ "${FAKE_MV_EXIT:-0}" != "0" ]; then echo "mv: refused by shim" >&2; exit "${FAKE_MV_EXIT}"; fi
exec /bin/mv "$@"
SHIM
chmod +x "$shim_dir/mv"

# ── du shim ──────────────────────────────────────────────────────────────────
# Pass-through unless FAKE_DU_EXIT says otherwise. Two failure shapes, both
# exercised by ci/seed-ccache.sh's sizing datum (3a/F4, 5a):
#   FAKE_DU_EXIT=1        — prints nothing and exits non-zero (empty read).
#   FAKE_DU_EXIT=partial  — prints a plausible total AND exits non-zero (the
#                           GNU `du` shape on an unreadable subtree — a
#                           partial total must not be presented as valid).
cat > "$shim_dir/du" <<'SHIM'
#!/usr/bin/env bash
if [ "${FAKE_DU_EXIT:-0}" = "partial" ]; then printf '8.0K\t%s\n' "${!#}"; exit 1; fi
if [ "${FAKE_DU_EXIT:-0}" != "0" ]; then exit "${FAKE_DU_EXIT}"; fi
exec /usr/bin/du "$@"
SHIM
chmod +x "$shim_dir/du"

# ── gh shim (prune) ──────────────────────────────────────────────────────────
# One page, one untagged orphan + the live tag. DELETE always succeeds unless
# FAKE_GH_DELETE_EXIT says otherwise.
cat > "$shim_dir/gh" <<'SHIM'
#!/usr/bin/env bash
if [ "${1:-}" = "api" ] && [ "${2:-}" = "--paginate" ]; then
  if [ -n "${FAKE_VERSIONS_JSON:-}" ]; then printf '%s\n' "$FAKE_VERSIONS_JSON"; exit 0; fi
  printf '[{"id":1,"metadata":{"container":{"tags":["%s"]}}},{"id":2,"metadata":{"container":{"tags":[]}}}]\n' "${FAKE_KEEP_TAG:-}"
  exit 0
fi
if [ "${1:-}" = "api" ] && [ "${2:-}" = "--method" ] && [ "${3:-}" = "DELETE" ]; then
  case "${4:-}" in
    /*) echo "SHIM-VIOLATION: leading-slash endpoint '${4}' (MSYS rewrite trap)" >&2; exit 2 ;;
  esac
  [ "${FAKE_GH_DELETE_EXIT:-0}" = "0" ] || { echo '{"status": "403"}' >&2; exit "${FAKE_GH_DELETE_EXIT}"; }
  exit 0
fi
echo "SHIM-VIOLATION: gh $*" >&2; exit 2
SHIM
chmod +x "$shim_dir/gh"

# ── curl shim (install-ccache.sh) ───────────────────────────────────────────
# install-ccache.sh calls exactly `curl -sSLf -o <dest> <url>`. The shim copies
# a FIXTURE archive to <dest> rather than fetching anything, so the download is
# byte-controlled by the case: FAKE_CURL_GOOD_SRC for a well-formed archive
# whose content matches the driven script's pinned checksum, FAKE_CURL_BAD_SRC
# for an equally well-formed archive that does NOT — the corrupted case has to
# be a valid tar containing a working executable, or `tar xf` under
# `set -euo pipefail` kills the step before the checksum guard is ever reached,
# proving nothing about that guard.
cat > "$shim_dir/curl" <<'SHIM'
#!/usr/bin/env bash
[ "${1:-}" = "-sSLf" ] && [ "${2:-}" = "-o" ] || { echo "SHIM-VIOLATION: curl $*" >&2; exit 2; }
dest="${3:-}"; url="${4:-}"
[ -n "$dest" ] || { echo "SHIM-VIOLATION: curl $*" >&2; exit 2; }
case "$url" in
  https://github.com/ccache/ccache/releases/download/v*) ;;
  *) echo "SHIM-VIOLATION: curl unexpected url '$url'" >&2; exit 2 ;;
esac
case "${FAKE_CURL_MODE:-ok}" in
  ok)      [ -n "${FAKE_CURL_GOOD_SRC:-}" ] || { echo "SHIM-VIOLATION: FAKE_CURL_GOOD_SRC unset" >&2; exit 2; }
           cp "$FAKE_CURL_GOOD_SRC" "$dest" ;;
  corrupt) [ -n "${FAKE_CURL_BAD_SRC:-}" ] || { echo "SHIM-VIOLATION: FAKE_CURL_BAD_SRC unset" >&2; exit 2; }
           cp "$FAKE_CURL_BAD_SRC" "$dest" ;;
  *) echo "SHIM-VIOLATION: unknown FAKE_CURL_MODE" >&2; exit 2 ;;
esac
exit 0
SHIM
chmod +x "$shim_dir/curl"

# ── runner ───────────────────────────────────────────────────────────────────
# Invoked as `bash <file>`, exactly as GitHub Actions invokes a run: block —
# never `bash -c`, which disagrees with it on `set -e` for some constructs.
run() {
  local script="$1"; shift
  OUT_FILE="$sandbox/out.$$"
  GH_OUTPUT="$sandbox/ghout.$$"
  SUMMARY="$sandbox/summary.$$"
  : > "$GH_OUTPUT"; : > "$SUMMARY"
  STATUS=0
  (
    cd "$sandbox" || exit 1
    PATH="$shim_dir:$PATH" \
    GITHUB_OUTPUT="$GH_OUTPUT" \
    GITHUB_STEP_SUMMARY="$SUMMARY" \
    bash "$script" "$@"
  ) > "$OUT_FILE" 2>&1 || STATUS=$?
  OUT="$(cat "$OUT_FILE")"
  HIT="$(sed -n 's/^hit=//p' "$GH_OUTPUT")"
  STEP_OUTPUTS="$(cat "$GH_OUTPUT")"
  # $SUMMARY is still CREATED and passed as GITHUB_STEP_SUMMARY — the scripts
  # write to it and would behave differently if it were unset — but its contents
  # are deliberately not captured: every summary line the scripts emit also goes
  # to stdout via note(), so $OUT already covers them and a second variable was
  # dead weight.
  rm -f "$OUT_FILE" "$GH_OUTPUT" "$SUMMARY"
  if printf '%s\n' "$OUT" | grep -q 'SHIM-VIOLATION'; then
    printf '%s\n' "$OUT" | sed 's/^/  | /'
    fail "the script under test violated a shim contract (see above)"
  fi
}

want_status() { [ "$STATUS" -eq "$1" ] || { printf '%s\n' "$OUT" | sed 's/^/  | /'; fail "$2: expected exit $1, got $STATUS"; }; }
want_out()    { printf '%s\n' "$OUT" | grep -q -- "$1" || { printf '%s\n' "$OUT" | sed 's/^/  | /'; fail "$2: expected output matching '$1'"; }; }
want_no_out() { if printf '%s\n' "$OUT" | grep -q -- "$1"; then printf '%s\n' "$OUT" | sed 's/^/  | /'; fail "$2: output must NOT match '$1'"; fi; }
want_hit()    { [ "${HIT:-<unset>}" = "$1" ] || fail "$2: expected hit=$1, got '${HIT:-<unset>}'"; }
want_step_output() {
  printf '%s\n' "$STEP_OUTPUTS" | grep -qx -- "$1" || {
    printf '%s\n' "$STEP_OUTPUTS" | sed 's/^/  | /'
    fail "$2: expected the step output line '$1' in \$GITHUB_OUTPUT"
  }
}

TAG="$(expected_tag 'fake-libc++')" || fail "could not compute the expected tag"
[ -n "$TAG" ] || fail "expected tag is empty"
echo "expected tag: $TAG"

# ═════ ci/ccache-cache-key.sh ════════════════════════════════════════════════
echo "── ccache-cache-key.sh ──"

case "$TAG" in
  'ccache-fake-libcxx-clang22-'*) ok "tag sanitizes '+' to 'x' and folds the compiler id" ;;
  *) fail "tag '$TAG' does not have the documented grammar" ;;
esac

# ── THE PRODUCER/MATCHER BRIDGE — the assertion this file exists for ─────────
#
# `ccache_tag_regex` must match a tag `ccache_cache_key` ACTUALLY MINTED. Both
# come from the real key script; nothing about the grammar is restated here. A
# change to the minting expression that the regex does not follow fails HERE,
# rather than surfacing months later as multi-GB versions accumulating while
# every log looks clean — a non-matching tag is classified as somebody else's
# and skipped SILENTLY, so the `prune: PENDING` note never fires.
TAG_RE="$(
  cd "$sandbox" && PATH="$shim_dir:$PATH" \
  . "$CI_DIR/ccache-cache-key.sh" && ccache_tag_regex 'fake-libc++' >/dev/null 2>&1 \
    && printf '%s' "$CCACHE_TAG_RE"
)"
[ -n "$TAG_RE" ] || fail "ccache_tag_regex produced nothing for the plain preset"
printf '%s' "$TAG" | grep -qE -- "$TAG_RE" \
  || fail "the pruner's regex '$TAG_RE' does not match the tag the key script minted ('$TAG') — producer and matcher have drifted"
ok "the pruner's regex matches a tag the key script actually minted"

# ANCHORED-REGEX COLLISION, asserted rather than reasoned about: the plain
# preset's regex must NOT match a sanitizer preset's tag. Both sides derived,
# neither restated — this is the whole reason both ends are anchored.
ASAN_TAG="$(expected_tag 'fake-libc++-asan')" || fail "asan tag"
if printf '%s' "$ASAN_TAG" | grep -qE -- "$TAG_RE"; then
  fail "the plain preset's regex '$TAG_RE' matches the asan preset's tag '$ASAN_TAG' — the four legs' namespaces are NOT disjoint"
fi
ok "the plain preset's regex does not match a sanitizer preset's tag"

# ── 2a/F3 — REJECT EVERY NEAR-MISS NO PRODUCER HERE MINTS ────────────────────
#
# ccache_cache_key derives `major` as digits-or-`unknown` and `digest` as
# exactly 8 lowercase hex (`sha256sum | cut -c1-8`). The old grammar
# (`[0-9a-z]+-[0-9a-f]+`) also accepted a non-numeric non-`unknown` major and a
# digest of any length — near-misses no producer here mints, but this regex is
# the sole classifier on an irreversible DELETE.
for bad in "ccache-fake-libcxx-clang22-a" "ccache-fake-libcxx-clangwat-deadbeef" "ccache-fake-libcxx-clang22-deadbeef00"; do
  if printf '%s' "$bad" | grep -qE -- "$TAG_RE"; then
    fail "prune/tag-regex-near-miss: '$bad' matches '$TAG_RE' — the pruner's classifier is wider than the grammar the minter can produce"
  fi
done
ok "the pruner's regex rejects every near-miss tag no producer here mints"

# The minter's 'unknown major' fallback must still classify as its own.
UNKNOWN_MAJOR_TAG="ccache-fake-libcxx-clangunknown-15dc124f"
printf '%s' "$UNKNOWN_MAJOR_TAG" | grep -qE -- "$TAG_RE" \
  || fail "prune/tag-regex-unknown-major: '$UNKNOWN_MAJOR_TAG' does not match '$TAG_RE' — the tightened regex must still accept the minter's 'unknown major' fallback"
ok "the pruner's regex still accepts the minter's 'unknown major' fallback tag"

# ── CONTAINER LANES (#259) — the SAME producer/matcher bridge, second grammar ─
#
# A container lane's compiler lives inside a pinned image and cannot be probed
# on the host, so `ccache_container_cache_key` mints `ccache-<lane>-<digest8>`
# with no `clang<major>` component. That is a SECOND grammar, and the pruner
# must classify it exactly — every assertion below is derived from the real
# script, nothing about either grammar is restated here.
KEYSH="$CI_DIR/ccache-cache-key.sh"
PINNED_REF='quay.io/pypa/manylinux_2_28_x86_64@sha256:012f4a50472412f18bb2b450c1cce7158434cfae4ae878591c2748a13a30c2be'
EXPECTED_CONTAINER_TAG='ccache-wheel-manylinux228-012f4a50'

CTAG="$( . "$KEYSH" && ccache_container_cache_key 'wheel-manylinux228' "$PINNED_REF" >/dev/null 2>&1 && printf '%s' "$CCACHE_CACHE_TAG" )"
[ -n "$CTAG" ] || fail "container/mint: ccache_container_cache_key produced no tag for a well-formed pinned reference"
case "$CTAG" in
  "$EXPECTED_CONTAINER_TAG") ok "container tag is lane + the pinned image digest's first 8 hex" ;;
  *) fail "container/mint: tag '$CTAG' is not the documented grammar" ;;
esac

CTAG_RE="$( . "$KEYSH" && ccache_tag_regex 'wheel-manylinux228' >/dev/null 2>&1 && printf '%s' "$CCACHE_TAG_RE" )"
[ -n "$CTAG_RE" ] || fail "container/regex: ccache_tag_regex produced nothing for the container lane"
printf '%s' "$CTAG" | grep -qE -- "$CTAG_RE" \
  || fail "container/bridge: the pruner's regex '$CTAG_RE' does not match the tag the container minter produced ('$CTAG') — producer and matcher have drifted"
ok "the container regex matches a tag the container minter actually produced"

# ⚠️ THE TWO NEGATIVES ARE THE POINT. Without them this file would only have
# shown each regex is loose enough, never that it is TIGHT enough — and this
# regex is the sole classifier on an irreversible DELETE. Cross-matching would
# mean the wheel lane's pruner could reclaim a host lane's live cache.
if printf '%s' "$TAG" | grep -qE -- "$CTAG_RE"; then
  fail "container/disjoint: the container regex '$CTAG_RE' matches a HOST lane's tag '$TAG' — the namespaces are not disjoint and a prune could delete a live host cache"
fi
if printf '%s' "$CTAG" | grep -qE -- "$TAG_RE"; then
  fail "container/disjoint: a host lane's regex '$TAG_RE' matches the CONTAINER tag '$CTAG' — the namespaces are not disjoint"
fi
ok "the container and host tag namespaces are mutually disjoint (both directions asserted)"

for bad in \
  "ccache-wheel-manylinux228-012f4a5" \
  "ccache-wheel-manylinux228-012f4a500" \
  "ccache-wheel-manylinux228-012F4A50" \
  "ccache-wheel-manylinux228-clang22-012f4a50"; do
  if printf '%s' "$bad" | grep -qE -- "$CTAG_RE"; then
    fail "container/near-miss: '$bad' matches '$CTAG_RE' — the classifier is wider than the grammar the container minter can produce"
  fi
done
ok "the container regex rejects every near-miss no producer here mints"

# ── FAIL-CLOSED ON A NON-PINNED REFERENCE ────────────────────────────────────
#
# A floating tag would give a STABLE cache key for a MOVING toolchain: internal
# misses forever, reported as a healthy HIT. Each of these must be REFUSED, and
# refused by returning non-zero rather than by minting something odd.
# ⚠️ THE BARE-DIGEST CASE IS THE ONE THAT DISCRIMINATES, and it was added only
# after a mutation SURVIVED. Deleting the `*@sha256:*` guard leaves the hex and
# length checks, which already reject every other entry here — so without a
# 64-hex input carrying no image reference, this loop proves the guard is
# present but not that it does anything. A caller passing a raw digest instead
# of a reference is exactly the mistake it exists to catch.
for badref in \
  'quay.io/pypa/manylinux_2_28_x86_64' \
  'quay.io/pypa/manylinux_2_28_x86_64:latest' \
  'quay.io/pypa/manylinux_2_28_x86_64@sha256:012f4a50' \
  '012f4a50472412f18bb2b450c1cce7158434cfae4ae878591c2748a13a30c2be' \
  'quay.io/pypa/manylinux_2_28_x86_64@sha256:ZZZf4a50472412f18bb2b450c1cce7158434cfae4ae878591c2748a13a30c2be'; do
  if ( . "$KEYSH" && ccache_container_cache_key 'wheel-manylinux228' "$badref" ) >/dev/null 2>&1; then
    fail "container/pin: '$badref' was ACCEPTED — a reference that is not digest-pinned keys a moving toolchain to a stable tag, which reads as a healthy cache forever"
  fi
done
ok "the container minter refuses every reference that is not digest-pinned"

# The dispatcher both sides use. If restore and seed could reach different
# minters, the tag would differ between publish and pull — a permanent MISS,
# indistinguishable from 'ccache did not help'.
DTAG_C="$( . "$KEYSH" && ccache_resolve_key 'wheel-manylinux228' "$PINNED_REF" >/dev/null 2>&1 && printf '%s' "$CCACHE_CACHE_TAG" )"
[ "$DTAG_C" = "$CTAG" ] \
  || fail "container/dispatch: ccache_resolve_key with an image ref produced '$DTAG_C', not the container minter's '$CTAG'"
# ⚠️ PATH is set on its OWN LINE inside the subshell, exactly as `expected_tag`
# does it. `PATH=… . file` applies the assignment only for the duration of the
# `.` builtin, so the shim would be gone by the time the compiler probe runs and
# the host minter would fail for a reason that has nothing to do with dispatch.
# The tag-regex block above survives that mistake only because
# `ccache_tag_regex` is pure string work and probes no compiler.
DTAG_H="$(
  cd "$sandbox" || exit 1
  PATH="$shim_dir:$PATH"
  . "$KEYSH"
  ccache_resolve_key 'fake-libc++' >/dev/null 2>&1 || exit 1
  printf '%s' "$CCACHE_CACHE_TAG"
)"
[ "$DTAG_H" = "$TAG" ] \
  || fail "container/dispatch: ccache_resolve_key without an image ref produced '$DTAG_H', not the host minter's '$TAG'"
ok "ccache_resolve_key dispatches to the same minter both restore and seed would use"

# ── ccache_resolve_key REJECTS AN ARGUMENT SHAPE THAT DISAGREES WITH THE
# ENUMERATION (opus_pr270_2_triage.md R2-F2) ─────────────────────────────────
#
# Before this fix ccache_resolve_key dispatched on `[ -n "${2-}" ]` alone,
# never consulting ccache_lane_is_container — so a future container lane added
# to the enumeration without every caller updated (or a ref passed for a lane
# never added there) would mint under one grammar while ccache_tag_regex
# matches against the other, a silent pruner skip. These two cases are the
# ones no current caller reaches (every host caller passes no second argument;
# the one container caller always passes a ref) but that the dispatcher must
# still refuse rather than silently mis-mint.
#
# Both assert the DISPOSITION MESSAGE, not just a non-zero exit — a bare exit
# check would pass for the wrong reason: 'linux-clang-libcxx' with a ref used
# to be silently ACCEPTED by the pre-fix dispatcher (tag-safe, not enumerated,
# so it fell into the old unconditional `ccache_container_cache_key` branch and
# minted a container-grammar tag for a host lane); 'wheel-manylinux228' with no
# ref used to fail too, but via the unrelated "preset declares no
# CMAKE_CXX_COMPILER" path, not the dispatcher noticing the shape mismatch —
# the message pins that it fails for the RIGHT reason.
out="$( ( . "$KEYSH" && ccache_resolve_key 'linux-clang-libcxx' "$PINNED_REF" ) 2>&1 )" && rc=0 || rc=$?
if [ "$rc" -eq 0 ]; then
  fail "resolve/shape: an UNKNOWN lane ('linux-clang-libcxx') given an image ref was ACCEPTED — it should be refused, since ccache_lane_is_container does not enumerate it and the tag would mint under the container grammar the pruner never matches for a host lane"
fi
case "$out" in
  *"is not an enumerated container lane"*) ;;
  *) fail "resolve/shape: unenumerated-lane-with-ref was rejected for the WRONG reason: $out" ;;
esac
ok "ccache_resolve_key refuses an unenumerated lane given an image ref, for the right reason"

out="$( ( . "$KEYSH" && ccache_resolve_key 'wheel-manylinux228' ) 2>&1 )" && rc=0 || rc=$?
if [ "$rc" -eq 0 ]; then
  fail "resolve/shape: the ENUMERATED container lane ('wheel-manylinux228') given NO image ref was ACCEPTED — it should be refused, since there is no host compiler to probe for an in-container toolchain"
fi
case "$out" in
  *"is a container lane but no digest-pinned image reference was given"*) ;;
  *) fail "resolve/shape: enumerated-container-lane-with-no-ref was rejected for the WRONG reason: $out" ;;
esac
ok "ccache_resolve_key refuses an enumerated container lane given no image ref, for the right reason"

# ── ci/wheel-ccache-ident.sh — the workflow's single source, bound to BOTH ends
#
# The wheel lane's name is written in ci/wheel-ccache-ident.sh and enumerated
# again in `ccache_lane_is_container`. If those drift, the pruner classifies the
# lane's tags as somebody else's and skips them SILENTLY — no `prune: PENDING`,
# no error, versions accumulating forever. Assert the agreement rather than
# asking a comment to hold it.
IDENT_OUT="$( cd "$CI_DIR/.." && bash ci/wheel-ccache-ident.sh 2>&1 )" \
  || fail "wheel-ccache-ident: exited non-zero against the real pyproject.toml: $IDENT_OUT"
IDENT_LANE="$(printf '%s\n' "$IDENT_OUT" | sed -n 's/^lane=//p')"
IDENT_REF="$(printf '%s\n' "$IDENT_OUT" | sed -n 's/^image_ref=//p')"
[ -n "$IDENT_LANE" ] || fail "wheel-ccache-ident printed no lane"
[ -n "$IDENT_REF" ]  || fail "wheel-ccache-ident printed no image_ref"

( . "$KEYSH" && ccache_lane_is_container "$IDENT_LANE" ) \
  || fail "wheel-ccache-ident's lane '$IDENT_LANE' is NOT enumerated by ccache_lane_is_container — the pruner would classify this lane's tags as somebody else's and skip them silently"
ok "the wheel lane name agrees with the container-lane enumeration"

# The ref in the tracked file must be one the minter accepts. If pyproject ever
# reverts to a floating alias, this fails HERE rather than in a CI job that then
# keys a cache to a toolchain it cannot identify.
( . "$KEYSH" && ccache_container_cache_key "$IDENT_LANE" "$IDENT_REF" ) >/dev/null 2>&1 \
  || fail "the pinned image reference in pyproject.toml ('$IDENT_REF') is not one ccache_container_cache_key accepts"
ok "the image reference pinned in pyproject.toml is digest-pinned and mintable"

# ── C++20 module scanning must stay OFF for the wheel build (#259) ───────────
#
# ⚠️ THIS IS A PREREQUISITE FOR THE CACHE, NOT A TUNING KNOB. Measured on the
# lane's first CI run (32047903054): with scanning ON, ccache was reached — 975
# calls — and declined **975/975** with `unsupported_compiler_option`, so
# `cacheable_calls` was 0 and the cache did nothing at all. `CMAKE_CXX_STANDARD`
# is 23, so CMake enables module scanning on Ninja, and for GCC that puts
# `-fmodules-ts -fmodule-mapper=… -fdeps-format=p1689r5` on every compile line;
# ccache does not support them. Reproduced in the pinned image: default ->
# unsupported_compiler_option 1 / cache_miss 0; scanning OFF -> 0 / 1.
#
# `ci/ccache-stats.sh`'s liveness assert DOES catch a regression here — that is
# how it was found — but only after a ~69-minute container build. This pins it
# where it fails in seconds instead.
SCAN_OFF="$(
  cd "$CI_DIR/.." && python3 -c '
import tomllib
d = tomllib.load(open("bindings/python/pyproject.toml", "rb"))
print(d["tool"]["scikit-build"]["cmake"]["define"].get("CMAKE_CXX_SCAN_FOR_MODULES", "<unset>"))
' 2>&1
)"
[ "$SCAN_OFF" = "OFF" ] \
  || fail "wheel/module-scan: [tool.scikit-build.cmake.define].CMAKE_CXX_SCAN_FOR_MODULES is '$SCAN_OFF', expected 'OFF'. With scanning ON, GCC receives -fmodules-ts/-fmodule-mapper= and ccache declines EVERY call as unsupported_compiler_option — the wheel lane's cache silently becomes a no-op (measured 975/975 uncacheable on run 32047903054). The project ships zero modules, so scanning buys nothing here."
ok "the wheel build keeps C++20 module scanning OFF (the cache is a no-op without it)"

# ── ident: malformed values must be REFUSED at the source ────────────────────
#
# The guard is not decorative. `$GITHUB_OUTPUT` carries `key=value` on ONE line,
# so a multi-line or malformed value is silently TRUNCATED rather than reported,
# and the mangled ref reaches restore and seed — a permanent MISS, which is
# indistinguishable from "ccache didn't help".
ident_with() {  # $1 = the manylinux-x86_64-image value, verbatim
  local tmp="$sandbox/pp-$RANDOM.toml"
  { echo '[tool.cibuildwheel]'; echo "manylinux-x86_64-image = $1"; } > "$tmp"
  run "$CI_DIR/wheel-ccache-ident.sh" "$tmp"
}

ident_with '"manylinux_2_28"'
want_status 1 "ident/floating-alias"
ok "ident refuses a floating alias (a moving toolchain under a stable tag)"

# Passes a naive `*@sha256:*` substring test, fails the real grammar.
ident_with '"quay.io/pypa/manylinux_2_28_x86_64@sha256:012f4a50"'
want_status 1 "ident/short-digest"
ok "ident refuses a truncated digest that a substring check would accept"

ident_with '"quay.io/pypa/manylinux_2_28_x86_64@sha256:012f4a50472412f18bb2b450c1cce7158434cfae4ae878591c2748a13a30c2be"'
want_status 0 "ident/well-formed"
want_out '@sha256:012f4a50' "ident/well-formed"
ok "ident accepts a well-formed digest-pinned reference"

# ── ci/assert-wheel-image.sh — driven through BOTH outcomes ──────────────────
#
# ⚠️ A VERIFICATION GREP PROVEN ONLY GREEN IS NOT AN ASSERTION. The failing
# case is asserted first, because that is the one whose absence would make this
# whole step decorative — a pin that is silently ignored keys the lane's ccache
# to a toolchain that is not compiling the wheel, and nothing else notices.
WI_LOG="$sandbox/cibw.log"

cat > "$WI_LOG" <<EOF
info: something before
Starting container image $IDENT_REF...
info: This container will host the build for cp310-manylinux_x86_64...
EOF
run "$CI_DIR/assert-wheel-image.sh" "$WI_LOG" "$IDENT_REF"
want_status 0 "assert-wheel-image/match"
want_out 'matched 1 line' "assert-wheel-image/match"
ok "the pinned-image assertion passes when cibuildwheel started the pinned image"

# The RED case: the log shows a DIFFERENT digest — i.e. the pin was ignored and
# cibuildwheel fell back to its own. This is the real-world shape (cibuildwheel
# `main` pins the same alias to f854c50a…, so this is not a synthetic string).
cat > "$WI_LOG" <<'EOF'
Starting container image quay.io/pypa/manylinux_2_28_x86_64@sha256:f854c50adf7b7a325bc4794316f3758d387a41d61f9e2ebca0f26c7dc8f761d4...
EOF
run "$CI_DIR/assert-wheel-image.sh" "$WI_LOG" "$IDENT_REF"
want_status 1 "assert-wheel-image/wrong-digest"
want_out 'did NOT start the pinned image' "assert-wheel-image/wrong-digest"
ok "the pinned-image assertion goes RED when a DIFFERENT digest was started"

# ── #270 Gate B r1, F4 — a MIXED log (the pin AND a foreign image) ───────────
#
# The old check counted matches of the EXPECTED reference and never inspected
# what else started — a log naming both the pin and a second, different image
# passed with `n=1`. That is not "the pinned image was used", only "it was
# used SOMEWHERE alongside something else" — this is the killed mutant/
# counter-test for the count-vs-set defect.
cat > "$WI_LOG" <<EOF
Starting container image $IDENT_REF...
Starting container image quay.io/pypa/manylinux_2_28_x86_64@sha256:f854c50adf7b7a325bc4794316f3758d387a41d61f9e2ebca0f26c7dc8f761d4...
EOF
run "$CI_DIR/assert-wheel-image.sh" "$WI_LOG" "$IDENT_REF"
want_status 1 "assert-wheel-image/mixed-log"
want_out 'did NOT start the pinned image' "assert-wheel-image/mixed-log"
want_out 'only 1 named the pin' "assert-wheel-image/mixed-log"
ok "the pinned-image assertion goes RED when the log names the pin AND a different image (count-vs-set)"

# A log that does not mention the image at all must also fail — a silent
# cibuildwheel change that stops printing the line must not read as success.
: > "$WI_LOG"
run "$CI_DIR/assert-wheel-image.sh" "$WI_LOG" "$IDENT_REF"
want_status 1 "assert-wheel-image/silent"
ok "the pinned-image assertion goes RED when the log never names an image"

# A missing log is 'unverified', not 'fine'.
run "$CI_DIR/assert-wheel-image.sh" "$sandbox/no-such.log" "$IDENT_REF"
want_status 1 "assert-wheel-image/missing-log"
want_out 'could not run' "assert-wheel-image/missing-log"
ok "a missing cibuildwheel log fails the assertion rather than passing it"

# ⚠️ NO CONFIDENT WRONG CAUSE ON AN UNRELATED BUILD FAILURE. If the build dies
# before any container starts — disk, Conan, a compile error — there is no image
# line, and blaming the PIN would stack a specific false attribution on top of
# the real failure. That is the class PR #245 exists to remove and was itself
# caught shipping four times.
: > "$WI_LOG"
run "$CI_DIR/assert-wheel-image.sh" "$WI_LOG" "$IDENT_REF" "failure"
want_status 0 "assert-wheel-image/build-failed"
want_out 'NO claim is made about the pin' "assert-wheel-image/build-failed"
want_no_out 'not taking effect' "assert-wheel-image/build-failed"
ok "a failed build yields a report, not an attribution to the pin"

# …but a build that FAILED LATE while still having started the right image must
# not be turned into a false green either — the pin evidence is present, so the
# assertion still passes on its own terms.
cat > "$WI_LOG" <<EOF
Starting container image $IDENT_REF...
EOF
run "$CI_DIR/assert-wheel-image.sh" "$WI_LOG" "$IDENT_REF" "failure"
want_status 0 "assert-wheel-image/build-failed-but-image-seen"
want_out 'matched 1 line' "assert-wheel-image/build-failed-but-image-seen"
ok "a late build failure still reports the pin evidence that IS present"

# ═════ ci/install-ccache.sh (#270 Gate B r1, F2) ═════════════════════════════
#
# Entirely untested before this — the PR body's "a killed mutant behind every
# new guard" was false for this script specifically. Three of its four guards
# are fatal by construction under `set -euo pipefail` (a failed download, a
# missing extracted binary, a non-working installed executable each abort the
# step with no help from this harness); the ONE guard that fails SILENTLY if
# deleted is `sha256sum -c -` — replace it with `true` and every green run
# stays green. That is the guard this section exists to pin.
echo "── install-ccache.sh ──"
IC_SANDBOX="$sandbox/install-ccache"
mkdir -p "$IC_SANDBOX"

# A small, REAL, executable "ccache" — a shell stub, not the actual binary; the
# script only needs `--version` to succeed. Archived under the exact directory
# name install-ccache.sh's NAME variable expects, so `tar xf` extracts to the
# path the script installs from.
IC_GOOD_ROOT="$IC_SANDBOX/good/ccache-4.13.6-linux-x86_64-musl-static"
mkdir -p "$IC_GOOD_ROOT"
cat > "$IC_GOOD_ROOT/ccache" <<'STUB'
#!/usr/bin/env bash
[ "${1:-}" = "--version" ] && { echo "ccache version 4.13.6 (shim)"; exit 0; }
echo "SHIM-VIOLATION: fake ccache binary called with $*" >&2
exit 2
STUB
chmod +x "$IC_GOOD_ROOT/ccache"
IC_GOOD_TAR="$IC_SANDBOX/good.tar"
tar -cf "$IC_GOOD_TAR" -C "$IC_SANDBOX/good" "ccache-4.13.6-linux-x86_64-musl-static"
IC_GOOD_SHA256="$(sha256sum "$IC_GOOD_TAR" | cut -d' ' -f1)"

# ⚠️ A DIFFERENT WELL-FORMED ARCHIVE, not garbage bytes. Garbage dies at
# `tar xf` regardless of the checksum guard — that would make the corrupted
# case pass "non-zero exit, nothing installed" whether or not `sha256sum -c`
# is even present, which is exactly the vacuous witness this harness's own
# header warns about (status alone proves nothing). This archive extracts and
# runs fine; only its DIGEST disagrees with what was pinned.
IC_BAD_ROOT="$IC_SANDBOX/bad/ccache-4.13.6-linux-x86_64-musl-static"
mkdir -p "$IC_BAD_ROOT"
cat > "$IC_BAD_ROOT/ccache" <<'STUB'
#!/usr/bin/env bash
[ "${1:-}" = "--version" ] && { echo "ccache version 0.0.0 (WRONG BUILD)"; exit 0; }
exit 2
STUB
chmod +x "$IC_BAD_ROOT/ccache"
IC_BAD_TAR="$IC_SANDBOX/bad.tar"
tar -cf "$IC_BAD_TAR" -C "$IC_SANDBOX/bad" "ccache-4.13.6-linux-x86_64-musl-static"

# A driven COPY of the real script with the pinned checksum replaced by the
# GOOD fixture's real digest. The production script's own SHA256 cannot be
# matched by a fixture invented here; a test-only env-var override on the
# checksum would itself be a seam on a supply-chain guard, which is worse.
IC_DRIVEN="$sandbox/install-ccache-driven.sh"
python3 - "$CI_DIR/install-ccache.sh" "$IC_DRIVEN" "$IC_GOOD_SHA256" <<'PYEOF'
import sys
src, dst, digest = sys.argv[1], sys.argv[2], sys.argv[3]
t = open(src).read()
old = "CCACHE_SHA256=156ec57c5198cc849d92834023d09910b83dc5504c6cf405d09e6ae7b208a3e5\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, f"CCACHE_SHA256={digest}\n"))
PYEOF
if cmp -s "$CI_DIR/install-ccache.sh" "$IC_DRIVEN"; then
  fail "install-ccache/driven: python literal-replace produced no change — the driven copy is not actually testing a matched checksum"
fi

IC_PREFIX_OK="$sandbox/install-prefix-ok"
mkdir -p "$IC_PREFIX_OK"
FAKE_CURL_MODE=ok FAKE_CURL_GOOD_SRC="$IC_GOOD_TAR" \
  run "$IC_DRIVEN" "$IC_PREFIX_OK"
want_status 0 "install-ccache/good"
want_out 'ccache installed at' "install-ccache/good"
[ -x "$IC_PREFIX_OK/ccache" ] || fail "install-ccache/good: ccache was not installed to the prefix"
"$IC_PREFIX_OK/ccache" --version | grep -q '4.13.6' \
  || fail "install-ccache/good: the installed binary is not the fixture that was verified"
ok "good download, verified checksum, installed and runnable"

IC_PREFIX_BAD="$sandbox/install-prefix-corrupt"
mkdir -p "$IC_PREFIX_BAD"
FAKE_CURL_MODE=corrupt FAKE_CURL_GOOD_SRC="$IC_GOOD_TAR" FAKE_CURL_BAD_SRC="$IC_BAD_TAR" \
  run "$IC_DRIVEN" "$IC_PREFIX_BAD"
want_status 1 "install-ccache/corrupted"
want_no_out 'ccache installed at' "install-ccache/corrupted"
[ ! -e "$IC_PREFIX_BAD/ccache" ] \
  || fail "install-ccache/corrupted: a binary was installed despite a checksum mismatch — this is the sha256sum -c -> true mutant, live"
ok "corrupted download (well-formed archive, wrong digest) — rejected, nothing installed (kills sha256sum -c -> true)"

# ═════ ci/restore-ccache.sh ══════════════════════════════════════════════════
echo "── restore-ccache.sh ──"
CDIR="$sandbox/ccache-dir"

restore_case() {
  local mode="$1" preset="${2:-fake-libc++}"
  rm -rf "$CDIR"
  local ref; ref="$IMAGE:$(expected_tag "$preset" || true)"
  FAKE_EXPECTED_REF="$ref" FAKE_ORAS_PULL_MODE="$mode" FAKE_PRESET="$preset" \
  CCACHE_DIR="$CDIR" \
    run "$CI_DIR/restore-ccache.sh" "$preset"
}

restore_case ok
want_status 0 "restore/hit"; want_hit true "restore/hit"
want_out '^ccache-cache HIT' "restore/hit"
want_no_out 'precondition was NOT verified' "restore/hit"
[ -f "$CDIR/aa/entry" ] || fail "restore/hit: the archive content was not swapped into CCACHE_DIR"
ok "HIT — archive extracted, swapped in, hit=true"

# The container lane must address the LITERAL pinned-image tag through the real
# restore/seed scripts, not just through ccache_resolve_key in isolation. This
# is the non-vacuous witness for the dispatcher each side calls.
rm -rf "$CDIR"
FAKE_EXPECTED_REF="$IMAGE:$EXPECTED_CONTAINER_TAG" FAKE_ORAS_PULL_MODE=ok FAKE_PRESET=wheel-manylinux228 \
CCACHE_DIR="$CDIR" \
  run "$CI_DIR/restore-ccache.sh" wheel-manylinux228 "$PINNED_REF"
want_status 0 "restore/container-tag"; want_hit true "restore/container-tag"
want_out "$EXPECTED_CONTAINER_TAG" "restore/container-tag"
ok "container restore addresses the literal pinned-image tag, not an empty or host-probed key"

# ── The run-me-before-anything-compiles precondition ────────────────────────
#
# Nonzero cacheable calls before the restore means the step was reordered below
# `Conan install`, and the HIT path's `rm -rf` would discard exactly the
# dependency-closure entries Conan just built — leaving a plausible hit rate
# over a cache that lost the lane's largest block of compile work.
PRESTATS="$sandbox/prestats.txt"
printf 'direct_cache_hit\t12\npreprocessed_cache_hit\t0\ncache_miss\t400\n' > "$PRESTATS"
rm -rf "$CDIR"
FAKE_EXPECTED_REF="$IMAGE:$TAG" FAKE_ORAS_PULL_MODE=ok FAKE_PRESET=fake-libc++ \
FAKE_STATS_FILE="$PRESTATS" CCACHE_DIR="$CDIR" \
  run "$CI_DIR/restore-ccache.sh" fake-libc++
want_status 1 "restore/precondition"
want_out '::error::412 cacheable ccache call' "restore/precondition"
want_no_out 'ccache-cache HIT' "restore/precondition"
ok "compiles already recorded before the restore — refuses, rather than discarding them"

# An UNREADABLE counter must NOT redden the lane — that would fail on the
# instrument rather than the fault. It warns and proceeds.
rm -rf "$CDIR"
FAKE_EXPECTED_REF="$IMAGE:$TAG" FAKE_ORAS_PULL_MODE=ok FAKE_PRESET=fake-libc++ \
FAKE_PRINT_STATS_EXIT=1 CCACHE_DIR="$CDIR" \
  run "$CI_DIR/restore-ccache.sh" fake-libc++
want_status 0 "restore/precondition-unreadable"; want_hit true "restore/precondition-unreadable"
want_out 'precondition was NOT verified' "restore/precondition-unreadable"
ok "unreadable counters — warns that the precondition is unverified, does not fail the lane"

# 6a — a body that reads (exit 0) but carries none of the three named counters
# must warn, not be misread as "zero calls, verified". awk's field guard
# yields an empty precalls here, routing into the same warning branch above —
# not a new fatal path.
KEYLESS="$sandbox/keyless.txt"
printf 'called_for_link\t412\n' > "$KEYLESS"
rm -rf "$CDIR"
FAKE_EXPECTED_REF="$IMAGE:$TAG" FAKE_ORAS_PULL_MODE=ok FAKE_PRESET=fake-libc++ \
FAKE_STATS_FILE="$KEYLESS" CCACHE_DIR="$CDIR" \
  run "$CI_DIR/restore-ccache.sh" fake-libc++
want_status 0 "restore/precondition-keyless"; want_hit true "restore/precondition-keyless"
want_out 'precondition was NOT verified' "restore/precondition-keyless"
ok "keyless-but-readable stats body — warns unverified rather than reading as zero calls (6a)"

restore_case fail
want_status 0 "restore/pull-fails"; want_hit false "restore/pull-fails"
want_out 'ccache-cache MISS' "restore/pull-fails"
want_no_out 'ccache-cache HIT' "restore/pull-fails"
ok "pull failure — MISS, hit=false, exit 0 (a down cache never reddens a green lane)"

restore_case missing-file
want_status 0 "restore/no-archive"; want_hit false "restore/no-archive"
want_out 'did not extract cleanly' "restore/no-archive"
ok "pulled but no archive — MISS with the attributed reason"

restore_case garbage
want_status 0 "restore/garbage"; want_hit false "restore/garbage"
want_out 'did not extract cleanly' "restore/garbage"
want_no_out 'ccache-cache HIT' "restore/garbage"
[ ! -e "$CDIR/aa" ] || fail "restore/garbage: a partial tree was swapped in"
ok "unextractable archive — MISS, and the partial tree is NOT swapped in"

rm -rf "$CDIR"
CCACHE_DIR="$CDIR" run "$CI_DIR/restore-ccache.sh" fake-gone-compiler
want_status 0 "restore/no-compiler"; want_hit false "restore/no-compiler"
want_out 'compiler unidentified' "restore/no-compiler"
ok "unidentifiable compiler — MISS, never fatal"

# The rm -rf guard. `/tmp` is one component below the root: refused.
CCACHE_DIR="/tmp" run "$CI_DIR/restore-ccache.sh" fake-libc++
want_status 1 "restore/root-guard"
want_out '::error::CCACHE_DIR' "restore/root-guard"
ok "CCACHE_DIR too close to the filesystem root — refuses to run"

# --zero-stats failure must WARN (the measurement is lost) and not redden.
restore_case_zero() {
  rm -rf "$CDIR"
  FAKE_EXPECTED_REF="$IMAGE:$TAG" FAKE_ORAS_PULL_MODE=ok FAKE_PRESET=fake-libc++ \
  FAKE_ZERO_EXIT=1 CCACHE_DIR="$CDIR" \
    run "$CI_DIR/restore-ccache.sh" fake-libc++
}
# The swap-in failing AFTER CCACHE_DIR was removed. The disposition must be
# MISS — the cache is genuinely gone at that point — and the directory must be
# recreated so the compiles below do not run against a path that vanished.
rm -rf "$CDIR"
FAKE_EXPECTED_REF="$IMAGE:$TAG" FAKE_ORAS_PULL_MODE=ok FAKE_PRESET=fake-libc++ \
FAKE_MV_EXIT=1 CCACHE_DIR="$CDIR" \
  run "$CI_DIR/restore-ccache.sh" fake-libc++
want_status 0 "restore/mv-fails"; want_hit false "restore/mv-fails"
want_out 'ccache-cache MISS' "restore/mv-fails"
want_no_out 'ccache-cache HIT' "restore/mv-fails"
[ -d "$CDIR" ] || fail "restore/mv-fails: CCACHE_DIR was removed and not recreated"
ok "swap-in failure — MISS (not HIT), and CCACHE_DIR is recreated empty"

restore_case_zero
want_status 0 "restore/zero-stats-fails"; want_hit true "restore/zero-stats-fails"
want_out '::warning::' "restore/zero-stats-fails"
want_out 'must NOT be read as this run' "restore/zero-stats-fails"
ok "--zero-stats failure — warns that the hit rate is cumulative, does not fail the lane"

# ═════ ci/seed-ccache.sh ═════════════════════════════════════════════════════
echo "── seed-ccache.sh ──"
PUSH_RECORD="$sandbox/pushes"

seed_case() {
  local preset="${1:-fake-libc++}" push_exit="${2:-0}" delete_exit="${3:-0}"
  : > "$PUSH_RECORD"
  local tag; tag="$(expected_tag "$preset" || true)"
  FAKE_EXPECTED_REF="$IMAGE:$tag" FAKE_KEEP_TAG="$tag" \
  FAKE_ORAS_PUSH_EXIT="$push_exit" FAKE_GH_DELETE_EXIT="$delete_exit" \
  FAKE_PUSH_RECORD="$PUSH_RECORD" \
  CCACHE_DIR="$CDIR" CCACHE_MAXSIZE="2G" \
    run "$CI_DIR/seed-ccache.sh" "$preset"
}

rm -rf "$CDIR"; mkdir -p "$CDIR/aa"; printf 'x\n' > "$CDIR/aa/entry"
seed_case fake-libc++ 0 0
want_status 0 "seed/ok"
want_out 'ccache-cache SEEDED' "seed/ok"
want_out 'ccache-cache archive' "seed/ok"
want_out 'cap `2G`' "seed/ok"
grep -q "ccache-fake-libc++.tar" "$PUSH_RECORD" || fail "seed/ok: nothing was pushed"
ok "SEEDED — archive published, size + cap reported"

rm -rf "$CDIR"; mkdir -p "$CDIR/aa"; printf 'x\n' > "$CDIR/aa/entry"
: > "$PUSH_RECORD"
FAKE_EXPECTED_REF="$IMAGE:$EXPECTED_CONTAINER_TAG" FAKE_KEEP_TAG="$EXPECTED_CONTAINER_TAG" \
FAKE_PUSH_RECORD="$PUSH_RECORD" CCACHE_DIR="$CDIR" CCACHE_MAXSIZE="2G" \
  run "$CI_DIR/seed-ccache.sh" wheel-manylinux228 "$PINNED_REF"
want_status 0 "seed/container-tag"
want_out "ccache-cache SEEDED \`$EXPECTED_CONTAINER_TAG\`" "seed/container-tag"
grep -q "ccache-wheel-manylinux228.tar" "$PUSH_RECORD" || fail "seed/container-tag: nothing was pushed for the container lane"
ok "container seed addresses the literal pinned-image tag, not an empty or host-probed key"

# ── 3a/F4 — THE SIZING DATUM FAILING MUST NOT ABORT THE PUBLISH ──────────────
#
# The reviewer's prescription (return nonzero on a `du` failure) was rejected:
# that sits before the `oras push` and would turn a cosmetic sizing failure
# into an unpublished cache, i.e. a cold rebuild next run. Warn, and let
# publication proceed with a `?` placeholder for the missing datum.
rm -rf "$CDIR"; mkdir -p "$CDIR/aa"; printf 'x\n' > "$CDIR/aa/entry"
FAKE_DU_EXIT=1 seed_case fake-libc++ 0 0
want_status 0 "seed/du-fails"
want_out '::warning::' "seed/du-fails"
want_out 'demand datum is MISSING' "seed/du-fails"
want_out 'ccache-cache SEEDED' "seed/du-fails"
want_out '— ? archive, ? on disk' "seed/du-fails"
ok "sizing datum unmeasurable — warns, publish still proceeds (3a/F4)"

# ── 5a — A PARTIAL `du` TOTAL MUST NOT BE PRESENTED AS A VALID MEASUREMENT ───
#
# GNU `du` can print a plausible-but-wrong total AND exit non-zero (e.g. an
# unreadable subtree). `2>/dev/null` swallows the diagnostic, so only a
# status-aware capture catches this — the empty-output case above does not.
rm -rf "$CDIR"; mkdir -p "$CDIR/aa"; printf 'x\n' > "$CDIR/aa/entry"
FAKE_DU_EXIT=partial seed_case fake-libc++ 0 0
want_status 0 "seed/du-partial"
want_out '::warning::' "seed/du-partial"
want_out 'demand datum is MISSING' "seed/du-partial"
want_out 'ccache-cache SEEDED' "seed/du-partial"
want_out '— ? archive, ? on disk' "seed/du-partial"
ok "sizing datum partial — warns, does not present a partial total as valid (5a)"

# The size line must precede the push, so the sizing datum survives a failed push.
seed_case fake-libc++ 7 0
want_status 1 "seed/push-fails"
want_out '::error::ccache-cache SEED FAILED' "seed/push-fails"
want_out 'ccache-cache archive' "seed/push-fails"
want_no_out 'ccache-cache SEEDED' "seed/push-fails"
ok "push failure — ::error:: SEED FAILED, and the size datum was still reported"

seed_case fake-libc++ 0 1
want_status 0 "seed/prune-refused"
want_out 'ccache-cache SEEDED' "seed/prune-refused"
want_out 'prune: PENDING' "seed/prune-refused"
want_out 'could not be reclaimed' "seed/prune-refused"
ok "prune refused — SEEDED still reported, backlog surfaced into the summary"

# A prune that dies BEFORE printing any disposition. `|| true` would leave no
# prune line in either the log or the job summary — a silent hole in the very
# thing being instrumented — so seed synthesizes the marker from the status.
# Stimulated by running seed out of a directory whose prune is a silent exit 3;
# nothing in production carries a test hook for this.
BROKEN_CI="$sandbox/ci-broken-prune"
mkdir -p "$BROKEN_CI"
for f in "$CI_DIR"/*.sh; do ln -sf "$f" "$BROKEN_CI/$(basename "$f")"; done
rm -f "$BROKEN_CI/prune-ccache.sh"
printf '#!/usr/bin/env bash\nexit 3\n' > "$BROKEN_CI/prune-ccache.sh"
chmod +x "$BROKEN_CI/prune-ccache.sh"
: > "$PUSH_RECORD"
FAKE_EXPECTED_REF="$IMAGE:$TAG" FAKE_KEEP_TAG="$TAG" FAKE_PUSH_RECORD="$PUSH_RECORD" \
CCACHE_DIR="$CDIR" \
  run "$BROKEN_CI/seed-ccache.sh" fake-libc++
want_status 0 "seed/prune-silent-death"
want_out 'ccache-cache SEEDED' "seed/prune-silent-death"
want_out 'exited 3 before reporting a disposition' "seed/prune-silent-death"
want_out 'could not be reclaimed' "seed/prune-silent-death"
ok "prune dies without a marker — the disposition is synthesized, never silent"

rm -rf "$CDIR"
seed_case fake-libc++ 0 0
want_status 0 "seed/no-dir"
want_out 'does not exist' "seed/no-dir"
want_no_out 'ccache-cache SEEDED' "seed/no-dir"
ok "missing CCACHE_DIR — says so, publishes nothing"

mkdir -p "$CDIR"
CCACHE_DIR="$CDIR" run "$CI_DIR/seed-ccache.sh" fake-gone-compiler
want_status 0 "seed/no-compiler"
want_out 'no identifiable compiler' "seed/no-compiler"
want_no_out 'ccache-cache SEEDED' "seed/no-compiler"
ok "unidentifiable compiler — publishes nothing, never fatal"

# ═════ ci/prune-ccache.sh ════════════════════════════════════════════════════
# Driven through the REAL wrapper, not against a regex re-typed here — a test
# that restates the pattern it is checking proves only that it can copy.
echo "── prune-ccache.sh ──"

# ⚠️ THE SUPERSEDED FIXTURE IS DERIVED FROM THE REAL TAG, NOT HARDCODED.
# id 1 is the keep-tag and is excluded from regex classification by `is_current`;
# id 2 belongs to a sibling lane. So id 3 is the ONLY version whose fate the
# regex decides — and a hardcoded old-shape string there would keep matching an
# unchanged regex forever, leaving the whole prune section green while the
# pruner matched nothing from the second republish onward. Mutating the digest
# of a genuinely minted tag keeps it in whatever grammar the key script
# currently produces.
SUPERSEDED_TAG="$(printf '%s' "$TAG" | sed 's/[0-9a-f]\{8\}$/00000000/')"
[ "$SUPERSEDED_TAG" != "$TAG" ] || fail "could not derive a superseded tag from '$TAG' — the fixture would be vacuous"
FAKE_VERSIONS_JSON="$(printf '[{"id":1,"metadata":{"container":{"tags":["%s"]}}},{"id":2,"metadata":{"container":{"tags":["%s"]}}},{"id":3,"metadata":{"container":{"tags":["%s"]}}},{"id":4,"metadata":{"container":{"tags":[]}}}]' "$TAG" "$ASAN_TAG" "$SUPERSEDED_TAG")"

FAKE_VERSIONS_JSON="$FAKE_VERSIONS_JSON" DRY_RUN=1 \
  run "$CI_DIR/prune-ccache.sh" 'fake-libc++' "$TAG"
want_status 0 "prune/anchors"
# id 3 is a superseded tag of THIS lane → dead. id 4 is untagged → dead.
want_out 'would delete version 3' "prune/anchors"
want_out 'would delete version 4' "prune/anchors"
# id 2 is the ASAN lane's LIVE cache. A start-anchored-only regex would call it
# "mine" and delete it, because `fake-libcxx` is a prefix of `fake-libcxx-asan`.
want_no_out 'would delete version 2' "prune/anchors"
want_no_out 'would delete version 1' "prune/anchors"
ok "prune keeps the sibling sanitizer lane's live cache (both-ends anchoring)"

# The keep-tag guard: pruning around a tag that is not present means the caller
# computed a different key than the one published, so every "dead" version in
# the listing is in fact the live cache.
FAKE_VERSIONS_JSON="$FAKE_VERSIONS_JSON" DRY_RUN=1 \
  run "$CI_DIR/prune-ccache.sh" 'fake-libc++' 'ccache-fake-libcxx-clang22-ffffffff'
want_status 0 "prune/keep-tag-absent"
want_out 'prune: PENDING ?' "prune/keep-tag-absent"
want_no_out 'would delete' "prune/keep-tag-absent"
ok "keep-tag absent from the listing — deletes nothing and says so"

# An unanchored regex must be REFUSED by the generic pruner, not silently
# substring-matched.
FAKE_VERSIONS_JSON="$FAKE_VERSIONS_JSON" DRY_RUN=1 \
  run "$CI_DIR/prune-compiler-cache.sh" fixpp-ccache lbl 'ccache-fake-libcxx-' "$TAG"
want_status 0 "prune/unanchored-start"
want_out "is not anchored at '\^'" "prune/unanchored-start"
want_no_out 'would delete' "prune/unanchored-start"
ok "a regex with no '^' is refused rather than substring-matched"

# The END anchor is refused too. A start-only regex still matches a LONGER
# preset's tags, which is exactly how pruning one lane deletes a sibling's live
# cache — the failure the ccache grammar makes reachable and the sccache one
# avoids only by accident of naming.
FAKE_VERSIONS_JSON="$FAKE_VERSIONS_JSON" DRY_RUN=1 \
  run "$CI_DIR/prune-compiler-cache.sh" fixpp-ccache lbl '^ccache-fake-libcxx-' "$TAG"
want_status 0 "prune/unanchored-end"
want_out "is not anchored at" "prune/unanchored-end"
want_no_out 'would delete' "prune/unanchored-end"
ok "a regex with no '\$' is refused — the sibling-lane deletion path is closed at the callee"

# ── The SCCACHE wrapper, checked here because nothing else checks it ─────────
#
# This harness exists for the ccache scripts, but `prune-sccache.sh` is the
# other caller of the same callee and has NO harness of its own. Without this,
# a Tier 2 regex that lost its end anchor would be caught only at runtime, as a
# `prune: PENDING ?` on a push:main — correct, but late and on the frozen path.
#
# ⚠️ O1 — MUST END IN A POSITIVE SENTINEL, NOT JUST `want_no_out`. A keep-tag
# that IS present in the fixture makes this wrapper's whole run print NOTHING
# (Guard 2 passes, nothing classifies as dead, DRY_RUN=1 emits no lines) — so a
# `want_no_out 'is not anchored'` over that empty output passes VACUOUSLY, and
# any mutation that aborts `ci/prune-sccache.sh` *before* the anchor check
# (e.g. an early `exit 0`) still scores this case green with the pruner never
# having run. Passing a keep-tag ABSENT from the fixture instead forces the
# run past both anchor checks into Guard 2, which bails with a printed reason
# — that positive line is what proves the anchor check was actually reached.
#
# The fixture is the ccache package's tag shape reused for an sccache-style
# keep-tag, so nothing in it matches the keep-tag by construction — Guard 2's
# "matched 0" bail is therefore the expected disposition here, not a
# classification result.
FAKE_VERSIONS_JSON='[{"id":9,"metadata":{"container":{"tags":["sccache-windows-msvc-debug-14.44.35207"]}}}]' DRY_RUN=1 \
  run "$CI_DIR/prune-sccache.sh" windows-msvc-debug sccache-windows-msvc-debug-99.99.99999
want_status 0 "prune/sccache-anchored"
want_out 'prune: PENDING ?' "prune/sccache-anchored"
want_out 'matched 0' "prune/sccache-anchored"
want_no_out 'is not anchored' "prune/sccache-anchored"
ok "the sccache wrapper's regex reaches Guard 2 — a positive sentinel proves the anchor check was not skipped (O1)"

# ═════ ci/ccache-stats.sh ════════════════════════════════════════════════════
echo "── ccache-stats.sh ──"
STATS_FILE="$sandbox/stats.txt"

write_stats() {
  # $8 (local_storage_write) and $9 (recache) default to $3 (cache_miss) — a
  # reasonable stand-in for the ordinary miss-driven case, and it keeps every
  # pre-1a call site behaving as before without having to touch each one. Any
  # case that needs `changed` to read something OTHER than `cache_miss` sets
  # $8 explicitly.
  cat > "$STATS_FILE" <<EOF
direct_cache_hit	${1:-0}
preprocessed_cache_hit	${2:-0}
cache_miss	${3:-0}
cache_size_kibibyte	${4:-0}
max_cache_size_kibibyte	${5:-512000}
cleanups_performed	${6:-0}
called_for_link	${7:-0}
local_storage_write	${8:-${3:-0}}
recache	${9:-0}
EOF
}

stats_case() {
  FAKE_STATS_FILE="$STATS_FILE" FAKE_PRINT_STATS_EXIT="${PS_EXIT:-0}" \
  FAKE_SHOW_STATS_EXIT="${SS_EXIT:-0}" CCACHE_DIR="$CDIR" \
    run "$CI_DIR/ccache-stats.sh" fake-libc++ "${1:-true}" "${2:-success}" ${3+"$3"}
}

write_stats 1400 0 61 1900000 2097152 0 0 37
stats_case true success
want_status 0 "stats/warm"
want_out 'ccache-hitrate 95% over 1461 cacheable calls' "stats/warm"
# DISTINCT values for misses (61) and writes (37) — the highest-value
# assertion in this fix: it proves `changed` reads local_storage_write, not
# cache_miss. Every OTHER case's $8 defaults to $3, so an implementation that
# emitted `changed=${miss}` would still pass them all; only a distinct pair
# catches it.
want_step_output 'misses=61' "stats/warm"
want_step_output 'changed=37' "stats/warm"
ok "warm run — hit rate reported, misses and changed are DIFFERENT counters, exit 0"

# The seed step's `&& changed != 0` guard reads this. An UNCHANGED cache must
# report zero so the seed can skip re-tarring and re-uploading ~2 GB to publish
# a byte-equivalent artifact. All hits AND zero writes — a real all-hit warm
# rerun.
write_stats 1461 0 0 1900000 2097152 0 0 0
stats_case true success
want_status 0 "stats/unchanged"
want_step_output 'misses=0' "stats/unchanged"
want_step_output 'changed=0' "stats/unchanged"
want_out 'ccache-hitrate 100%' "stats/unchanged"
ok "all hits, zero writes — changed=0 published so the seed can skip republishing"

# ── 1a/F2 — THE TWO REACHABLE "cache_miss==0 BUT WRITTEN" SHAPES ─────────────
#
# CCACHE_RECACHE: unreachable in this repo (grepped `*.yml`/`*.sh`/`*.json`/
# `*.cmake`/CMakeLists.txt — set nowhere), kept as documentation of the
# ccache-side mechanism rather than an independent kill: `ccache-stats.sh`
# never reads `recache`, so this reduces to the exact same discriminator as
# the case below (local_storage_write alone).
write_stats 1 0 0 100 512000 0 0 1 1
stats_case true success
want_status 0 "stats/changed-recache-shape"
want_step_output 'changed=1' "stats/changed-recache-shape"
ok "cache_miss=0 with a write (CCACHE_RECACHE shape, documentation only) — changed=1"

# direct-miss -> preprocessed-hit: REACHABLE and ORDINARY, reproduced against
# real ccache 4.9.1 with a same-line-count header comment edit — no env var
# needed. This is the shape that made the old `misses`-gated guard withhold a
# genuinely-changed cache.
write_stats 0 1 0 100 512000 0 0 1
stats_case true success
want_status 0 "stats/changed-direct-miss-preprocessed-hit"
want_step_output 'changed=1' "stats/changed-direct-miss-preprocessed-hit"
ok "cache_miss=0 with a preprocessed-hit write (reachable, ordinary) — changed=1"

# ── 1b/F2 — PIN THE OUTPUT NAME AGAINST THE WORKFLOW EXPRESSION THAT READS IT ─
#
# The producer (ccache-stats.sh) and the consumer (tier3-libcxx.yml) are joined
# only by a string, in different files. Derive the name FROM THE YAML — not
# hardcoded here — and assert the script's LAST run actually emitted a step
# output by that name, so a rename on EITHER side the other does not follow
# fails here instead of publishing 2 GB x 4 legs on every push, silently and
# green (the consumer's guard is fail-open). Same producer/matcher-drift
# argument `ccache_tag_regex` is co-located with its minter for, above.
WORKFLOW="$repo_root/.github/workflows/tier3-libcxx.yml"
[ -f "$WORKFLOW" ] || fail "stats/output-name: $WORKFLOW not found"
CHANGED_KEY="$(sed -n 's/.*steps\.ccache_stats\.outputs\.\([a-zA-Z0-9_]*\).*/\1/p' "$WORKFLOW" | head -1)"
[ -n "$CHANGED_KEY" ] || fail "stats/output-name: could not find a steps.ccache_stats.outputs.<name> expression in $WORKFLOW"
printf '%s\n' "$STEP_OUTPUTS" | grep -qx -- "${CHANGED_KEY}=1" \
  || fail "stats/output-name: tier3-libcxx.yml's Save-ccache guard reads 'steps.ccache_stats.outputs.${CHANGED_KEY}', but the last ccache-stats.sh run did not emit an output named '${CHANGED_KEY}' — producer and consumer have drifted"
ok "the publish guard reads the exact output name ccache-stats.sh emits"

# A missing local_storage_write key must fail closed like the other five
# counters — not silently read as zero (which would report changed=0 and
# withhold a cache that actually did change).
printf 'direct_cache_hit\t10\npreprocessed_cache_hit\t5\ncache_miss\t0\ncache_size_kibibyte\t100\nmax_cache_size_kibibyte\t512000\ncleanups_performed\t0\n' > "$STATS_FILE"
stats_case true success
want_status 1 "stats/changed-missing-key"
want_out 'does not contain exactly one' "stats/changed-missing-key"
want_out "'local_storage_write'" "stats/changed-missing-key"
ok "local_storage_write absent — fail-closed, same branch as the other five counters"

write_stats 0 0 0 0 512000 0
stats_case false success
want_status 1 "stats/zero-calls"
want_out '::error::ccache recorded ZERO cacheable calls' "stats/zero-calls"
want_out 'Causes still open' "stats/zero-calls"
# Emitted even on the FATAL path — the counters are computed and published
# before the liveness branch, because a value computed after a branch that can
# exit is how a step output silently goes missing.
want_step_output 'misses=0' "stats/zero-calls"
ok "zero cacheable calls after a SUCCESSFUL build — fatal, with the causes left open"

stats_case false failure
want_status 0 "stats/zero-calls-red-build"
want_out 'no liveness claim is made' "stats/zero-calls-red-build"
want_no_out '::error::' "stats/zero-calls-red-build"
ok "zero cacheable calls after a FAILED build — explained, no liveness claim"

# `skipped`, not `failure`. The stats step is `if: always()`, so a Configure
# failure reaches it with Build SKIPPED — a third outcome value, and the one a
# `== "failure"` test would miss. The branch is `!= "success"` and already
# correct; this pins it.
stats_case false skipped
want_status 0 "stats/zero-calls-skipped-build"
want_out 'reported `skipped`' "stats/zero-calls-skipped-build"
want_no_out '::error::' "stats/zero-calls-skipped-build"
ok "zero cacheable calls after a SKIPPED build (Configure failed) — same branch"

# A cold run is NOT red. This is the acceptance rule #240 inherits from #247:
# assert liveness, never a hit-rate floor.
write_stats 0 0 900 400000 512000 0
stats_case false success
want_status 0 "stats/cold"
want_out 'ccache-hitrate 0% over 900 cacheable calls' "stats/cold"
ok "cold run — 0% is GREEN (liveness is the gate, not a hit-rate floor)"

# "Read the PAIR" — a restore HIT with a near-zero rate is the change failing.
write_stats 3 0 900 400000 2097152 0
stats_case true success
want_status 0 "stats/hit-but-cold"
want_out '::warning::ccache RESTORED a cache' "stats/hit-but-cold"
want_out 'do not read the green tick as evidence' "stats/hit-but-cold"
ok "restore HIT + 0% rate — warned as the failing pair, still exit 0"

# The same rate on a MISS is the expected cold reading and must stay silent,
# or the warning becomes noise on every first run after a compiler bump.
stats_case false success
want_status 0 "stats/miss-and-cold"
want_no_out '::warning::ccache RESTORED a cache' "stats/miss-and-cold"
ok "restore MISS + 0% rate — silent, that is just a cold run"

write_stats 100 0 800 511000 512000 42
stats_case true success
want_status 0 "stats/thrash"
want_out '::warning::ccache performed 42 cleanup' "stats/thrash"
want_out '500 MiB cap' "stats/thrash"
ok "cleanups during the run — the cap-thrash warning #240 exists to surface"

# Leading zeros: `$((08))` is an invalid OCTAL literal, and under Actions'
# `bash -e {0}` that error is NON-FATAL — the step exits 0 having printed no
# disposition at all. The `10#` normalisation must make this a normal reading.
write_stats 08 09 08 08 512000 0
stats_case true success
want_status 0 "stats/leading-zero"
want_out 'hits=17' "stats/leading-zero"
ok "leading-zero counters parse as DECIMAL, not octal"

printf 'direct_cache_hit\t1\ncache_miss\t1\n' > "$STATS_FILE"
stats_case true success
want_status 1 "stats/missing-key"
want_out 'does not contain exactly one' "stats/missing-key"
ok "a missing counter key — attributed error, not a silently wrong number"

printf 'direct_cache_hit\t1\ndirect_cache_hit\t2\n' > "$STATS_FILE"
stats_case true success
want_status 1 "stats/duplicate-key"
want_out 'does not contain exactly one' "stats/duplicate-key"
ok "a duplicated counter key — same fail-closed branch"

write_stats 1 0 1 10 512000 0
PS_EXIT=1 stats_case true success
want_status 1 "stats/print-stats-fails"
want_out '::error::`ccache --print-stats` failed' "stats/print-stats-fails"
ok "--print-stats failure — attributed, fatal"

# `--show-stats` is diagnostic-only, but "diagnostic-only" describes what the
# OUTPUT is for, not what its FAILURE does. It must warn and let the authoritative
# --print-stats read below it produce the disposition.
write_stats 5 0 5 10 512000 0
SS_EXIT=1 stats_case true success
want_status 0 "stats/show-stats-fails"
want_out '::warning::' "stats/show-stats-fails"
want_out 'ccache-hitrate 50%' "stats/show-stats-fails"
# O2 — repointed from a `want_no_out` on a string ('is NOT in this log') that
# does not exist anywhere in ci/ccache-stats.sh, which could never fire for
# any regression. This is the wording actually shipped by the warning branch.
want_out 'may be absent or incomplete' "stats/show-stats-fails"
ok "--show-stats failure — warns without denying output the log may contain"

# ── OPT-IN HIT FLOOR (#259) ──────────────────────────────────────────────────
#
# ⚠️ THE EXEMPTION IS THE CASE THAT MATTERS, not the breach. A floor that also
# fires on a cold/MISS run would redden the SEEDING run — the first push:main
# after the lane lands, which legitimately serves 0 % because there was nothing
# to restore — making the change look broken at the exact moment it is working.
# That is the failure mode this gate is shaped to avoid, so it is asserted
# first and asserted explicitly.
write_stats 0 0 1461 10 512000 0
stats_case false success 60
want_status 0 "stats/floor-miss-exempt"
want_no_out 'HIT-FLOOR BREACHED' "stats/floor-miss-exempt"
want_no_out 'satisfied' "stats/floor-miss-exempt"
want_out 'NOT evaluated' "stats/floor-miss-exempt"
ok "hit floor is EXEMPT on a restore MISS — a cold/seeding run at 0% stays green"

write_stats 0 0 1461 10 512000 0
FAKE_STATS_FILE="$STATS_FILE" FAKE_PRINT_STATS_EXIT="${PS_EXIT:-0}" \
FAKE_SHOW_STATS_EXIT="${SS_EXIT:-0}" CCACHE_DIR="$CDIR" \
  run "$CI_DIR/ccache-stats.sh" fake-libc++ "" success 60
want_status 0 "stats/floor-no-restore-step"
want_no_out 'satisfied' "stats/floor-no-restore-step"
want_out 'restore=`n/a`' "stats/floor-no-restore-step"
want_out 'NOT evaluated' "stats/floor-no-restore-step"
ok "hit floor is also not evaluated when the restore step did not report a disposition"

# The breach itself: a cache was pulled and then matched almost nothing.
write_stats 100 0 1361 10 512000 0
stats_case true success 60
want_status 1 "stats/floor-breach"
want_out 'HIT-FLOOR BREACHED' "stats/floor-breach"
ok "hit floor is FATAL on a restore HIT below the floor"

# Above the floor, same HIT — proves the previous case failed for the RATE and
# not merely for passing a fourth argument.
write_stats 1400 0 61 10 512000 0
stats_case true success 60
want_status 0 "stats/floor-satisfied"
want_out 'hit-floor 60% satisfied' "stats/floor-satisfied"
ok "hit floor passes when the rate is above it"

# Omitted floor must behave EXACTLY as before — every existing caller passes
# three arguments, and this is the regression guard for them.
write_stats 100 0 1361 10 512000 0
stats_case true success
want_status 0 "stats/floor-absent"
want_no_out 'HIT-FLOOR BREACHED' "stats/floor-absent"
ok "no floor argument — the pre-existing three-argument behaviour is unchanged"

# A malformed floor must be an attributed error, not silently treated as 0
# (which would disable the check while looking configured).
write_stats 1400 0 61 10 512000 0
stats_case true success "sixty"
want_status 1 "stats/floor-malformed"
want_out 'is not a non-negative integer percent' "stats/floor-malformed"
ok "a malformed hit-floor is rejected rather than silently disabling the check"

# ── #270 Gate B r1, F3 — the RANGE half of the validator ────────────────────
#
# The old check validated only the CHARACTER CLASS (digits-only), not the
# RANGE: 101 and an arbitrary-length digit string both parsed as "valid". On
# the huge value the `-lt` comparison itself ERRORS ("integer expression
# expected"), and because this script deliberately runs WITHOUT `-e`,
# execution fell through past the error to the "satisfied" note — a false
# PASS emitted by the instrument whose whole purpose is to stop one.
# `want_no_out 'satisfied'` is the assertion that actually catches this; exit
# status alone is unaffected by the fall-through and would pass before AND
# after the fix for the wrong reason.
write_stats 1400 0 61 10 512000 0
stats_case true success "101"
want_status 1 "stats/floor-101"
want_out 'not a 0-100 integer percent' "stats/floor-101"
want_no_out 'satisfied' "stats/floor-101"
# 101 is small enough that `[ 95 -lt 101 ]` does not error — PRE-FIX this
# already exited 1, but via HIT-FLOOR BREACHED (a real rate vs. an impossible
# floor), not because 101 is out of range. want_status alone would be green
# before AND after; the message is what proves the RANGE check, not the
# ordinary comparison, is what fired.
want_no_out 'HIT-FLOOR BREACHED' "stats/floor-101"
ok "hit-floor 101 — rejected as out of range (0-100), not via the ordinary breach comparison"

write_stats 1400 0 61 10 512000 0
stats_case true success "1234567890123456789012345"
want_status 1 "stats/floor-huge"
want_out 'too many digits' "stats/floor-huge"
want_no_out 'satisfied' "stats/floor-huge"
ok "a 25-digit hit-floor — rejected before arithmetic can wrap it into [0,100]"

write_stats 1400 0 61 10 512000 0
stats_case true success "007"
want_status 0 "stats/floor-leading-zero"
want_out 'hit-floor 7% satisfied' "stats/floor-leading-zero"
want_no_out '007%' "stats/floor-leading-zero"
ok "hit-floor 007 — parsed as DECIMAL (7), not octal or malformed, and accepted"

echo
echo "PASS: $pass assertions over ci/{ccache-cache-key,restore-ccache,seed-ccache,ccache-stats,wheel-ccache-ident,assert-wheel-image,install-ccache}.sh — scripts: $CI_DIR"
