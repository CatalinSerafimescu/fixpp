// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_cross_strand_acquire.cpp — Seam #13
//
// Cross-strand resume via `post`, FIFO-fair drain.
// Two coroutines on different strands contend.  The waiter must resume on its
// OWN strand (not the unlocker's strand).  The `post` policy ensures the
// resumption is dispatched through the bound executor.
//
// On a single io_context with two strands the coroutines cooperatively
// alternate.  We verify:
//  - Both coroutines complete.
//  - Mutual exclusion is never violated.
//  - The second acquirer (on strand2) resumes and completes after the first
//    (on strand1) releases.
//
// Oracle: [2f §9 #13] — "Cross-strand acquire / FIFO-fair drain".
//         [2f §6.1.3] — cross-strand resume via post.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>

#include <atomic>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::completion_policy;

TEST(SeamCrossStrandAcquire, TwoStrandsNoOverlap) {
    // Mutex configured with `post` policy so resumption always goes through
    // the bound executor (cross-strand safety).
    asio::io_context ioc;
    auto strand1 = asio::make_strand(ioc);
    auto strand2 = asio::make_strand(ioc);
    // post policy: cross-strand unlock always posts to the waiter's executor.
    async_mutex mtx{completion_policy::post};

    std::atomic<int> in_critical{0};
    int overlap = 0;
    bool first_done = false, second_done = false;

    // Coroutine on strand1: acquires and holds while yielding.
    auto coro1 = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) ++overlap;
        // Yield so coro2 can post its acquire.
        co_await asio::post(co_await asio::this_coro::executor,
                            asio::use_awaitable);
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
        first_done = true;
        // guard released
    };

    // Coroutine on strand2: post once (so strand1's coro acquires first),
    // then acquire.
    auto coro2 = [&]() -> asio::awaitable<void> {
        co_await asio::post(co_await asio::this_coro::executor,
                            asio::use_awaitable);
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) ++overlap;
        second_done = true;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
    };

    auto f1 = asio::co_spawn(strand1, coro1(), asio::use_future);
    auto f2 = asio::co_spawn(strand2, coro2(), asio::use_future);
    ioc.run();
    f1.get();
    f2.get();

    EXPECT_EQ(overlap, 0) << "Mutual exclusion violated cross-strand";
    EXPECT_TRUE(first_done);
    EXPECT_TRUE(second_done);
}

TEST(SeamCrossStrandAcquire, MultipleWaitersOnDifferentStrands) {
    // 4 coroutines on 4 different strands contend.
    constexpr int NS = 4;
    asio::io_context ioc;
    async_mutex mtx{completion_policy::post};
    std::atomic<int> in_critical{0};
    int overlap = 0;
    int done_count = 0;

    std::vector<decltype(asio::make_strand(ioc))> strands;
    for (int i = 0; i < NS; ++i)
        strands.push_back(asio::make_strand(ioc));

    auto make_coro = [&](int idx) -> asio::awaitable<void> {
        (void)idx;
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) ++overlap;
        ++done_count;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
    };

    std::vector<std::future<void>> futs;
    for (int i = 0; i < NS; ++i)
        futs.push_back(
            asio::co_spawn(strands[i], make_coro(i), asio::use_future));
    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0);
    EXPECT_EQ(done_count, NS);
}

}  // namespace
