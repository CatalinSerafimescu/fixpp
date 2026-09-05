#!/usr/bin/env bash
# Canonical coverage DIGEST for #267 acceptance item 4.
#
# Usage:  ci/lcov-coverage-digest.sh <lcov-report>
# Prints key=value lines on stdout; exits non-zero and prints nothing on failure.
#
#   sorted_info_sha256=<hex>
#   branch_records_in_digest=<n>   # branch records that survived into the key
#   lines_covered=<n>  lines_total=<n>
#   branches_covered=<n>  branches_total=<n>
#
# ── WHY THIS IS A SCRIPT AND NOT THREE LINES IN THE DRIVER ───────────────────
#
# Because the cells that test it must exercise THE PATH THE DRIVER RUNS, and
# they could not. The digest used to be assembled inline in
# `run-parallelism-aba.sh:coverage_digest`, with the seam cells invoking
# `ci/lcov-coverage-key.awk` on their own fixtures and a `grep` asserting the
# driver still mentioned that awk somewhere. A hostile review produced the
# mutation that defeats exactly that arrangement:
#
#     -sha="$(sha256sum < "$keyed" | ...)"
#     +sha="$(sha256sum < "$info"  | ...)"
#
# The awk is still invoked, so the grep is satisfied; the cells still hash the
# awk's output themselves, so they agree; and the driver quietly goes back to
# hashing the RAW report — the original defect, restored, with every cell green.
# A source grep proves a call exists, never that its result is used. With the
# whole operation behind one entry point there is nothing left to diverge.
#
# ── WHY IT VALIDATES INSTEAD OF TRUSTING THE PIPELINE ────────────────────────
#
# `run-parallelism-aba.sh` runs under `set -uo pipefail` and NOT `set -e`, so a
# failing `awk` did not stop it. A missing, unreadable or syntactically broken
# key file left `sort` producing an EMPTY stream, and the sha256 of nothing is
# a perfectly valid constant — identical in all three passes. That is a false
# "coverage identical", i.e. this repo's signature failure: an instrument that
# reports clean because it could not report anything else. `lines_total` is read
# off the RAW report, so nothing downstream contradicted it either.
#
# The guard is structural rather than a non-empty check: every `DA:` and `FNDA:`
# record in the report must survive into the key. A key that lost records — or
# an awk that silently stopped emitting them — fails here instead of hashing to
# a stable, meaningless value.
set -uo pipefail

info="${1:-}"
[ -n "$info" ] && [ -s "$info" ] || {
  echo "lcov-coverage-digest: usage: $0 <non-empty lcov report>" >&2; exit 2; }

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
key="$here/lcov-coverage-key.awk"
[ -r "$key" ] || { echo "lcov-coverage-digest: missing $key" >&2; exit 2; }

keyed="$(mktemp)"; trap 'rm -f "$keyed"' EXIT

# ⚠️ The awk's status is checked, not assumed — see above. `pipefail` makes the
# pipeline's status the first non-zero, so a broken awk is caught even though
# `sort` succeeds on the empty stream it was handed.
if ! awk -f "$key" "$info" | LC_ALL=C sort > "$keyed"; then
  echo "lcov-coverage-digest: the coverage key failed on $info" >&2; exit 3
fi

# Structural check: no `DA:`/`FNDA:` record may be lost between report and key.
# ⚠️ COUNTED ON THE KEYED RECORD BOUNDARY (`\001`), NOT WITH A BARE /DA:/. Every
# keyed line carries its `SF:` pathname first, so a source path containing the
# token would be counted as a record of that kind. Real paths do not usually
# contain `BRDA:`, which is precisely why a bare match would go unnoticed until
# one did.
counts="$(awk -v OFS=' ' '
  BEGIN { FS = "\001" }
  { rec = (NF > 1 ? $2 : $0) }
  rec ~ /^DA:/   { kda++ }
  rec ~ /^FNDA:/ { kfn++ }
  rec ~ /^BRDA:/ { kbr++ }
  END { print kda + 0, kfn + 0, kbr + 0 }
' "$keyed")"
raw="$(awk '
  /^DA:/   { rda++ }
  /^FNDA:/ { rfn++ }
  END { print rda + 0, rfn + 0 }
' "$info")"
set -- $counts; k_da="$1"; k_fn="$2"; k_br="$3"
set -- $raw;    r_da="$1"; r_fn="$2"
if [ "$k_da" -ne "$r_da" ] || [ "$k_fn" -ne "$r_fn" ]; then
  echo "lcov-coverage-digest: the key LOST records (DA ${r_da}->${k_da}, FNDA ${r_fn}->${k_fn})" >&2
  exit 3
fi

sha="$(sha256sum < "$keyed" | cut -d' ' -f1)"

# Branch data is outside the key by design (see the awk). The report's branch
# counts are reported anyway: a future branch-stability measurement needs a
# starting point, and `branches_covered` beside a digest would otherwise invite
# the reader to assume it was compared. `branch_records_in_digest` is counted
# from the keyed bytes, so it states the exclusion rather than asserting it.
printf 'sorted_info_sha256=%s\n' "$sha"
printf 'branch_records_in_digest=%s\n' "$k_br"
awk '
  /^DA:/   { split(substr($0, 4), a, ","); lt++; if (a[2] + 0 > 0) lc++ }
  /^BRDA:/ { split(substr($0, 6), b, ","); bt++
             if (b[4] != "-" && b[4] + 0 > 0) bc++ }
  END      { printf "lines_covered=%d\nlines_total=%d\n", lc + 0, lt + 0
             printf "branches_covered=%d\nbranches_total=%d\n", bc + 0, bt + 0 }
' "$info"
