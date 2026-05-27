// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// bench/transport/bench_async_read_some_dispatch.cpp
// T021 — [2h §6.3] row 1 + [2h §9 seam #2]
//
// Read-path completion-handler dispatch latency under warm-cache HALO conditions.
//
// Target: ≤ 200 ns p99 (transport-side read dispatch).
// CI failure: > 5% regression per [const §VIII.2].
//
// Metric strategy: BENCHMARK_DEFINE_F + benchmark::Counters reporting p50/p95/p99
// estimated from the benchmark's timing histogram.
//
// DEPENDENCY: asio_tls_transport (T026) + loopback peer.
// Phase 3a: scaffold only — compiles when benchmark::benchmark is available.
// T029 wires this target after asio_tls_transport ships.
//
// NOTE: This bench cannot run meaningfully without the concrete transport impl.
// The BENCHMARK_DEFINE_F bodies are documented-intent only; the impl bodies
// are filled in by T029.

#include <benchmark/benchmark.h>

#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>

#include <array>
#include <cstddef>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// BM_AsyncReadSomeDispatch
//
// Measures the dispatch overhead from the completion-handler entry point to
// the return of async_read_some on the session strand (warm-cache, no I/O
// blocking — peer sends a pre-built frame on each iteration).
//
// Counters: p50, p95, p99 (ns) estimated from iteration timing distribution.
// ─────────────────────────────────────────────────────────────────────────────

class AsyncReadSomeFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State& /*state*/) override {
        // TODO (T029): construct asio_tls_transport + loopback peer.
        // Pre-load peer side with PREFILL frames so each iteration has a
        // frame ready to read — avoids network latency in the measured window.
    }

    void TearDown(const benchmark::State& /*state*/) override {
        // TODO (T029): close transport + peer.
    }

    // Read buffer — 256 KiB (matches Transport::Config::max_read_window_bytes).
    std::array<std::byte, 256 * 1024> read_buf_{};
};

BENCHMARK_DEFINE_F(AsyncReadSomeFixture, ReadDispatch)(benchmark::State& state) {
    // TODO (T029): replace with real async_read_some coroutine dispatch.
    // Each iteration:
    //   - Peer has one pre-loaded frame.
    //   - co_await transport.async_read_some(std::span{read_buf_})
    //   - benchmark::DoNotOptimize(result)
    //
    // For Phase 3a this benchmark body is a placeholder that compiles.
    // The benchmark must NOT run and report ≤200ns until T029 lands.
    for (auto _ : state) {
        benchmark::DoNotOptimize(read_buf_.data());
    }

    // Report p50/p95/p99 counters. Populated from iteration timing in T029.
    state.counters["p50_ns"] = 0.0;
    state.counters["p95_ns"] = 0.0;
    state.counters["p99_ns"] = 0.0;
}

// Register with ≥ 1M iterations.
BENCHMARK_REGISTER_F(AsyncReadSomeFixture, ReadDispatch)
    ->Iterations(1'000'000)
    ->Unit(benchmark::kNanosecond);

}  // namespace

BENCHMARK_MAIN();
