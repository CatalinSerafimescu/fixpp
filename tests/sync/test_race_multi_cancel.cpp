// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_race_multi_cancel.cpp — Seam #16
//
// Multi-cancel-same-list race (RC#1).
//
// Park N=8 waiters; release the holder; fire total cancellation on every
// waiter's slot simultaneously while the unlocker drains. Verify:
//   - each waiter completes exactly once;
//   - granted + aborted == N (no double-resume, no lost waiter);
//   - ASan/TSan-clean (verified by preset).
//
// ASIO cancellation pattern: each waiter is co_spawn'd with
// bind_cancellation_slot so its inherited cancellation_state is wired.
//
// Oracle: [2f §9 #16] — "multi-cancel-same-list race" (RC#1).
// SC-002: TSan-clean under concurrent cancellation signals.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
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
using fixpp::core::error;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// N=8 waiters queued; all cancellations fired while unlocker drains.
// Every waiter must complete exactly once.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceMultiCancel, EightWaitersAllFiredSimultaneously) {
    constexpr int N = 8;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> granted_count{0};
    std::atomic<int> aborted_count{0};
    std::atomic<int> total_completed{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());

        // Allow all N waiters to push onto the LIFO.
        co_await yield_n(N * 4);

        // Fire ALL cancellations.
        for (int i = 0; i < N; ++i)
            sigs[i].emit(asio::cancellation_type::total);

        // Yield to process cancel handlers before unlock.
        co_await yield_n(N * 2);
        // unlock via guard dtor.
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);

        auto r = co_await mtx.async_lock();

        if (r.has_value()) {
            granted_count.fetch_add(1, std::memory_order_acq_rel);
            co_await yield_n(1);
        } else {
            EXPECT_EQ(r.error(), error::sync_lock_aborted)
                << "Cancelled waiter " << idx << " must see sync_lock_aborted";
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        total_completed.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i) {
        futs.push_back(asio::co_spawn(
            ioc,
            make_waiter(i),
            asio::bind_cancellation_slot(sigs[i].slot(), asio::use_future)));
    }

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(total_completed.load(), N)
        << "All N waiters must complete exactly once";

    int g = granted_count.load();
    int a = aborted_count.load();
    EXPECT_EQ(g + a, N)
        << "granted (" << g << ") + aborted (" << a << ") must equal N=" << N;
}

// ─────────────────────────────────────────────────────────────────────────────
// Variant: N=32 to stress the residual FIFO chain splice logic.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceMultiCancel, ThirtyTwoWaitersLargeList) {
    constexpr int N = 32;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> granted_count{0};
    std::atomic<int> aborted_count{0};
    std::atomic<int> total_completed{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 4);
        for (int i = 0; i < N; ++i)
            sigs[i].emit(asio::cancellation_type::total);
        co_await yield_n(N * 2);
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (r.has_value()) {
            granted_count.fetch_add(1, std::memory_order_acq_rel);
            co_await yield_n(1);
        } else {
            EXPECT_EQ(r.error(), error::sync_lock_aborted);
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        total_completed.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i) {
        futs.push_back(asio::co_spawn(
            ioc,
            make_waiter(i),
            asio::bind_cancellation_slot(sigs[i].slot(), asio::use_future)));
    }

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(total_completed.load(), N);
    EXPECT_EQ(granted_count.load() + aborted_count.load(), N);
}

// ─────────────────────────────────────────────────────────────────────────────
// Verify: after all cancellations complete, a new uncancelled acquire succeeds.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceMultiCancel, NewAcquireSucceedsAfterAllCancelled) {
    constexpr int N = 4;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> total{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 3);
        for (int i = 0; i < N; ++i)
            sigs[i].emit(asio::cancellation_type::total);
        co_await yield_n(N * 2);
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        (void)r;
        total.fetch_add(1, std::memory_order_acq_rel);
        if (r.has_value()) co_await yield_n(1);
    };

    auto fh = asio::co_spawn(ioc, holder(), asio::use_future);
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i) {
        futs.push_back(asio::co_spawn(
            ioc,
            make_waiter(i),
            asio::bind_cancellation_slot(sigs[i].slot(), asio::use_future)));
    }

    ioc.run();
    fh.get();
    for (auto& f : futs) f.get();

    EXPECT_EQ(total.load(), N);

    // After all cancelled waiters complete, a fresh acquire must succeed.
    bool ok = false;
    auto freshen = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        ok = r.has_value();
    }, asio::use_future);
    ioc.restart();  // io_context drained by the first run(); restart before re-running.
    ioc.run();
    freshen.get();
    EXPECT_TRUE(ok) << "Fresh acquire after all-cancel must succeed";
}

}  // namespace
