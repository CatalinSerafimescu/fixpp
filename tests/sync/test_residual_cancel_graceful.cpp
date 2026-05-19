// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_residual_cancel_graceful.cpp — Seam #22
//
// Residual-chain cancellation under graceful close (RC-A).
//
// Park N=8 waiters on a held mutex. Release the holder so unlock() drains:
//   - first waiter (FIFO-front after LIFO reversal) is granted;
//   - remaining N-1 are spliced into next_drain_head_ (still phase queued).
// WITHOUT releasing the granted waiter, fire cancellation_type::total on every
// parked waiter's slot (the residual ones on next_drain_head_).
//
// Verify (RC-A / Codex C-P1-3):
//   (a) every cancelled waiter sees sync_lock_aborted — NOT silently swallowed
//       (v1.0 incorrectly CAS'd residual waiters to `draining`, losing cancellation);
//   (b) the granted waiter's eventual unlock() walks next_drain_head_, observes
//       phase_ == cancelled for all, skips them, CASes state_ to not_locked;
//   (c) ASan/TSan-clean.
//
// Oracle: [2f §9 #22] — "residual-chain cancellation under graceful close" (RC-A).
// [2f §4.5.1 window 4]: residual-cancellation race (parked on next_drain_head_).
// SC-002: TSan-clean.

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
// Main: 8 waiters queued; first granted; remaining 7 on next_drain_head_;
// all residual waiters are cancelled while parked.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamResidualCancelGraceful, SevenResidualWaitersCancelledWhileParked) {
    constexpr int N = 8;

    // N cancellation signals — one per waiter.
    std::vector<asio::cancellation_signal> sigs(N);

    std::atomic<int> granted_count{0};
    std::atomic<int> aborted_count{0};
    std::atomic<int> total_completed{0};

    // Which waiter index holds the lock after the initial grant.
    std::atomic<int> lock_holder_idx{-1};

    asio::io_context ioc;
    async_mutex mtx;

    // Initial holder: holds until all N waiters have pushed onto the LIFO,
    // then releases (no cancellation signals here).
    auto initial_holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        // Let all N waiters push.
        co_await yield_n(N * 4);
        // unlock() here → first queued waiter granted; remaining spliced to
        // next_drain_head_.
    };

    // Each waiter:
    // - If granted: fires cancel on all OTHER waiters (the residual ones on
    //   next_drain_head_), then yields and releases.
    // - If cancelled: records sync_lock_aborted.
    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        // One yield so initial_holder acquires first.
        co_await yield_n(1);

        auto r = co_await mtx.async_lock();

        if (r.has_value()) {
            // Granted — we are now the holder.
            granted_count.fetch_add(1, std::memory_order_acq_rel);
            lock_holder_idx.store(idx, std::memory_order_release);

            // Yield to ensure residual waiters are on next_drain_head_.
            co_await yield_n(4);

            // Fire cancel on all OTHER waiters (they should all be residual).
            for (int i = 0; i < N; ++i) {
                if (i != idx) {
                    sigs[i].emit(asio::cancellation_type::total);
                }
            }

            // Yield to let cancel handlers run and process.
            co_await yield_n(N * 2);

            // Release — unlock() walks next_drain_head_, observes cancelled,
            // skips all, CASes state_ → not_locked.
        } else {
            // Residual waiter — must see sync_lock_aborted.
            // This is the PRIMARY RC-A assertion: cancellation on
            // next_drain_head_ must NOT be silently swallowed.
            EXPECT_EQ(r.error(), error::sync_lock_aborted)
                << "Residual waiter " << idx
                << " must see sync_lock_aborted (RC-A / Codex C-P1-3)";
            aborted_count.fetch_add(1, std::memory_order_acq_rel);
        }
        total_completed.fetch_add(1, std::memory_order_acq_rel);
    };

    auto fh = asio::co_spawn(ioc, initial_holder(), asio::use_future);
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

    EXPECT_EQ(granted_count.load(), 1)
        << "Exactly one waiter must be granted";
    EXPECT_EQ(aborted_count.load(), N - 1)
        << "All residual waiters must see sync_lock_aborted "
           "(RC-A: cancellation on next_drain_head_ must not be swallowed)";
    EXPECT_EQ(total_completed.load(), N)
        << "All N waiters must complete exactly once (no double-resume)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: cancel fired BEFORE the holder releases (races with drain CAS).
// Both outcomes (granted or aborted) are valid; total must still equal N.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamResidualCancelGraceful, CancelBeforeHolderReleasesRaceWithDrain) {
    constexpr int N = 8;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> total_completed{0};
    std::atomic<int> granted_count{0};
    std::atomic<int> aborted_count{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 3);
        // Fire ALL cancellations while still holding — races with the
        // upcoming drain walk.
        for (int i = 0; i < N; ++i)
            sigs[i].emit(asio::cancellation_type::total);
        co_await yield_n(2);
        // unlock here.
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
// Test 3: after all residual waiters cancelled, new acquirer succeeds.
// Verifies: mutex is free (state_ == not_locked); destructor safe.
// ─────────────────────────────────────────────────────────────────────────────

TEST(SeamResidualCancelGraceful, MutexFreeAfterAllResidualsCancelled) {
    constexpr int N = 4;

    std::vector<asio::cancellation_signal> sigs(N);
    std::atomic<int> aborted{0};

    asio::io_context ioc;
    async_mutex mtx;

    auto holder = [&]() -> asio::awaitable<void> {
        auto g = co_await mtx.async_lock();
        EXPECT_TRUE(g.has_value());
        co_await yield_n(N * 3);
        for (int i = 0; i < N; ++i)
            sigs[i].emit(asio::cancellation_type::total);
        co_await yield_n(N * 2);
        // unlock.
    };

    auto make_waiter = [&](int idx) -> asio::awaitable<void> {
        co_await yield_n(1);
        auto r = co_await mtx.async_lock();
        if (!r.has_value()) aborted.fetch_add(1, std::memory_order_acq_rel);
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

    // After all waiters complete (however resolved), a fresh acquire must work.
    bool ok = false;
    auto freshen = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        auto r = co_await mtx.async_lock();
        ok = r.has_value();
    }, asio::use_future);
    ioc.restart();  // io_context drained by the first run(); restart before re-running.
    ioc.run();
    freshen.get();

    EXPECT_TRUE(ok)
        << "After all residual waiters cancelled, mutex must be acquirable";
}

}  // namespace
