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
# ⚠️ ACCEPTED BRITTLENESS — WAIVED AT GATE B ROUND 2 (finding 6a, P3). DO NOT
# "FIX" THIS BY LOOSENING THE PATTERNS.
#
# The consumer-position assertions match a specific FORM, not just a meaning.
# `RT_BASE="${{ … }}"` on its own line passes; `readonly RT_BASE="${{ … }}"`,
# an unquoted assignment, or a `\`-continued `env` invocation are semantically
# identical and RED. That was measured (round 2, X4) and it is deliberate:
#
#   * a false RED here is LOUD, self-announcing, and one line to fix at the
#     point of trip — the person who reformatted the line is looking at the
#     failure;
#   * loosening the match is a move toward substring tolerance, which is the
#     single mechanism behind every false-green this pin has had. Round 2 found
#     FOUR of them (X1/X2/X3/X5/X6/X7b), all through `contains()` selection or a
#     first-occurrence match. Trading a loud false RED for more of that is a bad
#     trade at any exchange rate.
#
# ⚠️ This is NOT in tension with the EXACT `if:` guard equality below, and a
# future reader must not "reconcile" the two. An added `if:` conjunct CHANGES
# WHICH LEGS RUN and must force a deliberate pin update; `readonly` on an
# assignment changes nothing. Behaviour-changing forms are pinned exactly;
# formatting variants are simply not normalized.
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

  # ── helpers ───────────────────────────────────────────────────────────────
  #
  # ⚠️ A SELECTOR IS AN ASSERTION. Gate B round 2 measured three false-greens
  # that all walked through the previous four-line version of this helper, which
  # used jq `first(... | contains(...))`:
  #   X1  the derive invocation replaced by a `#` COMMENT + `echo dead` — the
  #       comment satisfied the call-site check; the workflow emits no outputs.
  #   X2  a DECOY step before Configure merely echoing the configure line, with
  #       `-DFIXPP_INSTALL_PYTHON=OFF` deleted from the real one — `first()`
  #       picked the decoy. This restored M5, the false-green this pin was
  #       rewritten to close.
  #   X5  `false && steps.pysan.outputs.sanitizer != 'none'` — `contains` on the
  #       `if:` selected a permanently-skipped step.
  # Round 1 fixed this class at four sites and the fifth fix moved it one layer
  # up. Presence standing in for position, at the selector instead of the value.
  #
  # Three properties, each tied to one of those:
  #   (a) EXACTLY ONE match, never the first of several  -> X2
  #   (b) `run` matched on EFFECTIVE COMMANDS — `#` comments and blank lines
  #       stripped before matching                        -> X1
  #   (c) `if:` matched by EXACT normalized equality, not containment -> X5

  # Effective (non-comment, non-blank) command text of each step's `run`, joined.
  # Comments are stripped line-wise; a `#` inside a quoted string is not a comment
  # but stripping one only ever makes this check STRICTER, never laxer.
  #
  # ⚠️ `;` counts as a comment introducer position, not just whitespace. Round 3
  # (Codex finding 1) measured `echo dead >/dev/null ;# ci/derive-python-sanitizer.sh …`
  # selecting the derive step through a shell COMMENT: `;#` has no whitespace
  # before the `#`, so the first draft's `(^|[ \t])#` left the whole line intact.
  # The named capture re-emits the separator so `cmd ;` survives as a command.
  # `${VAR#pattern}` is deliberately NOT touched — no space or `;` precedes it.
  #
  # `run_cmd` additionally joins `\`-continuations, so one LOGICAL COMMAND is one
  # line. Without it a per-line assertion cannot see a whole invocation: the real
  # sanitizer call is `env <opts> LD_PRELOAD=… \` on one physical line and
  # `pytest bindings/python/tests/ -v` on the next, so "the line that runs pytest"
  # and "the line that carries the prefix" are different lines and no per-line
  # check can relate them. That gap is what X9 walked through. This is
  # normalization that makes the check STRICTER — not the kind finding 6a waived.
  local _steps_eff
  _steps_eff="$(echo "$json" | jq -c '
    [ .linux_steps[]
      | . + { run_eff: ( (.run // "")
                | split("\n")
                | map(sub("(?<p>[ \t;]|^)#.*$"; "\(.p)"))
                | map(select(test("[^ \t]")))
                | join("\n") ) }
      | . + { run_cmd: ( .run_eff | gsub("\\\\\n[ \t]*"; " ") ) } ]')"

  # Select the unique step whose <field> contains <literal>; fail on 0 or >1.
  step_where() {  # <field> <literal>
    local _f="$1" _needle="$2" _n
    _n="$(echo "$_steps_eff" | jq --arg f "$_f" --arg needle "$_needle" \
            '[ .[] | select((.[$f] // "") | contains($needle)) ] | length')"
    if [ "$_n" != "1" ]; then
      # ⚠️ `return 1`, NOT `fail`. These helpers are called inside `$( )`, and a
      # `fail` there `exit 1`s only the COMMAND-SUBSTITUTION SUBSHELL — the pin
      # carries on with an empty result. That is a fail that does not fail, the
      # very class this file exists to catch; it surfaced as a doubled FAIL line
      # while fixing round 2. Callers must use `|| exit 1` on a SEPARATE
      # assignment line, because `local x=$(...)` masks the substitution's status.
      echo "FAIL: $case_id: expected EXACTLY ONE step whose '$_f' contains '$_needle', found $_n. Zero means the step is gone; more than one means a decoy or diagnostic can shadow the real step, which is how X2 deleted -DFIXPP_INSTALL_PYTHON=OFF and still passed." >&2
      return 1
    fi
    echo "$_steps_eff" | jq -c --arg f "$_f" --arg needle "$_needle" \
      'first(.[] | select((.[$f] // "") | contains($needle)))'
  }
  # Select the unique step whose `if:` is EXACTLY <expr> after whitespace
  # normalization. Containment would admit `false && <expr>` and
  # `github.event_name != 'pull_request' && <expr>` — both change which legs run.
  step_with_guard() {  # <exact-if-expression>
    local _expr="$1" _n
    _n="$(echo "$_steps_eff" | jq --arg e "$_expr" \
            '[ .[] | select(((.["if"] // "") | gsub("\\s+"; " ") | ltrimstr(" ") | rtrimstr(" ")) == $e) ] | length')"
    if [ "$_n" != "1" ]; then
      # See the note in step_where: `return 1`, never `fail`, inside `$( )`.
      echo "FAIL: $case_id: expected EXACTLY ONE step guarded by \`if: $_expr\` (exact match), found $_n. An added conjunct changes which legs run and must be a deliberate pin update, not something the pin absorbs." >&2
      return 1
    fi
    echo "$_steps_eff" | jq -c --arg e "$_expr" \
      'first(.[] | select(((.["if"] // "") | gsub("\\s+"; " ") | ltrimstr(" ") | rtrimstr(" ")) == $e))'
  }
  step_field() { echo "$1" | jq -r --arg f "$2" '.[$f] // ""'; }

  # ── the call site: exactly one step must actually invoke the script ───────
  local derive_step
  derive_step="$(step_where run_eff 'ci/derive-python-sanitizer.sh "${{ matrix.preset }}"')" || exit 1

  # ⚠️ The step's `id:` is what every `steps.<id>.outputs.*` interpolation binds
  # to. Rename it and all three consumers below silently become the EMPTY
  # STRING while still reading as present.
  local derive_id
  derive_id="$(step_field "$derive_step" id)"
  [ "$derive_id" = "pysan" ] \
    || fail "$case_id: the derive step's id is '$derive_id', expected 'pysan' — every steps.pysan.outputs.* interpolation in this job would resolve to the empty string"

  # ⚠️ INVOKES the script, not MENTIONS it — the same distinction the pytest steps
  # carry. Uniquely selecting a step that CONTAINS the text still admits a
  # diagnostic `echo "running ci/derive-python-sanitizer.sh …"` in that same step
  # with the real invocation deleted; the step would emit no outputs at all and
  # every `steps.pysan.outputs.*` consumer would silently become the empty string.
  # The command must START a logical line.
  grep -qE '^[[:space:]]*ci/derive-python-sanitizer\.sh "\$\{\{ matrix\.preset \}\}"' \
    <<<"$(step_field "$derive_step" run_cmd)" \
    || fail "$case_id: no line of the derive step INVOKES \`ci/derive-python-sanitizer.sh \"\${{ matrix.preset }}\"\` — the text is present but sits inside another command, so the step emits no outputs and every steps.pysan.outputs.* consumer resolves to the empty string"

  # ── `sanitizer`: the Configure argument AND both pytest `if:` guards ───────
  local cfg
  cfg="$(step_where run_eff 'cmake --preset ${{ matrix.preset }}')" || exit 1
  [ -n "$cfg" ] && [ "$cfg" != "null" ] \
    || fail "$case_id: no Configure step (no run: containing 'cmake --preset \${{ matrix.preset }}')"
  # ⚠️ `run_eff`, NOT `run`. Selecting the step on effective commands while
  # ASSERTING against the raw text leaves the whole point of the strip behind —
  # and that is exactly what shipped in the round-2 fix. Measured (X8): delete
  # `-DFIXPP_INSTALL_PYTHON=OFF` from the Configure command and leave it in a `#`
  # comment in the SAME step, and the pin PASSED — announcing
  # "FIXPP_INSTALL_PYTHON=OFF" in its own success line while the six legs would
  # configure without the flag and ship the Python payload in both Release
  # packages. Same class as X1, one layer down, inside the fix for X1. M18.
  # ⚠️ SCOPED TO THE `cmake --preset` COMMAND, not to the step, and not to the
  # job. Three widenings of this same assertion have each been a live false-green:
  #
  #   M5  job-wide  — the install-witness step's `::error::` text quotes the flag,
  #                   so deleting the real flag stayed GREEN.
  #   X8  step-wide, raw `run` — the flag survives as a `#` comment. GREEN.
  #   X10 step-wide, effective — `echo "configuring with -DFIXPP_INSTALL_PYTHON=OFF"`
  #                   above a cmake line that lacks it. GREEN. And this is the MOST
  #                   plausible of the three: a diagnostic echo is exactly what
  #                   caused M5 in a neighbouring step.
  #
  # The command line itself is the only scope that means "this is what configures
  # the build". `run_cmd` joins `\`-continuations, so the whole invocation is one
  # line however it is wrapped.
  local cfg_cmd
  cfg_cmd="$(grep -F 'cmake --preset ${{ matrix.preset }}' <<<"$(step_field "$cfg" run_cmd)" | head -1)"
  [ -n "$cfg_cmd" ] \
    || fail "$case_id: the Configure step has no \`cmake --preset \${{ matrix.preset }}\` command line"

  # `-e` is required: without it grep parses the leading `-D` as its own --devices
  # option and dies, which under `set -e` aborts the pin instead of failing it.
  grep -qF -e '-DFIXPP_PYTHON_SANITIZER=${{ steps.pysan.outputs.sanitizer }}' <<<"$cfg_cmd" \
    || fail "$case_id: the Configure step's cmake command does not pass -DFIXPP_PYTHON_SANITIZER=\${{ steps.pysan.outputs.sanitizer }} — the derived identity is not what configures the build. Line: \`$cfg_cmd\`"

  grep -qF -e '-DFIXPP_INSTALL_PYTHON=OFF' <<<"$cfg_cmd" \
    || fail "$case_id: the linux job's Configure step does not pass -DFIXPP_INSTALL_PYTHON=OFF on its cmake command line — the Python payload would enter packages-linux-{clang,gcc}-release and falsify L-056-4 (#254). Line: \`$cfg_cmd\`"

  # Both pytest steps must BRANCH on the derived value. Substring sniffing
  # (`contains(matrix.preset, 'san')`) is the mapping spelled twice; a literal
  # `if:` is the mapping consumed once.
  local none_step san_step
  none_step="$(step_with_guard "steps.pysan.outputs.sanitizer == 'none'")" || exit 1
  san_step="$(step_with_guard  "steps.pysan.outputs.sanitizer != 'none'")" || exit 1

  # Both guards must be on steps that actually run pytest, or the branch is
  # decorative.
  local none_run san_run
  none_run="$(step_field "$none_step" run_cmd)"
  san_run="$(step_field "$san_step" run_cmd)"
  # ⚠️ INVOKES pytest, not MENTIONS it. `contains("pytest bindings/python/tests/")`
  # is satisfied by `echo "skipping: pytest bindings/python/tests/ -v"` with the
  # real invocation deleted — measured (X11), PASS, and the leg then runs no
  # Python tests at all while both guards, both `if:` exactness checks and the
  # two-step census all still hold.
  #
  # A logical line INVOKES pytest when everything before the command is `env` plus
  # `VAR=value` assignments and `${{ … }}` interpolations — nothing else. That
  # admits the two real forms (bare `pytest …` on the none leg,
  # `env ${{ … }} LD_PRELOAD="$RT" pytest …` on the sanitizer leg) and rejects any
  # line where the text sits inside another command's arguments.
  #
  # ⚠️ The tokens after `env` are restricted DELIBERATELY. A first draft allowed
  # any space-separated tokens there, and `env <opts> echo "… pytest bindings/…"`
  # matched it — the same mention-vs-invocation confusion one token to the right.
  local _pytest_invoke_re='^[[:space:]]*(env[[:space:]]+(([A-Za-z_][A-Za-z0-9_]*=[^[:space:]]*|\$\{\{[^}]*\}\})[[:space:]]+)*)?pytest[[:space:]]+bindings/python/tests/'
  grep -qE "$_pytest_invoke_re" <<<"$none_run" \
    || fail "$case_id: the sanitizer=='none' guarded step does not INVOKE \`pytest bindings/python/tests/\` — the text may appear inside another command (an echo, a comment in a quoted string), but no line of this step runs it"
  grep -qE "$_pytest_invoke_re" <<<"$san_run" \
    || fail "$case_id: the sanitizer!='none' guarded step does not INVOKE \`pytest bindings/python/tests/\` — the text may appear inside another command, but no line of this step runs it"

  # ⚠️ EXACTLY TWO pytest steps in the whole job, and they are these two. This is
  # what gives PG-2 ("every leg runs pytest, exactly once") an instrument instead
  # of an assertion: the two guards are exact and mutually exclusive, the derive
  # table is proven exhaustive over the six matrix presets, and there is no third
  # pytest step to take a leg down an unpinned path.
  #
  # ⚠️ Counted over steps that INVOKE pytest, by the same rule as above — not over
  # steps whose text merely contains the string. A census that counts mentions
  # would be satisfied by two echoes, and it is the census that makes "every leg
  # runs exactly one pytest step" a measurement rather than a claim. jq's `^` is
  # STRING-anchored, not line-anchored, so the per-step test is done in bash;
  # `@base64` carries the embedded newlines across intact.
  local _pytest_n=0 _b64 _rc
  while IFS= read -r _b64; do
    [ -n "$_b64" ] || continue
    _rc="$(printf '%s' "$_b64" | base64 -d)"
    if grep -qE "$_pytest_invoke_re" <<<"$_rc"; then
      _pytest_n=$((_pytest_n + 1))
    fi
  done < <(echo "$_steps_eff" | jq -r '.[].run_cmd | @base64')
  [ "$_pytest_n" = "2" ] \
    || fail "$case_id: the linux job has $_pytest_n steps INVOKING \`pytest bindings/python/tests/\`, expected exactly 2 (the none/non-none pair). A third would run on an unpinned guard; fewer means a leg runs no python tests at all."

  # ── `rt_base`: the RT_BASE assignment INSIDE the sanitizer pytest step ─────
  # Not "appears somewhere". If RT_BASE is hard-coded, the ubsan leg LD_PRELOADs
  # the wrong runtime while the token still exists in a dead echo elsewhere.
  # ⚠️ EXACTLY ONE ASSIGNMENT, and it is the interpolation. Two weaker rules were
  # tried and both were measured GREEN on a workflow that LD_PRELOADs the wrong
  # runtime on the UBSan leg:
  #
  #   "a correct one appears"  — X6: `RT_BASE="${{ … }}"` then `RT_BASE=asan`.
  #   "the LAST one is correct" — Codex round 3, finding 3:
  #        RT_BASE="${{ … }}"
  #        RT_BASE=asan
  #        if false; then RT_BASE="${{ … }}"; fi
  #     The last TEXTUAL assignment is correct and unreachable; the effective value
  #     is `asan`. The same escape is available through an uncalled function, a
  #     subshell, or a non-selected `case` arm.
  #
  # A pin cannot do shell data-flow analysis, and it does not need to: every one of
  # those forms requires a SECOND assignment. Counting them is exact where ordering
  # is guesswork. A legitimate second assignment would be a deliberate change to a
  # load-bearing line and should red — the same rule the `if:` guards follow.
  # `$san_run` is already comment-stripped and continuation-joined.
  local _rt_all _rt_n _rt_last
  _rt_all="$(grep -oE '^[[:space:]]*(readonly[[:space:]]+)?RT_BASE=.*$' <<<"$san_run" || true)"
  _rt_n="$(grep -c . <<<"$_rt_all" || true)"
  [ -z "$_rt_all" ] && _rt_n=0
  [ "$_rt_n" = "1" ] \
    || fail "$case_id: the sanitizer pytest step makes $_rt_n assignments to RT_BASE, expected exactly 1. More than one means the effective value depends on reachability the pin cannot evaluate — a dead \`if false\` branch, an uncalled function or a subshell can leave a correct-looking assignment last while the leg LD_PRELOADs the wrong runtime. Assignments: $(printf '%s' "$_rt_all" | tr '\n' '|')"
  _rt_last="$_rt_all"
  [ -n "$_rt_last" ] \
    || fail "$case_id: the sanitizer pytest step never assigns RT_BASE"
  grep -qE '^[[:space:]]*RT_BASE="\$\{\{ steps\.pysan\.outputs\.rt_base \}\}"[[:space:]]*$' <<<"$_rt_last" \
    || fail "$case_id: the LAST RT_BASE assignment in the sanitizer pytest step is \`$_rt_last\`, not \"\${{ steps.pysan.outputs.rt_base }}\" — the effective value is not the derived one, so the leg LD_PRELOADs the wrong runtime (an earlier correct assignment does not count; the last one wins)"

  # ── `san_opts`: the shell `env` PREFIX on the pytest invocation ────────────
  # ⚠️ The shell `env <opts> cmd` prefix, NOT a YAML `env:` key — this step has a
  # real YAML env: block for PYTHONPATH, and conflating the two turns this back
  # into a presence check. Lose this and a UBSan leg without halt_on_error=1
  # runs, finds, prints, and exits 0.
  # ⚠️ UNIVERSALLY QUANTIFIED over the step's pytest invocations — EVERY one must
  # carry the prefix, not "the first line that looks like one". The previous form
  # took `head -1` of the matching lines, and X9 walked straight through it: a
  # decoy `pytest … --collect-only` warm-up CARRYING the prefix, placed before a
  # real invocation that had LOST it. The pin passed; the UBSan leg would have run
  # its actual test suite with no sanitizer options at all. That is the third
  # consecutive round in which R2-P2-3's named scenario survived, so the check is
  # now over the whole set rather than over a representative of it.
  local _py_lines _l
  _py_lines="$(grep -E 'pytest bindings/python/tests/' <<<"$san_run" || true)"
  [ -n "$_py_lines" ] \
    || fail "$case_id: the sanitizer pytest step runs no \`pytest bindings/python/tests/\` command"
  while IFS= read -r _l; do
    [ -n "$_l" ] || continue
    if ! grep -qE '^[[:space:]]*env \$\{\{ steps\.pysan\.outputs\.san_opts \}\} ' <<<"$_l"; then
      fail "$case_id: a pytest invocation in the sanitizer step is not prefixed with \`env \${{ steps.pysan.outputs.san_opts }}\` — the sanitizer options are not the derived ones, so a leg can lose halt_on_error=1 and still report green (R2-P2-3). Line: \`$_l\`"
    fi
    # ⚠️ `env` takes the LAST assignment of a repeated variable, so a *SAN_OPTIONS
    # re-specified AFTER the interpolation silently overrides everything the derive
    # script emitted — the interpolation stays present and becomes inert (X7b).
    if grep -qE 'san_opts \}\}[^|&;]*[[:space:]](A|UB|T|L)SAN_OPTIONS=' <<<"$_l"; then
      fail "$case_id: a *SAN_OPTIONS assignment follows \${{ steps.pysan.outputs.san_opts }} on the same env invocation — \`env\` takes the LAST assignment, so the derived options are overridden and the interpolation is inert. Line: \`$_l\`"
    fi
  done <<<"$_py_lines"
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
MUTANTS_DECLARED=27  # M1 M2 M3 B M4 M5 M6 M7 M8 M9 M10 M11 M12 M13 M14 M15 M16 M17 M18 M19 M20 M21 M22 M23 M24 M25 M26
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
  # ⚠️ The reason message changed at Gate B round 2: the call site is now proven
  # by the EXACTLY-ONE selector, so a vanished invocation reds with "found 0"
  # rather than the old "dead call site" wording. M6 (invocation replaced) and
  # M12 (invocation commented out) both land here — deliberately: M12's value is
  # that a COMMENT does not count as an invocation, which is what X1 exploited.
  grep -q "expected EXACTLY ONE step whose 'run_eff'" "$m6_out" \
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
  # ⚠️ RE-POINTED when the san_opts check became universally quantified over the
  # step's pytest invocations (X9). M9 still reds, and still for its own defect —
  # but the wording moved from "does not prefix its invocation with" to the
  # per-invocation message. A mutant that reds under a message the check no longer
  # emits is testing nothing it claims to.
  grep -q "is not prefixed with" "$m9_out" \
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
  # Reason message changed at round 2 — the assertion is now on the LAST
  # assignment, so a hard-coded value reds as "the LAST RT_BASE assignment is …".
  grep -q 'LAST RT_BASE assignment' "$m10_out" \
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

  # M12 (X1): the invocation becomes a COMMENT. `id: pysan` intact.
  mutate_workflow M12 "derive invocation commented out" "expected EXACTLY ONE step whose 'run_eff'" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        run: ci/derive-python-sanitizer.sh \"${{ matrix.preset }}\" >> \"$GITHUB_OUTPUT\"\n"
new = ("        run: |\n"
       "          # ci/derive-python-sanitizer.sh \"${{ matrix.preset }}\" >> \"$GITHUB_OUTPUT\"\n"
       "          echo dead >/dev/null\n")
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M13 (X2): a DECOY step echoing the configure line, and the real flag deleted.
  # This is the shape that restored M5's false-green. It must red on AMBIGUITY —
  # two steps matching the Configure selector — not on the missing flag.
  mutate_workflow M13 "decoy Configure step + real flag deleted" "expected EXACTLY ONE step whose 'run_eff'" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
flag = "          -DFIXPP_INSTALL_PYTHON=OFF\n"
assert t.count(flag) == 1, t.count(flag)
t = t.replace(flag, "")
anchor = "      - name: Configure\n        # FIXPP_ARTIFACT_DIR is a CACHE variable"
decoy = ("      - name: Decoy\n"
         "        run: echo \"cmake --preset ${{ matrix.preset }} -DFIXPP_INSTALL_PYTHON=OFF\"\n")
assert t.count(anchor) == 1, t.count(anchor)
open(dst, "w").write(t.replace(anchor, decoy + anchor))
'

  # M14 (X3) / M15 (X5): the `if:` guards gain a conjunct. Both CHANGE WHICH LEGS
  # RUN, so both must red — M15 is the plausible one a reviewer would wave past.
  mutate_workflow M14 "decorative always-false guard on both pytest steps" "guarded by .if: steps.pysan.outputs.sanitizer" '
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

  mutate_workflow M15 "plausible event narrowing on a pytest guard" "guarded by .if: steps.pysan.outputs.sanitizer" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        if: steps.pysan.outputs.sanitizer != \x27none\x27\n"
new = "        if: github.event_name != \x27pull_request\x27 && steps.pysan.outputs.sanitizer != \x27none\x27\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M16 (X6): the correct RT_BASE assignment stays, and is SHADOWED after it.
  # The previous form accepted this — the interpolation was present and inert.
  # ⚠️ RE-POINTED at round 3: the rule moved from "the LAST assignment is correct"
  # to "there is exactly ONE assignment" (Codex finding 3 defeated the former with
  # an unreachable trailing assignment), so M16 now reds on the COUNT. Same defect,
  # different message — a mutant redding under wording the check no longer emits
  # is testing nothing it claims to.
  mutate_workflow M16 "RT_BASE shadowed after a verbatim-correct assignment" "assignments to RT_BASE, expected exactly 1" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "          RT_BASE=\"${{ steps.pysan.outputs.rt_base }}\"\n"
new = old + "          RT_BASE=asan\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M17 (X7b): *SAN_OPTIONS re-specified AFTER the interpolation. `env` takes the
  # last assignment, so the derived options — including halt_on_error=1 — are
  # overridden while the interpolation remains present.
  mutate_workflow M17 "UBSAN_OPTIONS re-specified after the san_opts prefix" "takes the LAST assignment" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "          env ${{ steps.pysan.outputs.san_opts }} LD_PRELOAD=\"$RT\" \\\n"
new = "          env ${{ steps.pysan.outputs.san_opts }} UBSAN_OPTIONS=print_stacktrace=1 LD_PRELOAD=\"$RT\" \\\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M18 (X8): the Configure step keeps `cmake --preset ${{ matrix.preset }}` — so
  # the selector still finds it, uniquely — but the real `-DFIXPP_INSTALL_PYTHON=OFF`
  # is DELETED from the command and survives only as a `#` comment in the same
  # step. Found by the orchestrator between rounds 2 and 3, and it was live: the
  # step selector used `run_eff` while the flag assertions read the RAW `run`.
  #
  # ⚠️ This must red on the MISSING FLAG, not on ambiguity (M13's reason) and not
  # on the sanitizer flag (which the mutant preserves). The reason-grep below is
  # deliberately the OFF clause's own wording.
  mutate_workflow M18 "the install flag survives only as a comment in the Configure step" "does not pass -DFIXPP_INSTALL_PYTHON=OFF" '
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
new = """        run: |
          # -DFIXPP_INSTALL_PYTHON=OFF temporarily dropped while chasing a wheel issue
          cmake --preset ${{ matrix.preset }} \\
            -DFIXPP_ARTIFACT_DIR=${{ github.workspace }}/_artifacts \\
            -DFIXPP_BUILD_PYTHON=ON \\
            -DFIXPP_PYTHON_SANITIZER=${{ steps.pysan.outputs.sanitizer }}
"""
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M19 (X9): a DECOY pytest invocation that CARRIES the prefix, placed before the
  # real one, which LOSES it. The old assertion took `head -1` of the lines that
  # looked like the invocation, so the decoy satisfied it while the actual test
  # suite ran with no sanitizer options at all — no halt_on_error=1, no
  # suppressions, on every sanitizer leg.
  #
  # ⚠️ The mutant deliberately keeps the interpolation PRESENT and correct. If it
  # ever starts redding with "runs no `pytest ...` command" or with M9's message,
  # it has degenerated and is no longer covering the quantifier.
  mutate_workflow M19 "decoy prefixed pytest warm-up, real invocation unprefixed" "is not prefixed with" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = """          env ${{ steps.pysan.outputs.san_opts }} LD_PRELOAD="$RT" \\
            pytest bindings/python/tests/ -v
"""
new = """          env ${{ steps.pysan.outputs.san_opts }} LD_PRELOAD="$RT" \\
            pytest bindings/python/tests/ --collect-only -q >/dev/null
          LD_PRELOAD="$RT" \\
            pytest bindings/python/tests/ -v
"""
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M20 (X10): the flag survives as a diagnostic ECHO above a cmake line that
  # lacks it. The most plausible of the three widenings this assertion has had —
  # M5 was caused by exactly this shape in a neighbouring step.
  mutate_workflow M20 "the install flag survives only in a diagnostic echo" "does not pass -DFIXPP_INSTALL_PYTHON=OFF on its cmake command line" '
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
new = """        run: |
          echo "configuring with -DFIXPP_INSTALL_PYTHON=OFF"
          cmake --preset ${{ matrix.preset }} \\
            -DFIXPP_ARTIFACT_DIR=${{ github.workspace }}/_artifacts \\
            -DFIXPP_BUILD_PYTHON=ON \\
            -DFIXPP_PYTHON_SANITIZER=${{ steps.pysan.outputs.sanitizer }}
"""
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M21 (X11): the none-guarded step MENTIONS the invocation in an echo and no
  # longer runs it. Every other assertion about that step still holds — the guard
  # is exact, the step exists, the text is present — so this is the one that
  # distinguishes "runs pytest" from "contains the words".
  mutate_workflow M21 "the none-leg pytest invocation replaced by an echo of itself" "does not INVOKE" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        run: pytest bindings/python/tests/ -v\n"
new = "        run: echo \"would run pytest bindings/python/tests/ -v\"\n"
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M22: the sanitizer step keeps a correct, complete `env <derived opts>` prefix
  # and runs `echo` under it instead of pytest. This is the mention-vs-invocation
  # confusion ONE TOKEN TO THE RIGHT of M21, and the first draft of the
  # invocation regex admitted it — the prefix tokens were unrestricted, so `echo`
  # was accepted as just another one. It pins the restriction, not the anchor.
  mutate_workflow M22 "pytest replaced by echo UNDER a correct env prefix" "does not INVOKE" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = """          env ${{ steps.pysan.outputs.san_opts }} LD_PRELOAD="$RT" \\
            pytest bindings/python/tests/ -v
"""
new = """          env ${{ steps.pysan.outputs.san_opts }} LD_PRELOAD="$RT" \\
            echo "would run pytest bindings/python/tests/ -v"
"""
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M23 (Codex round 3, finding 3): the last TEXTUAL RT_BASE assignment is correct
  # and UNREACHABLE. "Last one wins" is true of execution, not of text.
  mutate_workflow M23 "RT_BASE overridden, then re-assigned inside a dead if-false branch" "assignments to RT_BASE, expected exactly 1" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "          RT_BASE=\"${{ steps.pysan.outputs.rt_base }}\"\n"
new = ("          RT_BASE=\"${{ steps.pysan.outputs.rt_base }}\"\n"
       "          RT_BASE=asan\n"
       "          if false; then\n"
       "            RT_BASE=\"${{ steps.pysan.outputs.rt_base }}\"\n"
       "          fi\n")
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M24 (Codex round 3, finding 1): `;#` — a shell comment with NO whitespace
  # before the `#`. The first comment-stripper required whitespace, so the whole
  # line survived and selected the derive step through dead text.
  mutate_workflow M24 "derive invocation behind a ;# comment" "expected EXACTLY ONE step whose .run_eff." '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        run: ci/derive-python-sanitizer.sh \"${{ matrix.preset }}\" >> \"$GITHUB_OUTPUT\"\n"
new = ("        run: |\n"
       "          echo dead >/dev/null ;# ci/derive-python-sanitizer.sh \"${{ matrix.preset }}\" >> \"$GITHUB_OUTPUT\"\n")
assert t.count(old) == 1, t.count(old)
open(dst, "w").write(t.replace(old, new))
'

  # M25: a SAME-STEP diagnostic echo quoting the invocation, with the real command
  # deleted. Selection still finds exactly one step, so only the invokes-vs-mentions
  # rule can catch it. The step would emit no outputs and every
  # steps.pysan.outputs.* consumer would resolve to the empty string.
  mutate_workflow M25 "derive invocation replaced by an echo of itself in the same step" "the text is present but sits inside another command" '
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "        run: ci/derive-python-sanitizer.sh \"${{ matrix.preset }}\" >> \"$GITHUB_OUTPUT\"\n"
new = ("        run: |\n"
       "          echo running ci/derive-python-sanitizer.sh \"${{ matrix.preset }}\" >> \"$GITHUB_OUTPUT\"\n")
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

}

run_mutant_checks

if [ "$MUTANTS_RUN" != "$MUTANTS_DECLARED" ]; then
  fail "mutant count mismatch: $MUTANTS_RUN ran, $MUTANTS_DECLARED declared. A mutant was added, removed or short-circuited without updating MUTANTS_DECLARED — the summary below would otherwise claim coverage this run did not have."
fi

echo "PASS: ci/test-tier1-python-policy.sh — $MUTANTS_RUN/$MUTANTS_DECLARED mutants proven RED, real workflow proven GREEN"
