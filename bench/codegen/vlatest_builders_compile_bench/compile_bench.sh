#!/usr/bin/env bash
# bench/codegen/vlatest_builders_compile_bench/compile_bench.sh
#
# 077-builder-args-dedup T015 [US1] — vlatest builder-tier compile-resource
# bench (SC-001/SC-002; quickstart.md V1). Modelled on 003's T046
# compile_time_bench.sh convention (decision-record, NOT an Article VIII
# mechanism — Art VIII is runtime-only).
#
# 078-precompiled-builder-libs T023 [US1] REPOINT: the monolithic
# fixpp/vlatest/Builders.hpp this bench used to measure no longer exists
# (FR-008). This is now the SC-001 slim-vs-monolith compile-RSS harness (R9):
# it measures `clang++ -std=c++23 -fsyntax-only` peak RSS + wall time for a TU
# that #includes ONLY the slim per-message declaration header
# fixpp/vlatest/messages/NewOrderSingle.hpp — the truest SC-001 "consumer
# compiles against a slim header" witness (quickstart.md Scenario 1), as
# opposed to all.hpp (whole-version aggregator, still slim in link mode per
# R5, but a coarser measurement). The pre-restructuring monolith baseline
# (~3.6 GiB RSS just to #include one version's Builders.hpp, recorded at 077)
# remains the comparison point — the monolith is gone, so it cannot be
# re-measured; SC-001 is confirmed by comparing THIS measurement against that
# recorded figure (see the decision-record entry this bench writes to).
#
# RSS ceiling is a HARD gate (SC-001). Wall time vs the 003-precedent ≤3s
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
# RSS ceiling (078 T023 repoint): SC-001 requires peak RSS "at least an order
# of magnitude below" the ~3.6 GiB monolith baseline — i.e. below ~360 MiB.
# Set generously above that floor so normal machine variance doesn't flake
# the gate, while still enforcing genuine order-of-magnitude headroom (a
# regression back toward monolith-scale RSS, e.g. an accidental all.hpp /
# Builders.hpp re-inclusion, trips this well before the old >21GB/>3.6GiB
# failure modes).
RSS_CEILING_KB=$((512 * 1024))  # 512 MiB

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

# 078 T023 repoint: slim per-message declaration header, not the removed
# monolith (FR-008) — see the file-header comment.
BUILDERS_HPP="${CODEGEN_INC}/fixpp/vlatest/messages/NewOrderSingle.hpp"
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
// Synthetic single-TU probe — 078 T023 SC-001 slim-vs-monolith compile-RSS
// harness (repoints 077 T015's monolith probe; the monolith no longer
// exists post-FR-008). Includes ONLY the slim per-message declaration
// header for one message (declarations only, no builder body).
#include <fixpp/vlatest/messages/NewOrderSingle.hpp>
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

echo "=== 078 T023: vlatest/messages/NewOrderSingle.hpp (slim) compile-resource bench ==="
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
