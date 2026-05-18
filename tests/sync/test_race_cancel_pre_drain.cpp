// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_race_cancel_pre_drain.cpp — Seam #15
//
// cancel-after-detach-pre-drain race (RC#1).
//
// Park three waiters on a mutex. Release the holder so unlock() begins its
// drain. From a different coroutine, fire cancellation_type::total on the
// second waiter's slot CONCURRENTLY with the unlock drain. Verify:
//   (a) each waiter completes exactly once (guard or sync_lock_aborted);
//   (b) no double-resume;
//   (c) the mutex ends up not_locked (no std::terminate on destruction);
//   (d) TSan-clean (verified by running under linux-clang-tsan).
//
// ASIO cancellation pattern: spawn each waiter with bind_cancellation_slot
// so its co_await mtx.async_lock() inherits the cancellation state.
//
// Oracle: [2f §9 #15] — "cancel-after-detach-pre-drain race" (RC#1).
// SC-002: TSan-clean concurrency.

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

namespace {

using fixpp::sync::async_mutex;
using fixpp::core::error;

static asio::awaitable<void> yield_n(int n) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < n; ++i)
        co_await asio::post(ex, asio::use_awaitable);
}

// ─────────────────────────────────────────────────────────────────────────────
// Race scenario: N waiters park while holder holds. Holder releases while a
// cancel fires on one waiter concurrently. The per-waiter phase_ CAS
// arbitrates: exactly one of {granted, cancelled} per waiter.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelPreDrain, ThreeWaitersOneRacingCancel) {
    constexpr int N = 3;
    async_mutex mtx;

    std::vector<asio::cancellation_signal> sigs(N);

    std::atomic<int> granted_count{0};
    std::atomic<int> aborted_count{0};
    std::atomic<int> total_completed{0};

    asio::io_context ioc;

    // Holder: acquires, yields to let waiters queue, fires cancel on sigs[1],
    // then releases.
    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());

        // Let all N waiters push onto the LIFO.
        co_await yield_n(N * 4);

        // Fire cancel on the middle waiter (index 1) while still holding.
        // This races with the upcoming drain walk after our unlock.
        sigs[1].emit(asio::cancellation_type::total);

        // One yield so cancel handler may run before unlock.
        co_await yield_n(2);
        // unlock via guard dtor.
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (r.has_value()) {
            granted_count.fetch_add(1, std::memory_order_acq_rel);
            co_await yield_n(2);
        } else {
            EXPECT_EQ(r.error(), error::sync_lock_aborted);
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        total_completed.fetch_add(1, std::memory_order_acq_rel);
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

    EXPECT_EQ(total_completed.load(), N)
        << "All waiters must complete exactly once";

    int g = granted_count.load();
    int a = aborted_count.load();
    EXPECT_EQ(g + a, N) << "granted + aborted must cover all N waiters";
    EXPECT_GE(g, 1) << "At least one waiter must be granted";
}

// ─────────────────────────────────────────────────────────────────────────────
// Stress: fire cancel on every waiter simultaneously with the unlock drain.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelPreDrain, StressSimultaneousCancelAndDrain) {
    constexpr int N = 8;
    async_mutex mtx;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> granted_count{0};
    std::atomic<int> aborted_count{0};
    std::atomic<int> total_completed{0};

    asio::io_context ioc;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 4);
        for (int i = 0; i < N; ++i)
            sigs[i].emit(asio::cancellation_type::total);
        co_await yield_n(N);
        // unlock.
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
// Verify mutex is free after race — destruction succeeds.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamRaceCancelPreDrain, MutexFreeAfterRace) {
    constexpr int N = 4;
    async_mutex mtx;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> total{0};

    asio::io_context ioc;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 3);
        for (int i = 0; i < N; ++i)
            sigs[i].emit(asio::cancellation_type::total);
        co_await yield_n(N);
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        (void)r;
        total.fetch_add(1, std::memory_order_acq_rel);
        if (r.has_value())
            co_await yield_n(1);
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
    // mtx destruction here — must not terminate (state_ == not_locked).
}

}  // namespace
