#!/usr/bin/env bash
# Regression harness for the interop gate step's SHELL, not the Python checker
# it calls (fixpp#431 Gate B r1, Codex #5).
#
# WHY THIS EXISTS. ci/test-interop-skips.sh drives ci/assert-interop-skips.py
# directly; ci/assert-ci-lane-policy.py (ci/test-ci-lane-policy.sh) pins the
# gate step's STATIC shape (leg guard, no continue-on-error, -L interop
# twice, pin-file read, checker-call args). Neither EXECUTES the step's own
# shell — the count derivation, the CR normalisation, and the exactly-once
# schema-check exclusion. Codex #1's CRLF finding is live in exactly that gap:
# a Windows checkout (`core.autocrlf=true`) or native ctest output carrying
# `\r` silently miscounts, and no committed cell before this ran that code at
# all.
#
# HOW. Each cell EXTRACTS the real `run:` text of the "Interop gate" step out
# of the actual workflow file (tier1.yml, byte-identical to tier3-libcxx.yml
# per ci/assert-ci-lane-policy.py's drift check), substitutes the literal
# `${{ matrix.preset }}` value, and executes it with a fake `ctest` on PATH.
# This is the extracted PRODUCTION text, not a hand-copied restatement of it —
# there is nothing here for the step's shell and the tested shell to diverge
# on. The fake `ctest` records its own argv to a file, so a cell that would
# pass no matter what flags reached it (a fake that ignores argv and always
# returns the same canned listing) cannot pass here undetected.
#
# The "derivation" cells (D-*) stop the extracted script right after
# `binaries=` is computed — everything BEFORE the real (GTEST_OUTPUT) ctest
# run — because that is the CR-normalisation and count-arithmetic surface; the
# `E-*` cell runs the FULL, untruncated step to prove the second ctest call is
# itself annotated on failure. The `M-RC1*` cells re-apply the three round-1
# fixes' inverse to already-extracted (and already preset-substituted) text
# and confirm the CRLF/no-op-exclusion defects the fixes close come back.
#
# Run by the `ci-script-pins` job in tier1.yml, and locally with:
#   ci/test-interop-gate-step.sh
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
TIER1="$REPO/.github/workflows/tier1.yml"
TIER2="$REPO/.github/workflows/tier2.yml"
STEP_NAME="Interop gate — ctest -L interop, skip set asserted (#431)"
PRESET="linux-clang-release"
PRESET_TIER2="windows-msvc-release"
BINARIES_MARKER='binaries=$(echo "$names"'

command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }
python3 -c "import yaml" 2>/dev/null \
  || { echo "PyYAML is not importable — install it (pip install pyyaml) before running this pin" >&2; exit 1; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }

# extract_run <out-script> [truncate-marker]
#
# Pulls STEP_NAME's `run:` text out of TIER1, substitutes the literal PRESET
# for `${{ matrix.preset }}`, optionally truncates it to (and including) the
# first line containing <truncate-marker>, and writes it as an executable
# bash script to <out-script>. If <truncate-marker> is the derivation marker,
# appends one line echoing `$binaries` (the truncated script's own last
# computed variable) so the caller can see it — `$binaries` is local to the
# extracted script's own bash process and does not otherwise survive it.
extract_run() {
  extract_run_from "$TIER1" "$PRESET" "$@"
}

# extract_run_from <workflow> <preset-literal> <out-script> [truncate-marker]
#
# Same as extract_run, but over an explicit <workflow>/<preset-literal> pair
# instead of the TIER1/PRESET globals — used to exercise tier2.yml's own
# extracted text directly (tier2 carries `shell: bash`/cygpath/`python`
# AFTER the derivation truncation point, but the derivation-only body up to
# and including `binaries=` is textually identical to tier1/tier3's, so no
# fake cygpath or python is needed to run it).
extract_run_from() {
  local workflow="$1" preset="$2" out="$3" marker="${4:-}"
  python3 - "$workflow" "$STEP_NAME" "$preset" "$out" "$marker" <<'PY'
import sys
import yaml

workflow, step_name, preset, out, marker = sys.argv[1:6]
doc = yaml.safe_load(open(workflow, encoding="utf-8"))
run = None
for job in doc["jobs"].values():
    for step in job.get("steps") or []:
        if step.get("name") == step_name:
            run = step["run"]
            break
    if run is not None:
        break
if run is None:
    sys.exit(f"step {step_name!r} not found in {workflow}")

run = run.replace("${{ matrix.preset }}", preset)

if marker:
    lines = run.splitlines(keepends=True)
    idx = next((i for i, ln in enumerate(lines) if marker in ln), None)
    if idx is None:
        sys.exit(f"truncation marker {marker!r} not found in the extracted run: text")
    run = "".join(lines[:idx + 1])

with open(out, "w", encoding="utf-8") as f:
    f.write("#!/usr/bin/env bash\n")
    f.write(run)
    if marker:
        f.write('echo "DERIVATION_BINARIES=$binaries"\n')
PY
  chmod +x "$out"
}

# mutate_script <script> <python-src>
#
# Applies a textual mutation to an already-extracted script IN PLACE. Works
# line-by-line on STRIPPED content (indentation is not semantically load-
# bearing in bash, so matching on it would be fragile for no reason) and
# must assert the targeted line(s) were found exactly once before mutating —
# a mutant that silently fails to apply cannot be mistaken for one that
# applied and was caught.
mutate_script() {
  local script="$1" py="$2"
  python3 - "$script" <<<"$py"
}

# A fake ctest: records its own argv (one call per line) to
# $FAKE_CTEST_ARGV_LOG, and on a `-N` (registration) call cats
# $FAKE_CTEST_LISTING; any other call exits with $FAKE_CTEST_REAL_EXIT
# (default 0). It does not special-case any other flag — a mutant that
# changes `-L interop` to `-L interopX` is caught by the extracted script
# still passing that flag through to argv, which the caller asserts on
# directly, not by the fake refusing to run.
make_fake_ctest() {
  local bindir="$1"
  mkdir -p "$bindir"
  cat > "$bindir/ctest" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$FAKE_CTEST_ARGV_LOG"
case " $* " in
  *" -N "*) cat "$FAKE_CTEST_LISTING" ;;
  *) exit "${FAKE_CTEST_REAL_EXIT:-0}" ;;
esac
SH
  chmod +x "$bindir/ctest"
}

# ── Fixture listings and pin files ───────────────────────────────────────────
#
# A minimal 3-entry registration: two gtest binaries plus the one non-gtest
# pytest entry (`interop_cell_results_schema_check`) the step must exclude
# exactly once before deriving `binaries` (expected: 2).
LISTING_LF="$WORK/listing-lf.txt"
cat > "$LISTING_LF" <<'EOF'
Test project /fake
    Test #1: interop_alpha_test
    Test #2: interop_beta_test
    Test #3: interop_cell_results_schema_check

Total Tests: 3
EOF

# The CRLF form of the same listing — every line ends `\r\n`, as native
# Windows ctest emits (Codex #1's reproduction, crlf/derive.sh).
LISTING_CRLF="$WORK/listing-crlf.txt"
sed 's/$/\r/' "$LISTING_LF" > "$LISTING_CRLF"

# The schema-check entry replaced by a second ordinary binary — the count
# stays 3, but the exactly-once exclusion now has nothing to remove.
LISTING_NO_SCHEMA="$WORK/listing-no-schema.txt"
sed 's/interop_cell_results_schema_check/interop_gamma_test/' "$LISTING_LF" > "$LISTING_NO_SCHEMA"

PIN_LF="linux-clang-release   3
"
PIN_CRLF=$'linux-clang-release   3\r\n'
PIN_NO_PRESET_LINE="some-other-preset   9
"
# tier2's own preset, for the D-tier2-* cells below.
PIN_LF_TIER2="windows-msvc-release   3
"
PIN_CRLF_TIER2=$'windows-msvc-release   3\r\n'

# run_derivation <label> <listing-file> <pin-content> <want-rc> <frag>
#                [<script-override>] [<argv-fragments, |-separated>]
#
# Extracts (or, if given, uses the caller-supplied) the derivation-only
# script (everything up to and including `binaries=`), runs it with the fake
# ctest against <listing-file> and <pin-content>, and checks the exit code, a
# diagnostic fragment, and — when given — that the fake ctest's recorded argv
# contains every `|`-separated fragment (proving the extracted script still
# passes the real flags through, not merely that some fixed listing was
# returned regardless of them).
run_derivation() {
  local label="$1" listing="$2" pin="$3" want_rc="$4" frag="$5"
  local script_override="${6:-}" argv_frags="${7:-}"
  local celldir="$WORK/cell-$RANDOM$RANDOM"
  local bindir="$celldir-bin"
  mkdir -p "$celldir/ci" "$bindir"
  printf '%s' "$pin" > "$celldir/ci/expected-interop-tests.txt"
  make_fake_ctest "$bindir"
  local argvlog="$celldir.argv"; : > "$argvlog"
  local script="$script_override"
  if [ -z "$script" ]; then
    script="$celldir.sh"
    extract_run "$script" "$BINARIES_MARKER"
  fi
  local out rc=0
  out="$(cd "$celldir" && PATH="$bindir:$PATH" \
           FAKE_CTEST_ARGV_LOG="$argvlog" FAKE_CTEST_LISTING="$listing" \
           bash "$script" 2>&1)" || rc=$?
  if [ "$rc" -ne "$want_rc" ]; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$label — expected exit $want_rc, got $rc"; return
  fi
  if ! printf '%s\n' "$out" | grep -qF -- "$frag"; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$label — exited $rc but WITHOUT '$frag' (failed/passed for the wrong reason)"; return
  fi
  if [ -n "$argv_frags" ]; then
    local f
    local IFS='|'
    for f in $argv_frags; do
      if ! grep -qF -- "$f" "$argvlog"; then
        bad "$label — fake ctest's argv log is missing '$f': $(cat "$argvlog")"; return
      fi
    done
  fi
  ok "$label"
}

echo "== interop gate step — shell derivation witness (fixpp#431) =="

# ── D-a: LF listing, LF pin — the ordinary case, argv proves the real flags
# reached the fake ctest (not merely that SOME listing came back) ───────────
run_derivation "Da LF listing + LF pin derives binaries=2, real preset/label/-N reached ctest" \
  "$LISTING_LF" "$PIN_LF" 0 "DERIVATION_BINARIES=2" "" "--preset $PRESET|-L interop| -N"

# ── D-b: CRLF listing, CRLF pin — Codex #1's cell ────────────────────────────
run_derivation "Db CRLF listing + CRLF pin derives binaries=2 (Codex #1) — CR normalisation held" \
  "$LISTING_CRLF" "$PIN_CRLF" 0 "DERIVATION_BINARIES=2" "" "--preset $PRESET|-L interop| -N"

# ── D-c: schema-check entry missing, an extra binary in its place ───────────
# The exactly-once assertion must catch this BEFORE `binaries` is derived —
# without it, the silent-no-op exclusion would report binaries=3 (#4a).
run_derivation "Dc schema-check entry missing + extra binary is caught before binaries is derived (#4a)" \
  "$LISTING_NO_SCHEMA" "$PIN_LF" 1 \
  "interop_cell_results_schema_check registered 0 time(s) on $PRESET, expected exactly 1"

# ── D-d: the pin file has no line for this preset (pre-existing behaviour,
# never regression-pinned before this harness) ───────────────────────────────
run_derivation "Dd pin file missing this preset's line is caught" \
  "$LISTING_LF" "$PIN_NO_PRESET_LINE" 1 "expected '<no line>'"

# ── D-tier2-b/c: the SAME two properties, re-run against tier2.yml's OWN
# extracted body (windows-msvc-release). Without these, tier2's own copy of
# the CR-normalisation and exactly-once-exclusion fixes was pinned by
# NOTHING: Da/Db/Dc above only ever extract TIER1's text, the tier1==tier3
# identity check in ci/assert-ci-lane-policy.py explicitly exempts tier2,
# and its static checks there don't look for CR-normalisation/exactly-once
# text at all. Reverting either fix in tier2.yml ALONE left every other
# committed check green. The derivation-only body (up to and including
# `binaries=`) needs no fake cygpath/python — those appear only later in
# tier2's real body, after this truncation point.
tier2_script_b="$WORK/tier2-Db.sh"
extract_run_from "$TIER2" "$PRESET_TIER2" "$tier2_script_b" "$BINARIES_MARKER"
run_derivation "D-tier2-b CRLF listing + CRLF pin derives binaries=2 on tier2.yml's own body" \
  "$LISTING_CRLF" "$PIN_CRLF_TIER2" 0 "DERIVATION_BINARIES=2" "$tier2_script_b"

tier2_script_c="$WORK/tier2-Dc.sh"
extract_run_from "$TIER2" "$PRESET_TIER2" "$tier2_script_c" "$BINARIES_MARKER"
run_derivation "D-tier2-c schema-check entry missing is caught on tier2.yml's own body" \
  "$LISTING_NO_SCHEMA" "$PIN_LF_TIER2" 1 \
  "interop_cell_results_schema_check registered 0 time(s) on $PRESET_TIER2, expected exactly 1" \
  "$tier2_script_c"

# ── E: the real (GTEST_OUTPUT) ctest call fails — must be annotated ────────
# Runs the FULL, untruncated step. The fake ctest succeeds on `-N` (so the
# count check passes) and fails on the real run.
run_full() {
  local label="$1" real_exit="$2" want_rc="$3" frag="$4"
  local celldir="$WORK/cell-$RANDOM$RANDOM"
  local bindir="$celldir-bin"
  mkdir -p "$celldir/ci" "$bindir"
  printf '%s' "$PIN_LF" > "$celldir/ci/expected-interop-tests.txt"
  make_fake_ctest "$bindir"
  local argvlog="$celldir.argv"; : > "$argvlog"
  local script="$celldir.sh"
  extract_run "$script"
  local out rc=0
  out="$(cd "$celldir" && PATH="$bindir:$PATH" RUNNER_TEMP="$celldir/runnertemp" \
           FAKE_CTEST_ARGV_LOG="$argvlog" FAKE_CTEST_LISTING="$LISTING_LF" \
           FAKE_CTEST_REAL_EXIT="$real_exit" \
           bash "$script" 2>&1)" || rc=$?
  if [ "$rc" -ne "$want_rc" ]; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$label — expected exit $want_rc, got $rc"; return
  fi
  if ! printf '%s\n' "$out" | grep -qF -- "$frag"; then
    printf '%s\n' "$out" | sed 's/^/  | /'
    bad "$label — exited $rc but WITHOUT '$frag' (failed/passed for the wrong reason)"; return
  fi
  ok "$label"
}
run_full "E ctest failure on the real run is annotated with ::error, not a bare set -e abort" \
  1 1 "::error title=Interop gate::ctest -L interop failed on $PRESET."

# ── M-RC1a/b/c: the round-1 fixes' inverse, applied to already-extracted and
# preset-substituted text, must bring the pre-fix defects back ──────────────
#
# Matching is by STRIPPED line content, not exact indentation — indentation
# is not semantically load-bearing in bash, so pinning it here would be
# fragile for no reason.
run_mutant() {  # <label> <python-mutation> <listing> <pin> <want_rc> <frag>
  local label="$1" py="$2" listing="$3" pin="$4" want_rc="$5" frag="$6"
  local script="$WORK/mutant-$RANDOM$RANDOM.sh"
  extract_run "$script" "$BINARIES_MARKER"
  mutate_script "$script" "$py"
  run_derivation "$label" "$listing" "$pin" "$want_rc" "$frag" "$script"
}

# M-RC1a: remove the ctest-output CR strip. The CRLF listing then leaves `\r`
# in `$n`, mismatching the (still CR-stripped) pin value.
run_mutant "M-RC1a removing the ctest-output CR strip reintroduces the CRLF miscount" '
import sys
p = sys.argv[1]
sq = chr(39)
lines = open(p, encoding="utf-8").read().splitlines(keepends=True)
target = "full=" + chr(34) + "${full//$" + sq + "\\r" + sq + "/}" + chr(34)
hits = [i for i, ln in enumerate(lines) if ln.strip() == target]
assert len(hits) == 1, "MUTATION DID NOT APPLY (found " + str(len(hits)) + ") - re-point the pattern, do not delete the mutant: " + repr(target)
del lines[hits[0]]
open(p, "w", encoding="utf-8").writelines(lines)
' "$LISTING_CRLF" "$PIN_CRLF" 1 "expected '3'"

# M-RC1b: revert the pin-file read to the pre-fix (non-CR-stripped) form. The
# CRLF pin then leaves `\r` in `$expected`, mismatching the (still
# CR-stripped) listing-derived count.
run_mutant "M-RC1b reverting the pin-file CR strip reintroduces the CRLF miscount" '
import sys
p = sys.argv[1]
sq = chr(39)
lines = open(p, encoding="utf-8").read().splitlines(keepends=True)
hits = [i for i, ln in enumerate(lines) if ln.strip().startswith("expected=$(tr -d")]
assert len(hits) == 1, "MUTATION DID NOT APPLY (found " + str(len(hits)) + ") - re-point the pattern, do not delete the mutant"
i = hits[0]
assert "awk" in lines[i + 1], lines[i + 1]
new_line = ("expected=$(awk -v p=" + chr(34) + "linux-clang-release" + chr(34) + " "
            + sq + "$1 == p { print $2; exit }" + sq + " ci/expected-interop-tests.txt)\n")
lines[i:i + 2] = [new_line]
open(p, "w", encoding="utf-8").writelines(lines)
' "$LISTING_CRLF" "$PIN_CRLF" 1 "registered 3 tests"

# M-RC1c: remove the exactly-once schema-check assertion entirely. The
# silent-no-op exclusion (#4a) then derives binaries=3 instead of failing.
run_mutant "M-RC1c removing the exactly-once schema-check assertion reintroduces the #4a no-op" '
import sys
p = sys.argv[1]
lines = open(p, encoding="utf-8").read().splitlines(keepends=True)
hits = [i for i, ln in enumerate(lines) if ln.strip().startswith("schema_hits=$(echo")]
assert len(hits) == 1, f"MUTATION DID NOT APPLY (found {len(hits)}) — re-point the pattern, do not delete the mutant"
i = hits[0]
block = [ln.strip() for ln in lines[i:i + 5]]
assert block[1] == "if [ \"$schema_hits\" != \"1\" ]; then", block
assert block[3] == "exit 1", block
assert block[4] == "fi", block
del lines[i:i + 5]
open(p, "w", encoding="utf-8").writelines(lines)
' "$LISTING_NO_SCHEMA" "$PIN_LF" 0 "DERIVATION_BINARIES=3"

CELLS_DECLARED=10
TOTAL=$((PASS + FAIL))
echo
if [ "$TOTAL" -ne "$CELLS_DECLARED" ]; then
  echo "interop-gate-step harness: EXECUTION COUNT MISMATCH — ran ${TOTAL} cells, declared ${CELLS_DECLARED}."
  echo "A cell was added or lost without updating CELLS_DECLARED. Refusing to report a result."
  exit 1
fi
echo "interop-gate-step harness: ${PASS} passed, ${FAIL} failed (${TOTAL} cells)"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
