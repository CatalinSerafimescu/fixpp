#!/usr/bin/env bash
# ci/run-bench-suite.sh — run the CI benchmark allowlist (#209).
#
# WHY THIS EXISTS. `tier1.yml`'s `bench` job ran `placeholder_bench` — a benchmark
# that measures nothing — under `continue-on-error: true`, compared it with a
# comparator that `return 0`s by construction, and was absent from
# `tier1-required`'s `needs:`. No CI job has ever reported a runtime perf
# regression of any size. #263 is the concrete cost: `XmlLoader::load` regressed
# 60-90% ON MAIN and nothing saw it.
#
# This script is the "actually run the real benchmarks" half.
# `tools/bench_compare.py` is the "actually assert something" half.
#
# ⚠️ IT FAILS CLOSED, and that is the whole point. A missing binary, a crashed
# binary, or an unwritten results file is an ERROR here, not a warning — the
# previous design treated exactly those as "bench binary may not exist yet".
#
# Usage: ci/run-bench-suite.sh <build-dir> <out-dir> [--only-paired] [manifest]
#   build-dir  the CMake build directory (e.g. build/linux-clang-release).
#              ⚠️ NOT a `bin/` directory — bench/ sets RUNTIME_OUTPUT_DIRECTORY
#              per subdirectory, so the manifest carries each exe's own path.
#   out-dir    where to write <name>.json (created if absent)
#   --only-paired  run only the rows marked `paired` (tier 2's base leg — there
#              is no reason to run the full suite against the merge-base)
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR=""; OUT_DIR=""; ONLY_PAIRED=false; MANIFEST="$repo_root/bench/ci-suite.txt"
for arg in "$@"; do
    case "$arg" in
        --only-paired) ONLY_PAIRED=true ;;
        *) if   [ -z "$BUILD_DIR" ]; then BUILD_DIR="$arg"
           elif [ -z "$OUT_DIR" ];   then OUT_DIR="$arg"
           else MANIFEST="$arg"; fi ;;
    esac
done

fail() { echo "::error::run-bench-suite: $1" >&2; exit 1; }

[ -n "$BUILD_DIR" ] && [ -n "$OUT_DIR" ] || fail "usage: $0 <build-dir> <out-dir> [--only-paired] [manifest]"
[ -d "$BUILD_DIR" ] || fail "build-dir does not exist: $BUILD_DIR"
[ -f "$MANIFEST" ] || fail "manifest not found: $MANIFEST"

mkdir -p "$OUT_DIR"

# Repetitions + aggregates-only: a single sample carries no dispersion, so
# neither the tier-2 band nor any future tightening of it could ever be grounded
# in evidence. The `_median`/`_stddev`/`_cv` rows are what make the A-vs-A noise
# floor in tools/bench_compare.py --paired computable at all.
REPS="${FIXPP_BENCH_REPETITIONS:-3}"

# ── AC-1: the runner's own hardware and toolchain, printed. The pre-#209 job
# printed none of it, which is why #209's analysis had to REASON about the
# runner class instead of reading it.
echo "=== bench suite — runner context ==="
echo "  nproc       : $(nproc)"
echo "  repetitions : ${REPS}"
echo "  build dir   : ${BUILD_DIR}"
echo "  manifest    : ${MANIFEST}"
echo "  only-paired : ${ONLY_PAIRED}"
[ -r /proc/cpuinfo ] && echo "  cpu model   : $(grep -m1 '^model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
command -v clang++ >/dev/null && echo "  compiler    : $(clang++ --version | head -1)"
echo ""

count=0
skipped=0
while read -r exe_rel comparand tier2; do
    [ -z "${exe_rel:-}" ] && continue
    case "$exe_rel" in \#*) continue ;; esac
    # A row with the wrong arity would bind a field to the empty string and the
    # comparator would then look for a baseline named "".
    [ -n "${comparand:-}" ] && [ -n "${tier2:-}" ] \
        || fail "manifest row '${exe_rel}' does not have exactly 3 fields"
    case "$tier2" in paired|no) ;; *) fail "manifest row '${exe_rel}': tier-2 field must be 'paired' or 'no', got '${tier2}'" ;; esac

    if [ "$ONLY_PAIRED" = true ] && [ "$tier2" != "paired" ]; then
        skipped=$((skipped + 1)); continue
    fi

    name="$(basename "$exe_rel")"
    bin="${BUILD_DIR}/${exe_rel}"
    # T1-3 — an allowlisted binary missing from the build tree. Previously this
    # was the `continue-on-error` case: the suite silently shrank and the job
    # stayed green having measured nothing.
    [ -x "$bin" ] || fail "allowlisted benchmark '${name}' is not built or not executable at ${bin}. \
The suite must not silently shrink — either build it or remove its row from ${MANIFEST}."

    out="${OUT_DIR}/${name}.json"
    echo "--- ${name} ---"
    # ⚠️ FOUND BY ci/test-bench-gate.sh CELL R-SILENT, not by review. Without
    # this `rm -f`, the "binary exited 0 but wrote nothing" check below is
    # satisfied by a STALE file from an earlier invocation into the same
    # out-dir, and a benchmark that silently stopped emitting results reads as
    # a clean measurement. The cell is deliberately left running against a
    # populated out-dir so it keeps exercising exactly that.
    rm -f "$out"
    # No `|| true`, no `continue-on-error`: a crashed benchmark fails the job.
    "$bin" \
        --benchmark_format=json \
        --benchmark_out="${out}" \
        --benchmark_out_format=json \
        --benchmark_repetitions="${REPS}" \
        --benchmark_report_aggregates_only=true \
        --benchmark_display_aggregates_only=true \
      || fail "benchmark '${name}' exited non-zero"

    # T1-1 — a binary can exit 0 having written nothing (bad path, disk full).
    # Checked here as well as in the comparator so the error names the producer.
    [ -s "$out" ] || fail "benchmark '${name}' exited 0 but wrote no results to ${out}"

    count=$((count + 1))
done < "$MANIFEST"

# A manifest that parses to zero rows leaves every downstream assertion
# vacuously satisfied — the silent-empty class this repo has hit three times in
# one gate (feedback_silent_empty_recurred_three_times_including_inside_its_own_fix).
[ "$count" -gt 0 ] || fail "manifest ${MANIFEST} yielded zero runnable rows (only-paired=${ONLY_PAIRED})"

echo ""
echo "bench suite: ran ${count} binaries (${skipped} skipped by --only-paired) -> ${OUT_DIR}"
