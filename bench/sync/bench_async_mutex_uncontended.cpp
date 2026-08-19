// SPDX-License-Identifier: AGPL-3.0-or-later
// 006-async-mutex — seam #1 uncontended-acquire latency bench.
// [2f §6.3 row 1] async_lock uncontended (single CAS fast path) ≤ 20–25 ns;
// [2f §6.3 row 3] unlock uncontended (empty LIFO) ≤ 15 ns.
//
// T071: real workloads (replaces the Phase-1 placeholder). Warm-cache,
// single-threaded io_context, manual per-operation timing. The ±5% gate vs
// bench/baselines/sync/async_mutex_baselines.json is SOFT per [const §VIII.2].
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
constexpr int kCycles = 200'000;

// Row 1: uncontended async_lock fast path + implicit unlock (guard destruct).
// One coroutine, no contender, so every async_lock takes the not_locked ->
// locked_no_waiters CAS and returns without suspending.
void BM_AsyncMutex_AsyncLock_Uncontended(benchmark::State& state) {
    for (auto _ : state) {
        asio::io_context ioc;
        fixpp::sync::async_mutex m;
        auto t0 = bench_clock::now();
        asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
            for (int i = 0; i < kCycles; ++i) {
                auto g = co_await m.async_lock();
                benchmark::DoNotOptimize(&g);
                // g destructs here -> unlock (empty LIFO)
                // Trampoline: the uncontended fast path completes inline, so
                // asio resumes this coroutine without unwinding — unwind the
                // stack periodically (amortized ~0.02 ns/op).
                if ((i & 1023) == 0)
                    co_await asio::post(co_await asio::this_coro::executor,
                                        asio::use_awaitable);
            }
            co_return;
        }, asio::detached);
        ioc.run();
        auto t1 = bench_clock::now();
        state.SetIterationTime(
            std::chrono::duration<double>(t1 - t0).count() / kCycles);
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
BENCHMARK(BM_AsyncMutex_AsyncLock_Uncontended)->UseManualTime()->Iterations(3);

// Row 3: unlock uncontended in isolation. Acquire (fast path), disengage the
// guard via release(), then time only the explicit unlock() on the empty LIFO.
void BM_AsyncMutex_Unlock_Uncontended(benchmark::State& state) {
    for (auto _ : state) {
        asio::io_context ioc;
        fixpp::sync::async_mutex m;
        double accum = 0.0;
        asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
            for (int i = 0; i < kCycles; ++i) {
                auto g = co_await m.async_lock();
                auto* mm = g->release();         // disengage; no unlock yet
                auto t0 = bench_clock::now();
                mm->unlock();                    // timed: empty-LIFO unlock
                auto t1 = bench_clock::now();
                accum += std::chrono::duration<double>(t1 - t0).count();
                if ((i & 1023) == 0)
                    co_await asio::post(co_await asio::this_coro::executor,
                                        asio::use_awaitable);
            }
            co_return;
        }, asio::detached);
        ioc.run();
        state.SetIterationTime(accum / kCycles);
    }
}
BENCHMARK(BM_AsyncMutex_Unlock_Uncontended)->UseManualTime()->Iterations(3);

}  // namespace

BENCHMARK_MAIN();
