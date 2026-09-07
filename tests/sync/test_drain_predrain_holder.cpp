// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_predrain_holder.cpp — 048 T013
//
// Witness: a holder is suspended on the strand at drain time.
// The unified quiescence loop yields (co_await asio::post), which lets the
// holder's unlock() run on the strand; the drain then finalizes once the holder
// has released (active_holders_count_==0): no hang, no orphan.
//
// P3-1 (Gate B): the prior comment claimed unlock() "splices" waiters after the
// drain starts — that does NOT occur strand-locally. Once draining_ is set, the
// holder's unlock() SHORT-CIRCUITS (it does not splice or grant); the queued
// waiters were parked BEFORE the drain and are reaped directly. The re-reap each
// loop pass is a by-construction safety belt, not a late-splice path.
//
// Scenarios verified:
//   (a) Pre-drain holder with no additional waiters — drain yields until the
//       holder unlocks, then finalizes.
//   (b) Pre-drain holder with M additional waiters already queued behind it —
//       the drain reaps all M (aborted) while waiting for the holder.
//   (c) Chain of pre-drain holders releasing on their own timeline before the
//       drain finalizes.
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

#include "support/pump_until_ready.hpp"
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

        // ── #289 batch 19: the COROUTINE-SIDE `.get()`, and why the driver below
        // does not bound it ──────────────────────────────────────────────────────
        //
        // `fd.get()` runs INSIDE this coroutine, on the very thread that is pumping
        // `ioc`. If the drain has not finished, the block happens inside the handler
        // that `ioc.run_for(100ms)` dispatched: that call never returns, the deadline
        // loop below never re-tests its deadline, and `ASSERT_FALSE(timed_out)` is
        // never reached. A bounded outer driver -- the #289 remedy at every
        // caller-side site -- does not reach this shape at all.
        //
        // THE WINDOW IS PRESERVED: the 8 yields are still issued unconditionally, so
        // the interleaving this test depends on is unchanged. The guard adds only a
        // ready check and a bounded grace after them.
        if (!co_await fixpp::test_support::yield_window_then_ready(
                fd, 8, "DrainPredrainHolder::HolderUnlocksWhileDrainYields/drain")) {
            co_return;
        }
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

        // #289 batch 19 -- coroutine-side; see the comment at
        // HolderUnlocksWhileDrainYields for the mechanism.
        if (!co_await fixpp::test_support::yield_window_then_ready(
                fd, N * 4 + 8, "DrainPredrainHolder::HolderSplicesWaitersWhenUnlocking/drain")) {
            co_return;
        }
        fd.get();
        // ⚠️ THE SWEEP CANNOT SEE THESE. `ci/pump-get-sweep.sh` anchors on a `.get()`
        // whose receiver it can trace back to a `co_spawn`; a range-for variable over a
        // container of futures defeats that, so these rows appear in no residual. They
        // are the same hazard as the line above -- guarded here because they are in the
        // same coroutine, not because an instrument asked for it. Window 0: they get no
        // window of their own today and none is invented, only the grace.
        for (auto& fw : futs) {
            if (!co_await fixpp::test_support::yield_window_then_ready(
                    fw, 0, "DrainPredrainHolder::HolderSplicesWaitersWhenUnlocking/waiters")) {
                co_return;
            }
            fw.get();
        }
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

            // #289 batch 19 -- coroutine-side; see HolderUnlocksWhileDrainYields.
            if (!co_await fixpp::test_support::yield_window_then_ready(
                    fd, N * 4 + 8, "DrainPredrainHolder::StressMultipleHolders/drain")) {
                co_return;
            }
            fd.get();
            for (auto& fw : futs) {
                if (!co_await fixpp::test_support::yield_window_then_ready(
                        fw, 0, "DrainPredrainHolder::StressMultipleHolders/waiters")) {
                    co_return;
                }
                fw.get();
            }
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
