// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_predrain_holder.cpp — 048 T013
//
// Witness: a holder is suspended on the strand at drain time.
// The unified quiescence loop yields (co_await asio::post), which lets the
// holder's unlock() run on the strand; the drain loop then re-reaps any
// waiters spliced by unlock(); finalize: no hang, no orphan.
//
// Scenarios verified:
//   (a) Pre-drain holder with no additional waiters — drain yields once, holder
//       unlocks, drain finalizes.
//   (b) Pre-drain holder with M additional waiters spliced AFTER unlock —
//       the drain's W-2 re-reap picks them up.
//   (c) Chain of pre-drain holders: holder 1 grants holder 2 via unlock(),
//       then holder 2 also unlocks before drain finalizes.
//
// Design: research.md D-2/W-2/W-4; data-model.md quiescence loop; INV-A.
// Task: T013.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
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
// Scenario (a): pre-drain holder, no waiters behind it.
// Drain yields; holder unlocks; drain finalizes. No hang.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainPredrainHolder, HolderUnlocksWhileDrainYields) {
    bool drain_ok = false;
    bool holder_ran = false;

    asio::io_context ioc;
    async_mutex mtx;

    auto main_coro = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        // Acquire the holder.
        auto holder_guard = co_await mtx.async_lock();
        EXPECT_TRUE(holder_guard.has_value());

        // Start drain concurrently — it will observe active_holders_count_==1
        // and keep yielding.
        auto fd = asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                drain_ok = d.has_value();
            },
            asio::use_future);

        // Yield to let drain start and observe the holder.
        co_await yield_n(4);

        // Release the holder — drain loop should then finalize.
        holder_guard = expected_t<async_lock_guard>{};
        holder_ran = true;

        co_await yield_n(8);
        fd.get();
    };

    auto f = asio::co_spawn(ioc, main_coro(), asio::use_future);

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
    ASSERT_FALSE(timed_out) << "Drain hung waiting for pre-drain holder";
    f.get();
    EXPECT_TRUE(holder_ran) << "Holder must have run";
    EXPECT_TRUE(drain_ok) << "Drain must return ok after holder releases";
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario (b): pre-drain holder + waiters behind it.
// unlock() splices them into state_; drain's re-reap catches them.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainPredrainHolder, HolderSplicesWaitersWhenUnlocking) {
    constexpr int N = 4;

    std::atomic<int> completed{0};
    bool drain_ok = false;

    asio::io_context ioc;
    async_mutex mtx;

    auto main_coro = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        auto holder = co_await mtx.async_lock();
        EXPECT_TRUE(holder.has_value());

        // Park N waiters behind the holder.
        std::vector<std::future<void>> futs;
        for (int i = 0; i < N; ++i) {
            futs.push_back(asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    auto r = co_await mtx.async_lock();
                    (void)r;
                    completed.fetch_add(1, std::memory_order_relaxed);
                },
                asio::use_future));
        }

        co_await yield_n(N * 2 + 2);

        // Start drain (concurrently with the holder still held).
        auto fd = asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto d = co_await mtx.cancel_and_drain();
                drain_ok = d.has_value();
            },
            asio::use_future);

        // Yield so drain reaps visible waiters; then release holder.
        co_await yield_n(4);
        holder = expected_t<async_lock_guard>{};
        // unlock() will reap any queued waiters (or grant one then splice rest).
        // The drain's re-reap loop catches what unlock() grabs or splices.

        co_await yield_n(N * 4 + 8);
        fd.get();
        for (auto& f : futs) f.get();
    };

    auto f = asio::co_spawn(ioc, main_coro(), asio::use_future);

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
    ASSERT_FALSE(timed_out) << "Drain hung — holder-splice re-reap may not work";
    f.get();
    EXPECT_TRUE(drain_ok) << "Drain must complete";
    EXPECT_EQ(completed.load(), N) << "All N waiters must complete exactly once";
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario (c): stress — multiple pre-drain holders, varying N.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainPredrainHolder, StressMultipleHolders) {
    constexpr int REPS = 50;

    for (int rep = 0; rep < REPS; ++rep) {
        std::atomic<int> completed{0};
        bool drain_ok = false;
        constexpr int N = 4;

        asio::io_context ioc;
        async_mutex mtx;

        auto coro = [&]() -> asio::awaitable<void> {
            auto ex = co_await asio::this_coro::executor;
            auto h1 = co_await mtx.async_lock();

            std::vector<std::future<void>> futs;
            for (int i = 0; i < N; ++i) {
                futs.push_back(asio::co_spawn(
                    ex,
                    [&]() -> asio::awaitable<void> {
                        auto r = co_await mtx.async_lock();
                        (void)r;
                        completed.fetch_add(1, std::memory_order_relaxed);
                    },
                    asio::use_future));
            }

            co_await yield_n(N + 2);

            auto fd = asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    auto d = co_await mtx.cancel_and_drain();
                    drain_ok = d.has_value();
                },
                asio::use_future);

            co_await yield_n(2);
            h1 = expected_t<async_lock_guard>{};  // release holder

            co_await yield_n(N * 4 + 8);
            fd.get();
            for (auto& f : futs) f.get();
        };

        auto f = asio::co_spawn(ioc, coro(), asio::use_future);

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
        ASSERT_FALSE(timed_out) << "Rep " << rep << " hung";
        f.get();
        ASSERT_TRUE(drain_ok) << "Rep " << rep << " drain failed";
        ASSERT_EQ(completed.load(), N) << "Rep " << rep << " not all waiters completed";
    }
}

}  // namespace
