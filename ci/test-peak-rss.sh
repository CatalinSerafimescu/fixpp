#!/usr/bin/env bash
# Regression harness for the #266 peak-memory instrument:
#   ci/measure-peak-rss.py   — the sampler + command wrapper
#   ci/peak-memory-report.sh — the renderer + acceptance disposition
#
# Run by the `ci-script-pins` job in tier1.yml, and locally with:
#   ci/test-peak-rss.sh
#
# ── WHY THIS FILE EXISTS AT ALL ──────────────────────────────────────────────
#
# #266 is a bug report about an instrument that ran 8 times and measured nothing
# while exiting 0 every time.  Replacing it with a second unproven instrument
# would repeat the defect at a new source.  So every claim the new instrument
# makes is pinned here by a case that is shown to FAIL when the claim is false —
# not merely by a case that passes.
#
# ⚠️ THE DISCRIMINATIONS, not just the assertions.  Three of the checks below
# exist because the OBVIOUS wrong implementation passes a naive test:
#
#   * A sampler that reads only the root process's RSS still emits a plausible
#     number.  T2 pins the concurrent SUM against the largest SINGLE on a
#     fixture whose ratio is known by construction — which is precisely the
#     `/usr/bin/time -v` failure mode #266 rejects, made falsifiable.
#   * A sampler keyed on process GROUP still works for ordinary children.  T4
#     puts a child in a new session and pins that it is still counted.
#   * A `grep` for sanitizer reports that points at the wrong file reads 0, and
#     0 reads as "clean".  T16 proves the counter NON-ZERO on a log that contains
#     reports before T17 trusts a 0 (feedback_verification_grep_must_be_proven_
#     nonzero_on_the_unfixed_tree).
#
# The MUTANT section at the bottom closes the loop: every cell named above is
# re-run against a copy of the instrument that breaks exactly the property it
# claims to pin, and the harness must go RED at that cell.
#
# ⚠️ IF A MUTANT REPORTS `the mutation did NOT apply`, THE FIX IS TO RE-POINT THE
# PATTERN, not to delete the mutant. The mutations are `sed` expressions matching
# exact lines of the two scripts — including indentation, and for two of them the
# report script's awk regex verbatim — so any edit to a targeted line breaks its
# pattern. That is the cmp-guard doing its job: it has already fired twice during
# this file's own development (once after `run()` was re-indented by a try/except,
# once after the workload-size regex was tightened). A silently non-applying
# mutation would read as a thorough harness that tested nothing.
#
# No build tree and no ctest are required: every fixture is synthetic, because
# `ci-script-pins` runs on a bare runner in ~2.5 s.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Overridable so the MUTANT section at the bottom can re-invoke this same file
# against deliberately broken copies. Default is the shipped pair.
MEASURE="${PEAK_RSS_MEASURE:-${HERE}/measure-peak-rss.py}"
REPORT="${PEAK_RSS_REPORT:-${HERE}/peak-memory-report.sh}"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
check(){ if [ "$2" = "$3" ]; then ok "$1 ($2)"; else bad "$1 — expected '$3', got '$2'"; fi; }

kv() { awk -F= -v k="$1" '$1 == k { sub(/^[^=]*=/, ""); print; exit }' "$2"; }

# A workload that touches N MiB and holds it, so the pages are RESIDENT and not
# merely reserved — RSS is what the instrument reads.
cat > "$WORK/hog.py" <<'PY'
import sys, time
buf = bytearray(int(sys.argv[1]) * 1024 * 1024)
for i in range(0, len(buf), 4096):
    buf[i] = 1
time.sleep(float(sys.argv[2]))
PY

echo "== measure-peak-rss.py =="

# ── T1: it measures at all, and the number is not zero ───────────────────────
python3 "$MEASURE" --out "$WORK/t1.env" -- python3 "$WORK/hog.py" 200 1 >/dev/null 2>&1
check "T1 status is ok on a real workload" "$(kv status "$WORK/t1.env")" "ok"
T1_PEAK="$(kv peak_bytes "$WORK/t1.env")"
if [ "${T1_PEAK:-0}" -gt 100000000 ]; then ok "T1 peak is a real figure ($T1_PEAK bytes > 100 MB)"
else bad "T1 peak is implausibly small ($T1_PEAK bytes) — the sampler read nothing"; fi

# ── T2: the CONCURRENT SUM, not the largest single child ─────────────────────
#
# THE discrimination of this whole issue.  Three 200 MiB children at once: the
# tree sum must be ~3x, the largest single ~1x.  An instrument reporting
# getrusage(RUSAGE_CHILDREN)-style max-single passes T1 and fails here.
cat > "$WORK/spawn3.sh" <<PY
#!/bin/bash
python3 "$WORK/hog.py" 200 2 &
python3 "$WORK/hog.py" 200 2 &
python3 "$WORK/hog.py" 200 2 &
wait
PY
chmod +x "$WORK/spawn3.sh"
python3 "$MEASURE" --out "$WORK/t2.env" -- "$WORK/spawn3.sh" >/dev/null 2>&1
T2_PEAK="$(kv peak_bytes "$WORK/t2.env")"
T2_SINGLE="$(kv peak_max_single_bytes "$WORK/t2.env")"
T2_PROCS="$(kv peak_procs "$WORK/t2.env")"
if [ "${T2_SINGLE:-0}" -gt 0 ] && [ "$((T2_PEAK * 10 / T2_SINGLE))" -ge 25 ]; then
  ok "T2 sums the tree, not the largest child (peak/single = $((T2_PEAK * 10 / T2_SINGLE))/10, procs=$T2_PROCS)"
else
  bad "T2 peak ($T2_PEAK) is not >=2.5x the largest single ($T2_SINGLE) — the sampler is measuring ONE process, which is the /usr/bin/time -v failure mode #266 rejects"
fi

# ── T2b: the peak is a PER-INSTANT SUM, and the arithmetic proves it ─────────
#
# A sum over N processes at one instant CANNOT exceed N x the largest of them.
# The three fields are recorded at the SAME sample, so a correct implementation
# satisfies this by construction — while any implementation that accumulates
# across samples violates it after the second sample and keeps going.
#
# ⚠️ This cell exists because the seq-vs-concurrent ratio (T3) does NOT catch
# accumulation: accumulation inflates both arms roughly in proportion, so the
# ratio survives. That was found by running the mutant, not by reading the code.
if [ "${T2_PROCS:-0}" -gt 0 ] && [ "$T2_PEAK" -le "$((T2_PROCS * T2_SINGLE))" ]; then
  ok "T2b peak <= procs x largest single ($T2_PEAK <= $T2_PROCS x $T2_SINGLE) — a per-instant sum"
else
  bad "T2b peak ($T2_PEAK) exceeds procs x largest single ($T2_PROCS x $T2_SINGLE) — arithmetically impossible for a sum taken at one instant, so the sampler is accumulating across samples rather than taking a maximum"
fi

# ── T3: SEQUENTIAL must NOT read like concurrent ─────────────────────────────
#
# The mirror of T2, and it pins a DIFFERENT property: liveness. T2 is satisfied
# by a sampler that sums every process it has ever SEEN, dead ones included —
# such a sampler reports ~3x here too, where only one 200 MiB process is ever
# alive at a time. This cell is what separates "concurrently resident" from
# "cumulatively observed".
#
# ⚠️ It does NOT catch accumulation across samples: that inflates both arms in
# rough proportion and the ratio survives. T2b is the cell that catches it, and
# that division of labour was established by running the mutant, not by reading
# the code.
cat > "$WORK/seq3.sh" <<PY
#!/bin/bash
python3 "$WORK/hog.py" 200 1
python3 "$WORK/hog.py" 200 1
python3 "$WORK/hog.py" 200 1
PY
chmod +x "$WORK/seq3.sh"
python3 "$MEASURE" --out "$WORK/t3.env" -- "$WORK/seq3.sh" >/dev/null 2>&1
T3_PEAK="$(kv peak_bytes "$WORK/t3.env")"
if [ "$((T3_PEAK * 10 / T2_PEAK))" -le 6 ]; then
  ok "T3 a sequential run reads ~1x, not ~3x (seq/concurrent = $((T3_PEAK * 10 / T2_PEAK))/10)"
else
  bad "T3 sequential peak ($T3_PEAK) is not materially below the concurrent peak ($T2_PEAK) — the sampler is accumulating across samples rather than taking a per-instant maximum"
fi

# ── T4: a child that LEAVES THE PROCESS GROUP is still counted ───────────────
cat > "$WORK/detach.sh" <<PY
#!/bin/bash
setsid python3 "$WORK/hog.py" 250 2 &
sleep 2.5
PY
chmod +x "$WORK/detach.sh"
python3 "$MEASURE" --out "$WORK/t4.env" -- "$WORK/detach.sh" >/dev/null 2>&1
T4_PEAK="$(kv peak_bytes "$WORK/t4.env")"
if [ "${T4_PEAK:-0}" -gt 200000000 ]; then
  ok "T4 a setsid'd descendant is still counted ($T4_PEAK bytes)"
else
  bad "T4 peak ($T4_PEAK) missed the setsid'd 250 MiB child — tree membership is keyed on the process GROUP, which CTest and the codegen tests break"
fi

# ── T5: the wrapped command's exit status is the wrapper's ───────────────────
#
# An instrument that swallowed a non-zero status would turn a red suite green,
# which is a strictly worse defect than the one #266 reports.
python3 "$MEASURE" --out "$WORK/t5.env" -- bash -c 'exit 7' >/dev/null 2>&1
check "T5 exit status propagates" "$?" "7"
check "T5 records the status it propagated" "$(kv cmd_status "$WORK/t5.env")" "7"

# ── T6: an unrunnable command is a LOUD instrument failure, not a number ─────
T6_ERR="$(python3 "$MEASURE" --out "$WORK/t6.env" -- /nonexistent/binary 2>&1 >/dev/null)"
check "T6 start-failed is recorded" "$(kv status "$WORK/t6.env")" "start-failed"
case "$T6_ERR" in *"::error::"*) ok "T6 emits an attributed ::error::";;
                  *) bad "T6 emitted no ::error:: — a silent instrument failure is the #266 defect";; esac

# ── T7: the teed log is byte-faithful ────────────────────────────────────────
#
# ci/peak-memory-report.sh parses ctest's own timing lines out of this log, so
# any reformatting silently breaks the concurrency figure.
python3 "$MEASURE" --out "$WORK/t7.env" --log "$WORK/t7.log" \
  -- bash -c 'echo "  12/361 Test  #7: demo ...   Passed    1.42 sec"; echo err >&2' >/dev/null 2>&1
check "T7 log captures stdout verbatim" \
  "$(head -1 "$WORK/t7.log")" "  12/361 Test  #7: demo ...   Passed    1.42 sec"
check "T7 log captures stderr too" "$(grep -c '^err$' "$WORK/t7.log")" "1"

echo "== peak-memory-report.sh =="

# A ctest log fixture whose numbers are known by construction:
#   sum of per-test durations = 10 + 20 + 30 = 60 s;  real = 30 s  =>  2.00x
mk_log() {
  cat > "$1" <<'LOG'
    1/3 Test  #1: alpha ...........................   Passed   10.00 sec
    2/3 Test  #2: beta ............................   Passed   20.00 sec
    3/3 Test  #3: gamma ...........................   Passed   30.00 sec

100% tests passed, 0 tests failed out of 3

Total Test time (real) =  30.00 sec
LOG
}
mk_log "$WORK/ctest.log"
cp "$WORK/t1.env" "$WORK/good.env"

# The report resolves LastTest.log relative to cwd as build/<preset>/..., so the
# fixtures live in a throwaway tree under a preset name no lane uses.
FAKE=fake-preset
mkdir -p "$WORK/tree/build/$FAKE/Testing/Temporary"
printf 'nothing interesting here\n' > "$WORK/tree/build/$FAKE/Testing/Temporary/LastTest.log"

run_report() {  # <outcome> <peak-env> <log>  -> stdout+stderr, summary in $WORK/sum.md
  rm -f "$WORK/sum.md"; : > "$WORK/sum.md"
  ( cd "$WORK/tree" && GITHUB_STEP_SUMMARY="$WORK/sum.md" "$REPORT" "$FAKE" "$2" "$3" "$1" 2>&1 )
}

# `fake-preset` has no line in ci/expected-eligible-tests.txt, so EXPECTED is
# `<no line>`, which no count can equal — the un-recorded-lane path.
OUT="$(run_report success "$WORK/good.env" "$WORK/ctest.log")"
case "$OUT" in *"DIAGNOSTIC ONLY"*) ok "T8 a lane with no recorded basis is NOT evidence";;
               *) bad "T8 a lane with no recorded basis was labelled evidence: $OUT";; esac

# ── T8b: the log line carries the fields that discriminate a TRANSIENT ───────
#
# GitHub exposes no API for a step summary, so this echo is the only
# machine-readable record — a field missing here is recoverable only by
# re-running CI. `peak_at`/`procs_at_peak` are what separate "one nested-build
# transient at t~10 s" from "the concurrency peak", the confusion #229 already
# withdrew a finding over.
for field in "peak_at:" "procs_at_peak:" "largest_single:" "concurrency:" "samples:"; do
  case "$OUT" in *"$field"*) ok "T8b the log line carries ${field}";;
                 *) bad "T8b the log line omits ${field}, and the step summary is not API-readable, so it could only be recovered by re-running CI: $OUT";; esac
done

# ── T9: the parsed figures ───────────────────────────────────────────────────
case "$OUT" in *"concurrency: 2.00x"*) ok "T9 achieved concurrency parsed (2.00x from 60 s over 30 s)";;
               *) bad "T9 concurrency not parsed from the ctest log: $OUT";; esac
case "$OUT" in *"ran: 3/"*) ok "T9 executed-test count parsed from the run (3)";;
               *) bad "T9 executed-test count not parsed: $OUT";; esac

# ── T10: NOT MEASURED on a missing / failed instrument, and never a number ───
OUT="$(run_report success "$WORK/absent.env" "$WORK/ctest.log")"
SUM="$(cat "$WORK/sum.md")"
case "$SUM" in *"NOT MEASURED"*) ok "T10 a missing instrument file renders NOT MEASURED on the summary page";;
               *) bad "T10 a missing instrument file did not render NOT MEASURED: $SUM";; esac
case "$OUT" in *"::warning::"*) ok "T10 emits an attributed ::warning::";;
               *) bad "T10 emitted no warning for a missing measurement";; esac
case "$SUM" in *GiB*) bad "T10 the summary carries a memory figure for a measurement that was never taken: $SUM";;
               *) ok "T10 no number is rendered when none was measured";; esac

sed 's/^status=ok$/status=zero-peak/; s/^peak_bytes=.*/peak_bytes=0/' "$WORK/good.env" > "$WORK/zero.env"
OUT="$(run_report success "$WORK/zero.env" "$WORK/ctest.log")"
SUM="$(cat "$WORK/sum.md")"
case "$SUM" in *"NOT MEASURED"*) ok "T11 a zero-peak measurement renders NOT MEASURED";;
               *) bad "T11 a zero peak was rendered as a measurement: $SUM";; esac
case "$OUT" in *"::warning::"*) ok "T11 emits an attributed ::warning::";;
               *) bad "T11 emitted no warning for a zero-peak measurement";; esac

echo "== the acceptance disposition =="

# From here the pin is exercised for real, against a preset line this harness
# adds to a COPY of the pin file so the shipped one is never mutated.
PINNED_DIR="$WORK/pinned"; mkdir -p "$PINNED_DIR"
cp "$MEASURE" "$REPORT" "$PINNED_DIR/"
{ cat "$HERE/expected-eligible-tests.txt"; printf '%s %s\n' "$FAKE" 3; } > "$PINNED_DIR/expected-eligible-tests.txt"

run_pinned() {
  rm -f "$WORK/sum.md"
  ( cd "$WORK/tree" && GITHUB_STEP_SUMMARY="$WORK/sum.md" \
      "$PINNED_DIR/peak-memory-report.sh" "$FAKE" "$2" "$3" "$1" 2>&1 )
}

# ── T12: everything lines up => EVIDENCE ─────────────────────────────────────
OUT="$(run_pinned success "$WORK/good.env" "$WORK/ctest.log")"
case "$OUT" in *"DIAGNOSTIC ONLY"*|*"NOT MEASURED"*)
      bad "T12 a well-formed run was NOT labelled evidence — the acceptance path is unreachable, so no run can ever close criterion 4: $OUT";;
   *) case "$(cat "$WORK/sum.md")" in *"evidence (#266"*) ok "T12 a well-formed run IS labelled evidence";;
        *) bad "T12 summary carries no evidence heading";; esac;;
esac

# ── T13: a FAILED suite is never evidence ────────────────────────────────────
OUT="$(run_pinned failure "$WORK/good.env" "$WORK/ctest.log")"
case "$OUT" in *"DIAGNOSTIC ONLY"*) ok "T13 a failed Test step is not evidence";;
               *) bad "T13 a failed Test step was labelled evidence: $OUT";; esac

# ── T14: a workload of the WRONG SIZE is never evidence ──────────────────────
sed 's/out of 3$/out of 2/' "$WORK/ctest.log" > "$WORK/short.log"
OUT="$(run_pinned success "$WORK/good.env" "$WORK/short.log")"
case "$OUT" in *"DIAGNOSTIC ONLY"*) ok "T14 a count mismatch is not evidence (2 against a basis of 3)";;
               *) bad "T14 a run that executed FEWER tests than the basis was labelled evidence — the vacuity guard is dead: $OUT";; esac

# ── T14b: a KILLED ctest cannot be rescued by a decoy in a test's output ─────
#
# The reachable shape, and it ends in a FALSE ACCEPTANCE rather than a wrong
# number. `--output-on-failure` puts arbitrary test output in this log; if ctest
# is then killed (OOM, job timeout — the `exit 143` this repo has already
# mistaken for a flake) it never prints its summary. A scan for a bare
# `out of N` anywhere in the log picks the phrase out of a test's own assertion
# message, and if that number happens to equal the lane's pin the run is labelled
# acceptance evidence for a suite that never finished.
#
# ⚠️ The decoy must be in a log with NO summary. An earlier draft put it BEFORE
# a valid summary, where "last match wins" already protects the scan — the cell
# passed under the broken implementation too, and the mutant proved it useless.
printf 'ASSERT FAILED: 2 tests passed, 1 tests failed out of 3 in the widget suite\n' \
  > "$WORK/decoy.log"
printf 'Killed\n' >> "$WORK/decoy.log"
OUT="$(run_pinned success "$WORK/good.env" "$WORK/decoy.log")"
case "$OUT" in *"DIAGNOSTIC ONLY"*) ok "T14b a killed ctest is not evidence, decoy or no decoy";;
               *) bad "T14b a ctest that never printed a summary was labelled evidence off a decoy in test output: $OUT";; esac

# ── T15: a ctest log with no summary line is never evidence ──────────────────
printf 'ctest died before printing a summary\n' > "$WORK/truncated.log"
OUT="$(run_pinned success "$WORK/good.env" "$WORK/truncated.log")"
case "$OUT" in *"DIAGNOSTIC ONLY"*) ok "T15 an unparseable ctest log is not evidence";;
               *) bad "T15 an unparseable ctest log was labelled evidence: $OUT";; esac

echo "== the sanitizer counter =="

# ── T16: PROVEN NON-ZERO before a zero is trusted ────────────────────────────
#
# The counter reads build/<preset>/Testing/Temporary/LastTest.log.  Point it at
# the wrong path, or write the pattern wrong, and it reads 0 — which is exactly
# what "no sanitizer fired" looks like.  So: feed it reports and require it to
# see them.
cat > "$WORK/tree/build/$FAKE/Testing/Temporary/LastTest.log" <<'LOG'
WARNING: ThreadSanitizer: data race (pid=1234)
ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000010
src/foo.cpp:42:9: runtime error: signed integer overflow
this line is not a sanitizer report
LOG
OUT="$(run_pinned success "$WORK/good.env" "$WORK/ctest.log")"
case "$OUT" in *"sanitizer reports: 3"*) ok "T16 the sanitizer counter sees TSan, ASan and UBSan reports (3)";;
               *) bad "T16 the sanitizer counter did not read 3 from a log containing 3 reports — a 0 from this instrument would mean nothing: $OUT";; esac

# ── T16b: it says WHICH lines matched, not just how many ─────────────────────
#
# A bare count is not actionable, and shipping one WAS a defect: CI reported
# `sanitizer reports: 1` on two green lanes and the page did not say what
# matched, so a real finding and a false-positive pattern match could not be told
# apart without another CI run. The rule this repo works to is "real defect until
# disproven" — and disproving one requires seeing it.
for probe in "ThreadSanitizer: data race" "AddressSanitizer: heap-use-after-free" "runtime error: signed integer overflow"; do
  case "$OUT" in *"$probe"*) ok "T16b the matched line is reported: ${probe}";;
                 *) bad "T16b the count was reported without the matching line (${probe} missing): $OUT";; esac
done
case "$OUT" in *"this line is not a sanitizer report"*)
      bad "T16b a NON-matching line was echoed as a sanitizer report — the excerpt is not the grep's own output";;
   *) ok "T16b only matching lines are echoed";; esac

# ── T17: and it reads 0 on a clean log, not 'unreadable' ─────────────────────
printf 'all quiet\n' > "$WORK/tree/build/$FAKE/Testing/Temporary/LastTest.log"
OUT="$(run_pinned success "$WORK/good.env" "$WORK/ctest.log")"
case "$OUT" in *"sanitizer reports: 0"*) ok "T17 a clean log reads 0";;
               *) bad "T17 a clean log did not read 0: $OUT";; esac

# ── T18: a MISSING LastTest.log is 'unreadable', never 0 ─────────────────────
#
# The distinction is the point: 0 is a result, `unreadable` is the absence of
# one, and collapsing them is how a broken counter reads as a clean lane.
rm -f "$WORK/tree/build/$FAKE/Testing/Temporary/LastTest.log"
OUT="$(run_pinned success "$WORK/good.env" "$WORK/ctest.log")"
case "$OUT" in *"sanitizer reports: unreadable"*) ok "T18 a missing LastTest.log reads 'unreadable', not 0";;
               *) bad "T18 a missing LastTest.log did not report 'unreadable': $OUT";; esac

if [ -n "${PEAK_RSS_MUTANT:-}" ]; then
  echo
  echo "peak-rss harness (mutant run): ${PASS} passed, ${FAIL} failed"
  [ "$FAIL" -eq 0 ] || exit 1
  exit 0
fi

echo "== mutants — every cell above proven RED for its own stated reason =="
#
# ⚠️ A CELL THAT HAS NEVER RUN RED PROVES NOTHING (feedback_sanitizer_canary_
# must_be_proven_red).  Each mutant below breaks ONE property, and the harness
# is re-run against the broken copy; the run MUST fail, and it MUST fail at the
# named cell.  Asserting only "the mutant run failed" would be satisfied by a
# mutation that broke the harness itself.
#
# ⚠️ EVERY MUTATION IS cmp-GUARDED.  A sed that matches nothing leaves the file
# identical, the mutant run passes, and that reads as "the harness is thorough"
# when nothing was tested — the false-green shape this repo has paid for
# repeatedly.  A no-op mutation is a HARNESS failure here, not a skip.

MUT="$WORK/mut"; mkdir -p "$MUT"

mutant() {  # <name> <file: measure|report> <sed-expr> <cell that must go RED> <why>
  local name="$1" which="$2" expr="$3" cell="$4" why="$5"
  local src dst out rc
  rm -rf "$MUT"; mkdir -p "$MUT"
  cp "$MEASURE" "$MUT/measure-peak-rss.py"
  cp "$REPORT"  "$MUT/peak-memory-report.sh"
  cp "$HERE/expected-eligible-tests.txt" "$MUT/"
  chmod +x "$MUT/peak-memory-report.sh"
  case "$which" in
    measure) dst="$MUT/measure-peak-rss.py"; src="$MEASURE";;
    report)  dst="$MUT/peak-memory-report.sh"; src="$REPORT";;
  esac
  sed -i "$expr" "$dst"
  if cmp -s "$src" "$dst"; then
    bad "MUTANT ${name}: the mutation did NOT apply (files still identical) — this cell tested nothing"
    return
  fi
  out="$(PEAK_RSS_MUTANT=1 PEAK_RSS_MEASURE="$MUT/measure-peak-rss.py" \
         PEAK_RSS_REPORT="$MUT/peak-memory-report.sh" bash "$0" 2>&1)"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    bad "MUTANT ${name}: the harness stayed GREEN against a build that ${why}"
    return
  fi
  case "$out" in
    *"FAIL  ${cell}"*) ok "MUTANT ${name}: killed by ${cell} (${why})";;
    *) bad "MUTANT ${name}: the run failed, but NOT at ${cell} — the cell that is supposed to catch '${why}' did not. Failures were: $(echo "$out" | grep 'FAIL ' | tr '\n' ';')";;
  esac
}

mutant no-tree-walk measure \
  's|^        stack.extend(children.get(pid, ()))$|        pass  # MUTANT|' \
  "T2" "measures only the root process, i.e. the /usr/bin/time -v quantity #266 rejects"

mutant pgid-membership measure \
  's|^            ppid\[pid\] = int(after\[1\])$|            ppid[pid] = int(after[2])  # MUTANT: pgrp, not ppid|' \
  "T4" "keys tree membership on the process GROUP, losing any test that calls setsid"

# ⚠️ TWO edits, and both are load-bearing. Mutating only the assignment leaves
# the `total > self.peak` guard in place, which self-limits after ONE addition —
# the resulting figure landed within 0.5 % of the correct one, so that mutant was
# a near no-op that read as a passing test. The guard has to go too for the
# mutation to be the defect it names.
mutant accumulate measure \
  's|^                if total > self.peak:$|                if True:  # MUTANT|; s|^                    self.peak = total$|                    self.peak = self.peak + total  # MUTANT|' \
  "T2b" "accumulates across samples instead of taking a per-instant maximum"

# A sampler thread that dies leaves a peak covering an unstated fraction of the
# run — a plausible number, which is worse than none.
mutant sampler-crash measure \
  's|^                ppid, rss = read_proc_table()$|                raise RuntimeError("MUTANT")|' \
  "T1" "lets the sampler thread die and still reports the partial peak as a measurement"

mutant swallow-status measure \
  's|^    return status$|    return 0  # MUTANT|' \
  "T5" "reports success for a command that failed"

mutant always-evidence report \
  's|^if \[ "$TEST_OUTCOME" = "success" \] && \[ -n "$RAN" \] && \[ "$RAN" = "$EXPECTED" \]; then$|if true; then  # MUTANT|' \
  "T14" "labels every run acceptance evidence, including one that executed the wrong number of tests"

# The pre-tightening form of the workload-size scan: a bare `out of N` anywhere
# in the log. It reads a failing test's own output as the run's test count.
mutant bare-out-of report \
  's|^  RAN="$(awk .match($0, /\^\[0-9\]+% tests passed.*$|  RAN="$(awk '"'"'match($0, /out of [0-9]+/) { n = substr($0, RSTART+7, RLENGTH-7) } END { print n }'"'"' "$CTEST_LOG")"  # MUTANT|' \
  "T14b" "scans for a bare 'out of N' and so reads a failing test's output as the workload size"

# The intermediate form this script actually shipped for one revision: the whole
# sentence, but UNANCHORED. A decoy quoting ctest's phrasing walks through it.
mutant unanchored-summary report \
  's|^  RAN="$(awk .match($0, /\^\[0-9\]+% tests passed.*$|  RAN="$(awk '"'"'match($0, /tests passed,.* tests failed out of [0-9]+/) { m = substr($0, RSTART, RLENGTH); sub(/.* out of /, "", m); n = m } END { print n }'"'"' "$CTEST_LOG")"  # MUTANT|' \
  "T14b" "matches ctest's summary sentence anywhere on a line, so a test quoting that phrasing is read as the workload size"

# The form that shipped for one CI run: the count, with no attribution.
# The form that shipped for one CI run: headline only. It could not tell a
# nested-build transient from a concurrency peak, which is a WIDENING decision.
mutant headline-only report \
  's|^echo "peak=${PEAK_BYTES} bytes .*$|echo "peak=${PEAK_BYTES} bytes (${PEAK_GIB} GiB) of ${TOTAL_GIB} GiB — ${PCT} [preset: ${PRESET}] [concurrency: ${CONCURRENCY}] [ran: ${RAN:-unknown}/${EXPECTED}] [test outcome: ${TEST_OUTCOME}] [sanitizer reports: ${SAN_COUNT}]"  # MUTANT|' \
  "T8b" "prints the headline peak without peak_at / procs_at_peak, so a nested-build transient reads identically to a concurrency peak"

mutant count-without-lines report \
  's|^    SAN_LINES="$(grep -nE .*$|    SAN_LINES=""  # MUTANT|' \
  "T16b" "reports how many sanitizer reports there were without saying which lines matched"

mutant wrong-lasttest-path report \
  's|^LASTTEST="build/${PRESET}/Testing/Temporary/LastTest.log"$|LASTTEST="build/${PRESET}/Testing/LastTest.log"  # MUTANT|' \
  "T16" "points the sanitizer counter at a path that does not exist, so it always reads clean"

mutant number-without-measurement report \
  's|^if \[ "${STATUS:-}" != "ok" \] .*$|if false; then  # MUTANT|' \
  "T11" "renders a memory figure for a run whose instrument reported it measured nothing"

echo
echo "peak-rss harness: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
