// SPDX-License-Identifier: AGPL-3.0-or-later
// 006-async-mutex — seam #2 contended-enqueue latency bench.
// [2f §6.3 row 2] async_lock contended (waiter suspends; LIFO push + slot
// bind) ≤ 80 ns; #12 dispatch-vs-post completion-policy delta.
//
// T071: real workloads (replaces the Phase-1 placeholder). The ±5% gate vs
// bench/baselines/sync/async_mutex_baselines.json is SOFT per [const §VIII.2].
// Note (Erratum E-3): 2f waiter resumption is ALWAYS posted regardless of
// completion_policy, so the dispatch-vs-post delta is expected ≈ 0 (the policy
// is preserved as a semantic knob only) — measured here for the record.
#include <benchmark/benchmark.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <chrono>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using bench_clock = std::chrono::steady_clock;
constexpr int kHandoffs = 100'000;

// Two coroutines ping-pong the mutex: while A holds, B's async_lock observes
// locked_no_waiters and must take the contended path (waiter_record alloc +
// LIFO push + slot bind + suspend); A's unlock grants B and resumes it. Each
// loop iteration is one full contended acquire→suspend→grant→resume cycle.
double contended_cycle_seconds(fixpp::sync::completion_policy pol) {
    asio::io_context ioc;
    fixpp::sync::async_mutex m{pol};
    auto t0 = bench_clock::now();
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        for (int i = 0; i < kHandoffs; ++i) {
            auto g = co_await m.async_lock();
            benchmark::DoNotOptimize(&g);
            if ((i & 1023) == 0)
                co_await asio::post(co_await asio::this_coro::executor,
                                    asio::use_awaitable);
        }
        co_return;
    }, asio::detached);
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        for (int i = 0; i < kHandoffs; ++i) {
            auto g = co_await m.async_lock();
            benchmark::DoNotOptimize(&g);
            if ((i & 1023) == 0)
                co_await asio::post(co_await asio::this_coro::executor,
                                    asio::use_awaitable);
        }
        co_return;
    }, asio::detached);
    ioc.run();
    auto t1 = bench_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

// Row 2: contended async_lock (the suspend path), default dispatch policy.
void BM_AsyncMutex_AsyncLock_Contended(benchmark::State& state) {
    for (auto _ : state) {
        double s = contended_cycle_seconds(
            fixpp::sync::completion_policy::dispatch);
        state.SetIterationTime(s / (2 * kHandoffs));
    }
}
// ⚠️ `->Iterations(3)` IS LOAD-BEARING, NOT A TUNING CHOICE. These benchmarks
// report a PER-OPERATION time — `SetIterationTime(elapsed / kCycles)` — while one
// `state` iteration really costs kCycles * that. Google-Benchmark's auto-tuner
// grows the iteration count until the REPORTED time reaches `--benchmark_min_time`
// (default 0.5 s), so it is told ~25 ns and concludes it needs ~2e7 iterations of
// something that actually costs ~5 ms each: **~28 h per repetition, ~83 h at the
// suite's --benchmark_repetitions=3.** Pinning the count disables that tuning.
//
// This is not theoretical. #209 made the bench suite run in CI for the first time
// (the job previously executed only `placeholder_bench`), and the first real run
// parked here for over an hour before being killed. It is also why this file's
// baseline, `bench/baselines/sync/async_mutex_baselines.json`, is hand-authored
// with a partial schema and carries `none:` in bench/ci-suite.txt — the benchmark
// could never be machine-run, and the schema gap was the symptom.
//
// bench/threading/bench_threading.cpp uses the identical divisor pattern and got
// this right from the start: all eight of its registrations carry ->Iterations(3).
BENCHMARK(BM_AsyncMutex_AsyncLock_Contended)->UseManualTime()->Iterations(3);

// #12: |post − dispatch| per-handoff delta (expected ≈ 0 post-E-3).
void BM_AsyncMutex_DispatchVsPost_Delta(benchmark::State& state) {
    for (auto _ : state) {
        double d = contended_cycle_seconds(
            fixpp::sync::completion_policy::dispatch);
        double p = contended_cycle_seconds(
            fixpp::sync::completion_policy::post);
        double delta = (p - d) / (2 * kHandoffs);
        if (delta < 0) delta = -delta;
        state.SetIterationTime(delta);
    }
}
BENCHMARK(BM_AsyncMutex_DispatchVsPost_Delta)->UseManualTime()->Iterations(3);

}  // namespace

BENCHMARK_MAIN();
