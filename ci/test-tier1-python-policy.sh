#!/usr/bin/env bash
# Regression pin for tier 1's Python policy.
#
# HISTORY, because the target moved. #243/#251 wrote this to pin the
# sanitizer -> Conan-profile mapping of the `python-bindings` JOB, plus the
# `PY_RE` python-relevance filter in `gate-precheck`. #254 folded that job into
# the six `linux` matrix legs and deleted it, taking four of the six original
# assertions and five of the six mutants with it. What replaces them is not a
# narrowing: the same mapping now lives in ci/derive-python-sanitizer.sh and is
# pinned by DRIVING THE REAL SCRIPT, which is stronger than parsing YAML for it.
#
# A naive version of this pin would re-read the YAML to confirm the YAML —
# a tautology. These assertions are NOT that:
#
#   1. Derive-script behaviour, over an EXACT SET read from the workflow: the
#      six presets in the `linux` job's `strategy.matrix.preset` are read out of
#      tier1.yml and ci/derive-python-sanitizer.sh is EXECUTED on each, with all
#      three of its outputs (`sanitizer`, `rt_base`, `san_opts`) compared against
#      the expected table below.
#      ⚠️ **Precisely what is exact, corrected at Gate B round 1 (F3).** The set
#      equality holds between the MATRIX and this file's EXPECTED_DERIVE table:
#      a preset added to the matrix without a row here fails, and vice versa.
#      It does NOT enumerate the script's own accepted arms — adding a valid
#      `linux-clang-msan)` arm to the script while leaving it out of the matrix
#      still passes. That is untested-but-unreachable code, not a live
#      false-green (the workflow only ever calls the script with `matrix.preset`,
#      and that set IS pinned exactly), but the earlier wording claimed more than
#      it delivered. A `--list-presets` mode was considered and rejected: it adds
#      a surface to the production script to close a gap with no reachable defect.
#      Plus one unknown preset, asserting exit 1 rather than a defaulted `none`.
#   2. Call site: a tested script the workflow never invokes is the dead-call-site
#      shape, so BOTH halves are pinned — the `linux` job has a step that invokes
#      ci/derive-python-sanitizer.sh with `matrix.preset`, AND all three outputs
#      are consumed somewhere in that job. gate-b/r2 finding 2 on the deleted job
#      was exactly this failure mode under a different name: an interpolation
#      present but dead. ⚠️ `san_opts` is the one that matters most — a UBSan leg
#      that loses `halt_on_error=1` runs, finds, prints, and exits 0 (R2-P2-3).
#   3. `FIXPP_INSTALL_PYTHON=OFF` is on the `linux` job's Configure line. Without
#      it the Python payload enters packages-linux-{clang,gcc}-release and
#      falsifies L-056-4, and the §4.5.3 assertion step could be deleted with
#      nothing noticing.
#   4. `PY_RE` case table: the literal is extracted from the `gate-precheck`
#      step (not duplicated here, so this pin cannot pass against a stale
#      copy) and evaluated with `grep -E` against a fixed positive/negative
#      path list — genuinely behavioural, not a re-statement of the pattern.
#      ⚠️ UNCHANGED BY #254 and deliberately so: narrowing PY_RE now that it
#      gates the wheel jobs alone is #253's, not this change's (NG-4).
#   5. `tier1-required`'s `needs:` exact-set census: SEVEN job names since #254
#      (was eight; `python-bindings` removed). This is the assertion that guards
#      the trap — the `result` of a job not in `needs:` is the EMPTY STRING,
#      which is neither `success` nor `skipped`, so a half-applied deletion reds
#      the required check on every non-release run.
#
# EVERY assertion has at least one mutant driven through this same script via a
# MUTATED COPY of tier1.yml or of the derive script, so the pin proves it can
# fail before it is trusted to pass (feedback_verification_grep_must_be_proven_
# nonzero_on_the_unfixed_tree). See run_mutant_checks() below.
#
# ⚠️ M4 IS NEW AND ITS ABSENCE WAS A REAL HOLE. Assertion 5 (the `needs:`
# census) shipped in #251 with NO mutant — it had never been proven RED. #254
# re-bases it from eight names to seven, and re-basing a never-tested assertion
# under a new number is how an untested check acquires false credibility.
#
# ⚠️ The declared mutant count is asserted against the number that actually ran
# (see MUTANTS_DECLARED). PR #251's own review loop shipped a summary claiming
# five mutants where six ran; the remedy for that is a machine check, not a
# human eyeball.
#
# Hermetic: reads tracked files only. No build, no Conan, no network, no `nm`.
# It DOES hard-require an importable PyYAML on the caller's `python3` (see
# below) — providing that is the caller's responsibility, not this script's.
#
# Usage: ci/test-tier1-python-policy.sh [path-to-tier1.yml]
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKFLOW="${1:-$repo_root/.github/workflows/tier1.yml}"
# The derive script is a SECOND mutation target: M1/M2/M3 mutate it, not the
# workflow, so it has to be overridable the same way $WORKFLOW is.
DERIVE="${2:-$repo_root/ci/derive-python-sanitizer.sh}"

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

# #254: `python-bindings` is gone. The set this pin is exact over is now the
# `linux` job's own preset list — a list of STRINGS, not the list of dicts the
# deleted job's matrix.include was. Reading it from the YAML rather than
# hardcoding it here is what makes assertion 1 exact-SET rather than aspirational:
# add a preset to the matrix and the derive script must grow an arm for it.
linux_job = jobs["linux"]
linux_presets = linux_job["strategy"]["matrix"]["preset"]

# ⚠️ PER-STEP OBJECTS, not one concatenated blob — and the difference is the
# whole of Gate B round 1's F5. The previous version emitted only
# "\n".join(step["run"]), which means the step `if:` guards, the step `id`s and
# the YAML `env:` mappings WERE NOT IN THE STRING AT ALL. Every assertion written
# against it could therefore only ask "does this text appear ANYWHERE in the
# job", never "is it in the position that makes it load-bearing" — and a mutant
# that hard-coded the real consumers while re-introducing the tokens as dead
# `echo`s passed the pin, for a workflow that ran the UBSan leg's pytest under
# ASAN_OPTIONS with no halt_on_error=1. Measured, not hypothesised.
#
# This is the SAME class the install-flag assertion was already fixed for (see
# `assert_derive_call_site`). It had been fixed at ONE of five sites.
linux_steps = [
    {
        "name": str(step.get("name", "")),
        "id":   str(step.get("id", "")),
        "if":   str(step.get("if", "")),
        "run":  str(step.get("run", "")),
        # YAML `env:` on the step. Deliberately kept SEPARATE from the shell
        # `env` command prefix inside `run:` — the sanitizer pytest step has
        # both (a real YAML env: for PYTHONPATH, and `env <opts> pytest` in the
        # shell), and conflating them is how a position assertion silently
        # becomes a presence assertion again.
        "env":  {str(k): str(v) for k, v in (step.get("env") or {}).items()},
    }
    for step in linux_job["steps"]
]

# Retained for the assertions that legitimately are job-wide.
linux_runs = "\n".join(s["run"] for s in linux_steps)

gp_steps = jobs["gate-precheck"]["steps"]
decide_run = None
for step in gp_steps:
    if step.get("id") == "decide":
        decide_run = step.get("run", "")
        break

tier1_required_needs = jobs["tier1-required"]["needs"]

out = {
    "linux_presets": linux_presets,
    "linux_steps": linux_steps,
    "linux_runs": linux_runs,
    "decide_run": decide_run,
    "tier1_required_needs": tier1_required_needs,
}
print(json.dumps(out))
PYEOF
}

# ── 1: the derive script, driven over the linux matrix's EXACT preset set ───
# preset|sanitizer|rt_base|san_opts  — mirrors design doc §4.3.2, and the values
# are carried over byte-for-byte from the deleted job's strategy.matrix.include.
#
# ⚠️ `$GITHUB_WORKSPACE` in the tsan row is LITERAL and must stay unexpanded:
# the consumer is `env <opts> pytest` inside a `run:` block, which expands it
# there — exactly what the matrix scalar it replaces did. Single-quoted for that
# reason; do not "fix" it.
EXPECTED_DERIVE=(
  "linux-clang-debug|none||"
  "linux-clang-release|none||"
  "linux-clang-asan|asan|asan|ASAN_OPTIONS=detect_leaks=0:halt_on_error=1"
  'linux-clang-ubsan|ubsan|ubsan_standalone|UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1'
  'linux-clang-tsan|tsan|tsan|TSAN_OPTIONS=suppressions=$GITHUB_WORKSPACE/bindings/python/tests/tsan_suppressions.txt:halt_on_error=1'
  "linux-gcc-release|none||"
)

assert_derive_script() {
  local json="$1" case_id="$2" derive="$3"
  local got_presets expected_presets entry preset out
  local e_san e_rt e_opts g_san g_rt g_opts

  got_presets="$(echo "$json" | jq -r '.linux_presets[]' | sort | tr '\n' ',')"
  expected_presets="$(printf '%s\n' "${EXPECTED_DERIVE[@]}" | cut -d'|' -f1 | sort | tr '\n' ',')"
  # Exact SET equality in both directions — a preset added to the matrix without
  # an arm in the script fails here, and so does the reverse.
  [ "$got_presets" = "$expected_presets" ] \
    || fail "$case_id: linux matrix preset set '$got_presets' != this pin's expected set '$expected_presets'"

  for entry in "${EXPECTED_DERIVE[@]}"; do
    preset="$(echo "$entry" | cut -d'|' -f1)"
    e_san="$(echo "$entry"  | cut -d'|' -f2)"
    e_rt="$(echo "$entry"   | cut -d'|' -f3)"
    e_opts="$(echo "$entry" | cut -d'|' -f4-)"

    # A preset in the matrix that the script REJECTS is an exhaustiveness
    # failure, and it is reported as one — distinct from a value mismatch, so a
    # mutant that deletes an arm cannot be confused with one that changes a value.
    if ! out="$(bash "$derive" "$preset" 2>&1)"; then
      fail "$case_id: ci/derive-python-sanitizer.sh REJECTED '$preset', which IS in tier1.yml's linux matrix — the case is not exhaustive over the matrix. Output: $out"
    fi

    g_san="$(echo  "$out" | sed -n 's/^sanitizer=//p')"
    g_rt="$(echo   "$out" | sed -n 's/^rt_base=//p')"
    g_opts="$(echo "$out" | sed -n 's/^san_opts=//p')"

    [ "$g_san" = "$e_san" ] \
      || fail "$case_id: derive mismatch for '$preset' field 'sanitizer': expected '$e_san', got '$g_san'"
    [ "$g_rt" = "$e_rt" ] \
      || fail "$case_id: derive mismatch for '$preset' field 'rt_base': expected '$e_rt', got '$g_rt'"
    [ "$g_opts" = "$e_opts" ] \
      || fail "$case_id: derive mismatch for '$preset' field 'san_opts': expected '$e_opts', got '$g_opts'"
  done

  # Fail-closed on an unknown preset. A defaulted `none` on a future sanitizer
  # leg builds an UNINSTRUMENTED _fixpp.so and reports green — the #251 class.
  if bash "$derive" linux-clang-definitely-not-a-preset >/dev/null 2>&1; then
    fail "$case_id: derive script did NOT exit non-zero on an unknown preset — it must be fail-closed, never a defaulted 'none'"
  fi
}

# ── 2 + 3: the call site, the three outputs IN POSITION, and the OFF flag ───
#
# ⚠️ POSITION, NOT PRESENCE. Gate B round 1 F5 (P1) measured the difference: the
# previous version grepped the job's whole concatenated `run:` text for
# `steps.pysan.outputs.<out>`. A mutant that hard-coded both real consumers and
# re-introduced both tokens as dead `echo ... >/dev/null` lines PASSED this pin,
# for a workflow that ran the UBSan leg's pytest under ASAN_OPTIONS with no
# halt_on_error=1 and LD_PRELOADed the ASan runtime. M7/M8 delete the token
# outright, so they only ever proved ABSENCE DETECTION — never that the token
# sits where it does work. M9/M10 are the dead-echo mutants that close it.
#
# This is the same class as the install-flag false-green M5 caught. That one was
# fixed at ONE of five sites; these are the other four.
assert_derive_call_site() {
  local json="$1" case_id="$2"

  # ── helpers: select ONE step by a literal in a named field ────────────────
  # Returns the step object, or empty. `--arg` keeps jq from interpreting the
  # needle, so `${{ ... }}` and `-D...` are safe.
  step_where() {  # <field> <literal>
    echo "$json" | jq -c --arg f "$1" --arg needle "$2" \
      'first(.linux_steps[] | select((.[$f] // "") | contains($needle)))'
  }
  step_field() { echo "$1" | jq -r --arg f "$2" '.[$f] // ""'; }

  # ── the call site: SOME step must actually invoke the script ──────────────
  local derive_step
  derive_step="$(step_where run 'ci/derive-python-sanitizer.sh "${{ matrix.preset }}"')"
  [ -n "$derive_step" ] && [ "$derive_step" != "null" ] \
    || fail "$case_id: no step in the linux job invokes ci/derive-python-sanitizer.sh with matrix.preset — the script is tested but never called (dead call site)"

  # ⚠️ The step's `id:` is what every `steps.<id>.outputs.*` interpolation binds
  # to. Rename it and all three consumers below silently become the EMPTY
  # STRING while still reading as present. Nothing pinned this before F5.
  local derive_id
  derive_id="$(step_field "$derive_step" id)"
  [ "$derive_id" = "pysan" ] \
    || fail "$case_id: the derive step's id is '$derive_id', expected 'pysan' — every steps.pysan.outputs.* interpolation in this job would resolve to the empty string"

  # ── `sanitizer`: the Configure argument AND both pytest `if:` guards ───────
  local cfg
  cfg="$(step_where run 'cmake --preset ${{ matrix.preset }}')"
  [ -n "$cfg" ] && [ "$cfg" != "null" ] \
    || fail "$case_id: no Configure step (no run: containing 'cmake --preset \${{ matrix.preset }}')"
  local cfg_run; cfg_run="$(step_field "$cfg" run)"

  grep -qF -e '-DFIXPP_PYTHON_SANITIZER=${{ steps.pysan.outputs.sanitizer }}' <<<"$cfg_run" \
    || fail "$case_id: the Configure step does not pass -DFIXPP_PYTHON_SANITIZER=\${{ steps.pysan.outputs.sanitizer }} — the derived identity is not what configures the build"

  # ⚠️ SCOPED TO THE CONFIGURE STEP, not the job. A bare job-wide search also
  # matches the DIAGNOSTIC MESSAGE of the install-witness step, which quotes the
  # flag by name — with that match available, deleting the real flag left this
  # GREEN (M5). `-e` is required too: without it grep parses the leading `-D` as
  # its own --devices option and dies, which under `set -e` aborts the pin.
  grep -qF -e '-DFIXPP_INSTALL_PYTHON=OFF' <<<"$cfg_run" \
    || fail "$case_id: the linux job's Configure step does not pass -DFIXPP_INSTALL_PYTHON=OFF — the Python payload would enter packages-linux-{clang,gcc}-release and falsify L-056-4 (#254)"

  # Both pytest steps must BRANCH on the derived value. Substring sniffing
  # (`contains(matrix.preset, 'san')`) is the mapping spelled twice; a literal
  # `if:` is the mapping consumed once.
  local none_step san_step
  none_step="$(step_where if "steps.pysan.outputs.sanitizer == 'none'")"
  san_step="$(step_where  if "steps.pysan.outputs.sanitizer != 'none'")"
  [ -n "$none_step" ] && [ "$none_step" != "null" ] \
    || fail "$case_id: no step is guarded by \`if: steps.pysan.outputs.sanitizer == 'none'\` — the non-sanitizer pytest leg does not branch on the derived identity"
  [ -n "$san_step" ] && [ "$san_step" != "null" ] \
    || fail "$case_id: no step is guarded by \`if: steps.pysan.outputs.sanitizer != 'none'\` — the sanitizer pytest leg does not branch on the derived identity"

  # Both guards must be on steps that actually run pytest, or the branch is
  # decorative.
  local none_run san_run
  none_run="$(step_field "$none_step" run)"
  san_run="$(step_field "$san_step" run)"
  grep -qF 'pytest bindings/python/tests/' <<<"$none_run" \
    || fail "$case_id: the sanitizer=='none' guarded step does not run pytest bindings/python/tests/"
  grep -qF 'pytest bindings/python/tests/' <<<"$san_run" \
    || fail "$case_id: the sanitizer!='none' guarded step does not run pytest bindings/python/tests/"

  # ── `rt_base`: the RT_BASE assignment INSIDE the sanitizer pytest step ─────
  # Not "appears somewhere". If RT_BASE is hard-coded, the ubsan leg LD_PRELOADs
  # the wrong runtime while the token still exists in a dead echo elsewhere.
  grep -qE '^[[:space:]]*RT_BASE="\$\{\{ steps\.pysan\.outputs\.rt_base \}\}"[[:space:]]*$' <<<"$san_run" \
    || fail "$case_id: the sanitizer pytest step does not assign RT_BASE=\"\${{ steps.pysan.outputs.rt_base }}\" — the LD_PRELOADed runtime is not the derived one (a dead reference elsewhere does not count)"

  # ── `san_opts`: the shell `env` PREFIX on the pytest invocation ────────────
  # ⚠️ The shell `env <opts> cmd` prefix, NOT a YAML `env:` key — this step has a
  # real YAML env: block for PYTHONPATH, and conflating the two turns this back
  # into a presence check. Lose this and a UBSan leg without halt_on_error=1
  # runs, finds, prints, and exits 0.
  grep -qE '^[[:space:]]*env \$\{\{ steps\.pysan\.outputs\.san_opts \}\} ' <<<"$san_run" \
    || fail "$case_id: the sanitizer pytest step does not prefix its invocation with \`env \${{ steps.pysan.outputs.san_opts }}\` — the sanitizer options are not the derived ones, so a leg can lose halt_on_error=1 and still report green (R2-P2-3)"
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
# durability rests on: these job names staying in tier1-required's needs:.
#
# ⚠️ SEVEN since #254, was eight — `python-bindings` removed with the job. This
# is the census that guards the trap: the `result` of a job absent from `needs:`
# evaluates to the EMPTY STRING, which is neither `success` nor `skipped`, so a
# half-applied deletion reds this required check on EVERY non-release run, in
# BOTH branches of the python_touched split. Mutant M4 proves this assertion can
# fail; before #254 it never had a mutant at all.
assert_tier1_required_needs() {
  local json="$1" case_id="$2"
  local EXPECTED_NEEDS="check-layers,ci-script-pins,coverage,gate-precheck,linux,python-wheel-build,python-wheel-test"
  local got
  got="$(echo "$json" | jq -r '.tier1_required_needs | sort | join(",")')"
  [ "$got" = "$EXPECTED_NEEDS" ] \
    || fail "$case_id: tier1-required needs set '$got' != expected '$EXPECTED_NEEDS'"
}

# Two mutation targets, so two parameters: M1/M2/M3 mutate the DERIVE SCRIPT
# and leave the workflow alone; M4-M8 do the reverse.
run_full_pin() {
  local workflow="$1" case_id="$2" derive="${3:-$DERIVE}"
  local json
  json="$(extract_json "$workflow")"
  assert_derive_script "$json" "$case_id" "$derive"
  assert_derive_call_site "$json" "$case_id"
  assert_py_re_case_table "$json" "$case_id"
  assert_tier1_required_needs "$json" "$case_id"
}

# ── Real workflow: must pass all four assertions ────────────────────────────
run_full_pin "$WORKFLOW" "tier1.yml"
echo "PASS: derive-script table + call site + FIXPP_INSTALL_PYTHON=OFF + PY_RE case table + tier1-required needs — $WORKFLOW"

# ── Mutant witnesses: each must be shown RED before this pin is trusted ─────
# (feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree —
# an assertion never shown to fail is not evidence.) All mutants are applied
# to a TEMP COPY; the tracked workflow is never touched.
# ⚠️ DECLARED vs RUN, checked by machine. PR #251's own review loop shipped a
# summary claiming five mutants where six ran, caught by orchestrator
# verification after the fixer reported done. A human eyeball is not the remedy
# for a miscount; a counter is. MUTANTS_RUN is incremented by each mutant AFTER
# it has been proven RED for the right reason, so an early `return` or a mutant
# silently commented out changes the total.
MUTANTS_DECLARED=12  # M1 M2 M3 B M4 M5 M6 M7 M8 M9 M10 M11
MUTANTS_RUN=0

run_mutant_checks() {
  local mut_dir
  mut_dir="$(mktemp -d)"
  trap 'rm -rf "$mut_dir"' RETURN

  # ── M1/M2/M3 mutate THE DERIVE SCRIPT, not the workflow ────────────────
  # Heirs of the deleted mutants A and D: the mapping they guarded moved out of
  # YAML into ci/derive-python-sanitizer.sh, so the mutants follow it.

  # M1 (heir of mutant A): tsan -> none IN THE SCRIPT. Must fail the VALUE check
  # and name the preset. This is the shape that silently builds an
  # uninstrumented _fixpp.so on the tsan leg and reports green.
  local m1="$mut_dir/derive-m1.sh"
  local m1_out="$mut_dir/m1_out"
  python3 - "$DERIVE" "$m1" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "  linux-clang-tsan)\n    SANITIZER=tsan\n"
new = "  linux-clang-tsan)\n    SANITIZER=none\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$DERIVE" "$m1"; then
    fail "M1: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$WORKFLOW" "M1" "$m1" ) >"$m1_out" 2>&1; then
    fail "M1 (tsan -> none in the derive script) did NOT fail the pin — the derive value check cannot distinguish it from the real mapping"
  fi
  grep -q "derive mismatch for 'linux-clang-tsan' field 'sanitizer'" "$m1_out" \
    || fail "M1 failed the pin for the WRONG reason: $(cat "$m1_out")"
  echo "RED (expected): M1 (tsan -> none in the derive script) — $(cat "$m1_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # M2 (heir of mutant D): delete linux-gcc-release from the script's case. Must
  # fail the EXHAUSTIVENESS check, not merely a value check — the per-preset
  # value assertions validate only whatever arms remain, which is exactly how a
  # subset check misses a deletion.
  local m2="$mut_dir/derive-m2.sh"
  local m2_out="$mut_dir/m2_out"
  python3 - "$DERIVE" "$m2" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "  linux-clang-debug|linux-clang-release|linux-gcc-release)\n"
new = "  linux-clang-debug|linux-clang-release)\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$DERIVE" "$m2"; then
    fail "M2: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$WORKFLOW" "M2" "$m2" ) >"$m2_out" 2>&1; then
    fail "M2 (linux-gcc-release dropped from the derive case) did NOT fail the pin — the exhaustiveness check cannot see a deleted arm"
  fi
  grep -q "not exhaustive over the matrix" "$m2_out" \
    || fail "M2 failed the pin for the WRONG reason (it must be the EXHAUSTIVENESS message, not a value mismatch): $(cat "$m2_out")"
  echo "RED (expected): M2 (linux-gcc-release dropped from the derive case) — $(cat "$m2_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # M3: ubsan_standalone -> ubsan. The ONE non-identity row in the table, and
  # therefore the one a future edit "normalises" away. libclang_rt.ubsan.* does
  # not exist; the leg would then LD_PRELOAD nothing.
  local m3="$mut_dir/derive-m3.sh"
  local m3_out="$mut_dir/m3_out"
  python3 - "$DERIVE" "$m3" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "    RT_BASE=ubsan_standalone\n"
new = "    RT_BASE=ubsan\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$DERIVE" "$m3"; then
    fail "M3: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$WORKFLOW" "M3" "$m3" ) >"$m3_out" 2>&1; then
    fail "M3 (ubsan_standalone -> ubsan) did NOT fail the pin"
  fi
  grep -q "derive mismatch for 'linux-clang-ubsan' field 'rt_base'" "$m3_out" \
    || fail "M3 failed the pin for the WRONG reason: $(cat "$m3_out")"
  echo "RED (expected): M3 (ubsan_standalone -> ubsan) — $(cat "$m3_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

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
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # ── M4-M8 mutate THE WORKFLOW ──────────────────────────────────────────

  # M4: drop one name from tier1-required's needs:. ⚠️ THIS MUTANT DID NOT EXIST
  # BEFORE #254. Assertion 5 shipped in #251 with no witness, so it had never
  # been proven RED — and #254 re-bases it from eight names to seven. Carrying a
  # never-tested assertion forward under a new number is how an untested check
  # acquires false credibility, so the witness lands with the re-base.
  #
  # `coverage` is the target rather than a python job precisely because the
  # census is about the SET, not about python.
  local m4="$mut_dir/tier1-m4.yml"
  local m4_out="$mut_dir/m4_out"
  python3 - "$WORKFLOW" "$m4" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "    needs: [gate-precheck, linux, coverage, check-layers, ci-script-pins,\n"
new = "    needs: [gate-precheck, linux, check-layers, ci-script-pins,\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$WORKFLOW" "$m4"; then
    fail "M4: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$m4" "M4" ) >"$m4_out" 2>&1; then
    fail "M4 (coverage dropped from tier1-required's needs) did NOT fail the pin — the needs census has never been proven RED and cannot be trusted"
  fi
  grep -q "tier1-required needs set" "$m4_out" \
    || fail "M4 failed the pin for the WRONG reason: $(cat "$m4_out")"
  echo "RED (expected): M4 (coverage dropped from tier1-required needs) — $(cat "$m4_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # M5: remove -DFIXPP_INSTALL_PYTHON=OFF from the Configure line. The payload
  # then enters packages-linux-{clang,gcc}-release and falsifies L-056-4 — and
  # note that ctest would stay GREEN, because the ON-side witness registers
  # instead of the OFF-side one.
  local m5="$mut_dir/tier1-m5.yml"
  local m5_out="$mut_dir/m5_out"
  python3 - "$WORKFLOW" "$m5" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "          -DFIXPP_INSTALL_PYTHON=OFF\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, ""))
PYEOF2
  if cmp -s "$WORKFLOW" "$m5"; then
    fail "M5: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$m5" "M5" ) >"$m5_out" 2>&1; then
    fail "M5 (FIXPP_INSTALL_PYTHON=OFF removed) did NOT fail the pin"
  fi
  grep -q "FIXPP_INSTALL_PYTHON=OFF" "$m5_out" \
    || fail "M5 failed the pin for the WRONG reason: $(cat "$m5_out")"
  echo "RED (expected): M5 (FIXPP_INSTALL_PYTHON=OFF removed) — $(cat "$m5_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # M6: the derive step stops invoking the script. The script is still tested
  # and still correct — and never runs. Dead call site.
  local m6="$mut_dir/tier1-m6.yml"
  local m6_out="$mut_dir/m6_out"
  python3 - "$WORKFLOW" "$m6" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '        run: ci/derive-python-sanitizer.sh "${{ matrix.preset }}" >> "$GITHUB_OUTPUT"\n'
new = '        run: echo "sanitizer=none" >> "$GITHUB_OUTPUT"\n'
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$WORKFLOW" "$m6"; then
    fail "M6: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$m6" "M6" ) >"$m6_out" 2>&1; then
    fail "M6 (derive step no longer invokes the script) did NOT fail the pin — a tested script nothing calls is the dead-call-site shape"
  fi
  grep -q "dead call site" "$m6_out" \
    || fail "M6 failed the pin for the WRONG reason: $(cat "$m6_out")"
  echo "RED (expected): M6 (derive step no longer invokes the script) — $(cat "$m6_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # M7 (R2-P2-3): san_opts goes unconsumed. The sanitizer legs then run pytest
  # WITHOUT halt_on_error=1 — UBSan reports, pytest exits 0, the leg is green.
  # This is the output whose death is hardest to see, which is why it gets its
  # own mutant rather than riding on M1's.
  local m7="$mut_dir/tier1-m7.yml"
  local m7_out="$mut_dir/m7_out"
  python3 - "$WORKFLOW" "$m7" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '          env ${{ steps.pysan.outputs.san_opts }} LD_PRELOAD="$RT" \\\n'
new = '          env LD_PRELOAD="$RT" \\\n'
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$WORKFLOW" "$m7"; then
    fail "M7: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$m7" "M7" ) >"$m7_out" 2>&1; then
    fail "M7 (san_opts unconsumed) did NOT fail the pin — a sanitizer leg without halt_on_error=1 reports green after a real finding"
  fi
  grep -q "steps.pysan.outputs.san_opts" "$m7_out" \
    || fail "M7 failed the pin for the WRONG reason: $(cat "$m7_out")"
  echo "RED (expected): M7 (san_opts unconsumed) — $(cat "$m7_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # M8 (R2-P2-3): rt_base goes unconsumed, re-derived inline instead. The
  # shape the extraction exists to prevent — the mapping spelled a second time.
  local m8="$mut_dir/tier1-m8.yml"
  local m8_out="$mut_dir/m8_out"
  python3 - "$WORKFLOW" "$m8" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '          RT_BASE="${{ steps.pysan.outputs.rt_base }}"\n'
new = ('          RT_BASE="${{ steps.pysan.outputs.sanitizer }}"\n'
       '          [ "$RT_BASE" = "ubsan" ] && RT_BASE="ubsan_standalone"\n')
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$WORKFLOW" "$m8"; then
    fail "M8: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$m8" "M8" ) >"$m8_out" 2>&1; then
    fail "M8 (rt_base unconsumed, re-derived inline) did NOT fail the pin — the mapping is back to being spelled twice"
  fi
  grep -q "steps.pysan.outputs.rt_base" "$m8_out" \
    || fail "M8 failed the pin for the WRONG reason: $(cat "$m8_out")"
  echo "RED (expected): M8 (rt_base unconsumed, re-derived inline) — $(cat "$m8_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # ── M9/M10/M11 — the DEAD-USE mutants (Gate B round 1, F5) ─────────────────
  # M7/M8 delete the token outright, so they prove ABSENCE DETECTION only. These
  # keep the token present — in a position where it does nothing — while killing
  # the real consumer. That combination PASSED the pin before F5, for a workflow
  # that ran the UBSan leg under ASAN_OPTIONS with no halt_on_error=1.

  # M9: san_opts hard-coded at the pytest invocation, token survives as a dead echo.
  local m9="$mut_dir/tier1-m9.yml"
  local m9_out="$mut_dir/m9_out"
  python3 - "$WORKFLOW" "$m9" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '          env ${{ steps.pysan.outputs.san_opts }} LD_PRELOAD="$RT" \\\n'
new = ('          echo "dead: ${{ steps.pysan.outputs.san_opts }}" >/dev/null\n'
       '          env ASAN_OPTIONS=detect_leaks=0 LD_PRELOAD="$RT" \\\n')
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$WORKFLOW" "$m9"; then
    fail "M9: python literal-replace produced no change — mutant not applied"
  fi
  grep -qF 'steps.pysan.outputs.san_opts' "$m9" \
    || fail "M9: the dead reference was not preserved — this mutant must keep the token PRESENT, or it degenerates into M7"
  if ( run_full_pin "$m9" "M9" ) >"$m9_out" 2>&1; then
    fail "M9 (san_opts hard-coded, token alive as a dead echo) did NOT fail the pin — the assertion is a presence check, not a position check, and a UBSan leg without halt_on_error=1 would report green"
  fi
  grep -q "does not prefix its invocation with" "$m9_out" \
    || fail "M9 failed the pin for the WRONG reason: $(cat "$m9_out")"
  echo "RED (expected): M9 (san_opts dead-echo) — $(cat "$m9_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # M10: rt_base hard-coded, token survives as a dead echo.
  local m10="$mut_dir/tier1-m10.yml"
  local m10_out="$mut_dir/m10_out"
  python3 - "$WORKFLOW" "$m10" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = '          RT_BASE="${{ steps.pysan.outputs.rt_base }}"\n'
new = ('          echo "dead: ${{ steps.pysan.outputs.rt_base }}" >/dev/null\n'
       '          RT_BASE=asan\n')
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$WORKFLOW" "$m10"; then
    fail "M10: python literal-replace produced no change — mutant not applied"
  fi
  grep -qF 'steps.pysan.outputs.rt_base' "$m10" \
    || fail "M10: the dead reference was not preserved — this mutant must keep the token PRESENT, or it degenerates into M8"
  if ( run_full_pin "$m10" "M10" ) >"$m10_out" 2>&1; then
    fail "M10 (rt_base hard-coded to asan, token alive as a dead echo) did NOT fail the pin — the ubsan leg would LD_PRELOAD the ASan runtime"
  fi
  grep -q 'does not assign RT_BASE=' "$m10_out" \
    || fail "M10 failed the pin for the WRONG reason: $(cat "$m10_out")"
  echo "RED (expected): M10 (rt_base dead-echo) — $(cat "$m10_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

  # M11: rename the derive step's id. Every `steps.pysan.outputs.*` then resolves
  # to the EMPTY STRING while all three interpolations still read as present —
  # an untested structural coupling until F5. (Fail-closed today, since an empty
  # `sanitizer` sends all six legs down the `!= 'none'` branch where the RT
  # existence check reds; that is luck, not a pin.)
  local m11="$mut_dir/tier1-m11.yml"
  local m11_out="$mut_dir/m11_out"
  python3 - "$WORKFLOW" "$m11" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        id: pysan\n"
new = "        id: py_san\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$WORKFLOW" "$m11"; then
    fail "M11: python literal-replace produced no change — mutant not applied"
  fi
  if ( run_full_pin "$m11" "M11" ) >"$m11_out" 2>&1; then
    fail "M11 (derive step id renamed) did NOT fail the pin — every steps.pysan.outputs.* would resolve to the empty string with nothing noticing"
  fi
  grep -q "expected 'pysan'" "$m11_out" \
    || fail "M11 failed the pin for the WRONG reason: $(cat "$m11_out")"
  echo "RED (expected): M11 (derive step id renamed) — $(cat "$m11_out")"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

}

run_mutant_checks

if [ "$MUTANTS_RUN" != "$MUTANTS_DECLARED" ]; then
  fail "mutant count mismatch: $MUTANTS_RUN ran, $MUTANTS_DECLARED declared. A mutant was added, removed or short-circuited without updating MUTANTS_DECLARED — the summary below would otherwise claim coverage this run did not have."
fi

echo "PASS: ci/test-tier1-python-policy.sh — $MUTANTS_RUN/$MUTANTS_DECLARED mutants proven RED, real workflow proven GREEN"
