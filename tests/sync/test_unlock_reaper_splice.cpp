// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_unlock_reaper_splice.cpp — Seam #27
//
// unlock-vs-reaper splice race closure (RC-α / Opus C-R3-P1-3).
//
// Under draining_ == true, unlock() must NOT splice into next_drain_head_;
// it must only CAS state_ to not_locked and notify the latch.  The reaper's
// stable-loop re-walks both state_ and next_drain_head_ until both are null.
//
// For US1 (without cancel_and_drain), we verify the related invariant:
//   - unlock() correctly transfers ownership through next_drain_head_ when
//     it carries residual waiters from a prior unlock's LIFO drain.
//   - Multiple rounds of LIFO → next_drain_head_ → grant do not lose
//     any waiters.
//
// Oracle: [2f §9 #27] — "unlock-vs-reaper splice race closure" (RC-α RC-A).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <fixpp/core/sync/async_mutex.hpp>
#include <vector>

#include "support/pump_until_ready.hpp"

namespace {

using fixpp::sync::async_mutex;

TEST(SeamUnlockReaperSplice, ResidualFifoChainDrainedCorrectly) {
    // Scenario: N waiters push onto the LIFO.  The first unlock reverses to
    // FIFO and grants to waiter[0]; the rest are spliced into next_drain_head_.
    // Subsequent unlocks walk next_drain_head_ first, granting in FIFO order.
    //
    // All N waiters must eventually acquire and release.  No waiter is lost.
    constexpr int N = 32;
    std::atomic<int> in_critical{0};
    int overlap = 0;
    int total = 0;

    asio::io_context ioc;
    async_mutex mtx;

    // Holder: acquires first, yields to allow all waiters to queue, then
    // releases.  The first unlock drain produces next_drain_head_ residual.
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        // Yield N times to let all waiters push onto the LIFO.
        for (int i = 0; i < N * 2; ++i)
            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        // Release: first unlock reverses LIFO → grants first waiter,
        // remaining N-1 spliced into next_drain_head_.
    };

    auto make_waiter = [&]() -> asio::awaitable<void> {
        // One yield so holder acquires first.
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (v > 1) ++overlap;
        ++total;
        in_critical.fetch_sub(1, std::memory_order_acq_rel);
        // Each waiter's unlock walks next_drain_head_ first.
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));

    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fh, "SeamUnlockReaperSplice::ResidualFifoChainDrainedCorrectly")) {
        return;
    }
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0) << "Mutual exclusion violated";
    EXPECT_EQ(total, N) << "Lost waiter in residual FIFO chain";
}

TEST(SeamUnlockReaperSplice, MultipleRoundsResidualChain) {
    // Three rounds of: one holder + many waiters.  Each round exercises the
    // next_drain_head_ residual chain splicing across multiple unlock calls.
    constexpr int ROUNDS = 3;
    constexpr int PER_ROUND = 16;
    std::atomic<int> in_critical{0};
    int overlap = 0;
    int total = 0;

    asio::io_context ioc;
    async_mutex mtx;

    auto make_coro = [&]() -> asio::awaitable<void> {
        for (int r = 0; r < ROUNDS; ++r) {
            auto g = co_await mtx.async_lock();
            EXPECT_TRUE(g.has_value());
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) ++overlap;
            ++total;
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
        }
    };

    std::vector<std::future<void>> futs;
    for (int i = 0; i < PER_ROUND; ++i)
        futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));
    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0);
    EXPECT_EQ(total, ROUNDS * PER_ROUND);
}

}  // namespace
