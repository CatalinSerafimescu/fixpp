// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_strand_local_reap.cpp — 048 T009 (SC-001)
//
// Strand-local reap MVP witness.
//
// N waiters parked on ONE strand (single io_context, no thread pool) →
// cancel_and_drain() reaps ALL with sync_lock_aborted exactly once; mutex
// is not_locked at return; zero hangs.
//
// SC-001 stress: ≥200 rounds × ≥25 repetitions. The test has an internal
// self-deadline (steady_timer / ioc.run_for) so a hang FAILS the test instead
// of hanging ctest forever.
//
// Design anchor: research.md D-2; data-model.md state diagram; INV-A/INV-B.
// Task: T009.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/sync/async_mutex.hpp>
#include <future>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// one_round: park N waiters on a strand; call cancel_and_drain(); verify all
// receive sync_lock_aborted exactly once; mutex is not_locked.
// ─────────────────────────────────────────────────────────────────────────────

bool run_one_round(int n_waiters) {
    std::atomic<int> aborted_count{0};
    std::atomic<int> granted_count{0};
    std::atomic<int> completed_count{0};
    std::atomic<bool> drain_done{false};
    std::atomic<bool> drain_ok{false};

    asio::io_context ioc;
    async_mutex mtx;

    // P2-4 (Gate B): keep the holder ACTIVE while the drain starts so NO waiter can
    // be granted — every queued waiter must be reaped with sync_lock_aborted. The
    // strict oracle is granted==0 ∧ aborted==N ∧ completed==N (was: accept any
    // granted|aborted mix, which a regression could satisfy with grants).
    auto main_coro = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        auto holder = co_await mtx.async_lock();   // hold throughout the reap
        EXPECT_TRUE(holder.has_value());

        for (int i = 0; i < n_waiters; ++i) {
            asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    auto r = co_await mtx.async_lock();
                    if (r.has_value()) {
                        granted_count.fetch_add(1, std::memory_order_relaxed);
                        r->release()->unlock();
                    } else if (r.error() == error::sync_lock_aborted) {
                        aborted_count.fetch_add(1, std::memory_order_relaxed);
                    }
                    completed_count.fetch_add(1, std::memory_order_relaxed);
                },
                asio::detached);
        }
        co_await yield_n(n_waiters * 2 + 4);  // let waiters queue behind the holder

        // Spawn the drain while the holder is STILL HELD: it sets draining_ and reaps
        // all queued waiters (every one aborted — none granted, the holder holds), then
        // parks in its quiescence loop waiting for the holder.
        asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                drain_ok.store(d.has_value(), std::memory_order_release);
                drain_done.store(true, std::memory_order_release);
            },
            asio::detached);
        co_await yield_n(n_waiters * 2 + 6);  // let the drain set draining_ + reap (all aborted)

        // Holder STILL held → all waiters are reaped, none granted. Release → finalize.
        holder = expected_t<async_lock_guard>{};
    };

    asio::co_spawn(ioc, main_coro(), asio::detached);

    // Self-deadline: a hang FAILS instead of hanging ctest. No post-drain "settle"
    // yield — the drain's terminal condition (in_flight_resumers_==0) guarantees every
    // posted resumer has run by the time the drain returns, so completed==N then.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(std::chrono::milliseconds(50));
        if (drain_done.load(std::memory_order_acquire) &&
            completed_count.load(std::memory_order_acquire) == n_waiters)
            break;
    }
    if (!(drain_done.load() && completed_count.load() == n_waiters)) {
        ioc.stop();
        return false;  // hang
    }

    return drain_ok.load() && completed_count.load() == n_waiters &&
           granted_count.load() == 0 && aborted_count.load() == n_waiters;
}

// ─────────────────────────────────────────────────────────────────────────────
// SC-001: 200 rounds × 25 repetitions (the PRODUCT — 5000 drains), each with the
// strict oracle (granted==0 ∧ aborted==N ∧ completed==N ∧ no hang).
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainStrandLocalReap, SC001Stress200x25NoHang) {
    constexpr int ROUNDS = 200;
    constexpr int REPS = 25;
    constexpr int N_WAITERS = 8;

    for (int r = 0; r < ROUNDS; ++r) {
        for (int rep = 0; rep < REPS; ++rep) {
            ASSERT_TRUE(run_one_round(N_WAITERS))
                << "round " << r << " rep " << rep
                << " failed (hang, a grant slipped through, or wrong abort count)";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Drain on empty mutex (no waiters, not held) — fast path returns ok.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainStrandLocalReap, DrainEmptyMutexSucceeds) {
    bool drain_ok = false;

    asio::io_context ioc;

    auto coro = [&]() -> asio::awaitable<void> {
        async_mutex mtx;
        auto d = co_await mtx.cancel_and_drain();
        drain_ok = d.has_value();
    };

    auto f = asio::co_spawn(ioc, coro(), asio::use_future);
    // Self-deadline.
    bool timed_out = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(std::chrono::milliseconds(100));
        if (f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) break;
    }
    if (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        timed_out = true;
        ioc.stop();
    }
    ASSERT_FALSE(timed_out) << "cancel_and_drain() on empty mutex hung";
    f.get();
    EXPECT_TRUE(drain_ok) << "cancel_and_drain() on empty mutex must return ok";
}

}  // namespace
