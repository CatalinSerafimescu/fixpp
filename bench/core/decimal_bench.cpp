// bench/core/decimal_bench.cpp
// Seam #5 — Google Benchmark: BM_decimal_parse, BM_decimal_format, BM_decimal_compare
// Regression bars: parse ≤50 ns, format ≤30 ns, compare ≤20 ns median (5-digit mantissa workload).

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>

#include "fixpp/core/decimal.hpp"

using fixpp::core::decimal_traits;
using fixpp::core::pod_decimal;

namespace {

// 5-digit mantissa workload: "12345" (6 bytes)
constexpr const char kParseInput[] = "12345";
constexpr std::size_t kParseLen = sizeof(kParseInput) - 1;

}  // namespace

static void BM_decimal_parse(benchmark::State& state) {
    std::array<std::byte, kParseLen> src{};
    for (std::size_t i = 0; i < kParseLen; ++i) src[i] = static_cast<std::byte>(kParseInput[i]);
    for (auto _ : state) {
        auto r = decimal_traits<pod_decimal>::from_chars(std::span<const std::byte>{src},
                                                        std::pmr::null_memory_resource());
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_decimal_parse);

static void BM_decimal_format(benchmark::State& state) {
    pod_decimal v{12345, 0};
    std::array<std::byte, 64> dst{};

    for (auto _ : state) {
        auto r = decimal_traits<pod_decimal>::to_chars(v, std::span<std::byte>{dst});
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_decimal_format);

// Compare regime 1: same exponent — hits the §6.3 step-4 fast path (direct
// signed-mantissa compare). Dominant FX workload.
static void BM_decimal_compare(benchmark::State& state) {
    pod_decimal a{12345, 0};
    pod_decimal b{12346, 0};

    for (auto _ : state) {
        auto r = decimal_traits<pod_decimal>::compare(a, b);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_decimal_compare);

// Compare regime 2: same magnitude bucket, different exponents — hits the §6.3
// step-4 slow path (digit-string compare after canonicalization). Previously
// unsampled; a future perf regression on this path would silently pass
// BM_decimal_compare (same-exp) while degrading real traffic.
// {12345, 0} == {1234500, -2} == 12345; same value, different exponent.
static void BM_decimal_compare_diff_exp(benchmark::State& state) {
    pod_decimal a{12345, 0};
    pod_decimal b{1234500, -2};

    for (auto _ : state) {
        auto r = decimal_traits<pod_decimal>::compare(a, b);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_decimal_compare_diff_exp);

// Compare regime 3: different magnitude buckets — §6.3 step-3 early-exit
// (digit_count + exponent determines the winner before digit-string compare).
// {1, 0} = 1; {1, -3} = 0.001; clearly different magnitudes.
static void BM_decimal_compare_diff_bucket(benchmark::State& state) {
    pod_decimal a{1, 0};
    pod_decimal b{1, -3};

    for (auto _ : state) {
        auto r = decimal_traits<pod_decimal>::compare(a, b);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_decimal_compare_diff_bucket);

BENCHMARK_MAIN();
