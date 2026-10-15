# The three figures ci/peak-memory-report.sh reads out of a ctest log (#266 /
# #229 criterion 4), behind ONE entry point (#576):
#
#   awk -f ci/ctest-log-figures.awk <ctest-log>
#
# prints exactly three lines, in this order, each value EMPTY when the log does
# not carry it:
#
#   ran=<the executed test count>
#   real_s=<ctest's own `Total Test time (real)`>
#   sum_s=<the summed per-test durations, at one decimal>
#
# ── WHY A SEPARATE FILE ──────────────────────────────────────────────────────
#
# ci/parallelism-verdict.py's parse_ctest_log re-derives the same three figures,
# and cell S4 of ci/test-parallelism-aba-seam.sh compares the two. That cell used
# to run three awk programs typed again inside it, so an edit to the report's awk
# alone left it green. Both the report and the cell now run THIS file; there is
# no second copy to fall out of step.
#
# `key=value` lines rather than fields on one line: an absent figure is an empty
# string, and a reader that splits on whitespace would shift every later figure
# into the wrong name.
#
# One pass computes all three. FS is set for the real time alone; the other two
# rules read only $0, so it does not change what they match.
BEGIN { FS = "= *" }

# The executed count: `100% tests passed, 0 tests failed out of 361`. The
# trailing count is the size of the workload whatever the outcome, which is why
# it is preferred over the pass count.
#
# ANCHORED AT BOTH ENDS of ctest's own summary line, verified against real
# output: column 0 to end-of-line, no leading or trailing whitespace.
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
match($0, /^[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+$/) { m = $0; sub(/.* out of /, "", m); n = m }

# The real time: `Total Test time (real) =   5.10 sec`. The value keeps the log's
# own digits (a trailing zero included); a reader comparing it compares numbers.
/Total Test time \(real\)/ { t = $2 }

# The per-test durations: `  12/361 Test  #7: name ......   Passed    1.42 sec`.
/^ *[0-9]+\/[0-9]+ +Test +#[0-9]+/ && match($0, /[0-9.]+ sec$/) { s += substr($0, RSTART, RLENGTH-4) }

END {
  gsub(/[^0-9.]/, "", t)
  print "ran=" n
  print "real_s=" t
  if (s > 0) printf "sum_s=%.1f\n", s
  else print "sum_s="
}
