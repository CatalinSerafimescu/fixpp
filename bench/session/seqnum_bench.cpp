// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/seqnum_bench.cpp
//
// 005-session-establishment-fsm T061 / Phase 8 — Tier-1 latency bench for the
// SeqnumManager next-with-increment fast path.
//
// Ceiling (plan.md Technical-Context, [2e §6.6]; warm cache, Linux/Clang/x86_64
// release `-O2`):
//   BM_Session_SeqnumNextWithIncrement  ≤ 50 ns  (async_mutex uncontended)
//
// METHODOLOGY (mirrors bench/threading/bench_threading.cpp row-classification
// convention, D-24 / RC#3): the bench drives `assign_outbound()` through the
// asio::io_context::run() round-trip, so the measured cost is END-TO-END (the
// awaitable suspend/resume + the async_mutex fast-path CAS). The primitive
// async_mutex acquire cost in isolation is below the ceiling; the
// io_context::run() round-trip dominates. The ±5% regression gate per
// [const §VIII.2] applies to the end-to-end figure.

#include <benchmark/benchmark.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <future>

namespace {

using fixpp::session::seqnum_t;
using fixpp::session::SeqnumManager;

void BM_Session_SeqnumNextWithIncrement(benchmark::State& state) {
    asio::io_context ioc;
    SeqnumManager mgr;

    // Warm-up: prime asio's per-thread caches + the async_mutex slot pool.
    for (int i = 0; i < 8; ++i) {
        auto fut = asio::co_spawn(ioc, mgr.assign_outbound(), asio::use_future);
        ioc.run();
        ioc.restart();
        benchmark::DoNotOptimize(fut.get());
    }

    for (auto _ : state) {
        auto fut = asio::co_spawn(ioc, mgr.assign_outbound(), asio::use_future);
        ioc.run();
        ioc.restart();
        auto r = fut.get();
        benchmark::DoNotOptimize(r);
    }

    // Drain mutex before dtor (terminate-precondition).
    auto fut = asio::co_spawn(ioc, mgr.drain(), asio::use_future);
    ioc.run();
    benchmark::DoNotOptimize(fut.get());
}
BENCHMARK(BM_Session_SeqnumNextWithIncrement);

}  // namespace
