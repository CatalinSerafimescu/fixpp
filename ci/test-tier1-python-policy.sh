#!/usr/bin/env bash
# Regression pin for the sanitizer -> Conan-profile policy #243/#251 adds to
# `tier1.yml`'s `python-bindings` job, plus the `PY_RE` python-relevance
# filter in `gate-precheck` (gate-b/r1, PR #251 round 1, F3 items 1+3 / F4).
#
# A naive version of this pin would re-read the YAML to confirm the YAML —
# a tautology. These four assertions are NOT that:
#
#   1. Suffix rule: `sanitizer == 'none'` -> `conan_profile == linux-clang-debug`;
#      every other leg -> `conan_profile == "linux-clang-" + sanitizer`. This is
#      the one that kills a `tsan -> linux-clang-debug` mutant — profile-file
#      EXISTENCE (assertion 2) does not, because `linux-clang-debug` exists.
#   2. Profile-file existence: every `conan_profile` value names a real file
#      under `conan/profiles/` — catches a typo'd/renamed profile at PR time
#      instead of as a `--build=missing` from-source rebuild on the runner.
#   3. Step parameterisation: the `python-bindings` job's `Conan install` and
#      `Restore Conan cache from GHCR` steps both interpolate
#      `matrix.conan_profile`, and NEITHER hard-codes a `linux-clang-` profile
#      literal. This is the invariant #251's own comment argues is
#      load-bearing (a restore pinned back to `linux-clang-debug` silently
#      hands back the uninstrumented closure — no `--build=missing` failure,
#      no visible symptom).
#   4. `PY_RE` case table: the literal is extracted from the `gate-precheck`
#      step (not duplicated here, so this pin cannot pass against a stale
#      copy) and evaluated with `grep -E` against a fixed positive/negative
#      path list — genuinely behavioural, not a re-statement of the pattern.
#
# Assertion 1's mutant and assertion 4's mutant are both driven through this
# same script via a MUTATED COPY of tier1.yml, so the pin proves it can
# fail before it is trusted to pass (feedback_verification_grep_must_be_
# proven_nonzero_on_the_unfixed_tree). See run_mutant_checks() below.
#
# Hermetic: reads tracked files only. No build, no Conan, no network, no `nm`.
#
# Usage: ci/test-tier1-python-policy.sh [path-to-tier1.yml]
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKFLOW="${1:-$repo_root/.github/workflows/tier1.yml}"

fail() { echo "FAIL: $1" >&2; exit 1; }

command -v python3 >/dev/null || fail "python3 is required"
command -v jq >/dev/null || fail "jq is required"
python3 -c "import yaml" 2>/dev/null \
  || fail "PyYAML is not importable — install it (pip install pyyaml) before running this pin"

# Pulls the exact fields this pin needs out of a tier1.yml (or a mutated copy
# of one) into one JSON blob, so bash only ever reads jq output — the YAML
# parse happens exactly once per workflow file, here.
extract_json() {
  local workflow="$1"
  python3 - "$workflow" <<'PYEOF'
import json
import sys
import yaml

with open(sys.argv[1]) as f:
    doc = yaml.safe_load(f)

jobs = doc["jobs"]
pb_job = jobs["python-bindings"]
include = pb_job["strategy"]["matrix"]["include"]

steps = pb_job["steps"]
conan_install_run = None
restore_run = None
for step in steps:
    name = step.get("name", "")
    if name == "Conan install":
        conan_install_run = step.get("run", "")
    elif name == "Restore Conan cache from GHCR":
        restore_run = step.get("run", "")

gp_steps = jobs["gate-precheck"]["steps"]
decide_run = None
for step in gp_steps:
    if step.get("id") == "decide":
        decide_run = step.get("run", "")
        break

out = {
    "include": include,
    "conan_install_run": conan_install_run,
    "restore_run": restore_run,
    "decide_run": decide_run,
}
print(json.dumps(out))
PYEOF
}

# ── 1 + 2: suffix rule + profile-file existence ─────────────────────────────
assert_matrix_policy() {
  local json="$1" case_id="$2"
  local n
  n="$(echo "$json" | jq '.include | length')"
  [ "$n" -gt 0 ] || fail "$case_id: python-bindings matrix include[] is empty — nothing to check"

  local i sanitizer profile expected
  for i in $(seq 0 $((n - 1))); do
    sanitizer="$(echo "$json" | jq -r ".include[$i].sanitizer")"
    profile="$(echo "$json" | jq -r ".include[$i].conan_profile")"

    if [ "$sanitizer" = "none" ]; then
      expected="linux-clang-debug"
    else
      expected="linux-clang-$sanitizer"
    fi
    if [ "$profile" != "$expected" ]; then
      fail "$case_id: sanitizer '$sanitizer' -> conan_profile '$profile', expected '$expected' (suffix rule violated)"
    fi

    [ -f "$repo_root/conan/profiles/$profile" ] \
      || fail "$case_id: conan_profile '$profile' (sanitizer '$sanitizer') names no file under conan/profiles/"
  done
}

# ── 3: step parameterisation ────────────────────────────────────────────────
assert_step_parameterisation() {
  local json="$1" case_id="$2"
  local conan_install_run restore_run

  conan_install_run="$(echo "$json" | jq -r '.conan_install_run // ""')"
  restore_run="$(echo "$json" | jq -r '.restore_run // ""')"

  [ -n "$conan_install_run" ] || fail "$case_id: python-bindings job has no 'Conan install' step"
  [ -n "$restore_run" ] || fail "$case_id: python-bindings job has no 'Restore Conan cache from GHCR' step"

  echo "$conan_install_run" | grep -q 'matrix\.conan_profile' \
    || fail "$case_id: 'Conan install' step does not interpolate matrix.conan_profile"
  echo "$restore_run" | grep -q 'matrix\.conan_profile' \
    || fail "$case_id: 'Restore Conan cache from GHCR' step does not interpolate matrix.conan_profile"

  # Hard-coded profile literal check: any 'linux-clang-<word>' token that is
  # NOT part of the `matrix.conan_profile` interpolation itself. Strip the
  # interpolation expression first so its own literal text (the property
  # path, not a profile name) cannot trigger a false positive.
  local stripped
  stripped="$(echo "$conan_install_run" | sed -E 's/\$\{\{[^}]*\}\}//g')"
  if echo "$stripped" | grep -qE 'linux-clang-[a-z+]+'; then
    fail "$case_id: 'Conan install' step's run text hard-codes a linux-clang-* profile literal outside the matrix interpolation: $(echo "$stripped" | grep -oE 'linux-clang-[a-z+]+' | head -1)"
  fi
  stripped="$(echo "$restore_run" | sed -E 's/\$\{\{[^}]*\}\}//g')"
  if echo "$stripped" | grep -qE 'linux-clang-[a-z+]+'; then
    fail "$case_id: 'Restore Conan cache from GHCR' step's run text hard-codes a linux-clang-* profile literal outside the matrix interpolation: $(echo "$stripped" | grep -oE 'linux-clang-[a-z+]+' | head -1)"
  fi
}

# ── 4: PY_RE case table ─────────────────────────────────────────────────────
# path, expected-match (post-anchor / post-#251 behaviour). Table matches
# opus_pr251_1_triage.md's F4 19-case evaluation verbatim.
PY_RE_CASES=(
  "bindings/python/foo.py|yes"
  "include/fix/c_api.h|yes"
  ".github/workflows/tier1.yml|yes"
  "bindings/swig/fixpp.i|yes"
  "conanfile.py|yes"
  "conan/profiles/linux-clang-debug|yes"
  "conan/profiles/linux-clang-asan|yes"
  "conan/profiles/linux-clang-tsan|yes"
  "conan/profiles/linux-clang-ubsan|yes"
  "conan/profiles/windows-msvc-release|no"
  "conan/profiles/linux-clang-libc++|no"
  "conan/profiles/linux-clang-coverage|no"
  "conan/profiles/README.md|no"
  "conan/profiles/linux-clang-tsan.backup|no"
  ".github/workflows/tier2.yml|no"
  "conanfile.pyc|no"
  "docs/conanfile.py|no"
  "src/core/decimal.cpp|no"
  "tools/conan/profiles/foo|no"
)

assert_py_re_case_table() {
  local json="$1" case_id="$2"
  local decide_run py_re
  decide_run="$(echo "$json" | jq -r '.decide_run // ""')"
  [ -n "$decide_run" ] || fail "$case_id: gate-precheck's 'decide' step has no run text"

  py_re="$(echo "$decide_run" | sed -n "s/^[[:space:]]*PY_RE='\(.*\)'\$/\1/p" | head -1)"
  [ -n "$py_re" ] || fail "$case_id: could not extract a PY_RE='...' literal from gate-precheck's decide step"

  local entry path expected got
  for entry in "${PY_RE_CASES[@]}"; do
    path="${entry%%|*}"
    expected="${entry##*|}"
    if printf '%s\n' "$path" | grep -qE "$py_re"; then got=yes; else got=no; fi
    if [ "$got" != "$expected" ]; then
      fail "$case_id: PY_RE against '$path' — expected '$expected', got '$got' (PY_RE='$py_re')"
    fi
  done
}

run_full_pin() {
  local workflow="$1" case_id="$2"
  local json
  json="$(extract_json "$workflow")"
  assert_matrix_policy "$json" "$case_id"
  assert_step_parameterisation "$json" "$case_id"
  assert_py_re_case_table "$json" "$case_id"
}

# ── Real workflow: must pass all four assertions ────────────────────────────
run_full_pin "$WORKFLOW" "tier1.yml"
echo "PASS: matrix policy, step parameterisation, PY_RE case table — $WORKFLOW"

# ── Mutant witnesses: each must be shown RED before this pin is trusted ─────
# (feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree —
# an assertion never shown to fail is not evidence.) Both mutants are applied
# to a TEMP COPY; the tracked workflow is never touched.
run_mutant_checks() {
  local mut_dir
  mut_dir="$(mktemp -d)"
  trap 'rm -rf "$mut_dir"' RETURN

  # Mutant A (kills assertion 1's target): tsan -> linux-clang-debug.
  # Codex's named mutant from the F3 review. Profile-file existence alone
  # (assertion 2) does NOT catch this — linux-clang-debug exists — only the
  # suffix rule does.
  local mut_a="$mut_dir/tier1-mutant-a.yml"
  local mut_a_out="$mut_dir/mutant_a_out"
  sed 's/conan_profile: linux-clang-tsan$/conan_profile: linux-clang-debug/' "$WORKFLOW" > "$mut_a"
  grep -q 'sanitizer: tsan' "$mut_a" || fail "mutant A: sed setup did not find the tsan matrix entry — mutant not applied"
  # `fail()` inside `run_full_pin` calls `exit 1`, which would kill this WHOLE
  # script if invoked directly — run it in a subshell so its exit only fails
  # the subshell and the `if` sees a plain non-zero status.
  if ( run_full_pin "$mut_a" "mutant-a" ) >"$mut_a_out" 2>&1; then
    fail "mutant A (tsan -> linux-clang-debug) did NOT fail the pin — the suffix-rule assertion cannot distinguish it from the real policy"
  fi
  grep -q "suffix rule violated" "$mut_a_out" \
    || fail "mutant A failed the pin for the WRONG reason: $(cat "$mut_a_out")"
  echo "RED (expected): mutant A (tsan -> linux-clang-debug) — $(cat "$mut_a_out")"

  # Mutant B (kills assertion 4's target): un-anchor PY_RE back to the bare
  # `conan/profiles/` prefix this round's fix removed.
  local mut_b="$mut_dir/tier1-mutant-b.yml"
  local mut_b_out="$mut_dir/mutant_b_out"
  # Literal string replace (not a sed regex substitution) — the target text
  # itself contains `(`, `|`, `)`, `$`, which would need BRE/ERE escaping
  # gymnastics for no benefit; python does an exact literal match instead.
  python3 - "$WORKFLOW" "$mut_b" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src).read()
old = "conan/profiles/linux-clang-(debug|asan|tsan|ubsan)$"
new = "conan/profiles/"
assert text.count(old) == 1, f"expected exactly one occurrence of the anchored PY_RE alternation, found {text.count(old)}"
open(dst, "w").write(text.replace(old, new))
PYEOF
  grep -q "PY_RE='.*conan/profiles/|" "$mut_b" \
    || fail "mutant B: python literal-replace did not produce the un-anchored PY_RE literal — mutant not applied"
  if ( run_full_pin "$mut_b" "mutant-b" ) >"$mut_b_out" 2>&1; then
    fail "mutant B (un-anchored PY_RE) did NOT fail the pin — the PY_RE case-table assertion cannot distinguish it from the anchored policy"
  fi
  grep -q "PY_RE against" "$mut_b_out" \
    || fail "mutant B failed the pin for the WRONG reason: $(cat "$mut_b_out")"
  echo "RED (expected): mutant B (un-anchored PY_RE) — $(cat "$mut_b_out")"
}

run_mutant_checks

echo "PASS: ci/test-tier1-python-policy.sh — both mutants proven RED, real workflow proven GREEN"
