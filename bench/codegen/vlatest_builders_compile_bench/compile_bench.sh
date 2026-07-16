#!/usr/bin/env bash
# bench/codegen/vlatest_builders_compile_bench/compile_bench.sh
#
# 077-builder-args-dedup T015 [US1] — vlatest builder-tier compile-resource
# bench (SC-001/SC-002; quickstart.md V1). Modelled on 003's T046
# compile_time_bench.sh convention (decision-record, NOT an Article VIII
# mechanism — Art VIII is runtime-only).
#
# Measures `clang++ -std=c++23 -fsyntax-only` peak RSS + wall time for a TU
# that #includes ONLY fixpp/vlatest/Builders.hpp — the deduped (573-plan)
# typed builder tier. Pre-077 (076, message-rooted, ~26k structs / 137MB)
# measured >21GB RSS / OOM-killed; SC-001 requires "low single-digit GB".
#
# RSS ceiling is a HARD gate (SC-001) — the dedup mechanism's whole point is
# making this tier compilable at all. Wall time vs the 003-precedent ≤3s
# single-version syntax-only ceiling is a KNOWN_OVERAGE (recorded, not
# failed) — mirrors compile_time_bench.sh's v50sp2 treatment: a strictly
# larger generated surface than the legacy versions is an accepted,
# documented cost, not a regression.
#
# Usage:
#   compile_bench.sh <codegen_include_dir> [<cxx_compiler>] [<project_include_dir>]

set -euo pipefail

CODEGEN_INC="${1:-}"
CXX="${2:-clang++}"
SINGLE_CEILING=3
# RSS ceiling: "low single-digit GB" per SC-001 (measured ~3.7-4.6GB on the
# reference host at /implement) — set generously above the measured value so
# normal machine variance doesn't flake the gate, but far below the pre-dedup
# >21GB failure mode this bench exists to catch a regression back toward.
RSS_CEILING_KB=$((10 * 1024 * 1024))  # 10 GiB

if [[ -z "${CODEGEN_INC}" ]]; then
    echo "Usage: $0 <codegen_include_dir> [<cxx_compiler>] [<project_include_dir>]" >&2
    exit 1
fi

PROJECT_INC="${3:-}"
if [[ -z "${PROJECT_INC}" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    # scripts live under bench/codegen/vlatest_builders_compile_bench/ →
    # repo root is 3 levels up.
    REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
    PROJECT_INC="${REPO_ROOT}/include"
fi

BUILDERS_HPP="${CODEGEN_INC}/fixpp/vlatest/Builders.hpp"
if [[ ! -f "${BUILDERS_HPP}" ]]; then
    echo "SKIP: ${BUILDERS_HPP} not found (FIXPP_CODEGEN_FIX_LATEST=OFF?)" >&2
    exit 0
fi

if ! command -v /usr/bin/time >/dev/null 2>&1; then
    echo "SKIP: /usr/bin/time not available on this host" >&2
    exit 0
fi

TMPDIR_BENCH="$(mktemp -d)"
trap 'rm -rf "${TMPDIR_BENCH}"' EXIT

TU="${TMPDIR_BENCH}/tu_vlatest_builders.cpp"
cat > "${TU}" <<EOF
// Synthetic single-TU probe — 077 T015 / SC-001/SC-002 / seam #2 analog
#include <fixpp/vlatest/Builders.hpp>
EOF

TIME_LOG="${TMPDIR_BENCH}/time.log"
set +e
/usr/bin/time -v "${CXX}" -std=c++23 -fsyntax-only \
    -I"${CODEGEN_INC}" \
    -I"${PROJECT_INC}" \
    "${TU}" > "${TMPDIR_BENCH}/compile.log" 2> "${TIME_LOG}"
COMPILE_STATUS=$?
set -e

if [[ ${COMPILE_STATUS} -ne 0 ]]; then
    echo "FAIL: clang++ -fsyntax-only exited ${COMPILE_STATUS}" >&2
    cat "${TMPDIR_BENCH}/compile.log" >&2
    cat "${TIME_LOG}" >&2
    exit 1
fi

PEAK_RSS_KB="$(grep 'Maximum resident set size' "${TIME_LOG}" | awk -F': ' '{print $2}')"
WALL_CLOCK="$(grep 'Elapsed (wall clock) time' "${TIME_LOG}" | awk -F': ' '{print $2}')"

echo "=== 077 T015: vlatest/Builders.hpp compile-resource bench ==="
echo "    peak RSS:   ${PEAK_RSS_KB} KB"
echo "    wall clock: ${WALL_CLOCK}"
echo "    RSS ceiling: ${RSS_CEILING_KB} KB (SC-001, hard gate)"
echo "    wall ceiling: ${SINGLE_CEILING} s (003 T046 convention, KNOWN_OVERAGE if exceeded)"
echo ""

if [[ -z "${PEAK_RSS_KB}" ]]; then
    echo "FAIL: could not parse peak RSS from /usr/bin/time -v output" >&2
    exit 1
fi

if (( PEAK_RSS_KB > RSS_CEILING_KB )); then
    echo "FAIL: peak RSS ${PEAK_RSS_KB} KB exceeds ${RSS_CEILING_KB} KB ceiling (SC-001)" >&2
    exit 1
fi

echo "PASS: peak RSS within SC-001 ceiling; wall-time overage (if any) is a"
echo "      recorded KNOWN_OVERAGE per the 003 T046 decision-record convention"
echo "      (.specify/decisions/077-builder-args-dedup-verify.md)."
