// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_contended_latency.cpp — Seam #2
//
// Contended path: a second acquirer suspends when the mutex is already held.
// It must not busy-wait; it resumes only when the holder calls unlock().
// Mutual exclusion: both coroutines NEVER hold the lock simultaneously.
//
// Oracle: [2f §9 #2] — "Contended-enqueue latency Tier 1".
// SC-001: mutual exclusion invariant.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <chrono>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;

// Runs all co_spawned tasks to completion.
static void run_ctx(asio::io_context& ioc) { ioc.run(); }

TEST(SeamContendedLatency, SecondAcquirerSuspends) {
    // Two coroutines race for the same mutex.  The first one holds it while the
    // second is forced to suspend.  We verify:
    //  (a) they never overlap (in_critical is never > 1 simultaneously).
    //  (b) the second acquirer does in fact run (both complete).

    std::atomic<int> in_critical{0};
    int overlap_detected = 0;
    int first_done = 0, second_done = 0;

    asio::io_context ioc;
    async_mutex mtx;

    auto coro1 = [&]() -> asio::awaitable<void> {
        auto guard = co_await mtx.async_lock();
        EXPECT_TRUE(guard.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) overlap_detected++;
        // Yield to let coro2 start and block on the mutex.
        co_await asio::post(co_await asio::this_coro::executor,
                            asio::use_awaitable);
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
        first_done = 1;
        // guard destructor calls unlock() here
    };

    auto coro2 = [&]() -> asio::awaitable<void> {
        // Post after coro1 has started so coro1 acquires first.
        co_await asio::post(co_await asio::this_coro::executor,
                            asio::use_awaitable);
        auto guard = co_await mtx.async_lock();
        EXPECT_TRUE(guard.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) overlap_detected++;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
        second_done = 1;
    };

    auto f1 = asio::co_spawn(ioc, coro1(), asio::use_future);
    auto f2 = asio::co_spawn(ioc, coro2(), asio::use_future);
    ioc.run();
    f1.get();
    f2.get();

    EXPECT_EQ(overlap_detected, 0) << "Mutual exclusion violated";
    EXPECT_EQ(first_done, 1);
    EXPECT_EQ(second_done, 1);
}

TEST(SeamContendedLatency, MutualExclusionMultipleWaiters) {
    // N coroutines all try to acquire. Counter increment inside critical section
    // should remain consistent (each increment is visible to subsequent holders).

    constexpr int N = 8;
    std::atomic<int> in_critical{0};
    int overlap_count = 0;
    int counter = 0;

    asio::io_context ioc;
    async_mutex mtx;

    auto make_coro = [&]() -> asio::awaitable<void> {
        auto guard = co_await mtx.async_lock();
        EXPECT_TRUE(guard.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) overlap_count++;
        ++counter;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
    };

    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));
    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap_count, 0) << "Mutual exclusion violated";
    EXPECT_EQ(counter, N) << "All coroutines must complete";
}

}  // namespace
