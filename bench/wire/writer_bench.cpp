// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/wire/writer_bench.cpp
//
// T048 — [2b §6.6] Writer::commit latency harness (seam #5).
//
// [2b §6.6] ceilings:
//   Writer::commit   20-tag message             ≤  80 ns
//   Writer::commit   200-tag Instrument-heavy   ≤ 800 ns
//
// Baseline seed: bench/baselines/wire/writer_bench.json.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string_view>

#include <fixpp/wire/writer.hpp>

namespace {

using fixpp::wire::Writer;

// Helper: write a raw string tag-value field via append_raw.
[[nodiscard]] fixpp::core::expected_t<void>
append_sv(Writer& w, std::uint16_t tag, std::string_view sv) noexcept {
    auto sp = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    return w.append_raw(tag, sp);
}

}  // namespace

// ── BM_Writer_Commit_20tag ────────────────────────────────────────────────────
// Writer::commit on a ~20-tag NewOrderSingle-like message.
// [2b §6.6] ceiling ≤ 80 ns.
static void BM_Writer_Commit_20tag(benchmark::State& state) {
    std::array<std::byte, 8 * 1024> dst{};
    std::array<std::byte, 4 * 1024> scratch{};

    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource scratch_mr{
            scratch.data(), scratch.size(),
            std::pmr::null_memory_resource()};

        Writer w{std::span<std::byte>{dst}, &scratch_mr};

        // BeginString (8) written first — triggers the 9= placeholder injection.
        (void)append_sv(w, 8, "FIX.4.4");
        (void)append_sv(w, 35, "D");
        (void)append_sv(w, 34, "1");
        (void)append_sv(w, 49, "SENDER01");
        (void)append_sv(w, 56, "TARGET01");
        (void)append_sv(w, 52, "20260516-09:30:00.000");
        (void)append_sv(w, 11, "ORD12345678");
        (void)append_sv(w, 55, "AAPL");
        (void)append_sv(w, 54, "1");
        (void)append_sv(w, 38, "100");
        (void)append_sv(w, 40, "2");
        (void)append_sv(w, 44, "150.25");
        (void)append_sv(w, 59, "0");
        (void)append_sv(w, 60, "20260516-09:30:00.000");
        (void)append_sv(w, 1,  "ACC001");
        (void)append_sv(w, 21, "1");
        (void)append_sv(w, 110, "0");
        (void)append_sv(w, 111, "0");
        (void)append_sv(w, 15,  "USD");
        (void)append_sv(w, 58,  "BenchOrder");

        auto r = std::move(w).commit();
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Writer_Commit_20tag);

// ── BM_Writer_Commit_200tag ───────────────────────────────────────────────────
// Writer::commit on a ~200-field Instrument-heavy message (48 MDEntry groups
// × 4 fields each ≈ 200 total fields). Exercises the memmove backpatch path.
// [2b §6.6] ceiling ≤ 800 ns.
static void BM_Writer_Commit_200tag(benchmark::State& state) {
    std::array<std::byte, 64 * 1024> dst{};
    std::array<std::byte, 4 * 1024>  scratch{};

    for (auto _ : state) {
        std::pmr::monotonic_buffer_resource scratch_mr{
            scratch.data(), scratch.size(),
            std::pmr::null_memory_resource()};

        Writer w{std::span<std::byte>{dst}, &scratch_mr};

        (void)append_sv(w, 8,   "FIX.4.4");
        (void)append_sv(w, 35,  "W");
        (void)append_sv(w, 34,  "1");
        (void)append_sv(w, 49,  "SENDER01");
        (void)append_sv(w, 56,  "TARGET01");
        (void)append_sv(w, 52,  "20260516-09:30:00.000");
        (void)append_sv(w, 55,  "AAPL");
        (void)append_sv(w, 268, "48");

        for (int i = 0; i < 48; ++i) {
            (void)append_sv(w, 269, "0");
            (void)append_sv(w, 270, "150.25");
            (void)append_sv(w, 271, "100");
            (void)append_sv(w, 272, "20260516");
        }

        auto r = std::move(w).commit();
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Writer_Commit_200tag);

BENCHMARK_MAIN();
