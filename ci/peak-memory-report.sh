#!/usr/bin/env bash
# CI-side (#266 / #229 criterion 4): render the peak-memory measurement taken by
# ci/measure-peak-rss.py, and say whether it is ACCEPTANCE EVIDENCE or not.
#
#   ci/peak-memory-report.sh <preset> <peak-env-file> <ctest-log> <test-outcome>
#
#   peak-env-file — the key=value report written by ci/measure-peak-rss.py
#   ctest-log     — the teed ctest output (`--log` of the same invocation)
#   test-outcome  — steps.<id>.outcome ('success' / 'failure' / 'skipped' / '')
#
# NEVER FAILS THE LANE — it is a measurement, not a gate.  But a measurement
# that could not be taken must SAY SO: every path below emits an attributed
# disposition, and there is none on which "no number" can be read as "captured,
# and it was fine"
# (feedback_silent_empty_recurred_three_times_including_inside_its_own_fix).
#
# ── WHAT CHANGED FROM #245's VERSION, AND WHY ────────────────────────────────
#
# #245 read cgroup v2 `memory.peak` inline in tier1.yml.  That source does not
# exist in a GitHub-hosted runner's root cgroup and produced a reading on 0 of 8
# post-merge runs (#266).  The source is now ci/measure-peak-rss.py's sampled
# process-tree sum; see that file for what the number is and is not.
#
# Two further changes, both narrowing what can be claimed:
#
#  1. THE WORKLOAD SIZE IS READ FROM THE RUN, NOT RE-DERIVED.  #245 ran a second
#     `ctest -N -LE packaging` to count eligible tests.  That answers "what would
#     a run now select?", which is not the same question as "what did the run
#     this peak was measured over actually execute?" — they diverge the moment
#     anything between the two invocations changes.  The count now comes out of
#     the ctest log the peak was sampled around, so the figure and its basis are
#     the same event by construction.
#
#  2. THE ACHIEVED CONCURRENCY IS REPORTED.  `execution.jobs` in a preset is an
#     INTENTION.  The ratio of the summed per-test durations to ctest's own
#     `Total Test time (real)` is what actually happened — it is how #229
#     measured this lane's production 1.84x at j=2, and it is the only thing
#     that would catch a widening that silently failed to take effect.
#
# ⚠️ `set -uo pipefail` WITHOUT `-e`: every failure path below is explicitly
# dispositioned, and an unhandled abort here would produce exactly the silent
# nothing this script exists to prevent.  (Also `[ cond ] && x` as a final
# statement returns non-zero under `-e` on the PASSING branch —
# feedback_bracket_and_fail_under_set_e_aborts_on_the_passing_branch.)
set -uo pipefail

PRESET="${1:?usage: peak-memory-report.sh <preset> <peak-env> <ctest-log> <test-outcome>}"
PEAK_ENV="${2:?}"
CTEST_LOG="${3:?}"
TEST_OUTCOME="${4:-unavailable}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPECTED_FILE="${HERE}/expected-eligible-tests.txt"

summary() { [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$@" >> "$GITHUB_STEP_SUMMARY"; return 0; }

# ── The measurement ──────────────────────────────────────────────────────────
#
# Read as DATA, never sourced.  The file records a `command=` line holding the
# wrapped command verbatim; `source`ing it would execute a substring of the
# thing being measured.
kv() { awk -F= -v k="$1" '$1 == k { sub(/^[^=]*=/, ""); print; exit }' "$PEAK_ENV" 2>/dev/null; }

if [ ! -r "$PEAK_ENV" ]; then
  echo "::warning::#266 peak-memory report FAILED for ${PRESET} — the instrument's output file (${PEAK_ENV}) does not exist or is unreadable, so ci/measure-peak-rss.py did not run or could not write. The ctest --parallel memory criterion is UNMEASURED on this lane; do not read this run as evidence."
  summary "### Peak memory (#266) — NOT MEASURED (\`${PRESET}\`)" "" \
          "The instrument produced no output file. The \`--parallel\` widening criterion is still open for this lane."
  exit 0
fi

STATUS="$(kv status)"
PEAK_BYTES="$(kv peak_bytes)"

if [ "${STATUS:-}" != "ok" ] || ! [ "${PEAK_BYTES:-x}" -gt 0 ] 2>/dev/null; then
  echo "::warning::#266 peak-memory report FAILED for ${PRESET} — the instrument reported status='${STATUS:-<absent>}' (peak_bytes='${PEAK_BYTES:-<absent>}', samples='$(kv samples)'). The ctest --parallel memory criterion remains UNMEASURED; do not read this run as evidence."
  summary "### Peak memory (#266) — NOT MEASURED (\`${PRESET}\`)" "" \
          "Instrument status: \`${STATUS:-<absent>}\`. See \`ci/measure-peak-rss.py\`'s fail-loud contract for what each status means."
  exit 0
fi

MEM_TOTAL="$(kv mem_total_bytes)"
PEAK_GIB="$(awk -v b="$PEAK_BYTES" 'BEGIN{printf "%.2f", b/1073741824}')"
SINGLE_GIB="$(awk -v b="$(kv peak_max_single_bytes)" 'BEGIN{printf "%.2f", b/1073741824}')"
TOTAL_GIB="$(awk -v b="${MEM_TOTAL:-0}" 'BEGIN{printf "%.2f", b/1073741824}')"
PCT="n/a"
[ "${MEM_TOTAL:-0}" -gt 0 ] 2>/dev/null && \
  PCT="$(awk -v b="$PEAK_BYTES" -v t="$MEM_TOTAL" 'BEGIN{printf "%.1f%%", b/t*100}')"

# ── The workload, read out of the run the peak was sampled around ────────────
#
# `100% tests passed, 0 tests failed out of 361` — the trailing count is the
# size of the workload whatever the outcome, which is why it is preferred over
# the pass count.  RAN is empty when the line is absent (ctest died before its
# summary), and an empty RAN can match no pin, so that degrades to
# DIAGNOSTIC ONLY rather than to a silent pass.
RAN=""
CTEST_REAL=""
SUM_S=""
if [ -r "$CTEST_LOG" ]; then
  # ANCHORED AT BOTH ENDS of ctest's own summary line, verified against real
  # output: `100% tests passed, 0 tests failed out of 67` — column 0 to
  # end-of-line, no leading or trailing whitespace.
  #
  # ⚠️ Both looser forms were tried and both are defeated by a DECOY, because
  # `--output-on-failure` puts arbitrary test output in this log:
  #   * a bare `out of [0-9]+` reads any assertion message carrying the phrase;
  #   * the whole unanchored sentence `tests passed,.* tests failed out of N`
  #     reads a decoy that quotes ctest's phrasing — which is what a test
  #     asserting ON ctest output would naturally contain.
  # Neither was caught by reading; T14b of ci/test-peak-rss.sh caught both.
  #
  # The last match wins — the summary is printed after the tests it summarises.
  RAN="$(awk 'match($0, /^[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+$/) { m = $0; sub(/.* out of /, "", m); n = m } END { print n }' "$CTEST_LOG")"
  CTEST_REAL="$(awk -F'= *' '/Total Test time \(real\)/ { t = $2 } END { gsub(/[^0-9.]/, "", t); print t }' "$CTEST_LOG")"
  # `  12/361 Test  #7: name ......   Passed    1.42 sec`
  SUM_S="$(awk '/^ *[0-9]+\/[0-9]+ +Test +#[0-9]+/ && match($0, /[0-9.]+ sec$/) { s += substr($0, RSTART, RLENGTH-4) } END { if (s > 0) printf "%.1f", s }' "$CTEST_LOG")"
fi

CONCURRENCY="n/a"
if [ -n "$SUM_S" ] && [ -n "$CTEST_REAL" ]; then
  CONCURRENCY="$(awk -v s="$SUM_S" -v r="$CTEST_REAL" 'BEGIN{ if (r > 0) printf "%.2fx", s/r; else print "n/a" }')"
fi

# ── The pin ──────────────────────────────────────────────────────────────────
EXPECTED="$(awk -v p="$PRESET" '$1 == p { print $2; exit }' "$EXPECTED_FILE" 2>/dev/null)"
EXPECTED="${EXPECTED:-<no line>}"

# ── Sanitizer reports, counted SEPARATELY from ctest's exit code ─────────────
#
# ⚠️ A sanitizer report does not necessarily fail the test that emitted it, so
# "ctest was green" is not "no sanitizer fired".  #229's local sweep counted
# these separately for exactly that reason, and #267's acceptance item 5 treats
# any post-widening report as a real defect until disproven.
#
# ⚠️ FOR UBSan THAT IS NOT A CAVEAT, IT IS THE DEFAULT — MEASURED, not recalled.
# `cmake/Sanitizers.cmake` and `conan/profiles/linux-clang-ubsan` pass
# `-fsanitize=undefined` with NO `-fno-sanitize-recover`, and no `UBSAN_OPTIONS`
# is set anywhere in the repo.  Compiled with those exact flags, a signed-overflow
# probe prints
#     ub.cpp:2:51: runtime error: signed integer overflow: ...
# then CONTINUES and exits 0.  So `linux-clang-ubsan` cannot go red on a UBSan
# finding, and this counter is currently the only thing in CI that would surface
# one.  (ASan differs: `halt_on_error=1` is its default, so an ASan error aborts
# and does redden the lane.)
#
# Source is CTest's own LastTest.log, NOT the step log: the Test step runs with
# `--output-on-failure`, so a report emitted by a test that PASSED never reaches
# stdout.  Counting from the step log would systematically miss the interesting
# case. REPORTED, never asserted — this script does not gate.
#
# ⚠️ A COUNT ALONE IS NOT ACTIONABLE, and shipping one was a defect in this
# script. Run 32003367497 reported `sanitizer reports: 1` on BOTH the asan and
# ubsan lanes, on green runs — and nothing on the page said WHICH line matched,
# so the two candidate readings (a real finding that did not fail its test, vs. a
# `runtime error:` pattern matching ordinary test output) could not be told apart
# without re-running CI. The matched lines are now printed with the count. This
# repo's rule is that a sanitizer finding is a REAL DEFECT UNTIL DISPROVEN;
# disproving one requires seeing it.
SAN_PATTERN='WARNING: ThreadSanitizer:|ERROR: (Address|Leak|Memory)Sanitizer:|runtime error:'
LASTTEST="build/${PRESET}/Testing/Temporary/LastTest.log"
SAN_COUNT="unreadable"
SAN_LINES=""
UNEXPLAINED=""
if [ -r "$LASTTEST" ]; then
  SAN_COUNT="$(grep -cE "$SAN_PATTERN" "$LASTTEST" 2>/dev/null || true)"
  SAN_COUNT="${SAN_COUNT:-0}"
  if [ "$SAN_COUNT" != "0" ]; then
    # Truncated and capped: this goes on a summary page, and a sanitizer report
    # can be followed by a 60-frame stack. The cap is stated in the output so a
    # reader is never left thinking they saw all of them.
    SAN_LINES="$(grep -nE "$SAN_PATTERN" "$LASTTEST" 2>/dev/null | head -5 | cut -c1-200)"

    # ── Expected vs UNEXPLAINED ──────────────────────────────────────────────
    #
    # One lane emits a report BY DESIGN on every run (the ASan canary — see
    # ci/expected-sanitizer-reports.txt). Warning on it every time would make
    # this a permanent false alarm, and a warning that is always on is one
    # nobody reads. So the warning fires on the UNEXPLAINED count.
    #
    # ⚠️ THE LINES ARE PRINTED EITHER WAY. The allowlist decides whether to
    # WARN, never whether to SHOW — nothing here can hide a report.
    UNEXPLAINED="$SAN_COUNT"
    ALLOW_FILE="${HERE}/expected-sanitizer-reports.txt"
    EXPECTED_PATTERN="$(awk -v p="$PRESET" '$1 == p { sub(/^[^ \t]+[ \t]+/, ""); print }' "$ALLOW_FILE" 2>/dev/null | paste -sd'|' -)"
    if [ -n "$EXPECTED_PATTERN" ]; then
      MATCHED_EXPECTED="$(grep -cE "$EXPECTED_PATTERN" "$LASTTEST" 2>/dev/null || true)"
      UNEXPLAINED=$(( SAN_COUNT - ${MATCHED_EXPECTED:-0} ))
      # `if/fi`, not `[ … ] && x` — this file runs under `set -uo pipefail`
      # today so the `&&` form would be harmless, but it is one `set -e` away
      # from aborting on its own passing branch, and that trap has already been
      # paid for once in this repo.
      if [ "$UNEXPLAINED" -lt 0 ]; then
        UNEXPLAINED=0
      fi
    fi

    if [ "$UNEXPLAINED" -gt 0 ]; then
      echo "::warning::#267 acceptance item 5 — ${UNEXPLAINED} UNEXPLAINED sanitizer report(s) (of ${SAN_COUNT} total) in ${PRESET}'s LastTest.log, on a run whose ctest outcome was '${TEST_OUTCOME}'. A sanitizer report does NOT necessarily fail the test that emitted it — on linux-clang-ubsan it structurally CANNOT (see the UBSan note above and #268). Treat as a real defect until disproven; if it is deliberate, add it to ci/expected-sanitizer-reports.txt with the test, the mechanism and the run. Matches (line:text, capped at 5, 200 chars):"
    else
      echo "notice: ${SAN_COUNT} sanitizer report(s) in ${PRESET}'s LastTest.log, all matching ci/expected-sanitizer-reports.txt. Lines below; no unexplained report."
    fi
    printf '%s\n' "$SAN_LINES"
  fi
fi

# ⚠️ THIS LINE CARRIES THE DISCRIMINATING FIELDS, not just the headline.
#
# GitHub exposes no API for a job's step SUMMARY, so this echo is the only
# machine-readable record of the measurement — anything omitted here can only be
# recovered by re-running CI.
#
# `peak_at_s` and `peak_procs` are on it because a whole-run peak has two very
# different causes and the headline number cannot tell them apart. #229 already
# withdrew a finding over exactly this: `linux-clang-debug`'s 0.87 GiB serial
# peak looked like sub-linear memory scaling and was actually ONE transient at
# t=9.3 s — the `consumer::install-witness` test, which stage-installs a prefix
# and drives a nested cmake+ninja build across 12 processes — after which the
# same run never exceeded 0.39 GiB for eleven minutes. A peak at t≈10 s over ~12
# processes is that nested build; a peak at mid-run over `jobs` processes is the
# concurrency figure a widening decision needs. Sizing a widening off the first
# while believing it is the second is the error the field prevents.
echo "peak=${PEAK_BYTES} bytes (${PEAK_GIB} GiB) of ${TOTAL_GIB} GiB — ${PCT} [preset: ${PRESET}] [samples: $(kv samples)] [peak_at: $(kv peak_at_s)s of $(kv elapsed_s)s] [procs_at_peak: $(kv peak_procs)] [largest_single: ${SINGLE_GIB} GiB] [concurrency: ${CONCURRENCY}] [ran: ${RAN:-unknown}/${EXPECTED}] [test outcome: ${TEST_OUTCOME}] [sanitizer reports: ${SAN_COUNT}]"

# EVIDENCE requires all three: the suite succeeded, a basis is recorded for this
# lane, and the run executed exactly that many tests.  `-` (no CI basis yet) can
# never equal a number, so an un-recorded lane degrades to DIAGNOSTIC ONLY.
if [ "$TEST_OUTCOME" = "success" ] && [ -n "$RAN" ] && [ "$RAN" = "$EXPECTED" ]; then
  HEADING="### Peak memory — \`ctest --parallel\` evidence (#266 / #229 criterion 4) — \`${PRESET}\`"
else
  echo "::warning::#266 peak-memory figure for ${PRESET} is DIAGNOSTIC ONLY — Test outcome was '${TEST_OUTCOME}' and/or the executed test count (${RAN:-unknown}) did not match the recorded basis (${EXPECTED}), so this number does NOT discharge the ctest --parallel memory criterion. If only the count differs, that is the designed prompt to re-record the basis in ci/ctest-parallelism-probe.md and update ci/expected-eligible-tests.txt in the same commit."
  HEADING="### Peak memory (#266) — DIAGNOSTIC ONLY — \`${PRESET}\` (outcome: ${TEST_OUTCOME}, ran: ${RAN:-unknown}, expected: ${EXPECTED})"
fi

summary "$HEADING" "" \
  "| metric | value |" \
  "|---|---|" \
  "| peak concurrent RSS (process tree) | **${PEAK_GIB} GiB** |" \
  "| largest single process at peak | ${SINGLE_GIB} GiB |" \
  "| processes at peak | $(kv peak_procs) |" \
  "| runner MemTotal | ${TOTAL_GIB} GiB |" \
  "| utilisation | ${PCT} |" \
  "| samples / interval | $(kv samples) @ $(kv interval_ms) ms |" \
  "| ctest \`Total Test time (real)\` | ${CTEST_REAL:-unknown} s |" \
  "| sum of per-test durations | ${SUM_S:-unknown} s |" \
  "| **achieved concurrency** | **${CONCURRENCY}** |" \
  "| tests executed | ${RAN:-unknown} (basis ${EXPECTED}) |" \
  "| sanitizer reports in LastTest.log | ${SAN_COUNT} (${UNEXPLAINED:-0} unexplained) |" \
  "| Test outcome | ${TEST_OUTCOME} |"

if [ -n "$SAN_LINES" ]; then
  summary "" \
    "<details><summary>${SAN_COUNT} sanitizer report(s) — first 5, truncated</summary>" \
    "" '```' "$SAN_LINES" '```' "" \
    "A sanitizer report does not necessarily fail the test that emitted it, so a" \
    "green lane above is not evidence these are benign. Real defect until disproven." \
    "</details>"
fi

summary "" \
  "Sum of per-process RSS across the whole tree, sampled from \`/proc\`; shared pages" \
  "are counted once per mapping process, so this **over**-states physical occupancy." \
  "Unlike #245's cgroup source it covers the test phase ONLY, not the build — it is a" \
  "measurement of \`ctest\`, not a job-wide ceiling. See \`ci/measure-peak-rss.py\`."

exit 0
