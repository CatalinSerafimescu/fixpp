#!/usr/bin/env bash
# Regression pin for ci/restore-conan-cache.sh's restore-disposition logic
# (PR #245 Gate B round 1, RC#5 / P2-3). Shims `oras` and `conan` on a temp
# PATH and drives the REAL restore script through its three dispositions:
#
#   1. oras pull OK, `conan cache restore` FAILS -> hit=false, NO HIT line,
#      the "DOWNLOADED BUT NOT RESTORABLE" warning present.
#   2. oras pull FAILS                           -> hit=false, MISS annotation.
#   3. oras pull OK, `conan cache restore` OK    -> hit=true, HIT line.
#
# Case 1 is the regression case this file exists for: run this harness
# against `git show origin/main:ci/restore-conan-cache.sh` (written to a temp
# dir, alongside a copy of origin/main's ci/conan-cache-key.sh, since the
# script sources its sibling by $(dirname "$0")) and it must go RED — the
# pre-fix script prints HIT and emits hit=true over an unrestorable archive.
# Only after that RED is observed does a green run against the fixed script
# mean anything.
#
# Usage: ci/test-restore-conan-cache.sh [path-to-restore-conan-cache.sh]
#   (defaults to the in-tree ci/restore-conan-cache.sh)
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="${1:-$repo_root/ci/restore-conan-cache.sh}"
PROFILE="linux-clang-release"

fail() { echo "FAIL: $1" >&2; exit 1; }

shim_dir="$(mktemp -d)"
trap 'rm -rf "$shim_dir"' EXIT

# Fake oras: validates argv (`pull ... -o <dir>`) and, on a simulated
# success, actually WRITES the archive under whichever <dir> it was told
# (not necessarily $WORK) — that is what lets mutant C (a wrong `-o` target
# in the real script) surface: conan then finds nothing at the expected
# path. Any argv this shim does not recognise, or a write it cannot
# perform, prints SHIM-VIOLATION and exits 2 rather than silently
# succeeding for the wrong reason.
# Exit code controlled by FAKE_ORAS_EXIT (default 0 = pull succeeds).
cat > "$shim_dir/oras" <<'SHIM'
#!/usr/bin/env bash
if [ "${1:-}" != "pull" ]; then
  echo "SHIM-VIOLATION: oras $*" >&2
  exit 2
fi
shift
dir="" prev=""
for a in "$@"; do
  [ "$prev" = "-o" ] && dir="$a"
  prev="$a"
done
if [ -z "$dir" ]; then
  echo "SHIM-VIOLATION: oras $*" >&2
  exit 2
fi
if [ "${FAKE_ORAS_EXIT:-0}" != "0" ]; then
  echo "Error: unexpected status: 404 Not Found" >&2
  exit "${FAKE_ORAS_EXIT:-0}"
fi
if ! mkdir -p "$dir" || ! printf 'fake conan cache archive\n' > "$dir/conan-${FAKE_PROFILE}.tgz" 2>/dev/null; then
  echo "SHIM-VIOLATION: oras could not write $dir/conan-${FAKE_PROFILE}.tgz" >&2
  exit 2
fi
exit 0
SHIM
chmod +x "$shim_dir/oras"

# Fake conan: validates argv (`cache restore <path>`) AND requires the given
# path to exist with basename conan-$FAKE_PROFILE.tgz — this is what lets
# mutant B (a wrong archive name in the real script) and mutant C (oras
# wrote the archive somewhere else) surface as a contract violation instead
# of silently coinciding with an expected exit code.
# Exit code controlled by FAKE_CONAN_EXIT (default 0 = restore succeeds).
cat > "$shim_dir/conan" <<'SHIM'
#!/usr/bin/env bash
if [ "${1:-}" = "cache" ] && [ "${2:-}" = "restore" ]; then
  path="${3:-}"
  if [ -z "$path" ]; then
    echo "SHIM-VIOLATION: conan cache restore <missing archive argument>" >&2
    exit 2
  fi
  if [ ! -f "$path" ]; then
    echo "SHIM-VIOLATION: conan cache restore $path (archive does not exist)" >&2
    exit 2
  fi
  base="$(basename "$path")"
  if [ "$base" != "conan-${FAKE_PROFILE}.tgz" ]; then
    echo "SHIM-VIOLATION: conan cache restore $path (unexpected basename: $base, expected conan-${FAKE_PROFILE}.tgz)" >&2
    exit 2
  fi
  exit "${FAKE_CONAN_EXIT:-0}"
fi
echo "SHIM-VIOLATION: conan $*" >&2
exit 2
SHIM
chmod +x "$shim_dir/conan"

# run_case <label> <fake_oras_exit> <fake_conan_exit>
# Sets: OUT, STATUS, HIT for the caller to assert on.
run_case() {
  local label="$1" oras_exit="$2" conan_exit="$3"
  local out_file gh_output status=0

  out_file="$(mktemp)"
  gh_output="$(mktemp)"

  # GITHUB_OUTPUT MUST be exported: emit() at restore-conan-cache.sh:22 is the
  # last statement on every path and returns 1 when it is unset, which would
  # exit every case 1 for a reason unrelated to what is being tested.
  ( cd "$repo_root" && \
    PATH="$shim_dir:$PATH" \
    FAKE_ORAS_EXIT="$oras_exit" \
    FAKE_CONAN_EXIT="$conan_exit" \
    FAKE_PROFILE="$PROFILE" \
    GITHUB_OUTPUT="$gh_output" \
    bash "$SCRIPT" "$PROFILE" ) > "$out_file" 2>&1 || status=$?

  OUT="$(cat "$out_file")"
  HIT="$(sed -n 's/^hit=//p' "$gh_output")"
  STATUS="$status"
  rm -f "$out_file" "$gh_output"

  echo "── case: $label ──"
  echo "$OUT" | sed 's/^/  /'
  echo "  [exit=$STATUS hit=${HIT:-<unset>}]"

  # The shims validate argv AND the archive handoff between oras and conan;
  # a violation must always fail the case it occurred in, never coincide
  # silently with an expected exit code (e.g. mutant B's wrong archive name
  # makes the fake conan exit nonzero for the WRONG reason on case 1, which
  # already expects a nonzero conan exit).
  if echo "$OUT" | grep -q "SHIM-VIOLATION"; then
    fail "$label: the script under test violated a shim's argv/file contract — see output above"
  fi
}

# ── Case 1: pulled, but the archive is not restorable ──────────────────────
run_case "downloaded but unrestorable" 0 37
[ "$STATUS" -eq 0 ] || fail "case 1: expected exit 0, got $STATUS"
[ "$HIT" = "false" ] || fail "case 1: expected hit=false, got '${HIT:-<unset>}'"
if echo "$OUT" | grep -q "^conan-cache HIT"; then
  fail "case 1: must NOT print a HIT line over an unrestorable archive"
fi
echo "$OUT" | grep -qi "DOWNLOADED BUT NOT RESTORABLE" \
  || fail "case 1: expected the corrupt-artifact warning"

# ── Case 2: the pull itself fails (plain MISS) ──────────────────────────────
run_case "pull fails" 7 0
[ "$STATUS" -eq 0 ] || fail "case 2: expected exit 0, got $STATUS"
[ "$HIT" = "false" ] || fail "case 2: expected hit=false, got '${HIT:-<unset>}'"
echo "$OUT" | grep -q "^::warning::conan-cache MISS" \
  || fail "case 2: expected the ::warning:: MISS annotation (anchored — restore-conan-cache.sh:65's plain log line also contains the unanchored substring)"
echo "$OUT" | grep -q "^conan-cache: oras: " \
  || fail "case 2: expected the retained ORAS stderr to be echoed (prefixed 'conan-cache: oras: ')"

# ── Case 3: full HIT ─────────────────────────────────────────────────────────
run_case "full hit" 0 0
[ "$STATUS" -eq 0 ] || fail "case 3: expected exit 0, got $STATUS"
[ "$HIT" = "true" ] || fail "case 3: expected hit=true, got '${HIT:-<unset>}'"
echo "$OUT" | grep -q "^conan-cache HIT" || fail "case 3: expected a HIT line"

echo "PASS: ci/restore-conan-cache.sh restore-disposition matrix (3/3) — script: $SCRIPT"
