#!/usr/bin/env bash
# CI-side (Tier 3, #240): report this run's ccache counters, and ASSERT LIVENESS.
#
#   ci/ccache-stats.sh <preset> <restore-disposition> <build-outcome>
#
#   restore-disposition — steps.<id>.outputs.hit ('true' / 'false' / '' )
#   build-outcome       — steps.<id>.outcome ('success' / 'failure' / ...)
#
# ── WHAT IS ASSERTED, AND WHAT IS ONLY REPORTED ──────────────────────────────
#
# ASSERTED: that ccache was reached at all. Zero cacheable calls after a
# SUCCESSFUL build means the launcher never took effect, and the whole change is
# then a no-op that reports green — the exact ran-exited-0-measured-nothing
# shape this repo keeps paying for.
#
# ⚠️ REPORTED, NOT ASSERTED: the hit rate. There is deliberately no floor. The
# first run after a compiler bump is legitimately 0 %, and a cold run must not be
# red. Tighten only once a warm baseline exists — and note that the evidence for
# #240 is the PAIR (`restore HIT` AND a high hit rate), because a HIT with 0 % is
# green under this instrument BY DESIGN and is the change failing, not passing.
#
# ⚠️ Zero cacheable calls does NOT by itself prove the launcher was unwired.
# ccache may have run and classified every invocation as uncacheable, or no
# compile edge may have run at all. This script therefore REPORTS the open causes
# and the uncacheable counters that discriminate between them, rather than
# asserting one. (Same correction as PR #247's finding G2.)
#
# ⚠️ `set -uo pipefail` WITHOUT `-e` — see restore-ccache.sh's header. Every
# failure path below is explicitly dispositioned instead.
set -uo pipefail

PRESET="${1:?usage: ccache-stats.sh <preset> <restore-disposition> <build-outcome>}"
RESTORE="${2-}"
BUILD_OUTCOME="${3-}"

note() { echo "$1"; [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }

STATS="$(mktemp)"; trap 'rm -f "$STATS"' EXIT

# ⚠️ GUARDED EVEN THOUGH IT IS "DIAGNOSTIC ONLY". "Diagnostic-only" describes
# what the OUTPUT is for, not what its FAILURE does: an unguarded
# `ccache --show-stats` under a step that aborts on error kills the step before
# the attributed `::error::` below can be emitted. Measured on PR #247.
#
# The warning says the human-readable table "may be absent or incomplete" — NOT
# that it is absent. A tool that prints some counters and then exits non-zero is
# an ordinary failure shape, and a warning that denies output the log visibly
# contains is a warning nobody will trust again.
if ! ccache --show-stats; then
  echo "::warning::\`ccache --show-stats\` exited non-zero; its human-readable table above may be absent or incomplete and must not be relied upon. The machine-readable counters below are the authority."
fi

if ! ccache --print-stats > "$STATS" 2>/dev/null; then
  echo "::error::\`ccache --print-stats\` failed for ${PRESET}, so no counters could be read. Three causes reach here: ccache is not installed or not on PATH, the cache directory (\$CCACHE_DIR=${CCACHE_DIR:-<unset>}) is unreadable, or ccache exited non-zero for another reason. The compiler cache cannot be assessed on this run."
  exit 1
fi

# One counter, by exact key, with a CARDINALITY check.
#
# `n != 1` covers both a MISSING key (a ccache whose stats vocabulary differs
# from this one's) and a DUPLICATED key. Both are format failures and share this
# branch deliberately — printing nothing, which the caller turns into the shared
# `-z` message below. It does NOT exit non-zero on a missing key: that would
# conflate "this key is absent" with "awk itself failed", and the two need
# different messages.
val() { awk -v k="$1" '$1 == k { n++; v = $2 } END { if (n == 1) print v }' "$STATS"; }

read_counter() {
  local key="$1" out
  out="$(val "$key")" || {
    echo "::error::\`awk\` itself failed while reading '${key}' from ccache's stats output. This is an instrument failure, not a cache result." >&2
    return 2
  }
  if [ -z "$out" ]; then
    echo "::error::ccache's stats output does not contain exactly one '${key}' line. The counter vocabulary is not the one this script parses (ccache version drift), so the numbers below cannot be trusted and no liveness claim is made." >&2
    return 2
  fi
  # Canonical decimal. ccache emits plain integers, but a leading zero would be
  # read as OCTAL by `$(( ))` — and under Actions' `bash -e {0}` an arithmetic
  # error is NON-FATAL, so the step would sail past having printed no
  # disposition at all. Validate the shape, then force base 10.
  case "$out" in
    ''|*[!0-9]*)
      echo "::error::ccache reported '${key}' as '${out}', which is not a non-negative integer. Refusing to compute a hit rate from it." >&2
      return 2 ;;
  esac
  printf '%s' "$((10#$out))"
}

dhit="$(read_counter direct_cache_hit)"       || exit 1
phit="$(read_counter preprocessed_cache_hit)" || exit 1
miss="$(read_counter cache_miss)"             || exit 1
size="$(read_counter cache_size_kibibyte)"    || exit 1
maxs="$(read_counter max_cache_size_kibibyte)" || exit 1
clean="$(read_counter cleanups_performed)"    || exit 1

hits=$((dhit + phit))
calls=$((hits + miss))

# ── Published so the seed step can skip republishing an UNCHANGED cache ───────
#
# `cache_miss == 0` after a successful build means ccache wrote no new entry, so
# the tag already holds this content and re-archiving ~2 GB and re-uploading it
# accomplishes nothing except orphaning another untagged version for the pruner
# to reclaim. That is the ordinary shape of a CI-, docs- or workflow-only push
# to main — this PR is itself one.
#
# ⚠️ EMITTED HERE, BEFORE THE LIVENESS CHECK BELOW, ON PURPOSE. The value is
# wanted even on the paths that end in `exit 1`, and computing it after a branch
# that can exit is how an output silently goes missing.
#
# ⚠️ THE CONSUMER'S GUARD IS FAIL-OPEN, AND THAT IS THE RIGHT DIRECTION. If this
# step never ran or died before this line, `steps.<id>.outputs.misses` is empty,
# `!= '0'` is true, and the seed publishes exactly as it does today. The guard
# only ever SKIPS on positive evidence that nothing changed; it can never
# withhold a cache because a measurement was missing.
#
# ⚠️ `misses` ALONE IS SUFFICIENT — do not add `&& restore == 'hit'`. A restore
# MISS with zero misses would mean no compile ran at all, and that case cannot
# reach the seed: the liveness check below exits 1 on it, which fails the job and
# skips every later step. Adding the conjunct would look safer while guarding a
# path that is already closed, and would then also skip the legitimate
# cold-seed case if the reasoning behind it ever drifted.
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  {
    echo "misses=${miss}"
    echo "hits=${hits}"
    echo "calls=${calls}"
    echo "cleanups=${clean}"
  } >> "$GITHUB_OUTPUT"
fi

# ── The thrash indicator #240 exists to close ────────────────────────────────
#
# The 500M cap was defended by a comment measuring what the cap ALLOWED
# ("~460 MB each"), which is circular. `cleanups_performed` is the
# non-circular reading: counters are zeroed by restore-ccache.sh, so this is
# THIS RUN's evictions. A cache that evicts while it is still being filled is
# the 7-27 % hit rate's mechanism, stated as a number instead of inferred.
fullpct="n/a"
if [ "$maxs" -gt 0 ]; then fullpct="$(( size * 100 / maxs ))%"; fi

{
  echo "### ccache — ${PRESET}"
  echo ''
  echo "| | |"
  echo "|---|---|"
  echo "| restore | \`${RESTORE:-n/a}\` (hit) |"
  echo "| hits | ${hits} (direct=${dhit} preprocessed=${phit}) |"
  echo "| misses | ${miss} |"
  echo "| cacheable calls | ${calls} |"
  echo "| cache size | $((size / 1024)) MiB of $((maxs / 1024)) MiB (${fullpct} full) |"
  echo "| cleanups this run | ${clean} |"
} >> "${GITHUB_STEP_SUMMARY:-/dev/null}"

echo "ccache: hits=${hits} (direct=${dhit} preprocessed=${phit}) misses=${miss} cacheable_calls=${calls}"
echo "ccache: size=$((size / 1024)) MiB cap=$((maxs / 1024)) MiB (${fullpct} full) cleanups_this_run=${clean}"

if [ "$clean" -gt 0 ]; then
  note "::warning::ccache performed ${clean} cleanup(s) DURING this run on ${PRESET} — the cache hit its $((maxs / 1024)) MiB cap and evicted entries while it was still being populated. That is the mechanism behind #240's 7-27 % hit rate; the cap is too small for this lane's demand, and the archive size reported by the seed step is the demand measurement."
fi

if [ "$calls" -eq 0 ]; then
  if [ "$BUILD_OUTCOME" != "success" ]; then
    note "ccache: zero cacheable calls on ${PRESET}, but the Build step reported \`${BUILD_OUTCOME:-unknown}\` — a build that did not get as far as compiling is a sufficient explanation, so no liveness claim is made either way."
    exit 0
  fi
  # Report the causes that remain OPEN; do not select one. The uncacheable
  # counters below are what discriminates "ccache ran and declined everything"
  # from "ccache was never invoked" — a distinction the exit code cannot carry.
  echo "::error::ccache recorded ZERO cacheable calls on ${PRESET} after a SUCCESSFUL build. Something between CMake and the compiler is not routing through ccache, and this lane's compiler cache is a no-op. Causes still open at this point: (1) CMAKE_{C,CXX}_COMPILER_LAUNCHER did not reach the compile rules; (2) ccache ran but classified every invocation as uncacheable — the counters below say whether that happened; (3) no compile edge ran at all (a fully up-to-date build tree). This is fatal because a silently unwired launcher reports the lane green while measuring nothing."
  echo "--- non-zero uncacheable / error counters (empty means none, i.e. cause 2 is ruled out) ---"
  awk '$2 != 0 && $1 !~ /^(cache_size_kibibyte|max_cache_size_kibibyte|max_files_in_cache|files_in_cache|stats_updated_timestamp|stats_zeroed_timestamp)$/ { print "  " $1 " " $2 }' "$STATS"
  exit 1
fi

rate=$(( hits * 100 / calls ))
note "ccache-hitrate ${rate}% over ${calls} cacheable calls (${PRESET}), restore=\`${RESTORE:-n/a}\`"

# ── "READ THE PAIR" — made a CHECK instead of a sentence addressed to a human ─
#
# The rule that a restore HIT at ~0 % is the change FAILING, not passing, was
# stated in this file's header and again in the workflow, as prose. But this
# script holds BOTH inputs and was making no judgment on them — the exact shape
# the #244/#247 correction is on record about: an instrument that reports two
# numbers and leaves the only inference that matters to whoever reads the log.
#
# ⚠️ A WARNING, NOT AN ASSERT, AND THE THRESHOLD IS DELIBERATELY LOW. A hard
# floor is wrong here for the same reason the liveness check is not a rate
# check: a legitimate warm run can be well down after a large refactor or a
# codegen change. 10 % is set to catch the PATHOLOGICAL signature — the tag was
# pulled but almost nothing in it matched, i.e. compiler, flag or path drift —
# without firing on ordinary churn. Tighten only once a warm baseline exists,
# which is the same discipline the missing rate floor follows.
#
# Only fires on a HIT: on a MISS a 0 % rate is the expected cold-run reading and
# says nothing.
if [ "${RESTORE:-}" = "true" ] && [ "$rate" -lt 10 ]; then
  note "::warning::ccache RESTORED a cache for ${PRESET} and then hit only ${rate}% of ${calls} cacheable calls. That pair — restore HIT with a near-zero rate — is the signature of a cache that was pulled but whose entries do not match this build: compiler drift (CCACHE_COMPILERCHECK=content hashes the binary, and llvm.sh can silently fall back to an earlier clang major), a flag change, or a build-directory path change. It is NOT a failure of this step, and it is deliberately not fatal — but it means the compiler cache is doing almost nothing on this lane, so do not read the green tick as evidence that it works."
fi
exit 0
