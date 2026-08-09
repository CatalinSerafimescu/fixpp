#!/usr/bin/env bash
# Regression pin for the sanitizer -> Conan-profile policy #243/#251 adds to
# `tier1.yml`'s `python-bindings` job, plus the `PY_RE` python-relevance
# filter in `gate-precheck` (gate-b/r1, PR #251 round 1, F3 items 1+3 / F4).
#
# A naive version of this pin would re-read the YAML to confirm the YAML —
# a tautology. These assertions are NOT that:
#
#   1. Matrix exact-set census: the `python-bindings` job's matrix `include[]`
#      is exactly {asan, none, tsan, ubsan}, each mapped to its expected
#      `conan_profile` — not merely "every entry present is correctly mapped",
#      which a subset of the matrix (a deleted sanitizer leg) also satisfies
#      (gate-b/r2 finding 1; feedback_completeness_gate_exact_set_not_subset).
#   2. Suffix rule: `sanitizer == 'none'` -> `conan_profile == linux-clang-debug`;
#      every other leg -> `conan_profile == "linux-clang-" + sanitizer`. This is
#      the one that kills a `tsan -> linux-clang-debug` mutant — profile-file
#      EXISTENCE (assertion 3) does not, because `linux-clang-debug` exists.
#   3. Profile-file existence: every `conan_profile` value names a real file
#      under `conan/profiles/` — catches a typo'd/renamed profile at PR time
#      instead of as a `--build=missing` from-source rebuild on the runner.
#   4. Step parameterisation: the `python-bindings` job's `Conan install` and
#      `Restore Conan cache from GHCR` steps run text is EXACTLY (not merely
#      "contains") the expected parameterised call. gate-b/r2 finding 2: a
#      substring/interpolation-present check passes on dead interpolation —
#      an unused `${{ matrix.conan_profile }}` reference alongside a
#      hard-coded fallback that is what actually runs — which is precisely
#      the invariant #251's own comment argues is load-bearing (a restore
#      pinned back to `linux-clang-debug` silently hands back the
#      uninstrumented closure — no `--build=missing` failure, no visible
#      symptom).
#   5. `PY_RE` case table: the literal is extracted from the `gate-precheck`
#      step (not duplicated here, so this pin cannot pass against a stale
#      copy) and evaluated with `grep -E` against a fixed positive/negative
#      path list — genuinely behavioural, not a re-statement of the pattern.
#   6. `tier1-required`'s `needs:` exact-set census (gate-b/r2 finding 4
#      carve-out): the eight job names the F2 fix's durability rests on.
#
# Assertions 1, 2, 4 and 5 each have at least one mutant driven through this
# same script via a MUTATED COPY of tier1.yml, so the pin proves it can
# fail before it is trusted to pass (feedback_verification_grep_must_be_
# proven_nonzero_on_the_unfixed_tree). See run_mutant_checks() below.
#
# Hermetic: reads tracked files only. No build, no Conan, no network, no `nm`.
# It DOES hard-require an importable PyYAML on the caller's `python3` (see
# below) — providing that is the caller's responsibility, not this script's.
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

tier1_required_needs = jobs["tier1-required"]["needs"]

out = {
    "include": include,
    "conan_install_run": conan_install_run,
    "restore_run": restore_run,
    "decide_run": decide_run,
    "tier1_required_needs": tier1_required_needs,
}
print(json.dumps(out))
PYEOF
}

# ── 1 + 2: exact-set census + suffix rule + profile-file existence ──────────
assert_matrix_policy() {
  local json="$1" case_id="$2"
  local n

  # Exact-set census (gate-b/r2 finding 1): the `length > 0` check this
  # replaces only validates whatever entries remain, so 3 of the 4 sanitizer
  # legs — including asan and ubsan outright — can be deleted from the matrix
  # and the per-entry loop below still passes (feedback_completeness_gate_
  # exact_set_not_subset). Adding a fifth sanitizer requires an intentional
  # edit to EXPECTED_MATRIX here — that is the point, not friction.
  local EXPECTED_MATRIX="asan->linux-clang-asan,none->linux-clang-debug,tsan->linux-clang-tsan,ubsan->linux-clang-ubsan"
  local got
  got="$(echo "$json" | jq -r '[.include[]|"\(.sanitizer)->\(.conan_profile)"]|sort|join(",")')"
  [ "$got" = "$EXPECTED_MATRIX" ] \
    || fail "$case_id: matrix set '$got' != expected '$EXPECTED_MATRIX'"

  n="$(echo "$json" | jq '.include | length')"

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
# gate-b/r2 finding 2: the previous version of this function greped for
# `matrix\.conan_profile` ANYWHERE in the run text, and a hard-coded-literal
# heuristic whose character class excluded `"` and `$`. Both survive dead
# interpolation like `PROFILE=linux-clang-"${SANITIZER:-debug}"` — the
# expression is present in the step (in a no-op `: "${{ matrix.conan_profile
# }}"` line) while the value actually used is a hard-coded fallback. Exact
# normalized command assertions replace both checks; see the fix queue
# (opus_pr251_2_triage.md, finding 2) for the reproduction.
#
# Strips trailing `#` comments and collapses whitespace. Neither step's run
# text contains a legitimate `#` today — if one is ever added, this
# normalisation must change with it or it will silently eat part of the
# command.
norm() { sed -E 's/#.*$//; s/[[:space:]]+/ /g; s/^ //; s/ $//' | grep -v '^$'; }

assert_step_parameterisation() {
  local json="$1" case_id="$2"
  local conan_install_run restore_run

  conan_install_run="$(echo "$json" | jq -r '.conan_install_run // ""')"
  restore_run="$(echo "$json" | jq -r '.restore_run // ""')"

  [ -n "$conan_install_run" ] || fail "$case_id: python-bindings job has no 'Conan install' step"
  [ -n "$restore_run" ] || fail "$case_id: python-bindings job has no 'Restore Conan cache from GHCR' step"

  # B1: the restore step's run text, normalized, must be EXACTLY the single
  # parameterised call — string equality, not a match. Any extra line, any
  # variable indirection, breaks it.
  local nr
  nr="$(echo "$restore_run" | norm)"
  if [ "$nr" != 'ci/restore-conan-cache.sh ${{ matrix.conan_profile }}' ]; then
    fail "$case_id: 'Restore Conan cache from GHCR' step's run text is not exactly the parameterised call: '$nr'"
  fi

  # B2: a substring check is not sufficient for the install step — a second,
  # hard-coded `-pr` appended after the parameterised one would still contain
  # the parameterised literal and pass a bare `grep -qF` (mutant E). Count
  # BOTH the total number of `-pr ` occurrences and the number that are
  # exactly the parameterised literal; both must be 1.
  local ni total good
  ni="$(echo "$conan_install_run" | norm)"
  total="$(echo "$ni" | grep -oF -- '-pr ' | grep -c . || true)"
  good="$(echo "$ni" | grep -oF -- '-pr conan/profiles/${{ matrix.conan_profile }}' | grep -c . || true)"
  if [ "$total" != 1 ] || [ "$good" != 1 ]; then
    fail "$case_id: 'Conan install' step's run text has $total '-pr ' occurrence(s) and $good parameterised-literal occurrence(s), expected exactly 1 and 1: '$ni'"
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

# ── 5: tier1-required's needs: census (gate-b/r2 finding 4 carve-out) ───────
# Structural YAML, not run:-block shell — the rest of finding 4 (the truth
# table, the release early-exit, the id:/summary-output assertions) is waived
# to #248, which is where that shell gets a home it can be tested from
# without duplicating it. This pins the one thing the whole F2 fix's
# durability rests on: these eight job names staying in tier1-required's
# needs:.
assert_tier1_required_needs() {
  local json="$1" case_id="$2"
  local EXPECTED_NEEDS="check-layers,ci-script-pins,coverage,gate-precheck,linux,python-bindings,python-wheel-build,python-wheel-test"
  local got
  got="$(echo "$json" | jq -r '.tier1_required_needs | sort | join(",")')"
  [ "$got" = "$EXPECTED_NEEDS" ] \
    || fail "$case_id: tier1-required needs set '$got' != expected '$EXPECTED_NEEDS'"
}

run_full_pin() {
  local workflow="$1" case_id="$2"
  local json
  json="$(extract_json "$workflow")"
  assert_matrix_policy "$json" "$case_id"
  assert_step_parameterisation "$json" "$case_id"
  assert_py_re_case_table "$json" "$case_id"
  assert_tier1_required_needs "$json" "$case_id"
}

# ── Real workflow: must pass all four assertions ────────────────────────────
run_full_pin "$WORKFLOW" "tier1.yml"
echo "PASS: matrix policy, step parameterisation, PY_RE case table — $WORKFLOW"

# ── Mutant witnesses: each must be shown RED before this pin is trusted ─────
# (feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree —
# an assertion never shown to fail is not evidence.) All mutants are applied
# to a TEMP COPY; the tracked workflow is never touched.
run_mutant_checks() {
  local mut_dir
  mut_dir="$(mktemp -d)"
  trap 'rm -rf "$mut_dir"' RETURN

  # Mutant A: tsan -> linux-clang-debug. Codex's named mutant from the F3
  # review. Profile-file existence alone (assertion 3) does NOT catch this —
  # linux-clang-debug exists. Since gate-b/r2 added the matrix exact-set
  # census (assertion 1) ahead of the per-entry loop, that census now catches
  # this specific remapping before the suffix rule (assertion 2) gets to run
  # — both would catch it; the census runs first. Kept as a witness for both:
  # the failure message is checked below for either assertion's wording.
  local mut_a="$mut_dir/tier1-mutant-a.yml"
  local mut_a_out="$mut_dir/mutant_a_out"
  sed 's/conan_profile: linux-clang-tsan$/conan_profile: linux-clang-debug/' "$WORKFLOW" > "$mut_a"
  # O2 (gate-b/r2): the previous guard here (`grep -q 'sanitizer: tsan'`)
  # checks a DIFFERENT line than the one `sed` targets — a `sed` that matched
  # nothing (e.g. a future trailing comment on the profile line defeating the
  # `$` anchor) would still pass it, and the mutant would then fail the pin
  # for the wrong reason (or not at all) while this guard stayed green. `cmp`
  # against the unmutated workflow is the direct no-op check. Written as
  # `if cmp -s ...; then fail ...; fi`, NOT `cmp -s ... && fail ...` — the
  # latter aborts the script on the PASSING branch under `set -e`
  # (feedback_bracket_and_fail_under_set_e_aborts_on_the_passing_branch).
  if cmp -s "$WORKFLOW" "$mut_a"; then
    fail "mutant A: sed produced no change — mutant not applied"
  fi
  # `fail()` inside `run_full_pin` calls `exit 1`, which would kill this WHOLE
  # script if invoked directly — run it in a subshell so its exit only fails
  # the subshell and the `if` sees a plain non-zero status.
  if ( run_full_pin "$mut_a" "mutant-a" ) >"$mut_a_out" 2>&1; then
    fail "mutant A (tsan -> linux-clang-debug) did NOT fail the pin — neither the matrix census nor the suffix-rule assertion can distinguish it from the real policy"
  fi
  grep -q "suffix rule violated\|matrix set" "$mut_a_out" \
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
  if cmp -s "$WORKFLOW" "$mut_b"; then
    fail "mutant B: python literal-replace produced no change — mutant not applied"
  fi
  grep -q "PY_RE='.*conan/profiles/|" "$mut_b" \
    || fail "mutant B: python literal-replace did not produce the un-anchored PY_RE literal — mutant not applied"
  if ( run_full_pin "$mut_b" "mutant-b" ) >"$mut_b_out" 2>&1; then
    fail "mutant B (un-anchored PY_RE) did NOT fail the pin — the PY_RE case-table assertion cannot distinguish it from the anchored policy"
  fi
  grep -q "PY_RE against" "$mut_b_out" \
    || fail "mutant B failed the pin for the WRONG reason: $(cat "$mut_b_out")"
  echo "RED (expected): mutant B (un-anchored PY_RE) — $(cat "$mut_b_out")"

  # Mutant C (kills the step-parameterisation exact-match, gate-b/r2 finding
  # 2): dead interpolation on BOTH steps. `: "${{ matrix.conan_profile }}"`
  # is a no-op reference to the matrix expression, and the actual profile
  # used falls back to a hard-coded `debug` because `SANITIZER` is set
  # nowhere in the workflow. A restore-only variant would leave B2 (the
  # install-step assertion) unexercised, so this mutant patches both.
  local mut_c="$mut_dir/tier1-mutant-c.yml"
  local mut_c_out="$mut_dir/mutant_c_out"
  python3 - "$WORKFLOW" "$mut_c" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old_restore = "        run: ci/restore-conan-cache.sh ${{ matrix.conan_profile }}\n"
new_restore = ('        run: |\n'
               '          : "${{ matrix.conan_profile }}"\n'
               '          PROFILE=linux-clang-"${SANITIZER:-debug}"\n'
               '          ci/restore-conan-cache.sh "$PROFILE"\n')
assert t.count(old_restore) == 1, t.count(old_restore)
t = t.replace(old_restore, new_restore)
old_inst = ("          conan install . \\\n"
            "            -pr conan/profiles/${{ matrix.conan_profile }} \\\n")
new_inst = ('          PROFILE=linux-clang-"${SANITIZER:-debug}"\n'
            "          conan install . \\\n"
            '            -pr conan/profiles/"$PROFILE" \\\n')
assert t.count(old_inst) == 1, t.count(old_inst)
t = t.replace(old_inst, new_inst)
open(dst, "w").write(t)
PYEOF
  # Guard runs AFTER both replacements — a mutant applied to only one of the
  # two steps would still trip the OTHER assertion, which would read as a
  # pass while half the witness is missing.
  if cmp -s "$WORKFLOW" "$mut_c"; then
    fail "mutant C: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$mut_c" "mutant-c" ) >"$mut_c_out" 2>&1; then
    fail "mutant C (dead interpolation, both steps) did NOT fail the pin — the step-parameterisation assertion cannot distinguish it from the real policy"
  fi
  grep -q "does not interpolate\|is not exactly the parameterised call\|occurrence(s), expected exactly" "$mut_c_out" \
    || fail "mutant C failed the pin for the WRONG reason: $(cat "$mut_c_out")"
  echo "RED (expected): mutant C (dead interpolation, both steps) — $(cat "$mut_c_out")"

  # Mutant D (kills the matrix exact-set census, gate-b/r2 finding 1): delete
  # the asan `include` entry entirely. The per-entry suffix rule and
  # profile-file existence checks (assertions 1+2) validate only whatever
  # entries remain, so this survived until the exact-set census was added.
  local mut_d="$mut_dir/tier1-mutant-d.yml"
  local mut_d_out="$mut_dir/mutant_d_out"
  python3 - "$WORKFLOW" "$mut_d" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = ('          - sanitizer: asan\n'
       '            bdir: linux-clang-asan-py\n'
       '            conan_profile: linux-clang-asan\n'
       '            cfg_extra: "-DFIXPP_ENABLE_ASAN=ON -DBUILD_SHARED_LIBS=ON -DFIXPP_PYTHON_SANITIZER=asan"\n'
       '            san_opts: "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1"\n')
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, ""))
PYEOF
  if cmp -s "$WORKFLOW" "$mut_d"; then
    fail "mutant D: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$mut_d" "mutant-d" ) >"$mut_d_out" 2>&1; then
    fail "mutant D (deleted asan matrix entry) did NOT fail the pin — the matrix exact-set census cannot distinguish it from the real policy"
  fi
  grep -q "matrix set" "$mut_d_out" \
    || fail "mutant D failed the pin for the WRONG reason: $(cat "$mut_d_out")"
  echo "RED (expected): mutant D (deleted asan matrix entry) — $(cat "$mut_d_out")"

  # Mutant E (gate-b/r2 finding 2, second surviving shape found while writing
  # the fix): a second, hard-coded `-pr` appended after the parameterised
  # one. The parameterised literal is still present, so a bare `grep -qF`
  # for it (Codex's original prescription) would pass; Conan honours the
  # LAST `-pr` on the command line, so this silently reverts to
  # linux-clang-debug the same way mutant C does, just through a different
  # surviving shape. The counted-occurrence form of B2 is what catches it.
  local mut_e="$mut_dir/tier1-mutant-e.yml"
  local mut_e_out="$mut_dir/mutant_e_out"
  python3 - "$WORKFLOW" "$mut_e" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = ("          conan install . \\\n"
       "            -pr conan/profiles/${{ matrix.conan_profile }} \\\n")
new = ("          conan install . \\\n"
       "            -pr conan/profiles/${{ matrix.conan_profile }} \\\n"
       "            -pr conan/profiles/linux-clang-debug \\\n")
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF
  if cmp -s "$WORKFLOW" "$mut_e"; then
    fail "mutant E: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$mut_e" "mutant-e" ) >"$mut_e_out" 2>&1; then
    fail "mutant E (second hard-coded -pr appended) did NOT fail the pin — the step-parameterisation assertion cannot distinguish it from the real policy"
  fi
  grep -q "occurrence(s), expected exactly" "$mut_e_out" \
    || fail "mutant E failed the pin for the WRONG reason: $(cat "$mut_e_out")"
  echo "RED (expected): mutant E (second hard-coded -pr appended) — $(cat "$mut_e_out")"
}

run_mutant_checks

echo "PASS: ci/test-tier1-python-policy.sh — all five mutants proven RED, real workflow proven GREEN"
