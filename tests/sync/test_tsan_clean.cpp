// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_tsan_clean.cpp — Seam #7, T053
//
// TSan-clean contention mix: N=64 coroutines repeatedly acquire, hold briefly,
// and release the mutex; a fraction cancel mid-wait; a separate mutex instance
// exercises cancel_and_drain().
//
// The "TSan-clean" property is enforced by running this test under the
// linux-clang-tsan preset (which the parent re-verifies independently).
// This debug build asserts functional correctness:
//   - mutual exclusion: a shared counter incremented under the lock has no
//     lost updates (overlap == 0, counter == N * ACQUIRES_PER).
//   - no deadlock: ioc.run() returns within the test timeout.
//
// Oracle: [2f §9 #7] — "TSan-clean (seam #7)".

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
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

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;
using fixpp::sync::expected_t;
using fixpp::core::error;

static asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i)
        co_await asio::post(ex, asio::use_awaitable);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: N=64 coroutines, ACQUIRES_PER=8 each — mutual exclusion invariant.
// A shared counter is incremented under the lock; any overlap means a data race.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncTsanClean, ContentionMixMutualExclusion) {
    constexpr int N            = 64;
    constexpr int ACQUIRES_PER = 8;

    async_mutex mtx;
    std::atomic<int> in_critical{0};
    int overlap  = 0;
    int counter  = 0;

    asio::io_context ioc;

    auto make_coro = [&]() -> asio::awaitable<void> {
        for (int i = 0; i < ACQUIRES_PER; ++i) {
            auto g = co_await mtx.async_lock();
            if (!g.has_value()) {
                ADD_FAILURE() << "unexpected error: " << static_cast<int>(g.error());
                co_return;
            }
            int v = in_critical.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (v > 1) ++overlap;
            ++counter;
            in_critical.fetch_sub(1, std::memory_order_acq_rel);
            // guard released
        }
    };

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap,  0)                << "Mutual exclusion violated (overlap detected)";
    EXPECT_EQ(counter,  N * ACQUIRES_PER) << "Lost waiter: not all increments completed";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: A fraction of coroutines cancel mid-wait; the rest are granted.
//
// Each cancel-waiter coroutine owns a cancellation_signal and is spawned via
// bind_cancellation_slot + use_future (same idiom as test_cancellation_mid_wait
// seam #4). The signal is fired from a separate helper coroutine that is spawned
// detached on the same executor — no blocking fut.get() inside a coroutine.
//
// Functional assertion: (granted + cancelled) == N, no lost waiter.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncTsanClean, FractionCancelMidWait) {
    // N waiters total; every 4th will have cancellation fired against it.
    // 1 holder keeps the mutex locked long enough for all waiters to park.
    constexpr int N = 32;

    async_mutex mtx;
    std::atomic<int> granted_count{0};
    std::atomic<int> cancelled_count{0};
    std::atomic<int> completed_count{0};

    asio::io_context ioc;

    // Holder: parks long enough for all N waiters to enqueue.
    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 6 + 16);
        // guard released
    };

    // Waiter body (used for both normal and cancellable waiters).
    auto waiter_body = [&]() -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (r.has_value()) {
            granted_count.fetch_add(1, std::memory_order_acq_rel);
        } else if (r.error() == error::sync_lock_aborted) {
            cancelled_count.fetch_add(1, std::memory_order_acq_rel);
        }
        completed_count.fetch_add(1, std::memory_order_acq_rel);
    };

    // Spawn all waiters; track which ones need cancellation.
    // Cancel-waiters are spawned with bind_cancellation_slot.
    // Signals are stored externally (lifetime tied to ioc.run() block).
    std::vector<asio::cancellation_signal> signals(N / 4 + 1);
    std::vector<std::future<void>> futs;
    futs.reserve(N);

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);

    int sig_idx = 0;
    for (int i = 0; i < N; ++i) {
        if (i % 4 == 0) {
            // Cancel-waiter: bind slot so on_cancel fires.
            futs.push_back(asio::co_spawn(
                ioc, waiter_body(),
                asio::bind_cancellation_slot(
                    signals[sig_idx++].slot(), asio::use_future)));
        } else {
            futs.push_back(asio::co_spawn(ioc, waiter_body(), asio::use_future));
        }
    }

    // Fire cancellations after a delay using a detached helper coroutine.
    // This runs on the same ioc thread — no thread safety issues.
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        // Let all waiters park on the LIFO.
        co_await yield_n(N * 3 + 4);
        for (int j = 0; j < sig_idx; ++j)
            signals[j].emit(asio::cancellation_type::total);
    }, asio::detached);

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(completed_count.load(), N)
        << "Not all waiters completed (lost waiter)";
    EXPECT_EQ(granted_count.load() + cancelled_count.load(), N)
        << "granted + cancelled must equal N";
    // Some should have been cancelled (we fired N/4+1 signals).
    // We don't assert exactly how many cancelled vs granted, because the
    // holder may have released before some cancel signals fired; the invariant
    // is that all N complete and the sum is N.
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: cancel_and_drain() on a separate mutex instance.
// Exercises the drain path TSan-cleanly alongside the contention tests.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncTsanClean, OccasionalCancelAndDrain) {
    constexpr int N = 16;

    async_mutex drain_mtx;
    std::atomic<int> total_completed{0};

    asio::io_context ioc;

    auto run = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        auto holder = co_await drain_mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        for (int i = 0; i < N; ++i) {
            asio::co_spawn(ex, [&]() -> asio::awaitable<void> {
                auto r = co_await drain_mtx.async_lock();
                (void)r;
                total_completed.fetch_add(1, std::memory_order_acq_rel);
            }, asio::detached);
        }

        co_await yield_n(N * 4);

        // Release holder (unlock).
        holder = expected_t<async_lock_guard>{};

        auto d = co_await drain_mtx.cancel_and_drain();
        EXPECT_TRUE(d.has_value()) << "cancel_and_drain must succeed";

        co_await yield_n(N * 4);
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    EXPECT_EQ(total_completed.load(), N)
        << "Each waiter must complete exactly once";
}

}  // namespace
