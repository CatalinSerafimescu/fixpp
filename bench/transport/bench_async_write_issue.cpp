// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// bench/transport/bench_async_write_issue.cpp
// T022 — [2h §6.3] row 2 + [2h §9 seam #3]
//
// Write-path issue latency on the session strand under warm-cache conditions.
//
// Target: ≤ 200 ns p99 (write-path issue on strand).
// CI failure: > 5% regression per [const §VIII.2].
//
// DEPENDENCY: asio_tls_transport (T026) + loopback peer.
// Phase 3a: scaffold only. T029 wires this target after impl ships.

#include <benchmark/benchmark.h>

#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>

#include <array>
#include <cstddef>
#include <cstring>

namespace {

// A single FIX frame (pre-built, ~80 bytes) used for every write iteration.
// Approximate content: 35=D NewOrderSingle.
inline std::array<std::byte, 84> make_fix_frame() {
    constexpr char kFrameStr[] =
        "8=FIX.4.4\x01" "9=60\x01"
        "35=D\x01" "49=SENDER\x01" "56=TARGET\x01"
        "34=1\x01" "52=20260527-09:30:00\x01"
        "10=123\x01";
    std::array<std::byte, 84> frame{};
    std::memcpy(frame.data(), kFrameStr,
                std::min(sizeof(kFrameStr) - 1, frame.size()));
    return frame;
}

const auto kFrame = make_fix_frame();

// ─────────────────────────────────────────────────────────────────────────────
// BM_AsyncWriteIssue
//
// Measures the dispatch overhead from the strand-scheduled write coroutine
// entry to the completion of async_write on the session strand (warm-cache,
// peer is a fast consumer — measures issue latency, not end-to-end RTT).
//
// Counters: p50, p95, p99 (ns).
// ─────────────────────────────────────────────────────────────────────────────

class AsyncWriteFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State& /*state*/) override {
        // TODO (T029): construct asio_tls_transport + fast loopback consumer.
        // Consumer discards all received bytes immediately (measures issue, not RTT).
    }

    void TearDown(const benchmark::State& /*state*/) override {
        // TODO (T029): close transport + consumer.
    }
};

BENCHMARK_DEFINE_F(AsyncWriteFixture, WriteIssue)(benchmark::State& state) {
    // TODO (T029): replace with real async_write coroutine dispatch.
    // Each iteration:
    //   - co_await transport.async_write(std::span{kFrame})
    //   - benchmark::DoNotOptimize(result)
    for (auto _ : state) {
        benchmark::DoNotOptimize(kFrame.data());
    }

    state.counters["p50_ns"] = 0.0;
    state.counters["p95_ns"] = 0.0;
    state.counters["p99_ns"] = 0.0;
}

BENCHMARK_REGISTER_F(AsyncWriteFixture, WriteIssue)
    ->Iterations(1'000'000)
    ->Unit(benchmark::kNanosecond);

}  // namespace

BENCHMARK_MAIN();
