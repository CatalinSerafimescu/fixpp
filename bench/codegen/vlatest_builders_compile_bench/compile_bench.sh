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
# SC-001 was AMENDED during /implement (2026-07-17, see
# .specify/decisions/078-precompiled-builder-libs-verify.md "Resolution
# (Option 1 ...)" + L-078-1 in spec/behaviors-and-limitations.md): the
# universal order-of-magnitude target is NOT achievable on v50sp2/vlatest
# under 077's value-semantics `Args` (a message's transitive group-plan
# closure must be a complete type at the include site, and the deduped
# group graph is densely connected). SC-001 is now proportional-to-closure:
# v44 meets order-of-magnitude (~17x); v50sp2/vlatest range from ~2.6x
# (group-dense) to ~7.9x (median) below the monolith, materially below but
# not order-of-magnitude. This bench is a decision-record measurement
# (quickstart.md Scenario 1: "not a ctest gate"), so it REPORTS peak RSS/wall
# and does not hard-fail on a fixed ceiling — see the report-only note below.
#
# Wall time vs the 003-precedent <=3s single-version syntax-only ceiling is a
# KNOWN_OVERAGE (recorded, not failed) — mirrors compile_time_bench.sh's
# v50sp2 treatment: a strictly larger generated surface than the legacy
# versions is an accepted, documented cost, not a regression.
#
# Usage:
#   compile_bench.sh <codegen_include_dir> [<cxx_compiler>] [<project_include_dir>]

set -euo pipefail

CODEGEN_INC="${1:-}"
CXX="${2:-clang++}"
SINGLE_CEILING=3
# RSS is REPORT-ONLY (078 verify amendment): SC-001 was restated
# proportional-to-closure — the vlatest/v50sp2 NewOrderSingle closure (233 of
# ~560 plans) measures ~0.88 GiB, well above the old aspirational 512 MiB
# order-of-magnitude ceiling by design, not by regression (see the
# file-header comment / L-078-1). A fixed ceiling can no longer distinguish
# "expected closure-proportional cost" from "genuine regression" for this
# probe, so this bench no longer hard-fails on RSS. REGRESSION_CEILING_KB
# below is an advisory sanity bound only, set ABOVE the largest legitimate
# closure this probe could plausibly reach (the full per-version `groups.hpp`
# umbrella measures 1.573 GiB, `all.hpp` 1.984 GiB — both intentionally
# BELOW this ceiling and would NOT trip it) — it exists only to catch a
# monolith-scale regression (toward the old ~3.6 GiB / >21 GiB failure
# modes), not to distinguish closure sizes within the expected range.
REGRESSION_CEILING_KB=$((2560 * 1024))  # 2.5 GiB advisory (not SC-001 gate)

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
echo "    SC-001 (AMENDED, proportional-to-closure): report-only, not a hard gate."
echo "    Expected range for this probe (NewOrderSingle, common message, 233/558"
echo "    plans): ~0.88 GiB (~4.2x below the ~3.6 GiB monolith). v44 (small group"
echo "    set) separately measures ~0.21 GiB (~17x, order-of-magnitude MET)."
echo "    See .specify/decisions/078-precompiled-builder-libs-verify.md and"
echo "    spec/behaviors-and-limitations.md L-078-1 for the full per-version table."
echo "    advisory regression ceiling: ${REGRESSION_CEILING_KB} KB (gross-regression"
echo "    sanity bound only, NOT the SC-001 criterion)"
echo "    wall ceiling: ${SINGLE_CEILING} s (003 T046 convention, KNOWN_OVERAGE if exceeded)"
echo ""

if [[ -z "${PEAK_RSS_KB}" ]]; then
    echo "FAIL: could not parse peak RSS from /usr/bin/time -v output" >&2
    exit 1
fi

if (( PEAK_RSS_KB > REGRESSION_CEILING_KB )); then
    echo "FAIL: peak RSS ${PEAK_RSS_KB} KB exceeds the ${REGRESSION_CEILING_KB} KB" >&2
    echo "      advisory regression ceiling — this is above even the full" >&2
    echo "      groups.hpp umbrella (1.573 GiB) / all.hpp (1.984 GiB) costs and" >&2
    echo "      suggests a monolith-scale regression (e.g. an accidental" >&2
    echo "      re-inclusion of the pre-078 monolith), not the expected" >&2
    echo "      closure-proportional SC-001 cost." >&2
    exit 1
fi

echo "PASS (report-only): peak RSS recorded, no regression beyond the advisory"
echo "      ceiling. SC-001 adjudication (proportional-to-closure vs the ~3.6 GiB"
echo "      monolith) is made in the decision record, not by this script's exit"
echo "      code — wall-time overage (if any) is a recorded KNOWN_OVERAGE per the"
echo "      003 T046 decision-record convention."
