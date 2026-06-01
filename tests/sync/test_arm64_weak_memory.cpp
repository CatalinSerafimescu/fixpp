// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_arm64_weak_memory.cpp — Seam #18
//
// Weak-memory contention stress for the atomic state machine. Verifies the
// I-01..I-31 memory-ordering specification ([2f §6.2/§6.2.2]; FR-013; SC-007)
// holds under genuine cross-core contention: a MULTI-THREADED io_context runs
// the acquire/release/cancel mix so the release/acquire pairings on `state_`,
// `phase_`, `next_drain_head_`, the waiter_record refcount and the latch are
// exercised by distinct hardware threads.
//
// Host note: this build host is x86_64 (TSO). TSan models the C++ memory
// model independent of host ISA, so an under-specified ordering (e.g. a
// relaxed where release/acquire is required by I-03/I-06/I-07/I-08/I-32)
// surfaces here regardless of host. Native ARM64 (weak/LL-SC) execution of
// this seam is recorded host-unavailable in the verify doc; the TSan run
// under this seam is the portable I-01..I-31 verification (SC-007).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <fixpp/core/sync/async_mutex.hpp>
#include <thread>
#include <vector>

namespace {

using fixpp::sync::async_mutex;

// Multi-threaded contention: T worker threads draining one io_context, N
// coroutines contending for a single mutex. Mutual exclusion must hold with
// zero observed overlap; every coroutine completes exactly once.
TEST(SeamArm64WeakMemory, MultiThreadedContentionMutualExclusion) {
    constexpr int N = 4'000;
    const unsigned T = std::max(2u, std::thread::hardware_concurrency());

    std::atomic<int> in_critical{0};
    std::atomic<int> overlap{0};
    std::atomic<int> completed{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto make_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        if (g.has_value()) {
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) overlap.fetch_add(1, std::memory_order_relaxed);
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
        }
        completed.fetch_add(1, std::memory_order_acq_rel);
    };

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i) futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    std::vector<std::thread> pool;
    pool.reserve(T);
    for (unsigned t = 0; t < T; ++t) pool.emplace_back([&] { ioc.run(); });
    for (auto& th : pool) th.join();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap.load(), 0) << "mutual exclusion broken under weak memory";
    EXPECT_EQ(completed.load(), N) << "lost or double-completed coroutine";
}

// Graceful-close under multi-thread contention: a holder holds while a drain
// runs concurrently and a wave of acquirers enqueues; exercises the
// next_drain_head_ splice + waiter_record reclamation (I-32) + latch
// signalling ordering across threads. No overlap, no leak, no hang.
TEST(SeamArm64WeakMemory, DrainUnderMultiThreadContention) {
    constexpr int N = 1'500;
    const unsigned T = std::max(2u, std::thread::hardware_concurrency());

    std::atomic<int> in_critical{0};
    std::atomic<int> overlap{0};
    std::atomic<int> done{0};

    asio::io_context ioc;
    async_mutex mtx;

    // §4.7.4 canonical graceful-close: the holder holds, then releases on its
    // OWN bounded timeline (a fixed number of executor yields so the drain +
    // acquirer wave are in flight); cancel_and_drain() runs concurrently, sets
    // draining_, reaps the queued waiters, and WAITS for this holder. The
    // holder must NOT wait for the drain to finish (that would deadlock — the
    // drain is waiting for the holder).
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        for (int i = 0; i < 64; ++i)
            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        in_critical.fetch_add(1, std::memory_order_acq_rel);
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
        // guard released here -> reaper observes the holder gone, finalises
    };

    auto acquirer = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        if (g.has_value()) {
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) overlap.fetch_add(1, std::memory_order_relaxed);
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
        }
        done.fetch_add(1, std::memory_order_acq_rel);
    };

    auto drainer = [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.cancel_and_drain();
        EXPECT_TRUE(r.has_value());
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i) futs.push_back(asio::co_spawn(ioc, acquirer(), asio::use_future));
    auto fd = asio::co_spawn(ioc, drainer(), asio::use_future);

    std::vector<std::thread> pool;
    pool.reserve(T);
    for (unsigned t = 0; t < T; ++t) pool.emplace_back([&] { ioc.run(); });
    for (auto& th : pool) th.join();
    fh.get();
    fd.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap.load(), 0) << "mutual exclusion broken during drain";
    EXPECT_EQ(done.load(), N) << "an acquirer neither granted nor drained";
}

}  // namespace
