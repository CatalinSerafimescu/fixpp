// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_asan_clean.cpp — Seam #8, T054
//
// ASan/UBSan-clean contention mix: near-twin of test_tsan_clean.cpp with the
// same harness and a different suite name (SyncAsanClean).
//
// ASan and UBSan cleanliness is asserted by the linux-clang-asan and
// linux-clang-ubsan presets, which the parent re-verifies independently.
// This debug build asserts the same functional invariants:
//   - mutual exclusion (no overlap under the lock);
//   - no lost waiters.
//
// Oracle: [2f §9 #8] — "ASan/UBSan-clean (seam #8)".

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
#include <fixpp/core/sync/async_mutex.hpp>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: N=64 coroutines, ACQUIRES_PER=8 — mutual exclusion, no heap errors.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncAsanClean, ContentionMixMutualExclusion) {
    constexpr int N = 64;
    constexpr int ACQUIRES_PER = 8;

    std::atomic<int> in_critical{0};
    int overlap = 0;
    int counter = 0;

    asio::io_context ioc;
    async_mutex mtx;

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
    for (int i = 0; i < N; ++i) futs.push_back(asio::co_spawn(ioc, make_coro(), asio::use_future));

    ioc.run();
    for (auto& f : futs) f.get();

    EXPECT_EQ(overlap, 0) << "Mutual exclusion violated";
    EXPECT_EQ(counter, N * ACQUIRES_PER) << "Lost waiter detected";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: cancel mid-wait — no use-after-free, no heap-buffer-overflow.
// ASan catches these if any pointer is dangling in the awaiter lifecycle.
//
// Same idiom as test_cancellation_mid_wait (seam #4): cancellation signals are
// stored externally; a detached helper fires them after a delay.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncAsanClean, CancelMidWaitNoUseAfterFree) {
    constexpr int N = 32;

    std::atomic<int> granted_count{0};
    std::atomic<int> cancelled_count{0};
    std::atomic<int> completed_count{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto holder_coro = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 6 + 16);
    };

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

    std::vector<asio::cancellation_signal> signals(N / 4 + 1);
    std::vector<std::future<void>> futs;
    futs.reserve(N);

    auto fh = asio::co_spawn(ioc, holder_coro(), asio::use_future);

    int sig_idx = 0;
    for (int i = 0; i < N; ++i) {
        if (i % 4 == 0) {
            futs.push_back(asio::co_spawn(
                ioc, waiter_body(),
                asio::bind_cancellation_slot(signals[sig_idx++].slot(), asio::use_future)));
        } else {
            futs.push_back(asio::co_spawn(ioc, waiter_body(), asio::use_future));
        }
    }

    // Fire cancellations from a detached helper coroutine.
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            co_await yield_n(N * 3 + 4);
            for (int j = 0; j < sig_idx; ++j) signals[j].emit(asio::cancellation_type::total);
        },
        asio::detached);

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(completed_count.load(), N) << "Not all waiters completed";
    EXPECT_EQ(granted_count.load() + cancelled_count.load(), N)
        << "granted + cancelled must equal N";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: cancel_and_drain() — no heap errors on drain path.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SyncAsanClean, DrainPathNoHeapErrors) {
    constexpr int N = 16;

    std::atomic<int> total_completed{0};

    asio::io_context ioc;
    async_mutex drain_mtx;

    auto run = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        auto holder = co_await drain_mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        for (int i = 0; i < N; ++i) {
            asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    auto r = co_await drain_mtx.async_lock();
                    (void)r;
                    total_completed.fetch_add(1, std::memory_order_acq_rel);
                },
                asio::detached);
        }

        co_await yield_n(N * 4);

        // Release holder.
        holder = expected_t<async_lock_guard>{};

        auto d = co_await drain_mtx.cancel_and_drain();
        EXPECT_TRUE(d.has_value());

        co_await yield_n(N * 4);
    };

    auto f = asio::co_spawn(ioc, run(), asio::use_future);
    ioc.run();
    f.get();

    EXPECT_EQ(total_completed.load(), N) << "Each waiter must complete exactly once";
}

}  // namespace
