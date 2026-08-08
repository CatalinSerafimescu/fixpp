#!/usr/bin/env bash
# Regression pin for the Tier 3 ccache scripts (#240).
#
# ⚠️ THIS DOES NOT CLOSE #248. #248 is specifically about extracting TIER 1's
# in-workflow ccache probes (the ~170 lines of `run:` script around
# python-bindings) into a tested ci/ script; those `run:` blocks are UNCHANGED
# by this PR. What lands here is the same pattern applied to new code, and
# ci/ccache-stats.sh is deliberately shaped to absorb tier1's probes when #248
# is taken. Do not read a green run of this file as evidence about #248.
#
# Shims `oras`, `ccache`, `gh` and the compiler on a temp PATH, builds a
# throwaway CMakePresets.json, and drives the REAL scripts —
# ci/{ccache-cache-key,restore-ccache,seed-ccache,ccache-stats}.sh — through
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
    run "$CI_DIR/ccache-stats.sh" fake-libc++ "${1:-true}" "${2:-success}"
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

echo
echo "PASS: $pass assertions over ci/{ccache-cache-key,restore-ccache,seed-ccache,ccache-stats}.sh — scripts: $CI_DIR"
