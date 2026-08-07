#!/usr/bin/env bash
# Regression pin for ci/restore-conan-cache.sh's restore-disposition logic
# (PR #245 Gate B round 1, RC#5 / P2-3). Shims `oras` and `conan` on a temp
# PATH and drives the REAL restore script through its restore dispositions for
# two distinct Linux profiles plus the MSVC toolset-unidentified early return:
#
#   1. oras pull OK, `conan cache restore` FAILS -> hit=false, NO HIT line,
#      the "DOWNLOADED BUT NOT RESTORABLE" warning present.
#   2. oras pull FAILS                           -> hit=false, MISS annotation.
#   3. oras pull OK, `conan cache restore` OK    -> hit=true, HIT line.
#   4. `windows-msvc-release` with no toolset env -> hit=false, no oras/conan.
#
# The pin also enforces the restore script's registry/argv contract: exact OCI
# reference (`$IMAGE:$TAG`) into `oras pull`, and exactly `conan cache restore
# <archive>` with no extra arguments. Broken reference / bogus-extra-arg
# mutants must die as SHIM-VIOLATION, not preserve the same dispositions.
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
IMAGE="ghcr.io/catalinserafimescu/fixpp-conan-cache"

fail() { echo "FAIL: $1" >&2; exit 1; }

shim_dir="$(mktemp -d)"
trap 'rm -rf "$shim_dir"' EXIT

compute_expected_ref() {
  local profile="$1" toolset_mode="${2:-native}"
  if [ "$toolset_mode" = "unset" ]; then
    printf '%s' "__unused__"
    return 0
  fi
  (
    cd "$repo_root" || exit 1
    # shellcheck source=ci/conan-cache-key.sh
    . "$repo_root/ci/conan-cache-key.sh"
    case "$toolset_mode" in
      unset)
        unset VCToolsVersion VSCMD_VER
        ;;
      msvc)
        export VCToolsVersion="14.44.35207" VSCMD_VER="17.14.35"
        ;;
      native)
        ;;
      *)
        exit 99
        ;;
    esac
    conan_cache_key "$profile"
    [ -n "$CONAN_CACHE_TAG" ] || exit 98
    printf '%s' "$IMAGE:$CONAN_CACHE_TAG"
  )
}

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
if [ -z "${FAKE_EXPECTED_REF+x}" ] || [ -z "${FAKE_EXPECTED_REF:-}" ]; then
  echo "SHIM-VIOLATION: oras $*" >&2
  exit 2
fi
if [ "$#" -ne 4 ]; then
  echo "SHIM-VIOLATION: oras $*" >&2
  exit 2
fi
ref="${2:-}"
dir="${4:-}"
if [ "$ref" != "$FAKE_EXPECTED_REF" ] || [ "${3:-}" != "-o" ] || [ -z "$dir" ]; then
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
  if [ "$#" -ne 3 ]; then
    echo "SHIM-VIOLATION: conan $*" >&2
    exit 2
  fi
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
  if [ -n "${FAKE_CONAN_STDERR:-}" ]; then
    printf '%s\n' "$FAKE_CONAN_STDERR" >&2
  fi
  exit "${FAKE_CONAN_EXIT:-0}"
fi
echo "SHIM-VIOLATION: conan $*" >&2
exit 2
SHIM
chmod +x "$shim_dir/conan"

# run_case <label> <profile> <toolset_mode> <fake_oras_exit> <fake_conan_exit> [fake_conan_stderr]
# Sets: OUT, STATUS, HIT for the caller to assert on.
run_case() {
  local label="$1" profile="$2" toolset_mode="$3" oras_exit="$4" conan_exit="$5"
  local conan_stderr="${6:-}"
  local out_file gh_output status=0 expected_ref

  out_file="$(mktemp)"
  gh_output="$(mktemp)"
  expected_ref="$(compute_expected_ref "$profile" "$toolset_mode")" \
    || fail "$label: expected conan cache tag must not be empty"

  # GITHUB_OUTPUT MUST be exported: emit() at restore-conan-cache.sh:22 is the
  # last statement on every path and returns 1 when it is unset, which would
  # exit every case 1 for a reason unrelated to what is being tested.
  (
    cd "$repo_root" || exit 1
    unset VCToolsVersion VSCMD_VER
    case "$toolset_mode" in
      msvc)
        export VCToolsVersion="14.44.35207" VSCMD_VER="17.14.35"
        ;;
      native|unset)
        ;;
      *)
        exit 99
        ;;
    esac
    PATH="$shim_dir:$PATH" \
    FAKE_ORAS_EXIT="$oras_exit" \
    FAKE_CONAN_EXIT="$conan_exit" \
    FAKE_CONAN_STDERR="$conan_stderr" \
    FAKE_EXPECTED_REF="$expected_ref" \
    FAKE_PROFILE="$profile" \
    GITHUB_OUTPUT="$gh_output" \
    bash "$SCRIPT" "$profile"
  ) > "$out_file" 2>&1 || status=$?

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

assert_restore_failure_case() {
  local case_id="$1"
  [ "$STATUS" -eq 0 ] || fail "$case_id: expected exit 0, got $STATUS"
  [ "$HIT" = "false" ] || fail "$case_id: expected hit=false, got '${HIT:-<unset>}'"
  if echo "$OUT" | grep -q "^conan-cache HIT"; then
    fail "$case_id: must NOT print a HIT line over an unrestorable archive"
  fi
  echo "$OUT" | grep -qi "DOWNLOADED BUT NOT RESTORABLE" \
    || fail "$case_id: expected the restore-failure warning"
  echo "$OUT" | grep -q "could not be restored; inspect the Conan error above" \
    || fail "$case_id: expected neutral restore-failure wording"
  echo "$OUT" | grep -q "Possible causes include an invalid archive or a local cache/filesystem failure" \
    || fail "$case_id: expected mixed-cause guidance"
  echo "$OUT" | grep -q "allows an eligible push:main / dispatch-on-main publisher to attempt reseeding" \
    || fail "$case_id: expected the eligible-publisher wording"
  if echo "$OUT" | grep -Eq "unusable|truncated|corrupt|incompatible"; then
    fail "$case_id: must NOT declare the published archive unusable/corrupt"
  fi
  if echo "$OUT" | grep -q "republishes the tag"; then
    fail "$case_id: must NOT claim the current run republishes the tag"
  fi
}

assert_pull_failure_case() {
  local case_id="$1"
  [ "$STATUS" -eq 0 ] || fail "$case_id: expected exit 0, got $STATUS"
  [ "$HIT" = "false" ] || fail "$case_id: expected hit=false, got '${HIT:-<unset>}'"
  echo "$OUT" | grep -q "^::warning::conan-cache MISS" \
    || fail "$case_id: expected the ::warning:: MISS annotation (anchored — restore-conan-cache.sh:65's plain log line also contains the unanchored substring)"
  echo "$OUT" | grep -q "^conan-cache: oras: " \
    || fail "$case_id: expected the retained ORAS stderr to be echoed (prefixed 'conan-cache: oras: ')"
  echo "$OUT" | grep -q "falls back to normal Conan resolution" \
    || fail "$case_id: expected the context-free resolution wording"
  if echo "$OUT" | grep -q "rebuilds its Conan deps from source"; then
    fail "$case_id: must NOT claim every MISS rebuilds Conan deps from source"
  fi
}

assert_full_hit_case() {
  local case_id="$1"
  [ "$STATUS" -eq 0 ] || fail "$case_id: expected exit 0, got $STATUS"
  [ "$HIT" = "true" ] || fail "$case_id: expected hit=true, got '${HIT:-<unset>}'"
  echo "$OUT" | grep -q "^conan-cache HIT" || fail "$case_id: expected a HIT line"
}

assert_msvc_toolset_miss_case() {
  local case_id="$1"
  [ "$STATUS" -eq 0 ] || fail "$case_id: expected exit 0, got $STATUS"
  [ "$HIT" = "false" ] || fail "$case_id: expected hit=false, got '${HIT:-<unset>}'"
  echo "$OUT" | grep -q "^conan-cache MISS (windows-msvc-release, toolset unidentified)" \
    || fail "$case_id: expected the unidentified-toolset MISS"
  if echo "$OUT" | grep -q "^conan-cache: key inputs"; then
    fail "$case_id: must not print key inputs when key derivation fails"
  fi
  if echo "$OUT" | grep -q "^conan-cache HIT"; then
    fail "$case_id: must not print a HIT line"
  fi
}

run_linux_profile_matrix() {
  local profile="$1"

  run_case "$profile: downloaded but unrestorable" "$profile" native 0 37
  assert_restore_failure_case "$profile case 1"

  run_case "$profile: pull fails" "$profile" native 7 0
  assert_pull_failure_case "$profile case 2"

  run_case "$profile: full hit" "$profile" native 0 0
  assert_full_hit_case "$profile case 3"
}

# ── Linux matrices: two distinct profiles so $1 propagation is observable ───
run_linux_profile_matrix "linux-clang-release"
run_linux_profile_matrix "linux-clang-libc++"

# ── Counter-case: local Conan error text must not be blamed on the archive ───
run_case "linux-clang-release: restore fails with local ENOSPC" "linux-clang-release" native 0 41 "No space left on device"
assert_restore_failure_case "linux-clang-release no-space case"
echo "$OUT" | grep -q "No space left on device" \
  || fail "linux-clang-release no-space case: expected Conan stderr to surface"

# ── MSVC toolset missing: no oras/conan invocation, single hit=false ────────
run_case "windows-msvc-release: toolset unidentified" "windows-msvc-release" unset 0 0
assert_msvc_toolset_miss_case "windows-msvc-release"

echo "PASS: ci/restore-conan-cache.sh restore-disposition matrix (linux x2 + msvc early miss) — script: $SCRIPT"
