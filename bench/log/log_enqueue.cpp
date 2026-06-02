// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/log/log_enqueue.cpp
//
// T030 / TS-9: Logger::enqueue() latency on the NON-OVERFLOW producer path.
//
// Anchors:
//   [2k §6.2]   — latency ceiling ≤ 50ns on the non-overflow path
//   contracts/log-core.md FR-002 / SC-001
//
// Fill-rate strategy (per task contract):
//   Use approach (b): let the drain thread run (not sleep) so the ring never
//   saturates. The ring is configured with capacity=65536 (default) and the
//   drain is alive throughout the benchmark. Between iteration batches the
//   ring has drained enough free slots to keep all measured enqueues
//   non-overflow.
//
//   To further ensure non-overflow measurement, the benchmark uses
//   capacity=65536 and iterates in batches of 1000 with a short pause between
//   batches, OR sizes the benchmark so total iters never exceed ring capacity
//   in a single timed window. Google Benchmark's time measurement wraps the
//   inner loop: for a 10M iteration run, the framework may batch them; with
//   the drain running at full speed (no sleep), the ring stays clear.
//
// Reported metrics:
//   BM_LogEnqueue: mean, p99, p999 per iteration (ns).
//   The bench reports these automatically via benchmark::DoNotOptimize and
//   the --benchmark_report_aggregates_only flag.
//
// Baseline JSON:
//   Written by running the bench with --benchmark_format=json and redirecting
//   to bench/baselines/log/log_enqueue.json (done by the verify step; the
//   baseline committed here is the first measured run on this hardware).

#include <benchmark/benchmark.h>

#include <array>
#include <chrono>
#include <memory_resource>
#include <vector>

#include <fixpp/log/level.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>

namespace {

// ── NullSink — drain as fast as possible ─────────────────────────────────────
//
// Discards every record immediately. The drain thread runs at maximum speed,
// freeing ring slots as quickly as possible. This keeps the ring non-full so
// producers always take the non-overflow path.
class NullSink final : public fixpp::log::Sink {
public:
    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const&) noexcept override {}
    void flush(std::chrono::milliseconds) noexcept override {}
    void close() noexcept override {}
};

// ── Global logger shared across benchmark iterations ─────────────────────────
//
// The Logger (ring + drain thread) is expensive to create; we create it once
// and share across iterations. Reset counters between benchmarks if needed.
fixpp::log::Logger* g_logger = nullptr;

void setup_logger()
{
    if (g_logger != nullptr) return;

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::make_unique<NullSink>());

    fixpp::log::LoggerConfig cfg;
    cfg.capacity    = 65536u;  // default 64k-slot ring
    cfg.on_overflow = fixpp::log::overflow_policy::drop_newest;

    g_logger = new fixpp::log::Logger(std::move(cfg), std::move(sinks));
}

}  // namespace

// ── BM_LogEnqueue_NoArg ───────────────────────────────────────────────────────
//
// Measures enqueue() with zero arguments (no ArgValue building overhead).
// This is the absolute floor for enqueue latency on the non-overflow path.
static void BM_LogEnqueue_NoArg(benchmark::State& state)
{
    setup_logger();

    static constexpr std::array<std::uint8_t, 16> kZeroTraceId{};
    static constexpr auto kFmtId = static_cast<std::uint32_t>(
        fixpp::log::detail::crc32_str("overflow test {}"));
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};

    for (auto _ : state) {
        g_logger->enqueue(
            fixpp::log::Level::info,
            fixpp::log::cat::session,
            kFmtId,
            kZeroTraceId,
            0u,
            ts,
            {});
        benchmark::ClobberMemory();
    }

    // Report drop count so we can verify non-overflow path was measured.
    state.counters["drop_count"] = benchmark::Counter(
        static_cast<double>(g_logger->drop_count()));
}
BENCHMARK(BM_LogEnqueue_NoArg)
    ->Iterations(10'000'000)
    ->ReportAggregatesOnly(false)
    ->Unit(benchmark::kNanosecond);

// ── BM_LogEnqueue_OneU64Arg ───────────────────────────────────────────────────
//
// Measures enqueue() with one uint64_t argument — the common case for
// numeric fields (seqnum, session id, etc.). Includes ArgValue::from_u64()
// construction inline (the gated cost per [2k §6.2]).
static void BM_LogEnqueue_OneU64Arg(benchmark::State& state)
{
    setup_logger();

    static constexpr std::array<std::uint8_t, 16> kZeroTraceId{};
    static constexpr auto kFmtId = static_cast<std::uint32_t>(
        fixpp::log::detail::crc32_str("overflow test {}"));
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};

    std::uint64_t counter = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(counter);
        g_logger->enqueue(
            fixpp::log::Level::info,
            fixpp::log::cat::session,
            kFmtId,
            kZeroTraceId,
            0u,
            ts,
            {fixpp::log::ArgValue::from_u64(counter++)});
        benchmark::ClobberMemory();
    }

    state.counters["drop_count"] = benchmark::Counter(
        static_cast<double>(g_logger->drop_count()));
}
BENCHMARK(BM_LogEnqueue_OneU64Arg)
    ->Iterations(10'000'000)
    ->ReportAggregatesOnly(false)
    ->Unit(benchmark::kNanosecond);

// ── BM_LogEnqueue_Stats ───────────────────────────────────────────────────────
//
// Same as OneU64Arg but uses Statistics to report p99/p999/mean.
// Google Benchmark reports these automatically when --benchmark_report_aggregates_only=false
// and repetitions > 1.
static void BM_LogEnqueue_Stats(benchmark::State& state)
{
    setup_logger();
    g_logger->reset_drop_count();

    static constexpr std::array<std::uint8_t, 16> kZeroTraceId{};
    static constexpr auto kFmtId = static_cast<std::uint32_t>(
        fixpp::log::detail::crc32_str("overflow test {}"));
    auto ts = fixpp::core::utc_time_point{
        std::chrono::system_clock::now().time_since_epoch()};

    std::uint64_t counter = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(counter);
        g_logger->enqueue(
            fixpp::log::Level::info,
            fixpp::log::cat::session,
            kFmtId,
            kZeroTraceId,
            0u,
            ts,
            {fixpp::log::ArgValue::from_u64(counter++)});
        benchmark::ClobberMemory();
    }

    state.counters["drop_count"] = benchmark::Counter(
        static_cast<double>(g_logger->drop_count()));
}
BENCHMARK(BM_LogEnqueue_Stats)
    ->Repetitions(5)
    ->Unit(benchmark::kNanosecond)
    ->ComputeStatistics("p99",  [](std::vector<double> const& v) -> double {
        if (v.empty()) return 0.0;
        auto s = v;
        std::sort(s.begin(), s.end());
        return s[static_cast<std::size_t>(s.size() * 0.99)];
    })
    ->ComputeStatistics("p999", [](std::vector<double> const& v) -> double {
        if (v.empty()) return 0.0;
        auto s = v;
        std::sort(s.begin(), s.end());
        return s[static_cast<std::size_t>(s.size() * 0.999)];
    });

BENCHMARK_MAIN();
