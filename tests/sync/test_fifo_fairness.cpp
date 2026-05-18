// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_fifo_fairness.cpp — Seam #3
//
// FIFO fairness across a drain cycle.  N coroutines all wait on the mutex.
// The Lewis-Baker / cppcoro algorithm builds a LIFO push list but then
// reverses it on unlock, so within a single drain cycle the order is FIFO
// (the coroutines acquire in the order they pushed, which is the reverse of
// LIFO — "reversal on drain gives FIFO").
//
// Oracle: [2f §9 #3] — "FIFO fairness across drain cycles".
//         [2f §4.5.2] — unlock reversal gives FIFO within a cycle.
// SC-001: mutual exclusion.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>

#include <vector>
#include <atomic>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;

TEST(SeamFifoFairness, DrainCycleReversesLIFO) {
    // Spawn N coroutines. The first one acquires the mutex and holds it while
    // N-1 others all enqueue.  Then the holder releases.  On drain, unlock
    // reverses the LIFO list → FIFO order within the drain cycle.
    //
    // We record acquisition order and verify it is the reverse of enqueue order
    // (i.e., FIFO of the waiters — the last to enqueue is first in LIFO, but
    // unlock reverses it so the first enqueued gets the lock first).

    constexpr int N = 16;
    async_mutex mtx;
    std::atomic<int> in_critical{0};
    int overlap = 0;

    std::vector<int> enqueue_order; // coroutine index in the order they block
    std::vector<int> acquire_order; // coroutine index in the order they acquire

    asio::io_context ioc;

    // Holder: acquires first, lets others enqueue, then releases.
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        // Give all other coroutines a chance to start and block on async_lock.
        for (int i = 0; i < N * 2; ++i)
            co_await asio::post(co_await asio::this_coro::executor,
                                asio::use_awaitable);
        // Now release — all N waiters should be in the LIFO list.
        // unlock() reverses → FIFO relative to enqueue order.
    };

    // Waiters: enqueue after yielding once so the holder grabs the lock first.
    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor,
                            asio::use_awaitable);
        {
            // Record enqueue attempt order (approximate — single-threaded ioc).
            enqueue_order.push_back(idx);
            auto g = co_await mtx.async_lock();
            EXPECT_TRUE(g.has_value());
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) overlap++;
            acquire_order.push_back(idx);
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
        }
    };

    // Spawn holder first, then all waiters.
    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(i), asio::use_future));

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0) << "Mutual exclusion violated";
    ASSERT_EQ((int)acquire_order.size(), N) << "All waiters must acquire";

    // Within the first drain cycle, the acquire order should be the reverse
    // of the push order (LIFO → reversed → FIFO of push order).
    // enqueue_order[0] was the first to enqueue → it pushed first, so it is
    // at the BOTTOM of the LIFO stack.  Reversal makes it the HEAD of the FIFO
    // list, meaning it acquires first within the drain.
    // Verify acquire_order == enqueue_order (FIFO property).
    EXPECT_EQ(acquire_order, enqueue_order)
        << "FIFO within a drain cycle: acquire order must match enqueue order";
}

TEST(SeamFifoFairness, MutualExclusionPreservedAcrossMultipleDrains) {
    // Multiple drain cycles: each cycle has 4 waiters.
    constexpr int ROUNDS = 4;
    constexpr int PER_ROUND = 4;
    async_mutex mtx;
    std::atomic<int> in_critical{0};
    int overlap = 0;
    int total_acquires = 0;

    asio::io_context ioc;

    auto round_coros = [&]() -> asio::awaitable<void> {
        for (int r = 0; r < ROUNDS; ++r) {
            // Acquire + release PER_ROUND times sequentially in one coro
            // to generate multiple drain cycles.
            auto g = co_await mtx.async_lock();
            EXPECT_TRUE(g.has_value());
            if (g.has_value()) {
                int v = in_critical.fetch_add(1, std::memory_order_acq_rel)+1;
                if (v > 1) overlap++;
                ++total_acquires;
                in_critical.fetch_sub(1, std::memory_order_acq_rel);
            }
        }
    };

    std::vector<std::future<void>> futs;
    for (int i = 0; i < PER_ROUND; ++i)
        futs.push_back(asio::co_spawn(ioc, round_coros(), asio::use_future));
    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0);
    EXPECT_EQ(total_acquires, ROUNDS * PER_ROUND);
}

}  // namespace
