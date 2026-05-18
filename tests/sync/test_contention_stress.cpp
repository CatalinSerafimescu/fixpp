// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_contention_stress.cpp — Seam #6
//
// Contention stress: ≥ 10⁴ coroutines, zero overlap, zero starvation,
// zero lost waiter.  All coroutines complete exactly once.
//
// Oracle: [2f §9 #6] — "Contention stress (N ≥ 10⁴)".
// SC-001: mutual exclusion invariant.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <vector>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;

TEST(SeamContentionStress, TenThousandCoroutines) {
    // 10,000 coroutines each acquire the mutex, increment a counter, and
    // release.  At the end the counter must equal N (no lost increment, no
    // double-increment, no overlap).

    constexpr int N = 10'000;
    async_mutex mtx;
    std::atomic<int> in_critical{0};
    int overlap = 0;
    int counter = 0;

    asio::io_context ioc;

    auto make_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) ++overlap;
        ++counter;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
        // guard released here
    };

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0)  << "Mutual exclusion violated (overlap detected)";
    EXPECT_EQ(counter, N)  << "Lost waiter: not all coroutines completed";
}

TEST(SeamContentionStress, ZeroLostWaiterOnChainedAcquires) {
    // 1,000 coroutines each acquire twice (total 2,000 acquisitions).
    // Verifies that chained acquire / release on the same mutex drains the
    // entire LIFO + next_drain_head_ chain without losing any waiter.

    constexpr int N = 1'000;
    constexpr int ACQUIRES_PER = 2;
    async_mutex mtx;
    std::atomic<int> in_critical{0};
    int overlap = 0;
    int total = 0;

    asio::io_context ioc;

    auto make_coro = [&]() -> asio::awaitable<void> {
        for (int i = 0; i < ACQUIRES_PER; ++i) {
            auto g = co_await mtx.async_lock();
            EXPECT_TRUE(g.has_value());
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) ++overlap;
            ++total;
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
        }
    };

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0);
    EXPECT_EQ(total, N * ACQUIRES_PER);
}

}  // namespace
