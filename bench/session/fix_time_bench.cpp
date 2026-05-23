// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/fix_time_bench.cpp
//
// 005-session-establishment-fsm T061 / Phase 8 — Tier-1 latency benches for
// the folded core/ time-helper #4 (E8 / D-3).
//
// Ceilings (plan.md Technical-Context, [2e §6.6] / [2d §6.3]; warm cache,
// Linux/Clang/x86_64 release `-O2`):
//   BM_Session_UtcTimeToFixString  ≤ 60 ns  (ms precision, fixed buffer)
//   BM_Session_FixStringToUtcTime  ≤ 80 ns  (21-byte UTCTimestamp)
//
// ±5% regression gate per [const §VIII.2]; baselines under
// bench/baselines/session/fix_time_baseline.json.

#include <benchmark/benchmark.h>

#include <array>
#include <chrono>
#include <fixpp/core/fix_time.hpp>
#include <span>
#include <string_view>

namespace {

using fixpp::core::fix_string_to_utc_time;
using fixpp::core::fix_time_precision;
using fixpp::core::utc_time_point;
using fixpp::core::utc_time_to_fix_string;

// A fixed timestamp the format/parse benches share — 2026-05-22T10:30:45.123Z.
constexpr auto kFixedTp = utc_time_point{std::chrono::milliseconds{1779785445123LL}};

void BM_Session_UtcTimeToFixString(benchmark::State& state) {
    std::array<char, 32> buf{};
    const auto tp = kFixedTp;
    for (auto _ : state) {
        auto r = utc_time_to_fix_string(tp, fix_time_precision::millis, buf);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Session_UtcTimeToFixString);

void BM_Session_FixStringToUtcTime(benchmark::State& state) {
    // 21-byte ms-precision FIX UTCTimestamp (matches the format-side default).
    constexpr std::string_view kInput = "20260522-10:30:45.123";
    for (auto _ : state) {
        auto r = fix_string_to_utc_time(std::span<const char>{kInput.data(), kInput.size()});
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Session_FixStringToUtcTime);

}  // namespace
