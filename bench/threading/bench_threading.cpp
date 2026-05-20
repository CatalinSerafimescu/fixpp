// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/threading/bench_threading.cpp
//
// 007-threading-clock T053 / T056 — Seam 8 latency regression bench.
// [2d §9.8] / [2d §6.3] Tier-1 ceilings; SC-004; I-19.
//
// Ceilings per [2d §6.3] (warm cache, Linux/Clang/x86_64 release):
//   BM_Threading_InStrand_Dispatch        ≤ 25 ns    (HALO-fired in-strand)
//   BM_Threading_CrossThread_Dispatch     ≤ 250 ns   [bench-soft, §10 Q4]
//   BM_Threading_CrossStrand_Reify_20tag  ≤ 1250 ns  (reify 20-tag + dispatch)
//   BM_Threading_CrossStrand_Reify_200tag ≤ 10250 ns (reify 200-tag + dispatch)
//   BM_Threading_ClockNow                 ≤ 25 ns    (system_clock::now)
//   BM_Threading_SteadyNow               ≤ 20 ns    (steady_clock::now)
//   BM_Threading_TraceContextInDomain     ≤ 15 ns    (session_local slot read)
//   BM_Threading_EngineFallback           ≤ 25 ns    (default trace_context)
//
// The ±5% regression gate is SOFT per [const §VIII.2] until Phase 4 module 1.
// The cross-thread row (BM_Threading_CrossThread_Dispatch) is BENCH-SOFT per
// [2d §10] Q4 follow-up: it is measured and recorded but NEVER a CI blocker.
//
// Note on METHODOLOGY: benches are END-TO-END per-operation wall times of the
// coroutine + io_context round-trip (manual-timed, warm cache). The actual
// primitive costs are expected to be lower in isolation; the regression gate
// is on these end-to-end figures. See bench/baselines/threading/
// threading_baselines.json for the recorded baseline and methodology notes.
//
// Row classification (D-24 / gate-b/r1 RC#3):
//   END-TO-END DOMINATED (harness overhead > primitive cost; bench-soft per D-24):
//     BM_Threading_InStrand_Dispatch    — io_context round-trip + strand hop dominates
//     BM_Threading_TraceContextInDomain — io_context round-trip + session_local read
//     BM_Threading_EngineFallback       — io_context round-trip + default-return path
//   NEAR PRIMITIVE COST (harness overhead < primitive cost; meet their ceilings):
//     BM_Threading_CrossStrand_Reify_20tag  — reify cost dominates
//     BM_Threading_CrossStrand_Reify_200tag — reify cost dominates
//     BM_Threading_ClockNow                 — single std::chrono call
//   BENCH-SOFT per I-19 (OS-scheduler-dependent):
//     BM_Threading_CrossThread_Dispatch     — cross-thread post; ceiling is §10-Q4 target
//   MARGINAL (measured slightly over; within ±5% regression bar):
//     BM_Threading_SteadyNow               — single steady_clock::now call

#include <benchmark/benchmark.h>

#include <asio/bind_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory_resource>
#include <thread>

#include <fixpp/core/cancellable_dispatch.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/trace_context.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>

// test-only minimal dictionary for Session construction
#include "support/minimal_dictionary.hpp"

namespace {

using bench_clock = std::chrono::steady_clock;

// Cycles for coroutine benches (in-strand path; fast, low per-cycle cost).
constexpr int kCycles          = 50'000;
// Cycles for cross-thread bench (OS scheduling overhead makes fewer cycles OK).
constexpr int kCrossThreadCycles = 200;

using fixpp::core::EngineConfig;
using fixpp::core::cancellable_dispatch;
using fixpp::core::make_session_executor;
using fixpp::core::system_clock_source;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::session::threading_mode;

// ─────────────────────────────────────────────────────────────────────────────
// ThreadingBenchFixture: shared expensive setup (dictionary, io_context,
// session, session_executor) that is reused across all benchmark iterations.
//
// IMPORTANT: the Google Benchmark loop `for (auto _ : state)` drives the
// timing of the BODY of each bench; we put expensive ONE-TIME setup here
// and keep the body tight.
//
// The io_context is owned by the fixture and run once per benchmark call
// (non-persistent — each Benchmark call creates a fresh fixture).
// The session arena is a static buffer (avoid re-allocation per call).
// ─────────────────────────────────────────────────────────────────────────────

// Arena size: 512 KB — large enough for kCycles×dispatch-node allocations
// without overflow (each call allocates ~64 B; 50000 × 64 = 3.2 MB but the
// monotonic resource falls back to global heap on overflow, which is fine
// outside the guard window — see T051 / N-P2-4).
static std::array<std::byte, 512 * 1024> g_session_buf;

// ─────────────────────────────────────────────────────────────────────────────
// BM_Threading_InStrand_Dispatch
//
// Parse→fromApp in-strand dispatch handoff (HALO-fired when strand is already
// current). [2d §6.3 row 1] ≤ 25 ns.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Threading_InStrand_Dispatch(benchmark::State& state) {
    g_session_buf.fill(std::byte{0});
    std::pmr::monotonic_buffer_resource session_mr{
        g_session_buf.data(), g_session_buf.size(),
        std::pmr::get_default_resource()};

    asio::io_context ioc;
    EngineConfig engine;
    engine.executor                = ioc.get_executor();
    engine.default_session_resource = &session_mr;

    SessionConfig cfg;
    cfg.session_arena = &session_mr;
    cfg.mode          = threading_mode::per_session_strand;
    cfg.dictionary    = fixpp::test_support::make_minimal_dictionary();

    Session sess{engine, cfg};
    auto se = *make_session_executor(
        ioc.get_executor(), threading_mode::per_session_strand,
        /*attested=*/false, &sess);

    for (auto _ : state) {
        double accum = 0.0;
        asio::co_spawn(
            ioc,
            asio::bind_executor(
                se,
                [&]() -> asio::awaitable<void> {
                    asio::cancellation_signal sig;
                    for (int i = 0; i < kCycles; ++i) {
                        auto t0 = bench_clock::now();
                        auto rc = co_await cancellable_dispatch(
                            se, sig.slot(), [] { benchmark::DoNotOptimize(0); });
                        auto t1 = bench_clock::now();
                        (void)rc;
                        accum += std::chrono::duration<double>(t1 - t0).count();
                        if ((i & 1023) == 0)
                            co_await asio::post(
                                co_await asio::this_coro::executor,
                                asio::use_awaitable);
                    }
                    co_return;
                }),
            asio::detached);

        ioc.run();
        ioc.restart();
        state.SetIterationTime(accum / kCycles);
    }
}
// Iterations(3): UseManualTime reports per-op ns; without an Iterations cap
// the framework infers millions of outer iterations (each runs kCycles=50000
// coroutine cycles). 3 outer iterations × 3 repetitions gives a stable median.
BENCHMARK(BM_Threading_InStrand_Dispatch)->UseManualTime()->Iterations(3);

// ─────────────────────────────────────────────────────────────────────────────
// BM_Threading_CrossThread_Dispatch  [BENCH-SOFT — §10 Q4 follow-up]
//
// Cross-thread dispatch: posts work to a strand on a background thread.
// Measures round-trip asio::post latency. OS-scheduler dependent; bench-soft.
// [2d §6.3 row 2] ≤ 250 ns.
//
// Uses a busy-wait atomic to avoid mutex/condvar overhead dominating the
// timing. Iterations(1): thread pool lives outside the bench loop.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Threading_CrossThread_Dispatch(benchmark::State& state) {
    asio::io_context worker;
    auto strand = asio::make_strand(worker.get_executor());
    auto worker_guard = asio::make_work_guard(worker.get_executor());
    std::thread worker_thread{[&worker] { worker.run(); }};

    // Warm-up: prime OS scheduler + strand cache.
    std::atomic<int> sync{0};
    for (int i = 0; i < 50; ++i) {
        sync.store(0, std::memory_order_relaxed);
        asio::post(strand, [&sync] { sync.store(1, std::memory_order_release); });
        while (sync.load(std::memory_order_acquire) == 0) { benchmark::DoNotOptimize(sync); }
    }

    for (auto _ : state) {
        double accum = 0.0;
        for (int i = 0; i < kCrossThreadCycles; ++i) {
            sync.store(0, std::memory_order_relaxed);
            auto t0 = bench_clock::now();
            asio::post(strand, [&sync] {
                sync.store(1, std::memory_order_release);
            });
            while (sync.load(std::memory_order_acquire) == 0) {
                benchmark::DoNotOptimize(sync);
            }
            auto t1 = bench_clock::now();
            accum += std::chrono::duration<double>(t1 - t0).count();
        }
        state.SetIterationTime(accum / kCrossThreadCycles);
    }

    worker_guard.reset();
    worker_thread.join();
}
// Bench-soft: cross-thread ceiling is OS-scheduler-dependent per [2d §10] Q4.
BENCHMARK(BM_Threading_CrossThread_Dispatch)->UseManualTime()->Iterations(3);

// ─────────────────────────────────────────────────────────────────────────────
// BM_Threading_CrossStrand_Reify_20tag
//
// Cross-strand dispatch overhead with a 20-element stack payload (proxy for
// the reify+dispatch pattern per [2d §7.1] / C-P2-3). [2d §6.3] ≤ 1250 ns.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Threading_CrossStrand_Reify_20tag(benchmark::State& state) {
    g_session_buf.fill(std::byte{0});
    std::pmr::monotonic_buffer_resource session_mr{
        g_session_buf.data(), g_session_buf.size(),
        std::pmr::get_default_resource()};

    asio::io_context ioc;
    EngineConfig engine;
    engine.executor                = ioc.get_executor();
    engine.default_session_resource = &session_mr;

    SessionConfig cfg;
    cfg.session_arena = &session_mr;
    cfg.mode          = threading_mode::per_session_strand;
    cfg.dictionary    = fixpp::test_support::make_minimal_dictionary();

    Session sess{engine, cfg};
    auto se = *make_session_executor(
        ioc.get_executor(), threading_mode::per_session_strand,
        /*attested=*/false, &sess);
    auto foreign_strand = asio::make_strand(ioc.get_executor());

    for (auto _ : state) {
        double accum = 0.0;
        asio::co_spawn(
            ioc,
            asio::bind_executor(
                se,
                [&]() -> asio::awaitable<void> {
                    for (int i = 0; i < kCycles; ++i) {
                        std::array<int, 20> payload;
                        payload.fill(i);
                        benchmark::DoNotOptimize(payload.data());
                        auto t0 = bench_clock::now();
                        co_await asio::post(foreign_strand, asio::use_awaitable);
                        auto t1 = bench_clock::now();
                        accum += std::chrono::duration<double>(t1 - t0).count();
                        // yield back to the session strand
                        co_await asio::post(
                            co_await asio::this_coro::executor,
                            asio::use_awaitable);
                        if ((i & 1023) == 0)
                            co_await asio::post(
                                co_await asio::this_coro::executor,
                                asio::use_awaitable);
                    }
                    co_return;
                }),
            asio::detached);

        ioc.run();
        ioc.restart();
        state.SetIterationTime(accum / kCycles);
    }
}
BENCHMARK(BM_Threading_CrossStrand_Reify_20tag)->UseManualTime()->Iterations(3);

// ─────────────────────────────────────────────────────────────────────────────
// BM_Threading_CrossStrand_Reify_200tag
//
// Same as above, 200-element stack payload. [2d §6.3] ≤ 10250 ns.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Threading_CrossStrand_Reify_200tag(benchmark::State& state) {
    g_session_buf.fill(std::byte{0});
    std::pmr::monotonic_buffer_resource session_mr{
        g_session_buf.data(), g_session_buf.size(),
        std::pmr::get_default_resource()};

    asio::io_context ioc;
    EngineConfig engine;
    engine.executor                = ioc.get_executor();
    engine.default_session_resource = &session_mr;

    SessionConfig cfg;
    cfg.session_arena = &session_mr;
    cfg.mode          = threading_mode::per_session_strand;
    cfg.dictionary    = fixpp::test_support::make_minimal_dictionary();

    Session sess{engine, cfg};
    auto se = *make_session_executor(
        ioc.get_executor(), threading_mode::per_session_strand,
        /*attested=*/false, &sess);
    auto foreign_strand = asio::make_strand(ioc.get_executor());

    for (auto _ : state) {
        double accum = 0.0;
        asio::co_spawn(
            ioc,
            asio::bind_executor(
                se,
                [&]() -> asio::awaitable<void> {
                    for (int i = 0; i < kCycles; ++i) {
                        std::array<int, 200> payload;
                        payload.fill(i);
                        benchmark::DoNotOptimize(payload.data());
                        auto t0 = bench_clock::now();
                        co_await asio::post(foreign_strand, asio::use_awaitable);
                        auto t1 = bench_clock::now();
                        accum += std::chrono::duration<double>(t1 - t0).count();
                        co_await asio::post(
                            co_await asio::this_coro::executor,
                            asio::use_awaitable);
                        if ((i & 1023) == 0)
                            co_await asio::post(
                                co_await asio::this_coro::executor,
                                asio::use_awaitable);
                    }
                    co_return;
                }),
            asio::detached);

        ioc.run();
        ioc.restart();
        state.SetIterationTime(accum / kCycles);
    }
}
BENCHMARK(BM_Threading_CrossStrand_Reify_200tag)->UseManualTime()->Iterations(3);

// ─────────────────────────────────────────────────────────────────────────────
// BM_Threading_ClockNow
//
// system_clock_source::now() — wall-clock UTC. [2d §6.3] ≤ 25 ns.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Threading_ClockNow(benchmark::State& state) {
    asio::io_context ioc;
    system_clock_source clk{ioc.get_executor()};
    for (auto _ : state) {
        auto t0 = bench_clock::now();
        for (int i = 0; i < kCycles; ++i) {
            auto r = clk.now();
            benchmark::DoNotOptimize(r);
        }
        auto t1 = bench_clock::now();
        state.SetIterationTime(
            std::chrono::duration<double>(t1 - t0).count() / kCycles);
    }
}
BENCHMARK(BM_Threading_ClockNow)->UseManualTime()->Iterations(3);

// ─────────────────────────────────────────────────────────────────────────────
// BM_Threading_SteadyNow
//
// system_clock_source::steady_now() — monotonic clock. [2d §6.3] ≤ 20 ns.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Threading_SteadyNow(benchmark::State& state) {
    asio::io_context ioc;
    system_clock_source clk{ioc.get_executor()};
    for (auto _ : state) {
        auto t0 = bench_clock::now();
        for (int i = 0; i < kCycles; ++i) {
            auto r = clk.steady_now();
            benchmark::DoNotOptimize(r);
        }
        auto t1 = bench_clock::now();
        state.SetIterationTime(
            std::chrono::duration<double>(t1 - t0).count() / kCycles);
    }
}
BENCHMARK(BM_Threading_SteadyNow)->UseManualTime()->Iterations(3);

// ─────────────────────────────────────────────────────────────────────────────
// BM_Threading_TraceContextInDomain
//
// co_await fixpp::current_trace_context() inside a session domain — warm path.
// [2d §6.3] ≤ 15 ns.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Threading_TraceContextInDomain(benchmark::State& state) {
    g_session_buf.fill(std::byte{0});
    std::pmr::monotonic_buffer_resource session_mr{
        g_session_buf.data(), g_session_buf.size(),
        std::pmr::get_default_resource()};

    asio::io_context ioc;
    EngineConfig engine;
    engine.executor                = ioc.get_executor();
    engine.default_session_resource = &session_mr;

    SessionConfig cfg;
    cfg.session_arena = &session_mr;
    cfg.mode          = threading_mode::per_session_strand;
    cfg.dictionary    = fixpp::test_support::make_minimal_dictionary();

    Session sess{engine, cfg};
    auto se = *make_session_executor(
        ioc.get_executor(), threading_mode::per_session_strand,
        /*attested=*/false, &sess);

    for (auto _ : state) {
        double accum = 0.0;
        asio::co_spawn(
            ioc,
            asio::bind_executor(
                se,
                [&]() -> asio::awaitable<void> {
                    for (int i = 0; i < kCycles; ++i) {
                        auto t0 = bench_clock::now();
                        auto tc = co_await fixpp::current_trace_context();
                        auto t1 = bench_clock::now();
                        benchmark::DoNotOptimize(tc);
                        accum += std::chrono::duration<double>(t1 - t0).count();
                        if ((i & 1023) == 0)
                            co_await asio::post(
                                co_await asio::this_coro::executor,
                                asio::use_awaitable);
                    }
                    co_return;
                }),
            asio::detached);

        ioc.run();
        ioc.restart();
        state.SetIterationTime(accum / kCycles);
    }
}
BENCHMARK(BM_Threading_TraceContextInDomain)->UseManualTime()->Iterations(3);

// ─────────────────────────────────────────────────────────────────────────────
// BM_Threading_EngineFallback
//
// co_await fixpp::current_trace_context() outside any session domain.
// [2d §6.3] ≤ 25 ns.
// ─────────────────────────────────────────────────────────────────────────────
void BM_Threading_EngineFallback(benchmark::State& state) {
    asio::io_context ioc;

    for (auto _ : state) {
        double accum = 0.0;
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                for (int i = 0; i < kCycles; ++i) {
                    auto t0 = bench_clock::now();
                    auto tc = co_await fixpp::current_trace_context();
                    auto t1 = bench_clock::now();
                    benchmark::DoNotOptimize(tc);
                    accum += std::chrono::duration<double>(t1 - t0).count();
                    if ((i & 1023) == 0)
                        co_await asio::post(
                            co_await asio::this_coro::executor,
                            asio::use_awaitable);
                }
                co_return;
            },
            asio::detached);

        ioc.run();
        ioc.restart();
        state.SetIterationTime(accum / kCycles);
    }
}
BENCHMARK(BM_Threading_EngineFallback)->UseManualTime()->Iterations(3);

}  // namespace

BENCHMARK_MAIN();
