#!/usr/bin/env bash
# ci/run-bench-suite.sh — run the CI benchmark allowlist and stamp provenance (#209).
#
# WHY THIS EXISTS. `tier1.yml`'s `bench` job ran `placeholder_bench` — a benchmark
# that measures nothing — under `continue-on-error: true`, and compared it with a
# comparator that `return 0`s by construction. Three independent softnesses, so
# no CI job has ever reported a runtime perf regression of any size. #263 is the
# concrete cost: `XmlLoader::load` regressed 60-90% ON MAIN and nothing saw it.
#
# This script is the "actually run the real benchmarks" half. `tools/bench_compare.py`
# is the "actually assert something" half.
#
# ⚠️ IT FAILS CLOSED, and that is the entire point. A missing binary, a crashed
# binary, or an unwritten results file is an ERROR here, not a warning — the
# previous design treated exactly those as "bench binary may not exist yet" and
# exited 0. See .specify/ci209-bench-gate.md §4 cells A1/A6.
#
# PROVENANCE. Every emitted JSON gets `context.fixpp_provenance` stamped in.
# `tools/bench_compare.py` uses it to decide whether a baseline is a VALID
# comparand for a wall-clock comparison at all: every pre-#209 baseline in
# bench/baselines/ was recorded on the WSL2 dev host (num_cpus 8 or 10, or the
# key absent) and four wire/* baselines are `build_type: debug`. Comparing a CI
# measurement against those is invalid at ANY band, not merely noisy — #263
# measured the dev host drifting -35% against ITSELF across two sessions.
#
# Usage: ci/run-bench-suite.sh <bin-dir> <out-dir> [manifest]
#   bin-dir   directory holding the built benchmark executables
#             (e.g. build/linux-clang-release/bin)
#   out-dir   where to write <target>.json (created if absent)
#   manifest  default: bench/ci-suite.txt
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BIN_DIR="${1:-}"
OUT_DIR="${2:-}"
MANIFEST="${3:-$repo_root/bench/ci-suite.txt}"

fail() { echo "::error::run-bench-suite: $1" >&2; exit 1; }

[ -n "$BIN_DIR" ] && [ -n "$OUT_DIR" ] || fail "usage: $0 <bin-dir> <out-dir> [manifest]"
[ -d "$BIN_DIR" ] || fail "bin-dir does not exist: $BIN_DIR"
[ -f "$MANIFEST" ] || fail "manifest not found: $MANIFEST"
command -v python3 >/dev/null || fail "python3 is required (provenance stamping)"

mkdir -p "$OUT_DIR"

# Repetitions + aggregates-only: a single sample has no dispersion, so the
# timing axis could never grow a defensible band from it. `_median`/`_stddev`/
# `_cv` rows are what §6 needs to establish a noise floor before it can go hard.
# Baselines are SEEDED BY THIS SAME SCRIPT, so baseline and current carry the
# identical aggregate name set and the A3 name-set check compares like with like.
REPS="${FIXPP_BENCH_REPETITIONS:-3}"

# ── AC-1: the runner's own hardware, printed. Without this there is no evidence
# for the provenance argument at all — the pre-#209 job never printed it, which
# is why #209's analysis had to reason about the runner class rather than read it.
echo "=== bench suite — runner context ==="
echo "  nproc            : $(nproc)"
echo "  repetitions      : ${REPS}"
echo "  bin dir          : ${BIN_DIR}"
echo "  manifest         : ${MANIFEST}"
if [ -r /proc/cpuinfo ]; then
    echo "  model name       : $(grep -m1 '^model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//' || echo '(unknown)')"
fi
echo ""

# `RUNNER_*` are GitHub-set; empty locally, which is itself informative — a
# baseline seeded from a laptop must not claim CI provenance.
PROV_SOURCE="${FIXPP_BENCH_SOURCE:-${GITHUB_WORKFLOW:+ci/${GITHUB_WORKFLOW}/bench}}"
PROV_SOURCE="${PROV_SOURCE:-local}"
PROV_PRESET="${FIXPP_BENCH_PRESET:-linux-clang-release}"
PROV_RUNNER="${RUNNER_OS:+${RUNNER_OS}/${ImageOS:-unknown}}"
PROV_RUNNER="${PROV_RUNNER:-local}"

count=0
while read -r target baseline; do
    # Skip blanks / comments. Guard against a manifest row with the wrong arity:
    # a one-field row would silently bind $baseline to the empty string and the
    # comparator would then look for a baseline named "".
    [ -z "${target:-}" ] && continue
    case "$target" in \#*) continue ;; esac
    [ -n "${baseline:-}" ] || fail "manifest row for '${target}' has no baseline field (need exactly 2 fields)"

    bin="${BIN_DIR}/${target}"
    # A6 — an allowlisted binary missing from the build tree. Previously this
    # was the `continue-on-error` case; the suite would silently shrink and the
    # job stayed green having measured nothing.
    [ -x "$bin" ] || fail "allowlisted benchmark '${target}' is not built or not executable at ${bin}. \
The suite must not silently shrink — either build it or remove its row from ${MANIFEST}."

    out="${OUT_DIR}/${target}.json"
    echo "--- ${target} ---"
    # No `|| true`, no `continue-on-error`: a crashed benchmark fails the job.
    "$bin" \
        --benchmark_format=json \
        --benchmark_out="${out}" \
        --benchmark_out_format=json \
        --benchmark_repetitions="${REPS}" \
        --benchmark_report_aggregates_only=true \
        --benchmark_display_aggregates_only=true \
      || fail "benchmark '${target}' exited non-zero"

    # A1 — the binary can exit 0 having written nothing (bad --benchmark_out
    # path, disk full). Checked here rather than left for the comparator so the
    # error names the producer.
    [ -s "$out" ] || fail "benchmark '${target}' exited 0 but wrote no results to ${out}"

    python3 - "$out" "$PROV_SOURCE" "$PROV_PRESET" "$PROV_RUNNER" "$REPS" <<'PY'
import json, sys
path, source, preset, runner, reps = sys.argv[1:6]
with open(path) as f:
    d = json.load(f)
ctx = d.setdefault("context", {})
ctx["fixpp_provenance"] = {
    "source": source,
    "preset": preset,
    "runner": runner,
    "repetitions": int(reps),
}
with open(path, "w") as f:
    json.dump(d, f, indent=2)
    f.write("\n")
PY

    count=$((count + 1))
done < "$MANIFEST"

# A manifest that parsed to zero rows would leave the comparator with nothing to
# check and every downstream assertion vacuously satisfied — the silent-empty
# class this repo has hit three times in one gate
# (feedback_silent_empty_recurred_three_times_including_inside_its_own_fix).
[ "$count" -gt 0 ] || fail "manifest ${MANIFEST} yielded zero benchmark rows"

echo ""
echo "bench suite: ran ${count} benchmark binaries -> ${OUT_DIR}"
