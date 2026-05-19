// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_cancel_and_drain.cpp — Seam #19
//
// cancel_and_drain() basic contract (RC#3).
//
// Scenario: hold the mutex, queue N=8 waiters, call cancel_and_drain().
// Verify:
//   - every waiter completes EXACTLY once with error::sync_lock_aborted
//     (none granted, none lost, none double-resumed);
//   - cancel_and_drain() returns expected_t<void>{} (has_value) only after
//     all waiters have been reaped;
//   - after drain, a fresh co_await async_lock() returns
//     unexpected(error::sync_lock_drained) — no enqueue, no grant.
//
// Oracle: [2f §9 #19] — "cancel_and_drain basic contract" (RC#3).
//         [2f §4.7.2] — draining_ flag set; post-drain acquires fast-fail.
// SC-003: cancel_and_drain() is idempotent after the epoch completes.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <vector>

#include <fixpp/core/sync/async_mutex.hpp>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;
using fixpp::core::error;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: N=8 waiters all receive sync_lock_aborted; cancel_and_drain() succeeds.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrain, EightWaitersAllAbortedOnDrain) {
    constexpr int N = 8;

    std::atomic<int> aborted_count{0};
    std::atomic<int> granted_count{0};
    std::atomic<int> completed_count{0};
    bool drain_succeeded = false;

    asio::io_context ioc;
    async_mutex mtx;

    // Canonical §4.7.4 graceful-close sequencing: cancel_and_drain() runs
    // CONCURRENTLY while the holder is still in its critical section. The
    // reaper sets draining_ and reaps the queued waiters; the holder's later
    // unlock() observes draining_ == true and short-circuits (does NOT grant).
    // (Releasing the holder BEFORE the drain would let unlock() legitimately
    // grant a waiter per the US1 contract — that is not what this seam tests.)
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        // Hold long enough for the drainer to set draining_ and reap.
        co_await yield_n(N * 4 + 8);
        // Guard destructor calls unlock() (draining_ == true → short-circuit).
    };

    auto drainer_coro = [&]() -> asio::awaitable<void> {
        // Let the holder acquire and all N waiters park.
        co_await yield_n(N * 2);
        auto d = co_await mtx.cancel_and_drain();
        drain_succeeded = d.has_value();
    };

    auto make_waiter = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (r.has_value()) {
            granted_count.fetch_add(1, std::memory_order_acq_rel);
        } else {
            if (r.error() == error::sync_lock_aborted)
                aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        completed_count.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);
    auto fd = asio::co_spawn(ioc, drainer_coro(), asio::use_future);
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_waiter(), asio::use_future));
    ioc.run();
    fh.get();
    fd.get();
    for (auto& f : futs) f.get();

    EXPECT_TRUE(drain_succeeded) << "cancel_and_drain() must return success";
    EXPECT_EQ(completed_count.load(), N) << "All N waiters must complete exactly once";
    EXPECT_EQ(granted_count.load(), 0) << "No waiter must be granted after drain";
    EXPECT_EQ(aborted_count.load(), N) << "All N waiters must receive sync_lock_aborted";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: post-drain async_lock() fast-fails with sync_lock_drained.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrain, PostDrainAcquireReturnsSyncLockDrained) {
    bool drain_ok = false;
    bool post_drain_correct = false;

    asio::io_context ioc;
    async_mutex mtx;

    auto run = [&]() -> asio::awaitable<void> {
        // Drain an idle mutex (no waiters, not held).
        auto d = co_await mtx.cancel_and_drain();
        drain_ok = d.has_value();

        // After drain, any fresh async_lock() must get sync_lock_drained.
        auto r = co_await mtx.async_lock();
        post_drain_correct = !r.has_value() &&
                             r.error() == error::sync_lock_drained;
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    EXPECT_TRUE(drain_ok) << "cancel_and_drain() must succeed";
    EXPECT_TRUE(post_drain_correct)
        << "Post-drain async_lock() must return sync_lock_drained";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: no waiter is double-resumed; no waiter is lost.
// Uses completed_count to assert exactly-once completion per waiter.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrain, NoDoubleResumeNoLostWaiter) {
    constexpr int N = 16;

    std::atomic<int> total_completed{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto run = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        for (int i = 0; i < N; ++i) {
            asio::co_spawn(ex, [&]() -> asio::awaitable<void> {
                auto r = co_await mtx.async_lock();
                (void)r;
                total_completed.fetch_add(1, std::memory_order_acq_rel);
            }, asio::detached);
        }

        co_await yield_n(N * 4);

        // Drop holder guard (unlock).
        holder = expected_t<async_lock_guard>{};

        auto d = co_await mtx.cancel_and_drain();
        EXPECT_TRUE(d.has_value());

        co_await yield_n(N * 4);
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    EXPECT_EQ(total_completed.load(), N)
        << "Each waiter must complete exactly once (no double-resume, no lost)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: post-drain async_lock() from multiple concurrent coroutines all fail
// with sync_lock_drained (no enqueue).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamCancelAndDrain, MultiplePostDrainAcquiresAllGetDrained) {
    constexpr int M = 8;

    std::atomic<int> drained_count{0};
    bool drain_ok = false;

    asio::io_context ioc;
    async_mutex mtx;

    auto run = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        auto d = co_await mtx.cancel_and_drain();
        drain_ok = d.has_value();

        // Spawn M post-drain acquirers.
        for (int i = 0; i < M; ++i) {
            asio::co_spawn(ex, [&]() -> asio::awaitable<void> {
                auto r = co_await mtx.async_lock();
                if (!r.has_value() && r.error() == error::sync_lock_drained)
                    drained_count.fetch_add(1, std::memory_order_acq_rel);
            }, asio::detached);
        }

        co_await yield_n(M * 4);
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    EXPECT_TRUE(drain_ok);
    EXPECT_EQ(drained_count.load(), M)
        << "All post-drain acquirers must get sync_lock_drained";
}

}  // namespace
