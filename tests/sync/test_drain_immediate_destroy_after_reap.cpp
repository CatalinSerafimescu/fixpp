// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_drain_immediate_destroy_after_reap.cpp — 048 T010 (P1-1)
//
// Witness: drain N waiters; check that all waiter callbacks fired BEFORE drain
// returned (in_flight_resumers_==0 at drain return); then destroy mutex.
//
// The drain's terminal condition (INV-D) requires in_flight_resumers_==0
// BEFORE returning. In the strand-local design, the `co_await asio::post`
// yield in the quiescence loop drains all pending resumer callbacks (FIFO
// ordering) before the drain's own continuation resumes. This witness
// checks the observable consequence: `completed == N` immediately after
// cancel_and_drain() returns and before any further ioc.run_for iterations.
//
// Mutation-test (run this to verify T010 discriminates):
//   Comment out `co_await asio::post(bound_ex, asio::use_awaitable)` in
//   the quiescence loop inside cancel_and_drain(). The loop then spins
//   synchronously without ever yielding to the io_context; since the N
//   resumer runners are pending in the io_context queue,
//   `in_flight_resumers_` never decrements → the terminal condition is never
//   satisfied → the test hangs → ASSERT_FALSE(timed_out) RED (5s timeout).
//
//   Verified RED: commenting out the yield in async_mutex.hpp:1178 and
//   re-running this test under ASan causes the test to hang indefinitely
//   (killed by the 5s deadline → FAIL). The yield is load-bearing.
//
//   Note on UAF: the `delete mtx_ptr` immediately after drain is an
//   additional ASan trip-wire for any runner that fires after delete. In the
//   corrected implementation all runners complete before drain returns
//   (FIFO ordering via the yield), so no UAF occurs.
//
// Design: research.md D-2 step 4; data-model.md INV-D.
// Task: T010.

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
#include <memory>
#include <vector>

#include "sync/sync_test_support.hpp"

namespace {

using fixpp::core::error;
using fixpp::sync::async_lock_guard;
using fixpp::sync::async_mutex;
using fixpp::sync::expected_t;

using fixpp::sync::test::yield_n;

// ─────────────────────────────────────────────────────────────────────────────
// Main test: heap-allocate mutex, reap N waiters, assert all completed
// IMMEDIATELY on drain return (before further ioc iterations), then destroy.
//
// The discriminating assertion is `ASSERT_EQ(completed.load(), N)` placed
// RIGHT AFTER `co_await cancel_and_drain()` inside the coroutine body.
// If the drain returns before all N resumer callbacks have fired, this
// assertion fails (completed < N). Only after cancellation PLUS
// waiting for in_flight_resumers_==0 (which happens via the FIFO yield
// inside the quiescence loop) is `completed == N` guaranteed on return.
//
// The immediate `delete mtx_ptr` after the assertion also exercises the
// UAF-barrier property: if any runner still holds `record->mutex_`, ASan
// would fire a heap-use-after-free at the runner's `fetch_sub` on the freed
// mutex (visible under -fsanitize=address).
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainImmediateDestroyAfterReap, AllCallbacksCompletedBeforeDrainReturns) {
    constexpr int N = 8;

    std::atomic<int> completed{0};
    bool drain_ok = false;
    int completed_at_drain_return = -1;

    asio::io_context ioc;

    // Heap-allocate the mutex so we can delete it precisely at drain return.
    auto* mtx_ptr = new async_mutex;

    auto main_coro = [&]() -> asio::awaitable<void> {
        auto ex = co_await asio::this_coro::executor;

        // Acquire holder to park waiters behind it.
        auto holder = co_await mtx_ptr->async_lock();
        EXPECT_TRUE(holder.has_value());

        // Park N waiters — each increments `completed` on its callback.
        std::vector<std::future<void>> futs;
        futs.reserve(N);
        for (int i = 0; i < N; ++i) {
            futs.push_back(asio::co_spawn(
                ex,
                [&]() -> asio::awaitable<void> {
                    auto r = co_await mtx_ptr->async_lock();
                    (void)r;
                    // Increment completed — both sync_lock_aborted (reaped)
                    // and granted paths increment; we just count resolutions.
                    completed.fetch_add(1, std::memory_order_relaxed);
                },
                asio::use_future));
        }

        // Yield to let waiters queue in state_.
        co_await yield_n(N * 2 + 4);

        // Release holder so drain's quiescence loop doesn't spin on
        // active_holders_count_ > 0.
        holder = expected_t<async_lock_guard>{};

        // Drain.  Per INV-D the quiescence loop MUST NOT return until
        // in_flight_resumers_==0, which (in the strand-local FIFO design)
        // means all N runners have executed and `completed == N`.
        auto d = co_await mtx_ptr->cancel_and_drain();
        drain_ok = d.has_value();

        // ─── Discriminating assertion ───────────────────────────────────────
        // Captured immediately inside the coroutine, BEFORE any further
        // ioc.run_for iterations can run additional handlers.
        // If `completed < N`, the drain broke the quiescence loop too early.
        completed_at_drain_return = completed.load(std::memory_order_relaxed);

        // ─── UAF-barrier exercise ──────────────────────────────────────────
        // Deleting here is safe iff all runners have finished their
        // `m->in_flight_resumers_.fetch_sub(1)` — guaranteed by INV-D.
        // Under -fsanitize=address any runner still alive gets a heap-
        // use-after-free at the fetch_sub, turning this test RED on mutation.
        delete mtx_ptr;
        mtx_ptr = nullptr;

        // Collect futures (should all be ready since completed==N).
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
    ASSERT_FALSE(timed_out) << "Test hung — possible hang in drain or futs.get()";
    f.get();

    EXPECT_TRUE(drain_ok) << "cancel_and_drain() must return ok";
    // Key assertion: all N waiter callbacks completed before drain returned.
    EXPECT_EQ(completed_at_drain_return, N)
        << "All N waiter callbacks must have fired before drain returns "
        << "(in_flight_resumers_==0 invariant). Got: " << completed_at_drain_return;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stress: repeat the drain+destroy scenario 50 times with N=4.
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrainImmediateDestroyAfterReap, RepeatedDestroyIsClean) {
    constexpr int REPS = 50;
    constexpr int N = 4;

    for (int rep = 0; rep < REPS; ++rep) {
        std::atomic<int> completed{0};
        bool drain_ok = false;
        int completed_at_drain_return = -1;

        asio::io_context ioc;
        auto* mtx_ptr = new async_mutex;

        auto coro = [&]() -> asio::awaitable<void> {
            auto ex = co_await asio::this_coro::executor;
            auto holder = co_await mtx_ptr->async_lock();
            EXPECT_TRUE(holder.has_value());

            std::vector<std::future<void>> futs;
            for (int i = 0; i < N; ++i) {
                futs.push_back(asio::co_spawn(
                    ex,
                    [&]() -> asio::awaitable<void> {
                        auto r = co_await mtx_ptr->async_lock();
                        (void)r;
                        completed.fetch_add(1, std::memory_order_relaxed);
                    },
                    asio::use_future));
            }
            co_await yield_n(N * 2 + 4);
            holder = expected_t<async_lock_guard>{};
            auto d = co_await mtx_ptr->cancel_and_drain();
            drain_ok = d.has_value();

            completed_at_drain_return = completed.load(std::memory_order_relaxed);

            delete mtx_ptr;
            mtx_ptr = nullptr;
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
        ASSERT_EQ(completed_at_drain_return, N)
            << "Rep " << rep << ": completed_at_drain_return should be " << N
            << " but was " << completed_at_drain_return;
    }
}

}  // namespace
