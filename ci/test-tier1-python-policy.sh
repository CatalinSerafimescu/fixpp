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
#   2+3. The four load-bearing steps of the `linux` job — derive, Configure, and
#      the two pytest steps — have their `run:` blocks compared VERBATIM against
#      canonical text held in this file, plus their `id:`/`if:` fields, which are
#      not part of `run:`. This is a GOLDEN, adopted at Gate B round 3b after the
#      previous content-matching form was defeated nine times; the full reasoning,
#      the cost, and what the comparison is and is not, are documented at the
#      EXPECTED_*_RUN block below. Read that before touching this area.
#      ⚠️ `san_opts` is the consumer that matters most — a UBSan leg that loses
#      `halt_on_error=1` runs, finds, prints, and exits 0 (R2-P2-3).
#   6. `ci-script-pins` invokes all four `ci/` harnesses. A harness that is green
#      when run but never runs has no coverage at all (round 3b finding 5).
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
# ⚠️ ACCEPTED BRITTLENESS — WAIVED AT GATE B ROUND 2 (finding 6a, P3), AND
# GENERALIZED AT ROUND 3b. DO NOT "FIX" THIS BY LOOSENING ANYTHING.
#
# The four pinned steps match their canonical text exactly, so a purely cosmetic
# edit to any of them reds this pin. That was the accepted trade at round 2 for
# one assertion (`readonly RT_BASE=…` is semantically identical and RED — measured
# as X4), and round 3b made it the whole design. The reasoning is unchanged and
# now much stronger:
#
#   * a false RED here is LOUD, self-announcing, and fixed in one place by whoever
#     trips it — the person who edited the step is the person reading the diff the
#     failure prints;
#   * every false-green this pin has ever had — nine of them, measured across
#     three gate rounds — came from tolerating text that was not the text that
#     decides. Loosening is a move back toward exactly that.
#
# The one loosening that IS present is principled and proven: the comparison is
# against the YAML-PARSED value, so a folded scalar re-wrapped at different
# columns is the same string and stays GREEN. M28 is the positive control for it,
# and it is what distinguishes this golden from a file hash.
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

# #254 / Gate B round 3, Codex finding 6. `ci-script-pins` is where every ci/
# shell harness actually EXECUTES. A harness that is green when run but is never
# run is the dead-call-site shape this pin already guards for the derive script;
# it was measured on the new install-witness harness (replace the invocation with
# an echo and the job stays green, the negative layouts stop being exercised).
ci_pin_runs = "\n".join(str(s.get("run", "")) for s in jobs["ci-script-pins"]["steps"])

out = {
    "linux_presets": linux_presets,
    "linux_steps": linux_steps,
    "linux_runs": linux_runs,
    "decide_run": decide_run,
    "tier1_required_needs": tier1_required_needs,
    "ci_pin_runs": ci_pin_runs,
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

# ── 2 + 3: the four load-bearing steps, pinned as CANONICAL BLOCKS ──────────
#
# ⚠️ THIS IS A GOLDEN, AND THAT IS THE WHOLE DESIGN. Read this before changing it.
#
# Rounds 1-3b of Gate B found ONE class — an assertion satisfied by text that is
# not the text that decides — NINE times in this file, four of them inside the fix
# for the previous one. Each fix taught the pin to interpret one more piece of
# shell, and the next round found a construct it did not model:
#
#   round 1   the whole JOB was in scope        -> scope to the step
#   round 2   `first(… contains …)` picked a decoy step; the FIRST occurrence was
#             read instead of the effective one
#   round 3   the step's RAW text was read after selecting on its effective text;
#             a diagnostic `echo` carried the flag; `head -1` of the invocations;
#             `echo` MENTIONING pytest; the same under a correct `env` prefix
#   round 3b  heredoc data; `if false` blocks; `#` inside a QUOTED STRING (which
#             the stripper removed, HIDING a real unprefixed invocation — the
#             stripper could make the check laxer, not stricter, contrary to what
#             its own comment claimed); `pytest … | true`; `pytest … &`;
#             `export RT_BASE=asan`; `printf -v RT_BASE`; `unset RT`
#
# The pin was losing an arms race it could only win by becoming a shell
# interpreter. It is not one, and it will not become one.
#
# ⇒ The four steps whose text decides whether the six legs are correct are now
# compared VERBATIM against expected blocks held here. No comment stripping, no
# continuation joining, no invocation regexes, no notion of what shell means.
#
# The soundness argument is exactly one sentence: ANY change to these blocks REDS.
# `pytest … | true` is not defeated because the pin understands pipelines — it is
# defeated because the text differs. That is a complete argument where the
# interpretive one never was, and it is why ~280 lines of parsing were DELETED
# rather than left beside this as a "defence in depth" a later reader could
# reconstruct the weak form from.
#
# ⚠️ WHAT IS COMPARED IS THE YAML TEXT, NOT WHAT RAN. `${{ … }}` interpolations
# are expanded by Actions before the shell sees them; the blocks below therefore
# contain the interpolations, un-expanded, and that is correct — the property
# being pinned is "the derived value is what this step consumes". Do not "fix"
# this by trying to compare against an expanded command.
#
# ⚠️ COST, ACCEPTED KNOWINGLY, and consistent with the round-2 waiver of finding
# 6a: any edit to these four steps reds this pin, including a purely cosmetic one.
# That is the intended workflow — the failure message prints a unified diff and
# names this block as the place to update. Update it deliberately, having read
# what changed. A reflow-only edit is proven GREEN by M12; a one-character change
# to a flag or to the `env` prefix is proven RED by M13/M14.
#
# ⚠️ SELECTED BY `name:`, NEVER BY CONTENT. Selecting a golden-compared step by
# its own text would be circular, and `name:` is not the text under attack. A
# decoy would need a duplicate name, which the exactly-one rule below catches.
#
# NOT covered here, deliberately: `if:` on the two pytest steps IS part of the
# golden set (asserted separately below, exactly), and the derive/Configure steps
# are asserted to carry NO `if:` at all — a step that cannot run is not a step
# that does the work (round 3b finding 5, applied to this job as well as to
# `ci-script-pins`).

# The expected `run:` text of each load-bearing step, keyed by its exact `name:`.
# Trailing newlines are normalized away on both sides; nothing else is.
EXPECTED_DERIVE_RUN='ci/derive-python-sanitizer.sh "${{ matrix.preset }}" >> "$GITHUB_OUTPUT"'

EXPECTED_CONFIGURE_RUN='cmake --preset ${{ matrix.preset }} -DFIXPP_ARTIFACT_DIR=${{ github.workspace }}/_artifacts -DFIXPP_BUILD_PYTHON=ON -DFIXPP_INSTALL_PYTHON=OFF -DFIXPP_PYTHON_SANITIZER=${{ steps.pysan.outputs.sanitizer }}'

EXPECTED_PYTEST_NONE_RUN='pytest bindings/python/tests/ -v'

# ⚠️ Every line of this one is load-bearing and each has a recorded reason:
#   `set -euo pipefail`   pytest's status must reach the step
#   RT_BASE=<interp>      the derived runtime basename, not a hard-coded one
#   the two `clang` forms both naming schemes are tried (#243)
#   the `[ ! -f ]` guard  LD_PRELOAD of a MISSING path is silently ignored by the
#                         loader, so the suite would run UNINSTRUMENTED and report
#                         green — fail closed instead (R2-P2-3)
#   `env <interp>`        the derived sanitizer options, including halt_on_error=1
#   the bare pytest       no pipeline, no `&`, nothing that swallows its status
EXPECTED_PYTEST_SAN_RUN='set -euo pipefail
RT_BASE="${{ steps.pysan.outputs.rt_base }}"
RT=$(clang -print-file-name=libclang_rt.${RT_BASE}-x86_64.so)
[ -f "$RT" ] || RT="$(clang -print-runtime-dir)/libclang_rt.${RT_BASE}.so"
echo "${{ steps.pysan.outputs.sanitizer }} runtime: $RT"
if [ ! -f "$RT" ]; then
  echo "::error::libclang_rt.${RT_BASE} not found under either naming scheme."
  echo "::error::LD_PRELOAD of a missing path is silently ignored — the suite would run"
  echo "::error::UNINSTRUMENTED and report green. Refusing to run it."
  exit 1
fi
env ${{ steps.pysan.outputs.san_opts }} LD_PRELOAD="$RT" \
  pytest bindings/python/tests/ -v'

assert_derive_call_site() {
  local json="$1" case_id="$2"

  # Select by exact `name:`, requiring exactly one match.
  local _by_name
  _by_name="$(echo "$json" | jq -c '[ .linux_steps[] ]')"

  step_by_name() {  # <exact name>
    local _n _cnt
    _cnt="$(echo "$_by_name" | jq --arg n "$1" '[ .[] | select(.name == $n) ] | length')"
    if [ "$_cnt" != "1" ]; then
      # `return 1`, NOT `fail`: called inside `$( )`, where `exit 1` kills only the
      # substitution subshell and the pin carries on with an empty result — a fail
      # that does not fail, the very class this file exists to catch. Callers use
      # `|| exit 1` on a SEPARATE assignment line, because `local x=$(...)` masks
      # the substitution status.
      echo "FAIL: $case_id: expected EXACTLY ONE step named '$1' in the linux job, found $_cnt" >&2
      return 1
    fi
    echo "$_by_name" | jq -c --arg n "$1" 'first(.[] | select(.name == $n))'
  }

  # Compare a step's `run:` to its expected block, verbatim modulo trailing
  # newline. On mismatch print a diff — the point of the golden is that a human
  # reads what changed, so the message has to show it.
  assert_run_block() {  # <step-json> <expected> <label>
    local _step="$1" _expected="$2" _label="$3" _got
    _got="$(echo "$_step" | jq -r '.run // ""')"
    # Strip trailing newlines only (YAML block scalars differ in whether they
    # keep one; that difference is not a behavioural one).
    _got="${_got%"${_got##*[!$'\n']}"}"
    _expected="${_expected%"${_expected##*[!$'\n']}"}"
    if [ "$_got" != "$_expected" ]; then
      fail "$case_id: the '$_label' step's run: block does not match the canonical text pinned in this file.

This is a GOLDEN. It reds on ANY change, cosmetic ones included — see the block
comment above EXPECTED_DERIVE_RUN for why the pin no longer tries to interpret
shell. If the change is intended, read what changed and update the expected text
here in the same commit.

--- expected
+++ actual
$(diff <(printf '%s\n' "$_expected") <(printf '%s\n' "$_got") || true)"
    fi
  }

  local derive_step cfg_step none_step san_step
  derive_step="$(step_by_name 'Derive the python sanitizer identity from the preset')" || exit 1
  cfg_step="$(step_by_name 'Configure')"                                              || exit 1
  none_step="$(step_by_name 'Run Python tests')"                                      || exit 1
  san_step="$(step_by_name 'Run Python tests under sanitizer')"                       || exit 1

  assert_run_block "$derive_step" "$EXPECTED_DERIVE_RUN"     "Derive the python sanitizer identity from the preset"
  assert_run_block "$cfg_step"    "$EXPECTED_CONFIGURE_RUN"  "Configure"
  assert_run_block "$none_step"   "$EXPECTED_PYTEST_NONE_RUN" "Run Python tests"
  assert_run_block "$san_step"    "$EXPECTED_PYTEST_SAN_RUN"  "Run Python tests under sanitizer"

  # ⚠️ The step `id:` is what every `steps.<id>.outputs.*` interpolation binds to.
  # It is NOT part of the run: text, so the golden does not cover it. Rename it and
  # all three consumers silently become the empty string while still reading as
  # present.
  local derive_id
  derive_id="$(echo "$derive_step" | jq -r '.id // ""')"
  [ "$derive_id" = "pysan" ] \
    || fail "$case_id: the derive step's id is '$derive_id', expected 'pysan' — every steps.pysan.outputs.* interpolation in this job would resolve to the empty string"

  # ⚠️ `if:` is not part of `run:` either, and a step that cannot run is not a step
  # that does the work. The two pytest guards are pinned EXACTLY (an added conjunct
  # changes which legs run: `false && …` and a `github.event_name` narrowing were
  # both measured passing a `contains()` form). The derive and Configure steps
  # must carry NO guard at all.
  local _d_if _c_if _none_if _san_if
  _d_if="$(echo "$derive_step" | jq -r '.["if"] // ""')"
  _c_if="$(echo "$cfg_step"    | jq -r '.["if"] // ""')"
  [ -z "$_d_if" ] \
    || fail "$case_id: the derive step carries \`if: $_d_if\` — it must be unconditional; a guarded step can silently stop running on some or all legs, and every steps.pysan.outputs.* consumer would then read the empty string"
  [ -z "$_c_if" ] \
    || fail "$case_id: the Configure step carries \`if: $_c_if\` — it must be unconditional"

  _none_if="$(echo "$none_step" | jq -r '.["if"] // ""')"
  _san_if="$(echo "$san_step"   | jq -r '.["if"] // ""')"
  [ "$_none_if" = "steps.pysan.outputs.sanitizer == 'none'" ] \
    || fail "$case_id: the 'Run Python tests' guard is \`$_none_if\`, expected exactly \`steps.pysan.outputs.sanitizer == 'none'\`"
  [ "$_san_if" = "steps.pysan.outputs.sanitizer != 'none'" ] \
    || fail "$case_id: the 'Run Python tests under sanitizer' guard is \`$_san_if\`, expected exactly \`steps.pysan.outputs.sanitizer != 'none'\`"

  # ⚠️ These two are the ONLY steps in the job that may run pytest over the python
  # suite. The golden pins what each of them does; this pins that there is no
  # THIRD one taking some leg down an unpinned path. Together with the exact,
  # mutually exclusive guards and the derive table's proven exhaustiveness over the
  # six matrix presets, that is what makes PG-2 ("every leg runs pytest, exactly
  # once") a measurement rather than a claim.
  local _pytest_n
  _pytest_n="$(echo "$_by_name" | jq '[ .[] | select((.run // "") | contains("pytest bindings/python/tests/")) ] | length')"
  [ "$_pytest_n" = "2" ] \
    || fail "$case_id: $_pytest_n steps in the linux job mention \`pytest bindings/python/tests/\`, expected exactly 2 (the none/non-none pair, both pinned above). Note this counts MENTIONS deliberately — an extra step that merely echoes the command is also a finding here, because the two real ones are already pinned verbatim and anything else is unaccounted for."
  true
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

# ── 6: the ci/ harnesses are actually INVOKED by ci-script-pins ─────────────
# Gate B round 3, Codex finding 6. `ci-script-pins` is the only ungated tier-1
# job, and it is where every ci/ shell harness executes. A harness that passes
# when run but is never run has no durable coverage at all — Article VII §4's
# "no code without a test" is not satisfied by a test nothing calls. Measured:
# replacing `bash ci/test-python-install-witness.sh` with an echo left this pin,
# and the whole job, green while W1/W2/W3/W5 stopped being exercised.
#
# Same invokes-vs-mentions rule as everywhere else: the command must START a
# line, so an echo naming it does not satisfy the census.
CI_PIN_HARNESSES=(
  "ci/test-restore-conan-cache.sh"
  "ci/test-ccache-scripts.sh"
  "ci/test-tier1-python-policy.sh"
  "ci/test-python-install-witness.sh"
)

assert_ci_pin_call_sites() {
  local json="$1" case_id="$2"
  local runs h
  runs="$(echo "$json" | jq -r '.ci_pin_runs // ""')"
  [ -n "$runs" ] || fail "$case_id: the ci-script-pins job has no run: text at all"
  for h in "${CI_PIN_HARNESSES[@]}"; do
    grep -qE "^[[:space:]]*bash ${h//\//\\/}([[:space:]]|\$)" <<<"$runs" \
      || fail "$case_id: ci-script-pins does not INVOKE \`bash $h\` — the harness would never run, so its assertions have no durable coverage (a mention inside an echo does not count)"
  done
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
  assert_ci_pin_call_sites "$json" "$case_id"
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
MUTANTS_DECLARED=15  # M1 M2 M3 B M4 M5 M6 M7 M11 M14 M15 M21 M26 M27 + M28 — DOWN from 27 at
                     # round 3b, because the golden subsumed 14 of them. See the
                     # RETIRED block in run_mutant_checks for the list and the reason.
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
  # ⚠️ RE-POINTED TWICE, and the second time is the interesting one. Round 2 moved
  # the reason from "dead call site" to the exactly-one selector's "found 0";
  # round 3b moved it again, to the golden. M6 is now the GOLDEN-COVERAGE mutant
  # for the derive step: it proves the canonical-block comparison actually reads
  # that step, which is the only thing standing between the pin and every
  # dead-text form the derive call site has been attacked with (`#` comment, `;#`,
  # heredoc, `if false`, same-step echo).
  grep -q "Derive the python sanitizer identity from the preset' step's run: block does not match" "$m6_out" \
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

  # ── RETIRED AT GATE B ROUND 3b, WITH THE REASON ─────────────────────────────
  #
  # Fourteen mutants were DELETED here, not left declared: M8, M9, M10, M12, M13,
  # M16, M17, M18, M19, M20, M22, M23, M24, M25.
  #
  # Every one of them mutated the `run:` text of a step that is now compared
  # VERBATIM against a canonical block (see the EXPECTED_*_RUN section above), so
  # every one of them now reds with the same message — "this block does not match"
  # — and none of them distinguishes anything the four golden-coverage mutants
  # below do not. Keeping twenty mutants that all exercise one string comparison
  # would report coverage this suite does not have, which is the exact failure
  # MUTANTS_DECLARED exists to prevent.
  #
  # They are recorded here rather than silently dropped because each one is a
  # MEASURED false-green from rounds 1-3b, and the reason each is safe to retire
  # is that the golden subsumes it — not that it stopped mattering:
  #
  #   derive step run:      M12 (`#` comment), M24 (`;#`), M25 (same-step echo)
  #   Configure run:        M13 (decoy + flag deleted), M18 (`#` comment),
  #                         M20 (diagnostic echo)
  #   sanitizer pytest run: M8, M9, M10 (dead echoes), M16 (shadowed RT_BASE),
  #                         M17 (*SAN_OPTIONS after the interpolation),
  #                         M19 (decoy prefixed warm-up), M22 (echo under a
  #                         correct env prefix), M23 (dead if-false branch)
  #
  # ⚠️ If the golden is ever weakened back into content matching, these must come
  # back. That is the tripwire: the golden is load-bearing for fourteen recorded
  # false-greens, and the count below drops from 27 to 14 BECAUSE the instrument
  # got stronger, not because coverage was traded away.


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

  # ── M12-M17 — the SELECTOR mutants (Gate B round 2) ───────────────────────
  # Round 1 fixed "presence stands in for position" at the VALUE sites. Round 2
  # measured four live false-greens that all walked through the SELECTOR instead.
  # ⚠️ M13 and M16 are the two that must never be dropped: without M13 the M5
  # false-green is live again against a decoy shape, and without M16 round 1's F5
  # scenario survives a third round.
  #
  # Helper: apply a python literal replacement to $WORKFLOW, then run the pin and
  # require it RED for a reason matching <grep-re>.
  mutate_workflow() {  # <id> <label> <grep-re> <python-src>
    local _id="$1" _label="$2" _why="$3" _py="$4"
    local _f="$mut_dir/tier1-$_id.yml" _o="$mut_dir/${_id}_out"
    python3 - "$WORKFLOW" "$_f" <<<"$_py"
    if cmp -s "$WORKFLOW" "$_f"; then
      fail "$_id: replacement produced no change — mutant not applied"
    fi
    if ( run_full_pin "$_f" "$_id" ) >"$_o" 2>&1; then
      fail "$_id ($_label) did NOT fail the pin"
    fi
    grep -qE "$_why" "$_o" \
      || fail "$_id failed the pin for the WRONG reason: $(cat "$_o")"
    echo "RED (expected): $_id ($_label) — $(cat "$_o")"
    MUTANTS_RUN=$((MUTANTS_RUN + 1))
  }

  # M14 (X3) / M15 (X5): the `if:` guards gain a conjunct. Both CHANGE WHICH LEGS
  # RUN, so both must red — M15 is the plausible one a reviewer would wave past.
  mutate_workflow M14 "decorative always-false guard on both pytest steps" "guard is .*expected exactly" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
for old, new in (
    ("        if: steps.pysan.outputs.sanitizer == \x27none\x27\n",
     "        if: false && steps.pysan.outputs.sanitizer == \x27none\x27\n"),
    ("        if: steps.pysan.outputs.sanitizer != \x27none\x27\n",
     "        if: false && steps.pysan.outputs.sanitizer != \x27none\x27\n")):
    assert t.count(old) == 1, (old, t.count(old))
    t = t.replace(old, new)
open(dst, "w").write(t)
'

  mutate_workflow M15 "plausible event narrowing on a pytest guard" "guard is .*expected exactly" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        if: steps.pysan.outputs.sanitizer != \x27none\x27\n"
new = "        if: github.event_name != \x27pull_request\x27 && steps.pysan.outputs.sanitizer != \x27none\x27\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M21 (X11): the none-guarded step MENTIONS the invocation in an echo and no
  # longer runs it. Every other assertion about that step still holds — the guard
  # is exact, the step exists, the text is present — so this is the one that
  # distinguishes "runs pytest" from "contains the words".
  mutate_workflow M21 "the none-leg pytest invocation replaced by an echo of itself" "Run Python tests. step.s run: block does not match" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        run: pytest bindings/python/tests/ -v\n"
new = "        run: echo \"would run pytest bindings/python/tests/ -v\"\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M26 (Codex round 3, finding 6): the ci-script-pins call site for a harness is
  # replaced by an echo. Without this census a harness can exist, be green when
  # run, and never run — the dead-call-site shape the derive assertion already
  # guards for the derive script.
  mutate_workflow M26 "the install-witness harness call site replaced by an echo" "ci-script-pins does not INVOKE" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        run: bash ci/test-python-install-witness.sh\n"
new = "        run: echo \"bash ci/test-python-install-witness.sh\"\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M27: an `if:` added to the Configure step. NOT covered by the golden — the
  # golden compares `run:`, and a step that cannot run is not a step that does the
  # work. Round 3b raised this for `ci-script-pins`; it applies here too, and this
  # is the shape someone reaches for when trimming PR-side minutes.
  mutate_workflow M27 "a guard added to the unconditional Configure step" "must be unconditional" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
# ⚠️ `      - name: Configure` alone appears in BOTH the linux and coverage jobs,
# so the anchor carries the following comment line, which is unique to linux.
old = "      - name: Configure\n        # FIXPP_ARTIFACT_DIR is a CACHE variable"
new = "      - name: Configure\n        if: github.event_name != \x27pull_request\x27\n        # FIXPP_ARTIFACT_DIR is a CACHE variable"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # ── M28 — a POSITIVE control, and the golden needs one ──────────────────────
  #
  # Every other entry here proves the pin can go RED. This one proves it is not
  # simply a hash of the workflow bytes: the Configure step is a FOLDED (`run: >`)
  # scalar, so re-wrapping it across different line boundaries is a change to the
  # file that YAML folds back to the SAME string. It must stay GREEN.
  #
  # Without this, "compare the block exactly" is indistinguishable from `sha256sum
  # tier1.yml`, and nobody could tell whether the golden pins a VALUE or a file.
  # It pins the value.
  local m28="$mut_dir/tier1-m28.yml"
  local m28_out="$mut_dir/m28_out"
  python3 - "$WORKFLOW" "$m28" <<'PYEOF2'
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = """        run: >
          cmake --preset ${{ matrix.preset }}
          -DFIXPP_ARTIFACT_DIR=${{ github.workspace }}/_artifacts
          -DFIXPP_BUILD_PYTHON=ON
          -DFIXPP_INSTALL_PYTHON=OFF
          -DFIXPP_PYTHON_SANITIZER=${{ steps.pysan.outputs.sanitizer }}
"""
# Same flags, same order, different line breaks. Folded => identical value.
new = """        run: >
          cmake --preset ${{ matrix.preset }} -DFIXPP_ARTIFACT_DIR=${{ github.workspace }}/_artifacts
          -DFIXPP_BUILD_PYTHON=ON -DFIXPP_INSTALL_PYTHON=OFF
          -DFIXPP_PYTHON_SANITIZER=${{ steps.pysan.outputs.sanitizer }}
"""
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
PYEOF2
  if cmp -s "$WORKFLOW" "$m28"; then
    fail "M28: python literal-replace produced no change — the positive control was not applied, so it proves nothing"
  fi
  if ! ( run_full_pin "$m28" "M28" ) >"$m28_out" 2>&1; then
    fail "M28 (semantically identical re-fold of the Configure block) went RED. The golden is comparing FILE TEXT, not the parsed value — a folded scalar re-wrapped at different columns is the same string, and pinning the byte layout instead would make every unrelated edit in the file a false RED. Output: $(cat "$m28_out")"
  fi
  echo "GREEN (expected): M28 (Configure block re-folded, same value) — the golden pins the VALUE, not the file bytes"
  MUTANTS_RUN=$((MUTANTS_RUN + 1))

}

run_mutant_checks

if [ "$MUTANTS_RUN" != "$MUTANTS_DECLARED" ]; then
  fail "mutant count mismatch: $MUTANTS_RUN ran, $MUTANTS_DECLARED declared. A mutant was added, removed or short-circuited without updating MUTANTS_DECLARED — the summary below would otherwise claim coverage this run did not have."
fi

echo "PASS: ci/test-tier1-python-policy.sh — $MUTANTS_RUN/$MUTANTS_DECLARED mutants driven to their expected outcome (14 RED, 1 GREEN positive control), real workflow proven GREEN"
